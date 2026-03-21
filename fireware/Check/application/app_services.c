#include "app_services.h"

/* 这里封装“卡/指纹/Flash/音频”的组合操作，
 * 让上层业务不需要每次自己拼底层驱动调用顺序。 */

RC522_StatusTypeDef App_Rc522_ReadBlock(uint8_t block_addr,
                                        const uint8_t key[6],
                                        RC522_CardInfoTypeDef *card,
                                        uint8_t data[RC522_MIFARE_BLOCK_SIZE])
{
  RC522_StatusTypeDef status;

  if (card == NULL || data == NULL)
  {
    return RC522_ERROR;
  }

  status = RC522_ReadCard(card);
  if (status != RC522_OK)
  {
    return status;
  }

  /* RC522 的 Mifare 读写需要先选卡，再访问块，结束后主动 Halt。 */
  status = RC522_MifareReadBlock(block_addr, key, card->uid, data);
  RC522_Halt();
  return status;
}

RC522_StatusTypeDef App_Rc522_WriteBlock(uint8_t block_addr,
                                         const uint8_t key[6],
                                         RC522_CardInfoTypeDef *card,
                                         const uint8_t data[RC522_MIFARE_BLOCK_SIZE])
{
  RC522_StatusTypeDef status;

  if (card == NULL || data == NULL)
  {
    return RC522_ERROR;
  }

  status = RC522_ReadCard(card);
  if (status != RC522_OK)
  {
    return status;
  }

  /* 写块同样在业务层统一收尾，避免上层忘记停卡。 */
  status = RC522_MifareWriteBlock(block_addr, key, card->uid, data);
  RC522_Halt();
  return status;
}

ZW101_StatusTypeDef App_Zw101_EnrollUser(uint32_t password, uint16_t user_id)
{
  ZW101_StatusTypeDef status;

  status = ZW101_VerifyPassword(password);
  if (status != ZW101_OK)
  {
    return status;
  }

  /* 录入前先验密，保证指纹模块处于可配置状态。 */
  return ZW101_Enroll(user_id);
}

ZW101_StatusTypeDef App_Zw101_IdentifyUser(uint32_t password, ZW101_SearchResultTypeDef *result)
{
  ZW101_StatusTypeDef status;

  status = ZW101_VerifyPassword(password);
  if (status != ZW101_OK)
  {
    return status;
  }

  /* 识别流程同样先验密，便于和录入流程保持一致。 */
  return ZW101_Identify(result);
}

W25Q32_StatusTypeDef App_W25Q32_ReadBytes(uint32_t flash_addr, uint8_t *buffer, uint32_t length)
{
  return W25Q32_ReadData(flash_addr, buffer, length);
}

AudioDac_StatusTypeDef App_Audio_PlayFromFlashU8(uint32_t flash_addr,
                                                 uint8_t *cache,
                                                 uint32_t sample_count,
                                                 uint32_t sample_rate_hz)
{
  if (cache == NULL)
  {
    return AUDIO_DAC_ERROR;
  }

  if (W25Q32_ReadData(flash_addr, cache, sample_count) != W25Q32_OK)
  {
    return AUDIO_DAC_ERROR;
  }

  /* 当前版本先把一段音频完整读入 RAM，再交给 DAC DMA 播放。 */
  return AudioDac_PlayU8Mono(cache, sample_count, sample_rate_hz);
}
