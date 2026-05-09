#include "runtime_manager.h"

#include "Com_debug.h"
#include "app_services.h"
#include "cmsis_os.h"
#include "key_input.h"
#include "storage_manager.h"
#include "Com_protocol.h"
#include "oled_ssd1306.h"
#include "string.h"
#include "audio_dac_app.h"   // 或者 "dac_sound.h"，根据你实际文件名
/*
 * 模块说明：
 * runtime_manager 考勤机运行时状态机管理层
 * 核心职责：
 * 1. 整机状态流转：待机 / 发卡录入 / 指纹录入
 * 2. 刷卡、指纹采集轮询与事件分发
 * 3. 考勤规则判定、上下班/迟到/早退逻辑
 * 4. OLED页面管理、超时自动切回待机
 * 5. 本地记录存储、ESP事件上报、语音提示联动
 * 6. RTC时间周期同步、跨天状态自动清零
 */

// 指纹模块ZW101 通讯默认密码
#define ZW101_DEFAULT_PASSWORD            0U
// 待机调试日志打印周期 1000ms
#define RUNTIME_IDLE_LOG_PERIOD_MS        1000U
// 打卡结果页面默认常驻时长 3000ms
#define RUNTIME_RESULT_HOLD_MS            3000U
// 时间同步检测轮询周期 10s
#define RUNTIME_TIME_SYNC_PERIOD_MS       10000U
// 指纹低频轮询间隔 1500ms，防止频繁查询占用资源
#define RUNTIME_FINGER_SCAN_PERIOD_MS     1500U
// 指纹IRQ中断触发后缓冲防抖时长 2500ms
#define RUNTIME_FINGER_IRQ_GRACE_MS       2500U

/**
 * @brief 运行模式状态枚举（核心状态机）
 */
typedef enum
{
  RUNTIME_MODE_IDLE = 0,                // 正常待机打卡模式
  RUNTIME_MODE_ENROLL_WAIT_CARD,        // 录入模式：等待刷IC卡
  RUNTIME_MODE_ENROLL_WAIT_FINGER,      // 录入模式：等待按压指纹
  RUNTIME_MODE_USER_LIST,               // 浏览已注册用户列表
  RUNTIME_MODE_USER_LIST_CONFIRM,       // 用户删除确认对话框
} RuntimeModeTypeDef;

/**
 * @brief 全局运行时上下文结构体
 * 统一保存整机运行状态、界面、时间、计时戳、缓存数据
 */
typedef struct
{
  RuntimeModeTypeDef mode;                     // 当前运行模式
  AttendanceDailyStateTypeDef daily_state;     // 当日考勤全局状态（是否已上下班打卡）
  AttendanceDisplayModelTypeDef display;       // OLED当前显示缓冲区
  AttendanceDateTimeTypeDef now;               // 当前解析后的年月日时分秒
  uint16_t today_count;                       // 今日累计打卡人次
  uint32_t display_expire_tick;               // 界面超时自动切回待机的时间戳
  uint32_t last_idle_log_tick;                // 上一次打印调试日志的时间戳
  uint32_t last_time_sync_tick;               // 上一次触发时间同步检测的时间戳
  uint32_t last_finger_scan_tick;              // 上一次指纹扫描时间戳
  uint8_t last_rtc_valid;                      // 上一轮RTC时钟是否有效标记
  uint8_t pending_card_uid[4];                 // 录入模式临时缓存的卡号UID
  uint8_t pending_card_valid;                  // 临时卡号是否有效标志位
  uint32_t user_list_index;                    // 用户列表当前索引
  uint32_t user_list_count;                    // 用户列表计数
  uint32_t user_list_pending_delete_index;     // 待删除用户索引（确认阶段）
  uint8_t user_list_confirm_choice;            // 0=no,1=yes
} RuntimeContextTypeDef;

// 本文件静态全局运行时实例
static RuntimeContextTypeDef g_runtime;
static void RuntimeManager_HandleUserList(KeyEventTypeDef key_event);
static void RuntimeManager_HandleUserListConfirm(KeyEventTypeDef key_event);
/**
 * @brief 根据考勤结果播放对应提示音
 * @param result 考勤结果枚举
 */
static void RuntimeManager_PlayResultSound(AttendanceResultTypeDef result)
{
  switch (result)
  {
    case ATTENDANCE_RESULT_ON_DUTY_OK:    // 上班打卡成功
    case ATTENDANCE_RESULT_OFF_DUTY_OK:   // 下班打卡成功
      DAC_Sound_Success();                // 成功音
      break;

    case ATTENDANCE_RESULT_LATE:          // 迟到
      DAC_Sound_Error();                  // 错误音（可用长音代替）
      break;

    case ATTENDANCE_RESULT_EARLY:         // 早退
      DAC_Sound_Error();                  // 同样用错误音
      break;

    default:                              // 未知用户/时间非法/其他错误
      DAC_Sound_Error();                  // 错误音
      break;
  }
}

/**
 * @brief 更新全局显示缓存，并设置页面自动超时时间
 * @param display 待刷新的显示结构体指针
 * @note 常驻页面hold_ms=0，临时结果页默认3秒自动消失
 */
static void RuntimeManager_SetDisplay(const AttendanceDisplayModelTypeDef *display)
{
  // 空指针防护
  if (display == NULL)
  {
    return;
  }

  // 覆盖更新全局显示内容
  g_runtime.display = *display;
  // 计算超时截止tick：自定义时长 > 0 用自定义，否则用默认3秒
  g_runtime.display_expire_tick = HAL_GetTick() + ((display->hold_ms > 0U) ? display->hold_ms : RUNTIME_RESULT_HOLD_MS);
}

/**
 * @brief 刷新待机界面：时间+日期+今日打卡人数
 * @note 包含跨天自动清零逻辑，保证每日数据独立
 */
static void RuntimeManager_ShowIdle(void)
{
  // 获取当前RTC/网络同步时间
  if (Attendance_GetCurrentDateTime(&g_runtime.now) == 0U)
  {
    /* RTC 未同步时显示等待界面，不退出 */
    AttendanceDisplayModelTypeDef display;
    memset(&display, 0, sizeof(display));
    display.page = OLED_PAGE_IDLE;
    snprintf(display.line1, sizeof(display.line1), "SYNC WAIT");
    snprintf(display.line2, sizeof(display.line2), "RTC not ready");
    snprintf(display.line3, sizeof(display.line3), "Wait for NTP...");
    snprintf(display.line4, sizeof(display.line4), "TODAY:%u", g_runtime.today_count);
    g_runtime.display = display;
    g_runtime.display.hold_ms = 0U;
    Oled_RenderDisplayModel(&g_runtime.display);
    return;
  }

  // 判断是否跨天，跨天重置当日考勤统计
  if (g_runtime.daily_state.ymd != 0U)
  {
    // 日期压缩编码：月4bit + 日5bit + 年7bit，节省存储
    uint16_t current_ymd = (uint16_t)(((g_runtime.now.month & 0x0FU) << 12U) |
                                      ((g_runtime.now.day & 0x1FU) << 7U) |
                                      (g_runtime.now.year & 0x7FU));
    // 日期不一致 = 新的一天
    if (current_ymd != g_runtime.daily_state.ymd)
    {
      g_runtime.today_count = 0U;                // 清空今日打卡次数
      g_runtime.daily_state.has_on_duty = 0U;    // 清除已上班标记
      g_runtime.daily_state.has_off_duty = 0U;   // 清除已下班标记
      g_runtime.daily_state.ymd = current_ymd;  // 更新为当天日期编码
    }
  }

  // 组装待机页UI数据
  Attendance_BuildIdleDisplay(&g_runtime.display, &g_runtime.now, g_runtime.today_count, 1U);
  // 待机页面常驻，不启用超时
  g_runtime.display.hold_ms = 0U;
}

/**
 * @brief 统一考勤事件处理入口：存记录、上报ESP、弹窗、语音
 * @param storage_user 匹配到的用户存储信息
 * @param verify_type 验证方式：刷卡/指纹
 * @param result 最终考勤判定结果
 */
static void RuntimeManager_PublishEvent(const StorageUserTypeDef *storage_user,
                                        AttendanceVerifyTypeDef verify_type,
                                        AttendanceResultTypeDef result)
{
  AttendanceEventTypeDef event;
  AttendanceUserTypeDef user_view;
  AttendanceDisplayModelTypeDef display;
  StorageRecordTypeDef record;

  // 结构体清零，防止脏数据
  memset(&event, 0, sizeof(event));
  memset(&user_view, 0, sizeof(user_view));
  memset(&record, 0, sizeof(record));

  // 获取打卡时间戳
  if (Attendance_GetCurrentDateTime(&event.timestamp) == 0U)
  {
    return;
  }

  // 拷贝用户信息，用于界面展示与上报
  if (storage_user != NULL)
  {
    event.user_id = storage_user->user_id;
    user_view.user_id = storage_user->user_id;
    user_view.valid = storage_user->valid;
    user_view.finger_id = storage_user->finger_id;
    memcpy(user_view.rc522_uid, storage_user->rc522_uid, sizeof(user_view.rc522_uid));
    memcpy(user_view.employee_no, storage_user->employee_no, sizeof(user_view.employee_no));
    memcpy(user_view.name, storage_user->name, sizeof(user_view.name));
  }

  // 填充事件类型与结果
  event.verify_type = verify_type;
  event.result = result;

  // 仅有效考勤结果：写入本地Flash + WiFi上传
  if (result == ATTENDANCE_RESULT_ON_DUTY_OK ||
      result == ATTENDANCE_RESULT_OFF_DUTY_OK ||
      result == ATTENDANCE_RESULT_LATE ||
      result == ATTENDANCE_RESULT_EARLY)
  {
    // 更新当日上下班状态
    Attendance_UpdateDailyState(&g_runtime.daily_state, &event.timestamp, result);
    // 今日打卡计数+1
    g_runtime.today_count++;

    // 组装本地打卡记录
    record.user_id = event.user_id;
    record.year = event.timestamp.year;
    record.month = event.timestamp.month;
    record.day = event.timestamp.day;
    record.hour = event.timestamp.hour;
    record.minute = event.timestamp.minute;
    record.second = event.timestamp.second;
    record.verify_type = (uint8_t)verify_type;
    record.result_type = (uint8_t)result;
    record.upload_flag = 0U;  // 标记待上传

    // 追加记录到存储区
    (void)StorageManager_AppendRecord(&record, NULL);
    // 发送事件给ESP模块，用于网络上传
    (void)Attendance_SendEventToEsp(&event);
  }

  // 生成打卡结果页面
  Attendance_BuildResultDisplay(&display, (storage_user != NULL) ? &user_view : NULL, &event);
  // 更新显示并开启超时
  RuntimeManager_SetDisplay(&display);
  // 播放对应语音提示
  RuntimeManager_PlayResultSound(result);
}

/**
 * @brief 刷卡打卡业务处理
 * @param uid 读取到的4字节卡片UID
 */
static void RuntimeManager_HandleCardCheckIn(const uint8_t uid[4])
{
  StorageUserTypeDef user;
  AttendanceResultTypeDef result;
  AttendanceScheduleTypeDef schedule;

  // 根据卡号查询用户，无匹配则提示未知用户
  if (StorageManager_FindUserByCard(uid, &user) == 0U)
  {
    RuntimeManager_PublishEvent(NULL, ATTENDANCE_VERIFY_CARD, ATTENDANCE_RESULT_UNKNOWN_USER);
    return;
  }

  // 时间无效直接拦截打卡
  if (Attendance_GetCurrentDateTime(&g_runtime.now) == 0U)
  {
    RuntimeManager_PublishEvent(&user, ATTENDANCE_VERIFY_CARD, ATTENDANCE_RESULT_TIME_INVALID);
    return;
  }

  // 获取系统排班规则
  schedule = Attendance_GetSchedule();
  // 核心考勤规则判断
  result = Attendance_JudgeEvent(&schedule, &g_runtime.daily_state, &g_runtime.now);
  // 统一事件分发
  RuntimeManager_PublishEvent(&user, ATTENDANCE_VERIFY_CARD, result);
}

/**
 * @brief 指纹打卡业务处理
 * @param finger_id 识别到的指纹编号
 */
static void RuntimeManager_HandleFingerCheckIn(uint16_t finger_id)
{
  StorageUserTypeDef user;
  AttendanceResultTypeDef result;
  AttendanceScheduleTypeDef schedule;

  // 根据指纹ID查询用户
  if (StorageManager_FindUserByFinger(finger_id, &user) == 0U)
  {
    RuntimeManager_PublishEvent(NULL, ATTENDANCE_VERIFY_FINGER, ATTENDANCE_RESULT_UNKNOWN_USER);
    return;
  }

  // 时间有效性校验
  if (Attendance_GetCurrentDateTime(&g_runtime.now) == 0U)
  {
    RuntimeManager_PublishEvent(&user, ATTENDANCE_VERIFY_FINGER, ATTENDANCE_RESULT_TIME_INVALID);
    return;
  }

  // 获取排班 + 判定考勤结果
  schedule = Attendance_GetSchedule();
  result = Attendance_JudgeEvent(&schedule, &g_runtime.daily_state, &g_runtime.now);
  RuntimeManager_PublishEvent(&user, ATTENDANCE_VERIFY_FINGER, result);
}

/**
 * @brief 轮询RC522读卡器，检测刷卡动作
 * @note 读取到卡片后休眠读卡器+短延时防重复刷卡
 */
static void RuntimeManager_PollCard(void)
{
  RC522_CardInfoTypeDef card;

  // 读到有效卡片且UID长度合法
  if (RC522_ReadCard(&card) == RC522_OK && card.uid_len >= 4U)
  {
    RuntimeManager_HandleCardCheckIn(card.uid);
    RC522_Halt();        // 卡片模块挂起，降低干扰
    osDelay(800U);       // 延时防抖，防止连续重复刷卡
  }
}

/**
 * @brief 指纹检测：中断优先 + 低频轮询兜底
 * @note 兼顾响应速度与CPU开销，防止指纹漏检
 */
static void RuntimeManager_PollFinger(void)
{
  ZW101_SearchResultTypeDef result;
  uint32_t now_tick = HAL_GetTick();
  uint8_t irq_pending = ZW101_IrqConsumePending();  // 清除中断挂起
  uint8_t irq_active = ZW101_IrqIsActiveLevel();    // 读取IRQ引脚电平

  // 无中断、无手指、未到轮询间隔：直接跳过
  if (irq_pending == 0U &&
      irq_active == 0U &&
      (now_tick - g_runtime.last_finger_scan_tick) < RUNTIME_FINGER_SCAN_PERIOD_MS)
  {
    return;
  }

  // 手指刚按下缓冲期内，暂不识别，避免误判
  if (irq_pending == 0U &&
      irq_active != 0U &&
      (now_tick - g_runtime.last_finger_scan_tick) < RUNTIME_FINGER_IRQ_GRACE_MS)
  {
    return;
  }

  // 更新本次扫描时间戳
  g_runtime.last_finger_scan_tick = now_tick;
  // 执行指纹比对识别
  if (App_Zw101_IdentifyUser(ZW101_DEFAULT_PASSWORD, &result) == ZW101_OK)
  {
    RuntimeManager_HandleFingerCheckIn(result.page_id);
    osDelay(800U);  // 识别成功防抖延时
  }
}

/**
 * @brief 录入模式专用处理函数
 * @param key_event 按键扫描事件
 * @note 两步录入：先刷卡绑定UID，再录入指纹绑定模板
 */
static void RuntimeManager_HandleEnrollMode(KeyEventTypeDef key_event)
{
  RC522_CardInfoTypeDef card;
  StorageUserTypeDef user;
  StorageParamTypeDef param;
  uint16_t next_finger_id;

  /********** 第一步：等待刷卡 **********/
  if (g_runtime.mode == RUNTIME_MODE_ENROLL_WAIT_CARD)
  {
    // 短按OK键：退出录入，返回待机
    if (key_event == KEY_EVENT_OK_SHORT)
    {
      g_runtime.mode = RUNTIME_MODE_IDLE;
      RuntimeManager_ShowIdle();
      return;
    }

    // 读到有效卡片，缓存UID并跳转指纹录入步骤
    if (RC522_ReadCard(&card) == RC522_OK && card.uid_len >= 4U)
    {
      memcpy(g_runtime.pending_card_uid, card.uid, 4U);
      g_runtime.pending_card_valid = 1U;
      // 切换状态：等待录入指纹
      g_runtime.mode = RUNTIME_MODE_ENROLL_WAIT_FINGER;
      // 更新录入第二步界面
      snprintf(g_runtime.display.line1, sizeof(g_runtime.display.line1), "Enroll Step2");
      snprintf(g_runtime.display.line2, sizeof(g_runtime.display.line2), "Put Finger");
      snprintf(g_runtime.display.line3, sizeof(g_runtime.display.line3), "Press on sensor");
      g_runtime.display.page = OLED_PAGE_ENROLL;
      g_runtime.display.hold_ms = 0U;
      RC522_Halt();
      osDelay(500U);
    }
    return;
  }

  /********** 第二步：等待录入指纹并保存用户 **********/
  if (g_runtime.mode == RUNTIME_MODE_ENROLL_WAIT_FINGER)
  {
    // 短按OK退出录入
    if (key_event == KEY_EVENT_OK_SHORT)
    {
      g_runtime.mode = RUNTIME_MODE_IDLE;
      RuntimeManager_ShowIdle();
      return;
    }

    // 读取系统参数，获取下一个可用用户ID
    param = StorageManager_GetParam();
    (void)param;
    next_finger_id = StorageManager_GetNextFreeUserId();
    if (next_finger_id == 0U)
    {
      AttendanceDisplayModelTypeDef display_full;
      memset(&display_full, 0, sizeof(display_full));
      display_full.page = OLED_PAGE_ENROLL;
      display_full.hold_ms = 2000U;
      snprintf(display_full.line1, sizeof(display_full.line1), "USER FULL");
      snprintf(display_full.line2, sizeof(display_full.line2), "NO FREE ID");
      RuntimeManager_SetDisplay(&display_full);
      DAC_Sound_Error();
      return;
    }

    // 卡号有效：等待检测到指纹模块上有手指再开始录入
    if (g_runtime.pending_card_valid != 0U)
    {
      /* 仅在指纹模块 IRQ 电平有效（检测到手指）时才触发录入流程，避免刚刷完卡立即尝试采集导致 NO_FINGER */
      uint8_t irq_active = ZW101_IrqIsActiveLevel();
      (void)ZW101_IrqConsumePending();

      if (irq_active == 0U)
      {
        return; /* 继续等待手指或长按退出 */
      }

      /* 稳定性确认：等待短延时并再次确认 IRQ 保持有效，减少误触发 */
      HAL_Delay(80U);
      if (ZW101_IrqIsActiveLevel() == 0U)
      {
        return;
      }

      COM_DEBUG("开始执行指纹录入，finger_id=%u", next_finger_id);
      ZW101_StatusTypeDef st = App_Zw101_EnrollUser(ZW101_DEFAULT_PASSWORD, next_finger_id);
      COM_DEBUG("指纹录入完成，状态=%d", st);
      
      if (st != ZW101_OK)
      {
        COM_DEBUG("指纹录入失败，状态=%d，跳过用户创建", st);
        /* enroll failed, handled below with UI */
      }
      else
      {
        COM_DEBUG("指纹录入成功，开始创建用户");
        if (StorageManager_CreateUser(g_runtime.pending_card_uid, next_finger_id, &user) == 0U)
        {
          COM_DEBUG("StorageManager_CreateUser failed for finger_id=%u", next_finger_id);
          st = ZW101_ERROR; // use st to indicate failure for common handling
        }
        else
        {
          COM_DEBUG("用户创建成功，user_id=%u", user.user_id);
        }
      }

      COM_DEBUG("最终状态检查: st=%d", st);
      if (st == ZW101_OK)
      {
      AttendanceEventTypeDef event;
      AttendanceUserTypeDef user_view;
      AttendanceDisplayModelTypeDef display;

      memset(&event, 0, sizeof(event));
      memset(&user_view, 0, sizeof(user_view));

      // 组装展示用户信息
      user_view.user_id = user.user_id;
      user_view.valid = user.valid;
      user_view.finger_id = user.finger_id;
      memcpy(user_view.name, user.name, sizeof(user_view.name));
      memcpy(user_view.employee_no, user.employee_no, sizeof(user_view.employee_no));

      // 补全时间
      if (Attendance_GetCurrentDateTime(&event.timestamp) != 0U)
      {
        event.result = ATTENDANCE_RESULT_ON_DUTY_OK;
      }

      // 录入成功提示界面
      display.page = OLED_PAGE_ENROLL;
      display.hold_ms = 3000U;
      snprintf(display.line1, sizeof(display.line1), "Enroll OK");
      snprintf(display.line2, sizeof(display.line2), "Name:%s", user_view.name);
      snprintf(display.line3, sizeof(display.line3), "No:%s", user_view.employee_no);
      snprintf(display.line4, sizeof(display.line4), "Bind done");
      RuntimeManager_SetDisplay(&display);
      // 播放录入成功语音
      DAC_Sound_Success();   // 录入成功用成功音
      
      COM_DEBUG("用户注册成功，准备发送到ESP");
      // 上报新增用户到 ESP/WiFi 模块，方便小程序同步人员信息
      {
        char payload[80];
        int len = snprintf(payload, sizeof(payload), "%lu|%s|%s|%02X%02X%02X%02X|%u",
                           (unsigned long)user.user_id,
                           user.employee_no,
                           user.name,
                           user.rc522_uid[0], user.rc522_uid[1], user.rc522_uid[2], user.rc522_uid[3],
                           (unsigned int)user.finger_id);
        
        // 确保字符串结束符存在，计算实际长度
        if(len >= (int)sizeof(payload)) {
          len = sizeof(payload) - 1;  // 确保不越界
          payload[len] = '\0';
        }
        
        // 再次计算有效长度（不包含末尾的null字符）
        len = strlen(payload);
        
        COM_DEBUG("构建用户注册消息: '%s', 长度: %d", payload, len);
        if (len > 0 && len < 250) {  // 使用合理上限
          COM_DEBUG("发送用户注册消息: '%s'", payload);
          SendFrameToESP(TYPE_USER_REGISTER, (uint8_t*)payload, (uint8_t)len);
        } else {
          COM_DEBUG("用户注册消息长度异常: %d，跳过发送", len);
        }
      }
      // 录入完成切回待机
      g_runtime.mode = RUNTIME_MODE_IDLE;
      g_runtime.pending_card_valid = 0U;
      osDelay(800U);
    }
    else
    {
      /* 录入失败提示 */
      AttendanceDisplayModelTypeDef display_fail;
      memset(&display_fail, 0, sizeof(display_fail));
      display_fail.page = OLED_PAGE_ENROLL;
      display_fail.hold_ms = 2000U;
      snprintf(display_fail.line1, sizeof(display_fail.line1), "ENROLL FAIL");
      snprintf(display_fail.line2, sizeof(display_fail.line2), "CODE:%d", (int)st);
      RuntimeManager_SetDisplay(&display_fail);
      DAC_Sound_Error();
      /* 保持在录入等待指纹态，允许重试或长按退出 */
      g_runtime.mode = RUNTIME_MODE_ENROLL_WAIT_FINGER;
      osDelay(800U);
    }
    return;
  }
}}

/**
 * @brief 获取当前界面快照，供外部OLED刷新调用
 * @param display 接收显示数据的指针
 */
void RuntimeManager_GetDisplaySnapshot(AttendanceDisplayModelTypeDef *display)
{
  if (display != NULL)
  {
    *display = g_runtime.display;
  }
}

/**
 * @brief 运行时管理器初始化
 * @note 系统上电初始化入口：存储、考勤、RTC、排班、状态清零
 */
void RuntimeManager_Init(void)
{
  StorageParamTypeDef param;

  // 全局运行时变量清零
  memset(&g_runtime, 0, sizeof(g_runtime));
  // 存储管理器初始化
  StorageManager_Init();
  // 读取系统配置参数
  param = StorageManager_GetParam();

  // 考勤基础模块初始化
  Attendance_Init();
  // 同步RTC有效状态
  Attendance_SetRtcValid(param.rtc_valid);
  g_runtime.last_rtc_valid = param.rtc_valid;

  // 解析存储的上下班时间，加载排班规则
  if (param.work_start_min < param.work_end_min)
  {
    char schedule_text[16];
    snprintf(schedule_text,
             sizeof(schedule_text),
             "%02u:%02u|%02u:%02u",
             param.work_start_min / 60U,
             param.work_start_min % 60U,
             param.work_end_min / 60U,
             param.work_end_min % 60U);
    (void)Attendance_SetScheduleFromString(schedule_text);
  }

  // 上电默认进入待机模式
  g_runtime.mode = RUNTIME_MODE_IDLE;
  // 刷新初始待机界面
  RuntimeManager_ShowIdle();
}

/**
 * @brief 业务逻辑主轮询任务
 * 1. 非待机模式：只处理录入逻辑
 * 2. 待机模式：长按OK进入录入 + 轮询刷卡指纹
 */
void RuntimeManager_CheckTaskStep(void)
{
  KeyEventTypeDef key_event = KeyInput_Scan();

  // 录入模式优先处理
  if (g_runtime.mode == RUNTIME_MODE_ENROLL_WAIT_CARD || g_runtime.mode == RUNTIME_MODE_ENROLL_WAIT_FINGER)
  {
    RuntimeManager_HandleEnrollMode(key_event);
    return;
  }

  // 用户列表模式优先处理
  if (g_runtime.mode == RUNTIME_MODE_USER_LIST)
  {
    RuntimeManager_HandleUserList(key_event);
    return;
  }
  if (g_runtime.mode == RUNTIME_MODE_USER_LIST_CONFIRM)
  {
    RuntimeManager_HandleUserListConfirm(key_event);
    return;
  }

  // 待机模式：短按OK进入用户列表，长按OK进入发卡+指纹录入流程
  if (key_event == KEY_EVENT_OK_SHORT)
  {
    /* 在用户列表模式内，短按 OK 弹出删除确认对话框 */
    if (g_runtime.mode == RUNTIME_MODE_USER_LIST)
    {
      /* 进入确认态，记录待删除索引，默认选中 No */
      g_runtime.user_list_pending_delete_index = g_runtime.user_list_index;
      g_runtime.user_list_confirm_choice = 0U; /* 0 = No, 1 = Yes */
      g_runtime.mode = RUNTIME_MODE_USER_LIST_CONFIRM;

      /* 显示确认对话 */
      StorageUserTypeDef user;
      if (StorageManager_GetUserByIndex(g_runtime.user_list_pending_delete_index, &user))
      {
        AttendanceDisplayModelTypeDef display;
        memset(&display, 0, sizeof(display));
        display.page = OLED_PAGE_IDLE;
        display.hold_ms = 0U;
        snprintf(display.line1, sizeof(display.line1), "Delete User?");
        snprintf(display.line2, sizeof(display.line2), "Name:%s", user.name);
        snprintf(display.line3, sizeof(display.line3), "No:%s", user.employee_no);
        snprintf(display.line4, sizeof(display.line4), "[No]   Yes");
        RuntimeManager_SetDisplay(&display);
      }
      return;
    }
    /* 进入用户列表 */
    g_runtime.user_list_count = StorageManager_GetUserCount();
    if (g_runtime.user_list_count == 0U)
    {
      /* 无用户 */
      AttendanceDisplayModelTypeDef display;
      memset(&display, 0, sizeof(display));
      display.page = OLED_PAGE_IDLE;
      display.hold_ms = 2000U;
      snprintf(display.line1, sizeof(display.line1), "No Users");
      RuntimeManager_SetDisplay(&display);
      return;
    }
    g_runtime.user_list_index = 0U;
    g_runtime.mode = RUNTIME_MODE_USER_LIST;
    /* 显示第一个用户 */
    {
      StorageUserTypeDef user;
      if (StorageManager_GetUserByIndex(g_runtime.user_list_index, &user))
      {
        AttendanceDisplayModelTypeDef display;
        memset(&display, 0, sizeof(display));
        display.page = OLED_PAGE_IDLE;
        display.hold_ms = 0U;
        snprintf(display.line1, sizeof(display.line1), "User %lu/%lu", (unsigned long)(g_runtime.user_list_index+1), (unsigned long)g_runtime.user_list_count);
        snprintf(display.line2, sizeof(display.line2), "Name:%s", user.name);
        snprintf(display.line3, sizeof(display.line3), "No:%s", user.employee_no);
        snprintf(display.line4, sizeof(display.line4), "UID:%02X%02X%02X%02X F:%u", user.rc522_uid[0], user.rc522_uid[1], user.rc522_uid[2], user.rc522_uid[3], (unsigned)user.finger_id);
        RuntimeManager_SetDisplay(&display);
      }
    }
    return;
  }

  if (key_event == KEY_EVENT_UP_SHORT)
  {
    g_runtime.mode = RUNTIME_MODE_ENROLL_WAIT_CARD;
    memset(g_runtime.pending_card_uid, 0, sizeof(g_runtime.pending_card_uid));
    g_runtime.pending_card_valid = 0U;
    g_runtime.display.page = OLED_PAGE_ENROLL;
    g_runtime.display.hold_ms = 0U;
    snprintf(g_runtime.display.line1, sizeof(g_runtime.display.line1), "Enroll Mode");
    snprintf(g_runtime.display.line2, sizeof(g_runtime.display.line2), "Step1 Swipe Card");
    snprintf(g_runtime.display.line3, sizeof(g_runtime.display.line3), "Then Finger");
    snprintf(g_runtime.display.line4, sizeof(g_runtime.display.line4), "Press OK:Exit");
    return;
  }

  // 短按DOWN键：重启ESP8266
  if (key_event == KEY_EVENT_DOWN_SHORT)
  {
    COM_DEBUG("待机模式下检测到DOWN按键，重启ESP8266");
    
    // 发送重启ESP8266命令
    SendFrameToESP(TYPE_RESTART_ESP, NULL, 0U);
    
    // 显示重启提示
    AttendanceDisplayModelTypeDef display;
    memset(&display, 0, sizeof(display));
    display.page = OLED_PAGE_IDLE;
    display.hold_ms = 2000U;
    snprintf(display.line1, sizeof(display.line1), "Restarting ESP...");
    snprintf(display.line2, sizeof(display.line2), "Please Wait");
    RuntimeManager_SetDisplay(&display);
    
    return;
  }
  
  // 待机常态：轮询刷卡 + 指纹检测
  RuntimeManager_PollCard();
  RuntimeManager_PollFinger();
}

/**
 * @brief 界面刷新管理任务
 * 1. 临时页面超时自动切回待机
 * 2. 定时打印OLED调试日志
 */
void RuntimeManager_DisplayTaskStep(void)
{
  uint32_t now_tick = HAL_GetTick();

  // 待机模式下：超时自动回归待机页
  if (g_runtime.mode == RUNTIME_MODE_IDLE &&
      g_runtime.display.hold_ms > 0U &&
      now_tick > g_runtime.display_expire_tick)
  {
    RuntimeManager_ShowIdle();
  }
  // 常驻待机页，持续刷新时间
  else if (g_runtime.mode == RUNTIME_MODE_IDLE && g_runtime.display.hold_ms == 0U)
  {
    RuntimeManager_ShowIdle();
  }

  // 周期打印调试信息
  if ((now_tick - g_runtime.last_idle_log_tick) >= RUNTIME_IDLE_LOG_PERIOD_MS)
  {
    g_runtime.last_idle_log_tick = now_tick;
    /* 保留最小调试输出：当前页码 */
    COM_DEBUG("OLED page=%d", g_runtime.display.page);
  }
}

/**
 * @brief 时间同步&参数保存定时任务
 * 1. 时钟异常自动请求网络校时
 * 2. 首次校时成功弹窗+语音提示
 * 3. 配置变动自动持久化保存到Flash
 */
void RuntimeManager_TimeSyncTaskStep(void)
{
  StorageParamTypeDef param = StorageManager_GetParam();
  AttendanceScheduleTypeDef schedule = Attendance_GetSchedule();
  uint8_t rtc_valid = Attendance_IsRtcValid();

  // 定时检测：RTC无效则主动发起时间同步请求
  if ((HAL_GetTick() - g_runtime.last_time_sync_tick) >= RUNTIME_TIME_SYNC_PERIOD_MS)
  {
    g_runtime.last_time_sync_tick = HAL_GetTick();
    if (rtc_valid == 0U)
    {
      Attendance_RequestTimeSync();
    }
  }

  // 从时间无效 → 时间有效：首次校时完成提示
  if (g_runtime.last_rtc_valid == 0U && rtc_valid != 0U)
  {
    AttendanceDisplayModelTypeDef display;
    memset(&display, 0, sizeof(display));
    display.page = OLED_PAGE_TIME_SYNC;
    display.hold_ms = 2000U;
    snprintf(display.line1, sizeof(display.line1), "TIME SYNC OK");
    snprintf(display.line2, sizeof(display.line2), "RTC Updated");
    RuntimeManager_SetDisplay(&display);
    DAC_Sound_Success();   // 时间同步成功也用成功音
  }
  // 更新上一轮时钟状态
  g_runtime.last_rtc_valid = rtc_valid;

  // 配置发生变化，自动保存参数到Flash
  if (param.rtc_valid != rtc_valid ||
      param.work_start_min != schedule.work_start_min ||
      param.work_end_min != schedule.work_end_min ||
      param.split_min != schedule.split_min)
  {
    param.rtc_valid = rtc_valid;
    param.work_start_min = schedule.work_start_min;
    param.work_end_min = schedule.work_end_min;
    param.split_min = schedule.split_min;
    (void)StorageManager_SaveParam(&param);
  }
}

/**
 * @brief 处理用户列表界面输入（上下翻页 + 长按退出）
 */
static void RuntimeManager_HandleUserList(KeyEventTypeDef key_event)
{
  if (key_event == KEY_EVENT_OK_LONG)
  {
    g_runtime.mode = RUNTIME_MODE_IDLE;
    RuntimeManager_ShowIdle();
    return;
  }

  /* 短按 OK 弹出删除确认对话框 */
  if (key_event == KEY_EVENT_OK_SHORT)
  {
    if (g_runtime.user_list_count == 0U)
    {
      return;
    }
    g_runtime.user_list_pending_delete_index = g_runtime.user_list_index;
    g_runtime.user_list_confirm_choice = 0U; /* 默认 No */
    g_runtime.mode = RUNTIME_MODE_USER_LIST_CONFIRM;

    StorageUserTypeDef user;
    if (StorageManager_GetUserByIndex(g_runtime.user_list_pending_delete_index, &user))
    {
      AttendanceDisplayModelTypeDef display;
      memset(&display, 0, sizeof(display));
      display.page = OLED_PAGE_IDLE;
      display.hold_ms = 0U;
      snprintf(display.line1, sizeof(display.line1), "Delete User?");
      snprintf(display.line2, sizeof(display.line2), "Name:%s", user.name);
      snprintf(display.line3, sizeof(display.line3), "No:%s", user.employee_no);
      snprintf(display.line4, sizeof(display.line4), "[No]   Yes");
      RuntimeManager_SetDisplay(&display);
    }
    return;
  }

  if (key_event == KEY_EVENT_UP_SHORT)
  {
    if (g_runtime.user_list_count == 0U) return;
    if (g_runtime.user_list_index == 0U)
    {
      g_runtime.user_list_index = g_runtime.user_list_count - 1U;
    }
    else
    {
      g_runtime.user_list_index--;
    }
  }
  else if (key_event == KEY_EVENT_DOWN_SHORT)
  {
    if (g_runtime.user_list_count == 0U) return;
    g_runtime.user_list_index = (g_runtime.user_list_index + 1U) % g_runtime.user_list_count;
  }

  /* 刷新显示为当前索引的用户 */
  {
    StorageUserTypeDef user;
    if (StorageManager_GetUserByIndex(g_runtime.user_list_index, &user))
    {
      AttendanceDisplayModelTypeDef display;
      memset(&display, 0, sizeof(display));
      display.page = OLED_PAGE_IDLE;
      display.hold_ms = 0U;
      snprintf(display.line1, sizeof(display.line1), "User %lu/%lu", (unsigned long)(g_runtime.user_list_index+1), (unsigned long)g_runtime.user_list_count);
      snprintf(display.line2, sizeof(display.line2), "Name:%s", user.name);
      snprintf(display.line3, sizeof(display.line3), "No:%s", user.employee_no);
      snprintf(display.line4, sizeof(display.line4), "UID:%02X%02X%02X%02X F:%u", user.rc522_uid[0], user.rc522_uid[1], user.rc522_uid[2], user.rc522_uid[3], (unsigned)user.finger_id);
      RuntimeManager_SetDisplay(&display);
    }
  }
}

/**
 * @brief 处理用户删除确认界面输入
 */
static void RuntimeManager_HandleUserListConfirm(KeyEventTypeDef key_event)
{
  /* 长按取消 */
  if (key_event == KEY_EVENT_OK_LONG)
  {
    g_runtime.mode = RUNTIME_MODE_USER_LIST;
    /* 恢复显示当前用户 */
    StorageUserTypeDef user;
    if (StorageManager_GetUserByIndex(g_runtime.user_list_index, &user))
    {
      AttendanceDisplayModelTypeDef display;
      memset(&display, 0, sizeof(display));
      display.page = OLED_PAGE_IDLE;
      display.hold_ms = 0U;
      snprintf(display.line1, sizeof(display.line1), "User %lu/%lu", (unsigned long)(g_runtime.user_list_index+1), (unsigned long)g_runtime.user_list_count);
      snprintf(display.line2, sizeof(display.line2), "Name:%s", user.name);
      snprintf(display.line3, sizeof(display.line3), "No:%s", user.employee_no);
      snprintf(display.line4, sizeof(display.line4), "UID:%02X%02X%02X%02X F:%u", user.rc522_uid[0], user.rc522_uid[1], user.rc522_uid[2], user.rc522_uid[3], (unsigned)user.finger_id);
      RuntimeManager_SetDisplay(&display);
    }
    return;
  }

  /* 切换选择 Yes/No */
  if (key_event == KEY_EVENT_UP_SHORT || key_event == KEY_EVENT_DOWN_SHORT)
  {
    g_runtime.user_list_confirm_choice = g_runtime.user_list_confirm_choice ? 0U : 1U;
  }

  /* 更新确认界面显示 */
  {
    StorageUserTypeDef user;
    if (StorageManager_GetUserByIndex(g_runtime.user_list_pending_delete_index, &user))
    {
      AttendanceDisplayModelTypeDef display;
      memset(&display, 0, sizeof(display));
      display.page = OLED_PAGE_IDLE;
      display.hold_ms = 0U;
      snprintf(display.line1, sizeof(display.line1), "Delete User?");
      snprintf(display.line2, sizeof(display.line2), "Name:%s", user.name);
      snprintf(display.line3, sizeof(display.line3), "No:%s", user.employee_no);
      if (g_runtime.user_list_confirm_choice == 0U)
      {
        snprintf(display.line4, sizeof(display.line4), "[No]   Yes");
      }
      else
      {
        snprintf(display.line4, sizeof(display.line4), "No   [Yes]");
      }
      RuntimeManager_SetDisplay(&display);
    }
  }

  /* 确认删除 */
  if (key_event == KEY_EVENT_OK_SHORT)
  {
    if (g_runtime.user_list_confirm_choice == 1U)
    {
      StorageUserTypeDef cur;
      if (StorageManager_GetUserByIndex(g_runtime.user_list_pending_delete_index, &cur))
      {
        (void)App_Zw101_DeleteUser(ZW101_DEFAULT_PASSWORD, cur.finger_id);
        if (StorageManager_DeleteUser(cur.user_id))
        {
          g_runtime.user_list_count = StorageManager_GetUserCount();
          if (g_runtime.user_list_count == 0U)
          {
            g_runtime.mode = RUNTIME_MODE_IDLE;
            RuntimeManager_ShowIdle();
            return;
          }
          /* 调整索引到有效范围 */
          if (g_runtime.user_list_index >= g_runtime.user_list_count)
          {
            g_runtime.user_list_index = g_runtime.user_list_count - 1U;
          }
        }
      }
    }

    /* 无论是否删除，返回用户列表页面并刷新 */
    g_runtime.mode = RUNTIME_MODE_USER_LIST;
    StorageUserTypeDef user2;
    if (StorageManager_GetUserByIndex(g_runtime.user_list_index, &user2))
    {
      AttendanceDisplayModelTypeDef display;
      memset(&display, 0, sizeof(display));
      display.page = OLED_PAGE_IDLE;
      display.hold_ms = 0U;
      snprintf(display.line1, sizeof(display.line1), "User %lu/%lu", (unsigned long)(g_runtime.user_list_index+1), (unsigned long)g_runtime.user_list_count);
      snprintf(display.line2, sizeof(display.line2), "Name:%s", user2.name);
      snprintf(display.line3, sizeof(display.line3), "No:%s", user2.employee_no);
      snprintf(display.line4, sizeof(display.line4), "UID:%02X%02X%02X%02X F:%u", user2.rc522_uid[0], user2.rc522_uid[1], user2.rc522_uid[2], user2.rc522_uid[3], (unsigned)user2.finger_id);
      RuntimeManager_SetDisplay(&display);
    }
  }
}

uint8_t RuntimeManager_RemoteCheckInByUserId(uint32_t user_id)
{
  StorageUserTypeDef user;
  AttendanceResultTypeDef result;
  AttendanceScheduleTypeDef schedule;

  if (StorageManager_FindUserById(user_id, &user) == 0U)
  {
    RuntimeManager_PublishEvent(NULL, ATTENDANCE_VERIFY_REMOTE, ATTENDANCE_RESULT_UNKNOWN_USER);
    COM_DEBUG("Remote check-in failed: user_id=%lu not found", (unsigned long)user_id);
    return 0U;
  }

  if (Attendance_GetCurrentDateTime(&g_runtime.now) == 0U)
  {
    RuntimeManager_PublishEvent(&user, ATTENDANCE_VERIFY_REMOTE, ATTENDANCE_RESULT_TIME_INVALID);
    COM_DEBUG("Remote check-in failed: RTC invalid");
    return 0U;
  }

  schedule = Attendance_GetSchedule();
  result = Attendance_JudgeEvent(&schedule, &g_runtime.daily_state, &g_runtime.now);
  RuntimeManager_PublishEvent(&user, ATTENDANCE_VERIFY_REMOTE, result);

  COM_DEBUG("Remote check-in done: user_id=%lu result=%u",
            (unsigned long)user_id,
            (unsigned)result);
  return 1U;
}