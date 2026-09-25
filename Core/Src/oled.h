/*
 * oled.h
 * Done by: Felix Yuen Pin Qi, A0272059B NUS
 */

#ifndef OLED_H
#define OLED_H

#include "main.h"

#define OLED_WIDTH      128
#define OLED_HEIGHT     64

#define OLED_I2C_ADDR   (0x3C << 1)

void OLED_Init(void);
void OLED_Clear(void);
void OLED_Update(void);

void OLED_SetPixel(uint8_t x, uint8_t y, uint8_t color);

void OLED_DrawLine(int x0, int y0,
                   int x1, int y1);

void OLED_DrawRect(int x, int y,
                   int width, int height);

void OLED_FillRect(int x, int y,
                   int width, int height);

void OLED_DrawCircle(int x0, int y0,
                     int radius);

void OLED_ShowFallDetected(void);

void OLED_ShowSmiley(void);
void OLED_ShowFrown(void);

#endif /* SRC_OLED_H_ */
