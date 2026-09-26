#ifndef RC522_H
#define RC522_H

#include "main.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    RC522_OK = 0, RC522_NO_CARD, RC522_DUPLICATE, RC522_TIMEOUT,
    RC522_IO_ERROR, RC522_PROTOCOL_ERROR, RC522_COLLISION,
    RC522_CRC_ERROR, RC522_AUTH_ERROR, RC522_NACK,
    RC522_BAD_ARG, RC522_DEVICE_ERROR
} RC522_Status;

typedef struct {
    uint8_t bytes[10];
    uint8_t size;
    uint8_t sak;
} RC522_UID;

typedef enum {
    RC522_DBG_NONE = 0,
    RC522_DBG_BAD_HANDLE = 1,
    RC522_DBG_WAKEUP = 2,
    RC522_DBG_ATQA = 3,
    RC522_DBG_ANTICOLL = 4,
    RC522_DBG_BCC = 5,
    RC522_DBG_SELECT = 6,
    RC522_DBG_SAK = 7,
    RC522_DBG_ATQA_LENGTH = 8,
    RC522_DBG_ATQA_BITS = 9,
    RC522_DBG_HALT = 10,
    RC522_DBG_DONE = 20
} RC522_DebugStage;

typedef struct {
    SPI_HandleTypeDef *spi;
    GPIO_TypeDef *cs_port;
    GPIO_TypeDef *rst_port;
    uint16_t cs_pin;
    uint16_t rst_pin;
    uint32_t spi_timeout_ms;
    uint32_t command_timeout_ms;
    uint32_t removal_ms;
    RC522_UID last_uid;
    uint32_t absent_since;
    uint8_t latched;
    uint8_t absent_pending;
    uint8_t initialized;
} RC522_Handle;

/* Startup-only SPI/FIFO check. It does not send RF commands or write a card.
 * version_changes counts samples differing from the first successful sample.
 * completed means all planned checks ran, not that every check passed.
 * Register snapshots are diagnostic information, not authenticity checks. */
typedef struct {
    uint16_t version_reads;
    uint16_t version_changes;
    uint16_t fifo_bytes_tested;
    uint16_t fifo_mismatches;
    uint16_t fifo_level_errors;
    uint16_t io_errors;
    uint16_t first_bad_index; /* 0xFFFF if no FIFO data mismatch */
    uint8_t first_bad_expected;
    uint8_t first_bad_actual;
    uint8_t version_first;
    uint8_t version_last;
    uint8_t tx_mode;
    uint8_t rx_mode;
    uint8_t tx_ask;
    uint8_t tx_control;
    uint8_t mode;
    uint8_t bit_framing;
    uint8_t completed;
} RC522_SPITest;

/* Snapshot of the last RF command, not necessarily the last ReadUID call.
 * rx_len is the actual FIFOLevel register value, including on frame errors.
 * On timeout, FIFO contents are not proof of a received frame.
 * registers_valid=1 only after Error/FIFOLevel/Control were all read.
 * These global diagnostics are intended for one reader in one task. */
extern volatile RC522_DebugStage rc522_debug_stage;
extern volatile uint8_t rc522_debug_rx_len;
extern volatile uint8_t rc522_debug_last_bits;
extern volatile uint8_t rc522_debug_irq;
extern volatile uint8_t rc522_debug_error_reg;
extern volatile uint8_t rc522_debug_command;
extern volatile uint8_t rc522_debug_registers_valid;

/* Call after HAL_Init(), MX_GPIO_Init(), MX_SPIx_Init(). Zero-init the handle. */
RC522_Status RC522_Init(RC522_Handle *d);
RC522_Status RC522_GetVersion(RC522_Handle *d, uint8_t *version);
/* Call once immediately after successful Init, BEFORE any card operations.
 * Destructively uses/clears the chip FIFO; no concurrent reader task allowed.
 * Checks 100 version reads and 256 FIFO bytes in four 64-byte batches.
 * OK verifies only the sampled SPI/FIFO operations, not RF/card operation. */
RC522_Status RC522_RunSPIDiagnostic(RC522_Handle *d, RC522_SPITest *out);
/* Selects one ISO14443A card, supporting 4/7/10-byte UID.
 * No duplicate filter; leaves the card selected for MIFARE authentication.
 * Multiple cards are not enumerated: collisions are returned as errors. */
RC522_Status RC522_ReadUID(RC522_Handle *d, RC522_UID *uid);
/* Reads + halts + filters duplicates. Poll continuously.
 * OK=new event, DUPLICATE=same UID held, NO_CARD=no response to REQA/WUPA.
 * Same UID rearms after observed absence >= removal_ms; errors do not rearm.
 * If Halt fails, Poll reports its error and does not change the UID latch.
 * uid is valid only for OK/DUPLICATE. This latch is RAM-only. */
RC522_Status RC522_Poll(RC522_Handle *d, RC522_UID *uid);
RC522_Status RC522_Halt(RC522_Handle *d);
RC522_Status RC522_StopCrypto1(RC522_Handle *d);
/* Call ReadUID before Authenticate. key_b=0 (A), key_b=1 (B).
 * Finish the authenticated session with Halt, then StopCrypto1. */
RC522_Status RC522_Authenticate(RC522_Handle *d, uint8_t block,
    uint8_t key_b, const uint8_t key[6], const RC522_UID *uid);
RC522_Status RC522_ReadBlock(RC522_Handle *d, uint8_t block, uint8_t data[16]);
/* MIFARE Classic only. Refuses block 0 and 1K/4K sector trailers.
 * Caller is responsible for card capacity, sector key and access rights. */
RC522_Status RC522_WriteBlock(RC522_Handle *d, uint8_t block, const uint8_t data[16]);
const char *RC522_StatusString(RC522_Status s);
const char *RC522_DebugStageString(RC522_DebugStage s);

#ifdef __cplusplus
}
#endif
#endif
