#ifndef RC522_APP_H
#define RC522_APP_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f1xx_hal.h"

#define RC522_UID_MAX_LEN              10U
#define RC522_MIFARE_BLOCK_SIZE        16U

typedef enum
{
  RC522_OK = 0x00U,
  RC522_NO_TAG = 0x01U,
  RC522_ERROR = 0x02U,
  RC522_AUTH_ERROR = 0x03U,
  RC522_CRC_ERROR = 0x04U,
} RC522_StatusTypeDef;
uint8_t RC522_ReadRegister(uint8_t reg);
typedef struct
{
  uint8_t uid[RC522_UID_MAX_LEN];
  uint8_t uid_len;
  uint8_t sak;
} RC522_CardInfoTypeDef;

void RC522_Init(void);
RC522_StatusTypeDef RC522_IsCardPresent(void);
RC522_StatusTypeDef RC522_ReadCard(RC522_CardInfoTypeDef *card);
RC522_StatusTypeDef RC522_MifareReadBlock(uint8_t block_addr,
                                          const uint8_t key[6],
                                          const uint8_t uid[4],
                                          uint8_t data[RC522_MIFARE_BLOCK_SIZE]);
RC522_StatusTypeDef RC522_MifareWriteBlock(uint8_t block_addr,
                                           const uint8_t key[6],
                                           const uint8_t uid[4],
                                           const uint8_t data[RC522_MIFARE_BLOCK_SIZE]);
void RC522_Halt(void);

#ifdef __cplusplus
}
#endif

#endif
