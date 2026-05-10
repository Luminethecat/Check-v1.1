/**
 * @file audio_system_init.h
 * @brief 音频系统初始化头文件
 */

#ifndef __AUDIO_SYSTEM_INIT_H
#define __AUDIO_SYSTEM_INIT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f1xx_hal.h"

/**
 * @brief  音频系统初始化（首次运行时存储音频到Flash）
 * @param  None
 * @retval 0: 成功, -1: 失败
 */
int8_t AudioSystem_Init(void);

/**
 * @brief  播放打卡成功提示音
 * @param  None
 * @retval 0: 成功, -1: 失败
 */
int8_t AudioSystem_PlayCheckSuccess(void);

/**
 * @brief  播放打卡失败提示音
 * @param  None
 * @retval 0: 成功, -1: 失败
 */
int8_t AudioSystem_PlayCheckFailure(void);

#ifdef __cplusplus
}
#endif

#endif /* __AUDIO_SYSTEM_INIT_H */