#ifndef UART_CMD_H
#define UART_CMD_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

/* Lenh qua UART (115200 8N1, ket thuc dong bang Enter):
 *   HELP                          danh sach lenh
 *   TIME                          xem gio
 *   TIME YYYY-MM-DD HH:MM:SS      dat gio RTC                 (*)
 *   LOG                           xuat toan bo nhat ky dang CSV
 *   LOG CLEAR                     xoa nhat ky trong Flash     (*)
 *   LIST                          xuat danh sach the dang CSV
 *   NAME <id> <ho ten>            dat ten cho the              (*)
 *   STAT                          thong ke
 * (*) chi chay khi dang o che do admin (quet the admin truoc),
 *     hoac khi he thong chua co the admin nao.
 * Can bat "USART1 global interrupt" trong CubeMX (NVIC Settings). */
void uart_cmd_init(UART_HandleTypeDef *huart);
void uart_cmd_task(void);           /* goi lien tuc trong vong lap chinh */
void uart_cmd_print(const char *s);

#ifdef __cplusplus
}
#endif

#endif /* UART_CMD_H */