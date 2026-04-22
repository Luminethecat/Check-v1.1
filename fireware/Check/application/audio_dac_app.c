#include "audio_dac_app.h"
#include "main.h"
#include "cmsis_os.h"
#include "dac.h"
#include "stm32f1xx_hal.h"
#include "app_board.h"

extern DAC_HandleTypeDef hdac;

#define DAC_MID       2048
#define DAC_VOLUME    40


/* ================= Tone 播放函数 ================= */

static void play_tone(
        uint16_t freq_hz,
        uint16_t duration_ms,
        uint16_t amplitude)
{
    uint32_t period_us = 1000000 / freq_hz;
    uint32_t half_period_us = period_us / 2;

    uint32_t delay_ms = half_period_us / 1000;

    if(delay_ms == 0)
        delay_ms = 1;

    uint32_t end_tick =
        xTaskGetTickCount() + pdMS_TO_TICKS(duration_ms);

    uint16_t high = DAC_MID + amplitude;
    uint16_t low  = DAC_MID - amplitude;

    while(xTaskGetTickCount() < end_tick)
    {
        HAL_DAC_SetValue(
            &hdac,
            DAC_CHANNEL_1,
            DAC_ALIGN_12B_R,
            high);

        osDelay(delay_ms);

        HAL_DAC_SetValue(
            &hdac,
            DAC_CHANNEL_1,
            DAC_ALIGN_12B_R,
            low);

        osDelay(delay_ms);
    }

    HAL_DAC_SetValue(
        &hdac,
        DAC_CHANNEL_1,
        DAC_ALIGN_12B_R,
        DAC_MID);
}


/* ================= DAC 初始化 ================= */

void DAC_Sound_Init(void)
{
    DAC_ChannelConfTypeDef sConfig = {0};

    sConfig.DAC_Trigger = DAC_TRIGGER_NONE;
    sConfig.DAC_OutputBuffer = DAC_OUTPUTBUFFER_ENABLE;

    HAL_DAC_ConfigChannel(
        &hdac,
        &sConfig,
        DAC_CHANNEL_1);

    HAL_DAC_Start(
        &hdac,
        DAC_CHANNEL_1);

    HAL_DAC_SetValue(
        &hdac,
        DAC_CHANNEL_1,
        DAC_ALIGN_12B_R,
        DAC_MID);
}


/* ================= 声音函数 ================= */

void DAC_Sound_Beep(void)
{
    Mute_Enable();
    play_tone(1500, 80, DAC_VOLUME);
    Mute_Disable();
}


void DAC_Sound_Success(void)
{
    Mute_Enable();
    play_tone(1200, 80, DAC_VOLUME);

    osDelay(30);

    play_tone(1800, 120, DAC_VOLUME);
    Mute_Disable();
}


void DAC_Sound_Error(void)
{
    Mute_Enable();
    play_tone(500, 250, DAC_VOLUME);
    Mute_Disable();
}


void DAC_Sound_Welcome(void)
{
    Mute_Enable();
    play_tone(800, 120, DAC_VOLUME);

    osDelay(40);

    play_tone(1200, 150, DAC_VOLUME);
    Mute_Disable();
}