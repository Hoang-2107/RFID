#ifndef ATTENDANCE_H
#define ATTENDANCE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint8_t hours;
    uint8_t minutes;
    uint8_t seconds;
} AttTime;

typedef enum {
    ATT_CHECKED_IN = 0,   /* diem danh thanh cong (lan dau trong ngay) */
    ATT_ALREADY,          /* da diem danh truoc do -> tra ve gio lan dau */
    ATT_FULL              /* het cho luu (khong xay ra neu so the <= CARD_DB_MAX_ITEMS) */
} AttResult;

/* Danh sach "ai da co mat hom nay" trong RAM. Tu xoa khi sang ngay moi (theo RTC).
 * Goi rtc_clock_init() va attlog_init() truoc. */
void      attendance_init(void);

/* Nap lai danh sach co mat hom nay tu nhat ky Flash (sau khi mat dien / reset) */
void      attendance_restore_from_log(void);

AttResult attendance_check_in(uint32_t card_id, AttTime *time_out);
bool      attendance_get(uint32_t card_id, AttTime *time_out);
uint8_t   attendance_count(void);
void      attendance_remove(uint32_t card_id);   /* goi khi xoa the */
void      attendance_clear(void);                /* bat dau buoi moi */
void      attendance_now(AttTime *t);

#ifdef __cplusplus
}
#endif

#endif /* ATTENDANCE_H */