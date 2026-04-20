#include "rc522_app.h"

#include "app_board.h"
#include "string.h"

#define RC522_CMD_IDLE                 0x00U
#define RC522_CMD_CALCCRC              0x03U
#define RC522_CMD_TRANSCEIVE           0x0CU
#define RC522_CMD_MFAUTHENT            0x0EU
#define RC522_CMD_SOFTRESET            0x0FU

#define RC522_REG_COMMAND              0x01U
#define RC522_REG_COMMIEN              0x02U
#define RC522_REG_COMMIRQ              0x04U
#define RC522_REG_DIVIRQ               0x05U
#define RC522_REG_ERROR                0x06U
#define RC522_REG_STATUS2              0x08U
#define RC522_REG_FIFO_DATA            0x09U
#define RC522_REG_FIFO_LEVEL           0x0AU
#define RC522_REG_CONTROL              0x0CU
#define RC522_REG_BIT_FRAMING          0x0DU
#define RC522_REG_MODE                 0x11U
#define RC522_REG_TX_MODE              0x12U
#define RC522_REG_RX_MODE              0x13U
#define RC522_REG_TX_CONTROL           0x14U
#define RC522_REG_TX_AUTO              0x15U
#define RC522_REG_CRC_RESULT_MSB       0x21U
#define RC522_REG_CRC_RESULT_LSB       0x22U
#define RC522_REG_T_MODE               0x2AU
#define RC522_REG_T_PRESCALER          0x2BU
#define RC522_REG_T_RELOAD_MSB         0x2CU
#define RC522_REG_T_RELOAD_LSB         0x2DU

#define PICC_CMD_REQA                  0x26U
#define PICC_CMD_ANTICOLL_CL1          0x93U
#define PICC_CMD_SELECT_CL1            0x93U
#define PICC_CMD_HLTA                  0x50U
#define PICC_CMD_MF_AUTH_KEY_A         0x60U
#define PICC_CMD_MF_READ               0x30U
#define PICC_CMD_MF_WRITE              0xA0U

static SPI_HandleTypeDef *const rc522_spi = &APP_RC522_SPI_HANDLE;

static void RC522_ResetPin(GPIO_PinState level)
{
  HAL_GPIO_WritePin(APP_RC522_RST_GPIO_Port, APP_RC522_RST_Pin, level);
}

static void RC522_HardReset(void)
{
  /* 先给 RC522 一个明确的硬复位脉冲，再做软复位，启动更稳。 */
  RC522_ResetPin(GPIO_PIN_RESET);
  HAL_Delay(2U);
  RC522_ResetPin(GPIO_PIN_SET);
  HAL_Delay(10U);
}

static void RC522_Select(void)
{
  HAL_GPIO_WritePin(APP_RC522_CS_GPIO_Port, APP_RC522_CS_Pin, GPIO_PIN_RESET);
}

static void RC522_Unselect(void)
{
  HAL_GPIO_WritePin(APP_RC522_CS_GPIO_Port, APP_RC522_CS_Pin, GPIO_PIN_SET);
}

static void RC522_WriteRegister(uint8_t reg, uint8_t value)
{
  uint8_t tx_data[2];

  tx_data[0] = (uint8_t)((reg << 1U) & 0x7EU);
  tx_data[1] = value;

  RC522_Select();
  (void)HAL_SPI_Transmit(rc522_spi, tx_data, 2U, APP_SPI_TIMEOUT_MS);
  RC522_Unselect();
}

uint8_t RC522_ReadRegister(uint8_t reg)
{
  uint8_t tx_data[2];
  uint8_t rx_data[2] = {0U, 0U};

  tx_data[0] = (uint8_t)(((reg << 1U) & 0x7EU) | 0x80U);
  tx_data[1] = 0x00U;

  RC522_Select();
  (void)HAL_SPI_TransmitReceive(rc522_spi, tx_data, rx_data, 2U, APP_SPI_TIMEOUT_MS);
  RC522_Unselect();

  return rx_data[1];
}

static void RC522_SetBitMask(uint8_t reg, uint8_t mask)
{
  RC522_WriteRegister(reg, (uint8_t)(RC522_ReadRegister(reg) | mask));
}

static void RC522_ClearBitMask(uint8_t reg, uint8_t mask)
{
  RC522_WriteRegister(reg, (uint8_t)(RC522_ReadRegister(reg) & (uint8_t)(~mask)));
}

static void RC522_AntennaOn(void)
{
  if ((RC522_ReadRegister(RC522_REG_TX_CONTROL) & 0x03U) != 0x03U)
  {
    RC522_SetBitMask(RC522_REG_TX_CONTROL, 0x03U);
  }
}

static RC522_StatusTypeDef RC522_CalculateCRC(const uint8_t *data, uint8_t length, uint8_t *crc_out)
{
  uint32_t timeout = HAL_GetTick() + 10U;
  uint8_t idx;

  RC522_WriteRegister(RC522_REG_COMMAND, RC522_CMD_IDLE);
  RC522_WriteRegister(RC522_REG_DIVIRQ, 0x04U);
  RC522_SetBitMask(RC522_REG_FIFO_LEVEL, 0x80U);

  for (idx = 0U; idx < length; idx++)
  {
    RC522_WriteRegister(RC522_REG_FIFO_DATA, data[idx]);
  }

  RC522_WriteRegister(RC522_REG_COMMAND, RC522_CMD_CALCCRC);
  while ((RC522_ReadRegister(RC522_REG_DIVIRQ) & 0x04U) == 0U)
  {
    if (HAL_GetTick() > timeout)
    {
      return RC522_ERROR;
    }
  }

  crc_out[0] = RC522_ReadRegister(RC522_REG_CRC_RESULT_LSB);
  crc_out[1] = RC522_ReadRegister(RC522_REG_CRC_RESULT_MSB);
  return RC522_OK;
}

static RC522_StatusTypeDef RC522_Transceive(const uint8_t *tx_data,
                                            uint8_t tx_len,
                                            uint8_t *rx_data,
                                            uint8_t *rx_len,
                                            uint8_t tx_last_bits,
                                            uint8_t *valid_bits_out)
{
  uint8_t irq;
  uint8_t fifo_level;
  uint8_t last_bits;
  uint8_t idx;
  uint32_t timeout = HAL_GetTick() + 30U;

  RC522_WriteRegister(RC522_REG_COMMAND, RC522_CMD_IDLE);
  RC522_WriteRegister(RC522_REG_COMMIEN, 0xF7U);
  RC522_ClearBitMask(RC522_REG_COMMIRQ, 0x80U);
  RC522_SetBitMask(RC522_REG_FIFO_LEVEL, 0x80U);
  RC522_WriteRegister(RC522_REG_BIT_FRAMING, tx_last_bits);

  for (idx = 0U; idx < tx_len; idx++)
  {
    RC522_WriteRegister(RC522_REG_FIFO_DATA, tx_data[idx]);
  }

  RC522_WriteRegister(RC522_REG_COMMAND, RC522_CMD_TRANSCEIVE);
  RC522_SetBitMask(RC522_REG_BIT_FRAMING, 0x80U);

  do
  {
    irq = RC522_ReadRegister(RC522_REG_COMMIRQ);
    if (HAL_GetTick() > timeout)
    {
      RC522_ClearBitMask(RC522_REG_BIT_FRAMING, 0x80U);
      return RC522_ERROR;
    }
  } while ((irq & 0x30U) == 0U && (irq & 0x01U) == 0U);

  RC522_ClearBitMask(RC522_REG_BIT_FRAMING, 0x80U);

  if ((RC522_ReadRegister(RC522_REG_ERROR) & 0x1BU) != 0U)
  {
    return RC522_ERROR;
  }

  if ((irq & 0x01U) != 0U)
  {
    return RC522_NO_TAG;
  }

  fifo_level = RC522_ReadRegister(RC522_REG_FIFO_LEVEL);
  last_bits = (uint8_t)(RC522_ReadRegister(RC522_REG_CONTROL) & 0x07U);

  if (valid_bits_out != NULL)
  {
    *valid_bits_out = last_bits;
  }

  if (fifo_level > *rx_len)
  {
    return RC522_ERROR;
  }

  *rx_len = fifo_level;
  for (idx = 0U; idx < fifo_level; idx++)
  {
    rx_data[idx] = RC522_ReadRegister(RC522_REG_FIFO_DATA);
  }

  return RC522_OK;
}

static RC522_StatusTypeDef RC522_RequestA(void)
{
  uint8_t cmd = PICC_CMD_REQA;
  uint8_t response[2];
  uint8_t response_len = sizeof(response);
  uint8_t valid_bits = 0U;
  RC522_StatusTypeDef status;

  status = RC522_Transceive(&cmd, 1U, response, &response_len, 0x07U, &valid_bits);
  if (status != RC522_OK)
  {
    return status;
  }

  return (response_len == 2U && valid_bits == 0U) ? RC522_OK : RC522_ERROR;
}

static RC522_StatusTypeDef RC522_AntiCollision(uint8_t uid[5])
{
  uint8_t cmd[2] = {PICC_CMD_ANTICOLL_CL1, 0x20U};
  uint8_t response[5];
  uint8_t response_len = sizeof(response);
  uint8_t bcc = 0U;
  uint8_t idx;
  RC522_StatusTypeDef status;

  status = RC522_Transceive(cmd, 2U, response, &response_len, 0x00U, NULL);
  if (status != RC522_OK || response_len != 5U)
  {
    return RC522_ERROR;
  }

  for (idx = 0U; idx < 4U; idx++)
  {
    uid[idx] = response[idx];
    bcc ^= response[idx];
  }
  uid[4] = response[4];

  return (bcc == response[4]) ? RC522_OK : RC522_CRC_ERROR;
}

static RC522_StatusTypeDef RC522_SelectCardInternal(const uint8_t uid[5], uint8_t *sak)
{
  uint8_t cmd[9] = {PICC_CMD_SELECT_CL1, 0x70U};
  uint8_t response[3];
  uint8_t response_len = sizeof(response);
  RC522_StatusTypeDef status;

  memcpy(&cmd[2], uid, 5U);
  status = RC522_CalculateCRC(cmd, 7U, &cmd[7]);
  if (status != RC522_OK)
  {
    return status;
  }

  status = RC522_Transceive(cmd, sizeof(cmd), response, &response_len, 0x00U, NULL);
  if (status != RC522_OK || response_len != 3U)
  {
    return RC522_ERROR;
  }

  if (sak != NULL)
  {
    *sak = response[0];
  }

  return RC522_OK;
}

static RC522_StatusTypeDef RC522_Authenticate(uint8_t block_addr,
                                              const uint8_t key[6],
                                              const uint8_t uid[4])
{
  uint8_t idx;
  uint8_t irq;
  uint32_t timeout = HAL_GetTick() + 30U;

  RC522_WriteRegister(RC522_REG_COMMAND, RC522_CMD_IDLE);
  RC522_WriteRegister(RC522_REG_COMMIEN, 0x92U);
  RC522_ClearBitMask(RC522_REG_COMMIRQ, 0x80U);
  RC522_SetBitMask(RC522_REG_FIFO_LEVEL, 0x80U);

  RC522_WriteRegister(RC522_REG_FIFO_DATA, PICC_CMD_MF_AUTH_KEY_A);
  RC522_WriteRegister(RC522_REG_FIFO_DATA, block_addr);
  for (idx = 0U; idx < 6U; idx++)
  {
    RC522_WriteRegister(RC522_REG_FIFO_DATA, key[idx]);
  }
  for (idx = 0U; idx < 4U; idx++)
  {
    RC522_WriteRegister(RC522_REG_FIFO_DATA, uid[idx]);
  }

  RC522_WriteRegister(RC522_REG_COMMAND, RC522_CMD_MFAUTHENT);
  do
  {
    irq = RC522_ReadRegister(RC522_REG_COMMIRQ);
    if (HAL_GetTick() > timeout)
    {
      return RC522_AUTH_ERROR;
    }
  } while ((irq & 0x10U) == 0U && (irq & 0x01U) == 0U);

  if ((RC522_ReadRegister(RC522_REG_ERROR) & 0x13U) != 0U)
  {
    return RC522_AUTH_ERROR;
  }

  return ((RC522_ReadRegister(RC522_REG_STATUS2) & 0x08U) != 0U) ? RC522_OK : RC522_AUTH_ERROR;
}

static void RC522_StopCrypto1(void)
{
  RC522_ClearBitMask(RC522_REG_STATUS2, 0x08U);
}

void RC522_Init(void)
{
  RC522_Unselect();
  RC522_HardReset();
  RC522_WriteRegister(RC522_REG_COMMAND, RC522_CMD_SOFTRESET);
  HAL_Delay(50U);

  RC522_WriteRegister(RC522_REG_T_MODE, 0x8DU);
  RC522_WriteRegister(RC522_REG_T_PRESCALER, 0x3EU);
  RC522_WriteRegister(RC522_REG_T_RELOAD_LSB, 30U);
  RC522_WriteRegister(RC522_REG_T_RELOAD_MSB, 0U);
  RC522_WriteRegister(RC522_REG_TX_AUTO, 0x40U);
  RC522_WriteRegister(RC522_REG_MODE, 0x3DU);
  RC522_WriteRegister(RC522_REG_TX_MODE, 0x00U);
  RC522_WriteRegister(RC522_REG_RX_MODE, 0x00U);
  RC522_AntennaOn();
}

RC522_StatusTypeDef RC522_IsCardPresent(void)
{
  return RC522_RequestA();
}

RC522_StatusTypeDef RC522_ReadCard(RC522_CardInfoTypeDef *card)
{
  uint8_t uid_raw[5];
  RC522_StatusTypeDef status;

  if (card == NULL)
  {
    return RC522_ERROR;
  }

  status = RC522_RequestA();
  if (status != RC522_OK)
  {
    return status;
  }

  status = RC522_AntiCollision(uid_raw);
  if (status != RC522_OK)
  {
    return status;
  }

  status = RC522_SelectCardInternal(uid_raw, &card->sak);
  if (status != RC522_OK)
  {
    return status;
  }

  memcpy(card->uid, uid_raw, 4U);
  card->uid_len = 4U;
  return RC522_OK;
}

RC522_StatusTypeDef RC522_MifareReadBlock(uint8_t block_addr,
                                          const uint8_t key[6],
                                          const uint8_t uid[4],
                                          uint8_t data[RC522_MIFARE_BLOCK_SIZE])
{
  uint8_t cmd[4] = {PICC_CMD_MF_READ, block_addr, 0U, 0U};
  uint8_t rx_buffer[18];
  uint8_t rx_len = 18U;
  RC522_StatusTypeDef status;

  if (data == NULL || key == NULL || uid == NULL)
  {
    return RC522_ERROR;
  }

  status = RC522_Authenticate(block_addr, key, uid);
  if (status != RC522_OK)
  {
    return status;
  }

  status = RC522_CalculateCRC(cmd, 2U, &cmd[2]);
  if (status == RC522_OK)
  {
    status = RC522_Transceive(cmd, sizeof(cmd), rx_buffer, &rx_len, 0x00U, NULL);
  }

  RC522_StopCrypto1();

  if (status == RC522_OK && rx_len == 18U)
  {
    memcpy(data, rx_buffer, RC522_MIFARE_BLOCK_SIZE);
    return RC522_OK;
  }

  return RC522_ERROR;
}

RC522_StatusTypeDef RC522_MifareWriteBlock(uint8_t block_addr,
                                           const uint8_t key[6],
                                           const uint8_t uid[4],
                                           const uint8_t data[RC522_MIFARE_BLOCK_SIZE])
{
  uint8_t ack[1];
  uint8_t ack_len = sizeof(ack);
  uint8_t write_cmd[4] = {PICC_CMD_MF_WRITE, block_addr, 0U, 0U};
  uint8_t frame[18];
  RC522_StatusTypeDef status;

  if (data == NULL || key == NULL || uid == NULL)
  {
    return RC522_ERROR;
  }

  status = RC522_Authenticate(block_addr, key, uid);
  if (status != RC522_OK)
  {
    return status;
  }

  status = RC522_CalculateCRC(write_cmd, 2U, &write_cmd[2]);
  if (status == RC522_OK)
  {
    status = RC522_Transceive(write_cmd, sizeof(write_cmd), ack, &ack_len, 0x00U, NULL);
  }

  if (status != RC522_OK || ack_len != 1U || (ack[0] & 0x0FU) != 0x0AU)
  {
    RC522_StopCrypto1();
    return RC522_ERROR;
  }

  memcpy(frame, data, RC522_MIFARE_BLOCK_SIZE);
  status = RC522_CalculateCRC(frame, RC522_MIFARE_BLOCK_SIZE, &frame[16]);
  if (status == RC522_OK)
  {
    ack_len = sizeof(ack);
    status = RC522_Transceive(frame, sizeof(frame), ack, &ack_len, 0x00U, NULL);
  }

  RC522_StopCrypto1();

  return (status == RC522_OK && ack_len == 1U && (ack[0] & 0x0FU) == 0x0AU) ? RC522_OK : RC522_ERROR;
}

void RC522_Halt(void)
{
  uint8_t cmd[4] = {PICC_CMD_HLTA, 0x00U, 0U, 0U};
  uint8_t rx_data[4];
  uint8_t rx_len = sizeof(rx_data);

  if (RC522_CalculateCRC(cmd, 2U, &cmd[2]) == RC522_OK)
  {
    (void)RC522_Transceive(cmd, sizeof(cmd), rx_data, &rx_len, 0x00U, NULL);
  }

  RC522_StopCrypto1();
}
