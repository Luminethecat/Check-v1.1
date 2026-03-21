#ifndef APP_SERVICES_H
#define APP_SERVICES_H

#ifdef __cplusplus
extern "C" {
#endif

#include "audio_dac_app.h"
#include "rc522_app.h"
#include "w25q32_app.h"
#include "zw101_app.h"

/* 业务组合接口：
 * 面向上层任务，隐藏底层驱动的调用顺序。 */
RC522_StatusTypeDef App_Rc522_ReadBlock(uint8_t block_addr,
                                        const uint8_t key[6],
                                        RC522_CardInfoTypeDef *card,
                                        uint8_t data[RC522_MIFARE_BLOCK_SIZE]);
RC522_StatusTypeDef App_Rc522_WriteBlock(uint8_t block_addr,
                                         const uint8_t key[6],
                                         RC522_CardInfoTypeDef *card,
                                         const uint8_t data[RC522_MIFARE_BLOCK_SIZE]);

ZW101_StatusTypeDef App_Zw101_EnrollUser(uint32_t password, uint16_t user_id);
ZW101_StatusTypeDef App_Zw101_IdentifyUser(uint32_t password, ZW101_SearchResultTypeDef *result);

W25Q32_StatusTypeDef App_W25Q32_ReadBytes(uint32_t flash_addr, uint8_t *buffer, uint32_t length);
AudioDac_StatusTypeDef App_Audio_PlayFromFlashU8(uint32_t flash_addr,
                                                 uint8_t *cache,
                                                 uint32_t sample_count,
                                                 uint32_t sample_rate_hz);

#ifdef __cplusplus
}
#endif

#endif
