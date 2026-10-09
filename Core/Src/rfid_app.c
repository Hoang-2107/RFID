#include "rfid_app.h"
#include <stdio.h>
#include <string.h>

RC522_Handle rc522 = {0};
volatile uint8_t rc522_version = 0;
volatile RC522_Status rfid_init_status = RC522_BAD_ARG;
volatile RC522_Status rfid_last_status = RC522_NO_CARD;
volatile RC522_UID rfid_last_uid = {{0}, 0, 0};
volatile uint32_t rfid_event_count = 0;
volatile RC522_SPITest rfid_spi_diag = {0};
volatile RC522_Status rfid_spi_diag_status = RC522_BAD_ARG;
volatile RC522_Status rfid_error_status = RC522_OK;
volatile RC522_DebugStage rfid_error_stage = RC522_DBG_NONE;
volatile uint8_t rfid_error_command = 0;
volatile uint8_t rfid_error_irq = 0;
volatile uint8_t rfid_error_reg = 0;
volatile uint8_t rfid_error_len = 0;
volatile uint8_t rfid_error_bits = 0;
volatile uint8_t rfid_error_registers_valid = 0;

static UART_HandleTypeDef *rfid_uart;
static uint8_t led_edges_left;
static uint32_t led_next_ms;
static uint32_t led_half_period_ms;

static void UART_Print(const char *str)
{
    if (rfid_uart)
    {
        (void)HAL_UART_Transmit(rfid_uart, (uint8_t *)str,
                               (uint16_t)strlen(str), 100);
    }
}

static void RFID_PrintUID(const RC522_UID *uid)
{
    char text[64];
    if (!uid || uid->size > sizeof(uid->bytes)) return;

    int pos = snprintf(text, sizeof(text), "UID(%u): ", (unsigned)uid->size);
    for (uint8_t i = 0; i < uid->size; i++)
    {
        pos += snprintf(text + pos, sizeof(text) - (size_t)pos,
                        "%02X ", (unsigned)uid->bytes[i]);
    }
    (void)snprintf(text + pos, sizeof(text) - (size_t)pos, "\r\n");
    UART_Print(text);
}

static void LED_Start(uint8_t pulses, uint32_t half_period_ms)
{
    if (!pulses) return;

    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);
    led_edges_left = (uint8_t)(2U * pulses - 1U);
    led_half_period_ms = half_period_ms;
    led_next_ms = HAL_GetTick() + half_period_ms;
}

static void LED_Task(void)
{
    uint32_t now = HAL_GetTick();
    if (led_edges_left && (int32_t)(now - led_next_ms) >= 0)
    {
        HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
        led_edges_left--;
        led_next_ms = now + led_half_period_ms;
        if (!led_edges_left)
            HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);
    }
}

static void SaveError(RC522_Status status)
{
    rfid_error_status = status;
    rfid_error_stage = rc522_debug_stage;
    rfid_error_command = rc522_debug_command;
    rfid_error_irq = rc522_debug_irq;
    rfid_error_reg = rc522_debug_error_reg;
    rfid_error_len = rc522_debug_rx_len;
    rfid_error_bits = rc522_debug_last_bits;
    rfid_error_registers_valid = rc522_debug_registers_valid;
}

static void RFID_Task(void)
{
    static uint32_t last_poll_ms;
    static uint32_t last_report_ms;
    static uint8_t reported_error;
    RC522_UID uid;
    uint32_t now = HAL_GetTick();

    if ((uint32_t)(now - last_poll_ms) < 30U) return;
    last_poll_ms = now;

    RC522_Status st = RC522_Poll(&rc522, &uid);
    rfid_last_status = st;

    if (st == RC522_OK)
    {
        rfid_last_uid = uid;
        rfid_event_count++;
        reported_error = 0;
        RFID_PrintUID(&uid);
        LED_Start(1, 2000);
    }
    else if (st != RC522_NO_CARD && st != RC522_DUPLICATE)
    {
        SaveError(st);
        now = HAL_GetTick();
        if (!reported_error || (uint32_t)(now - last_report_ms) >= 1500U)
        {
            char text[192];
            const char *detail = (st == RC522_COLLISION)
                ? "multiple cards in field"
                : "see reader status and debug registers";

            (void)snprintf(text, sizeof(text),
                "ERROR=%s DETAIL=%s STAGE=%s CMD=%02X IRQ=%02X ERR=%02X FIFO=%u BIT=%u VALID=%u\r\n",
                RC522_StatusString(st), detail,
                RC522_DebugStageString(rfid_error_stage),
                (unsigned)rfid_error_command, (unsigned)rfid_error_irq,
                (unsigned)rfid_error_reg, (unsigned)rfid_error_len,
                (unsigned)rfid_error_bits,
                (unsigned)rfid_error_registers_valid);
            UART_Print(text);
            LED_Start(st == RC522_COLLISION ? 2 : 3, 150);
            reported_error = 1;
            last_report_ms = now;
        }
    }
}

void RFID_App_Init(SPI_HandleTypeDef *spi, UART_HandleTypeDef *uart)
{
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);
    rfid_uart = uart;

    rc522.spi = spi;
    rc522.cs_port = RC522_CS_GPIO_Port;
    rc522.cs_pin = RC522_CS_Pin;
    rc522.rst_port = RC522_RST_GPIO_Port;
    rc522.rst_pin = RC522_RST_Pin;
    rc522.spi_timeout_ms = 20;
    rc522.command_timeout_ms = 50;
    rc522.removal_ms = 300;

    rfid_init_status = RC522_Init(&rc522);
    uint8_t version = 0;
    RC522_Status version_status = RC522_GetVersion(&rc522, &version);
    rc522_version = version;

    char text[100];
    (void)snprintf(text, sizeof(text),
        "INIT=%s VERSION_READ=%s VERSION=%02X\r\n",
        RC522_StatusString(rfid_init_status),
        RC522_StatusString(version_status), (unsigned)version);
    UART_Print(text);

    if (rfid_init_status != RC522_OK || version_status != RC522_OK)
    {
        rfid_error_status = (rfid_init_status != RC522_OK)
            ? rfid_init_status : version_status;
        
    }

    RC522_SPITest spi_result;
    RC522_Status spi_status = RC522_RunSPIDiagnostic(&rc522, &spi_result);
    rfid_spi_diag = spi_result;
    rfid_spi_diag_status = spi_status;
    (void)snprintf(text, sizeof(text),
        "SPI=%s VERSION_CHANGES=%u FIFO_BAD=%u LEVEL_BAD=%u IO=%u\r\n",
        RC522_StatusString(spi_status),
        (unsigned)spi_result.version_changes,
        (unsigned)spi_result.fifo_mismatches,
        (unsigned)spi_result.fifo_level_errors,
        (unsigned)spi_result.io_errors);
    UART_Print(text);

    // if (spi_status != RC522_OK)
    // {
    //     while (1)
    //     {
    //         HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
    //         HAL_Delay(150);
    //     }
    // }

    for (uint8_t i = 0; i < 2; i++)
    {
        HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_RESET);
        HAL_Delay(120);
        HAL_GPIO_WritePin(GPIOC, GPIO_PIN_13, GPIO_PIN_SET);
        HAL_Delay(120);
    }
    UART_Print("READY: present one card.\r\n");
}

void RFID_App_Task(void)
{
    LED_Task();
    RFID_Task();
    LED_Task();
}
