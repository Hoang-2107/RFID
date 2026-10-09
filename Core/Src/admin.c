/**
 * @file    admin.c
 * @brief   Quyen admin: quan ly the bang the admin + 3 nut + OLED
 *
 * Menu admin:
 *   Add card      - quet the moi -> them, ID tu tang, ten "Card <ID>"
 *   Delete card   - quet the -> xoa
 *   Lock/Unlock   - quet the -> doi trang thai khoa/mo
 *   Add admin     - quet the -> them vao danh sach admin
 *   Remove admin  - quet the -> bo quyen admin
 *   Erase all     - xoa toan bo the nguoi dung (giu lai admin), can xac nhan OK
 *   Exit
 * Moi thay doi deu duoc luu Flash ngay (flash_store_save).
 * O cac muc quet the co the quet lien tiep nhieu the, nhan MENU de quay lai.
 */
#include "admin.h"
#include "flash.h"
#include "OLED.h"
#include "main.h"
#include <stdio.h>
#include <string.h>
#include "attendance.h"
#include "attlog.h"
#include "rtc_clock.h"
#include "uart_cmd.h"
/* CubeMX thuong sinh BTN_xxx_GPIO_Port trong main.h; neu khong co thi dung GPIOA */
#ifndef BTN_MENU_GPIO_Port
#define BTN_MENU_GPIO_Port GPIOA
#endif
#ifndef BTN_NEXT_GPIO_Port
#define BTN_NEXT_GPIO_Port GPIOA
#endif
#ifndef BTN_OK_GPIO_Port
#define BTN_OK_GPIO_Port   GPIOA
#endif

#define BTN_DEBOUNCE_MS  30u
#define ADMIN_MSG_MS     1500u

#define EV_MENU  0x01u
#define EV_NEXT  0x02u
#define EV_OK    0x04u

typedef enum { ST_OFF = 0, ST_MENU, ST_WAIT_CARD, ST_CONFIRM, ST_MSG } AdmState;

typedef enum {
    ACT_ADD_CARD = 0,
    ACT_DEL_CARD,
    ACT_TOGGLE,
    ACT_ADD_ADMIN,
    ACT_DEL_ADMIN,
    ACT_ERASE_ALL,
    ACT_EXIT,
    ACT_COUNT
} AdmAction;

static const char *const k_menu[ACT_COUNT] = {
    "Add card", "Delete card", "Lock/Unlock", "Add admin",
    "Remove admin", "Erase all cards", "Exit"
};

typedef struct {
    uint8_t uid[CARD_UID_MAX_LEN];
    uint8_t len;
} AdminUid;

typedef struct {
    GPIO_TypeDef *port;
    uint16_t      pin;
    uint8_t       stable;   /* 1 = dang nhan (da chong doi) */
    uint8_t       raw;
    uint32_t      t_change;
} Button;

static AdminUid s_admins[ADMIN_MAX_CARDS];
static uint8_t  s_admin_count;

/* Thu tu phai khop EV_MENU, EV_NEXT, EV_OK */
static Button s_btn[3] = {
    {BTN_MENU_GPIO_Port, BTN_MENU_Pin, 0, 0, 0},
    {BTN_NEXT_GPIO_Port, BTN_NEXT_Pin, 0, 0, 0},
    {BTN_OK_GPIO_Port,   BTN_OK_Pin,   0, 0, 0},
};

static AdmState  s_state = ST_OFF;
static AdmState  s_after = ST_MENU;     /* trang thai sau khi het thong bao */
static AdmAction s_action = ACT_ADD_CARD;
static uint8_t   s_sel;
static uint32_t  s_msg_until;
static uint32_t  s_last_activity;

/* ========================== Danh sach admin ========================== */

static int admin_index(const uint8_t *uid, uint8_t uid_len)
{
    if ((uid == NULL) || (uid_len == 0u) || (uid_len > CARD_UID_MAX_LEN)) return -1;
    for (uint8_t i = 0; i < s_admin_count; i++)
    {
        if ((s_admins[i].len == uid_len) && (memcmp(s_admins[i].uid, uid, uid_len) == 0))
        {
            return (int)i;
        }
    }
    return -1;
}

void admin_clear(void)
{
    memset(s_admins, 0, sizeof(s_admins));
    s_admin_count = 0;
}

uint8_t admin_get_count(void)
{
    return s_admin_count;
}

bool admin_get_at(uint8_t index, const uint8_t **uid, uint8_t *uid_len)
{
    if ((index >= s_admin_count) || (uid == NULL) || (uid_len == NULL)) return false;
    *uid = s_admins[index].uid;
    *uid_len = s_admins[index].len;
    return true;
}

bool admin_is_admin_uid(const uint8_t *uid, uint8_t uid_len)
{
    return admin_index(uid, uid_len) >= 0;
}

bool admin_add_uid(const uint8_t *uid, uint8_t uid_len)
{
    if ((uid == NULL) || (uid_len == 0u) || (uid_len > CARD_UID_MAX_LEN)) return false;
    if ((s_admin_count >= ADMIN_MAX_CARDS) || (admin_index(uid, uid_len) >= 0)) return false;

    memset(&s_admins[s_admin_count], 0, sizeof(s_admins[0]));
    memcpy(s_admins[s_admin_count].uid, uid, uid_len);
    s_admins[s_admin_count].len = uid_len;
    s_admin_count++;
    return true;
}

bool admin_remove_uid(const uint8_t *uid, uint8_t uid_len)
{
    int idx = admin_index(uid, uid_len);
    if (idx < 0) return false;

    for (uint8_t j = (uint8_t)idx; (uint8_t)(j + 1u) < s_admin_count; j++)
    {
        s_admins[j] = s_admins[j + 1u];
    }
    s_admin_count--;
    memset(&s_admins[s_admin_count], 0, sizeof(s_admins[0]));
    return true;
}

/* ========================== Nut bam ========================== */

static uint8_t button_read(const Button *b)
{
    return (HAL_GPIO_ReadPin(b->port, b->pin) == GPIO_PIN_RESET) ? 1u : 0u;  /* pull-up, nhan = 0 */
}

/* Tra ve bit EV_xxx cho nhung nut vua duoc NHAN (canh xuong da chong doi) */
static uint8_t buttons_poll(void)
{
    uint32_t now = HAL_GetTick();
    uint8_t ev = 0;

    for (uint8_t i = 0; i < 3u; i++)
    {
        Button *b = &s_btn[i];
        uint8_t raw = button_read(b);

        if (raw != b->raw)
        {
            b->raw = raw;
            b->t_change = now;
        }
        else if ((raw != b->stable) && ((uint32_t)(now - b->t_change) >= BTN_DEBOUNCE_MS))
        {
            b->stable = raw;
            if (raw) ev |= (uint8_t)(1u << i);
        }
    }
    return ev;
}

/* ========================== Hien thi ========================== */

static void uid_to_hex(const uint8_t *uid, uint8_t len, char *out, size_t size)
{
    size_t o = 0;
    for (uint8_t i = 0; (i < len) && (o + 2u < size); i++)
    {
        o += (size_t)snprintf(out + o, size - o, "%02X", uid[i]);
    }
    out[o] = '\0';
}

static void draw_menu(void)
{
    OLED_Clear();
    OLED_PrintCenter(0, "== ADMIN MENU ==");
    for (uint8_t i = 0; i < ACT_COUNT; i++)
    {
        OLED_Printf((uint8_t)(i + 1u), "%c %s", (i == s_sel) ? '>' : ' ', k_menu[i]);
    }
    OLED_InvertRow((uint8_t)(s_sel + 1u));
    (void)OLED_Update();
}

static void draw_wait(void)
{
    OLED_Clear();
    OLED_PrintCenter(0, k_menu[s_action]);
    OLED_PrintCenter(3, "Scan card...");
    OLED_PrintCenter(7, "MENU: back");
    (void)OLED_Update();
}

static void draw_confirm(void)
{
    OLED_Clear();
    OLED_PrintCenter(1, "ERASE ALL CARDS?");
    OLED_Printf(3, "  Cards: %u", (unsigned)card_db_count());
    OLED_PrintCenter(5, "OK: erase");
    OLED_PrintCenter(7, "MENU: cancel");
    (void)OLED_Update();
}

/* Hien thong bao ADMIN_MSG_MS roi chuyen sang trang thai 'after' */
static void show_msg(const char *l1, const char *l2, const char *l3, AdmState after)
{
    OLED_Clear();
    if (l1) OLED_PrintCenter(1, l1);
    if (l2) OLED_PrintCenter(3, l2);
    if (l3) OLED_PrintCenter(5, l3);
    (void)OLED_Update();

    s_after = after;
    s_msg_until = HAL_GetTick() + ADMIN_MSG_MS;
    s_state = ST_MSG;
}

/* ========================== Xu ly ========================== */

static uint32_t next_card_id(void)
{
    uint32_t max_id = ADMIN_FIRST_CARD_ID - 1u;
    for (uint8_t i = 0; i < card_db_count(); i++)
    {
        const CardEntry *e = card_db_get_at(i);
        if ((e != NULL) && (e->card_id > max_id)) max_id = e->card_id;
    }
    return max_id + 1u;
}

static void menu_select(void)
{
    s_action = (AdmAction)s_sel;
    switch (s_action)
    {
    case ACT_EXIT:
        s_state = ST_OFF;
        break;
    case ACT_ERASE_ALL:
        s_state = ST_CONFIRM;
        draw_confirm();
        break;
    default:
        s_state = ST_WAIT_CARD;
        draw_wait();
        break;
    }
}

void admin_init(void)
{
    admin_clear();
    s_state = ST_OFF;
    s_sel = 0;
    for (uint8_t i = 0; i < 3u; i++)   /* lay trang thai hien tai, tranh su kien gia luc khoi dong */
    {
        s_btn[i].raw = s_btn[i].stable = button_read(&s_btn[i]);
        s_btn[i].t_change = HAL_GetTick();
    }
}
void admin_keep_alive(void)
{
    if (s_state != ST_OFF) s_last_activity = HAL_GetTick();
}
bool admin_is_active(void)
{
    return s_state != ST_OFF;
}

void admin_enter(void)
{
    s_last_activity = HAL_GetTick();
    s_sel = 0;

    if (s_admin_count == 0u)
    {
        /* Chua co admin: vao thang muc them the admin */
        s_action = ACT_ADD_ADMIN;
        s_sel = ACT_ADD_ADMIN;
        show_msg("SETUP MODE", "No admin card", "Scan one to add", ST_WAIT_CARD);
    }
    else
    {
        show_msg("ADMIN MODE", "Welcome", NULL, ST_MENU);
    }
}

void admin_on_card(const uint8_t *uid, uint8_t uid_len)
{
    char line[24];
    char hex[2u * CARD_UID_MAX_LEN + 1u];
    const CardEntry *e;
    bool ok;

    if (s_state != ST_WAIT_CARD) return;
    s_last_activity = HAL_GetTick();
    uid_to_hex(uid, uid_len, hex, sizeof(hex));

    switch (s_action)
    {
    case ACT_ADD_CARD:
        if (admin_is_admin_uid(uid, uid_len))
        {
            show_msg("ADMIN CARD", "Cannot add", hex, ST_WAIT_CARD);
        }
        else if ((e = card_db_find_by_uid(uid, uid_len)) != NULL)
        {
            (void)snprintf(line, sizeof(line), "ID %lu", (unsigned long)e->card_id);
            show_msg("ALREADY EXISTS", line, hex, ST_WAIT_CARD);
        }
        else if (card_db_is_full())
        {
            show_msg("DATABASE FULL", NULL, NULL, ST_WAIT_CARD);
        }
        else
        {
            uint32_t id = next_card_id();
            char name[CARD_NAME_LEN];
            (void)snprintf(name, sizeof(name), "Card %lu", (unsigned long)id);
            ok = card_db_add(id, uid, uid_len, name, true) && flash_store_save();
            (void)snprintf(line, sizeof(line), "ID %lu", (unsigned long)id);
            show_msg(ok ? "CARD ADDED" : "SAVE FAILED", line, hex, ST_WAIT_CARD);
        }
        break;

    case ACT_DEL_CARD:
        e = card_db_find_by_uid(uid, uid_len);
        if (e == NULL)
        {
            show_msg("NOT FOUND", NULL, hex, ST_WAIT_CARD);
        }
        else
        {
            (void)snprintf(line, sizeof(line), "ID %lu", (unsigned long)e->card_id);
            ok = card_db_remove_by_uid(uid, uid_len) && flash_store_save();   /* e khong dung sau dong nay */
            show_msg(ok ? "CARD DELETED" : "SAVE FAILED", line, hex, ST_WAIT_CARD);
        }
        break;

    case ACT_TOGGLE:
        e = card_db_find_by_uid(uid, uid_len);
        if (e == NULL)
        {
            show_msg("NOT FOUND", NULL, hex, ST_WAIT_CARD);
        }
        else
        {
            bool new_state = !e->enabled;
            (void)snprintf(line, sizeof(line), "%s", e->name);
            ok = card_db_update_status(e->card_id, new_state) && flash_store_save();
            show_msg(!ok ? "SAVE FAILED" : (new_state ? "CARD UNLOCKED" : "CARD LOCKED"),
                     line, hex, ST_WAIT_CARD);
        }
        break;

    case ACT_ADD_ADMIN:
        if (admin_is_admin_uid(uid, uid_len))
        {
            show_msg("ALREADY ADMIN", NULL, hex, ST_WAIT_CARD);
        }
        else if (card_db_is_registered(uid, uid_len))
        {
            show_msg("USER CARD", "Delete it first", hex, ST_WAIT_CARD);
        }
        else if (s_admin_count >= ADMIN_MAX_CARDS)
        {
            show_msg("ADMIN LIST FULL", NULL, NULL, ST_WAIT_CARD);
        }
        else
        {
            ok = admin_add_uid(uid, uid_len) && flash_store_save();
            (void)snprintf(line, sizeof(line), "Admins: %u", (unsigned)s_admin_count);
            show_msg(ok ? "ADMIN ADDED" : "SAVE FAILED", line, hex, ST_WAIT_CARD);
        }
        break;

    case ACT_DEL_ADMIN:
        if (!admin_is_admin_uid(uid, uid_len))
        {
            show_msg("NOT AN ADMIN", NULL, hex, ST_WAIT_CARD);
        }
        else
        {
            ok = admin_remove_uid(uid, uid_len) && flash_store_save();
            (void)snprintf(line, sizeof(line), "Admins: %u", (unsigned)s_admin_count);
            show_msg(ok ? "ADMIN REMOVED" : "SAVE FAILED", line,
                     (s_admin_count == 0u) ? "No admin left!" : hex, ST_WAIT_CARD);
        }
        break;

    default:
        break;
    }
}

bool admin_task(void)
{
    uint8_t  ev = buttons_poll();
    uint32_t now = HAL_GetTick();
    AdmState before = s_state;

    if (s_state == ST_OFF)
    {
        /* Chua co admin nao: cho phep vao che do cai dat bang nut MENU */
        if ((ev & EV_MENU) && (s_admin_count == 0u))
        {
            admin_enter();
        }
        return false;
    }

    if (ev) s_last_activity = now;
    if ((uint32_t)(now - s_last_activity) >= ADMIN_TIMEOUT_MS)
    {
        s_state = ST_OFF;
        return true;
    }

    switch (s_state)
    {
    case ST_MENU:
        if (ev & EV_NEXT)
        {
            s_sel = (uint8_t)((s_sel + 1u) % ACT_COUNT);
            draw_menu();
        }
        else if (ev & EV_OK)
        {
            menu_select();
        }
        else if (ev & EV_MENU)
        {
            s_state = ST_OFF;
        }
        break;

    case ST_WAIT_CARD:
        if (ev & EV_MENU)
        {
            s_state = ST_MENU;
            draw_menu();
        }
        break;

    case ST_CONFIRM:
        if (ev & EV_OK)
        {
            card_db_clear();
            bool ok = flash_store_save();
            show_msg(ok ? "ALL CARDS ERASED" : "SAVE FAILED", NULL, NULL, ST_MENU);
        }
        else if (ev & EV_MENU)
        {
            s_state = ST_MENU;
            draw_menu();
        }
        break;

    case ST_MSG:
        if ((int32_t)(now - s_msg_until) >= 0)
        {
            s_state = s_after;
            if (s_state == ST_MENU)           draw_menu();
            else if (s_state == ST_WAIT_CARD) draw_wait();
        }
        break;

    default:
        break;
    }

    return (before != ST_OFF) && (s_state == ST_OFF);
}