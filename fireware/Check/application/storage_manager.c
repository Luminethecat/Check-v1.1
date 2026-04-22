#include "storage_manager.h"

#include "stdio.h"
#include "string.h"
#include "stm32f1xx_hal.h"

/* storage_manager 负责把“设备参数 / 用户档案 / 打卡记录”
 * 映射到内部Flash的固定地址空间，并处理页更新细节。 */

#define STORAGE_PARAM_SECTOR_ADDR          STORAGE_ADDR_SYS_PARAM_BASE
#define STORAGE_PARAM_SECTOR_SIZE          1024U  // Flash页大小

static StorageParamTypeDef g_storage_param;
static StorageUserTypeDef g_storage_users[STORAGE_MAX_USER_COUNT];
static uint8_t g_storage_sector_buffer[1024U];  /* Flash页缓冲区 */
static uint32_t g_storage_record_index = 0U;
static uint8_t g_storage_flash_write_enabled = 0U;  /* 测试阶段默认关闭写Flash */

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
  HAL_StatusTypeDef status;
  uint32_t flash_addr = address;
  uint32_t data;
  uint32_t i;

  if (length % 4 != 0)
  {
    return 0U; // 必须4字节对齐
  }

  HAL_FLASH_Unlock();

  for (i = 0; i < length; i += 4)
  {
    data = *(uint32_t *)&buffer[i];
    status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, flash_addr, data);
    if (status != HAL_OK)
    {
      HAL_FLASH_Lock();
      return 0U;
    }
    flash_addr += 4;
  }

  HAL_FLASH_Lock();
  return 1U;
}

static uint8_t StorageManager_UpdatePage(uint32_t page_addr, uint32_t offset_in_page, const uint8_t *data, uint32_t length)
{
  FLASH_EraseInitTypeDef erase_init;
  uint32_t page_error;

  if ((offset_in_page + length) > 1024U)
  {
    return 0U;
  }

  // 读出整页
  memcpy(g_storage_sector_buffer, (void *)page_addr, 1024U);

  // 修改数据
  memcpy(&g_storage_sector_buffer[offset_in_page], data, length);

  // 擦除页
  erase_init.TypeErase = FLASH_TYPEERASE_PAGES;
  erase_init.PageAddress = page_addr;
  erase_init.NbPages = 1;

  HAL_FLASH_Unlock();
  if (HAL_FLASHEx_Erase(&erase_init, &page_error) != HAL_OK)
  {
    HAL_FLASH_Lock();
    return 0U;
  }

  // 写回整页
  if (StorageManager_WriteBuffer(page_addr, g_storage_sector_buffer, 1024U) == 0U)
  {
    HAL_FLASH_Lock();
    return 0U;
  }

  HAL_FLASH_Lock();
  return 1U;
}

static uint8_t StorageManager_LoadParamFromFlash(StorageParamTypeDef *param)
{
  if (param == NULL)
  {
    return 0U;
  }

  memcpy(param, (void *)STORAGE_ADDR_SYS_PARAM_BASE, sizeof(StorageParamTypeDef));
  return 1U;
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

  if (g_storage_users[index].valid != 1U)
  {
    return 0U;
  }

  *user = g_storage_users[index];
  return 1U;
}

void StorageManager_Init(void)
{
  if (StorageManager_LoadParamFromFlash(&g_storage_param) == 0U ||
      g_storage_param.magic != STORAGE_PARAM_MAGIC)
  {
    /* 首次上电或参数损坏时自动恢复默认参数。
     * 目前不写入Flash，避免测试阶段擦坏内部Flash。 */
    StorageManager_DefaultParam(&g_storage_param);
  }

  for (uint32_t i = 0U; i < STORAGE_MAX_USER_COUNT; i++)
  {
    g_storage_users[i].valid = 0U;
  }
  g_storage_record_index = g_storage_param.next_record_index;
}

void StorageManager_SetFlashWriteEnabled(uint8_t enabled)
{
  g_storage_flash_write_enabled = enabled ? 1U : 0U;
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

  if (g_storage_flash_write_enabled)
  {
    if (StorageManager_UpdatePage(STORAGE_PARAM_SECTOR_ADDR,
                                  0U,
                                  (const uint8_t *)param,
                                  sizeof(StorageParamTypeDef)) == 0U)
    {
      return 0U;
    }
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
  uint32_t page_addr;
  uint32_t offset_in_page;

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
      g_storage_users[idx] = *user;

      if (g_storage_flash_write_enabled)
      {
        addr = StorageManager_UserAddr(idx);
        page_addr = addr & ~(1024U - 1UL);
        offset_in_page = addr - page_addr;
        return StorageManager_UpdatePage(page_addr,
                                         offset_in_page,
                                         (const uint8_t *)user,
                                         sizeof(StorageUserTypeDef));
      }

      return 1U;
    }
  }

  return 0U;
}

uint8_t StorageManager_CreateUser(const uint8_t uid[4], uint16_t finger_id, StorageUserTypeDef *user_out)
{
  StorageUserTypeDef user;
  uint32_t idx;
  uint32_t addr;
  uint32_t page_addr;
  uint32_t offset_in_page;
  for (idx = 0U; idx < STORAGE_MAX_USER_COUNT; idx++)
  {
    /* 直接检查内部数组中的 valid 字段，避免通过 ReadUserByIndex 跳过空槽 */
    if (g_storage_users[idx].valid == 1U)
    {
      continue; /* 已占用 */
    }

    /* 找到空槽位后自动分配 user_id，并生成默认姓名/工号占位。 */
    memset(&user, 0, sizeof(user));
    user.user_id = g_storage_param.next_user_id;
    user.valid = 1U;
    user.finger_id = finger_id;
    memcpy(user.rc522_uid, uid, 4U);
    snprintf(user.employee_no, sizeof(user.employee_no), "%04lu", (unsigned long)user.user_id);
    snprintf(user.name, sizeof(user.name), "USER%04lu", (unsigned long)user.user_id);

    g_storage_users[idx] = user;

    if (g_storage_flash_write_enabled)
    {
      addr = StorageManager_UserAddr(idx);
      page_addr = addr & ~(1024U - 1UL);
      offset_in_page = addr - page_addr;
      if (StorageManager_UpdatePage(page_addr,
                                    offset_in_page,
                                    (const uint8_t *)&user,
                                    sizeof(StorageUserTypeDef)) == 0U)
      {
        return 0U;
      }
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

  return 0U;
}

uint8_t StorageManager_AppendRecord(const StorageRecordTypeDef *record, uint32_t *record_index_out)
{
  StorageRecordTypeDef record_local;
  uint32_t record_index;
  uint32_t addr;
  uint32_t page_addr;

  if (record == NULL)
  {
    return 0U;
  }

  record_local = *record;
  record_index = g_storage_param.next_record_index % STORAGE_MAX_RECORD_COUNT;
  record_local.record_id = g_storage_param.next_record_index + 1U;
  addr = StorageManager_RecordAddr(record_index);
  page_addr = addr & ~(1024U - 1UL);

  if (g_storage_flash_write_enabled)
  {
    /* 记录区采用顺序追加；写到新页开头前先擦除该页。 */
    if ((addr % 1024U) == 0U)
    {
      FLASH_EraseInitTypeDef erase_init;
      uint32_t page_error;

      erase_init.TypeErase = FLASH_TYPEERASE_PAGES;
      erase_init.PageAddress = page_addr;
      erase_init.NbPages = 1;

      HAL_FLASH_Unlock();
      if (HAL_FLASHEx_Erase(&erase_init, &page_error) != HAL_OK)
      {
        HAL_FLASH_Lock();
        return 0U;
      }
      HAL_FLASH_Lock();
    }

    if (StorageManager_WriteBuffer(addr, (const uint8_t *)&record_local, sizeof(StorageRecordTypeDef)) == 0U)
    {
      return 0U;
    }
  }

  g_storage_param.next_record_index++;
  (void)StorageManager_SaveParam(&g_storage_param);

  if (record_index_out != NULL)
  {
    *record_index_out = record_index;
  }

  return 1U;
}

uint8_t StorageManager_LoadUserData(void *buffer, uint32_t buffer_size)
{
  if (buffer == NULL || buffer_size < sizeof(g_storage_users))
  {
    return 0U;
  }

  memcpy(buffer, g_storage_users, sizeof(g_storage_users));
  return 1U;
}

uint8_t StorageManager_SaveUserData(void *buffer, uint32_t buffer_size)
{
  if (buffer == NULL || buffer_size < sizeof(g_storage_users))
  {
    return 0U;
  }

  memcpy(g_storage_users, buffer, sizeof(g_storage_users));

  // 保存到Flash
  if (g_storage_flash_write_enabled)
  {
    uint32_t i;
    for (i = 0; i < STORAGE_MAX_USER_COUNT; i++)
    {
      uint32_t addr = StorageManager_UserAddr(i);
      if (g_storage_users[i].valid)
      {
        if (StorageManager_WriteBuffer(addr, (const uint8_t *)&g_storage_users[i], sizeof(StorageUserTypeDef)) == 0U)
        {
          return 0U;
        }
      }
    }
  }

  return 1U;
}

uint32_t StorageManager_GetUserCount(void)
{
  return (uint32_t)g_storage_param.user_count;
}

uint8_t StorageManager_GetUserByIndex(uint32_t index, StorageUserTypeDef *user_out)
{
  if (index >= STORAGE_MAX_USER_COUNT || user_out == NULL)
  {
    return 0U;
  }

  if (g_storage_users[index].valid != 1U)
  {
    return 0U;
  }

  *user_out = g_storage_users[index];
  return 1U;
}
