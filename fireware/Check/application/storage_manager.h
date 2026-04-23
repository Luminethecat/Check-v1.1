#ifndef STORAGE_MANAGER_H
#define STORAGE_MANAGER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "attendance_app.h"
#include "storage_layout.h"

#define STORAGE_PARAM_MAGIC                0x544B4348UL
#define STORAGE_USER_NAME_LEN              16U
#define STORAGE_EMPLOYEE_NO_LEN            12U

typedef struct
{
  uint32_t magic;
  uint16_t work_start_min;
  uint16_t work_end_min;
  uint16_t split_min;
  uint8_t rtc_valid;
  uint8_t reserved0;
  uint16_t reserved1;
  uint32_t next_user_id;
  uint32_t user_count;
  uint32_t next_record_index;
  uint32_t reserved2[4];
} StorageParamTypeDef;

typedef struct
{
  uint32_t user_id;
  uint8_t rc522_uid[4];
  uint16_t finger_id;
  char employee_no[STORAGE_EMPLOYEE_NO_LEN];
  char name[STORAGE_USER_NAME_LEN];
  uint8_t valid;
  uint8_t reserved[9];
} StorageUserTypeDef;

typedef struct
{
  uint32_t record_id;
  uint32_t user_id;
  uint16_t year;
  uint8_t month;
  uint8_t day;
  uint8_t hour;
  uint8_t minute;
  uint8_t second;
  uint8_t verify_type;
  uint8_t result_type;
  uint8_t upload_flag;
  uint8_t reserved0;
  uint32_t reserved1[3];
} StorageRecordTypeDef;

/* 本地存储管理接口：
 * 负责参数、用户档案、打卡记录的持久化。 */
void StorageManager_Init(void);
StorageParamTypeDef StorageManager_GetParam(void);
uint8_t StorageManager_SaveParam(const StorageParamTypeDef *param);
void StorageManager_SetFlashWriteEnabled(uint8_t enabled);

uint8_t StorageManager_FindUserByCard(const uint8_t uid[4], StorageUserTypeDef *user_out);
uint8_t StorageManager_FindUserByFinger(uint16_t finger_id, StorageUserTypeDef *user_out);
uint8_t StorageManager_FindUserById(uint32_t user_id, StorageUserTypeDef *user_out);
uint16_t StorageManager_GetNextFreeUserId(void);
uint8_t StorageManager_SaveUser(const StorageUserTypeDef *user);
uint8_t StorageManager_CreateUser(const uint8_t uid[4], uint16_t finger_id, StorageUserTypeDef *user_out);
uint8_t StorageManager_AppendRecord(const StorageRecordTypeDef *record, uint32_t *record_index_out);
uint8_t StorageManager_LoadUserData(void *buffer, uint32_t buffer_size);
uint8_t StorageManager_SaveUserData(void *buffer, uint32_t buffer_size);
uint32_t StorageManager_GetUserCount(void);
uint8_t StorageManager_GetUserByIndex(uint32_t index, StorageUserTypeDef *user_out);
uint8_t StorageManager_DeleteUser(uint32_t user_id);

#ifdef __cplusplus
}
#endif

#endif
