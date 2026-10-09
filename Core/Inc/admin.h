#ifndef ADMIN_H
#define ADMIN_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include "card_db.h"

#ifndef ADMIN_MAX_CARDS
#define ADMIN_MAX_CARDS      4u        /* so the admin toi da */
#endif
#ifndef ADMIN_TIMEOUT_MS
#define ADMIN_TIMEOUT_MS     30000u    /* khong thao tac 30 s -> tu thoat */
#endif
#ifndef ADMIN_FIRST_CARD_ID
#define ADMIN_FIRST_CARD_ID  1u        /* ma the dau tien khi them tu dong */
#endif

/* ---- Khoi tao: xoa danh sach admin, dua ve trang thai binh thuong ---- */
void    admin_init(void);

/* ---- Danh sach the admin (luu Flash cung voi card_db) ---- */
void    admin_clear(void);
uint8_t admin_get_count(void);
bool    admin_get_at(uint8_t index, const uint8_t **uid, uint8_t *uid_len);
bool    admin_is_admin_uid(const uint8_t *uid, uint8_t uid_len);
bool    admin_add_uid(const uint8_t *uid, uint8_t uid_len);
bool    admin_remove_uid(const uint8_t *uid, uint8_t uid_len);
void    admin_keep_alive(void);
/* ---- Che do quan tri ----
 * Cach dung trong vong lap chinh:
 *   - Goi admin_task() lien tuc. Tra ve true dung 1 lan khi vua thoat
 *     che do admin -> main ve lai man hinh cho.
 *   - Khi co the moi:
 *       admin_is_active()    -> admin_on_card(uid, len)
 *       admin_is_admin_uid() -> admin_enter()
 *       con lai              -> xu ly cham the binh thuong
 *   - Khi chua co the admin nao, nhan MENU o man hinh cho de vao che do
 *     cai dat va dang ky the admin dau tien.
 * Nut: NEXT = chon muc ke, OK = chon/xac nhan, MENU = quay lai/thoat. */
bool    admin_is_active(void);
void    admin_enter(void);
void    admin_on_card(const uint8_t *uid, uint8_t uid_len);
bool    admin_task(void);

#ifdef __cplusplus
}
#endif

#endif /* ADMIN_H */