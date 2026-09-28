#ifndef RFID_APP_H
#define RFID_APP_H

#include "main.h"
#include "rc522.h"

#ifdef __cplusplus
extern "C" {
#endif

extern RC522_Handle rc522;
extern volatile uint8_t rc522_version;
extern volatile RC522_Status rfid_init_status;
extern volatile RC522_Status rfid_last_status;
extern volatile RC522_UID rfid_last_uid;
extern volatile uint32_t rfid_event_count;
extern volatile RC522_SPITest rfid_spi_diag;
extern volatile RC522_Status rfid_spi_diag_status;
extern volatile RC522_Status rfid_error_status;
extern volatile RC522_DebugStage rfid_error_stage;
extern volatile uint8_t rfid_error_command;
extern volatile uint8_t rfid_error_irq;
extern volatile uint8_t rfid_error_reg;
extern volatile uint8_t rfid_error_len;
extern volatile uint8_t rfid_error_bits;
extern volatile uint8_t rfid_error_registers_valid;

void RFID_App_Init(SPI_HandleTypeDef *spi, UART_HandleTypeDef *uart);
void RFID_App_Task(void);

#ifdef __cplusplus
}
#endif

#endif
