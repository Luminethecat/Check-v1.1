#ifndef ZW101_APP_H
#define ZW101_APP_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f1xx_hal.h"

#define ZW101_TEMPLATE_BUFFER_1        0x01U
#define ZW101_TEMPLATE_BUFFER_2        0x02U

typedef enum
{
  ZW101_OK = 0x00U,
  ZW101_ERROR = 0x01U,
  ZW101_TIMEOUT = 0x02U,
  ZW101_NO_FINGER = 0x03U,
  ZW101_NOT_FOUND = 0x04U,
  ZW101_PACKET_ERROR = 0x05U,
} ZW101_StatusTypeDef;

typedef struct
{
  uint16_t page_id;
  uint16_t match_score;
} ZW101_SearchResultTypeDef;

void ZW101_Init(void);
void ZW101_IrqNotify(void);
uint8_t ZW101_IrqConsumePending(void);
uint8_t ZW101_IrqIsActiveLevel(void);
ZW101_StatusTypeDef ZW101_VerifyPassword(uint32_t password);
ZW101_StatusTypeDef ZW101_CollectImage(void);
ZW101_StatusTypeDef ZW101_GenerateChar(uint8_t buffer_id);
ZW101_StatusTypeDef ZW101_CreateModel(void);
ZW101_StatusTypeDef ZW101_StoreModel(uint8_t buffer_id, uint16_t page_id);
ZW101_StatusTypeDef ZW101_DeleteModel(uint16_t page_id, uint16_t count);
ZW101_StatusTypeDef ZW101_EmptyLibrary(void);
ZW101_StatusTypeDef ZW101_Search(uint8_t buffer_id,
                                 uint16_t start_page,
                                 uint16_t page_count,
                                 ZW101_SearchResultTypeDef *result);
ZW101_StatusTypeDef ZW101_Enroll(uint16_t page_id);
ZW101_StatusTypeDef ZW101_Identify(ZW101_SearchResultTypeDef *result);

#ifdef __cplusplus
}
#endif

#endif
