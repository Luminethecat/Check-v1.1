#include "app_services.h"

RC522_StatusTypeDef App_Rc522_ReadBlock(uint8_t block_addr,
                                        const uint8_t key[6],
                                        RC522_CardInfoTypeDef *card,
                                        uint8_t data[RC522_MIFARE_BLOCK_SIZE])
{
  RC522_StatusTypeDef status;

  if ((card == NULL) || (data == NULL))
  {
    return RC522_ERROR;
  }

  status = RC522_ReadCard(card);
  if (status != RC522_OK)
  {
    return status;
  }

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

  if ((card == NULL) || (data == NULL))
  {
    return RC522_ERROR;
  }

  status = RC522_ReadCard(card);
  if (status != RC522_OK)
  {
    return status;
  }

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

  return ZW101_Identify(result);
}

ZW101_StatusTypeDef App_Zw101_DeleteUser(uint32_t password, uint16_t user_id)
{
  ZW101_StatusTypeDef status;

  status = ZW101_VerifyPassword(password);
  if (status != ZW101_OK)
  {
    return status;
  }

  return ZW101_DeleteModel(user_id, 1U);
}

ZW101_StatusTypeDef App_Zw101_ClearLibrary(uint32_t password)
{
  ZW101_StatusTypeDef status;

  status = ZW101_VerifyPassword(password);
  if (status != ZW101_OK)
  {
    return status;
  }

  return ZW101_EmptyLibrary();
}
