#ifndef BUZZER_MELODY_H
#define BUZZER_MELODY_H
#include <stdint.h>
/* User-supplied beep sequence. Each delay is silence AFTER its beep.
 * Absolute offsets avoid cumulative drift; the caller samples at 20 ms.
 */
typedef struct { uint32_t start_ms; uint16_t hz, on_ms; } BuzzerNote;
static const BuzzerNote buzzer_melody[] = {
    {0U, 660U, 100U}, /* rest 150 ms */
    {250U, 660U, 100U}, /* rest 300 ms */
    {650U, 660U, 100U}, /* rest 300 ms */
    {1050U, 510U, 100U}, /* rest 100 ms */
    {1250U, 660U, 100U}, /* rest 300 ms */
    {1650U, 770U, 100U}, /* rest 550 ms */
    {2300U, 380U, 100U}, /* rest 575 ms */
    {2975U, 510U, 100U}, /* rest 450 ms */
    {3525U, 380U, 100U}, /* rest 400 ms */
    {4025U, 320U, 100U}, /* rest 500 ms */
    {4625U, 440U, 100U}, /* rest 300 ms */
    {5025U, 480U, 80U}, /* rest 330 ms */
    {5435U, 450U, 100U}, /* rest 150 ms */
    {5685U, 430U, 100U}, /* rest 300 ms */
    {6085U, 380U, 100U}, /* rest 200 ms */
    {6385U, 660U, 80U}, /* rest 200 ms */
    {6665U, 760U, 50U}, /* rest 150 ms */
    {6865U, 860U, 100U}, /* rest 300 ms */
    {7265U, 700U, 80U}, /* rest 150 ms */
    {7495U, 760U, 50U}, /* rest 350 ms */
    {7895U, 660U, 80U}, /* rest 300 ms */
    {8275U, 520U, 80U}, /* rest 150 ms */
    {8505U, 580U, 80U}, /* rest 150 ms */
    {8735U, 480U, 80U}, /* rest 500 ms */
    {9315U, 510U, 100U}, /* rest 450 ms */
    {9865U, 380U, 100U}, /* rest 400 ms */
    {10365U, 320U, 100U}, /* rest 500 ms */
    {10965U, 440U, 100U}, /* rest 300 ms */
    {11365U, 480U, 80U}, /* rest 330 ms */
    {11775U, 450U, 100U}, /* rest 150 ms */
    {12025U, 430U, 100U}, /* rest 300 ms */
    {12425U, 380U, 100U}, /* rest 200 ms */
    {12725U, 660U, 80U}, /* rest 200 ms */
    {13005U, 760U, 50U}, /* rest 150 ms */
    {13205U, 860U, 100U}, /* rest 300 ms */
    {13605U, 700U, 80U}, /* rest 150 ms */
    {13835U, 760U, 50U}, /* rest 350 ms */
    {14235U, 660U, 80U}, /* rest 300 ms */
    {14615U, 520U, 80U}, /* rest 150 ms */
    {14845U, 580U, 80U}, /* rest 150 ms */
    {15075U, 480U, 80U}, /* rest 500 ms */
    {15655U, 500U, 100U}, /* rest 300 ms */
    {16055U, 760U, 100U}, /* rest 100 ms */
    {16255U, 720U, 100U}, /* rest 150 ms */
    {16505U, 680U, 100U}, /* rest 150 ms */
    {16755U, 620U, 150U}, /* rest 300 ms */
    {17205U, 650U, 150U}, /* rest 300 ms */
    {17655U, 380U, 100U}, /* rest 150 ms */
    {17905U, 430U, 100U}, /* rest 150 ms */
    {18155U, 500U, 100U}, /* rest 300 ms */
    {18555U, 430U, 100U}, /* rest 150 ms */
    {18805U, 500U, 100U}, /* rest 100 ms */
    {19005U, 570U, 100U}, /* rest 220 ms */
    {19325U, 500U, 100U}, /* rest 300 ms */
    {19725U, 760U, 100U}, /* rest 100 ms */
    {19925U, 720U, 100U}, /* rest 150 ms */
    {20175U, 680U, 100U}, /* rest 150 ms */
    {20425U, 620U, 150U}, /* rest 300 ms */
    {20875U, 650U, 200U}, /* rest 300 ms */
    {21375U, 1020U, 80U}, /* rest 300 ms */
    {21755U, 1020U, 80U}, /* rest 150 ms */
    {21985U, 1020U, 80U}, /* rest 300 ms */
    {22365U, 380U, 100U}, /* rest 300 ms */
    {22765U, 500U, 100U}, /* rest 300 ms */
    {23165U, 760U, 100U}, /* rest 100 ms */
    {23365U, 720U, 100U}, /* rest 150 ms */
    {23615U, 680U, 100U}, /* rest 150 ms */
    {23865U, 620U, 150U}, /* rest 300 ms */
    {24315U, 650U, 150U}, /* rest 300 ms */
    {24765U, 380U, 100U}, /* rest 150 ms */
    {25015U, 430U, 100U}, /* rest 150 ms */
    {25265U, 500U, 100U}, /* rest 300 ms */
    {25665U, 430U, 100U}, /* rest 150 ms */
    {25915U, 500U, 100U}, /* rest 100 ms */
    {26115U, 570U, 100U}, /* rest 420 ms */
    {26635U, 585U, 100U}, /* rest 450 ms */
    {27185U, 550U, 100U}, /* rest 420 ms */
    {27705U, 500U, 100U}, /* rest 360 ms */
    {28165U, 380U, 100U}, /* rest 300 ms */
    {28565U, 500U, 100U}, /* rest 300 ms */
    {28965U, 500U, 100U}, /* rest 150 ms */
    {29215U, 500U, 100U}, /* rest 300 ms */
    {29615U, 500U, 100U}, /* rest 300 ms */
    {30015U, 760U, 100U}, /* rest 100 ms */
    {30215U, 720U, 100U}, /* rest 150 ms */
    {30465U, 680U, 100U}, /* rest 150 ms */
    {30715U, 620U, 150U}, /* rest 300 ms */
    {31165U, 650U, 150U}, /* rest 300 ms */
    {31615U, 380U, 100U}, /* rest 150 ms */
    {31865U, 430U, 100U}, /* rest 150 ms */
    {32115U, 500U, 100U}, /* rest 300 ms */
    {32515U, 430U, 100U}, /* rest 150 ms */
    {32765U, 500U, 100U}, /* rest 100 ms */
    {32965U, 570U, 100U}, /* rest 220 ms */
    {33285U, 500U, 100U}, /* rest 300 ms */
    {33685U, 760U, 100U}, /* rest 100 ms */
    {33885U, 720U, 100U}, /* rest 150 ms */
    {34135U, 680U, 100U}, /* rest 150 ms */
    {34385U, 620U, 150U}, /* rest 300 ms */
    {34835U, 650U, 200U}, /* rest 300 ms */
    {35335U, 1020U, 80U}, /* rest 300 ms */
    {35715U, 1020U, 80U}, /* rest 150 ms */
    {35945U, 1020U, 80U}, /* rest 300 ms */
    {36325U, 380U, 100U}, /* rest 300 ms */
    {36725U, 500U, 100U}, /* rest 300 ms */
    {37125U, 760U, 100U}, /* rest 100 ms */
    {37325U, 720U, 100U}, /* rest 150 ms */
    {37575U, 680U, 100U}, /* rest 150 ms */
    {37825U, 620U, 150U}, /* rest 300 ms */
    {38275U, 650U, 150U}, /* rest 300 ms */
    {38725U, 380U, 100U}, /* rest 150 ms */
    {38975U, 430U, 100U}, /* rest 150 ms */
    {39225U, 500U, 100U}, /* rest 300 ms */
    {39625U, 430U, 100U}, /* rest 150 ms */
    {39875U, 500U, 100U}, /* rest 100 ms */
    {40075U, 570U, 100U}, /* rest 420 ms */
    {40595U, 585U, 100U}, /* rest 450 ms */
    {41145U, 550U, 100U}, /* rest 420 ms */
    {41665U, 500U, 100U}, /* rest 360 ms */
    {42125U, 380U, 100U}, /* rest 300 ms */
    {42525U, 500U, 100U}, /* rest 300 ms */
    {42925U, 500U, 100U}, /* rest 150 ms */
    {43175U, 500U, 100U}, /* rest 300 ms */
    {43575U, 500U, 60U}, /* rest 150 ms */
    {43785U, 500U, 80U}, /* rest 300 ms */
    {44165U, 500U, 60U}, /* rest 350 ms */
    {44575U, 500U, 80U}, /* rest 150 ms */
    {44805U, 580U, 80U}, /* rest 350 ms */
    {45235U, 660U, 80U}, /* rest 150 ms */
    {45465U, 500U, 80U}, /* rest 300 ms */
    {45845U, 430U, 80U}, /* rest 150 ms */
    {46075U, 380U, 80U}, /* rest 600 ms */
    {46755U, 500U, 60U}, /* rest 150 ms */
    {46965U, 500U, 80U}, /* rest 300 ms */
    {47345U, 500U, 60U}, /* rest 350 ms */
    {47755U, 500U, 80U}, /* rest 150 ms */
    {47985U, 580U, 80U}, /* rest 150 ms */
    {48215U, 660U, 80U}, /* rest 550 ms */
    {48845U, 870U, 80U}, /* rest 325 ms */
    {49250U, 760U, 80U}, /* rest 600 ms */
    {49930U, 500U, 60U}, /* rest 150 ms */
    {50140U, 500U, 80U}, /* rest 300 ms */
    {50520U, 500U, 60U}, /* rest 350 ms */
    {50930U, 500U, 80U}, /* rest 150 ms */
    {51160U, 580U, 80U}, /* rest 350 ms */
    {51590U, 660U, 80U}, /* rest 150 ms */
    {51820U, 500U, 80U}, /* rest 300 ms */
    {52200U, 430U, 80U}, /* rest 150 ms */
    {52430U, 380U, 80U}, /* rest 600 ms */
    {53110U, 660U, 100U}, /* rest 150 ms */
    {53360U, 660U, 100U}, /* rest 300 ms */
    {53760U, 660U, 100U}, /* rest 300 ms */
    {54160U, 510U, 100U}, /* rest 100 ms */
    {54360U, 660U, 100U}, /* rest 300 ms */
    {54760U, 770U, 100U}, /* rest 550 ms */
    {55410U, 380U, 100U}, /* rest 575 ms */
};
/* Preserve the supplied score; playback is 3/2 speed and one octave up. */
#define BUZZER_MELODY_DURATION_MS 37390U
#define BUZZER_MELODY_NOTES (sizeof(buzzer_melody) / sizeof(buzzer_melody[0]))
static uint32_t BuzzerMelody_Hz(uint32_t elapsed)
{
    if (elapsed >= BUZZER_MELODY_DURATION_MS) return 0;
    elapsed = elapsed * 3U / 2U;
    for (uint32_t i = 0; i < BUZZER_MELODY_NOTES; ++i) {
        const BuzzerNote *note = &buzzer_melody[i];
        if (elapsed < note->start_ms) return 0;
        if (elapsed - note->start_ms < note->on_ms) return note->hz * 2U;
    }
    return 0;
}
#endif
