/**
 * @file    attendance.c
 * @brief   Diem danh: moi the chi ghi nhan 1 lan moi ngay, luu gio lan dau.
 *          Danh sach nam trong RAM, tu xoa khi sang ngay moi, va duoc nap lai
 *          tu nhat ky Flash (attlog) khi khoi dong.
 */
#include "attendance.h"
#include "attlog.h"
#include "card_db.h"
#include "rtc_clock.h"
#include <string.h>

#define SECS_PER_DAY 86400u

typedef struct {
    uint32_t card_id;
    AttTime  time;
} AttRecord;

static AttRecord s_rec[CARD_DB_MAX_ITEMS];
static uint8_t   s_count;
static uint32_t  s_day;

static void to_att_time(uint32_t t, AttTime *o)
{
    uint32_t rem = t % SECS_PER_DAY;
    o->hours   = (uint8_t)(rem / 3600u);
    o->minutes = (uint8_t)((rem % 3600u) / 60u);
    o->seconds = (uint8_t)(rem % 60u);
}

static int att_index(uint32_t card_id)
{
    for (uint8_t i = 0; i < s_count; i++)
    {
        if (s_rec[i].card_id == card_id) return (int)i;
    }
    return -1;
}

/* Sang ngay moi -> xoa danh sach */
static void roll_day(uint32_t now)
{
    uint32_t day = now / SECS_PER_DAY;
    if (day != s_day)
    {
        attendance_clear();
        s_day = day;
    }
}

static void add_record(uint32_t card_id, uint32_t t)
{
    if ((s_count >= CARD_DB_MAX_ITEMS) || (att_index(card_id) >= 0)) return;
    s_rec[s_count].card_id = card_id;
    to_att_time(t, &s_rec[s_count].time);
    s_count++;
}

void attendance_init(void)
{
    attendance_clear();
    s_day = rtc_clock_now() / SECS_PER_DAY;
}

void attendance_clear(void)
{
    memset(s_rec, 0, sizeof(s_rec));
    s_count = 0;
}

static void restore_visitor(const AttLogRecord *rec, void *ctx)
{
    uint32_t day = *(const uint32_t *)ctx;
    if ((rec->result == ATTLOG_OK) && ((rec->time / SECS_PER_DAY) == day) &&
        (card_db_find_by_id(rec->card_id) != NULL))
    {
        add_record(rec->card_id, rec->time);
    }
}

void attendance_restore_from_log(void)
{
    if (!rtc_clock_is_set()) return;      /* gio sai thi "hom nay" khong co nghia */
    uint32_t day = rtc_clock_now() / SECS_PER_DAY;
    s_day = day;
    attendance_clear();
    attlog_foreach(restore_visitor, &day);
}

void attendance_now(AttTime *t)
{
    if (t != NULL) to_att_time(rtc_clock_now(), t);
}

AttResult attendance_check_in(uint32_t card_id, AttTime *time_out)
{
    uint32_t now = rtc_clock_now();
    int i;

    roll_day(now);
    i = att_index(card_id);
    if (i >= 0)
    {
        if (time_out) *time_out = s_rec[i].time;
        return ATT_ALREADY;
    }
    if (s_count >= CARD_DB_MAX_ITEMS)
    {
        return ATT_FULL;
    }

    add_record(card_id, now);
    if (time_out) *time_out = s_rec[s_count - 1u].time;
    return ATT_CHECKED_IN;
}

bool attendance_get(uint32_t card_id, AttTime *time_out)
{
    roll_day(rtc_clock_now());
    int i = att_index(card_id);
    if (i < 0) return false;
    if (time_out) *time_out = s_rec[i].time;
    return true;
}

uint8_t attendance_count(void)
{
    roll_day(rtc_clock_now());
    return s_count;
}

void attendance_remove(uint32_t card_id)
{
    int i = att_index(card_id);
    if (i < 0) return;

    for (uint8_t j = (uint8_t)i; (uint8_t)(j + 1u) < s_count; j++)
    {
        s_rec[j] = s_rec[j + 1u];
    }
    s_count--;
    memset(&s_rec[s_count], 0, sizeof(s_rec[0]));
}