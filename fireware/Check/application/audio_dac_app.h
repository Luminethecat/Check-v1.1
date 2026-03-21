#ifndef AUDIO_DAC_APP_H
#define AUDIO_DAC_APP_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f1xx_hal.h"

#define AUDIO_DAC_MAX_SAMPLES            4096U

typedef enum
{
  AUDIO_DAC_OK = 0x00U,
  AUDIO_DAC_ERROR = 0x01U,
  AUDIO_DAC_BUSY = 0x02U,
} AudioDac_StatusTypeDef;

/* DAC 音频播放接口：
 * 当前使用 TIM6 + DMA + DAC_CH1 输出单声道 PCM。 */
void AudioDac_Init(void);
void AudioDac_SetVolume(uint8_t percent);
AudioDac_StatusTypeDef AudioDac_PlayU8Mono(const uint8_t *samples,
                                           uint32_t sample_count,
                                           uint32_t sample_rate_hz);
AudioDac_StatusTypeDef AudioDac_PlayU12Mono(const uint16_t *samples,
                                            uint32_t sample_count,
                                            uint32_t sample_rate_hz);
void AudioDac_Stop(void);
uint8_t AudioDac_IsBusy(void);

#ifdef __cplusplus
}
#endif

#endif
