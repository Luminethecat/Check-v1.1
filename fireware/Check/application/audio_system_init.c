/**
 * @file audio_system_init.c
 * @brief 音频系统初始化
 */

#include "stm32f1xx_hal.h"
#include "audio_flash_storage.h"
#include "audio_resources.h"  // 假设您创建了这个头文件
#include <stdio.h>
#include "Com_debug.h"
#include "w25q32_test.h"  // 包含W25Q32驱动函数
// 从audio_resources.c导入函数
extern int8_t AudioResources_StoreAllToFlash(void);

/**
 * @brief  音频系统初始化（首次运行时存储音频到Flash）
 * @param  None
 * @retval 0: 成功, -1: 失败
 */
int8_t AudioSystem_Init(void)
{
    COM_DEBUG("=== 音频系统初始化 ===\r\n");
    
    // 初始化W25Q32
    if(AudioFlashStorage_Init() != 0)
    {
        COM_DEBUG("W25Q32初始化失败!\r\n");
        return -1;
    }
    
    // 为了确保使用正确的采样率，彻底擦除W25Q32芯片
    // 这将清除所有旧的音频数据，强制重新存储
    COM_DEBUG("正在擦除整个W25Q32芯片，请稍候...\r\n");
    
    // 芯片擦除（这会擦除整个Flash芯片，可能需要较长时间）
    W25Q32_ChipErase();
    
    // 等待芯片擦除完成（根据W25Q32规格，可能需要几秒到几十秒）
    uint32_t timeout = 1000; // 10秒超时
    uint8_t status;
    do {
        status = W25Q32_ReadStatusRegister();
        HAL_Delay(100); // 等待100ms
        timeout--;
    } while((status & 0x01) && timeout > 0); // 检查WIP位是否清零
    
    if(timeout == 0) {
        COM_DEBUG("芯片擦除超时!\r\n");
        return -1;
    }
    
    COM_DEBUG("芯片擦除完成，重新存储音频资源...\r\n");
    
    // 存储音频资源到Flash（使用新的采样率参数）
    if(AudioResources_StoreAllToFlash() != 0)
    {
        COM_DEBUG("音频资源存储失败!\r\n");
        return -1;
    }
    
    COM_DEBUG("音频系统初始化完成!\r\n");
    return 0;
}

/**
 * @brief  播放打卡成功提示音
 * @param  None
 * @retval 0: 成功, -1: 失败
 */
int8_t AudioSystem_PlayCheckSuccess(void)
{
    extern int8_t AudioResources_PlayCheckSuccess(void);
    return AudioResources_PlayCheckSuccess();
}

/**
 * @brief  播放打卡失败提示音
 * @param  None
 * @retval 0: 成功, -1: 失败
 */
int8_t AudioSystem_PlayCheckFailure(void)
{
    extern int8_t AudioResources_PlayCheckFailure(void);
    return AudioResources_PlayCheckFailure();
}