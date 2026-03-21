#include "audio_prompt.h"

#include "app_services.h"
#include "w25q32_app.h"

/* 音频提示层只关心“索引 -> 元信息 -> 播放”。
 * 具体音频数据仍由 W25Q32 和 DAC 模块负责。 */

static uint8_t audio_prompt_cache[4096];

void AudioPrompt_Init(void)
{
}

AudioDac_StatusTypeDef AudioPrompt_Play(uint8_t prompt_index)
{
  AudioPromptIndexTypeDef meta;
  uint32_t meta_addr;

  meta_addr = STORAGE_ADDR_AUDIO_INDEX_BASE + ((uint32_t)prompt_index * sizeof(AudioPromptIndexTypeDef));
  if (W25Q32_ReadData(meta_addr, (uint8_t *)&meta, sizeof(meta)) != W25Q32_OK)
  {
    return AUDIO_DAC_ERROR;
  }

  /* 当前先支持 8bit PCM，后面若扩展 ADPCM/12bit 可以在这里分流。 */
  if (meta.valid == 0U || meta.sample_count == 0U || meta.sample_count > sizeof(audio_prompt_cache))
  {
    return AUDIO_DAC_ERROR;
  }

  if (meta.bits_per_sample == 8U)
  {
    return App_Audio_PlayFromFlashU8(meta.data_addr,
                                     audio_prompt_cache,
                                     meta.sample_count,
                                     meta.sample_rate_hz);
  }

  return AUDIO_DAC_ERROR;
}
