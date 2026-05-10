/**
 * @file audio_resources.h
 * @brief 音频资源头文件
 */

#ifndef __AUDIO_RESOURCES_H
#define __AUDIO_RESOURCES_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f1xx_hal.h"

/**
 * @brief  存储所有音频资源到Flash
 * @param  None
 * @retval 0: 成功, -1: 失败
 */
int8_t AudioResources_StoreAllToFlash(void);

/**
 * @brief  获取音频资源总数
 * @param  None
 * @retval 音频资源数量
 */
uint8_t AudioResources_GetCount(void);

/**
 * @brief  播放打卡成功提示音
 * @param  None
 * @retval 0: 成功, -1: 失败
 */
int8_t AudioResources_PlayCheckSuccess(void);

/**
 * @brief  播放打卡失败提示音
 * @param  None
 * @retval 0: 成功, -1: 失败
 */
int8_t AudioResources_PlayCheckFailure(void);

/**
 * @brief  播放指定名称的音频
 * @param  name: 音频文件名
 * @retval 0: 成功, -1: 失败
 */
int8_t AudioResources_PlayByName(const char* name);

#ifdef __cplusplus
}
#endif

#endif /* __AUDIO_RESOURCES_H */