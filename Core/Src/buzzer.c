#include "main.h"
#include "buzzer.h"

/* D6 = PB1, AF2 = TIM3_CH4. TIM3 belongs exclusively to this driver.
 * Hardware generates the waveform; no timer ISR, DMA, RTOS delay or busy loop.
 */
volatile uint32_t buzzer_frequency_hz;
static int initialized;

void Buzzer_Init(void)
{
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_TIM3_CLK_ENABLE();
    __HAL_RCC_TIM3_FORCE_RESET();
    __HAL_RCC_TIM3_RELEASE_RESET();
    uint32_t clock = HAL_RCC_GetPCLK1Freq();
    if ((RCC->CFGR & RCC_CFGR_PPRE1) != 0U) clock *= 2U;
    /* Current clock configuration gives 80 MHz / 80 = 1 MHz counter. */
    if (clock < 1000000U || clock % 1000000U) App_Fatal();
    TIM3->PSC = clock / 1000000U - 1U;
    TIM3->ARR = 999U;
    TIM3->CCR4 = 0;
    TIM3->CCMR2 = TIM_CCMR2_OC4M_1 | TIM_CCMR2_OC4M_2 | TIM_CCMR2_OC4PE;
    TIM3->CR1 = TIM_CR1_ARPE;
    TIM3->EGR = TIM_EGR_UG;
    TIM3->SR = 0;
    TIM3->CCER = TIM_CCER_CC4E; /* Active-high PWM mode 1, initially 0% duty. */
    GPIO_InitTypeDef pin = {0};
    pin.Pin = ARD_D6_Pin;
    pin.Mode = GPIO_MODE_AF_PP;
    pin.Pull = GPIO_PULLDOWN;
    pin.Speed = GPIO_SPEED_FREQ_LOW;
    pin.Alternate = GPIO_AF2_TIM3;
    HAL_GPIO_Init(ARD_D6_GPIO_Port, &pin);
    initialized = 1;
}

void Buzzer_Stop(void)
{
    if (!initialized) return;
    TIM3->CCR4 = 0;
    TIM3->EGR = TIM_EGR_UG; /* Load zero duty immediately and reset counter. */
    TIM3->CR1 &= ~TIM_CR1_CEN;
    TIM3->SR = 0;
    buzzer_frequency_hz = 0;
}

void Buzzer_SetFrequency(uint32_t hz)
{
    if (!initialized) return;
    if (hz < 100U || hz > 5000U) hz = 0;
    if (hz == buzzer_frequency_hz) return; /* Do not restart each 20 ms sample. */
    Buzzer_Stop();
    if (!hz) return;
    uint32_t period = (1000000U + hz / 2U) / hz;
    TIM3->ARR = period - 1U;
    TIM3->CCR4 = period / 2U; /* Approximately 50% duty. */
    TIM3->EGR = TIM_EGR_UG;
    TIM3->SR = 0;
    TIM3->CR1 |= TIM_CR1_CEN;
    buzzer_frequency_hz = hz;
}
