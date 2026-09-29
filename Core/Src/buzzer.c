#include "main.h"
#include "buzzer.h"

#define TIMER_TICK_HZ   1000000U    // run the counter at 1 MHz so ARR = period in us
#define BUZZER_MIN_HZ   100U
#define BUZZER_MAX_HZ   5000U

volatile uint32_t buzzer_frequency_hz;  // 0 = off (extra_sensors reads this to mask the mic)
static int initialized;

void Buzzer_Init(void)
{
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_TIM3_CLK_ENABLE();
    __HAL_RCC_TIM3_FORCE_RESET();       // start from a clean timer
    __HAL_RCC_TIM3_RELEASE_RESET();

    // timer clock is 2x PCLK1 if the APB1 prescaler isn't 1
    uint32_t clock = HAL_RCC_GetPCLK1Freq();
    if ((RCC->CFGR & RCC_CFGR_PPRE1) != 0U) clock *= 2U;

    // with our 80 MHz setup this is 80 MHz / 80 = 1 MHz
    if (clock < TIMER_TICK_HZ || clock % TIMER_TICK_HZ) App_Fatal();
    TIM3->PSC = clock / TIMER_TICK_HZ - 1U;
    TIM3->ARR = 999U;
    TIM3->CCR4 = 0;                     // 0% duty = silent

    // PWM mode 1 on CH4 with preload so changes apply cleanly
    TIM3->CCMR2 = TIM_CCMR2_OC4M_1 | TIM_CCMR2_OC4M_2 | TIM_CCMR2_OC4PE;
    TIM3->CR1 = TIM_CR1_ARPE;
    TIM3->EGR = TIM_EGR_UG;
    TIM3->SR = 0;
    TIM3->CCER = TIM_CCER_CC4E;         // active high output

    GPIO_InitTypeDef pin = {0};
    pin.Pin = ARD_D6_Pin;
    pin.Mode = GPIO_MODE_AF_PP;
    pin.Pull = GPIO_PULLDOWN;           // keeps the buzzer quiet just in case pin floats
    pin.Speed = GPIO_SPEED_FREQ_LOW;
    pin.Alternate = GPIO_AF2_TIM3;
    HAL_GPIO_Init(ARD_D6_GPIO_Port, &pin);

    initialized = 1;
}

void Buzzer_Stop(void)
{
    if (!initialized) return;
    TIM3->CCR4 = 0;
    TIM3->EGR = TIM_EGR_UG;             // load 0 duty right away and reset the counter
    TIM3->CR1 &= ~TIM_CR1_CEN;
    TIM3->SR = 0;
    buzzer_frequency_hz = 0;
}

/* Anything outside 100-5000 Hz (including 0) just turns it off. */
void Buzzer_SetFrequency(uint32_t hz)
{
    if (!initialized) return;
    if (hz < BUZZER_MIN_HZ || hz > BUZZER_MAX_HZ) hz = 0;

    // this gets called every 20 ms, so don't restart the tone if nothing changed
    if (hz == buzzer_frequency_hz) return;

    Buzzer_Stop();
    if (!hz) return;

    uint32_t period = (TIMER_TICK_HZ + hz / 2U) / hz;   // rounded to nearest us
    TIM3->ARR = period - 1U;
    TIM3->CCR4 = period / 2U;           // ~50% duty, loudest for a piezo
    TIM3->EGR = TIM_EGR_UG;
    TIM3->SR = 0;
    TIM3->CR1 |= TIM_CR1_CEN;
    buzzer_frequency_hz = hz;
}
