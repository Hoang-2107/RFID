
#ifndef RTC_CLOCK_H
#define RTC_CLOCK_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "main.h"

/*
 * Dong ho thuc dung bo dem 32 bit cua RTC STM32F1.
 * Gia tri tinh bang giay tu 2000-01-01 00:00:00.
 */

#define RTC_CLOCK_MAGIC    0x32F2u
#define RTC_CLOCK_BKP_REG  RTC_BKP_DR1

typedef struct {
    uint16_t year;   /* 2000..2135 */
    uint8_t month;  /* 1..12 */
    uint8_t day;    /* 1..31 */
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
} RtcDateTime;

void rtc_clock_init(RTC_HandleTypeDef *hrtc);

uint32_t rtc_clock_now(void);
bool rtc_clock_set(uint32_t t);
bool rtc_clock_is_set(void);

void rtc_clock_to_datetime(uint32_t t, RtcDateTime *dt);
bool rtc_clock_from_datetime(const RtcDateTime *dt, uint32_t *t);

bool rtc_clock_parse(const char *s, uint32_t *t);
void rtc_clock_format(uint32_t t, char *buf, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* RTC_CLOCK_H */
