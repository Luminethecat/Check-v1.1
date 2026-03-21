#ifndef AUDIO_PROMPT_H
#define AUDIO_PROMPT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "audio_dac_app.h"
#include "storage_layout.h"
#include <stdint.h>

typedef struct
{
  uint32_t data_addr;
  uint32_t sample_count;
  uint32_t sample_rate_hz;
  uint8_t bits_per_sample;
  uint8_t valid;
  uint8_t reserved[2];
} AudioPromptIndexTypeDef;

/* 音频提示索引层：
 * 根据事件索引查找 W25Q32 中的音频元信息并触发播放。 */
void AudioPrompt_Init(void);
AudioDac_StatusTypeDef AudioPrompt_Play(uint8_t prompt_index);

#ifdef __cplusplus
}
#endif

#endif
