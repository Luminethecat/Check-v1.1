#include "user_db.h"
#include "../commen/Com_debug.h"
#include "storage_manager.h"
#include <string.h>
// 定义存储相关类型
#define STORAGE_OK 1
typedef UserTypeDef UserDatabaseTypeDef[MAX_USERS];

static UserTypeDef g_users[MAX_USERS];
static uint16_t g_user_count = 0;
static uint32_t g_next_user_id = 1;

void UserDB_Init(void)
{
    // 尝试从存储加载用户数据库
    if (StorageManager_LoadUserData((UserDatabaseTypeDef*)&g_users, sizeof(g_users)) != STORAGE_OK)
    {
        // 如果加载失败，初始化空数据库
        g_user_count = 0;
        g_next_user_id = 1;
        COM_DEBUG("User database initialized with empty data");
    }
    else
    {
        COM_DEBUG("User database loaded successfully, %d users", g_user_count);
    }
}

uint8_t UserDB_AddUser(const UserTypeDef* user)
{
    if (user == NULL || g_user_count >= MAX_USERS)
    {
        return 0;
    }

    // 检查用户ID是否已存在
    for (uint16_t i = 0; i < g_user_count; i++)
    {
        if (g_users[i].user_id == user->user_id)
        {
            return 0;
        }
    }

    // 添加新用户
    UserTypeDef new_user = *user;
    new_user.user_id = g_next_user_id++;
    new_user.is_active = 1;

    g_users[g_user_count++] = new_user;

    // 保存到存储
    StorageManager_SaveUserData((UserDatabaseTypeDef*)&g_users, sizeof(g_users));

    COM_DEBUG("User added: ID=%u, Name=%s", new_user.user_id, new_user.name);
    return 1;
}

uint32_t UserDB_IdentifyByFingerprint(void)
{
    ZW101_SearchResultTypeDef search_result;
    ZW101_StatusTypeDef status;

    status = ZW101_Identify(&search_result);
    if (status != ZW101_OK)
    {
        return 0;
    }

    // 查找匹配的指纹
    for (uint16_t i = 0; i < g_user_count; i++)
    {
        if (g_users[i].is_active &&
            g_users[i].fingerprint_page_id == search_result.page_id)
        {
            return g_users[i].user_id;
        }
    }

    return 0;
}

uint32_t UserDB_IdentifyByRFID(const uint8_t rfid_uid[4])
{
    // 查找匹配的RFID
    for (uint16_t i = 0; i < g_user_count; i++)
    {
        if (g_users[i].is_active)
        {
            if (memcmp(g_users[i].rfid_uid, rfid_uid, 4) == 0)
            {
                return g_users[i].user_id;
            }
        }
    }

    return 0;
}

UserTypeDef* UserDB_GetUser(uint32_t user_id)
{
    for (uint16_t i = 0; i < g_user_count; i++)
    {
        if (g_users[i].user_id == user_id)
        {
            return &g_users[i];
        }
    }
    return NULL;
}

uint8_t UserDB_DeleteUser(uint32_t user_id)
{
    for (uint16_t i = 0; i < g_user_count; i++)
    {
        if (g_users[i].user_id == user_id)
        {
            // 标记为非活动状态
            g_users[i].is_active = 0;
            StorageManager_SaveUserData((UserDatabaseTypeDef*)&g_users, sizeof(g_users));
            COM_DEBUG("User deleted: ID=%u", user_id);
            return 1;
        }
    }
    return 0;
}

uint16_t UserDB_GetUserCount(void)
{
    return g_user_count;
}

// 添加测试用户
void UserDB_AddTestUser(void)
{
    UserTypeDef test_user;

    // 测试用户1
    memset(&test_user, 0, sizeof(test_user));
    test_user.user_id = 1;
    strcpy(test_user.name, "张三");
    strcpy(test_user.employee_no, "EMP001");
    test_user.rfid_uid[0] = 0x12;
    test_user.rfid_uid[1] = 0x34;
    test_user.rfid_uid[2] = 0x56;
    test_user.rfid_uid[3] = 0x78;
    test_user.fingerprint_page_id = 1;
    test_user.is_active = 1;

    if (UserDB_AddUser(&test_user)) {
        COM_DEBUG("Test user 1 added: %s", test_user.name);
    } else {
        COM_DEBUG("Failed to add test user 1");
    }

    // 测试用户2
    test_user.user_id = 2;
    strcpy(test_user.name, "李四");
    strcpy(test_user.employee_no, "EMP002");
    test_user.rfid_uid[0] = 0x87;
    test_user.rfid_uid[1] = 0x65;
    test_user.rfid_uid[2] = 0x43;
    test_user.rfid_uid[3] = 0x21;
    test_user.fingerprint_page_id = 2;
    test_user.is_active = 1;

    if (UserDB_AddUser(&test_user)) {
        COM_DEBUG("Test user 2 added: %s", test_user.name);
    } else {
        COM_DEBUG("Failed to add test user 2");
    }
}

// 打印所有用户信息
void UserDB_PrintAllUsers(void)
{
    COM_DEBUG("=== User List ===");
    for (uint16_t i = 0; i < g_user_count; i++) {
        if (g_users[i].is_active) {
            COM_DEBUG("ID:%u Name:%s No:%s RFID:%02X%02X%02X%02X Finger:%u",
                     g_users[i].user_id,
                     g_users[i].name,
                     g_users[i].employee_no,
                     g_users[i].rfid_uid[0],
                     g_users[i].rfid_uid[1],
                     g_users[i].rfid_uid[2],
                     g_users[i].rfid_uid[3],
                     g_users[i].fingerprint_page_id);
        }
    }
    COM_DEBUG("Total: %u users", g_user_count);
}