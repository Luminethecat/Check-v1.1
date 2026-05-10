/**
  ******************************************************************************
  * @file    audio_flash_storage.c
  * @brief   音频数据在Flash中存储和播放系统
  ******************************************************************************
  */

#include "stm32f1xx_hal.h"
#include "main.h"
#include "audio_flash_storage.h"  // 包含所有必要的定义
#include "w25q32_test.h"  // W25Q32驱动
#include "audio_dac_app.h"
#include "dac.h"  // 包含DAC句柄
#include <stdio.h>
#include <string.h>
#include "cmsis_os.h"
// 全局变量用于中断音频播放
static volatile const uint8_t* g_current_audio_data = NULL;
static volatile uint32_t g_audio_index = 0;
static volatile uint32_t g_audio_length = 0;
static volatile uint8_t g_audio_playing = 0;
// 音频音量控制宏定义
#define AUDIO_VOLUME_GAIN    4    // 音频增益，可根据需要调节 (建议范围: 1-30)
                          // 16: 默认音量 (16/16 = 100%)
                          // 8: 一半音量 (8/16 = 50%) - 建议使用此值
                          // 4: 四分之一音量 (4/16 = 25%)
                          // 2: 八分之一音量 (2/16 = 12.5%) - 非常小
                          // 1: 十六分之一音量 (1/16 = 6.25%) - 极小
                          
#include <math.h>
#include "Com_debug.h"
#include "tim.h"
// 从dac.h导入DAC句柄
extern DAC_HandleTypeDef hdac;

// 导入音频资源
extern const uint8_t check_success_audio_data[14592];

// 音频文件地址映射表
// 根据日志中显示的地址，为常用音频文件预先设定地址
#define AUDIO_ADDR_CHECK_OK     0x00100000  // check_ok.bin
#define AUDIO_ADDR_LATE         0x00104000  // late.bin
#define AUDIO_ADDR_EARLY        0x00108000  // early.bin
#define AUDIO_ADDR_CHECK_FAIL   0x0010B000  // check_fail.bin
#define AUDIO_ADDR_ENROLL_OK    0x0010F000  // enroll_ok.bin
#define AUDIO_ADDR_TIME_SYNC    0x00113000  // time_sync.bin
#define AUDIO_ADDR_UNKNOWN      0x00117000  // unknown.bin
#define AUDIO_ADDR_REPEAT       0x0011B000  // repeat.bin

// 音频文件地址映射结构
typedef struct {
    const char* name;
    uint32_t addr;
} AudioAddressMap;

// 音频文件地址映射表
static const AudioAddressMap g_audio_address_map[] = {
    {"check_ok.bin",    AUDIO_ADDR_CHECK_OK},
    {"late.bin",        AUDIO_ADDR_LATE},
    {"early.bin",       AUDIO_ADDR_EARLY},
    {"check_fail.bin",  AUDIO_ADDR_CHECK_FAIL},
    {"enroll_ok.bin",   AUDIO_ADDR_ENROLL_OK},
    {"time_sync.bin",   AUDIO_ADDR_TIME_SYNC},
    {"unknown.bin",     AUDIO_ADDR_UNKNOWN},
    {"repeat.bin",      AUDIO_ADDR_REPEAT},
    {NULL, 0}  // 结束标记
};

// 定义音频参数常量
#define CHECK_SUCCESS_AUDIO_SIZE 14592
#define CHECK_SUCCESS_SAMPLE_RATE 8000

// 音频数据存储区域定义 (在Flash中分配空间)
#define AUDIO_STORAGE_BASE_ADDR     0x100000  // 从1MB地址开始
#define MAX_AUDIO_FILES             10        // 最大音频文件数量
#define MAX_AUDIO_NAME_LENGTH       20        // 音频文件名最大长度

// 音频文件头部标识
#define AUDIO_FILE_MAGIC            0x44554100  // "AUD\0" (little endian)

// 音频播放缓冲区
#define PLAY_BUFFER_SIZE    256
static uint8_t g_play_buffer[PLAY_BUFFER_SIZE];

/**
  * @brief  初始化音频Flash存储系统
  * @param  None
  * @retval 0: 成功, -1: 失败
  */
int8_t AudioFlashStorage_Init(void)
{
    COM_DEBUG("初始化音频Flash存储系统...\r\n");
    
    // 首先初始化W25Q32
    W25Q32_Test_Init();
    HAL_Delay(100);
    
    // 检查W25Q32是否可用
    uint32_t jedec_id = W25Q32_ReadJEDECID();
    if(jedec_id != 0xEF4016)  // W25Q32
    {
        COM_DEBUG("错误: W25Q32不可用 (ID: 0x%06lX)\r\n", jedec_id);
        return -1;
    }
    
    COM_DEBUG("音频Flash存储系统初始化成功!\r\n");
    return 0;
}

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
                                   uint16_t bits_per_sample)
{
    if(strlen(name) >= MAX_AUDIO_NAME_LENGTH)
    {
        COM_DEBUG("错误: 音频文件名过长\r\n");
        return -1;
    }
    
    // 查找下一个可用的存储位置
// 查找下一个可用的存储位置
uint32_t current_addr = AUDIO_STORAGE_BASE_ADDR;
COM_DEBUG("开始搜索可用存储位置，基础地址: 0x%08lX\r\n", current_addr);

// 搜索第一个可用的存储位置（即魔数为0xFFFFFFFF的位置）
for(int i = 0; i < MAX_AUDIO_FILES; i++) {
    uint8_t header_bytes[sizeof(AudioFileInfo_t)];
    W25Q32_Read(header_bytes, current_addr, sizeof(AudioFileInfo_t));
    
    // 安全地复制到结构体以避免对齐问题
    AudioFileInfo_t temp_header;
    memcpy(&temp_header, header_bytes, sizeof(AudioFileInfo_t));
    
    COM_DEBUG("检查地址 0x%08lX, 魔数: 0x%08lX, 名称: %.12s\r\n", 
              current_addr, temp_header.magic, temp_header.name);
    
    // 如果当前位置的魔数为0xFFFFFFFF，说明是未使用的区域
    if(temp_header.magic == 0xFFFFFFFF) {
        COM_DEBUG("找到空闲位置: 0x%08lX\r\n", current_addr);
        break;  // 找到可用位置
    }
    
    // 如果是相同名称的文件，覆盖它
    if(temp_header.magic == AUDIO_FILE_MAGIC && 
       strncmp(temp_header.name, name, MAX_AUDIO_NAME_LENGTH) == 0) {
        COM_DEBUG("找到同名文件，将覆盖: %s at 0x%08lX\r\n", name, current_addr);
        break;  // 找到同名文件，准备覆盖
    }
    
    // 移动到下一个可能的文件位置
    uint32_t file_size = sizeof(AudioFileInfo_t) + temp_header.data_size;
    uint32_t aligned_size = ((file_size + 4095) / 4096) * 4096;  // 对齐到扇区边界
    COM_DEBUG("当前文件大小: %lu, 对齐后大小: %lu\r\n", file_size, aligned_size);
    current_addr += aligned_size;
    COM_DEBUG("移动到下一位置: 0x%08lX\r\n", current_addr);
}

COM_DEBUG("准备在地址 0x%08lX 处存储文件 '%s'\r\n", current_addr, name);


    // 创建音频文件头部
    AudioFileInfo_t header;
    header.magic = AUDIO_FILE_MAGIC; // "AUD\0" (little endian)
    strncpy(header.name, name, MAX_AUDIO_NAME_LENGTH - 1);
    header.name[MAX_AUDIO_NAME_LENGTH - 1] = '\0';
    header.sample_rate = sample_rate;
    header.data_size = data_size;
    header.data_addr = current_addr + sizeof(AudioFileInfo_t);  // 数据紧随头部
    header.bits_per_sample = bits_per_sample;
    header.channels = 1;  // 单声道
    header.reserved = 0;
    
    COM_DEBUG("保存音频文件 '%s': 大小=%lu字节, 采样率=%luHz\r\n", 
           name, data_size, sample_rate);
    
    // 计算需要的总空间
    uint32_t total_size = sizeof(AudioFileInfo_t) + data_size;
    
    // 擦除必要的扇区
    uint32_t sector_start = (current_addr / 4096) * 4096;  // 对齐到扇区边界
    uint32_t sector_end = ((current_addr + total_size + 4095) / 4096) * 4096;
    
    for(uint32_t addr = sector_start; addr < sector_end; addr += 4096)
    {
        W25Q32_EraseSector(addr);
        HAL_Delay(10);  // 等待擦除完成
    }
    
    // 将头部和音频数据组合到一个缓冲区中
   // 检查文件大小限制以避免内存分配失败
 // 使用较小的缓冲区分块处理，支持大文件
const uint32_t CHUNK_SIZE = 2048; // 2KB缓冲区

if(total_size <= CHUNK_SIZE) {
    // 小文件：一次性分配
    uint8_t* combined_buffer = malloc(total_size);
    if(combined_buffer == NULL)
    {
        COM_DEBUG("错误: 无法分配小文件内存\r\n");
        return -1;
    }

    // 复制头部到缓冲区开头
    memcpy(combined_buffer, &header, sizeof(AudioFileInfo_t));

    // 复制音频数据到缓冲区后面
    memcpy(&combined_buffer[sizeof(AudioFileInfo_t)], audio_data, data_size);

    // 分批写入整个文件（头部+数据）
    uint32_t written = 0;
    while(written < total_size)
    {
        // 计算到下一个页面边界的距离，避免跨页写入
        uint32_t page_boundary = 256 - ((current_addr + written) % 256);
        uint16_t to_write = (total_size - written > page_boundary) ? page_boundary : (total_size - written);
        
        // 确保不超过256字节限制和剩余数据大小
        if(to_write > 256) to_write = 256;
        if(to_write > (total_size - written)) to_write = (total_size - written);
        
        W25Q32_WritePage(&combined_buffer[written], current_addr + written, to_write);
        
        // 每次写入后等待完成
        uint32_t timeout = 100; // 1秒超时
        uint8_t status;
        do {
            status = W25Q32_ReadStatusRegister();
            HAL_Delay(10); // 等待10ms
            timeout--;
        } while((status & 0x01) && timeout > 0); // 检查WIP位是否清零
        
        if(timeout == 0) {
            COM_DEBUG("Flash写入超时!\r\n");
            free(combined_buffer);
            return -1;
        }
        
        written += to_write;
    }

    // 释放临时缓冲区
    free(combined_buffer);
} else {
    // 大文件：分块处理，先写头部
    uint8_t header_buffer[sizeof(AudioFileInfo_t)];
    memcpy(header_buffer, &header, sizeof(AudioFileInfo_t));
    
    // 写入头部
    uint32_t written = 0;
    while(written < sizeof(AudioFileInfo_t))
    {
        uint16_t to_write = (sizeof(AudioFileInfo_t) - written > 256) ? 256 : (sizeof(AudioFileInfo_t) - written);
        W25Q32_WritePage(&header_buffer[written], current_addr + written, to_write);
        
        // 每次写入后等待完成
        uint32_t timeout = 100; // 1秒超时
        uint8_t status;
        do {
            status = W25Q32_ReadStatusRegister();
            HAL_Delay(10); // 等待10ms
            timeout--;
        } while((status & 0x01) && timeout > 0); // 检查WIP位是否清零
        
        if(timeout == 0) {
            COM_DEBUG("Flash头部写入超时!\r\n");
            return -1;
        }
        
        written += to_write;
    }
    
    // 分块写入音频数据
    uint32_t data_written = 0;
    while(data_written < data_size)
    {
        uint32_t to_copy = (data_size - data_written > CHUNK_SIZE) ? CHUNK_SIZE : (data_size - data_written);
        uint8_t* data_chunk = malloc(to_copy);
        if(data_chunk == NULL)
        {
            COM_DEBUG("错误: 无法分配数据块内存\r\n");
            return -1;
        }
        
        memcpy(data_chunk, &audio_data[data_written], to_copy);
        
        // 写入数据块
        uint32_t chunk_written = 0;
        while(chunk_written < to_copy)
        {
            // 计算到下一个页面边界的距离，避免跨页写入
            uint32_t page_boundary = 256 - ((current_addr + sizeof(AudioFileInfo_t) + data_written + chunk_written) % 256);
            uint16_t to_write = (to_copy - chunk_written > page_boundary) ? page_boundary : (to_copy - chunk_written);
            
            // 确保不超过256字节限制和剩余数据大小
            if(to_write > 256) to_write = 256;
            if(to_write > (to_copy - chunk_written)) to_write = (to_copy - chunk_written);
            
            W25Q32_WritePage(&data_chunk[chunk_written], 
                             current_addr + sizeof(AudioFileInfo_t) + data_written + chunk_written, 
                             to_write);
            
            // 每次写入后等待完成
            uint32_t timeout = 100; // 1秒超时
            uint8_t status;
            do {
                status = W25Q32_ReadStatusRegister();
                HAL_Delay(10); // 等待10ms
                timeout--;
            } while((status & 0x01) && timeout > 0); // 检查WIP位是否清零
            
            if(timeout == 0) {
                COM_DEBUG("Flash数据写入超时!\r\n");
                free(data_chunk);
                return -1;
            }
            
            chunk_written += to_write;
        }
        
        free(data_chunk);
        data_written += to_copy;
    }
}
  
    
    // 验证整个文件写入是否成功
    uint8_t verify_header_bytes[sizeof(AudioFileInfo_t)];
    W25Q32_Read(verify_header_bytes, current_addr, sizeof(AudioFileInfo_t));
    
    // 安全地复制到结构体以避免对齐问题
    AudioFileInfo_t verify_header;
    memcpy(&verify_header, verify_header_bytes, sizeof(AudioFileInfo_t));
    
    COM_DEBUG("验证读取: 魔数: 0x%08lX, 名称: %.12s\r\n", verify_header.magic, verify_header.name);
    COM_DEBUG("原始魔数: 0x%08lX, 验证魔数: 0x%08lX\r\n", header.magic, verify_header.magic);
    
    if(verify_header.magic != AUDIO_FILE_MAGIC || 
       strncmp(verify_header.name, name, MAX_AUDIO_NAME_LENGTH) != 0)
    {
        COM_DEBUG("错误: 文件写入验证失败!\r\n");
        return -1;
    }
    else
    {
        COM_DEBUG("文件写入验证成功!\r\n");
    }
    
    // 等待Flash内部操作完成
    HAL_Delay(100);
    
    COM_DEBUG("音频文件保存完成!\r\n");
    return 0;
}

/**
  * @brief  从Flash加载音频文件信息
  * @param  name: 音频文件名
  * @param  header: 音频文件头部信息输出
  * @retval 0: 成功, -1: 失败
  */
int8_t AudioFlashStorage_LoadAudioHeader(const char* name, AudioFileInfo_t* header)
{
    // 首先尝试从预定义地址映射表中查找
    for(int i = 0; g_audio_address_map[i].name != NULL; i++) {
        if(strcmp(g_audio_address_map[i].name, name) == 0) {
            // 从预定义地址直接读取
            uint32_t addr = g_audio_address_map[i].addr;
            
            COM_DEBUG("尝试从预定义地址 0x%08lX 读取 '%s'\r\n", addr, name);
            
            uint8_t header_bytes[sizeof(AudioFileInfo_t)];
            W25Q32_Read(header_bytes, addr, sizeof(AudioFileInfo_t));
            
            // 安全地复制到结构体以避免对齐问题
            memcpy(header, header_bytes, sizeof(AudioFileInfo_t));
            
            // 验证文件是否有效
            if(header->magic == AUDIO_FILE_MAGIC && 
               strncmp(header->name, name, MAX_AUDIO_NAME_LENGTH) == 0) {
                COM_DEBUG("成功从预定义地址找到 '%s'\r\n", name);
                return 0;  // 找到并验证成功
            } else {
                COM_DEBUG("预定义地址无效，魔数: 0x%08lX, 名称: %.12s\r\n", header->magic, header->name);
                // 预定义地址无效，继续尝试搜索
            }
        }
    }
    
    // 如果预定义地址未找到或无效，执行传统搜索
    COM_DEBUG("预定义地址未找到 '%s'，开始搜索...\r\n", name);
    
    // 在搜索前等待一段时间，确保之前的Flash写入操作完全完成
    HAL_Delay(10);  // 减少等待时间，从100ms改为10ms
    
    // 从基础地址开始搜索音频文件
    uint32_t current_addr = AUDIO_STORAGE_BASE_ADDR;
    
    // 限制搜索范围
    for(int i = 0; i < MAX_AUDIO_FILES; i++)
    {
        COM_DEBUG("搜索第 %d 个位置，地址: 0x%08lX\r\n", i, current_addr);
        
        // 读取头部
        uint8_t header_bytes[sizeof(AudioFileInfo_t)];
        W25Q32_Read(header_bytes, current_addr, sizeof(AudioFileInfo_t));
        
        // 安全地复制到结构体以避免对齐问题
        AudioFileInfo_t temp_header;
        memcpy(&temp_header, header_bytes, sizeof(AudioFileInfo_t));
        
        COM_DEBUG("读取到魔数: 0x%08lX, 名称: %.12s\r\n", temp_header.magic, temp_header.name);
        
        // 检查魔数和文件名
        if(temp_header.magic == AUDIO_FILE_MAGIC && 
           strncmp(temp_header.name, name, MAX_AUDIO_NAME_LENGTH) == 0)
        {
            // 找到匹配的文件
            memcpy(header, &temp_header, sizeof(AudioFileInfo_t));
            COM_DEBUG("在地址 0x%08lX 找到 '%s'\r\n", current_addr, name);
            return 0;
        }
        
        // 如果魔数为0xFFFFFFFF，说明到达未使用的区域
        if(temp_header.magic == 0xFFFFFFFF)
        {
            COM_DEBUG("到达未使用区域，停止搜索\r\n");
            break;
        }
        
        // 移动到下一个可能的文件位置 (固定间距，因为我们按顺序存储)
        // 计算当前文件占用的空间大小（包括头部和数据）
        uint32_t file_size = sizeof(AudioFileInfo_t) + temp_header.data_size;
        // 对齐到下一个扇区边界
        uint32_t aligned_size = ((file_size + 4095) / 4096) * 4096;
        current_addr += aligned_size;
        
        // 确保地址始终是扇区对齐的
        current_addr = ((current_addr + 4095) / 4096) * 4096;
    }
    
    COM_DEBUG("未找到音频文件: %s\r\n", name);
    return -1;  // 未找到文件
}

/**
  * @brief  播放Flash中的音频文件 (仅支持8000Hz 8位音频)
  * @param  name: 音频文件名
  * @retval 0: 成功, -1: 失败
  */
int8_t AudioFlashStorage_PlayAudio(const char* name)
{
   AudioFileInfo_t header;
    
    if(AudioFlashStorage_LoadAudioHeader(name, &header) != 0)
    {
        return -1;
    }
    
    // 显示实际音频参数
    COM_DEBUG("音频文件 '%s': 大小=%lu字节, 采样率=%luHz, 位深=%d位, 声道=%d\r\n", 
           header.name, header.data_size, header.sample_rate, header.bits_per_sample, header.channels);
    
    // 检查是否为支持的音频格式
    if(header.sample_rate != 8000 || header.bits_per_sample != 8)
    {
        COM_DEBUG("警告: 音频参数不是8000Hz/8位，当前参数: %luHz/%d位\r\n", 
                  header.sample_rate, header.bits_per_sample);
        COM_DEBUG("注意: 当前代码只支持8000Hz 8位音频，播放可能不正常\r\n");
    }
    
    // 对于较小的文件，使用原来的方式
    if(header.data_size <= 16000)  // 如果文件小于15000字节（15KB），使用原方法
    {
        uint8_t* audio_buffer = malloc(header.data_size);
        if(audio_buffer == NULL)
        {   
            COM_DEBUG("错误: 无法分配音频缓冲区内存\r\n");
            return -1;
        }
        
        // 一次性从Flash读取整个音频文件
        W25Q32_Read(audio_buffer, header.data_addr, header.data_size);
        
        // 使用中断方式播放以确保准确的采样率
        int8_t result = AudioFlashStorage_PlayRawAudio(audio_buffer, header.data_size, header.sample_rate);
        
        free(audio_buffer);
        return result;
    }
    else  // 对于较大的文件，使用流式播放
    {
        // 使用较小的缓冲区进行流式播放，避免RAM不足
        const uint32_t SMALL_BUFFER_SIZE = 15000;  // 使用14000字节的小缓冲区
        uint8_t* audio_buffer = malloc(SMALL_BUFFER_SIZE);
        if(audio_buffer == NULL)
        {
            COM_DEBUG("错误: 无法分配小缓冲区内存\r\n");
            return -1;
        }
        
        uint32_t bytes_played = 0;
        while(bytes_played < header.data_size)
        {
            uint32_t remaining = header.data_size - bytes_played;
            uint32_t read_size = (remaining > SMALL_BUFFER_SIZE) ? SMALL_BUFFER_SIZE : remaining;
            
            // 从Flash读取小块数据
            W25Q32_Read(audio_buffer, header.data_addr + bytes_played, read_size);
            
            // 播放小块数据
            if(AudioFlashStorage_PlayRawAudio(audio_buffer, read_size, header.sample_rate) != 0)
            {
                free(audio_buffer);
                return -1;
            }
            
            bytes_played += read_size;
            
            // 适当延时，确保播放连续性
            osDelay(1);  // 如果使用FreeRTOS
            // 或者 HAL_Delay(1);  // 如果使用裸机
        }
        
        free(audio_buffer);
        return 0;
    }
}

/**
  * @brief  播放预定义的音频提示
  * @param  index: 音频提示索引
  * @retval 0: 成功, -1: 失败
  */
int8_t AudioFlashStorage_PlayPrompt(uint8_t index)
{
    const char* audio_names[] = {
        "check_ok.bin",      // 0: 打卡成功
        "late.bin",          // 1: 迟到
        "early.bin",         // 2: 早退
        "check_fail.bin",    // 3: 打卡失败
        "enroll_ok.bin",     // 4: 录入成功
        "time_sync.bin",      // 5: 时间同步
        "unknown.bin",        // 6: 未知用户
        "repeat.bin"          // 7: 重复打卡
    };
    
    if(index >= sizeof(audio_names) / sizeof(audio_names[0]))
    {
        COM_DEBUG("无效的音频提示索引: %d\r\n", index);
        return -1;
    }
    
    return AudioFlashStorage_PlayAudio(audio_names[index]);
}

/**
  * @brief  直接播放测试音频数据
  * @param  None
  * @retval None
  */
void AudioFlashStorage_PlayDirectTest(void)
{
    // 直接播放一段测试音频，不从Flash读取
    COM_DEBUG("开始直接音频播放测试\r\n");
    
    // 生成一个简单的正弦波测试数据
    uint8_t sine_wave[512];
    for(int i = 0; i < 512; i++) {
        // 生成简单的正弦波，频率约1000Hz
        float angle = (float)i * 2.0f * 3.14159f * 1000.0f / 6400.0f; // 6400Hz采样率
        float value = sinf(angle);
        sine_wave[i] = (uint8_t)(128 + (value * 100)); // 中心128，振幅100
    }
    
    // 启用音频输出
    Mute_Enable();
    
    // 播放测试音频
    for(int i = 0; i < 512; i++)
    {
        // 将8位音频数据转换为DAC值
        uint8_t raw_sample = sine_wave[i];
        
        // 直接映射到12位DAC范围
        uint16_t dac_value = 0;
        if(raw_sample > 128) {
            dac_value = 2048 + (raw_sample - 128) * 16;
            if(dac_value > 4095) dac_value = 4095;
        } else if(raw_sample < 128) {
            dac_value = 2048 - (128 - raw_sample) * 16;
            if(dac_value > 2048) dac_value = 0;
        } else {
            dac_value = 2048;
        }
        
        // 输出到DAC
        HAL_DAC_SetValue(&hdac, DAC_CHANNEL_1, DAC_ALIGN_12B_R, dac_value);
        
        // 6400Hz采样率的延时
        for(volatile uint32_t j = 0; j < 1100; j++) {
            __NOP();
        }
    }
    
    // 恢复DAC到中间值
    HAL_DAC_SetValue(&hdac, DAC_CHANNEL_1, DAC_ALIGN_12B_R, 2048);
    
    // 关闭音频输出
    Mute_Disable();

    COM_DEBUG("直接音频播放测试完成\r\n");
}

/**
  * @brief  播放实际的打卡成功音频
  * @param  None
  * @retval None
  */
void AudioFlashStorage_PlayActualCheckSuccess(void)
{
    COM_DEBUG("播放实际打卡成功音频 (大小: %d, 采样率: %dHz)\r\n", 
              CHECK_SUCCESS_AUDIO_SIZE, CHECK_SUCCESS_SAMPLE_RATE);
    
    // 播放实际的打卡成功音频数据
    AudioFlashStorage_PlayRawAudio(check_success_audio_data, CHECK_SUCCESS_AUDIO_SIZE, CHECK_SUCCESS_SAMPLE_RATE);
}

/**
  * @brief  音频存储测试函数
  * @param  None
  * @retval None
  */
void AudioFlashStorage_Test(void)
{
    COM_DEBUG("=== 音频Flash存储测试 ===\r\n");
    
    // 先运行直接播放测试
    AudioFlashStorage_PlayDirectTest();
    HAL_Delay(1000); // 等待1秒
    
    // 初始化音频存储系统
    if(AudioFlashStorage_Init() != 0)
    {
        COM_DEBUG("音频存储系统初始化失败!\r\n");
        return;
    }
    
    // 创建一些示例音频数据 (简单的方波，用于测试)
    uint8_t check_ok_audio[512];
    for(int i = 0; i < 512; i++)
    {
        // 生成简单的音频波形（方波，频率约1000Hz，持续约0.5秒）
        check_ok_audio[i] = (i % 10 < 5) ? 64 : 192;  // 8位音频数据
    }
    
    // 保存"打卡成功"音频
    COM_DEBUG("保存'打卡成功'音频...\r\n");
    if(AudioFlashStorage_SaveAudio("check_ok.bin", check_ok_audio, 512, 8000, 8) == 0)
    {
        COM_DEBUG("'打卡成功'音频保存成功!\r\n");
        
        // 播放测试
        COM_DEBUG("播放'打卡成功'音频测试...\r\n");
        AudioFlashStorage_PlayAudio("check_ok.bin");
    }
    else
    {
        COM_DEBUG("音频保存失败!\r\n");
    }
    
    // 创建另一个音频数据（更高频率）
    uint8_t error_audio[256];
    for(int i = 0; i < 256; i++)
    {
        error_audio[i] = (i % 4 < 2) ? 32 : 224;  // 更高频的方波
    }
    
    // 保存"打卡失败"音频
    COM_DEBUG("保存'打卡失败'音频...\r\n");
    if(AudioFlashStorage_SaveAudio("check_fail.bin", error_audio, 256, 8000, 8) == 0)
    {
        COM_DEBUG("'打卡失败'音频保存成功!\r\n");
        
        // 播放测试
        COM_DEBUG("播放'打卡失败'音频测试...\r\n");
        AudioFlashStorage_PlayAudio("check_fail.bin");
    }
    
    COM_DEBUG("=== 音频Flash存储测试完成 ===\r\n");
}

/**
  * @brief  最简单的音频播放测试函数
  * @param  none
  * @retval none
  */
void AudioFlashStorage_SimpleTest(void)
{
    COM_DEBUG("开始简单音频播放测试\r\n");
    
    // 启动DAC通道
    if(HAL_DAC_Start(&hdac, DAC_CHANNEL_1) != HAL_OK)
    {
        COM_DEBUG("DAC启动失败\r\n");
        return;
    }
    
    // 启用音频输出
    Mute_Enable();
    COM_DEBUG("音频输出已启用\r\n");
    
    // 播放一个简单的方波信号作为测试
    for(int cycle = 0; cycle < 100; cycle++) // 播放100个周期
    {
        // 播放高电平 (模拟方波的上半部分)
        for(int i = 0; i < 40; i++) // 每个电平持续40个样本 (约8000Hz/200Hz = 40 samples per half-cycle)
        {
            HAL_DAC_SetValue(&hdac, DAC_CHANNEL_1, DAC_ALIGN_12B_R, 3072); // 高电平 (3/4 Vref)
            
            // 精确延时，确保200Hz的方波
            for(volatile uint32_t j = 0; j < 900; j++) {
                __NOP();
            }
        }
        
        // 播放低电平 (模拟方波的下半部分)
        for(int i = 0; i < 40; i++)
        {
            HAL_DAC_SetValue(&hdac, DAC_CHANNEL_1, DAC_ALIGN_12B_R, 1024); // 低电平 (1/4 Vref)
            
            // 精确延时
            for(volatile uint32_t j = 0; j < 900; j++) {
                __NOP();
            }
        }
    }
    
    // 恢复DAC到中间值
    HAL_DAC_SetValue(&hdac, DAC_CHANNEL_1, DAC_ALIGN_12B_R, 2048);
    
    // 关闭音频输出
    Mute_Disable();
    
    // 停止DAC通道
    HAL_DAC_Stop(&hdac, DAC_CHANNEL_1);
    
    COM_DEBUG("简单音频播放测试完成\r\n");
}

/**
  * @brief  将音频数据烧录到W25Q32 Flash中
  * @param  name: 音频文件名
  * @param  audio_data: 音频数据指针
  * @param  data_size: 音频数据大小
  * @retval 0: 成功, -1: 失败
  */
int8_t AudioFlashStorage_BurnToFlash(const char* name, const uint8_t* audio_data, uint32_t data_size)
{
    COM_DEBUG("开始烧录音频到Flash: %s, 大小: %lu 字节\r\n", name, data_size);
    
    if(audio_data == NULL || data_size == 0 || name == NULL) {
        COM_DEBUG("音频数据或名称为空\r\n");
        return -1;
    }
    
    // 保存音频数据到Flash，使用8000Hz采样率，8位位深度
    int8_t result = AudioFlashStorage_SaveAudio(name, (uint8_t*)audio_data, data_size, 8000, 8);
    
    if(result == 0) {
        COM_DEBUG("音频数据烧录成功: %s\r\n", name);
    } else {
        COM_DEBUG("音频数据烧录失败: %s, 错误码: %d\r\n", name, result);
    }
    
    return result;
}

/**
  * @brief  直接播放音频数组（不从Flash读取）
  * @param  audio_data: 音频数据数组
  * @param  data_size: 音频数据大小
  * @param  sample_rate: 采样率
  * @retval 0: 成功, -1: 失败
  */

/**
  * @brief  直接播放音频数据（固定8000Hz采样率）
  * @param  audio_data: 音频数据指针
  * @param  data_size: 音频数据大小
  * @retval 0: 成功, -1: 失败
  */
int8_t AudioFlashStorage_PlayDirectAudio(const uint8_t* audio_data, uint32_t data_size)
{
    COM_DEBUG("直接播放音频数据，大小: %lu 字节，采样率: 8000Hz (固定)\r\n", data_size);
    
    if(audio_data == NULL || data_size == 0) return -1;
    
    // 调用正确的中断方式播放函数
    int8_t result = AudioFlashStorage_PlayRawAudio(audio_data, data_size, 8000);
    
    COM_DEBUG("直接音频播放完成\r\n");
    return result;
}

/**
  * @brief  简化版直接音频播放函数（用于调试）
  * @param  audio_data: 音频数据指针
  * @param  data_size: 音频数据大小
  * @retval 0: 成功, -1: 失败
  */
int8_t AudioFlashStorage_PlayDirectAudioSimple(const uint8_t* audio_data, uint32_t data_size)
{
    COM_DEBUG("简化版直接播放音频数据，大小: %lu 字节\r\n", data_size);
    
    if(audio_data == NULL || data_size == 0) return -1;
    
    // 确保DAC已初始化
    // 启动DAC通道
    if(HAL_DAC_Start(&hdac, DAC_CHANNEL_1) != HAL_OK)
    {
        COM_DEBUG("DAC启动失败\r\n");
        return -1;
    }
    
    // 启用音频输出
    Mute_Enable();
    
    // 播放音频数据
    for(uint32_t i = 0; i < data_size; i++)
    {
        uint8_t raw_sample = audio_data[i];
        
        // 按照参考解码程序的思路，直接将8位数据映射到12位DAC
        // 参考解码程序: AD_VALUE = *Sound_codeIndex;
        // 这意味着直接使用原始数据
        uint16_t dac_value = (uint16_t)raw_sample << 4;  // 左移4位相当于乘以16，将8位扩展到12位
        
        // 确保在有效范围内
        if(dac_value > 4095) dac_value = 4095;
        
        // 输出到DAC
        HAL_DAC_SetValue(&hdac, DAC_CHANNEL_1, DAC_ALIGN_12B_R, dac_value);
        
        // 精确延时以达到8000Hz采样率
        // 每个样本需要125微秒间隔
        HAL_Delay(1); // 简单测试，后续需要精确延时
        
        // 每8000个样本大约延时1秒，所以每125微秒需要精确延时
        // 使用循环延时更精确
        for(volatile uint32_t delay = 0; delay < 900; delay++) {
            __NOP();
        }
    }
    
    // 恢复DAC到中间值
    HAL_DAC_SetValue(&hdac, DAC_CHANNEL_1, DAC_ALIGN_12B_R, 2048);
    
    // 关闭音频输出
    Mute_Disable();
    
    // 停止DAC通道
    HAL_DAC_Stop(&hdac, DAC_CHANNEL_1);
    
    COM_DEBUG("简化版直接音频播放完成\r\n");
    return 0;
}



/**
  * @brief  音频播放定时器回调函数（由定时器中断调用）
  * @param  htim: 定时器句柄
  * @retval None
  */
void AudioPlay_TimerCallback(TIM_HandleTypeDef *htim)
{
    if(htim->Instance == TIM6 && g_audio_playing && g_current_audio_data && g_audio_index < g_audio_length)
    {
        // 获取当前音频样本
        uint8_t raw_sample = g_current_audio_data[g_audio_index];
        
        // 将8位数据映射到12位DAC范围
        uint16_t dac_value = (uint16_t)((((uint32_t)raw_sample) * 4095) / 255);
        
        // 应用音量控制
        dac_value = (dac_value * AUDIO_VOLUME_GAIN) / 16;
        
        // 确保在有效范围内
        if(dac_value > 4095) dac_value = 4095;
        
        // 输出到DAC
        HAL_DAC_SetValue(&hdac, DAC_CHANNEL_1, DAC_ALIGN_12B_R, dac_value);
        
        // 移动到下一个样本
        g_audio_index++;
    }
    else if(g_audio_index >= g_audio_length && g_audio_playing)
    {
        // 播放完成，停止播放
        g_audio_playing = 0;
        
        // 停止定时器
        HAL_TIM_Base_Stop_IT(htim);
        
        // 恢复DAC到中间值
        HAL_DAC_SetValue(&hdac, DAC_CHANNEL_1, DAC_ALIGN_12B_R, 2048);
        
        // 关闭音频输出
        Mute_Disable();
    }
}

/**
  * @brief  直接播放音频数组（使用定时器中断，精确时序）
  * @param  audio_data: 音频数据数组
  * @param  data_size: 音频数据大小
  * @param  sample_rate: 采样率
  * @retval 0: 成功, -1: 失败
  */
int8_t AudioFlashStorage_PlayRawAudio(const uint8_t* audio_data, uint32_t data_size, uint32_t sample_rate)
{
 if(audio_data == NULL || data_size == 0) {
        return -1;
    }
    
    // 启动DAC通道
    if(HAL_DAC_Start(&hdac, DAC_CHANNEL_1) != HAL_OK)
    {
        return -1;
    }
    
    // 启用音频输出
    Mute_Enable();
    
    // 确保DAC初始值为中间值
    HAL_DAC_SetValue(&hdac, DAC_CHANNEL_1, DAC_ALIGN_12B_R, 2048);
    
    // 停止现有定时器
    HAL_TIM_Base_Stop_IT(&htim6);
    
    // 配置TIM6用于音频播放
    TIM_HandleTypeDef htim6_temp = htim6;
    
    uint16_t prescaler;
    uint16_t period;
    
    if(sample_rate == 8000) {
        prescaler = 71;  // 72MHz/(71+1) = 1MHz
        period = 124;    // 1MHz / 125 = 8000Hz (每125微秒中断一次)
    } else {
        prescaler = 71;  // 保持1MHz计数频率
        period = (1000000 / sample_rate) - 1;  // 根据采样率计算周期
    }
    
    // 重新配置定时器
    htim6_temp.Init.Prescaler = prescaler;
    htim6_temp.Init.CounterMode = TIM_COUNTERMODE_UP;
    htim6_temp.Init.Period = period;
    htim6_temp.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    htim6_temp.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    
    if (HAL_TIM_Base_Init(&htim6_temp) != HAL_OK)
    {
        g_audio_playing = 0;
        return -1;
    }
    
    // 启动定时器中断
    if (HAL_TIM_Base_Start_IT(&htim6_temp) != HAL_OK)
    {
        g_audio_playing = 0;
        return -1;
    }
    
    // 设置全局变量
    g_current_audio_data = audio_data;
    g_audio_length = data_size;
    g_audio_index = 0;
    g_audio_playing = 1;
    
    // 不等待播放完成，立即返回
    return 0;
}

// 添加检查播放状态的函数
uint8_t AudioFlashStorage_IsPlaying(void)
{
    return g_audio_playing;
}
