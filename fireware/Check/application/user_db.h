#ifndef USER_DB_H
#define USER_DB_H

#include "zw101_app.h"
#include "rc522_app.h"

#define MAX_USERS 20

typedef struct {
    uint32_t user_id;
    uint8_t rfid_uid[4];
    uint16_t fingerprint_page_id;
    char employee_no[12];
    char name[16];
    uint8_t is_active;
} UserTypeDef;

// 初始化用户数据库
void UserDB_Init(void);

// 添加用户
uint8_t UserDB_AddUser(const UserTypeDef* user);

// 通过串口添加测试用户
void UserDB_AddTestUser(void);

// 打印用户信息
void UserDB_PrintAllUsers(void);

// 根据指纹识别用户
uint32_t UserDB_IdentifyByFingerprint(void);

// 根据RFID识别用户
uint32_t UserDB_IdentifyByRFID(const uint8_t rfid_uid[4]);

// 获取用户信息
UserTypeDef* UserDB_GetUser(uint32_t user_id);

// 删除用户
uint8_t UserDB_DeleteUser(uint32_t user_id);

// 获取用户总数
uint16_t UserDB_GetUserCount(void);

#endif /* USER_DB_H */