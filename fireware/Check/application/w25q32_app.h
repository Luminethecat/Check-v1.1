#ifndef W25Q32_APP_H
#define W25Q32_APP_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f1xx_hal.h"

#define W25Q32_PAGE_SIZE               256U
#define W25Q32_SECTOR_SIZE             4096U

typedef enum
{
  W25Q32_OK = 0x00U,
  W25Q32_ERROR = 0x01U,
  W25Q32_TIMEOUT = 0x02U,
  W25Q32_BUSY = 0x03U,
} W25Q32_StatusTypeDef;

void W25Q32_Init(void);
W25Q32_StatusTypeDef W25Q32_ReadJedecId(uint8_t jedec_id[3]);
W25Q32_StatusTypeDef W25Q32_ReadData(uint32_t address, uint8_t *buffer, uint32_t length);
W25Q32_StatusTypeDef W25Q32_PageProgram(uint32_t address, const uint8_t *buffer, uint16_t length);
W25Q32_StatusTypeDef W25Q32_SectorErase(uint32_t address);
W25Q32_StatusTypeDef W25Q32_WaitWhileBusy(uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif
