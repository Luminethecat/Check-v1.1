#ifndef ATTENDANCE_APP_H
#define ATTENDANCE_APP_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f1xx_hal.h"
#include <stdint.h>

#define ATTENDANCE_NAME_LEN            16U
#define ATTENDANCE_EMP_NO_LEN          12U
#define ATTENDANCE_ESP_PAYLOAD_LEN     25U

typedef enum
{
  ATTENDANCE_VERIFY_CARD = 0x01U,
  ATTENDANCE_VERIFY_FINGER = 0x02U,
  ATTENDANCE_VERIFY_REMOTE = 0x03U,
} AttendanceVerifyTypeDef;

typedef enum
{
  ATTENDANCE_RESULT_UNKNOWN = 0x00U,
  ATTENDANCE_RESULT_ON_DUTY_OK = 0x01U,
  ATTENDANCE_RESULT_OFF_DUTY_OK = 0x02U,
  ATTENDANCE_RESULT_LATE = 0x03U,
  ATTENDANCE_RESULT_EARLY = 0x04U,
  ATTENDANCE_RESULT_REPEAT_ON_DUTY = 0x11U,
  ATTENDANCE_RESULT_REPEAT_OFF_DUTY = 0x12U,
  ATTENDANCE_RESULT_UNKNOWN_USER = 0x13U,
  ATTENDANCE_RESULT_TIME_INVALID = 0x14U,
} AttendanceResultTypeDef;

typedef enum
{
  OLED_PAGE_IDLE = 0,
  OLED_PAGE_IDLE_STATS,
  OLED_PAGE_CHECK_OK,
  OLED_PAGE_CHECK_FAIL,
  OLED_PAGE_ENROLL,
  OLED_PAGE_TIME_SYNC,
} OledPageTypeDef;

typedef struct
{
  uint16_t year;
  uint8_t month;
  uint8_t day;
  uint8_t hour;
  uint8_t minute;
  uint8_t second;
  uint8_t weekday;
} AttendanceDateTimeTypeDef;

typedef struct
{
  uint16_t work_start_min;
  uint16_t work_end_min;
  uint16_t split_min;
} AttendanceScheduleTypeDef;

typedef struct
{
  uint32_t user_id;
  char employee_no[ATTENDANCE_EMP_NO_LEN];
  char name[ATTENDANCE_NAME_LEN];
  uint8_t rc522_uid[4];
  uint16_t finger_id;
  uint8_t valid;
} AttendanceUserTypeDef;

typedef struct
{
  uint16_t ymd;
  uint8_t has_on_duty;
  uint8_t has_off_duty;
} AttendanceDailyStateTypeDef;

typedef struct
{
  uint32_t user_id;
  AttendanceVerifyTypeDef verify_type;
  AttendanceResultTypeDef result;
  AttendanceDateTimeTypeDef timestamp;
} AttendanceEventTypeDef;

typedef struct
{
  OledPageTypeDef page;
  char line1[22];
  char line2[22];
  char line3[22];
  char line4[22];
  uint32_t hold_ms;
} AttendanceDisplayModelTypeDef;

void Attendance_Init(void);
void Attendance_RequestTimeSync(void);
void Attendance_SetRtcValid(uint8_t valid);
uint8_t Attendance_IsRtcValid(void);

AttendanceScheduleTypeDef Attendance_GetSchedule(void);
uint8_t Attendance_SetScheduleFromString(const char *work_time_payload);
void Attendance_SetDefaultSchedule(void);

uint8_t Attendance_GetCurrentDateTime(AttendanceDateTimeTypeDef *now);
uint16_t Attendance_GetMinutesOfDay(const AttendanceDateTimeTypeDef *now);
AttendanceResultTypeDef Attendance_JudgeEvent(const AttendanceScheduleTypeDef *schedule,
                                              const AttendanceDailyStateTypeDef *daily_state,
                                              const AttendanceDateTimeTypeDef *now);
void Attendance_UpdateDailyState(AttendanceDailyStateTypeDef *daily_state,
                                 const AttendanceDateTimeTypeDef *now,
                                 AttendanceResultTypeDef result);
uint8_t Attendance_BuildEspCheckData(const AttendanceEventTypeDef *event,
                                     uint8_t payload[ATTENDANCE_ESP_PAYLOAD_LEN]);
uint8_t Attendance_SendEventToEsp(const AttendanceEventTypeDef *event);

void Attendance_BuildIdleDisplay(AttendanceDisplayModelTypeDef *display,
                                 const AttendanceDateTimeTypeDef *now,
                                 uint16_t today_count,
                                 uint8_t wifi_online);
void Attendance_BuildResultDisplay(AttendanceDisplayModelTypeDef *display,
                                   const AttendanceUserTypeDef *user,
                                   const AttendanceEventTypeDef *event);

#ifdef __cplusplus
}
#endif

#endif
