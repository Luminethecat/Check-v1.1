#include "attendance_app.h"

#include "Com_protocol.h"
#include "rtc.h"
#include "stdio.h"
#include "string.h"

static AttendanceScheduleTypeDef g_schedule = {540U, 1080U, 900U};
static uint8_t g_rtc_valid = 0U;

static uint16_t Attendance_ParseHHMM(const char *text)
{
  int hour;
  int minute;

  if (text == NULL || sscanf(text, "%d:%d", &hour, &minute) != 2)
  {
    return 0xFFFFU;
  }

  if (hour < 0 || hour > 23 || minute < 0 || minute > 59)
  {
    return 0xFFFFU;
  }

  return (uint16_t)((hour * 60) + minute);
}

static uint8_t Attendance_ResultToEspType(AttendanceResultTypeDef result)
{
  switch (result)
  {
    case ATTENDANCE_RESULT_ON_DUTY_OK:
      return 0x01U;

    case ATTENDANCE_RESULT_OFF_DUTY_OK:
      return 0x02U;

    case ATTENDANCE_RESULT_LATE:
      return 0x03U;

    case ATTENDANCE_RESULT_EARLY:
      return 0x04U;

    default:
      return 0x00U;
  }
}

static const char *Attendance_ResultText(AttendanceResultTypeDef result)
{
  switch (result)
  {
    case ATTENDANCE_RESULT_ON_DUTY_OK:
      return "ON DUTY OK";

    case ATTENDANCE_RESULT_OFF_DUTY_OK:
      return "OFF DUTY OK";

    case ATTENDANCE_RESULT_LATE:
      return "LATE";

    case ATTENDANCE_RESULT_EARLY:
      return "EARLY LEAVE";

    case ATTENDANCE_RESULT_REPEAT_ON_DUTY:
      return "ON DUTY DONE";

    case ATTENDANCE_RESULT_REPEAT_OFF_DUTY:
      return "OFF DUTY DONE";

    case ATTENDANCE_RESULT_UNKNOWN_USER:
      return "UNKNOWN USER";

    case ATTENDANCE_RESULT_TIME_INVALID:
      return "WAIT SYNC";

    default:
      return "UNKNOWN";
  }
}

static uint8_t Attendance_ToBcdDigit(uint16_t value)
{
  return (uint8_t)((value % 10U));
}

static void Attendance_FormatEspTime(const AttendanceDateTimeTypeDef *timestamp,
                                     uint8_t out_time[20])
{
  uint16_t year;
  uint8_t month;
  uint8_t day;
  uint8_t hour;
  uint8_t minute;
  uint8_t second;

  if (timestamp == NULL || out_time == NULL)
  {
    return;
  }

  /* 这里固定按 "YYYY-MM-DD HH:MM:SS" 共 19 个字符编码，
   * 避免 snprintf 因整数理论范围触发 format-truncation 告警。 */
  year = (timestamp->year > 9999U) ? 9999U : timestamp->year;
  month = (timestamp->month > 99U) ? 99U : timestamp->month;
  day = (timestamp->day > 99U) ? 99U : timestamp->day;
  hour = (timestamp->hour > 99U) ? 99U : timestamp->hour;
  minute = (timestamp->minute > 99U) ? 99U : timestamp->minute;
  second = (timestamp->second > 99U) ? 99U : timestamp->second;

  out_time[0] = (uint8_t)('0' + ((year / 1000U) % 10U));
  out_time[1] = (uint8_t)('0' + ((year / 100U) % 10U));
  out_time[2] = (uint8_t)('0' + ((year / 10U) % 10U));
  out_time[3] = Attendance_ToBcdDigit(year);
  out_time[4] = '-';
  out_time[5] = (uint8_t)('0' + ((month / 10U) % 10U));
  out_time[6] = Attendance_ToBcdDigit(month);
  out_time[7] = '-';
  out_time[8] = (uint8_t)('0' + ((day / 10U) % 10U));
  out_time[9] = Attendance_ToBcdDigit(day);
  out_time[10] = ' ';
  out_time[11] = (uint8_t)('0' + ((hour / 10U) % 10U));
  out_time[12] = Attendance_ToBcdDigit(hour);
  out_time[13] = ':';
  out_time[14] = (uint8_t)('0' + ((minute / 10U) % 10U));
  out_time[15] = Attendance_ToBcdDigit(minute);
  out_time[16] = ':';
  out_time[17] = (uint8_t)('0' + ((second / 10U) % 10U));
  out_time[18] = Attendance_ToBcdDigit(second);
  out_time[19] = '\0';
}

void Attendance_Init(void)
{
  Attendance_SetDefaultSchedule();
  g_rtc_valid = 0U;
}

void Attendance_RequestTimeSync(void)
{
  SendFrameToESP(TYPE_TIME_REQ, NULL, 0U);
}

void Attendance_SetRtcValid(uint8_t valid)
{
  g_rtc_valid = valid;
}

uint8_t Attendance_IsRtcValid(void)
{
  return g_rtc_valid;
}

AttendanceScheduleTypeDef Attendance_GetSchedule(void)
{
  return g_schedule;
}

uint8_t Attendance_SetScheduleFromString(const char *work_time_payload)
{
  char temp[32];
  char *split;
  uint16_t start_min;
  uint16_t end_min;

  if (work_time_payload == NULL || strlen(work_time_payload) >= sizeof(temp))
  {
    return 0U;
  }

  memset(temp, 0, sizeof(temp));
  strcpy(temp, work_time_payload);
  split = strchr(temp, '|');
  if (split == NULL)
  {
    return 0U;
  }

  *split = '\0';
  start_min = Attendance_ParseHHMM(temp);
  end_min = Attendance_ParseHHMM(split + 1);
  if (start_min == 0xFFFFU || end_min == 0xFFFFU || start_min >= end_min)
  {
    return 0U;
  }

  g_schedule.work_start_min = start_min;
  g_schedule.work_end_min = end_min;
  g_schedule.split_min = (uint16_t)((start_min + end_min) / 2U);
  return 1U;
}

void Attendance_SetDefaultSchedule(void)
{
  g_schedule.work_start_min = 9U * 60U;
  g_schedule.work_end_min = 18U * 60U;
  g_schedule.split_min = 15U * 60U;
}

uint8_t Attendance_GetCurrentDateTime(AttendanceDateTimeTypeDef *now)
{
  RTC_TimeTypeDef rtc_time;
  RTC_DateTypeDef rtc_date;

  if (now == NULL)
  {
    return 0U;
  }

  if (HAL_RTC_GetTime(&hrtc, &rtc_time, RTC_FORMAT_BIN) != HAL_OK)
  {
    return 0U;
  }
  if (HAL_RTC_GetDate(&hrtc, &rtc_date, RTC_FORMAT_BIN) != HAL_OK)
  {
    return 0U;
  }

  now->year = (uint16_t)(2000U + rtc_date.Year);
  now->month = rtc_date.Month;
  now->day = rtc_date.Date;
  now->hour = rtc_time.Hours;
  now->minute = rtc_time.Minutes;
  now->second = rtc_time.Seconds;
  now->weekday = rtc_date.WeekDay;
  return 1U;
}

uint16_t Attendance_GetMinutesOfDay(const AttendanceDateTimeTypeDef *now)
{
  if (now == NULL)
  {
    return 0U;
  }

  return (uint16_t)((now->hour * 60U) + now->minute);
}

AttendanceResultTypeDef Attendance_JudgeEvent(const AttendanceScheduleTypeDef *schedule,
                                              const AttendanceDailyStateTypeDef *daily_state,
                                              const AttendanceDateTimeTypeDef *now)
{
  uint16_t current_min;
  uint16_t ymd;

  if (schedule == NULL || now == NULL || Attendance_IsRtcValid() == 0U)
  {
    return ATTENDANCE_RESULT_TIME_INVALID;
  }

  current_min = Attendance_GetMinutesOfDay(now);
  ymd = (uint16_t)(((now->month & 0x0FU) << 12U) | ((now->day & 0x1FU) << 7U) | (now->year & 0x7FU));

  if (daily_state != NULL && daily_state->ymd == ymd)
  {
    if (current_min < schedule->split_min)
    {
      if (daily_state->has_on_duty != 0U)
      {
        return ATTENDANCE_RESULT_REPEAT_ON_DUTY;
      }
    }
    else if (daily_state->has_off_duty != 0U)
    {
      return ATTENDANCE_RESULT_REPEAT_OFF_DUTY;
    }
  }

  if (current_min < schedule->split_min)
  {
    return (current_min <= schedule->work_start_min) ? ATTENDANCE_RESULT_ON_DUTY_OK : ATTENDANCE_RESULT_LATE;
  }

  return (current_min >= schedule->work_end_min) ? ATTENDANCE_RESULT_OFF_DUTY_OK : ATTENDANCE_RESULT_EARLY;
}

void Attendance_UpdateDailyState(AttendanceDailyStateTypeDef *daily_state,
                                 const AttendanceDateTimeTypeDef *now,
                                 AttendanceResultTypeDef result)
{
  if (daily_state == NULL || now == NULL)
  {
    return;
  }

  daily_state->ymd = (uint16_t)(((now->month & 0x0FU) << 12U) | ((now->day & 0x1FU) << 7U) | (now->year & 0x7FU));

  if (result == ATTENDANCE_RESULT_ON_DUTY_OK || result == ATTENDANCE_RESULT_LATE)
  {
    daily_state->has_on_duty = 1U;
  }
  else if (result == ATTENDANCE_RESULT_OFF_DUTY_OK || result == ATTENDANCE_RESULT_EARLY)
  {
    daily_state->has_off_duty = 1U;
  }
}

uint8_t Attendance_BuildEspCheckData(const AttendanceEventTypeDef *event,
                                     uint8_t payload[ATTENDANCE_ESP_PAYLOAD_LEN])
{
  if (event == NULL || payload == NULL)
  {
    return 0U;
  }

  payload[0] = (uint8_t)(event->user_id >> 24U);
  payload[1] = (uint8_t)(event->user_id >> 16U);
  payload[2] = (uint8_t)(event->user_id >> 8U);
  payload[3] = (uint8_t)(event->user_id);

  Attendance_FormatEspTime(&event->timestamp, &payload[4]);

  payload[24] = Attendance_ResultToEspType(event->result);
  return 1U;
}

uint8_t Attendance_SendEventToEsp(const AttendanceEventTypeDef *event)
{
  uint8_t payload[ATTENDANCE_ESP_PAYLOAD_LEN];

  if (Attendance_BuildEspCheckData(event, payload) == 0U)
  {
    return 0U;
  }

  SendFrameToESP(TYPE_CHECK_DATA, payload, ATTENDANCE_ESP_PAYLOAD_LEN);
  return 1U;
}

void Attendance_BuildIdleDisplay(AttendanceDisplayModelTypeDef *display,
                                 const AttendanceDateTimeTypeDef *now,
                                 uint16_t today_count,
                                 uint8_t wifi_online)
{
  uint16_t current_min;
  const char *period_text;

  if (display == NULL || now == NULL)
  {
    return;
  }

  current_min = Attendance_GetMinutesOfDay(now);
  period_text = (current_min < g_schedule.split_min) ? "ON DUTY" : "OFF DUTY";

  memset(display, 0, sizeof(*display));
  display->page = OLED_PAGE_IDLE;
  snprintf(display->line1, sizeof(display->line1), "%04u-%02u-%02u %02u:%02u",
           now->year, now->month, now->day, now->hour, now->minute);
  snprintf(display->line2, sizeof(display->line2), "%s", period_text);
  snprintf(display->line3, sizeof(display->line3), "WiFi:%s RTC:%s",
           (wifi_online != 0U) ? "OK" : "OFF",
           (Attendance_IsRtcValid() != 0U) ? "OK" : "WAIT");
  snprintf(display->line4, sizeof(display->line4), "TODAY:%u", today_count);
}

void Attendance_BuildResultDisplay(AttendanceDisplayModelTypeDef *display,
                                   const AttendanceUserTypeDef *user,
                                   const AttendanceEventTypeDef *event)
{
  if (display == NULL || event == NULL)
  {
    return;
  }

  memset(display, 0, sizeof(*display));
  display->hold_ms = 3000U;

  if (event->result == ATTENDANCE_RESULT_UNKNOWN_USER ||
      event->result == ATTENDANCE_RESULT_TIME_INVALID)
  {
    display->page = OLED_PAGE_CHECK_FAIL;
    snprintf(display->line1, sizeof(display->line1), "CHECK FAIL");
    snprintf(display->line2, sizeof(display->line2), "%s", Attendance_ResultText(event->result));
    snprintf(display->line3, sizeof(display->line3), "ASK ADMIN");
    return;
  }

  display->page = OLED_PAGE_CHECK_OK;
  snprintf(display->line1, sizeof(display->line1), "%s", Attendance_ResultText(event->result));
  snprintf(display->line2, sizeof(display->line2), "NAME:%s", (user != NULL) ? user->name : "--");
  snprintf(display->line3, sizeof(display->line3), "NO:%s", (user != NULL) ? user->employee_no : "--");
  snprintf(display->line4, sizeof(display->line4), "%02u:%02u:%02u",
           event->timestamp.hour, event->timestamp.minute, event->timestamp.second);
}
