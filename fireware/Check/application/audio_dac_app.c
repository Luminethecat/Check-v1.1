#include "audio_dac_app.h"

#include "app_board.h"
#include "stm32f1xx_hal_dac_ex.h"
#include "stm32f1xx_hal_rcc_ex.h"
#include "stm32f1xx_hal_tim.h"

/* DAC 播放改成 TIM6 触发 + DMA 推送：
 * W25Q32 读出的 PCM 数据先转到 audio_dma_buffer，再由定时器稳定输出。 */

#define AUDIO_DAC_TIMER_BASE_HZ            1000000UL

static TIM_HandleTypeDef htim6_audio;
static uint16_t audio_dma_buffer[AUDIO_DAC_MAX_SAMPLES];
static uint8_t audio_volume_percent = 100U;
static uint8_t audio_is_playing = 0U;
static uint8_t audio_is_initialized = 0U;

static uint16_t AudioDac_ApplyVolume12Bit(uint16_t sample)
{
  uint32_t scaled = ((uint32_t)sample * audio_volume_percent) / 100U;

  if (scaled > 4095U)
  {
    scaled = 4095U;
  }

  return (uint16_t)scaled;
}

static uint32_t AudioDac_GetTim6ClockHz(void)
{
  RCC_ClkInitTypeDef clk_config;
  uint32_t flash_latency;
  uint32_t pclk1_hz;

  HAL_RCC_GetClockConfig(&clk_config, &flash_latency);
  pclk1_hz = HAL_RCC_GetPCLK1Freq();

  /* APB1 被分频时，定时器时钟会自动翻倍。 */
  if (clk_config.APB1CLKDivider == RCC_HCLK_DIV1)
  {
    return pclk1_hz;
  }

  return pclk1_hz * 2UL;
}

static HAL_StatusTypeDef AudioDac_ConfigTimer(uint32_t sample_rate_hz)
{
  TIM_MasterConfigTypeDef master_config;
  uint32_t tim_clk_hz;
  uint32_t prescaler;
  uint32_t period;

  if (sample_rate_hz == 0U)
  {
    return HAL_ERROR;
  }

  tim_clk_hz = AudioDac_GetTim6ClockHz();
  if (tim_clk_hz < AUDIO_DAC_TIMER_BASE_HZ)
  {
    return HAL_ERROR;
  }

  /* 先把 TIM6 规整到 1MHz 基准，再通过 ARR 得到最终采样率。 */
  prescaler = (tim_clk_hz / AUDIO_DAC_TIMER_BASE_HZ) - 1UL;
  period = (AUDIO_DAC_TIMER_BASE_HZ / sample_rate_hz);
  if (period == 0U)
  {
    return HAL_ERROR;
  }

  __HAL_RCC_TIM6_CLK_ENABLE();

  htim6_audio.Instance = TIM6;
  htim6_audio.Init.Prescaler = (uint16_t)prescaler;
  htim6_audio.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim6_audio.Init.Period = (uint16_t)(period - 1UL);
  htim6_audio.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;

  if (HAL_TIM_Base_Init(&htim6_audio) != HAL_OK)
  {
    return HAL_ERROR;
  }

  master_config.MasterOutputTrigger = TIM_TRGO_UPDATE;
  master_config.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim6_audio, &master_config) != HAL_OK)
  {
    return HAL_ERROR;
  }

  return HAL_OK;
}

static HAL_StatusTypeDef AudioDac_ConfigPeripheral(void)
{
  DAC_ChannelConfTypeDef dac_config;
  DMA_HandleTypeDef *dma_handle = APP_AUDIO_DAC_HANDLE.DMA_Handle1;

  if (dma_handle == NULL)
  {
    return HAL_ERROR;
  }

  /* 语音提示播完即停，因此 DMA 用 NORMAL 模式而不是循环模式。 */
  (void)HAL_DMA_DeInit(dma_handle);
  dma_handle->Init.Mode = DMA_NORMAL;
  if (HAL_DMA_Init(dma_handle) != HAL_OK)
  {
    return HAL_ERROR;
  }

  (void)HAL_DAC_Stop(&APP_AUDIO_DAC_HANDLE, APP_AUDIO_DAC_CHANNEL);

  dac_config.DAC_Trigger = DAC_TRIGGER_T6_TRGO;
  dac_config.DAC_OutputBuffer = DAC_OUTPUTBUFFER_ENABLE;
  if (HAL_DAC_ConfigChannel(&APP_AUDIO_DAC_HANDLE, &dac_config, APP_AUDIO_DAC_CHANNEL) != HAL_OK)
  {
    return HAL_ERROR;
  }

  return HAL_OK;
}

static AudioDac_StatusTypeDef AudioDac_StartBuffer(uint32_t sample_count, uint32_t sample_rate_hz)
{
  if (sample_count == 0U || sample_count > AUDIO_DAC_MAX_SAMPLES)
  {
    return AUDIO_DAC_ERROR;
  }

  if (audio_is_playing != 0U)
  {
    return AUDIO_DAC_BUSY;
  }

  if (AudioDac_ConfigTimer(sample_rate_hz) != HAL_OK)
  {
    return AUDIO_DAC_ERROR;
  }

  audio_is_playing = 1U;
  /* 先启 DAC DMA，再启 TIM6，避免触发早到导致首个采样丢失。 */
  if (HAL_DAC_Start_DMA(&APP_AUDIO_DAC_HANDLE,
                        APP_AUDIO_DAC_CHANNEL,
                        (uint32_t *)audio_dma_buffer,
                        sample_count,
                        DAC_ALIGN_12B_R) != HAL_OK)
  {
    audio_is_playing = 0U;
    return AUDIO_DAC_ERROR;
  }

  if (HAL_TIM_Base_Start(&htim6_audio) != HAL_OK)
  {
    (void)HAL_DAC_Stop_DMA(&APP_AUDIO_DAC_HANDLE, APP_AUDIO_DAC_CHANNEL);
    audio_is_playing = 0U;
    return AUDIO_DAC_ERROR;
  }

  return AUDIO_DAC_OK;
}

void AudioDac_Init(void)
{
  if (AudioDac_ConfigPeripheral() != HAL_OK)
  {
    return;
  }

  if (AudioDac_ConfigTimer(8000U) != HAL_OK)
  {
    return;
  }

  audio_is_initialized = 1U;
  /* 空闲时保持中点电平，减小扬声器直流冲击。 */
  HAL_DAC_SetValue(&APP_AUDIO_DAC_HANDLE, APP_AUDIO_DAC_CHANNEL, DAC_ALIGN_12B_R, 2048U);
}

void AudioDac_SetVolume(uint8_t percent)
{
  if (percent > 100U)
  {
    percent = 100U;
  }

  audio_volume_percent = percent;
}

AudioDac_StatusTypeDef AudioDac_PlayU8Mono(const uint8_t *samples,
                                           uint32_t sample_count,
                                           uint32_t sample_rate_hz)
{
  uint32_t idx;

  if (audio_is_initialized == 0U || samples == NULL || sample_count == 0U || sample_count > AUDIO_DAC_MAX_SAMPLES)
  {
    return AUDIO_DAC_ERROR;
  }

  for (idx = 0U; idx < sample_count; idx++)
  {
    /* 8bit 无符号 PCM 左移到 12bit DAC 范围。 */
    audio_dma_buffer[idx] = AudioDac_ApplyVolume12Bit((uint16_t)samples[idx] << 4U);
  }

  return AudioDac_StartBuffer(sample_count, sample_rate_hz);
}

AudioDac_StatusTypeDef AudioDac_PlayU12Mono(const uint16_t *samples,
                                            uint32_t sample_count,
                                            uint32_t sample_rate_hz)
{
  uint32_t idx;

  if (audio_is_initialized == 0U || samples == NULL || sample_count == 0U || sample_count > AUDIO_DAC_MAX_SAMPLES)
  {
    return AUDIO_DAC_ERROR;
  }

  for (idx = 0U; idx < sample_count; idx++)
  {
    audio_dma_buffer[idx] = AudioDac_ApplyVolume12Bit(samples[idx] & 0x0FFFU);
  }

  return AudioDac_StartBuffer(sample_count, sample_rate_hz);
}

void AudioDac_Stop(void)
{
  /* 播放结束后同时关闭 TIM 和 DMA，并把输出恢复到中点。 */
  (void)HAL_TIM_Base_Stop(&htim6_audio);
  (void)HAL_DAC_Stop_DMA(&APP_AUDIO_DAC_HANDLE, APP_AUDIO_DAC_CHANNEL);
  HAL_DAC_SetValue(&APP_AUDIO_DAC_HANDLE, APP_AUDIO_DAC_CHANNEL, DAC_ALIGN_12B_R, 2048U);
  audio_is_playing = 0U;
}

uint8_t AudioDac_IsBusy(void)
{
  return audio_is_playing;
}

void HAL_DAC_ConvCpltCallbackCh1(DAC_HandleTypeDef *hdac)
{
  if (hdac->Instance == DAC)
  {
    /* 单次 DMA 播放完成后由回调统一收尾。 */
    AudioDac_Stop();
  }
}

void HAL_DAC_ErrorCallbackCh1(DAC_HandleTypeDef *hdac)
{
  if (hdac->Instance == DAC)
  {
    AudioDac_Stop();
  }
}
