#include "oled.h"

#include "stm32l4xx_hal.h"
#include <stdint.h>
#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>
#include "../../Drivers/BSP/B-L4S5I-IOT01/stm32l4s5i_iot01_hsensor.h"
#include "../../Drivers/BSP/B-L4S5I-IOT01/stm32l4s5i_iot01_tsensor.h"

/* Felix's framebuffer artwork, integrated on a dedicated low-priority task.
 * Only this task touches I2C1; motion sensors use the separate I2C2 bus. */
static I2C_HandleTypeDef hi2c1;
volatile uint32_t oled_online;
volatile uint32_t oled_errors;
static volatile uint32_t requested_status;
static uint32_t displayed_status = 0xffffffffU;
static uint32_t displayed_phase = 0xffffffffU;
static uint32_t transfer_failed;

void OLED_SetStatus(uint32_t status) { requested_status = status; }

static HAL_StatusTypeDef OLED_Transmit(uint8_t *data, uint16_t length)
{
    if (transfer_failed) return HAL_ERROR;
    HAL_StatusTypeDef result = HAL_I2C_Master_Transmit(
        &hi2c1, OLED_I2C_ADDR, data, length, 25);
    if (result != HAL_OK) {
        transfer_failed = 1;
        oled_online = 0;
        oled_errors++;
    }
    return result;
}

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

    OLED_Transmit(data, 2);
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
    return OLED_Transmit(buffer, length + 1);
}

void OLED_Init(void)
{
    transfer_failed = 0;
    oled_online = 0;
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
    OLED_WriteCommand(0x02); // page mode, matching OLED_Update
    OLED_WriteCommand(0xD3); OLED_WriteCommand(0x00); // display offset
    OLED_WriteCommand(0xDA); OLED_WriteCommand(0x12); // 128x64 COM pins
    OLED_WriteCommand(0xD9); OLED_WriteCommand(0xF1); // precharge
    OLED_WriteCommand(0xDB); OLED_WriteCommand(0x40); // VCOMH
    OLED_WriteCommand(0xA4); // display RAM
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
    oled_online = !transfer_failed;
    displayed_status = displayed_phase = 0xffffffffU;
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
    //block letters:
    //F
    OLED_FillRect(18, 10, 4, 16);
    OLED_FillRect(18, 10, 10, 4);
    OLED_FillRect(18, 16, 8, 4);
    //A
    OLED_FillRect(34, 10, 4, 16);
    OLED_FillRect(44, 10, 4, 16);
    OLED_FillRect(38, 10, 6, 4);
    OLED_FillRect(38, 16, 6, 4);
    //L
    OLED_FillRect(54, 10, 4, 16);
    OLED_FillRect(54, 22, 10, 4);
    //L
    OLED_FillRect(70, 10, 4, 16);
    OLED_FillRect(70, 22, 10, 4);
    //
    //D
    OLED_FillRect(12, 36, 3, 14);
    OLED_FillRect(12, 36, 7, 3);
    OLED_FillRect(12, 47, 7, 3);
    OLED_FillRect(19, 39, 3, 8);
    //E
    OLED_FillRect(25, 36, 3, 14);
    OLED_FillRect(25, 36, 8, 3);
    OLED_FillRect(25, 42, 7, 3);
    OLED_FillRect(25, 47, 8, 3);
    //T
    OLED_FillRect(36, 36, 9, 3);
    OLED_FillRect(39, 36, 3, 14);
    //E
    OLED_FillRect(48, 36, 3, 14);
    OLED_FillRect(48, 36, 8, 3);
    OLED_FillRect(48, 42, 7, 3);
    OLED_FillRect(48, 47, 8, 3);
    //C
    OLED_FillRect(59, 36, 3, 14);
    OLED_FillRect(59, 36, 8, 3);
    OLED_FillRect(59, 47, 8, 3);
    //T
    OLED_FillRect(70, 36, 9, 3);
    OLED_FillRect(73, 36, 3, 14);
    //E
    OLED_FillRect(82, 36, 3, 14);
    OLED_FillRect(82, 36, 8, 3);
    OLED_FillRect(82, 42, 7, 3);
    OLED_FillRect(82, 47, 8, 3);
    //D
    OLED_FillRect(93, 36, 3, 14);
    OLED_FillRect(93, 36, 7, 3);
    OLED_FillRect(93, 47, 7, 3);
    OLED_FillRect(100, 39, 3, 8);

    //!!!
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


static const uint8_t digit_font[10][5] =
{
    {0x3E, 0x51, 0x49, 0x45, 0x3E}, // 0
    {0x00, 0x42, 0x7F, 0x40, 0x00}, // 1
    {0x42, 0x61, 0x51, 0x49, 0x46}, // 2
    {0x21, 0x41, 0x45, 0x4B, 0x31}, // 3
    {0x18, 0x14, 0x12, 0x7F, 0x10}, // 4
    {0x27, 0x45, 0x45, 0x45, 0x39}, // 5
    {0x3C, 0x4A, 0x49, 0x49, 0x30}, // 6
    {0x01, 0x71, 0x09, 0x05, 0x03}, // 7
    {0x36, 0x49, 0x49, 0x49, 0x36}, // 8
    {0x06, 0x49, 0x49, 0x29, 0x1E}  // 9
};

static void OLED_DrawDigit(uint8_t digit, int x, int y)
{
    for (int col = 0; col < 5; col++) {
        for (int row = 0; row < 7; row++) {
            if (digit_font[digit][col] & (1 << row)) {
                OLED_FillRect(
                    x + col,
                    y + row,
                    1,
                    1
                );
            }
        }
    }
}

int OLED_WriteWords(int x, int y, int len, const uint8_t chars[][5])
{
	int last_x = 0;
    for (int c = 0; c < len; c++) {
        for (int column = 0; column < 5; column++) {
            for (int row = 0; row < 7; row++) {
                if (chars[c][column] & (1 << row)) {
                	last_x = x + c * 6 + column;
                    OLED_SetPixel(last_x, y + row, 1);
                }
            }
        }
    }
    return last_x;
}

static void OLED_WriteTemperature(int x, int y, char temp_str[10])
{
    const uint8_t chars[][5] =
    {
        {0x01, 0x01, 0x7F, 0x01, 0x01}, //T
        {0x38, 0x54, 0x54, 0x54, 0x18}, //e
        {0x7C, 0x04, 0x18, 0x04, 0x78}, //m
        {0x7C, 0x14, 0x14, 0x14, 0x08}, //p
        {0x38, 0x54, 0x54, 0x54, 0x18}, //e
        {0x7C, 0x08, 0x04, 0x04, 0x08}, //r
        {0x38, 0x54, 0x54, 0x54, 0x7C}, //a
        {0x04, 0x3F, 0x44, 0x40, 0x20}, //t
        {0x3C, 0x40, 0x40, 0x20, 0x7C}, //u
        {0x7C, 0x08, 0x04, 0x04, 0x08}, //r
        {0x38, 0x54, 0x54, 0x54, 0x18}, //e
        {0x00, 0x36, 0x36, 0x00, 0x00} //:
    };
    x = OLED_WriteWords(x, y, 12, chars);
    x += 6;

    for (int i = 0; temp_str[i] != '\0'; i++)
    {
        if (temp_str[i] >= '0' && temp_str[i] <= '9') { //number
            OLED_DrawDigit(temp_str[i] - '0', x, y);
            x += 6;
        }
        else if (temp_str[i] == '.') { //dot
            OLED_FillRect(x, y + 6, 2, 2);
            x += 4;
        }
    }
    const uint8_t unit_chars[][5]= {
    	{0x06, 0x09, 0x09, 0x06, 0x00}, //degree
		{0x3E, 0x41, 0x41, 0x41, 0x22} //C
    };

    x = OLED_WriteWords(x, y, 2, unit_chars);
}

static void OLED_WriteHumidity(int x, int y, char hum_str[10]) {
    const uint8_t chars[][5] = {
        {0x7F, 0x08, 0x08, 0x08, 0x7F}, //H
        {0x3C, 0x40, 0x40, 0x20, 0x7C}, //u
        {0x7C, 0x04, 0x18, 0x04, 0x78}, //m
        {0x00, 0x44, 0x7D, 0x40, 0x00}, //i
        {0x38, 0x44, 0x44, 0x44, 0x7F}, //d
        {0x00, 0x44, 0x7D, 0x40, 0x00}, //i
        {0x04, 0x3F, 0x44, 0x40, 0x20}, //t
        {0x0C, 0x50, 0x50, 0x50, 0x3C}, //y
        {0x00, 0x36, 0x36, 0x00, 0x00} //:
    };
    x = OLED_WriteWords(x, y, 9, chars);
    x += 6;

    for (int i = 0; hum_str[i] != '\0'; i++) {
        if (hum_str[i] >= '0' && hum_str[i] <= '9') { //number
            OLED_DrawDigit(hum_str[i] - '0', x, y);
            x += 6;
        }
        else if (hum_str[i] == '.') { //dot
            OLED_FillRect(x, y + 6, 2, 2);
            x += 4;
        }
    }
    const uint8_t percent[][5] = {{0x61, 0x12, 0x08, 0x24, 0x43}};
    x = OLED_WriteWords(x, y, 1, percent);
}

void OLED_ShowTemperatureHumidity(float temperature, float humidity) {
    char temp_str[10];
    char hum_str[10];

    snprintf(temp_str, sizeof(temp_str), "%.2f", temperature);
    snprintf(hum_str, sizeof(hum_str), "%.1f", humidity);
    OLED_Clear();
    OLED_WriteTemperature(4,5, temp_str);
    OLED_WriteHumidity(4,28, hum_str);
}

static void OLED_ShowSOS(void)
{
    OLED_Clear();
    /* Large SOS; manual assistance must not be labelled a detected fall. */
    for (int x = 18; x <= 86; x += 68) {
        OLED_FillRect(x, 18, 24, 4);
        OLED_FillRect(x, 30, 24, 4);
        OLED_FillRect(x, 42, 24, 4);
        OLED_FillRect(x, 18, 4, 16);
        OLED_FillRect(x + 20, 30, 4, 16);
    }
    OLED_FillRect(52, 18, 24, 4);
    OLED_FillRect(52, 42, 24, 4);
    OLED_FillRect(52, 18, 4, 28);
    OLED_FillRect(72, 18, 4, 28);
}

void OLED_Service(uint32_t now)
{
    if (!oled_online) return;
    uint32_t status = requested_status;
    uint32_t phase = (now / 2000U) % 2U;

    if (status == 0U) {
        if (status == displayed_status && phase == displayed_phase) return;
        if (phase) {
        	float temperature = BSP_TSENSOR_ReadTemp();
        	float humidity = BSP_HSENSOR_ReadHumidity();
        	OLED_ShowTemperatureHumidity(temperature,humidity);
        } else {
        	OLED_ShowSmiley();
        }
    } else {
        if (status == displayed_status) return;
        if (status == 2U) OLED_ShowSOS();
        else OLED_ShowFallDetected();
    }

    OLED_Update();
    if (oled_online) {
        displayed_status = status;
        displayed_phase = phase;
    }
}

static int OLED_BusInit(void)
{
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_I2C1_CLK_ENABLE();
    __HAL_RCC_I2C1_CONFIG(RCC_I2C1CLKSOURCE_PCLK1);
    __HAL_RCC_I2C1_FORCE_RESET();
    __HAL_RCC_I2C1_RELEASE_RESET();
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin = GPIO_PIN_8 | GPIO_PIN_9;
    gpio.Mode = GPIO_MODE_AF_OD;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF4_I2C1;
    HAL_GPIO_Init(GPIOB, &gpio);
    hi2c1.Instance = I2C1;

    /* 80 MHz PCLK1, approximately 100 kHz; verify with actual bus pull-ups. */
    //hi2c1.Init.Timing = 0x10909CEC;
    hi2c1.Init.Timing = 0x00702991; //chose 400kHz to be faste
    hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
    if (HAL_I2C_Init(&hi2c1) != HAL_OK) return 0;
    return HAL_I2CEx_ConfigAnalogFilter(&hi2c1, I2C_ANALOGFILTER_ENABLE) == HAL_OK;
}

void OLED_Task(void *argument)
{
    (void)argument;
    vTaskDelay(pdMS_TO_TICKS(100));
    for (;;) {
        if (!oled_online) {
            if (OLED_BusInit()) OLED_Init();
            else oled_errors++;
            if (!oled_online) { vTaskDelay(pdMS_TO_TICKS(5000)); continue; }
        }
        OLED_Service(HAL_GetTick());
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}


