/**
  ******************************************************************************
  * @file    w25q32_test.c
  * @brief   W25Q32 SPI Flash 测试程序 - 修复版本，移除音频相关函数
  ******************************************************************************
  */

#include "stm32f1xx_hal.h"
#include "main.h"
#include <stdio.h>
#include <string.h>
#include "spi.h"
#include "w25q32_test.h"
#include "Com_debug.h"
// W25Q32命令定义
#define W25X_WriteEnable        0x06
#define W25X_WriteDisable       0x04
#define W25X_ReadStatusReg      0x05
#define W25X_WriteStatusReg     0x01
#define W25X_ReadData           0x03
#define W25X_FastReadData       0x0B
#define W25X_FastReadDual       0x3B
#define W25X_PageProgram        0x02
#define W25X_BlockErase         0xD8
#define W25X_SectorErase        0x20
#define W25X_ChipErase          0xC7
#define W25X_PowerDown          0xB9
#define W25X_ReleasePowerDown   0xAB
#define W25X_DeviceID           0xAB
#define W25X_ManufactDeviceID   0x90
#define W25X_JedecDeviceID      0x9F

// 状态寄存器位定义
#define W25X_WIP_FLAG           0x01    // 写入进行中标志
#define W25X_WEL_FLAG           0x02    // 写使能锁存标志

// CS片选引脚控制
#define W25Q32_CS_LOW()     HAL_GPIO_WritePin(SPI1_CS_GPIO_Port, SPI1_CS_Pin, GPIO_PIN_RESET)
#define W25Q32_CS_HIGH()    HAL_GPIO_WritePin(SPI1_CS_GPIO_Port, SPI1_CS_Pin, GPIO_PIN_SET)

// 串口句柄
extern UART_HandleTypeDef huart3;

// 私有函数声明
void W25Q32_WriteEnable(void);
uint8_t W25Q32_ReadStatusRegister(void);
void W25Q32_WriteStatusRegister(uint8_t status);
static void W25Q32_SPI_TransmitReceive(uint8_t *tx_data, uint8_t *rx_data, uint16_t size);
static void W25Q32_SPI_Transmit(uint8_t *tx_data, uint16_t size);
static void W25Q32_WaitForWriteComplete(void);

/**
  * @brief  W25Q32初始化
  * @param  None
  * @retval None
  */
void W25Q32_Test_Init(void)
{
    // 初始化CS引脚为高电平（不选中）
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    
    // 使能GPIO时钟
    __HAL_RCC_GPIOB_CLK_ENABLE();
    
    // 配置CS引脚为推挽输出
    GPIO_InitStruct.Pin = SPI1_CS_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(SPI1_CS_GPIO_Port, &GPIO_InitStruct);
    
    // 拉高CS引脚，不选中Flash
    W25Q32_CS_HIGH();
    
    // 等待Flash稳定
    HAL_Delay(100);
    
    COM_DEBUG("W25Q32 初始化完成\r\n");
}

/**
  * @brief  读取W25Q32 JEDEC ID
  * @param  None
  * @retval 芯片ID (24位)
  */
uint32_t W25Q32_ReadJEDECID(void)
{
    uint8_t tx_data[4] = {W25X_JedecDeviceID, 0x00, 0x00, 0x00};
    uint8_t rx_data[4] = {0};
    
    W25Q32_CS_LOW();
    HAL_Delay(1);
    W25Q32_SPI_TransmitReceive(tx_data, rx_data, 4);
    W25Q32_CS_HIGH();
    HAL_Delay(1);
    
    // 返回制造商ID + 设备ID (24位)
    uint32_t id = ((uint32_t)rx_data[1] << 16) | ((uint32_t)rx_data[2] << 8) | rx_data[3];
    return id;
}

/**
  * @brief  读取W25Q32状态寄存器
  * @param  None
  * @retval 状态寄存器值
  */
uint8_t W25Q32_ReadStatusRegister(void)
{
    uint8_t tx_data[2] = {W25X_ReadStatusReg, 0x00};
    uint8_t rx_data[2] = {0};
    
    W25Q32_CS_LOW();
    HAL_Delay(1);
    W25Q32_SPI_TransmitReceive(tx_data, rx_data, 2);
    W25Q32_CS_HIGH();
    HAL_Delay(1);
    
    return rx_data[1];
}

/**
  * @brief  等待写操作完成
  * @param  None
  * @retval None
  */
static void W25Q32_WaitForWriteComplete(void)
{
    uint32_t timeout = 100000;
    while(timeout--)
    {
        if((W25Q32_ReadStatusRegister() & W25X_WIP_FLAG) == 0)  // 检查WIP位
            return;
        HAL_Delay(1);  // 延迟一点避免过度轮询
    }
}

/**
  * @brief  使能写操作
  * @param  None
  * @retval None
  */
void W25Q32_WriteEnable(void)
{
    uint8_t cmd = W25X_WriteEnable;
    
    W25Q32_CS_LOW();
    HAL_Delay(1);
    W25Q32_SPI_Transmit(&cmd, 1);
    W25Q32_CS_HIGH();
    HAL_Delay(1);
}

/**
  * @brief  读取数据
  * @param  pBuffer: 数据缓冲区
  * @param  ReadAddr: 读取地址
  * @param  NumByteToRead: 读取字节数
  * @retval None
  */
void W25Q32_Read(uint8_t* pBuffer, uint32_t ReadAddr, uint16_t NumByteToRead)
{
    uint8_t cmd_addr[4] = {W25X_ReadData, 
                          (uint8_t)((ReadAddr & 0xFF0000) >> 16),
                          (uint8_t)((ReadAddr & 0xFF00) >> 8),
                          (uint8_t)(ReadAddr & 0xFF)};
    
    W25Q32_CS_LOW();
    HAL_Delay(1);
    W25Q32_SPI_Transmit(cmd_addr, 4);
    
    // 接收数据
    for(uint16_t i = 0; i < NumByteToRead; i++)
    {
        pBuffer[i] = 0xFF;  // 预填充
    }
    W25Q32_SPI_TransmitReceive(pBuffer, pBuffer, NumByteToRead);
    W25Q32_CS_HIGH();
    HAL_Delay(1);
}

/**
  * @brief  写入数据到页（不超过一页256字节）
  * @param  pBuffer: 数据缓冲区
  * @param  WriteAddr: 写入地址
  * @param  NumByteToWrite: 写入字节数
  * @retval None
  */
void W25Q32_WritePage(uint8_t* pBuffer, uint32_t WriteAddr, uint16_t NumByteToWrite)
{
    uint8_t cmd_addr[4] = {W25X_PageProgram, 
                          (uint8_t)((WriteAddr & 0xFF0000) >> 16),
                          (uint8_t)((WriteAddr & 0xFF00) >> 8),
                          (uint8_t)(WriteAddr & 0xFF)};
    
    // 确保写使能
    W25Q32_WriteEnable();
    
    W25Q32_CS_LOW();
    HAL_Delay(1);
    W25Q32_SPI_Transmit(cmd_addr, 4);
    W25Q32_SPI_Transmit(pBuffer, NumByteToWrite);
    W25Q32_CS_HIGH();
    
    // 等待写操作完成
    W25Q32_WaitForWriteComplete();
}

/**
  * @brief  擦除扇区（4KB）
  * @param  SectorAddr: 扇区地址
  * @retval None
  */
void W25Q32_EraseSector(uint32_t SectorAddr)
{
    uint8_t cmd_addr[4] = {W25X_SectorErase, 
                          (uint8_t)((SectorAddr & 0xFF0000) >> 16),
                          (uint8_t)((SectorAddr & 0xFF00) >> 8),
                          (uint8_t)(SectorAddr & 0xFF)};
    
    // 确保写使能
    W25Q32_WriteEnable();
    
    W25Q32_CS_LOW();
    HAL_Delay(1);
    W25Q32_SPI_Transmit(cmd_addr, 4);
    W25Q32_CS_HIGH();
    
    // 等待擦除操作完成 (扇区擦除可能需要较长时间)
    W25Q32_WaitForWriteComplete();
}

/**
  * @brief  擦除整个芯片
  * @param  None
  * @retval None
  */
void W25Q32_ChipErase(void)
{
    uint8_t cmd = W25X_ChipErase;
    
    // 确保写使能
    W25Q32_WriteEnable();
    
    W25Q32_CS_LOW();
    HAL_Delay(1);
    W25Q32_SPI_Transmit(&cmd, 1);
    W25Q32_CS_HIGH();
    
    // 芯片擦除需要很长时间，可能长达几十秒
    COM_DEBUG("芯片擦除中，请稍候...\r\n");
    W25Q32_WaitForWriteComplete();
    COM_DEBUG("芯片擦除完成\r\n");
}

/**
  * @brief  通过SPI发送和接收数据
  * @param  tx_data: 发送数据缓冲区
  * @param  rx_data: 接收数据缓冲区
  * @param  size: 数据大小
  * @retval None
  */
static void W25Q32_SPI_TransmitReceive(uint8_t *tx_data, uint8_t *rx_data, uint16_t size)
{
    HAL_SPI_TransmitReceive(&hspi1, tx_data, rx_data, size, 1000);
}

/**
  * @brief  通过SPI发送数据
  * @param  tx_data: 发送数据缓冲区
  * @param  size: 数据大小
  * @retval None
  */
static void W25Q32_SPI_Transmit(uint8_t *tx_data, uint16_t size)
{
    HAL_SPI_Transmit(&hspi1, tx_data, size, 1000);
}

/**
  * @brief  W25Q32数据存储测试函数
  * @param  None
  * @retval None
  */
void W25Q32_DataStorageTest(void)
{
    COM_DEBUG("=== W25Q32 数据存储测试开始 ===\r\n");
    
    // 为了测试多次写入，我们使用不同的扇区
    uint32_t test_address1 = 0x000000;  // 第一个扇区
    uint32_t test_address2 = 0x001000;  // 第二个扇区 (4KB偏移)
    
    uint8_t test_data1[64];
    uint8_t test_data2[64];
    uint8_t read_back[64];
    
    // 准备第一组测试数据
    for(int i = 0; i < 64; i++)
    {
        test_data1[i] = 0x55 + i;  // 从0x55开始递增
    }
    
    // 准备第二组测试数据
    for(int i = 0; i < 64; i++)
    {
        test_data2[i] = 0xAA + i;  // 从0xAA开始递增
    }
    
    // 测试1: 第一个扇区数据存储测试
    COM_DEBUG("测试1: 第一个扇区数据存储测试\r\n");
    COM_DEBUG("准备擦除扇区 0x%06lX\r\n", test_address1);
    W25Q32_EraseSector(test_address1);
    printf("扇区擦除完成\r\n");
    
    printf("写入第一组测试数据到地址 0x%06lX\r\n", test_address1);
    W25Q32_WritePage(test_data1, test_address1, 64);
    printf("写入完成\r\n");
    
    // 短暂延迟确保写入完成
    HAL_Delay(10);
    
    printf("读取验证第一组数据...\r\n");
    W25Q32_Read(read_back, test_address1, 64);
    
    // 验证第一组数据
    uint8_t data_match = 1;
    for(int i = 0; i < 64; i++)
    {
        if(test_data1[i] != read_back[i])
        {
            printf("第一组数据不匹配! 位置 %d: 期望 0x%02X, 实际 0x%02X\r\n", 
                   i, test_data1[i], read_back[i]);
            data_match = 0;
            break;
        }
    }
    
    if(data_match)
    {
        printf("✓ 第一个扇区数据存储测试通过\r\n");
        printf("  - 写入数据示例 (前10字节): ");
        for(int i = 0; i < 10; i++)
        {
            printf("0x%02X ", read_back[i]);
        }
        printf("\r\n");
    }
    else
    {
        printf("✗ 第一个扇区数据存储测试失败\r\n");
        return;
    }
    
    // 测试2: 第二个扇区数据存储测试 (新数据)
    printf("测试2: 第二个扇区数据存储测试\r\n");
    printf("准备擦除扇区 0x%06lX\r\n", test_address2);
    W25Q32_EraseSector(test_address2);
    printf("扇区擦除完成\r\n");
    
    printf("写入第二组测试数据到地址 0x%06lX\r\n", test_address2);
    W25Q32_WritePage(test_data2, test_address2, 64);
    printf("写入完成\r\n");
    
    // 短暂延迟确保写入完成
    HAL_Delay(10);
    
    printf("读取验证第二组数据...\r\n");
    W25Q32_Read(read_back, test_address2, 64);
    
    // 验证第二组数据
    data_match = 1;
    for(int i = 0; i < 64; i++)
    {
        if(test_data2[i] != read_back[i])
        {
            printf("第二组数据不匹配! 位置 %d: 期望 0x%02X, 实际 0x%02X\r\n", 
                   i, test_data2[i], read_back[i]);
            data_match = 0;
            break;
        }
    }
    
    if(data_match)
    {
        printf("✓ 第二个扇区数据存储测试通过\r\n");
        printf("  - 写入数据示例 (前10字节): ");
        for(int i = 0; i < 10; i++)
        {
            printf("0x%02X ", read_back[i]);
        }
        printf("\r\n");
    }
    else
    {
        printf("✗ 第二个扇区数据存储测试失败\r\n");
        return;
    }
    
    // 测试3: 回读第一个扇区验证数据持久性
    printf("测试3: 验证第一个扇区数据持久性\r\n");
    printf("读取第一个扇区原有数据...\r\n");
    W25Q32_Read(read_back, test_address1, 64);
    
    // 验证第一组数据仍然存在
    data_match = 1;
    for(int i = 0; i < 64; i++)
    {
        if(test_data1[i] != read_back[i])
        {
            printf("第一组数据丢失! 位置 %d: 期望 0x%02X, 实际 0x%02X\r\n", 
                   i, test_data1[i], read_back[i]);
            data_match = 0;
            break;
        }
    }
    
    if(data_match)
    {
        printf("✓ 数据持久性测试通过\r\n");
        printf("  - 第一个扇区数据保持完好\r\n");
    }
    else
    {
        printf("✗ 数据持久性测试失败\r\n");
        return;
    }
    
    // 测试4: 页面内不同地址写入测试
    printf("测试4: 页面内不同地址写入测试\r\n");
    uint32_t page_test_addr = 0x002000;  // 第三个扇区
    uint32_t page_offset_addr = 0x0020F0;  // 第三页接近末尾的位置
    
    printf("准备擦除扇区 0x%06lX\r\n", page_test_addr);
    W25Q32_EraseSector(page_test_addr);
    printf("扇区擦除完成\r\n");
    
    uint8_t offset_data[16];
    for(int i = 0; i < 16; i++)
    {
        offset_data[i] = 0x80 + i;
    }
    
    printf("在页面偏移地址写入数据 (0x%06lX)\r\n", page_offset_addr);
    W25Q32_WritePage(offset_data, page_offset_addr, 16);
    HAL_Delay(10);
    
    printf("读取验证偏移地址数据...\r\n");
    W25Q32_Read(read_back, page_offset_addr, 16);
    
    // 验证偏移地址数据
    data_match = 1;
    for(int i = 0; i < 16; i++)
    {
        if(offset_data[i] != read_back[i])
        {
            printf("偏移地址数据不匹配! 位置 %d: 期望 0x%02X, 实际 0x%02X\r\n", 
                   i, offset_data[i], read_back[i]);
            data_match = 0;
            break;
        }
    }
    
    if(data_match)
    {
        printf("✓ 页面内偏移地址写入测试通过\r\n");
        printf("  - 偏移数据: ");
        for(int i = 0; i < 16; i++)
        {
            printf("0x%02X ", read_back[i]);
        }
        printf("\r\n");
    }
    else
    {
        printf("✗ 页面内偏移地址写入测试失败\r\n");
        return;
    }
    
    printf("=== W25Q32 数据存储测试全部通过 ===\r\n");
    printf("结论: W25Q32 可靠地支持数据存储功能\r\n");
    printf("  - 支持多扇区独立操作\r\n");
    printf("  - 数据持久性良好\r\n");
    printf("  - 支持页面内任意地址写入\r\n\r\n");
}

/**
  * @brief  W25Q32完整测试函数
  * @param  None
  * @retval None
  */
void W25Q32_FullTest(void)
{
    printf("=== W25Q32 SPI Flash 测试开始 ===\r\n");
    
    // 基础ID检测
    uint32_t jedec_id = W25Q32_ReadJEDECID();
    printf("JEDEC ID: 0x%06lX\r\n", jedec_id);
    
    uint8_t manufacturer = (jedec_id >> 16) & 0xFF;
    uint16_t device = (jedec_id & 0xFFFF);  // 修正：正确的设备ID解析
    
    printf("Manufacturer: 0x%02X, Device: 0x%04X\r\n", manufacturer, device);
    
    if(manufacturer == 0xEF && device == 0x4016)  // W25Q32
    {
        printf("检测到 W25Q32 芯片 - 测试通过!\r\n");
    }
    else if(manufacturer == 0xEF && device == 0x4015)  // W25Q16
    {
        printf("检测到 W25Q16 芯片 - 测试通过!\r\n");
    }
    else if(jedec_id == 0xFFFFFF || jedec_id == 0x000000)
    {
        printf("读取ID失败 - 检查SPI连接!\r\n");
        return;
    }
    else
    {
        printf("检测到其他Flash芯片 (0x%02X 0x%04X) - 继续测试\r\n", manufacturer, device);
    }
    
    // 状态寄存器测试
    uint8_t status_reg = W25Q32_ReadStatusRegister();
    printf("状态寄存器: 0x%02X\r\n", status_reg);
    
    // 执行数据存储测试
    W25Q32_DataStorageTest();
    
    printf("=== W25Q32 SPI Flash 测试结束 ===\r\n\r\n");
}