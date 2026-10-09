#ifndef CARD_DB_H
#define CARD_DB_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

/* [SUA] Bo cac macro trung lap: MAX_STUDENTS trung voi CARD_DB_MAX_ITEMS.
 *       Them #ifndef de chinh duoc tu ngoai. Macro chi dinh nghia O DAY, khong
 *       dinh nghia lai trong card_db.c. */
#ifndef CARD_UID_MAX_LEN
#define CARD_UID_MAX_LEN   10u
#endif
#ifndef CARD_NAME_LEN
#define CARD_NAME_LEN      32u
#endif
#ifndef CARD_DB_MAX_ITEMS
#define CARD_DB_MAX_ITEMS  32u
#endif
// [CU] #define MAX_STUDENTS 32

typedef struct {
    uint32_t card_id;                  /* ma sinh vien / ma the, duy nhat */
    uint8_t  uid[CARD_UID_MAX_LEN];    /* UID the RFID (4/7/10 byte)      */
    uint8_t  uid_len;
    char     name[CARD_NAME_LEN];      /* ho ten, toi da 31 ky tu         */
    bool     enabled;                  /* false = the bi khoa tam thoi    */
} CardEntry;



/* ---- Khoi tao ---- */
void    card_db_init(void);                 /* xoa sach danh sach trong RAM */
void    card_db_clear(void);

/* ---- Thong tin ---- */
uint8_t card_db_count(void);
bool    card_db_is_full(void);
/* [SUA] them: duyet danh sach (de in LIST / xuat CSV). NULL neu index sai. */
const CardEntry *card_db_get_at(uint8_t index);

/* ---- Tim kiem ----
 * [SUA] Tra con tro const: nguoi goi khong sua truc tiep uid/card_id duoc
 *       (sua truc tiep co the tao UID trung). Con tro chi hop le den lan
 *       them/xoa ke tiep vi mang bi dich chuyen khi xoa. */
const CardEntry *card_db_find_by_uid(const uint8_t *uid, uint8_t uid_len);
const CardEntry *card_db_find_by_id(uint32_t card_id);
bool    card_db_is_registered(const uint8_t *uid, uint8_t uid_len);
bool    card_db_is_enabled(uint32_t card_id);

/* ---- Them / xoa / sua ---- */
bool    card_db_add(uint32_t card_id, const uint8_t *uid, uint8_t uid_len,
                    const char *name, bool enabled);
bool    card_db_remove_by_uid(const uint8_t *uid, uint8_t uid_len);
bool    card_db_remove_by_id(uint32_t card_id);
bool    card_db_update_status(uint32_t card_id, bool enabled);
bool    card_db_update_name(uint32_t card_id, const char *name);

#ifdef __cplusplus
}
#endif

#endif /* CARD_DB_H */