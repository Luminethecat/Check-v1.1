#include "storage_manager.h"

#include "Com_debug.h"
#include "stdio.h"
#include "string.h"
#include "stm32f1xx_hal.h"

#define STORAGE_PAGE_SIZE                  2048U
#define STORAGE_PARAM_SECTOR_ADDR          STORAGE_ADDR_SYS_PARAM_BASE

static StorageParamTypeDef g_storage_param;
static StorageUserTypeDef g_storage_users[STORAGE_MAX_USER_COUNT];
static uint8_t g_storage_page_buffer[STORAGE_PAGE_SIZE];
static uint8_t g_storage_flash_write_enabled = 1U;

static uint32_t StorageManager_UserAddr(uint32_t index)
{
  return STORAGE_ADDR_USER_BASE + (index * sizeof(StorageUserTypeDef));
}

static uint32_t StorageManager_RecordAddr(uint32_t index)
{
  return STORAGE_ADDR_RECORD_BASE + (index * sizeof(StorageRecordTypeDef));
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

static uint8_t StorageManager_LoadParamFromFlash(StorageParamTypeDef *param)
{
  if (param == NULL)
  {
    return 0U;
  }

  memcpy(param, (const void *)STORAGE_ADDR_SYS_PARAM_BASE, sizeof(StorageParamTypeDef));
  return (param->magic == STORAGE_PARAM_MAGIC) ? 1U : 0U;
}

static uint8_t StorageManager_ProgramWords(uint32_t address, const uint8_t *buffer, uint32_t length)
{
  uint32_t flash_addr = address;
  uint32_t i;
  HAL_StatusTypeDef status;
  uint32_t data_word;

  if ((buffer == NULL) || (length == 0U) || ((length % 4U) != 0U))
  {
    return 0U;
  }

  for (i = 0U; i < length; i += 4U)
  {
    memcpy(&data_word, &buffer[i], sizeof(data_word));
    status = HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, flash_addr, data_word);
    if (status != HAL_OK)
    {
      return 0U;
    }
    flash_addr += 4U;
  }

  return 1U;
}

static uint8_t StorageManager_UpdatePage(uint32_t page_addr,
                                         uint32_t offset_in_page,
                                         const uint8_t *data,
                                         uint32_t length)
{
  FLASH_EraseInitTypeDef erase_init;
  uint32_t page_error = 0U;

  if ((data == NULL) || ((offset_in_page + length) > STORAGE_PAGE_SIZE))
  {
    return 0U;
  }

  memcpy(g_storage_page_buffer, (const void *)page_addr, STORAGE_PAGE_SIZE);
  memcpy(&g_storage_page_buffer[offset_in_page], data, length);

  HAL_FLASH_Unlock();

  erase_init.TypeErase = FLASH_TYPEERASE_PAGES;
  erase_init.PageAddress = page_addr;
  erase_init.NbPages = 1U;

  if (HAL_FLASHEx_Erase(&erase_init, &page_error) != HAL_OK)
  {
    HAL_FLASH_Lock();
    return 0U;
  }

  if (StorageManager_ProgramWords(page_addr, g_storage_page_buffer, STORAGE_PAGE_SIZE) == 0U)
  {
    HAL_FLASH_Lock();
    return 0U;
  }

  HAL_FLASH_Lock();
  return 1U;
}

static uint8_t StorageManager_ReadUserByIndex(uint32_t index, StorageUserTypeDef *user)
{
  if ((index >= STORAGE_MAX_USER_COUNT) || (user == NULL))
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

uint16_t StorageManager_GetNextFreeUserId(void)
{
  uint16_t candidate;
  uint32_t idx;

  for (candidate = 1U; candidate < 0xFFF0U; candidate++)
  {
    uint8_t used = 0U;

    for (idx = 0U; idx < STORAGE_MAX_USER_COUNT; idx++)
    {
      if ((g_storage_users[idx].valid == 1U) && (g_storage_users[idx].user_id == candidate))
      {
        used = 1U;
        break;
      }
    }

    if (used == 0U)
    {
      return candidate;
    }
  }

  return 0U;
}

void StorageManager_Init(void)
{
  uint32_t i;
  uint8_t param_ok;
  uint8_t need_save_default = 0U;

  memset(&g_storage_param, 0, sizeof(g_storage_param));
  memset(g_storage_users, 0, sizeof(g_storage_users));

  param_ok = StorageManager_LoadParamFromFlash(&g_storage_param);
  if ((param_ok == 0U) || (g_storage_param.magic != STORAGE_PARAM_MAGIC))
  {
    StorageManager_DefaultParam(&g_storage_param);
    need_save_default = 1U;
    COM_DEBUG("StorageManager: first boot or invalid param, use defaults");
  }

  /* 正式设备默认允许写内部 Flash。
   * 之前这里首启默认关闭，导致录入流程只改 RAM，不会真正持久化。 */
  g_storage_flash_write_enabled = 1U;

  if ((param_ok != 0U) && (g_storage_param.magic == STORAGE_PARAM_MAGIC))
  {
    memcpy(g_storage_users, (const void *)STORAGE_ADDR_USER_BASE, sizeof(g_storage_users));
  }
  else
  {
    for (i = 0U; i < STORAGE_MAX_USER_COUNT; i++)
    {
      memset(&g_storage_users[i], 0, sizeof(g_storage_users[i]));
    }
  }

  if (need_save_default != 0U)
  {
    if (StorageManager_SaveParam(&g_storage_param) != 0U)
    {
      COM_DEBUG("StorageManager: default param saved to flash");
    }
    else
    {
      COM_DEBUG("StorageManager: default param save failed");
    }
  }
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

  if (g_storage_flash_write_enabled != 0U)
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

    if ((user.valid == 1U) && (memcmp(user.rc522_uid, uid, 4U) == 0))
    {
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

    if ((user.valid == 1U) && (user.finger_id == finger_id))
    {
      if (user_out != NULL)
      {
        *user_out = user;
      }
      return 1U;
    }
  }

  return 0U;
}

uint8_t StorageManager_FindUserById(uint32_t user_id, StorageUserTypeDef *user_out)
{
  StorageUserTypeDef user;
  uint32_t idx;

  if (user_id == 0U)
  {
    return 0U;
  }

  for (idx = 0U; idx < STORAGE_MAX_USER_COUNT; idx++)
  {
    if (StorageManager_ReadUserByIndex(idx, &user) == 0U)
    {
      continue;
    }

    if ((user.valid == 1U) && (user.user_id == user_id))
    {
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
  uint32_t idx;
  uint32_t addr;
  uint32_t page_addr;
  uint32_t offset_in_page;
  StorageUserTypeDef slot_user;

  if ((user == NULL) || (user->user_id == 0U))
  {
    return 0U;
  }

  for (idx = 0U; idx < STORAGE_MAX_USER_COUNT; idx++)
  {
    if (StorageManager_ReadUserByIndex(idx, &slot_user) == 0U)
    {
      continue;
    }

    if ((slot_user.valid == 1U) && (slot_user.user_id == user->user_id))
    {
      g_storage_users[idx] = *user;

      if (g_storage_flash_write_enabled != 0U)
      {
        addr = StorageManager_UserAddr(idx);
        page_addr = addr & ~(STORAGE_PAGE_SIZE - 1UL);
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

  if (uid == NULL)
  {
    return 0U;
  }

  for (idx = 0U; idx < STORAGE_MAX_USER_COUNT; idx++)
  {
    if (g_storage_users[idx].valid == 1U)
    {
      continue;
    }

    memset(&user, 0, sizeof(user));
    user.user_id = StorageManager_GetNextFreeUserId();
    if (user.user_id == 0U)
    {
      return 0U;
    }
    user.valid = 1U;
    user.finger_id = finger_id;
    memcpy(user.rc522_uid, uid, 4U);
    snprintf(user.employee_no, sizeof(user.employee_no), "%04lu", (unsigned long)user.user_id);
    snprintf(user.name, sizeof(user.name), "USER%04lu", (unsigned long)user.user_id);

    g_storage_users[idx] = user;

    if (g_storage_flash_write_enabled != 0U)
    {
      addr = StorageManager_UserAddr(idx);
      page_addr = addr & ~(STORAGE_PAGE_SIZE - 1UL);
      offset_in_page = addr - page_addr;
      if (StorageManager_UpdatePage(page_addr,
                                    offset_in_page,
                                    (const uint8_t *)&user,
                                    sizeof(StorageUserTypeDef)) == 0U)
      {
        COM_DEBUG("StorageManager: save user page failed, idx=%lu", (unsigned long)idx);
        return 0U;
      }
    }

    g_storage_param.next_user_id = StorageManager_GetNextFreeUserId();
    g_storage_param.user_count++;
    if (StorageManager_SaveParam(&g_storage_param) == 0U)
    {
      COM_DEBUG("StorageManager: save param after create user failed");
      return 0U;
    }

    if (user_out != NULL)
    {
      *user_out = user;
    }

    COM_DEBUG("StorageManager: user created id=%lu finger=%u",
              (unsigned long)user.user_id,
              (unsigned)user.finger_id);
    return 1U;
  }

  return 0U;
}

uint8_t StorageManager_AppendRecord(const StorageRecordTypeDef *record, uint32_t *record_index_out)
{
  StorageRecordTypeDef record_local;
  FLASH_EraseInitTypeDef erase_init;
  uint32_t record_index;
  uint32_t addr;
  uint32_t page_addr;
  uint32_t page_error = 0U;

  if (record == NULL)
  {
    return 0U;
  }

  record_local = *record;
  record_index = g_storage_param.next_record_index % STORAGE_MAX_RECORD_COUNT;
  record_local.record_id = g_storage_param.next_record_index + 1U;
  addr = StorageManager_RecordAddr(record_index);
  page_addr = addr & ~(STORAGE_PAGE_SIZE - 1UL);

  if (g_storage_flash_write_enabled != 0U)
  {
    if ((addr % STORAGE_PAGE_SIZE) == 0U)
    {
      HAL_FLASH_Unlock();
      erase_init.TypeErase = FLASH_TYPEERASE_PAGES;
      erase_init.PageAddress = page_addr;
      erase_init.NbPages = 1U;
      if (HAL_FLASHEx_Erase(&erase_init, &page_error) != HAL_OK)
      {
        HAL_FLASH_Lock();
        return 0U;
      }
      HAL_FLASH_Lock();
    }

    HAL_FLASH_Unlock();
    if (StorageManager_ProgramWords(addr,
                                    (const uint8_t *)&record_local,
                                    sizeof(StorageRecordTypeDef)) == 0U)
    {
      HAL_FLASH_Lock();
      return 0U;
    }
    HAL_FLASH_Lock();
  }

  g_storage_param.next_record_index++;
  if (StorageManager_SaveParam(&g_storage_param) == 0U)
  {
    return 0U;
  }

  if (record_index_out != NULL)
  {
    *record_index_out = record_index;
  }

  return 1U;
}

uint8_t StorageManager_LoadUserData(void *buffer, uint32_t buffer_size)
{
  if ((buffer == NULL) || (buffer_size < sizeof(g_storage_users)))
  {
    return 0U;
  }

  memcpy(buffer, g_storage_users, sizeof(g_storage_users));
  return 1U;
}

uint8_t StorageManager_SaveUserData(void *buffer, uint32_t buffer_size)
{
  uint32_t i;

  if ((buffer == NULL) || (buffer_size < sizeof(g_storage_users)))
  {
    return 0U;
  }

  memcpy(g_storage_users, buffer, sizeof(g_storage_users));

  if (g_storage_flash_write_enabled != 0U)
  {
    for (i = 0U; i < STORAGE_MAX_USER_COUNT; i++)
    {
      uint32_t addr = StorageManager_UserAddr(i);
      uint32_t page_addr = addr & ~(STORAGE_PAGE_SIZE - 1UL);
      uint32_t offset = addr - page_addr;

      if (StorageManager_UpdatePage(page_addr,
                                    offset,
                                    (const uint8_t *)&g_storage_users[i],
                                    sizeof(StorageUserTypeDef)) == 0U)
      {
        return 0U;
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
  if ((index >= STORAGE_MAX_USER_COUNT) || (user_out == NULL))
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

uint8_t StorageManager_DeleteUser(uint32_t user_id)
{
  uint32_t idx;

  for (idx = 0U; idx < STORAGE_MAX_USER_COUNT; idx++)
  {
    if ((g_storage_users[idx].valid == 1U) && (g_storage_users[idx].user_id == user_id))
    {
      uint32_t last = STORAGE_MAX_USER_COUNT - 1U;
      StorageUserTypeDef empty_user;

      while ((last > 0U) && (g_storage_users[last].valid == 0U))
      {
        last--;
      }

      if (idx != last)
      {
        g_storage_users[idx] = g_storage_users[last];
        memset(&g_storage_users[last], 0, sizeof(StorageUserTypeDef));
      }
      else
      {
        memset(&g_storage_users[idx], 0, sizeof(StorageUserTypeDef));
      }

      if (g_storage_flash_write_enabled != 0U)
      {
        uint32_t addr_idx = StorageManager_UserAddr(idx);
        uint32_t page_addr_idx = addr_idx & ~(STORAGE_PAGE_SIZE - 1UL);
        uint32_t offset_idx = addr_idx - page_addr_idx;
        (void)StorageManager_UpdatePage(page_addr_idx,
                                        offset_idx,
                                        (const uint8_t *)&g_storage_users[idx],
                                        sizeof(StorageUserTypeDef));

        memset(&empty_user, 0, sizeof(empty_user));
        {
          uint32_t addr_last = StorageManager_UserAddr(last);
          uint32_t page_addr_last = addr_last & ~(STORAGE_PAGE_SIZE - 1UL);
          uint32_t offset_last = addr_last - page_addr_last;
          (void)StorageManager_UpdatePage(page_addr_last,
                                          offset_last,
                                          (const uint8_t *)&empty_user,
                                          sizeof(StorageUserTypeDef));
        }
      }

      if (g_storage_param.user_count > 0U)
      {
        g_storage_param.user_count--;
      }
      g_storage_param.next_user_id = StorageManager_GetNextFreeUserId();
      (void)StorageManager_SaveParam(&g_storage_param);
      return 1U;
    }
  }

  return 0U;
}
