#include "w25q32_app.h"

#include "app_board.h"

#define W25Q32_CMD_JEDEC_ID            0x9FU
#define W25Q32_CMD_READ_DATA           0x03U
#define W25Q32_CMD_PAGE_PROGRAM        0x02U
#define W25Q32_CMD_SECTOR_ERASE        0x20U
#define W25Q32_CMD_WRITE_ENABLE        0x06U
#define W25Q32_CMD_READ_STATUS1        0x05U

#define W25Q32_STATUS_BUSY             0x01U
#define W25Q32_STATUS_WEL              0x02U

static SPI_HandleTypeDef *const w25q32_spi = &APP_W25Q32_SPI_HANDLE;

static void W25Q32_Select(void)
{
  HAL_GPIO_WritePin(APP_W25Q32_CS_GPIO_Port, APP_W25Q32_CS_Pin, GPIO_PIN_RESET);
}

static void W25Q32_Unselect(void)
{
  HAL_GPIO_WritePin(APP_W25Q32_CS_GPIO_Port, APP_W25Q32_CS_Pin, GPIO_PIN_SET);
}

static W25Q32_StatusTypeDef W25Q32_Transmit(const uint8_t *buffer, uint16_t length)
{
  if (HAL_SPI_Transmit(w25q32_spi, (uint8_t *)buffer, length, APP_SPI_TIMEOUT_MS) != HAL_OK)
  {
    return W25Q32_ERROR;
  }
  return W25Q32_OK;
}

static W25Q32_StatusTypeDef W25Q32_Receive(uint8_t *buffer, uint16_t length)
{
  if (HAL_SPI_Receive(w25q32_spi, buffer, length, APP_SPI_TIMEOUT_MS) != HAL_OK)
  {
    return W25Q32_ERROR;
  }
  return W25Q32_OK;
}

static W25Q32_StatusTypeDef W25Q32_ReadStatus1(uint8_t *status)
{
  uint8_t cmd = W25Q32_CMD_READ_STATUS1;

  if (status == NULL)
  {
    return W25Q32_ERROR;
  }

  W25Q32_Select();
  if (W25Q32_Transmit(&cmd, 1U) != W25Q32_OK || W25Q32_Receive(status, 1U) != W25Q32_OK)
  {
    W25Q32_Unselect();
    return W25Q32_ERROR;
  }
  W25Q32_Unselect();

  return W25Q32_OK;
}

static W25Q32_StatusTypeDef W25Q32_WriteEnable(void)
{
  uint8_t cmd = W25Q32_CMD_WRITE_ENABLE;
  uint8_t status = 0U;

  W25Q32_Select();
  if (W25Q32_Transmit(&cmd, 1U) != W25Q32_OK)
  {
    W25Q32_Unselect();
    return W25Q32_ERROR;
  }
  W25Q32_Unselect();

  if (W25Q32_ReadStatus1(&status) != W25Q32_OK)
  {
    return W25Q32_ERROR;
  }

  return ((status & W25Q32_STATUS_WEL) != 0U) ? W25Q32_OK : W25Q32_ERROR;
}

void W25Q32_Init(void)
{
  W25Q32_Unselect();
}

W25Q32_StatusTypeDef W25Q32_ReadJedecId(uint8_t jedec_id[3])
{
  uint8_t cmd = W25Q32_CMD_JEDEC_ID;

  if (jedec_id == NULL)
  {
    return W25Q32_ERROR;
  }

  W25Q32_Select();
  if (W25Q32_Transmit(&cmd, 1U) != W25Q32_OK || W25Q32_Receive(jedec_id, 3U) != W25Q32_OK)
  {
    W25Q32_Unselect();
    return W25Q32_ERROR;
  }
  W25Q32_Unselect();

  return W25Q32_OK;
}

W25Q32_StatusTypeDef W25Q32_ReadData(uint32_t address, uint8_t *buffer, uint32_t length)
{
  uint8_t header[4];
  uint32_t remaining;
  uint16_t chunk;

  if (buffer == NULL || length == 0U)
  {
    return W25Q32_ERROR;
  }

  remaining = length;
  while (remaining > 0U)
  {
    chunk = (remaining > 65535U) ? 65535U : (uint16_t)remaining;

    header[0] = W25Q32_CMD_READ_DATA;
    header[1] = (uint8_t)(address >> 16U);
    header[2] = (uint8_t)(address >> 8U);
    header[3] = (uint8_t)(address);

    W25Q32_Select();
    if (W25Q32_Transmit(header, sizeof(header)) != W25Q32_OK ||
        HAL_SPI_Receive(w25q32_spi, buffer, chunk, APP_SPI_TIMEOUT_MS) != HAL_OK)
    {
      W25Q32_Unselect();
      return W25Q32_ERROR;
    }
    W25Q32_Unselect();

    address += chunk;
    buffer += chunk;
    remaining -= chunk;
  }

  return W25Q32_OK;
}

W25Q32_StatusTypeDef W25Q32_PageProgram(uint32_t address, const uint8_t *buffer, uint16_t length)
{
  uint8_t header[4];

  if (buffer == NULL || length == 0U || length > W25Q32_PAGE_SIZE)
  {
    return W25Q32_ERROR;
  }

  if (((address & (W25Q32_PAGE_SIZE - 1U)) + length) > W25Q32_PAGE_SIZE)
  {
    return W25Q32_ERROR;
  }

  if (W25Q32_WriteEnable() != W25Q32_OK)
  {
    return W25Q32_ERROR;
  }

  header[0] = W25Q32_CMD_PAGE_PROGRAM;
  header[1] = (uint8_t)(address >> 16U);
  header[2] = (uint8_t)(address >> 8U);
  header[3] = (uint8_t)(address);

  W25Q32_Select();
  if (W25Q32_Transmit(header, sizeof(header)) != W25Q32_OK ||
      HAL_SPI_Transmit(w25q32_spi, (uint8_t *)buffer, length, APP_SPI_TIMEOUT_MS) != HAL_OK)
  {
    W25Q32_Unselect();
    return W25Q32_ERROR;
  }
  W25Q32_Unselect();

  return W25Q32_WaitWhileBusy(100U);
}

W25Q32_StatusTypeDef W25Q32_SectorErase(uint32_t address)
{
  uint8_t header[4];

  if (W25Q32_WriteEnable() != W25Q32_OK)
  {
    return W25Q32_ERROR;
  }

  header[0] = W25Q32_CMD_SECTOR_ERASE;
  header[1] = (uint8_t)(address >> 16U);
  header[2] = (uint8_t)(address >> 8U);
  header[3] = (uint8_t)(address);

  W25Q32_Select();
  if (W25Q32_Transmit(header, sizeof(header)) != W25Q32_OK)
  {
    W25Q32_Unselect();
    return W25Q32_ERROR;
  }
  W25Q32_Unselect();

  return W25Q32_WaitWhileBusy(3000U);
}

W25Q32_StatusTypeDef W25Q32_WaitWhileBusy(uint32_t timeout_ms)
{
  uint32_t tick_start = HAL_GetTick();
  uint8_t status = 0U;

  while ((HAL_GetTick() - tick_start) < timeout_ms)
  {
    if (W25Q32_ReadStatus1(&status) != W25Q32_OK)
    {
      return W25Q32_ERROR;
    }

    if ((status & W25Q32_STATUS_BUSY) == 0U)
    {
      return W25Q32_OK;
    }
  }

  return W25Q32_TIMEOUT;
}
