#include "rc522.h"
#include <string.h>
#define RFCFG_REG  0x26
#define TX_ASK_REG 0x15

#define ERROR_WRITE          0x80
#define ERROR_TEMPERATURE    0x40
#define ERROR_BUFFER_OVERFLOW 0x10
#define ERROR_COLLISION      0x08
#define ERROR_CRC            0x04
#define ERROR_PARITY         0x02
#define ERROR_PROTOCOL       0x01

/* MFRC522 registers used by this driver. */
enum {
    COMMAND      = 0x01,
    COM_IRQ      = 0x04,
    ERROR_REG    = 0x06,
    STATUS2      = 0x08,
    FIFO_DATA    = 0x09,
    FIFO_LEVEL   = 0x0A,
    CONTROL      = 0x0C,
    BIT_FRAMING  = 0x0D,
    COLL          = 0x0E,

    MODE          = 0x11,
    TX_MODE       = 0x12,
    RX_MODE       = 0x13,
    TX_CONTROL    = 0x14,
    TX_ASK        = 0x15,
	
	

    MOD_WIDTH     = 0x24,
    T_MODE        = 0x2A,
    T_PRESCALER  = 0x2B,
    T_RELOAD_H   = 0x2C,
    T_RELOAD_L   = 0x2D,

    VERSION       = 0x37
};

/* MFRC522 commands. */
enum {
    CMD_IDLE        = 0x00,
    CMD_MEM         = 0x01,
    CMD_CALC_CRC    = 0x03,
    CMD_TRANSCEIVE  = 0x0C,
    CMD_MFAUTHENT   = 0x0E,
    CMD_SOFTRESET   = 0x0F
};

#define TRY(x) do {                    \
    RC522_Status st_ = (x);            \
    if (st_ != RC522_OK) return st_;   \
} while (0)

volatile RC522_DebugStage rc522_debug_stage = RC522_DBG_NONE;
volatile uint8_t rc522_debug_rx_len = 0;
volatile uint8_t rc522_debug_last_bits = 0;
volatile uint8_t rc522_debug_irq = 0;
volatile uint8_t rc522_debug_error_reg = 0;
volatile uint8_t rc522_debug_command = 0;
volatile uint8_t rc522_debug_registers_valid = 0;

/* -------------------------------------------------------------------------- */
/* Low-level helpers                                                          */
/* -------------------------------------------------------------------------- */

static int valid(const RC522_Handle *d)
{
    return d &&
           d->spi &&
           d->cs_port &&
           d->rst_port &&
           d->cs_pin &&
           d->rst_pin;
}


static RC522_Status reg_io(
    RC522_Handle *d,
    uint8_t reg,
    uint8_t *value,
    int read)
{
    uint8_t tx[2];
    uint8_t rx[2] = {0, 0};

    if (!valid(d) || !value)
        return RC522_BAD_ARG;

    /*
     * MFRC522 SPI address format:
     * bit7 = 1 for read, 0 for write
     * bits6..1 = register address
     * bit0 = 0
     */
    tx[0] = (uint8_t)(((reg << 1) & 0x7E) | (read ? 0x80 : 0x00));
    tx[1] = read ? 0x00 : *value;

    HAL_GPIO_WritePin(
        d->cs_port,
        d->cs_pin,
        GPIO_PIN_RESET
    );

    HAL_StatusTypeDef hs = HAL_SPI_TransmitReceive(
        d->spi,
        tx,
        rx,
        2,
        d->spi_timeout_ms
    );

    HAL_GPIO_WritePin(
        d->cs_port,
        d->cs_pin,
        GPIO_PIN_SET
    );

    if (hs != HAL_OK)
        return RC522_IO_ERROR;

    if (read)
        *value = rx[1];

    return RC522_OK;
}


static RC522_Status wr(
    RC522_Handle *d,
    uint8_t reg,
    uint8_t value)
{
    return reg_io(d, reg, &value, 0);
}


static RC522_Status rd(
    RC522_Handle *d,
    uint8_t reg,
    uint8_t *value)
{
    return reg_io(d, reg, value, 1);
}


static RC522_Status set_bits(
    RC522_Handle *d,
    uint8_t reg,
    uint8_t bits)
{
    uint8_t value;

    TRY(rd(d, reg, &value));

    value |= bits;

    return wr(d, reg, value);
}


static RC522_Status clear_bits(
    RC522_Handle *d,
    uint8_t reg,
    uint8_t bits)
{
    uint8_t value;

    TRY(rd(d, reg, &value));

    value &= (uint8_t)~bits;

    return wr(d, reg, value);
}

static RC522_Status transceive_error_status(uint8_t error)
{
    if (error & ERROR_COLLISION)
        return RC522_COLLISION;
    if (error & ERROR_CRC)
        return RC522_CRC_ERROR;
    if (error & (ERROR_BUFFER_OVERFLOW | ERROR_PARITY | ERROR_PROTOCOL))
        return RC522_PROTOCOL_ERROR;
    if (error & (ERROR_WRITE | ERROR_TEMPERATURE))
        return RC522_DEVICE_ERROR;

    return RC522_OK;
}

static RC522_Status recover_rf(RC522_Handle *d)
{
    TRY(wr(d, COMMAND, CMD_IDLE));
    TRY(clear_bits(d, BIT_FRAMING, 0x80));
    TRY(wr(d, FIFO_LEVEL, 0x80));
    TRY(clear_bits(d, STATUS2, 0x08));
    TRY(clear_bits(d, TX_CONTROL, 0x03));
    HAL_Delay(2);
    return set_bits(d, TX_CONTROL, 0x03);
}


/* -------------------------------------------------------------------------- */
/* ISO14443A CRC_A                                                            */
/* -------------------------------------------------------------------------- */

static uint16_t crc_a(
    const uint8_t *data,
    uint8_t length)
{
    uint16_t crc = 0x6363;

    while (length--)
    {
        uint8_t x = (uint8_t)(*data++ ^ crc);

        x ^= (uint8_t)(x << 4);

        crc = (uint16_t)(
            (crc >> 8) ^
            ((uint16_t)x << 8) ^
            ((uint16_t)x << 3) ^
            (x >> 4)
        );
    }

    return crc;
}


static void append_crc(
    uint8_t *data,
    uint8_t length)
{
    uint16_t crc = crc_a(data, length);

    data[length]     = (uint8_t)crc;
    data[length + 1] = (uint8_t)(crc >> 8);
}


static int crc_ok(
    const uint8_t *data,
    uint8_t length)
{
    if (length < 2)
        return 0;

    uint16_t crc = crc_a(
        data,
        (uint8_t)(length - 2)
    );

    return data[length - 2] == (uint8_t)crc &&
           data[length - 1] == (uint8_t)(crc >> 8);
}


/* -------------------------------------------------------------------------- */
/* Core command exchange                                                      */
/* -------------------------------------------------------------------------- */

static RC522_Status exchange(
    RC522_Handle *d,
    uint8_t command,
    const uint8_t *tx,
    uint8_t tx_len,
    uint8_t tx_last_bits,
    uint8_t *rx,
    uint8_t *rx_len,
    uint8_t *rx_last_bits)
{
    uint8_t irq = 0, err = 0, fifo_count = 0, control = 0;
    uint8_t capacity = rx_len ? *rx_len : 0;
    RC522_Status result = RC522_TIMEOUT;
    RC522_Status s;

    if (rx_len) *rx_len = 0;
    if (rx_last_bits) *rx_last_bits = 0;
    rc522_debug_rx_len = 0;
    rc522_debug_last_bits = 0;
    rc522_debug_irq = 0;
    rc522_debug_error_reg = 0;
    rc522_debug_command = (tx && tx_len) ? tx[0] : 0;
    rc522_debug_registers_valid = 0;

    if (!valid(d) || !tx || tx_len == 0 || tx_len > 64 || tx_last_bits > 7)
        return RC522_BAD_ARG;
    if (command != CMD_TRANSCEIVE && command != CMD_MFAUTHENT)
        return RC522_BAD_ARG;
    if (command == CMD_TRANSCEIVE && (!rx || !rx_len || !capacity))
        return RC522_BAD_ARG;

    TRY(wr(d, COMMAND, CMD_IDLE));
    TRY(wr(d, COM_IRQ, 0x7F));
    TRY(wr(d, FIFO_LEVEL, 0x80));
    TRY(wr(d, BIT_FRAMING, 0x00));

    for (uint8_t i = 0; i < tx_len; i++)
        TRY(wr(d, FIFO_DATA, tx[i]));

    TRY(wr(d, BIT_FRAMING, (uint8_t)(tx_last_bits & 0x07)));
    TRY(wr(d, COMMAND, command));

    if (command == CMD_TRANSCEIVE)
    {
        s = set_bits(d, BIT_FRAMING, 0x80);
        if (s != RC522_OK)
        {
            (void)wr(d, COMMAND, CMD_IDLE);
            return s;
        }
    }

    uint32_t start = HAL_GetTick();
    while ((uint32_t)(HAL_GetTick() - start) < d->command_timeout_ms)
    {
        s = rd(d, COM_IRQ, &irq);
        if (s != RC522_OK)
        {
            result = s;
            break;
        }

        rc522_debug_irq = irq;

        if (irq & 0x01)
        {
            result = RC522_TIMEOUT;
            break;
        }

        if ((command == CMD_MFAUTHENT && (irq & 0x10)) ||
            (command == CMD_TRANSCEIVE && (irq & 0x20)))
        {
            result = RC522_OK;
            break;
        }
    }

    s = rd(d, ERROR_REG, &err);
    if (s == RC522_OK) s = rd(d, FIFO_LEVEL, &fifo_count);
    if (s == RC522_OK) s = rd(d, CONTROL, &control);
    if (s != RC522_OK)
    {
        (void)wr(d, COMMAND, CMD_IDLE);
        if (command == CMD_TRANSCEIVE)
            (void)clear_bits(d, BIT_FRAMING, 0x80);
        return s;
    }

    rc522_debug_error_reg = err;
    rc522_debug_rx_len = fifo_count;
    rc522_debug_last_bits = (uint8_t)(control & 0x07);
    rc522_debug_registers_valid = 1;

    if (rx_len) *rx_len = fifo_count;
    if (rx_last_bits) *rx_last_bits = (uint8_t)(control & 0x07);

    if (err != 0U)
    {
        if (err & ERROR_COLLISION)
            result = RC522_COLLISION;
        else if (err & ERROR_CRC)
            result = RC522_CRC_ERROR;
        else if (err & (ERROR_PARITY | ERROR_PROTOCOL | ERROR_BUFFER_OVERFLOW))
            result = RC522_PROTOCOL_ERROR;
        else if (err & ERROR_WRITE)
            result = RC522_DEVICE_ERROR;
        else if (err & ERROR_TEMPERATURE)
            result = RC522_DEVICE_ERROR;
        else
            result = RC522_PROTOCOL_ERROR;
    }
    else if (command == CMD_TRANSCEIVE && fifo_count > capacity)
    {
        result = RC522_PROTOCOL_ERROR;
    }
    else if (result == RC522_TIMEOUT)
    {
        result = RC522_TIMEOUT;
    }
    else if (command == CMD_TRANSCEIVE && fifo_count == 0U)
    {
        result = RC522_PROTOCOL_ERROR;
    }

    if (result != RC522_OK)
    {
        (void)wr(d, COMMAND, CMD_IDLE);
        if (command == CMD_TRANSCEIVE)
            (void)clear_bits(d, BIT_FRAMING, 0x80);
        return result;
    }

    if (command == CMD_MFAUTHENT)
    {
        (void)wr(d, COMMAND, CMD_IDLE);
        return RC522_OK;
    }

    if (fifo_count == 0U || fifo_count > capacity)
    {
        (void)wr(d, COMMAND, CMD_IDLE);
        (void)clear_bits(d, BIT_FRAMING, 0x80);
        return RC522_PROTOCOL_ERROR;
    }

    for (uint8_t i = 0; i < fifo_count; i++)
    {
        s = rd(d, FIFO_DATA, &rx[i]);
        if (s != RC522_OK)
        {
            (void)wr(d, COMMAND, CMD_IDLE);
            (void)clear_bits(d, BIT_FRAMING, 0x80);
            return RC522_IO_ERROR;
        }
    }

    TRY(wr(d, COMMAND, CMD_IDLE));
    TRY(clear_bits(d, BIT_FRAMING, 0x80));

    return RC522_OK;
}





/* -------------------------------------------------------------------------- */
/* Public API                                                                 */
/* -------------------------------------------------------------------------- */

RC522_Status RC522_GetVersion(
    RC522_Handle *d,
    uint8_t *version)
{
    if (!valid(d) || !version)
        return RC522_BAD_ARG;

    return rd(d, VERSION, version);
}


RC522_Status RC522_Init(RC522_Handle *d)
{
    if (!valid(d))
        return RC522_BAD_ARG;

    d->initialized = 0;
    d->latched = 0;
    d->absent_pending = 0;

    if (!d->spi_timeout_ms)
        d->spi_timeout_ms = 20;

    if (!d->command_timeout_ms)
        d->command_timeout_ms = 50;

    if (!d->removal_ms)
        d->removal_ms = 300;

    HAL_GPIO_WritePin(
        d->cs_port,
        d->cs_pin,
        GPIO_PIN_SET
    );

    /* Hardware reset pulse. */
    HAL_GPIO_WritePin(
        d->rst_port,
        d->rst_pin,
        GPIO_PIN_RESET
    );

    HAL_Delay(2);

    HAL_GPIO_WritePin(
        d->rst_port,
        d->rst_pin,
        GPIO_PIN_SET
    );

    HAL_Delay(50);

    /* Software reset. */
    TRY(wr(d, COMMAND, CMD_SOFTRESET));
    HAL_Delay(50);

    /* Wait until PowerDown bit in CommandReg clears. */
    uint8_t command = 0;
    uint32_t start = HAL_GetTick();

    do
    {
        TRY(rd(d, COMMAND, &command));

        if (!(command & 0x10))
            break;

        if ((uint32_t)(HAL_GetTick() - start) >= 100)
            return RC522_TIMEOUT;

    } while (1);

    /*
     * Do not require only 0x91/0x92 here:
     * many RC522-compatible clone modules report another value.
     * 0x00 and 0xFF normally indicate broken/no SPI communication.
     */
    uint8_t version = 0;

    TRY(rd(d, VERSION, &version));

    if (version == 0x00 || version == 0xFF)
        return RC522_DEVICE_ERROR;

    /* ISO14443A / 106 kBd baseline configuration. */
    TRY(wr(d, TX_MODE, 0x00));
    TRY(wr(d, RX_MODE, 0x00));
    TRY(wr(d, MOD_WIDTH, 0x26));

    /*
     * Timer ~25 ms:
     * f_timer = 13.56 MHz / (2*169 + 1) ~= 40 kHz
     * reload 1000 -> ~25 ms.
     */
    TRY(wr(d, T_MODE, 0x80));
    TRY(wr(d, T_PRESCALER, 0xA9));
    TRY(wr(d, T_RELOAD_H, 0x03));
    TRY(wr(d, T_RELOAD_L, 0xE8));

    TRY(wr(d, TX_ASK, 0x40));
    TRY(wr(d, MODE, 0x3D));

   /* Antenna on */
TRY(set_bits(
    d,
    TX_CONTROL,
    0x03
));


/* Tang c�ng su?t RF cho RC522 clone */
TRY(wr(
    d,
    RFCFG_REG,
    0x70
));


/* Enable 100% ASK */
TRY(wr(
    d,
    TX_ASK,
    0x40
));


HAL_Delay(5);

d->initialized = 1;

return RC522_OK;
}


RC522_Status RC522_StopCrypto1(RC522_Handle *d)
{
    if (!valid(d) || !d->initialized)
        return RC522_BAD_ARG;

    return clear_bits(d, STATUS2, 0x08);
}


RC522_Status RC522_RunSPIDiagnostic(RC522_Handle *d, RC522_SPITest *out)
{
    RC522_Status result = RC522_OK;
    RC522_Status cleanup;
    uint8_t value;

    if (!out) return RC522_BAD_ARG;
    memset(out, 0, sizeof(*out));
    out->first_bad_index = 0xFFFF;
    if (!valid(d) || !d->initialized) return RC522_BAD_ARG;

    /* Preserve the first failure, but still try to leave the FIFO empty. */
#define DIAG_IO(call) do {                         \
    result = (call);                              \
    if (result != RC522_OK) {                      \
        if (result == RC522_IO_ERROR)              \
            out->io_errors++;                     \
        goto diag_cleanup;                        \
    }                                             \
} while (0)

    DIAG_IO(wr(d, COMMAND, CMD_IDLE));
    for (uint16_t i = 0; i < 100; i++)
    {
        DIAG_IO(rd(d, VERSION, &value));
        if (i == 0) out->version_first = value;
        else if (value != out->version_first) out->version_changes++;
        out->version_last = value;
        out->version_reads++;
        HAL_Delay(1);
    }

    DIAG_IO(rd(d, TX_MODE, &out->tx_mode));
    DIAG_IO(rd(d, RX_MODE, &out->rx_mode));
    DIAG_IO(rd(d, TX_ASK, &out->tx_ask));
    DIAG_IO(rd(d, TX_CONTROL, &out->tx_control));
    DIAG_IO(rd(d, MODE, &out->mode));
    DIAG_IO(rd(d, BIT_FRAMING, &out->bit_framing));

    /* In Idle, FIFO writes/reads exercise the SPI path without a card.
     * Cover every byte value, respecting the chip's 64-byte FIFO capacity. */
    for (uint16_t batch = 0; batch < 4; batch++)
    {
        DIAG_IO(wr(d, FIFO_LEVEL, 0x80));
        DIAG_IO(rd(d, FIFO_LEVEL, &value));
        if (value != 0) out->fifo_level_errors++;

        for (uint16_t i = 0; i < 64; i++)
            DIAG_IO(wr(d, FIFO_DATA, (uint8_t)(batch * 64U + i)));

        DIAG_IO(rd(d, FIFO_LEVEL, &value));
        if (value != 64) out->fifo_level_errors++;

        for (uint16_t i = 0; i < 64; i++)
        {
            uint16_t index = (uint16_t)(batch * 64U + i);
            uint8_t expected = (uint8_t)index;
            DIAG_IO(rd(d, FIFO_DATA, &value));
            out->fifo_bytes_tested++;
            if (value != expected)
            {
                if (out->fifo_mismatches == 0)
                {
                    out->first_bad_index = index;
                    out->first_bad_expected = expected;
                    out->first_bad_actual = value;
                }
                out->fifo_mismatches++;
            }
        }
        DIAG_IO(rd(d, FIFO_LEVEL, &value));
        if (value != 0) out->fifo_level_errors++;
    }
    out->completed = 1;
    if (out->version_changes || out->fifo_mismatches || out->fifo_level_errors)
        result = RC522_PROTOCOL_ERROR;
    else if (out->version_first == 0x00 || out->version_first == 0xFF)
        result = RC522_DEVICE_ERROR;

diag_cleanup:
#undef DIAG_IO
    cleanup = wr(d, COMMAND, CMD_IDLE);
    if (cleanup != RC522_OK)
    {
        if (cleanup == RC522_IO_ERROR) out->io_errors++;
        result = cleanup;
        out->completed = 0;
    }
    cleanup = wr(d, FIFO_LEVEL, 0x80);
    if (cleanup != RC522_OK)
    {
        if (cleanup == RC522_IO_ERROR) out->io_errors++;
        result = cleanup;
        out->completed = 0;
    }
    return result;
}


static RC522_Status request_a(
    RC522_Handle *d,
    uint8_t command,
    uint8_t atqa[2])
{
    uint8_t rx[2] = {0, 0};
    uint8_t rx_len = sizeof(rx);
    uint8_t rx_last_bits = 0;

    /*
     * REQA/WUPA is not the bitwise anticollision phase.
     * Keep ValuesAfterColl = 1.
     */
    TRY(set_bits(d, COLL, 0x80));

    RC522_Status s = exchange(
        d,
        CMD_TRANSCEIVE,
        &command,
        1,
        7,
        rx,
        &rx_len,
        &rx_last_bits
    );

    if (s != RC522_OK)
        return s;

    if (rx_len != 2)
    {
        rc522_debug_stage = RC522_DBG_ATQA_LENGTH;
        return RC522_PROTOCOL_ERROR;
    }

    if (rx_last_bits != 0)
    {
        rc522_debug_stage = RC522_DBG_ATQA_BITS;
        return RC522_PROTOCOL_ERROR;
    }

    if (atqa)
    {
        atqa[0] = rx[0];
        atqa[1] = rx[1];
    }

    return RC522_OK;
}


RC522_Status RC522_ReadUID(
    RC522_Handle *d,
    RC522_UID *uid)
{
    rc522_debug_stage = RC522_DBG_NONE;
    rc522_debug_rx_len = 0;
    rc522_debug_last_bits = 0;
    rc522_debug_irq = 0;
    rc522_debug_error_reg = 0;
    rc522_debug_command = 0;
    rc522_debug_registers_valid = 0;

    if (!valid(d) || !d->initialized || !uid)
    {
        rc522_debug_stage = RC522_DBG_BAD_HANDLE;
        return RC522_BAD_ARG;
    }

    RC522_UID out;
    memset(&out, 0, sizeof(out));

    uint8_t rx[5];
    uint8_t rx_len;
    uint8_t rx_last_bits;
    RC522_Status s;
    uint8_t atqa[2];

    TRY(RC522_StopCrypto1(d));

    /*
     * First try REQA (0x26) for a card in IDLE state.
     * If the card was previously halted, WUPA (0x52) wakes it.
     */
    rc522_debug_stage = RC522_DBG_WAKEUP;

    s = request_a(d, 0x26, atqa);

    if (s == RC522_TIMEOUT)
        s = request_a(d, 0x52, atqa);

    if (s == RC522_TIMEOUT)
    {
        return RC522_NO_CARD;
    }

    if (s != RC522_OK)
    {
        RC522_Status recover_status = recover_rf(d);
        if (recover_status != RC522_OK)
            return recover_status;
        return s;
    }

    rc522_debug_stage = RC522_DBG_ATQA;

    /*
     * Bitwise anticollision phase:
     * ValuesAfterColl = 0.
     */
    TRY(clear_bits(d, COLL, 0x80));

   for (uint8_t level = 0; level < 3; level++)
{


    uint8_t sel = (uint8_t)(0x93 + (2 * level));

    uint8_t frame[9] = {
        sel,
        0x20,
        0,0,0,0,0,0,0
    };
        rc522_debug_stage = RC522_DBG_ANTICOLL;

        rx_len = 5;
        rx_last_bits = 0;

        s = exchange(
            d,
            CMD_TRANSCEIVE,
            frame,
            2,
            0,
            rx,
            &rx_len,
            &rx_last_bits
        );

        if (s != RC522_OK)
        {
            (void)set_bits(d, COLL, 0x80);
            return s;
        }

        if (rx_len != 5 || rx_last_bits != 0)
        {
            (void)set_bits(d, COLL, 0x80);
            return RC522_PROTOCOL_ERROR;
        }

        rc522_debug_stage = RC522_DBG_BCC;

        if ((uint8_t)(rx[0] ^ rx[1] ^ rx[2] ^ rx[3]) != rx[4])
        {
            (void)set_bits(d, COLL, 0x80);
            return RC522_PROTOCOL_ERROR;
        }

        uint8_t cascade = (rx[0] == 0x88);

        if (cascade && level == 2)
        {
            (void)set_bits(d, COLL, 0x80);
            return RC522_PROTOCOL_ERROR;
        }

        /*
         * SELECT is no longer the bitwise anticollision operation.
         * Restore ValuesAfterColl = 1.
         */
       TRY(set_bits(d, COLL, 0x80));

				TRY(wr(
						d,
						BIT_FRAMING,
						0x00
				));

				frame[0] = sel;
				frame[1] = 0x70;

				memcpy(frame + 2, rx, 5);
				append_crc(frame, 7);

        rc522_debug_stage = RC522_DBG_SELECT;

        rx_len = 3;
        rx_last_bits = 0;

        s = exchange(
            d,
            CMD_TRANSCEIVE,
            frame,
            9,
            0,
            rx,
            &rx_len,
            &rx_last_bits
        );

        if (s != RC522_OK)
            return s;

        if (rx_len != 3 || rx_last_bits != 0)
            return RC522_PROTOCOL_ERROR;

        if (!crc_ok(rx, 3))
            return RC522_CRC_ERROR;

        rc522_debug_stage = RC522_DBG_SAK;

        if (((rx[0] & 0x04) ? 1U : 0U) != (cascade ? 1U : 0U))
            return RC522_PROTOCOL_ERROR;

        uint8_t count = cascade ? 3 : 4;

        if ((uint8_t)(out.size + count) > sizeof(out.bytes))
            return RC522_PROTOCOL_ERROR;

        memcpy(
            out.bytes + out.size,
            frame + 2 + (cascade ? 1 : 0),
            count
        );

        out.size = (uint8_t)(out.size + count);

        if (!cascade)
        {
            out.sak = rx[0];
            *uid = out;

            rc522_debug_stage = RC522_DBG_DONE;

            return RC522_OK;
        }

        /*
         * A cascaded UID continues with a new anticollision level.
         */
        TRY(clear_bits(d, COLL, 0x80));
    }

    (void)set_bits(d, COLL, 0x80);

    return RC522_PROTOCOL_ERROR;
}


RC522_Status RC522_Halt(RC522_Handle *d)
{
    if (!valid(d) || !d->initialized)
        return RC522_BAD_ARG;

    uint8_t cmd[4] = {0x50, 0x00, 0x00, 0x00};
    uint8_t rx[2];
    uint8_t rx_len = sizeof(rx);
    uint8_t last_bits = 0;

    append_crc(cmd, 2);

    RC522_Status s = exchange(
        d,
        CMD_TRANSCEIVE,
        cmd,
        4,
        0,
        rx,
        &rx_len,
        &last_bits
    );

    /*
     * A compliant PICC does not answer HLTA,
     * therefore timeout means success here.
     */
    if (s == RC522_TIMEOUT)
        return RC522_OK;

    if (s == RC522_OK)
        return RC522_PROTOCOL_ERROR;

    return s;
}


RC522_Status RC522_Poll(
    RC522_Handle *d,
    RC522_UID *uid)
{
    if (!valid(d) || !d->initialized || !uid)
        return RC522_BAD_ARG;

    RC522_UID current;
    RC522_Status s = RC522_ReadUID(d, &current);
    uint32_t now = HAL_GetTick();

    if (s == RC522_NO_CARD)
    {
        if (!d->absent_pending)
        {
            d->absent_pending = 1;
            d->absent_since = now;
            return RC522_NO_CARD;
        }

        if ((uint32_t)(now - d->absent_since) >= d->removal_ms)
            d->latched = 0;

        return RC522_NO_CARD;
    }

    d->absent_pending = 0;

    if (s != RC522_OK)
        return s;

    s = RC522_Halt(d);
    if (s != RC522_OK)
    {
        rc522_debug_stage = RC522_DBG_HALT;
        (void)recover_rf(d);
        return s;
    }

    *uid = current;

    if (d->latched &&
        current.size == d->last_uid.size &&
        !memcmp(
            current.bytes,
            d->last_uid.bytes,
            current.size))
    {
        return RC522_DUPLICATE;
    }

    d->last_uid = current;
    d->latched = 1;

    return RC522_OK;
}


RC522_Status RC522_Authenticate(
    RC522_Handle *d,
    uint8_t block,
    uint8_t key_b,
    const uint8_t key[6],
    const RC522_UID *uid)
{
    if (!valid(d) ||
        !d->initialized ||
        !key ||
        !uid ||
        key_b > 1 ||
        (uid->size != 4 &&
         uid->size != 7 &&
         uid->size != 10))
    {
        return RC522_BAD_ARG;
    }

    uint8_t cmd[12];
    uint8_t status2 = 0;

    cmd[0] = key_b ? 0x61 : 0x60;
    cmd[1] = block;

    memcpy(cmd + 2, key, 6);
    memcpy(cmd + 8, uid->bytes + uid->size - 4, 4);

    RC522_Status s = exchange(
        d,
        CMD_MFAUTHENT,
        cmd,
        12,
        0,
        NULL,
        NULL,
        NULL
    );

    if (s != RC522_OK)
    {
        (void)RC522_StopCrypto1(d);
        return s;
    }

    TRY(rd(d, STATUS2, &status2));

    return (status2 & 0x08)
        ? RC522_OK
        : RC522_AUTH_ERROR;
}


RC522_Status RC522_ReadBlock(
    RC522_Handle *d,
    uint8_t block,
    uint8_t data[16])
{
    if (!valid(d) || !d->initialized || !data)
        return RC522_BAD_ARG;

    uint8_t cmd[4] = {0x30, block, 0, 0};
    uint8_t rx[18];

    uint8_t rx_len = sizeof(rx);
    uint8_t last_bits = 0;

    append_crc(cmd, 2);

    TRY(exchange(
        d,
        CMD_TRANSCEIVE,
        cmd,
        4,
        0,
        rx,
        &rx_len,
        &last_bits
    ));

    if (rx_len == 1 && last_bits == 4)
        return RC522_NACK;

    if (rx_len != 18 || last_bits != 0)
        return RC522_PROTOCOL_ERROR;

    if (!crc_ok(rx, rx_len))
        return RC522_CRC_ERROR;

    memcpy(data, rx, 16);

    return RC522_OK;
}


static RC522_Status check_ack(
    RC522_Handle *d,
    uint8_t *frame,
    uint8_t length)
{
    uint8_t rx[2];
    uint8_t rx_len = sizeof(rx);
    uint8_t last_bits = 0;

    TRY(exchange(
        d,
        CMD_TRANSCEIVE,
        frame,
        length,
        0,
        rx,
        &rx_len,
        &last_bits
    ));

    if (rx_len != 1 || last_bits != 4)
        return RC522_PROTOCOL_ERROR;

    return ((rx[0] & 0x0F) == 0x0A)
        ? RC522_OK
        : RC522_NACK;
}


RC522_Status RC522_WriteBlock(
    RC522_Handle *d,
    uint8_t block,
    const uint8_t data[16])
{
    if (!valid(d) || !d->initialized || !data)
        return RC522_BAD_ARG;

    /*
     * Refuse manufacturer block 0 and sector trailer blocks.
     * MIFARE Classic 1K/4K layout.
     */
    if (block == 0)
        return RC522_BAD_ARG;

    if (block < 128)
    {
        if ((block % 4) == 3)
            return RC522_BAD_ARG;
    }
    else
    {
        if (((block - 128) % 16) == 15)
            return RC522_BAD_ARG;
    }

    uint8_t cmd[4] = {0xA0, block, 0, 0};
    uint8_t frame[18];

    append_crc(cmd, 2);

    TRY(check_ack(d, cmd, 4));

    memcpy(frame, data, 16);
    append_crc(frame, 16);

    return check_ack(d, frame, 18);
}


const char *RC522_StatusString(RC522_Status s)
{
    static const char *const names[] = {
        "OK",
        "NO_CARD",
        "DUPLICATE",
        "TIMEOUT",
        "IO_ERROR",
        "PROTOCOL_ERROR",
        "COLLISION",
        "CRC_ERROR",
        "AUTH_ERROR",
        "NACK",
        "BAD_ARG",
        "DEVICE_ERROR"
    };

    if ((unsigned)s >=
        (sizeof(names) / sizeof(names[0])))
    {
        return "UNKNOWN";
    }

    return names[s];
}


const char *RC522_DebugStageString(RC522_DebugStage s)
{
    switch (s)
    {
        case RC522_DBG_NONE:       return "NONE";
        case RC522_DBG_BAD_HANDLE: return "BAD_HANDLE";
        case RC522_DBG_WAKEUP:     return "WAKEUP";
        case RC522_DBG_ATQA:       return "ATQA";
        case RC522_DBG_ANTICOLL:   return "ANTICOLL";
        case RC522_DBG_BCC:        return "BCC";
        case RC522_DBG_SELECT:      return "SELECT";
        case RC522_DBG_SAK:         return "SAK";
        case RC522_DBG_ATQA_LENGTH: return "ATQA_LENGTH";
        case RC522_DBG_ATQA_BITS:   return "ATQA_BITS";
        case RC522_DBG_HALT:        return "HALT";
        case RC522_DBG_DONE:        return "DONE";
        default:                   return "UNKNOWN";
    }
}
