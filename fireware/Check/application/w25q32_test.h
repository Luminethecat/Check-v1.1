/**
  ******************************************************************************
  * @file    w25q32_test.h
  * @brief   W25Q32 SPI Flash 测试头文件
  ******************************************************************************
  */

#ifndef __W25Q32_TEST_H
#define __W25Q32_TEST_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f1xx_hal.h"
#include <stdint.h>

// 外部函数声明 - 从w25q32_test.c导入
extern void W25Q32_Test_Init(void);
extern uint32_t W25Q32_ReadJEDECID(void);
extern void W25Q32_WriteEnable(void);
extern uint8_t W25Q32_ReadStatusRegister(void);
extern void W25Q32_Read(uint8_t* pBuffer, uint32_t ReadAddr, uint16_t NumByteToRead);
extern void W25Q32_WritePage(uint8_t* pBuffer, uint32_t WriteAddr, uint16_t NumByteToWrite);
extern void W25Q32_EraseSector(uint32_t SectorAddr);
extern void W25Q32_FullTest(void);
extern void W25Q32_DataStorageTest(void);
extern void W25Q32_ChipErase(void);
#ifdef __cplusplus
}
#endif

#endif /* __W25Q32_TEST_H */