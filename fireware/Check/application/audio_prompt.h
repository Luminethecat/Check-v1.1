#ifndef AUDIO_PROMPT_H
#define AUDIO_PROMPT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "audio_dac_app.h"
#include <stdint.h>

#define AUDIO_INDEX_CHECK_OK           0U
#define AUDIO_INDEX_LATE               1U
#define AUDIO_INDEX_EARLY              2U
#define AUDIO_INDEX_CHECK_FAIL         3U
#define AUDIO_INDEX_ENROLL_OK          4U
#define AUDIO_INDEX_TIME_SYNC_OK       5U

/* 音频提示索引层：
 * 播放简单beep音。 */
void AudioPrompt_Init(void);
AudioDac_StatusTypeDef AudioPrompt_Play(uint8_t prompt_index);

#ifdef __cplusplus
}
#endif

#endif
