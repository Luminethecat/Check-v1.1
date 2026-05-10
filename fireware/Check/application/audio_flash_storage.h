/**
  ******************************************************************************
  * @file    audio_flash_storage.h
  * @brief   音频数据在Flash中存储和播放系统头文件
  ******************************************************************************
  */

#ifndef __AUDIO_FLASH_STORAGE_H
#define __AUDIO_FLASH_STORAGE_H

#include <sys/syslimits.h>
#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f1xx_hal.h"
#include <stdint.h>
#include <stdlib.h>
#include "w25q32_test.h"
#include "audio_dac_app.h"  // 包含Mute_Enable/Mute_Disable函数

// 从dac.h导入DAC句柄
extern DAC_HandleTypeDef hdac;
#define NAME_MAX 20
// 音频文件信息结构体定义
#pragma pack(push, 1)  // 设置1字节对齐
typedef struct {
    uint32_t magic;           // 文件标识符 "AUD\0"
    char name[NAME_MAX];            // 音频名称
    uint32_t sample_rate;     // 采样率
    uint32_t data_size;       // 音频数据大小
    uint32_t data_addr;       // 音频数据在Flash中的地址
    uint16_t bits_per_sample; // 采样位数 (通常为8或16)
    uint8_t channels;         // 声道数 (1=单声道, 2=立体声)
    uint8_t reserved;         // 保留字节
} AudioFileInfo_t;
#pragma pack(pop)   // 恢复默认对齐

#include "audio_resources.h"
// 在 audio_flash_storage.h 中添加
int8_t AudioFlashStorage_PlayRawAudioNonBlocking(const uint8_t* audio_data, uint32_t data_size, uint32_t sample_rate);
uint8_t AudioFlashStorage_IsPlaying(void);
/**
  * @brief  播放实际的打卡成功音频
  * @param  None
  * @retval None
  */
extern void AudioFlashStorage_PlayActualCheckSuccess(void);

/**
  * @brief  初始化音频Flash存储系统
  * @param  None
  * @retval 0: 成功, -1: 失败
  */
int8_t AudioFlashStorage_Init(void);

/**
  * @brief  保存音频文件到Flash
  * @param  name: 音频文件名
  * @param  audio_data: 音频数据指针
  * @param  data_size: 音频数据大小
  * @param  sample_rate: 采样率
  * @param  bits_per_sample: 采样位数
  * @retval 0: 成功, -1: 失败
  */
int8_t AudioFlashStorage_SaveAudio(const char* name, uint8_t* audio_data, 
                                   uint32_t data_size, uint32_t sample_rate, 
                                   uint16_t bits_per_sample);

/**
  * @brief  从Flash加载音频文件信息
  * @param  name: 音频文件名
  * @param  header: 音频文件头部信息输出
  * @retval 0: 成功, -1: 失败
  */
int8_t AudioFlashStorage_LoadAudioHeader(const char* name, AudioFileInfo_t* header);

/**
  * @brief  播放Flash中的音频文件
  * @param  name: 音频文件名
  * @retval 0: 成功, -1: 失败
  */
int8_t AudioFlashStorage_PlayAudio(const char* name);

/**
  * @brief  播放预定义的音频提示
  * @param  index: 音频提示索引
  * @retval 0: 成功, -1: 失败
  */
int8_t AudioFlashStorage_PlayPrompt(uint8_t index);
void AudioPlay_TimerCallback(TIM_HandleTypeDef *htim);

/**
  * @brief  音频存储测试函数
  * @param  None
  * @retval None
  */
extern void AudioFlashStorage_Test(void);

/**
  * @brief  直接播放测试音频数据（不从Flash读取）
  * @param  None
  * @retval None
  */
extern void AudioFlashStorage_PlayDirectTest(void);

/**
  * @brief  将音频数据烧录到W25Q32 Flash中
  * @param  name: 音频文件名
  * @param  audio_data: 音频数据指针
  * @param  data_size: 音频数据大小
  * @retval 0: 成功, -1: 失败
  */
extern int8_t AudioFlashStorage_BurnToFlash(const char* name, const uint8_t* audio_data, uint32_t data_size);

/**
  * @brief  直接播放音频数组（不从Flash读取）
  * @param  audio_data: 音频数据数组
  * @param  data_size: 音频数据大小
  * @param  sample_rate: 采样率
  * @retval 0: 成功, -1: 失败
  */
extern int8_t AudioFlashStorage_PlayRawAudio(const uint8_t* audio_data, uint32_t data_size, uint32_t sample_rate);

#ifdef __cplusplus
}
#endif

#endif /* __AUDIO_FLASH_STORAGE_H */