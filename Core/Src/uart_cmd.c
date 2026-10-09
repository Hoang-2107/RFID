/**
 * @file    uart_cmd.c
 * @brief   Nhan lenh qua UART (ngat), xuat CSV nhat ky diem danh / danh sach the
 */
#include "uart_cmd.h"
#include "admin.h"
#include "attendance.h"
#include "attlog.h"
#include "card_db.h"
#include "flash.h"
#include "rtc_clock.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RX_BUF_SIZE  128u      /* luy thua cua 2 */
#define LINE_MAX     72u

static UART_HandleTypeDef *s_huart;
static uint8_t s_rx_byte;
static volatile uint8_t s_rx_buf[RX_BUF_SIZE];
static volatile uint8_t s_rx_head;     /* ISR ghi */
static volatile uint8_t s_rx_tail;     /* vong lap chinh doc */

static char    s_line[LINE_MAX];
static uint8_t s_line_len;
static bool    s_line_overflow;

/* ========================== Ngat UART ========================== */

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart != s_huart) return;

    uint8_t next = (uint8_t)((s_rx_head + 1u) & (RX_BUF_SIZE - 1u));
    if (next != s_rx_tail)                  /* day thi bo ky tu */
    {
        s_rx_buf[s_rx_head] = s_rx_byte;
        s_rx_head = next;
    }
    (void)HAL_UART_Receive_IT(huart, &s_rx_byte, 1);
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart != s_huart) return;
    __HAL_UART_CLEAR_OREFLAG(huart);
    (void)HAL_UART_Receive_IT(huart, &s_rx_byte, 1);
}

/* ========================== Xuat ========================== */

void uart_cmd_print(const char *s)
{
    if (s_huart != NULL)
    {
        (void)HAL_UART_Transmit(s_huart, (uint8_t *)s, (uint16_t)strlen(s), 500);
    }
}

static void uid_to_hex(const uint8_t *uid, uint8_t len, char *out, size_t size)
{
    size_t o = 0;
    for (uint8_t i = 0; (i < len) && (o + 2u < size); i++)
    {
        o += (size_t)snprintf(out + o, size - o, "%02X", uid[i]);
    }
    out[o] = '\0';
}

/* "YYYY-MM-DD HH:MM:SS" -> "YYYY-MM-DD,HH:MM:SS" (2 cot CSV) */
static void format_csv_time(uint32_t t, char *buf, size_t size)
{
    rtc_clock_format(t, buf, size);
    if (strlen(buf) > 10u) buf[10] = ',';
}

/* ========================== Lenh ========================== */

static bool allowed(void)
{
    if ((admin_get_count() == 0u) || admin_is_active())
    {
        admin_keep_alive();
        return true;
    }
    uart_cmd_print("ERR: scan admin card first\r\n");
    return false;
}

static void cmd_help(void)
{
    uart_cmd_print(
        "Commands:\r\n"
        "  TIME                       show clock\r\n"
        "  TIME YYYY-MM-DD HH:MM:SS   set clock (*)\r\n"
        "  LOG                        export attendance log (CSV)\r\n"
        "  LOG CLEAR                  erase log (*)\r\n"
        "  LIST                       export card list (CSV)\r\n"
        "  NAME <id> <name>           rename card (*)\r\n"
        "  STAT                       statistics\r\n"
        "(*) admin mode required: scan admin card first\r\n");
}

static void cmd_time(const char *arg)
{
    char buf[64];
    char ts[24];

    if (*arg != '\0')
    {
        uint32_t t;
        if (!allowed()) return;
        if (!rtc_clock_parse(arg, &t))
        {
            uart_cmd_print("ERR: format TIME YYYY-MM-DD HH:MM:SS\r\n");
            return;
        }
        if (!rtc_clock_set(t))
        {
            uart_cmd_print("ERR: RTC write failed\r\n");
            return;
        }
        attendance_restore_from_log();      /* "hom nay" co the da doi */
    }
    rtc_clock_format(rtc_clock_now(), ts, sizeof(ts));
    (void)snprintf(buf, sizeof(buf), "TIME %s%s\r\n", ts,
                   rtc_clock_is_set() ? "" : " (NOT SET)");
    uart_cmd_print(buf);
}

static void log_visitor(const AttLogRecord *r, void *ctx)
{
    char line[128];
    char ts[24];
    char id[12];
    char uid[2u * CARD_UID_MAX_LEN + 4u];
    const char *name;

    (void)ctx;
    format_csv_time(r->time, ts, sizeof(ts));

    if (r->result == ATTLOG_UNKNOWN)
    {
        uint8_t b[4] = {(uint8_t)(r->card_id >> 24), (uint8_t)(r->card_id >> 16),
                        (uint8_t)(r->card_id >> 8),  (uint8_t)r->card_id};
        uid_to_hex(b, (r->uid_len < 4u) ? r->uid_len : 4u, uid, sizeof(uid));
        if (r->uid_len > 4u) strcat(uid, "..");
        (void)snprintf(id, sizeof(id), "-");
        name = "";
    }
    else
    {
        const CardEntry *e = card_db_find_by_id(r->card_id);
        (void)snprintf(id, sizeof(id), "%lu", (unsigned long)r->card_id);
        if (e != NULL)
        {
            uid_to_hex(e->uid, e->uid_len, uid, sizeof(uid));
            name = e->name;
        }
        else
        {
            (void)snprintf(uid, sizeof(uid), "-");
            name = "(deleted)";
        }
    }

    (void)snprintf(line, sizeof(line), "%lu,%s,%s,\"%s\",%s,%s\r\n",
                   (unsigned long)r->seq, ts, id, name, uid,
                   attlog_result_str(r->result));
    uart_cmd_print(line);
}

static void cmd_log(const char *arg)
{
    char buf[64];

    if (*arg != '\0')
    {
        if (strcmp(arg, "CLEAR") != 0)
        {
            uart_cmd_print("ERR: LOG or LOG CLEAR\r\n");
            return;
        }
        if (!allowed()) return;
        uart_cmd_print(attlog_clear() ? "OK: log cleared\r\n" : "ERR: flash erase failed\r\n");
        return;
    }

    (void)snprintf(buf, sizeof(buf), "# BEGIN ATTLOG %u records\r\n", (unsigned)attlog_count());
    uart_cmd_print(buf);
    uart_cmd_print("seq,date,time,card_id,name,uid,result\r\n");
    attlog_foreach(log_visitor, NULL);
    uart_cmd_print("# END ATTLOG\r\n");
}

static void cmd_list(void)
{
    char line[128];
    char uid[2u * CARD_UID_MAX_LEN + 1u];
    AttTime t;

    uart_cmd_print("# BEGIN CARDS\r\n");
    uart_cmd_print("card_id,name,uid,state,present_at\r\n");
    for (uint8_t i = 0; i < card_db_count(); i++)
    {
        const CardEntry *e = card_db_get_at(i);
        char present[12] = "";

        if (e == NULL) continue;
        uid_to_hex(e->uid, e->uid_len, uid, sizeof(uid));
        if (attendance_get(e->card_id, &t))
        {
            (void)snprintf(present, sizeof(present), "%02u:%02u:%02u",
                           (unsigned)t.hours, (unsigned)t.minutes, (unsigned)t.seconds);
        }
        (void)snprintf(line, sizeof(line), "%lu,\"%s\",%s,%s,%s\r\n",
                       (unsigned long)e->card_id, e->name, uid,
                       e->enabled ? "ACTIVE" : "LOCKED", present);
        uart_cmd_print(line);
    }
    uart_cmd_print("# END CARDS\r\n");
}

static void cmd_name(const char *arg)
{
    char *end;
    unsigned long id;

    if (!allowed()) return;
    id = strtoul(arg, &end, 10);
    if ((end == arg) || (*end != ' '))
    {
        uart_cmd_print("ERR: NAME <id> <name>\r\n");
        return;
    }
    while (*end == ' ') end++;
    if (*end == '\0')
    {
        uart_cmd_print("ERR: name is empty\r\n");
        return;
    }
    if (strchr(end, '"') != NULL)
    {
        uart_cmd_print("ERR: name must not contain '\"'\r\n");
        return;
    }
    if (!card_db_update_name((uint32_t)id, end))
    {
        uart_cmd_print("ERR: card id not found\r\n");
        return;
    }
    uart_cmd_print(flash_store_save() ? "OK: name saved\r\n" : "ERR: flash save failed\r\n");
}

static void cmd_stat(void)
{
    char buf[160];
    char ts[24];

    rtc_clock_format(rtc_clock_now(), ts, sizeof(ts));
    (void)snprintf(buf, sizeof(buf),
                   "CARDS=%u ADMINS=%u PRESENT=%u LOG=%u/%u TIME=%s%s\r\n",
                   (unsigned)card_db_count(), (unsigned)admin_get_count(),
                   (unsigned)attendance_count(), (unsigned)attlog_count(),
                   (unsigned)attlog_capacity(), ts,
                   rtc_clock_is_set() ? "" : " (NOT SET)");
    uart_cmd_print(buf);
}

static void execute(char *line)
{
    char *arg = line;

    while (*line == ' ') line++;
    arg = line;
    while ((*arg != '\0') && (*arg != ' ')) arg++;
    if (*arg != '\0')
    {
        *arg++ = '\0';
        while (*arg == ' ') arg++;
    }
    /* Viet hoa ten lenh; voi LOG viet hoa ca tham so (CLEAR) */
    for (char *p = line; *p; p++) *p = (char)toupper((unsigned char)*p);
    if (strcmp(line, "LOG") == 0)
    {
        for (char *p = arg; *p; p++) *p = (char)toupper((unsigned char)*p);
    }
    /* bo khoang trang cuoi tham so */
    for (size_t n = strlen(arg); (n > 0u) && (arg[n - 1u] == ' '); n--) arg[n - 1u] = '\0';

    if      (strcmp(line, "HELP") == 0) cmd_help();
    else if (strcmp(line, "TIME") == 0) cmd_time(arg);
    else if (strcmp(line, "LOG")  == 0) cmd_log(arg);
    else if (strcmp(line, "LIST") == 0) cmd_list();
    else if (strcmp(line, "NAME") == 0) cmd_name(arg);
    else if (strcmp(line, "STAT") == 0) cmd_stat();
    else if (*line != '\0')             uart_cmd_print("ERR: unknown command, type HELP\r\n");
}

/* ========================== API ========================== */

void uart_cmd_init(UART_HandleTypeDef *huart)
{
    s_huart = huart;
    s_rx_head = s_rx_tail = 0;
    s_line_len = 0;
    s_line_overflow = false;
    (void)HAL_UART_Receive_IT(huart, &s_rx_byte, 1);
}

void uart_cmd_task(void)
{
    while (s_rx_tail != s_rx_head)
    {
        char c = (char)s_rx_buf[s_rx_tail];
        s_rx_tail = (uint8_t)((s_rx_tail + 1u) & (RX_BUF_SIZE - 1u));

        if ((c == '\r') || (c == '\n'))
        {
            if (s_line_len > 0u)
            {
                s_line[s_line_len] = '\0';
                if (s_line_overflow) uart_cmd_print("ERR: line too long\r\n");
                else                 execute(s_line);
            }
            s_line_len = 0;
            s_line_overflow = false;
        }
        else if ((c == '\b') || (c == 0x7F))
        {
            if (s_line_len > 0u) s_line_len--;
        }
        else if (s_line_len < (LINE_MAX - 1u))
        {
            s_line[s_line_len++] = c;
        }
        else
        {
            s_line_overflow = true;
        }
    }

    /* Phong truong hop nhan bi dung (loi UART khi dang truyen): khoi dong lai */
    if ((s_huart != NULL) && (s_huart->RxState == HAL_UART_STATE_READY))
    {
        (void)HAL_UART_Receive_IT(s_huart, &s_rx_byte, 1);
    }
}