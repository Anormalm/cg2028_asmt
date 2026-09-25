#include "oled.h"

#include "stm32l4xx_hal.h"
#include <stdint.h>


extern I2C_HandleTypeDef hi2c1;

#define OLED_WIDTH              128
#define OLED_HEIGHT             64
#define OLED_I2C_ADDR           (0x3C << 1)

static uint8_t OLED_Buffer[
    OLED_WIDTH * OLED_HEIGHT / 8
];

static void OLED_WriteCommand(uint8_t command)
{
    uint8_t data[2];

    data[0] = 0x00;
    data[1] = command;

    HAL_I2C_Master_Transmit(
        &hi2c1,
        OLED_I2C_ADDR,
        data,
        2,
        HAL_MAX_DELAY
    );
}

static HAL_StatusTypeDef OLED_WriteData(
    uint8_t *data,
    uint16_t length)
{
    uint8_t buffer[129];
    buffer[0] = 0x40;
    for (uint16_t i = 0; i < length; i++)
    {
        buffer[i + 1] = data[i];
    }
    return HAL_I2C_Master_Transmit(
        &hi2c1,
        OLED_I2C_ADDR,
        buffer,
        length + 1,
        HAL_MAX_DELAY
    );
}

void OLED_Init(void)
{
    HAL_Delay(100);
    OLED_WriteCommand(0xAE); //off display
    //configure clock
    OLED_WriteCommand(0xD5);
    OLED_WriteCommand(0x80);
    //multiplex ratio
    OLED_WriteCommand(0xA8);
    OLED_WriteCommand(0x3F);
    //start line
    OLED_WriteCommand(0x40);
    //charge pump
    OLED_WriteCommand(0x8D);
    OLED_WriteCommand(0x14);
    //memory addressing mode
    OLED_WriteCommand(0x20);
    OLED_WriteCommand(0x00);
    //segment remap
    OLED_WriteCommand(0xA1);
    //COM scan direction
    OLED_WriteCommand(0xC8);
    //Contrast
    OLED_WriteCommand(0x81);
    OLED_WriteCommand(0xCF);
    //Normal display
    OLED_WriteCommand(0xA6);

    //Clear framebuffer
    OLED_Clear();

    //Send empty framebuffer
    OLED_Update();

    //Display ON
    OLED_WriteCommand(0xAF);
}

void OLED_Clear(void)
{
    for (uint16_t i = 0;
         i < sizeof(OLED_Buffer);
         i++)
    {
        OLED_Buffer[i] = 0x00;
    }
}

void OLED_SetPixel(
    uint8_t x,
    uint8_t y,
    uint8_t color)
{
    if (x >= OLED_WIDTH || y >= OLED_HEIGHT) {
        return;
    }

    uint16_t index = x + (y / 8) * OLED_WIDTH;
    uint8_t bit = 1 << (y % 8);

    if (color) {
        OLED_Buffer[index] |= bit;
    } else {
        OLED_Buffer[index] &= ~bit;
    }
}

void OLED_DrawLine(int x0, int y0, int x1, int y1) {
    int dx = x1 - x0;
    int dy = y1 - y0;

    int sx = (dx >= 0) ? 1 : -1;
    int sy = (dy >= 0) ? 1 : -1;

    dx = (dx >= 0) ? dx : -dx;
    dy = (dy >= 0) ? dy : -dy;

    int err = dx - dy;

    while (1)
    {
        OLED_SetPixel((uint8_t)x0, (uint8_t)y0, 1);
        if (x0 == x1 && y0 == y1) {
            break;
        }
        int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void OLED_DrawRect(int x, int y, int width, int height) {
    OLED_DrawLine(x, y, x + width, y);
    OLED_DrawLine(x, y + height, x + width, y + height);
    OLED_DrawLine( x, y, x, y + height);
    OLED_DrawLine( x + width, y, x + width, y + height);
}

void OLED_FillRect(int x, int y, int width, int height) {
    for (int yy = y; yy < y + height; yy++) {
        for (int xx = x; xx < x + width; xx++) {
            OLED_SetPixel((uint8_t)xx, (uint8_t)yy, 1);
        }
    }
}

void OLED_DrawCircle(int x0, int y0, int radius) {
    int x = radius;
    int y = 0;
    int err = 0;

    while (x >= y) {
        OLED_SetPixel(x0 + x, y0 + y, 1);
        OLED_SetPixel(x0 + y, y0 + x, 1);
        OLED_SetPixel(x0 - y, y0 + x, 1);
        OLED_SetPixel(x0 - x, y0 + y, 1);
        OLED_SetPixel(x0 - x, y0 - y, 1);
        OLED_SetPixel(x0 - y, y0 - x, 1);
        OLED_SetPixel(x0 + y, y0 - x, 1);
        OLED_SetPixel(x0 + x, y0 - y, 1);
        if (err <= 0) {
            y++;
            err += 2 * y + 1;
        } else {
            x--;
            err -= 2 * x + 1;
        }
    }
}

void OLED_Update(void)
{
    for (uint8_t page = 0;
         page < 8;
         page++)
    {
        OLED_WriteCommand(0xB0 | page);
        OLED_WriteCommand(0x00);
        OLED_WriteCommand(0x10);
        OLED_WriteData(&OLED_Buffer[page * OLED_WIDTH],OLED_WIDTH);
    }
}

void OLED_ShowFallDetected(void)
{
    OLED_Clear();
    /*
     * F
     */
    OLED_FillRect(18, 10, 4, 16);
    OLED_FillRect(18, 10, 10, 4);
    OLED_FillRect(18, 16, 8, 4);

    /*
     * A
     */
    OLED_FillRect(34, 10, 4, 16);
    OLED_FillRect(44, 10, 4, 16);
    OLED_FillRect(38, 10, 6, 4);
    OLED_FillRect(38, 16, 6, 4);

    /*
     * L
     */
    OLED_FillRect(54, 10, 4, 16);
    OLED_FillRect(54, 22, 10, 4);

    /*
     * L
     */
    OLED_FillRect(70, 10, 4, 16);
    OLED_FillRect(70, 22, 10, 4);

    /*
     * D
     */
    OLED_FillRect(12, 36, 3, 14);
    OLED_FillRect(12, 36, 7, 3);
    OLED_FillRect(12, 47, 7, 3);
    OLED_FillRect(19, 39, 3, 8);

    /*
     * E
     */
    OLED_FillRect(25, 36, 3, 14);
    OLED_FillRect(25, 36, 8, 3);
    OLED_FillRect(25, 42, 7, 3);
    OLED_FillRect(25, 47, 8, 3);

    /*
     * T
     */
    OLED_FillRect(36, 36, 9, 3);
    OLED_FillRect(39, 36, 3, 14);

    /*
     * E
     */
    OLED_FillRect(48, 36, 3, 14);
    OLED_FillRect(48, 36, 8, 3);
    OLED_FillRect(48, 42, 7, 3);
    OLED_FillRect(48, 47, 8, 3);

    /*
     * C
     */
    OLED_FillRect(59, 36, 3, 14);
    OLED_FillRect(59, 36, 8, 3);
    OLED_FillRect(59, 47, 8, 3);
    OLED_FillRect(59, 42, 5, 3);

    /*
     * T
     */
    OLED_FillRect(70, 36, 9, 3);
    OLED_FillRect(73, 36, 3, 14);

    /*
     * E
     */
    OLED_FillRect(82, 36, 3, 14);
    OLED_FillRect(82, 36, 8, 3);
    OLED_FillRect(82, 42, 7, 3);
    OLED_FillRect(82, 47, 8, 3);

    /*
     * D
     */
    OLED_FillRect(93, 36, 3, 14);
    OLED_FillRect(93, 36, 7, 3);
    OLED_FillRect(93, 47, 7, 3);
    OLED_FillRect(100, 39, 3, 8);

    /*
     * !!!
     */
    OLED_FillRect(107, 36, 3, 9);
    OLED_FillRect(107, 48, 3, 3);

    OLED_FillRect(114, 36, 3, 9);
    OLED_FillRect(114, 48, 3, 3);

    OLED_FillRect(121, 36, 3, 9);
    OLED_FillRect(121, 48, 3, 3);
}

void OLED_ShowSmiley(void)
{
    OLED_Clear();
    //face
    OLED_DrawCircle(64, 32, 25);
    //eyes
    OLED_FillRect(54, 24, 4, 5);
    OLED_FillRect(70, 24, 4, 5);
    //mouth
    OLED_DrawLine(53, 40, 57, 44);
    OLED_DrawLine(57, 44, 61, 46);
    OLED_DrawLine(61, 46, 67, 46);
    OLED_DrawLine(67, 46, 71, 44);
    OLED_DrawLine(71, 44, 75, 40);
}


void OLED_ShowFrown(void)
{
    OLED_Clear();
    //face
    OLED_DrawCircle(64, 32, 25);
    //eyes
    OLED_FillRect(54, 24, 4, 5);
    OLED_FillRect(70, 24, 4, 5);
    //mouth
    OLED_DrawLine(53, 47, 57, 43);
    OLED_DrawLine(57, 43, 61, 41);
    OLED_DrawLine(61, 41, 67, 41);
    OLED_DrawLine(67, 41, 71, 43);
    OLED_DrawLine(71, 43, 75, 47);
}
