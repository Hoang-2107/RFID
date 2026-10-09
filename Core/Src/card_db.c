#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "card_db.h"

/* [SUA] Bo dinh nghia lai macro: da co trong card_db.h. Neu 2 noi khac gia tri
 *       se gay sai lech kich thuoc mang giua cac file. */
// [CU] #define CARD_UID_MAX_LEN   10u
// [CU] #define CARD_NAME_LEN      32u
// [CU] #define MAX_STUDENTS 32
// [CU] #define CARD_DB_MAX_ITEMS  32u

/* [SUA] Bo mang Student khong dung (lang phi 3104 byte RAM). */
// [CU] static Student g_student_db[MAX_STUDENTS];

/* Co so du lieu the trong RAM */
static CardEntry g_card_db[CARD_DB_MAX_ITEMS];
/* So the hien co; cac the hop le nam o index 0 .. g_card_db_count-1 */
static uint8_t   g_card_db_count = 0u;

/* ========================== Ham noi bo ========================== */

static bool card_db_is_index_valid(uint8_t index)
{
    return (index < g_card_db_count);
}

static bool card_db_uid_len_valid(const uint8_t *uid, uint8_t uid_len)
{
    return (uid != NULL) && (uid_len > 0u) && (uid_len <= CARD_UID_MAX_LEN);
}

/* So sanh 2 UID: phai cung do dai va cung tung byte */
// [CU] // tìm kiếm sinh viên theo mã sinh viên     <- [SUA] comment sai chuc nang
static bool card_db_uid_is_equal(const uint8_t *left, uint8_t left_len,
                                 const uint8_t *right, uint8_t right_len)
{
    if ((left == NULL) || (right == NULL))
    {
        return false;
    }

    if (left_len != right_len)
    {
        return false;
    }

    return (memcmp(left, right, left_len) == 0);
}

/* [SUA] THEM MOI: tim index theo UID / id, dung chung cho find, remove, update */
static int card_db_index_by_uid(const uint8_t *uid, uint8_t uid_len)
{
    if (!card_db_uid_len_valid(uid, uid_len))
    {
        return -1;
    }
    for (uint8_t i = 0u; i < g_card_db_count; i++)
    {
        if (card_db_uid_is_equal(g_card_db[i].uid, g_card_db[i].uid_len, uid, uid_len))
        {
            return (int)i;
        }
    }
    return -1;
}

static int card_db_index_by_id(uint32_t card_id)
{
    for (uint8_t i = 0u; i < g_card_db_count; i++)
    {
        if (g_card_db[i].card_id == card_id)
        {
            return (int)i;
        }
    }
    return -1;
}

/* [SUA] THEM MOI: xoa phan tu tai index va don mang.
 *       Truoc day remove_by_uid va remove_by_id lap lai y het doan code nay;
 *       nay dung chung, va dung luon card_db_is_index_valid (truoc bi bo khong dung). */
static bool card_db_remove_at(uint8_t index)
{
    if (!card_db_is_index_valid(index))
    {
        return false;
    }
    for (uint8_t j = index; (uint8_t)(j + 1u) < g_card_db_count; j++)
    {
        g_card_db[j] = g_card_db[j + 1u];
    }
    memset(&g_card_db[g_card_db_count - 1u], 0, sizeof(g_card_db[0]));
    g_card_db_count--;
    return true;
}

static void card_db_copy_name(char *dst, const char *src)
{
    if (src != NULL)
    {
        strncpy(dst, src, CARD_NAME_LEN - 1u);
        dst[CARD_NAME_LEN - 1u] = '\0';
    }
    else
    {
        dst[0] = '\0';
    }
}

/* ========================== Khoi tao ========================== */

// [CU] // khởi tạo cơ  sở dữ liệu thẻ
/* Khoi tao co so du lieu the (xoa sach) */
void card_db_init(void)
{
    memset(g_card_db, 0, sizeof(g_card_db));
    g_card_db_count = 0u;
}

void card_db_clear(void)
{
    card_db_init();
}

/* ========================== Thong tin ========================== */

// [CU] // danh sách sinh viên có trong cơ sở      <- [SUA] comment sai chuc nang
/* Danh sach da day chua */
bool card_db_is_full(void)
{
    return (g_card_db_count >= CARD_DB_MAX_ITEMS);
}

/* So the hien co trong co so du lieu */
uint8_t card_db_count(void)
{
    return g_card_db_count;
}

/* [SUA] THEM MOI */
const CardEntry *card_db_get_at(uint8_t index)
{
    return card_db_is_index_valid(index) ? &g_card_db[index] : NULL;
}

/* ========================== Tim kiem ========================== */

/* Tim the theo UID */
// [CU] CardEntry *card_db_find_by_uid(const uint8_t *uid, uint8_t uid_len)
// [CU] { ... vong for tim kiem ... }
const CardEntry *card_db_find_by_uid(const uint8_t *uid, uint8_t uid_len)
{
    int i = card_db_index_by_uid(uid, uid_len);
    return (i >= 0) ? &g_card_db[i] : NULL;
}

/* Tim the theo ma the / ma sinh vien */
// [CU] CardEntry *card_db_find_by_id(uint32_t card_id)
const CardEntry *card_db_find_by_id(uint32_t card_id)
{
    int i = card_db_index_by_id(card_id);
    return (i >= 0) ? &g_card_db[i] : NULL;
}

bool card_db_is_registered(const uint8_t *uid, uint8_t uid_len)
{
    return (card_db_index_by_uid(uid, uid_len) >= 0);
}

bool card_db_is_enabled(uint32_t card_id)
{
    const CardEntry *entry = card_db_find_by_id(card_id);
    return (entry != NULL) && entry->enabled;
}

/* ========================== Them / xoa / sua ========================== */

/* Them the sinh vien. Tu choi khi: day, UID sai, UID trung, card_id trung. */
bool card_db_add(uint32_t card_id,
                 const uint8_t *uid,
                 uint8_t uid_len,
                 const char *name,
                 bool enabled)
{
    if (card_db_is_full())
    {
        return false;
    }

    // [CU] if ((uid == NULL) || (uid_len == 0u) || (uid_len > CARD_UID_MAX_LEN))
    if (!card_db_uid_len_valid(uid, uid_len))
    {
        return false;
    }

    if ((card_db_index_by_uid(uid, uid_len) >= 0) || (card_db_index_by_id(card_id) >= 0))
    {
        return false;
    }

    CardEntry *entry = &g_card_db[g_card_db_count];
    memset(entry, 0, sizeof(*entry));

    entry->card_id = card_id;
    entry->uid_len = uid_len;
    memcpy(entry->uid, uid, uid_len);
    entry->enabled = enabled;
    card_db_copy_name(entry->name, name);

    g_card_db_count++;
    return true;
}

bool card_db_remove_by_uid(const uint8_t *uid, uint8_t uid_len)
{
    // [CU] vong for tim + vong for dich mang (lap lai voi remove_by_id)
    int i = card_db_index_by_uid(uid, uid_len);
    return (i >= 0) && card_db_remove_at((uint8_t)i);
}

bool card_db_remove_by_id(uint32_t card_id)
{
    // [CU] vong for tim + vong for dich mang (lap lai voi remove_by_uid)
    int i = card_db_index_by_id(card_id);
    return (i >= 0) && card_db_remove_at((uint8_t)i);
}

bool card_db_update_status(uint32_t card_id, bool enabled)
{
    int i = card_db_index_by_id(card_id);
    if (i < 0)
    {
        return false;
    }
    g_card_db[i].enabled = enabled;
    return true;
}

bool card_db_update_name(uint32_t card_id, const char *name)
{
    int i = card_db_index_by_id(card_id);
    if ((i < 0) || (name == NULL))
    {
        return false;
    }
    card_db_copy_name(g_card_db[i].name, name);
    return true;
}