#include "user_db.h"
#include "Com_debug.h"
#include <string.h>

/* 简单数组存储实现，后续可替换为 Flash/EEPROM 存储。
 * 该实现为非线程安全；若在多任务场景中并发访问，应在外部加互斥保护。
 */

#define USER_DB_MAX_ENTRIES 128U

static AttendanceUserTypeDef s_users[USER_DB_MAX_ENTRIES];
static uint16_t s_user_count = 0U;

void UserDb_Init(void)
{
  memset(s_users, 0, sizeof(s_users));
  s_user_count = 0U;
}

uint8_t UserDb_AddUser(const AttendanceUserTypeDef *user)
{
  if (user == NULL) return 0U;

  /* 若 user_id 已存在，拒绝重复添加 */
  for (uint16_t i = 0; i < s_user_count; i++) {
    if (s_users[i].user_id == user->user_id) {
      return 0U;
    }
  }

  if (s_user_count >= USER_DB_MAX_ENTRIES) return 0U;

  memcpy(&s_users[s_user_count], user, sizeof(AttendanceUserTypeDef));
  s_users[s_user_count].valid = 1U;
  s_user_count++;
  return 1U;
}

uint8_t UserDb_FindById(uint32_t user_id, AttendanceUserTypeDef *out_user)
{
  if (out_user == NULL) return 0U;
  for (uint16_t i = 0; i < s_user_count; i++) {
    if (s_users[i].valid && s_users[i].user_id == user_id) {
      memcpy(out_user, &s_users[i], sizeof(AttendanceUserTypeDef));
      return 1U;
    }
  }
  return 0U;
}

uint8_t UserDb_FindByFinger(uint16_t finger_id, AttendanceUserTypeDef *out_user)
{
  if (out_user == NULL) return 0U;
  for (uint16_t i = 0; i < s_user_count; i++) {
    if (s_users[i].valid && s_users[i].finger_id == finger_id && finger_id != 0U) {
      memcpy(out_user, &s_users[i], sizeof(AttendanceUserTypeDef));
      return 1U;
    }
  }
  return 0U;
}

uint8_t UserDb_FindByCard(const uint8_t uid[4], AttendanceUserTypeDef *out_user)
{
  if (uid == NULL || out_user == NULL) return 0U;
  for (uint16_t i = 0; i < s_user_count; i++) {
    if (!s_users[i].valid) continue;
    if (memcmp(s_users[i].rc522_uid, uid, 4) == 0) {
      memcpy(out_user, &s_users[i], sizeof(AttendanceUserTypeDef));
      return 1U;
    }
  }
  return 0U;
}

uint8_t UserDb_Remove(uint32_t user_id)
{
  for (uint16_t i = 0; i < s_user_count; i++) {
    if (s_users[i].valid && s_users[i].user_id == user_id) {
      /* 标记无效并紧凑数组 */
      s_users[i].valid = 0U;
      if (i != (s_user_count - 1U)) {
        memcpy(&s_users[i], &s_users[s_user_count - 1U], sizeof(AttendanceUserTypeDef));
      }
      memset(&s_users[s_user_count - 1U], 0, sizeof(AttendanceUserTypeDef));
      s_user_count--;
      return 1U;
    }
  }
  return 0U;
}

uint16_t UserDb_Count(void)
{
  return s_user_count;
}

/* Backwards-compatible wrappers for existing code that used UserDB_* APIs */
void UserDB_Init(void)
{
  UserDb_Init();
}

uint8_t UserDB_AddUser(const UserTypeDef* user)
{
  if (user == NULL) return 0U;
  return UserDb_AddUser((const AttendanceUserTypeDef*)user);
}

void UserDB_AddTestUser(void)
{
  AttendanceUserTypeDef u;
  memset(&u, 0, sizeof(u));
  u.user_id = 1;
  strncpy(u.employee_no, "EMP001", sizeof(u.employee_no)-1);
  strncpy(u.name, "TestUser1", sizeof(u.name)-1);
  u.rc522_uid[0] = 0x12; u.rc522_uid[1] = 0x34; u.rc522_uid[2] = 0x56; u.rc522_uid[3] = 0x78;
  u.finger_id = 1;
  UserDb_AddUser(&u);

  memset(&u, 0, sizeof(u));
  u.user_id = 2;
  strncpy(u.employee_no, "EMP002", sizeof(u.employee_no)-1);
  strncpy(u.name, "TestUser2", sizeof(u.name)-1);
  u.rc522_uid[0] = 0x87; u.rc522_uid[1] = 0x65; u.rc522_uid[2] = 0x43; u.rc522_uid[3] = 0x21;
  u.finger_id = 2;
  UserDb_AddUser(&u);
}

void UserDB_PrintAllUsers(void)
{
  COM_DEBUG("=== User List ===");
  for (uint16_t i = 0; i < s_user_count; i++) {
    if (s_users[i].valid) {
      COM_DEBUG("ID:%u Name:%s No:%s RFID:%02X%02X%02X%02X Finger:%u",
                s_users[i].user_id,
                s_users[i].name,
                s_users[i].employee_no,
                s_users[i].rc522_uid[0], s_users[i].rc522_uid[1], s_users[i].rc522_uid[2], s_users[i].rc522_uid[3],
                s_users[i].finger_id);
    }
  }
  COM_DEBUG("Total: %u users", s_user_count);
}

uint32_t UserDB_IdentifyByFingerprint(void)
{
  /* Not implemented: requires fingerprint sensor integration. Return 0 as "not found". */
  return 0U;
}

uint32_t UserDB_IdentifyByRFID(const uint8_t rfid_uid[4])
{
  AttendanceUserTypeDef out;
  if (UserDb_FindByCard(rfid_uid, &out)) {
    return out.user_id;
  }
  return 0U;
}

UserTypeDef* UserDB_GetUser(uint32_t user_id)
{
  for (uint16_t i = 0; i < s_user_count; i++) {
    if (s_users[i].valid && s_users[i].user_id == user_id) {
      return (UserTypeDef*)&s_users[i];
    }
  }
  return NULL;
}

uint8_t UserDB_DeleteUser(uint32_t user_id)
{
  return UserDb_Remove(user_id);
}

uint16_t UserDB_GetUserCount(void)
{
  return UserDb_Count();
}