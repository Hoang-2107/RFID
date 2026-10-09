/**
 * @file    rtc_clock.c
 * @brief   Dong ho thuc tren bo dem RTC STM32F1 (giay tu 2000-01-01)
 */
#include "rtc_clock.h"
#include <stdio.h>

#define SECS_PER_DAY  86400u

static RTC_HandleTypeDef *s_hrtc;

static const uint8_t k_mdays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

static bool is_leap(uint16_t y)
{
    return ((y % 4u == 0u) && (y % 100u != 0u)) || (y % 400u == 0u);
}

static uint8_t days_in_month(uint16_t y, uint8_t m)
{
    return (uint8_t)((m == 2u && is_leap(y)) ? 29u : k_mdays[m - 1u]);
}

/* Cho RTC san sang nhan lenh ghi (bit RTOFF) */
static bool rtc_wait_rtoff(void)
{
    uint32_t t0 = HAL_GetTick();
    while ((RTC->CRL & RTC_CRL_RTOFF) == 0u)
    {
        if ((uint32_t)(HAL_GetTick() - t0) > 100u) return false;
    }
    return true;
}

void rtc_clock_init(RTC_HandleTypeDef *hrtc)
{
    s_hrtc = hrtc;
}

uint32_t rtc_clock_now(void)
{
    /* Doc 2 thanh ghi 16 bit; neu phan cao thay doi giua chung thi doc lai phan thap */
    uint16_t high1 = (uint16_t)(RTC->CNTH & 0xFFFFu);
    uint16_t low   = (uint16_t)(RTC->CNTL & 0xFFFFu);
    uint16_t high2 = (uint16_t)(RTC->CNTH & 0xFFFFu);

    if (high1 != high2)
    {
        low = (uint16_t)(RTC->CNTL & 0xFFFFu);
    }
    return ((uint32_t)high2 << 16) | low;
}

bool rtc_clock_set(uint32_t t)
{
    if (s_hrtc == NULL) return false;

    HAL_PWR_EnableBkUpAccess();
    if (!rtc_wait_rtoff()) return false;
    RTC->CRL |= RTC_CRL_CNF;                  /* vao che do cau hinh */
    RTC->CNTH = (t >> 16) & 0xFFFFu;
    RTC->CNTL = t & 0xFFFFu;
    RTC->CRL &= ~RTC_CRL_CNF;                 /* thoat -> ghi vao bo dem */
    if (!rtc_wait_rtoff()) return false;

    HAL_RTCEx_BKUPWrite(s_hrtc, RTC_CLOCK_BKP_REG, RTC_CLOCK_MAGIC);
    return true;
}

bool rtc_clock_is_set(void)
{
    return (s_hrtc != NULL) && (HAL_RTCEx_BKUPRead(s_hrtc, RTC_CLOCK_BKP_REG) == RTC_CLOCK_MAGIC);
}

void rtc_clock_to_datetime(uint32_t t, RtcDateTime *dt)
{
    uint32_t days = t / SECS_PER_DAY;
    uint32_t rem  = t % SECS_PER_DAY;
    uint16_t y = 2000u;
    uint8_t  m = 1u;

    dt->hour   = (uint8_t)(rem / 3600u);
    dt->minute = (uint8_t)((rem % 3600u) / 60u);
    dt->second = (uint8_t)(rem % 60u);

    while (days >= (is_leap(y) ? 366u : 365u))
    {
        days -= is_leap(y) ? 366u : 365u;
        y++;
    }
    while (days >= days_in_month(y, m))
    {
        days -= days_in_month(y, m);
        m++;
    }
    dt->year  = y;
    dt->month = m;
    dt->day   = (uint8_t)(days + 1u);
}

bool rtc_clock_from_datetime(const RtcDateTime *dt, uint32_t *t)
{
    if ((dt->year < 2000u) || (dt->year > 2135u) ||
        (dt->month < 1u) || (dt->month > 12u) ||
        (dt->day < 1u) || (dt->day > days_in_month(dt->year, dt->month)) ||
        (dt->hour > 23u) || (dt->minute > 59u) || (dt->second > 59u))
    {
        return false;
    }

    uint32_t days = 0;
    for (uint16_t y = 2000u; y < dt->year; y++) days += is_leap(y) ? 366u : 365u;
    for (uint8_t m = 1u; m < dt->month; m++)    days += days_in_month(dt->year, m);
    days += (uint32_t)dt->day - 1u;

    *t = days * SECS_PER_DAY + (uint32_t)dt->hour * 3600u
       + (uint32_t)dt->minute * 60u + dt->second;
    return true;
}

/* Doc dung n chu so */
static bool read_num(const char **p, uint8_t n, uint16_t *out)
{
    uint16_t v = 0;
    for (uint8_t i = 0; i < n; i++)
    {
        char c = (*p)[i];
        if ((c < '0') || (c > '9')) return false;
        v = (uint16_t)(v * 10u + (uint16_t)(c - '0'));
    }
    *p += n;
    *out = v;
    return true;
}

static bool expect(const char **p, char c)
{
    if (**p != c) return false;
    (*p)++;
    return true;
}

bool rtc_clock_parse(const char *s, uint32_t *t)
{
    uint16_t y, mo, d, h, mi, se;
    RtcDateTime dt;

    if ((s == NULL) || (t == NULL)) return false;
    if (!read_num(&s, 4, &y)  || !expect(&s, '-') ||
        !read_num(&s, 2, &mo) || !expect(&s, '-') ||
        !read_num(&s, 2, &d)  || !expect(&s, ' ') ||
        !read_num(&s, 2, &h)  || !expect(&s, ':') ||
        !read_num(&s, 2, &mi) || !expect(&s, ':') ||
        !read_num(&s, 2, &se))
    {
        return false;
    }

    dt.year = y;
    dt.month = (uint8_t)mo;
    dt.day = (uint8_t)d;
    dt.hour = (uint8_t)h;
    dt.minute = (uint8_t)mi;
    dt.second = (uint8_t)se;
    return rtc_clock_from_datetime(&dt, t);
}

void rtc_clock_format(uint32_t t, char *buf, size_t size)
{
    RtcDateTime dt;
    rtc_clock_to_datetime(t, &dt);
    (void)snprintf(buf, size, "%04u-%02u-%02u %02u:%02u:%02u",
                   (unsigned)dt.year, (unsigned)dt.month, (unsigned)dt.day,
                   (unsigned)dt.hour, (unsigned)dt.minute, (unsigned)dt.second);
}