#include "storage_manager.h"

#include "stdio.h"
#include "string.h"

/* storage_manager 负责把“设备参数 / 用户档案 / 打卡记录”
 * 映射到 W25Q32 的固定地址空间，并处理扇区更新细节。 */

#define STORAGE_PARAM_SECTOR_ADDR          STORAGE_ADDR_SYS_PARAM_BASE
#define STORAGE_PARAM_SECTOR_SIZE          W25Q32_SECTOR_SIZE

static StorageParamTypeDef g_storage_param;
static uint8_t g_storage_sector_buffer[W25Q32_SECTOR_SIZE];

static uint32_t StorageManager_UserAddr(uint32_t index)
{
  return STORAGE_ADDR_USER_BASE + (index * sizeof(StorageUserTypeDef));
}

static uint32_t StorageManager_RecordAddr(uint32_t index)
{
  return STORAGE_ADDR_RECORD_BASE + (index * sizeof(StorageRecordTypeDef));
}

static uint8_t StorageManager_WriteBuffer(uint32_t address, const uint8_t *buffer, uint32_t length)
{
  uint16_t chunk;
  uint32_t offset = 0U;
  uint32_t page_offset;

  /* W25Q32 页编程不能跨页，这里按页拆分写入。 */
  while (offset < length)
  {
    page_offset = (address + offset) & (W25Q32_PAGE_SIZE - 1U);
    chunk = (uint16_t)(W25Q32_PAGE_SIZE - page_offset);
    if (chunk > (length - offset))
    {
      chunk = (uint16_t)(length - offset);
    }

    if (W25Q32_PageProgram(address + offset, &buffer[offset], chunk) != W25Q32_OK)
    {
      return 0U;
    }
    offset += chunk;
  }

  return 1U;
}

static uint8_t StorageManager_UpdateSector(uint32_t sector_addr,
                                           uint32_t offset_in_sector,
                                           const uint8_t *data,
                                           uint32_t length)
{
  if ((offset_in_sector + length) > W25Q32_SECTOR_SIZE)
  {
    return 0U;
  }

  /* 需要改动一个扇区中间的数据时，先整扇区读出、修改、擦除、再整扇区回写。 */
  if (W25Q32_ReadData(sector_addr, g_storage_sector_buffer, W25Q32_SECTOR_SIZE) != W25Q32_OK)
  {
    return 0U;
  }

  memcpy(&g_storage_sector_buffer[offset_in_sector], data, length);

  if (W25Q32_SectorErase(sector_addr) != W25Q32_OK)
  {
    return 0U;
  }

  return StorageManager_WriteBuffer(sector_addr, g_storage_sector_buffer, W25Q32_SECTOR_SIZE);
}

static uint8_t StorageManager_LoadParamFromFlash(StorageParamTypeDef *param)
{
  if (param == NULL)
  {
    return 0U;
  }

  return (W25Q32_ReadData(STORAGE_ADDR_SYS_PARAM_BASE, (uint8_t *)param, sizeof(StorageParamTypeDef)) == W25Q32_OK) ? 1U : 0U;
}

static void StorageManager_DefaultParam(StorageParamTypeDef *param)
{
  memset(param, 0, sizeof(*param));
  param->magic = STORAGE_PARAM_MAGIC;
  param->work_start_min = 9U * 60U;
  param->work_end_min = 18U * 60U;
  param->split_min = 15U * 60U;
  param->next_user_id = 1U;
  param->user_count = 0U;
  param->next_record_index = 0U;
}

static uint8_t StorageManager_ReadUserByIndex(uint32_t index, StorageUserTypeDef *user)
{
  if (index >= STORAGE_MAX_USER_COUNT || user == NULL)
  {
    return 0U;
  }

  return (W25Q32_ReadData(StorageManager_UserAddr(index), (uint8_t *)user, sizeof(StorageUserTypeDef)) == W25Q32_OK) ? 1U : 0U;
}

void StorageManager_Init(void)
{
  if (StorageManager_LoadParamFromFlash(&g_storage_param) == 0U ||
      g_storage_param.magic != STORAGE_PARAM_MAGIC)
  {
    /* 首次上电或参数损坏时自动恢复默认参数。 */
    StorageManager_DefaultParam(&g_storage_param);
    (void)StorageManager_SaveParam(&g_storage_param);
  }
}

StorageParamTypeDef StorageManager_GetParam(void)
{
  return g_storage_param;
}

uint8_t StorageManager_SaveParam(const StorageParamTypeDef *param)
{
  if (param == NULL)
  {
    return 0U;
  }

  if (StorageManager_UpdateSector(STORAGE_PARAM_SECTOR_ADDR,
                                  0U,
                                  (const uint8_t *)param,
                                  sizeof(StorageParamTypeDef)) == 0U)
  {
    return 0U;
  }

  g_storage_param = *param;
  return 1U;
}

uint8_t StorageManager_FindUserByCard(const uint8_t uid[4], StorageUserTypeDef *user_out)
{
  StorageUserTypeDef user;
  uint32_t idx;

  if (uid == NULL)
  {
    return 0U;
  }

  for (idx = 0U; idx < STORAGE_MAX_USER_COUNT; idx++)
  {
    if (StorageManager_ReadUserByIndex(idx, &user) == 0U)
    {
      continue;
    }

    if (user.valid == 1U && memcmp(user.rc522_uid, uid, 4U) == 0)
    {
      /* 卡 UID 是当前本地身份映射的第一入口。 */
      if (user_out != NULL)
      {
        *user_out = user;
      }
      return 1U;
    }
  }

  return 0U;
}

uint8_t StorageManager_FindUserByFinger(uint16_t finger_id, StorageUserTypeDef *user_out)
{
  StorageUserTypeDef user;
  uint32_t idx;

  for (idx = 0U; idx < STORAGE_MAX_USER_COUNT; idx++)
  {
    if (StorageManager_ReadUserByIndex(idx, &user) == 0U)
    {
      continue;
    }

    if (user.valid == 1U && user.finger_id == finger_id)
    {
      /* 指纹识别返回模板号后，通过 finger_id 映射回本地用户。 */
      if (user_out != NULL)
      {
        *user_out = user;
      }
      return 1U;
    }
  }

  return 0U;
}

uint8_t StorageManager_SaveUser(const StorageUserTypeDef *user)
{
  StorageUserTypeDef slot_user;
  uint32_t idx;
  uint32_t addr;
  uint32_t sector_addr;
  uint32_t offset_in_sector;

  if (user == NULL || user->user_id == 0U)
  {
    return 0U;
  }

  for (idx = 0U; idx < STORAGE_MAX_USER_COUNT; idx++)
  {
    if (StorageManager_ReadUserByIndex(idx, &slot_user) == 0U)
    {
      continue;
    }

    if (slot_user.valid == 1U && slot_user.user_id == user->user_id)
    {
      addr = StorageManager_UserAddr(idx);
      sector_addr = addr & ~(W25Q32_SECTOR_SIZE - 1UL);
      offset_in_sector = addr - sector_addr;
      return StorageManager_UpdateSector(sector_addr,
                                         offset_in_sector,
                                         (const uint8_t *)user,
                                         sizeof(StorageUserTypeDef));
    }
  }

  return 0U;
}

uint8_t StorageManager_CreateUser(const uint8_t uid[4], uint16_t finger_id, StorageUserTypeDef *user_out)
{
  StorageUserTypeDef user;
  uint32_t idx;
  uint32_t addr;
  uint32_t sector_addr;
  uint32_t offset_in_sector;

  for (idx = 0U; idx < STORAGE_MAX_USER_COUNT; idx++)
  {
    if (StorageManager_ReadUserByIndex(idx, &user) == 0U)
    {
      continue;
    }

    if (user.valid == 0xFFU || user.valid == 0U)
    {
      /* 找到空槽位后自动分配 user_id，并生成默认姓名/工号占位。 */
      memset(&user, 0, sizeof(user));
      user.user_id = g_storage_param.next_user_id;
      user.valid = 1U;
      user.finger_id = finger_id;
      memcpy(user.rc522_uid, uid, 4U);
      snprintf(user.employee_no, sizeof(user.employee_no), "%04lu", (unsigned long)user.user_id);
      snprintf(user.name, sizeof(user.name), "USER%04lu", (unsigned long)user.user_id);

      addr = StorageManager_UserAddr(idx);
      sector_addr = addr & ~(W25Q32_SECTOR_SIZE - 1UL);
      offset_in_sector = addr - sector_addr;

      if (StorageManager_UpdateSector(sector_addr,
                                      offset_in_sector,
                                      (const uint8_t *)&user,
                                      sizeof(StorageUserTypeDef)) == 0U)
      {
        return 0U;
      }

      g_storage_param.next_user_id++;
      g_storage_param.user_count++;
      (void)StorageManager_SaveParam(&g_storage_param);

      if (user_out != NULL)
      {
        *user_out = user;
      }
      return 1U;
    }
  }

  return 0U;
}

uint8_t StorageManager_AppendRecord(const StorageRecordTypeDef *record, uint32_t *record_index_out)
{
  StorageRecordTypeDef record_local;
  uint32_t record_index;
  uint32_t addr;
  uint32_t sector_addr;

  if (record == NULL)
  {
    return 0U;
  }

  record_local = *record;
  record_index = g_storage_param.next_record_index % STORAGE_MAX_RECORD_COUNT;
  record_local.record_id = g_storage_param.next_record_index + 1U;
  addr = StorageManager_RecordAddr(record_index);
  sector_addr = addr & ~(W25Q32_SECTOR_SIZE - 1UL);

  /* 记录区采用顺序追加；写到新扇区开头前先擦除该扇区。 */
  if ((addr % W25Q32_SECTOR_SIZE) == 0U)
  {
    if (W25Q32_SectorErase(sector_addr) != W25Q32_OK)
    {
      return 0U;
    }
  }

  if (StorageManager_WriteBuffer(addr, (const uint8_t *)&record_local, sizeof(StorageRecordTypeDef)) == 0U)
  {
    return 0U;
  }

  g_storage_param.next_record_index++;
  (void)StorageManager_SaveParam(&g_storage_param);

  if (record_index_out != NULL)
  {
    *record_index_out = record_index;
  }

  return 1U;
}
