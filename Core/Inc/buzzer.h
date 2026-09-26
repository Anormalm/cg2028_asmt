#ifndef BUZZER_H
#define BUZZER_H
#include <stdint.h>
void Buzzer_Init(void);
void Buzzer_SetFrequency(uint32_t hz);
void Buzzer_Stop(void);
/* Read-only diagnostic for CubeIDE Live Expressions: requested pitch, 0=silent. */
extern volatile uint32_t buzzer_frequency_hz;
#endif
