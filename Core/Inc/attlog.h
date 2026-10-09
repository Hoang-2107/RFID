#ifndef ATTLOG_H
#define ATTLOG_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include "main.h"

/* Nhat ky diem danh: bo dem vong trong Flash.
 * Mac dinh 8 KB ngay truoc vung flash_store (0x0800D800..0x0800F7FF).
 * -> Trong file linker (.ld) phai dat FLASH LENGTH = 54K.
 * Moi ban ghi 16 byte; 8 KB = 512 o, dam bao giu duoc it nhat 448 lan quet
 * gan nhat (luon co 1 page bi xoa de ghi tiep). Khi day, ban ghi cu nhat
 * bi ghi de theo tung page. */
#ifndef ATTLOG_ADDR
#define ATTLOG_ADDR   0x0800D800u
#endif
#ifndef ATTLOG_SIZE
#define ATTLOG_SIZE   8192u          /* boi so cua FLASH_PAGE_SIZE, it nhat 2 page */
#endif

/* 0 = khong ghi Flash khi 1 the quet lai trong cung buoi (tiet kiem cho trong) */
#ifndef ATTLOG_SAVE_REPEAT
#define ATTLOG_SAVE_REPEAT  0
#endif

typedef enum {
    ATTLOG_OK = 0,     /* diem danh thanh cong            */
    ATTLOG_AGAIN,      /* quet lai, da diem danh truoc do */
    ATTLOG_LOCKED,     /* the bi khoa                     */
    ATTLOG_UNKNOWN     /* the chua dang ky                */
} AttLogResult;

typedef struct {
    uint32_t seq;      /* so thu tu tang dan */
    uint32_t time;     /* giay tu 2000-01-01 (rtc_clock) */
    uint32_t card_id;  /* ATTLOG_UNKNOWN: 4 byte dau cua UID (byte 0 o bit cao nhat) */
    uint8_t  result;   /* AttLogResult */
    uint8_t  uid_len;  /* do dai UID that cua the */
} AttLogRecord;

typedef void (*AttLogVisitor)(const AttLogRecord *rec, void *ctx);

void     attlog_init(void);            /* quet Flash, tim vi tri ghi tiep */
bool     attlog_append(AttLogResult result, uint32_t card_id,
                       const uint8_t *uid, uint8_t uid_len);
uint16_t attlog_count(void);           /* so ban ghi hop le dang luu */
uint16_t attlog_capacity(void);        /* so ban ghi toi thieu dam bao giu duoc */
void     attlog_foreach(AttLogVisitor fn, void *ctx);   /* cu nhat -> moi nhat */
bool     attlog_clear(void);
const char *attlog_result_str(uint8_t result);

#ifdef __cplusplus
}
#endif

#endif /* ATTLOG_H */