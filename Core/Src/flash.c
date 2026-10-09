/**
 * @file    flash_store.c
 * @brief   Luu card_db va danh sach admin vao Flash noi (STM32F1, HAL)
 *
 * Bo cuc vung Flash:
 *   [Header 16 byte][Ban ghi the 48 byte x N][Ban ghi admin 12 byte x M]
 *
 * Ban ghi the (48 byte):
 *   0..3   card_id (little-endian)
 *   4      uid_len
 *   5      enabled (0/1)
 *   6..15  uid[10]
 *   16..47 name[32]
 *
 * Ban ghi admin (12 byte):
 *   0      uid_len
 *   1..10  uid[10]
 *   11     du phong
 *
 * Header duoc ghi SAU CUNG: neu mat dien giua chung, header con trong
 * -> lan khoi dong sau bao "khong co du lieu" chu khong nap du lieu rac.
 */
#include "flash.h"
#include "card_db.h"
#include "admin.h"
#include <string.h>

#define FS_MAGIC           0x42444652u      /* "RFDB" */
#define FS_VERSION         1u

#define FS_HEADER_SIZE     16u
#define FS_CARD_REC_SIZE   48u
#define FS_ADMIN_REC_SIZE  12u

#define FS_OFF_ID          0u
#define FS_OFF_UID_LEN     4u
#define FS_OFF_ENABLED     5u
#define FS_OFF_UID         6u
#define FS_OFF_NAME        (FS_OFF_UID + CARD_UID_MAX_LEN)

#define FS_DATA_ADDR       (FLASH_STORE_ADDR + FS_HEADER_SIZE)
#define FS_MAX_DATA        (CARD_DB_MAX_ITEMS * FS_CARD_REC_SIZE + ADMIN_MAX_CARDS * FS_ADMIN_REC_SIZE)

/* ---- Kiem tra cau hinh luc bien dich ---- */
#if (FLASH_STORE_SIZE % FLASH_PAGE_SIZE) != 0
#error "FLASH_STORE_SIZE phai la boi so cua FLASH_PAGE_SIZE"
#endif
#if (FLASH_STORE_ADDR % FLASH_PAGE_SIZE) != 0
#error "FLASH_STORE_ADDR phai nam dau mot page"
#endif
_Static_assert(FS_HEADER_SIZE + FS_MAX_DATA <= FLASH_STORE_SIZE, "Vung Flash qua nho cho du lieu");
_Static_assert(FS_OFF_NAME + CARD_NAME_LEN <= FS_CARD_REC_SIZE, "Ban ghi the qua nho");
_Static_assert(1u + CARD_UID_MAX_LEN <= FS_ADMIN_REC_SIZE, "Ban ghi admin qua nho");
_Static_assert(CARD_DB_MAX_ITEMS <= 255u, "card_count luu bang uint8_t");

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint8_t  card_count;
    uint8_t  admin_count;
    uint32_t crc;         /* CRC32 cua [card_count, admin_count] + vung du lieu */
    uint32_t reserved;
} FsHeader;

_Static_assert(sizeof(FsHeader) == FS_HEADER_SIZE, "Sai kich thuoc header");

/* ========================== Ham noi bo ========================== */

/* CRC32 (da thuc 0xEDB88320), goi noi tiep duoc: crc = crc32_update(crc, ...) */
static uint32_t crc32_update(uint32_t crc, const uint8_t *p, uint32_t n)
{
    crc = ~crc;
    while (n--)
    {
        crc ^= *p++;
        for (uint8_t k = 0; k < 8u; k++)
        {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

static void pack_card(const CardEntry *e, uint8_t rec[FS_CARD_REC_SIZE])
{
    memset(rec, 0, FS_CARD_REC_SIZE);
    rec[FS_OFF_ID + 0] = (uint8_t)(e->card_id);
    rec[FS_OFF_ID + 1] = (uint8_t)(e->card_id >> 8);
    rec[FS_OFF_ID + 2] = (uint8_t)(e->card_id >> 16);
    rec[FS_OFF_ID + 3] = (uint8_t)(e->card_id >> 24);
    rec[FS_OFF_UID_LEN] = e->uid_len;
    rec[FS_OFF_ENABLED] = e->enabled ? 1u : 0u;
    memcpy(&rec[FS_OFF_UID], e->uid, CARD_UID_MAX_LEN);
    memcpy(&rec[FS_OFF_NAME], e->name, CARD_NAME_LEN);
    rec[FS_OFF_NAME + CARD_NAME_LEN - 1u] = '\0';
}

static void pack_admin(uint8_t index, uint8_t rec[FS_ADMIN_REC_SIZE])
{
    const uint8_t *uid = NULL;
    uint8_t len = 0;

    memset(rec, 0, FS_ADMIN_REC_SIZE);
    if (admin_get_at(index, &uid, &len))
    {
        rec[0] = len;
        memcpy(&rec[1], uid, len);
    }
}

/* CRC cua du lieu dang co trong RAM (dung cung cach dong goi nhu khi ghi) */
static uint32_t ram_crc(uint8_t card_count, uint8_t admin_count)
{
    uint8_t rec[FS_CARD_REC_SIZE];
    uint8_t counts[2] = {card_count, admin_count};
    uint32_t crc = crc32_update(0u, counts, 2u);

    for (uint8_t i = 0; i < card_count; i++)
    {
        pack_card(card_db_get_at(i), rec);
        crc = crc32_update(crc, rec, FS_CARD_REC_SIZE);
    }
    for (uint8_t i = 0; i < admin_count; i++)
    {
        pack_admin(i, rec);
        crc = crc32_update(crc, rec, FS_ADMIN_REC_SIZE);
    }
    return crc;
}

/* Kiem tra header va CRC cua du lieu dang nam trong Flash */
static const FsHeader *flash_header_valid(void)
{
    const FsHeader *h = (const FsHeader *)FLASH_STORE_ADDR;

    if ((h->magic != FS_MAGIC) || (h->version != FS_VERSION)) return NULL;
    if ((h->card_count > CARD_DB_MAX_ITEMS) || (h->admin_count > ADMIN_MAX_CARDS)) return NULL;

    uint8_t counts[2] = {h->card_count, h->admin_count};
    uint32_t len = (uint32_t)h->card_count * FS_CARD_REC_SIZE
                 + (uint32_t)h->admin_count * FS_ADMIN_REC_SIZE;
    uint32_t crc = crc32_update(0u, counts, 2u);
    crc = crc32_update(crc, (const uint8_t *)FS_DATA_ADDR, len);

    return (crc == h->crc) ? h : NULL;
}

static bool flash_erase_area(void)
{
    FLASH_EraseInitTypeDef er = {0};
    uint32_t page_error = 0;

    er.TypeErase   = FLASH_TYPEERASE_PAGES;
    er.Banks       = FLASH_BANK_1;
    er.PageAddress = FLASH_STORE_ADDR;
    er.NbPages     = FLASH_STORE_SIZE / FLASH_PAGE_SIZE;
    return HAL_FLASHEx_Erase(&er, &page_error) == HAL_OK;
}

/* Ghi theo half-word (STM32F1 bat buoc). len phai chan. */
static bool flash_program(uint32_t addr, const uint8_t *data, uint32_t len)
{
    for (uint32_t i = 0; i + 1u < len; i += 2u)
    {
        uint16_t hw = (uint16_t)(data[i] | ((uint16_t)data[i + 1u] << 8));
        if (hw == 0xFFFFu) continue;                 /* o nho da xoa san la 0xFFFF */
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, addr + i, hw) != HAL_OK)
        {
            return false;
        }
    }
    return true;
}

/* ========================== API ========================== */

bool flash_store_has_data(void)
{
    return flash_header_valid() != NULL;
}

bool flash_store_load(void)
{
    const FsHeader *h = flash_header_valid();
    if (h == NULL) return false;

    const uint8_t *p = (const uint8_t *)FS_DATA_ADDR;
    char name[CARD_NAME_LEN];

    card_db_init();
    admin_clear();

    for (uint8_t i = 0; i < h->card_count; i++, p += FS_CARD_REC_SIZE)
    {
        uint32_t id = (uint32_t)p[FS_OFF_ID]
                    | ((uint32_t)p[FS_OFF_ID + 1] << 8)
                    | ((uint32_t)p[FS_OFF_ID + 2] << 16)
                    | ((uint32_t)p[FS_OFF_ID + 3] << 24);
        memcpy(name, &p[FS_OFF_NAME], CARD_NAME_LEN);
        name[CARD_NAME_LEN - 1u] = '\0';
        (void)card_db_add(id, &p[FS_OFF_UID], p[FS_OFF_UID_LEN], name, p[FS_OFF_ENABLED] != 0u);
    }
    for (uint8_t i = 0; i < h->admin_count; i++, p += FS_ADMIN_REC_SIZE)
    {
        (void)admin_add_uid(&p[1], p[0]);
    }
    return true;
}

bool flash_store_save(void)
{
    uint8_t card_count  = card_db_count();
    uint8_t admin_count = admin_get_count();
    uint32_t crc = ram_crc(card_count, admin_count);

    /* Khong co gi thay doi -> khong ghi */
    const FsHeader *old = flash_header_valid();
    if ((old != NULL) && (old->crc == crc) &&
        (old->card_count == card_count) && (old->admin_count == admin_count))
    {
        return true;
    }

    uint8_t rec[FS_CARD_REC_SIZE];
    uint32_t addr = FS_DATA_ADDR;
    bool ok;

    HAL_FLASH_Unlock();
    ok = flash_erase_area();

    for (uint8_t i = 0; ok && (i < card_count); i++)
    {
        pack_card(card_db_get_at(i), rec);
        ok = flash_program(addr, rec, FS_CARD_REC_SIZE);
        addr += FS_CARD_REC_SIZE;
    }
    for (uint8_t i = 0; ok && (i < admin_count); i++)
    {
        pack_admin(i, rec);
        ok = flash_program(addr, rec, FS_ADMIN_REC_SIZE);
        addr += FS_ADMIN_REC_SIZE;
    }

    if (ok)   /* header ghi sau cung */
    {
        FsHeader h;
        uint8_t raw[FS_HEADER_SIZE];

        h.magic       = FS_MAGIC;
        h.version     = FS_VERSION;
        h.card_count  = card_count;
        h.admin_count = admin_count;
        h.crc         = crc;
        h.reserved    = 0xFFFFFFFFu;
        memcpy(raw, &h, sizeof(raw));
        ok = flash_program(FLASH_STORE_ADDR, raw, FS_HEADER_SIZE);
    }
    HAL_FLASH_Lock();

    /* Doc lai de xac nhan */
    return ok && (flash_header_valid() != NULL);
}

bool flash_store_erase(void)
{
    HAL_FLASH_Unlock();
    bool ok = flash_erase_area();
    HAL_FLASH_Lock();
    return ok;
}