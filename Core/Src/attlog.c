/**
 * @file    attlog.c
 * @brief   Nhat ky diem danh dang bo dem vong trong Flash noi STM32F1
 *
 * Ban ghi 16 byte:
 *   0..3   seq        (little-endian, ghi TRUOC TIEN)
 *   4..7   time       (giay tu 2000-01-01)
 *   8..11  card_id    (the la: 4 byte dau UID)
 *   12     result
 *   13     uid_len
 *   14..15 CRC16-CCITT cua byte 0..13
 *
 * Ghi tuan tu tung o. Khi con tro ghi cham dau mot page, page do (chua
 * du lieu cu nhat) bi xoa truoc khi ghi -> moi page chi bi xoa 1 lan cho
 * moi vong, mon deu tren tat ca cac page.
 * Mat dien giua chung: o do co CRC sai -> bi bo qua khi doc va khi tim
 * vi tri ghi tiep.
 */
#include "attlog.h"
#include "flash.h"
#include "rtc_clock.h"
#include <string.h>

#define REC_SIZE        16u
#define SLOTS           (ATTLOG_SIZE / REC_SIZE)
#define SLOTS_PER_PAGE  (FLASH_PAGE_SIZE / REC_SIZE)
#define PAGES           (ATTLOG_SIZE / FLASH_PAGE_SIZE)

#if (ATTLOG_SIZE % FLASH_PAGE_SIZE) != 0
#error "ATTLOG_SIZE phai la boi so cua FLASH_PAGE_SIZE"
#endif
#if (ATTLOG_ADDR % FLASH_PAGE_SIZE) != 0
#error "ATTLOG_ADDR phai nam dau mot page"
#endif
#if (ATTLOG_SIZE / FLASH_PAGE_SIZE) < 2
#error "Nhat ky can it nhat 2 page"
#endif
#if !((ATTLOG_ADDR + ATTLOG_SIZE <= FLASH_STORE_ADDR) || (ATTLOG_ADDR >= FLASH_STORE_ADDR + FLASH_STORE_SIZE))
#error "Vung nhat ky chong len vung flash_store"
#endif
_Static_assert(SLOTS <= 65535u, "Qua nhieu o");

static uint16_t s_next;      /* o se ghi tiep theo */
static uint32_t s_seq = 1u;  /* seq cho ban ghi tiep theo */
static uint16_t s_count;

/* ========================== Ham noi bo ========================== */

static uint32_t slot_addr(uint16_t i)
{
    return ATTLOG_ADDR + (uint32_t)i * REC_SIZE;
}

static void slot_read(uint16_t i, uint8_t b[REC_SIZE])
{
    memcpy(b, (const void *)(uintptr_t)slot_addr(i), REC_SIZE);
}

static bool is_blank(const uint8_t *b, uint32_t n)
{
    for (uint32_t k = 0; k < n; k++)
    {
        if (b[k] != 0xFFu) return false;
    }
    return true;
}

static bool slot_blank(uint16_t i)
{
    uint8_t b[REC_SIZE];
    slot_read(i, b);
    return is_blank(b, REC_SIZE);
}

static uint16_t crc16(const uint8_t *p, uint32_t n)
{
    uint16_t c = 0xFFFFu;
    while (n--)
    {
        c ^= (uint16_t)((uint16_t)(*p++) << 8);
        for (uint8_t k = 0; k < 8u; k++)
        {
            c = (c & 0x8000u) ? (uint16_t)((c << 1) ^ 0x1021u) : (uint16_t)(c << 1);
        }
    }
    return c;
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static void pack(const AttLogRecord *r, uint8_t b[REC_SIZE])
{
    wr32(&b[0], r->seq);
    wr32(&b[4], r->time);
    wr32(&b[8], r->card_id);
    b[12] = r->result;
    b[13] = r->uid_len;
    uint16_t c = crc16(b, 14u);
    b[14] = (uint8_t)c;
    b[15] = (uint8_t)(c >> 8);
}

static bool unpack(const uint8_t b[REC_SIZE], AttLogRecord *r)
{
    if (is_blank(b, REC_SIZE)) return false;
    if (crc16(b, 14u) != (uint16_t)(b[14] | ((uint16_t)b[15] << 8))) return false;

    r->seq     = rd32(&b[0]);
    r->time    = rd32(&b[4]);
    r->card_id = rd32(&b[8]);
    r->result  = b[12];
    r->uid_len = b[13];
    return (r->seq != 0xFFFFFFFFu) && (r->result <= (uint8_t)ATTLOG_UNKNOWN);
}

static bool slot_valid(uint16_t i, AttLogRecord *r)
{
    uint8_t b[REC_SIZE];
    slot_read(i, b);
    return unpack(b, r);
}

static uint16_t page_valid_count(uint16_t page)
{
    AttLogRecord r;
    uint16_t n = 0;
    for (uint16_t i = 0; i < SLOTS_PER_PAGE; i++)
    {
        if (slot_valid((uint16_t)(page * SLOTS_PER_PAGE + i), &r)) n++;
    }
    return n;
}

static bool page_blank(uint16_t page)
{
    return is_blank((const uint8_t *)(uintptr_t)slot_addr((uint16_t)(page * SLOTS_PER_PAGE)),
                    FLASH_PAGE_SIZE);
}

static bool erase_pages(uint16_t first_page, uint16_t n)
{
    FLASH_EraseInitTypeDef er = {0};
    uint32_t page_error = 0;
    HAL_StatusTypeDef st;

    er.TypeErase   = FLASH_TYPEERASE_PAGES;
    er.Banks       = FLASH_BANK_1;
    er.PageAddress = ATTLOG_ADDR + (uint32_t)first_page * FLASH_PAGE_SIZE;
    er.NbPages     = n;

    HAL_FLASH_Unlock();
    st = HAL_FLASHEx_Erase(&er, &page_error);
    HAL_FLASH_Lock();
    return st == HAL_OK;
}

static bool program_slot(uint16_t i, const uint8_t b[REC_SIZE])
{
    bool ok = true;
    uint32_t addr = slot_addr(i);

    HAL_FLASH_Unlock();
    for (uint32_t k = 0; ok && (k < REC_SIZE); k += 2u)   /* seq (byte 0..3) duoc ghi truoc */
    {
        uint16_t hw = (uint16_t)(b[k] | ((uint16_t)b[k + 1u] << 8));
        ok = (HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, addr + k, hw) == HAL_OK);
    }
    HAL_FLASH_Lock();
    return ok;
}

/* ========================== API ========================== */

void attlog_init(void)
{
    AttLogRecord r;
    uint32_t max_seq = 0;
    int32_t  max_idx = -1;
    bool     any_used = false;

    s_count = 0;
    for (uint16_t i = 0; i < SLOTS; i++)
    {
        if (slot_blank(i)) continue;
        any_used = true;
        if (slot_valid(i, &r))
        {
            s_count++;
            if ((max_idx < 0) || (r.seq > max_seq))
            {
                max_seq = r.seq;
                max_idx = i;
            }
        }
    }

    if (max_idx < 0)
    {
        /* Khong co ban ghi hop le nao: xoa sach neu vung nho co rac */
        if (any_used) (void)erase_pages(0, PAGES);
        s_next = 0;
        s_seq = 1u;
        s_count = 0;
        return;
    }

    s_seq = max_seq + 1u;
    uint16_t n = (uint16_t)((max_idx + 1) % SLOTS);
    /* Bo qua cac o hong (ghi do dang) ngay sau ban ghi moi nhat */
    for (uint16_t k = 0; (k < SLOTS) && ((n % SLOTS_PER_PAGE) != 0u) && !slot_blank(n); k++)
    {
        n = (uint16_t)((n + 1u) % SLOTS);
    }
    s_next = n;
}

bool attlog_append(AttLogResult result, uint32_t card_id, const uint8_t *uid, uint8_t uid_len)
{
    AttLogRecord r;
    uint8_t b[REC_SIZE];

    /* Den dau page: xoa page do (dang chua du lieu cu nhat) */
    if ((s_next % SLOTS_PER_PAGE) == 0u)
    {
        uint16_t page = (uint16_t)(s_next / SLOTS_PER_PAGE);
        if (!page_blank(page))
        {
            uint16_t lost = page_valid_count(page);
            if (!erase_pages(page, 1u)) return false;
            s_count = (s_count >= lost) ? (uint16_t)(s_count - lost) : 0u;
        }
    }

    if (result == ATTLOG_UNKNOWN)
    {
        card_id = 0;
        for (uint8_t i = 0; i < 4u; i++)
        {
            card_id <<= 8;
            if ((uid != NULL) && (i < uid_len)) card_id |= uid[i];
        }
    }

    r.seq     = s_seq;
    r.time    = rtc_clock_now();
    r.card_id = card_id;
    r.result  = (uint8_t)result;
    r.uid_len = uid_len;
    pack(&r, b);

    bool ok = program_slot(s_next, b);
    s_next = (uint16_t)((s_next + 1u) % SLOTS);   /* o loi cung bo qua */
    s_seq++;
    if (ok && (s_count < SLOTS)) s_count++;
    return ok;
}

uint16_t attlog_count(void)
{
    return s_count;
}

uint16_t attlog_capacity(void)
{
    return (uint16_t)(SLOTS - SLOTS_PER_PAGE);
}

void attlog_foreach(AttLogVisitor fn, void *ctx)
{
    AttLogRecord r;
    uint16_t start;

    if (fn == NULL) return;

    /* Ban ghi cu nhat nam o page ngay sau page dang ghi;
     * rieng khi con tro ghi o dau page thi chinh page do (chua bi xoa) la cu nhat. */
    if ((s_next % SLOTS_PER_PAGE) == 0u)
    {
        start = s_next;
    }
    else
    {
        start = (uint16_t)((((s_next / SLOTS_PER_PAGE) + 1u) % PAGES) * SLOTS_PER_PAGE);
    }

    for (uint16_t k = 0; k < SLOTS; k++)
    {
        uint16_t i = (uint16_t)((start + k) % SLOTS);
        if (slot_valid(i, &r)) fn(&r, ctx);
    }
}

bool attlog_clear(void)
{
    bool ok = erase_pages(0, PAGES);
    s_next = 0;
    s_seq = 1u;
    s_count = 0;
    return ok;
}

const char *attlog_result_str(uint8_t result)
{
    switch (result)
    {
    case ATTLOG_OK:      return "OK";
    case ATTLOG_AGAIN:   return "AGAIN";
    case ATTLOG_LOCKED:  return "LOCKED";
    case ATTLOG_UNKNOWN: return "UNKNOWN";
    default:             return "?";
    }
}