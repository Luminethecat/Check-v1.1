#ifndef USER_DB_H
#define USER_DB_H

#include "attendance_app.h"
#include <stdint.h>

/* 简单内存型用户数据库接口（非线程安全） */

void UserDb_Init(void);

/* 添加用户，成功返回 1，已存在或空间不足返回 0 */
uint8_t UserDb_AddUser(const AttendanceUserTypeDef *user);

/* 按 user_id 查找，找到返回 1 并拷贝到 out_user；未找到返回 0 */
uint8_t UserDb_FindById(uint32_t user_id, AttendanceUserTypeDef *out_user);

/* 按指纹 id 查找，找到返回 1 并拷贝到 out_user；未找到返回 0 */
uint8_t UserDb_FindByFinger(uint16_t finger_id, AttendanceUserTypeDef *out_user);

/* 按 rc522 uid 查找（4 字节），找到返回 1 并拷贝到 out_user；未找到返回 0 */
uint8_t UserDb_FindByCard(const uint8_t uid[4], AttendanceUserTypeDef *out_user);

/* 删除用户，成功返回 1；否则返回 0 */
uint8_t UserDb_Remove(uint32_t user_id);

/* 当前用户数量 */
uint16_t UserDb_Count(void);


/* Backwards compatibility: expose old UserTypeDef and UserDB_* API names mapped to new implementations */
typedef AttendanceUserTypeDef UserTypeDef;

/* Old-style API wrappers (implemented in user_db.c) */
void UserDB_Init(void);
uint8_t UserDB_AddUser(const UserTypeDef* user);
void UserDB_AddTestUser(void);
void UserDB_PrintAllUsers(void);
uint32_t UserDB_IdentifyByFingerprint(void);
uint32_t UserDB_IdentifyByRFID(const uint8_t rfid_uid[4]);
UserTypeDef* UserDB_GetUser(uint32_t user_id);
uint8_t UserDB_DeleteUser(uint32_t user_id);
uint16_t UserDB_GetUserCount(void);


#endif
