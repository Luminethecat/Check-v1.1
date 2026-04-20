#include "zw101_app.h"

#include "app_board.h"

#define ZW101_START_CODE_H             0xEFU
#define ZW101_START_CODE_L             0x01U

#define ZW101_PACKET_COMMAND           0x01U
#define ZW101_PACKET_ACK               0x07U

#define ZW101_CMD_GEN_IMAGE            0x01U
#define ZW101_CMD_IMAGE2TZ             0x02U
#define ZW101_CMD_SEARCH               0x04U
#define ZW101_CMD_REG_MODEL            0x05U
#define ZW101_CMD_STORE                0x06U
#define ZW101_CMD_DELETE               0x0CU
#define ZW101_CMD_EMPTY                0x0DU
#define ZW101_CMD_VERIFY_PASSWORD      0x13U

#define ZW101_CONFIRM_OK               0x00U
#define ZW101_CONFIRM_NO_FINGER        0x02U
#define ZW101_CONFIRM_NOT_FOUND        0x09U

static UART_HandleTypeDef *const zw101_uart = &APP_ZW101_UART_HANDLE;
static volatile uint8_t g_zw101_irq_pending;

static ZW101_StatusTypeDef ZW101_SendPacket(uint8_t packet_type,
                                            const uint8_t *payload,
                                            uint16_t payload_len)
{
  uint8_t tx_frame[32];
  uint16_t checksum;
  uint16_t frame_len;
  uint16_t idx;

  if (payload_len > 22U)
  {
    return ZW101_ERROR;
  }

  tx_frame[0] = ZW101_START_CODE_H;
  tx_frame[1] = ZW101_START_CODE_L;
  tx_frame[2] = 0xFFU;
  tx_frame[3] = 0xFFU;
  tx_frame[4] = 0xFFU;
  tx_frame[5] = 0xFFU;
  tx_frame[6] = packet_type;
  tx_frame[7] = (uint8_t)((payload_len + 2U) >> 8U);
  tx_frame[8] = (uint8_t)(payload_len + 2U);

  for (idx = 0U; idx < payload_len; idx++)
  {
    tx_frame[9U + idx] = payload[idx];
  }

  checksum = (uint16_t)(packet_type + tx_frame[7] + tx_frame[8]);
  for (idx = 0U; idx < payload_len; idx++)
  {
    checksum = (uint16_t)(checksum + payload[idx]);
  }

  frame_len = (uint16_t)(9U + payload_len);
  tx_frame[frame_len] = (uint8_t)(checksum >> 8U);
  tx_frame[frame_len + 1U] = (uint8_t)(checksum);

  if (HAL_UART_Transmit(zw101_uart, tx_frame, (uint16_t)(frame_len + 2U), APP_UART_TIMEOUT_MS) != HAL_OK)
  {
    return ZW101_TIMEOUT;
  }

  return ZW101_OK;
}

static ZW101_StatusTypeDef ZW101_ReceiveAck(uint8_t *payload,
                                           uint16_t *payload_len,
                                           uint32_t timeout_ms)
{
  uint8_t header[9];
  uint8_t byte;
  uint16_t i;
  uint16_t frame_payload_len;
  uint16_t checksum;
  uint8_t checksum_bytes[2];

  if (payload == NULL || payload_len == NULL)
  {
    return ZW101_ERROR;
  }

  /* ============================= */
  /* 1. 找帧头 EF 01 */
  /* ============================= */
  while (1)
  {
    if (HAL_UART_Receive(zw101_uart, &byte, 1, timeout_ms) != HAL_OK)
    {
      return ZW101_TIMEOUT;
    }

    if (byte == ZW101_START_CODE_H)
    {
      header[0] = byte;

      if (HAL_UART_Receive(zw101_uart, &byte, 1, timeout_ms) != HAL_OK)
      {
        return ZW101_TIMEOUT;
      }

      if (byte == ZW101_START_CODE_L)
      {
        header[1] = byte;
        break;
      }
    }
  }

  /* ============================= */
  /* 2. 收剩余7字节 header */
  /* ============================= */
  for (i = 2; i < 9; i++)
  {
    if (HAL_UART_Receive(zw101_uart, &header[i], 1, timeout_ms) != HAL_OK)
    {
      return ZW101_TIMEOUT;
    }
  }

  /* ============================= */
  /* 3. 校验 packet type */
  /* ============================= */
  if (header[6] != ZW101_PACKET_ACK)
  {
    return ZW101_PACKET_ERROR;
  }

  /* ============================= */
  /* 4. 计算 payload length */
  /* ============================= */
  frame_payload_len = (uint16_t)((header[7] << 8) | header[8]);

  if (frame_payload_len < 2U)
  {
    return ZW101_PACKET_ERROR;
  }

  *payload_len = frame_payload_len - 2U;

  if (*payload_len > 16U)
  {
    return ZW101_PACKET_ERROR;
  }

  /* ============================= */
  /* 5. 收 payload */
  /* ============================= */
  for (i = 0; i < *payload_len; i++)
  {
    if (HAL_UART_Receive(zw101_uart, &payload[i], 1, timeout_ms) != HAL_OK)
    {
      return ZW101_TIMEOUT;
    }
  }

  /* ============================= */
  /* 6. 收 checksum */
  /* ============================= */
  if (HAL_UART_Receive(zw101_uart, checksum_bytes, 2, timeout_ms) != HAL_OK)
  {
    return ZW101_TIMEOUT;
  }

  /* ============================= */
  /* 7. 校验 checksum */
  /* ============================= */
  checksum = ZW101_PACKET_ACK + header[7] + header[8];

  for (i = 0; i < *payload_len; i++)
  {
    checksum += payload[i];
  }

  if (checksum != ((checksum_bytes[0] << 8) | checksum_bytes[1]))
  {
    return ZW101_PACKET_ERROR;
  }

  return ZW101_OK;
}

static ZW101_StatusTypeDef ZW101_RunCommand(const uint8_t *cmd_payload,
                                            uint16_t cmd_len,
                                            uint8_t *ack_payload,
                                            uint16_t *ack_len,
                                            uint32_t timeout_ms)
{
  ZW101_StatusTypeDef status;

  status = ZW101_SendPacket(ZW101_PACKET_COMMAND, cmd_payload, cmd_len);
  if (status != ZW101_OK)
  {
    return status;
  }

  status = ZW101_ReceiveAck(ack_payload, ack_len, timeout_ms);
  if (status != ZW101_OK)
  {
    return status;
  }

  switch (ack_payload[0])
  {
    case ZW101_CONFIRM_OK:
      return ZW101_OK;

    case ZW101_CONFIRM_NO_FINGER:
      return ZW101_NO_FINGER;

    case ZW101_CONFIRM_NOT_FOUND:
      return ZW101_NOT_FOUND;

    default:
      return ZW101_ERROR;
  }
}

void ZW101_Init(void)
{
  g_zw101_irq_pending = 0U;
}

void ZW101_IrqNotify(void)
{
  g_zw101_irq_pending = 1U;
}

uint8_t ZW101_IrqConsumePending(void)
{
  uint8_t pending = g_zw101_irq_pending;

  g_zw101_irq_pending = 0U;
  return pending;
}

uint8_t ZW101_IrqIsActiveLevel(void)
{
  /* 当前模块 IRQ 有效电平为高，任务层可据此做一次软件确认。 */
  return (HAL_GPIO_ReadPin(APP_ZW101_IRQ_GPIO_Port, APP_ZW101_IRQ_Pin) == GPIO_PIN_SET) ? 1U : 0U;
}

ZW101_StatusTypeDef ZW101_VerifyPassword(uint32_t password)
{
  uint8_t cmd[5];
  uint8_t ack[16];
  uint16_t ack_len = sizeof(ack);

  cmd[0] = ZW101_CMD_VERIFY_PASSWORD;
  cmd[1] = (uint8_t)(password >> 24U);
  cmd[2] = (uint8_t)(password >> 16U);
  cmd[3] = (uint8_t)(password >> 8U);
  cmd[4] = (uint8_t)(password);

  return ZW101_RunCommand(cmd, sizeof(cmd), ack, &ack_len, 300U);
}

ZW101_StatusTypeDef ZW101_CollectImage(void)
{
  uint8_t cmd = ZW101_CMD_GEN_IMAGE;
  uint8_t ack[8];
  uint16_t ack_len = sizeof(ack);

  return ZW101_RunCommand(&cmd, 1U, ack, &ack_len, 1000U);
}

ZW101_StatusTypeDef ZW101_GenerateChar(uint8_t buffer_id)
{
  uint8_t cmd[2] = {ZW101_CMD_IMAGE2TZ, buffer_id};
  uint8_t ack[8];
  uint16_t ack_len = sizeof(ack);

  return ZW101_RunCommand(cmd, sizeof(cmd), ack, &ack_len, 1000U);
}

ZW101_StatusTypeDef ZW101_CreateModel(void)
{
  uint8_t cmd = ZW101_CMD_REG_MODEL;
  uint8_t ack[8];
  uint16_t ack_len = sizeof(ack);

  return ZW101_RunCommand(&cmd, 1U, ack, &ack_len, 1000U);
}

ZW101_StatusTypeDef ZW101_StoreModel(uint8_t buffer_id, uint16_t page_id)
{
  uint8_t cmd[4];
  uint8_t ack[8];
  uint16_t ack_len = sizeof(ack);

  cmd[0] = ZW101_CMD_STORE;
  cmd[1] = buffer_id;
  cmd[2] = (uint8_t)(page_id >> 8U);
  cmd[3] = (uint8_t)(page_id);

  return ZW101_RunCommand(cmd, sizeof(cmd), ack, &ack_len, 1000U);
}

ZW101_StatusTypeDef ZW101_DeleteModel(uint16_t page_id, uint16_t count)
{
  uint8_t cmd[5];
  uint8_t ack[8];
  uint16_t ack_len = sizeof(ack);

  cmd[0] = ZW101_CMD_DELETE;
  cmd[1] = (uint8_t)(page_id >> 8U);
  cmd[2] = (uint8_t)(page_id);
  cmd[3] = (uint8_t)(count >> 8U);
  cmd[4] = (uint8_t)(count);

  return ZW101_RunCommand(cmd, sizeof(cmd), ack, &ack_len, 1000U);
}

ZW101_StatusTypeDef ZW101_EmptyLibrary(void)
{
  uint8_t cmd = ZW101_CMD_EMPTY;
  uint8_t ack[8];
  uint16_t ack_len = sizeof(ack);

  return ZW101_RunCommand(&cmd, 1U, ack, &ack_len, 3000U);
}

ZW101_StatusTypeDef ZW101_Search(uint8_t buffer_id,
                                 uint16_t start_page,
                                 uint16_t page_count,
                                 ZW101_SearchResultTypeDef *result)
{
  uint8_t cmd[6];
  uint8_t ack[16];
  uint16_t ack_len = sizeof(ack);
  ZW101_StatusTypeDef status;

  if (result == NULL)
  {
    return ZW101_ERROR;
  }

  cmd[0] = ZW101_CMD_SEARCH;
  cmd[1] = buffer_id;
  cmd[2] = (uint8_t)(start_page >> 8U);
  cmd[3] = (uint8_t)(start_page);
  cmd[4] = (uint8_t)(page_count >> 8U);
  cmd[5] = (uint8_t)(page_count);

  status = ZW101_RunCommand(cmd, sizeof(cmd), ack, &ack_len, 1000U);
  if (status != ZW101_OK)
  {
    return status;
  }

  if (ack_len < 5U)
  {
    return ZW101_PACKET_ERROR;
  }

  result->page_id = (uint16_t)(((uint16_t)ack[1] << 8U) | ack[2]);
  result->match_score = (uint16_t)(((uint16_t)ack[3] << 8U) | ack[4]);
  return ZW101_OK;
}

ZW101_StatusTypeDef ZW101_Enroll(uint16_t page_id)
{
  ZW101_StatusTypeDef status;

  status = ZW101_CollectImage();
  if (status != ZW101_OK)
  {
    return status;
  }

  status = ZW101_GenerateChar(ZW101_TEMPLATE_BUFFER_1);
  if (status != ZW101_OK)
  {
    return status;
  }

  HAL_Delay(800U);

  status = ZW101_CollectImage();
  if (status != ZW101_OK)
  {
    return status;
  }

  status = ZW101_GenerateChar(ZW101_TEMPLATE_BUFFER_2);
  if (status != ZW101_OK)
  {
    return status;
  }

  status = ZW101_CreateModel();
  if (status != ZW101_OK)
  {
    return status;
  }

  return ZW101_StoreModel(ZW101_TEMPLATE_BUFFER_1, page_id);
}

ZW101_StatusTypeDef ZW101_Identify(ZW101_SearchResultTypeDef *result)
{
  ZW101_StatusTypeDef status;

  status = ZW101_CollectImage();
  if (status != ZW101_OK)
  {
    return status;
  }

  status = ZW101_GenerateChar(ZW101_TEMPLATE_BUFFER_1);
  if (status != ZW101_OK)
  {
    return status;
  }

  return ZW101_Search(ZW101_TEMPLATE_BUFFER_1, 0U, 300U, result);
}
