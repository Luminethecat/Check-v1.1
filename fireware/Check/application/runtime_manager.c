#include "runtime_manager.h"

#include "Com_debug.h"
#include "audio_prompt.h"
#include "app_services.h"
#include "cmsis_os.h"
#include "key_input.h"
#include "storage_manager.h"
#include "string.h"

/* runtime_manager 是考勤机的运行时状态机：
 * 管待机、录入、打卡判定、页面切换、语音触发和时间同步反馈。 */

#define ZW101_DEFAULT_PASSWORD            0U
#define RUNTIME_IDLE_LOG_PERIOD_MS        1000U
#define RUNTIME_RESULT_HOLD_MS            3000U
#define RUNTIME_TIME_SYNC_PERIOD_MS       10000U
#define RUNTIME_FINGER_SCAN_PERIOD_MS     1500U
#define RUNTIME_FINGER_IRQ_GRACE_MS       2500U

typedef enum
{
  RUNTIME_MODE_IDLE = 0,
  RUNTIME_MODE_ENROLL_WAIT_CARD,
  RUNTIME_MODE_ENROLL_WAIT_FINGER,
} RuntimeModeTypeDef;

typedef struct
{
  RuntimeModeTypeDef mode;
  AttendanceDailyStateTypeDef daily_state;
  AttendanceDisplayModelTypeDef display;
  AttendanceDateTimeTypeDef now;
  uint16_t today_count;
  uint32_t display_expire_tick;
  uint32_t last_idle_log_tick;
  uint32_t last_time_sync_tick;
  uint32_t last_finger_scan_tick;
  uint8_t last_rtc_valid;
  uint8_t pending_card_uid[4];
  uint8_t pending_card_valid;
} RuntimeContextTypeDef;

static RuntimeContextTypeDef g_runtime;

static uint8_t RuntimeManager_ResultPromptIndex(AttendanceResultTypeDef result)
{
  switch (result)
  {
    case ATTENDANCE_RESULT_ON_DUTY_OK:
    case ATTENDANCE_RESULT_OFF_DUTY_OK:
      return AUDIO_INDEX_CHECK_OK;

    case ATTENDANCE_RESULT_LATE:
      return AUDIO_INDEX_LATE;

    case ATTENDANCE_RESULT_EARLY:
      return AUDIO_INDEX_EARLY;

    default:
      return AUDIO_INDEX_CHECK_FAIL;
  }
}

static void RuntimeManager_SetDisplay(const AttendanceDisplayModelTypeDef *display)
{
  if (display == NULL)
  {
    return;
  }

  g_runtime.display = *display;
  /* 非常驻页面通过超时自动回到待机页。 */
  g_runtime.display_expire_tick = HAL_GetTick() + ((display->hold_ms > 0U) ? display->hold_ms : RUNTIME_RESULT_HOLD_MS);
}

static void RuntimeManager_ShowIdle(void)
{
  if (Attendance_GetCurrentDateTime(&g_runtime.now) == 0U)
  {
    return;
  }

  if (g_runtime.daily_state.ymd != 0U)
  {
    uint16_t current_ymd = (uint16_t)(((g_runtime.now.month & 0x0FU) << 12U) |
                                      ((g_runtime.now.day & 0x1FU) << 7U) |
                                      (g_runtime.now.year & 0x7FU));
    if (current_ymd != g_runtime.daily_state.ymd)
    {
      /* 跨天后清掉“今日统计”和当日重复打卡状态。 */
      g_runtime.today_count = 0U;
      g_runtime.daily_state.has_on_duty = 0U;
      g_runtime.daily_state.has_off_duty = 0U;
      g_runtime.daily_state.ymd = current_ymd;
    }
  }

  Attendance_BuildIdleDisplay(&g_runtime.display, &g_runtime.now, g_runtime.today_count, 1U);
  g_runtime.display.hold_ms = 0U;
}

static void RuntimeManager_PublishEvent(const StorageUserTypeDef *storage_user,
                                        AttendanceVerifyTypeDef verify_type,
                                        AttendanceResultTypeDef result)
{
  AttendanceEventTypeDef event;
  AttendanceUserTypeDef user_view;
  AttendanceDisplayModelTypeDef display;
  StorageRecordTypeDef record;

  memset(&event, 0, sizeof(event));
  memset(&user_view, 0, sizeof(user_view));
  memset(&record, 0, sizeof(record));

  if (Attendance_GetCurrentDateTime(&event.timestamp) == 0U)
  {
    return;
  }

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

  event.verify_type = verify_type;
  event.result = result;

  if (result == ATTENDANCE_RESULT_ON_DUTY_OK ||
      result == ATTENDANCE_RESULT_OFF_DUTY_OK ||
      result == ATTENDANCE_RESULT_LATE ||
      result == ATTENDANCE_RESULT_EARLY)
  {
    /* 只有有效考勤事件才写本地记录并上传 ESP。 */
    Attendance_UpdateDailyState(&g_runtime.daily_state, &event.timestamp, result);
    g_runtime.today_count++;

    record.user_id = event.user_id;
    record.year = event.timestamp.year;
    record.month = event.timestamp.month;
    record.day = event.timestamp.day;
    record.hour = event.timestamp.hour;
    record.minute = event.timestamp.minute;
    record.second = event.timestamp.second;
    record.verify_type = (uint8_t)verify_type;
    record.result_type = (uint8_t)result;
    record.upload_flag = 0U;
    (void)StorageManager_AppendRecord(&record, NULL);
    (void)Attendance_SendEventToEsp(&event);
  }

  Attendance_BuildResultDisplay(&display, (storage_user != NULL) ? &user_view : NULL, &event);
  RuntimeManager_SetDisplay(&display);
  (void)AudioPrompt_Play(RuntimeManager_ResultPromptIndex(result));
}

static void RuntimeManager_HandleCardCheckIn(const uint8_t uid[4])
{
  StorageUserTypeDef user;
  AttendanceResultTypeDef result;
  AttendanceScheduleTypeDef schedule;

  if (StorageManager_FindUserByCard(uid, &user) == 0U)
  {
    RuntimeManager_PublishEvent(NULL, ATTENDANCE_VERIFY_CARD, ATTENDANCE_RESULT_UNKNOWN_USER);
    return;
  }

  if (Attendance_GetCurrentDateTime(&g_runtime.now) == 0U)
  {
    RuntimeManager_PublishEvent(&user, ATTENDANCE_VERIFY_CARD, ATTENDANCE_RESULT_TIME_INVALID);
    return;
  }

  schedule = Attendance_GetSchedule();
  result = Attendance_JudgeEvent(&schedule, &g_runtime.daily_state, &g_runtime.now);
  RuntimeManager_PublishEvent(&user, ATTENDANCE_VERIFY_CARD, result);
}

static void RuntimeManager_HandleFingerCheckIn(uint16_t finger_id)
{
  StorageUserTypeDef user;
  AttendanceResultTypeDef result;
  AttendanceScheduleTypeDef schedule;

  if (StorageManager_FindUserByFinger(finger_id, &user) == 0U)
  {
    RuntimeManager_PublishEvent(NULL, ATTENDANCE_VERIFY_FINGER, ATTENDANCE_RESULT_UNKNOWN_USER);
    return;
  }

  if (Attendance_GetCurrentDateTime(&g_runtime.now) == 0U)
  {
    RuntimeManager_PublishEvent(&user, ATTENDANCE_VERIFY_FINGER, ATTENDANCE_RESULT_TIME_INVALID);
    return;
  }

  schedule = Attendance_GetSchedule();
  result = Attendance_JudgeEvent(&schedule, &g_runtime.daily_state, &g_runtime.now);
  RuntimeManager_PublishEvent(&user, ATTENDANCE_VERIFY_FINGER, result);
}

static void RuntimeManager_PollCard(void)
{
  RC522_CardInfoTypeDef card;

  if (RC522_ReadCard(&card) == RC522_OK && card.uid_len >= 4U)
  {
    RuntimeManager_HandleCardCheckIn(card.uid);
    RC522_Halt();
    osDelay(800U);
  }
}

static void RuntimeManager_PollFinger(void)
{
  ZW101_SearchResultTypeDef result;
  uint32_t now_tick = HAL_GetTick();
  uint8_t irq_pending = ZW101_IrqConsumePending();
  uint8_t irq_active = ZW101_IrqIsActiveLevel();

  /* IRQ 触发后优先识别；若中断丢失，再用低频轮询兜底。 */
  if (irq_pending == 0U &&
      irq_active == 0U &&
      (now_tick - g_runtime.last_finger_scan_tick) < RUNTIME_FINGER_SCAN_PERIOD_MS)
  {
    return;
  }

  if (irq_pending == 0U &&
      irq_active != 0U &&
      (now_tick - g_runtime.last_finger_scan_tick) < RUNTIME_FINGER_IRQ_GRACE_MS)
  {
    return;
  }

  g_runtime.last_finger_scan_tick = now_tick;
  if (App_Zw101_IdentifyUser(ZW101_DEFAULT_PASSWORD, &result) == ZW101_OK)
  {
    RuntimeManager_HandleFingerCheckIn(result.page_id);
    osDelay(800U);
  }
}

static void RuntimeManager_HandleEnrollMode(KeyEventTypeDef key_event)
{
  RC522_CardInfoTypeDef card;
  StorageUserTypeDef user;
  StorageParamTypeDef param;
  uint16_t next_finger_id;

  if (g_runtime.mode == RUNTIME_MODE_ENROLL_WAIT_CARD)
  {
    if (key_event == KEY_EVENT_OK_LONG)
    {
      g_runtime.mode = RUNTIME_MODE_IDLE;
      RuntimeManager_ShowIdle();
      return;
    }

    if (RC522_ReadCard(&card) == RC522_OK && card.uid_len >= 4U)
    {
      /* 录入模式先采卡 UID，再进入指纹录入。 */
      memcpy(g_runtime.pending_card_uid, card.uid, 4U);
      g_runtime.pending_card_valid = 1U;
      g_runtime.mode = RUNTIME_MODE_ENROLL_WAIT_FINGER;
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

  if (g_runtime.mode == RUNTIME_MODE_ENROLL_WAIT_FINGER)
  {
    if (key_event == KEY_EVENT_OK_LONG)
    {
      g_runtime.mode = RUNTIME_MODE_IDLE;
      RuntimeManager_ShowIdle();
      return;
    }

    param = StorageManager_GetParam();
    next_finger_id = (uint16_t)param.next_user_id;
    if (g_runtime.pending_card_valid != 0U &&
        App_Zw101_EnrollUser(ZW101_DEFAULT_PASSWORD, next_finger_id) == ZW101_OK &&
        StorageManager_CreateUser(g_runtime.pending_card_uid, next_finger_id, &user) != 0U)
    {
      /* 当前策略：指纹模板号直接跟随 user_id，便于查找和维护。 */
      AttendanceEventTypeDef event;
      AttendanceUserTypeDef user_view;
      AttendanceDisplayModelTypeDef display;

      memset(&event, 0, sizeof(event));
      memset(&user_view, 0, sizeof(user_view));
      user_view.user_id = user.user_id;
      user_view.valid = user.valid;
      user_view.finger_id = user.finger_id;
      memcpy(user_view.name, user.name, sizeof(user_view.name));
      memcpy(user_view.employee_no, user.employee_no, sizeof(user_view.employee_no));

      if (Attendance_GetCurrentDateTime(&event.timestamp) != 0U)
      {
        event.result = ATTENDANCE_RESULT_ON_DUTY_OK;
      }
      display.page = OLED_PAGE_ENROLL;
      display.hold_ms = 3000U;
      snprintf(display.line1, sizeof(display.line1), "Enroll OK");
      snprintf(display.line2, sizeof(display.line2), "Name:%s", user_view.name);
      snprintf(display.line3, sizeof(display.line3), "No:%s", user_view.employee_no);
      snprintf(display.line4, sizeof(display.line4), "Bind done");
      RuntimeManager_SetDisplay(&display);
      (void)AudioPrompt_Play(AUDIO_INDEX_ENROLL_OK);
      g_runtime.mode = RUNTIME_MODE_IDLE;
      g_runtime.pending_card_valid = 0U;
      osDelay(800U);
    }
    return;
  }
}

void RuntimeManager_GetDisplaySnapshot(AttendanceDisplayModelTypeDef *display)
{
  if (display != NULL)
  {
    *display = g_runtime.display;
  }
}

void RuntimeManager_Init(void)
{
  StorageParamTypeDef param;

  memset(&g_runtime, 0, sizeof(g_runtime));
  StorageManager_Init();
  param = StorageManager_GetParam();

  Attendance_Init();
  Attendance_SetRtcValid(param.rtc_valid);
  g_runtime.last_rtc_valid = param.rtc_valid;
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

  g_runtime.mode = RUNTIME_MODE_IDLE;
  RuntimeManager_ShowIdle();
}

void RuntimeManager_CheckTaskStep(void)
{
  KeyEventTypeDef key_event = KeyInput_Scan();

  if (g_runtime.mode != RUNTIME_MODE_IDLE)
  {
    RuntimeManager_HandleEnrollMode(key_event);
    return;
  }

  if (key_event == KEY_EVENT_OK_LONG)
  {
    /* 长按 OK 进入录入模式，适合无菜单键的小设备。 */
    g_runtime.mode = RUNTIME_MODE_ENROLL_WAIT_CARD;
    memset(g_runtime.pending_card_uid, 0, sizeof(g_runtime.pending_card_uid));
    g_runtime.pending_card_valid = 0U;
    g_runtime.display.page = OLED_PAGE_ENROLL;
    g_runtime.display.hold_ms = 0U;
    snprintf(g_runtime.display.line1, sizeof(g_runtime.display.line1), "Enroll Mode");
    snprintf(g_runtime.display.line2, sizeof(g_runtime.display.line2), "Step1 Swipe Card");
    snprintf(g_runtime.display.line3, sizeof(g_runtime.display.line3), "Then Finger");
    snprintf(g_runtime.display.line4, sizeof(g_runtime.display.line4), "Hold OK to Exit");
    return;
  }

  RuntimeManager_PollCard();
  RuntimeManager_PollFinger();
}

void RuntimeManager_DisplayTaskStep(void)
{
  uint32_t now_tick = HAL_GetTick();

  if (g_runtime.mode == RUNTIME_MODE_IDLE &&
      g_runtime.display.hold_ms > 0U &&
      now_tick > g_runtime.display_expire_tick)
  {
    RuntimeManager_ShowIdle();
  }
  else if (g_runtime.mode == RUNTIME_MODE_IDLE && g_runtime.display.hold_ms == 0U)
  {
    RuntimeManager_ShowIdle();
  }

  if ((now_tick - g_runtime.last_idle_log_tick) >= RUNTIME_IDLE_LOG_PERIOD_MS)
  {
    g_runtime.last_idle_log_tick = now_tick;
    COM_DEBUG("OLED[%d]: %s | %s | %s | %s",
              g_runtime.display.page,
              g_runtime.display.line1,
              g_runtime.display.line2,
              g_runtime.display.line3,
              g_runtime.display.line4);
  }
}

void RuntimeManager_TimeSyncTaskStep(void)
{
  StorageParamTypeDef param = StorageManager_GetParam();
  AttendanceScheduleTypeDef schedule = Attendance_GetSchedule();
  uint8_t rtc_valid = Attendance_IsRtcValid();

  if ((HAL_GetTick() - g_runtime.last_time_sync_tick) >= RUNTIME_TIME_SYNC_PERIOD_MS)
  {
    g_runtime.last_time_sync_tick = HAL_GetTick();
    if (rtc_valid == 0U)
    {
      Attendance_RequestTimeSync();
    }
  }

  if (g_runtime.last_rtc_valid == 0U && rtc_valid != 0U)
  {
    AttendanceDisplayModelTypeDef display;

    /* 首次完成校时后给出一次显式提示，并播放同步成功音。 */
    memset(&display, 0, sizeof(display));
    display.page = OLED_PAGE_TIME_SYNC;
    display.hold_ms = 2000U;
    snprintf(display.line1, sizeof(display.line1), "TIME SYNC OK");
    snprintf(display.line2, sizeof(display.line2), "RTC Updated");
    RuntimeManager_SetDisplay(&display);
    (void)AudioPrompt_Play(AUDIO_INDEX_TIME_SYNC_OK);
  }
  g_runtime.last_rtc_valid = rtc_valid;

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
