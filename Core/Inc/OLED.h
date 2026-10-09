/*
 * OLED.h
 *
 *  Created on: 29 thg 9, 2026
 *      Author: PC
 */

#ifndef INC_OLED_H_
#define INC_OLED_H_
#ifdef __cplusplus
extern "C"{
#endif
#include "stm32f1xx_hal.h"
#include "stdint.h"
#include "stdbool.h"

#ifndef OLED_I2C_ADDR
#define OLED_I2C_ADDR     (0x3C << 1)   /* mot so module la 0x3D */
#endif
#ifndef OLED_USE_SH1106
#define OLED_USE_SH1106   0             /* =1 neu dung OLED 1.3" SH1106 */
#endif

#define OLED_WIDTH        128
#define OLED_HEIGHT       64
#define OLED_ROWS         8             /* so dong chu (page) */
#define OLED_COLS         21            /* ky tu/dong font 6x8 */

bool OLED_Init(I2C_HandleTypeDef *hi2c);
bool OLED_IsReady(void);
bool OLED_Update(void);                 /* day framebuffer ra man hinh */


void OLED_Clear(void);
void OLED_ClearRow(uint8_t row);
void OLED_SetCursor(uint8_t x_px, uint8_t row);
void OLED_PutChar(char c);
void OLED_Print(const char *s);
void OLED_PrintLine(uint8_t row, const char *s);      /* xoa dong + in tu trai */
void OLED_PrintCenter(uint8_t row, const char *s);    /* xoa dong + in giua    */
void OLED_Printf(uint8_t row, const char *fmt, ...);
void OLED_PrintLargeCenter(uint8_t row, const char *s); /* chiem 2 dong: row, row+1 */
void OLED_InvertRow(uint8_t row);
void OLED_DrawPixel(uint8_t x, uint8_t y, bool on);

void OLED_SetContrast(uint8_t value);
void OLED_DisplayOn(bool on);


#ifdef __cplusplus

}
#endif

#endif /* INC_OLED_H_ */
