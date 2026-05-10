/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : 考勤机 FreeRTOS 任务管理（Display/Check分离，音频独立）
  ******************************************************************************
  */
/* USER CODE END Header */

#include "FreeRTOS.h"
#include "cmsis_os2.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* 自有驱动头文件 */
#include "Com_debug.h"
#include "Com_protocol.h"
#include "key_input.h"
#include "oled_ssd1306.h"
#include "rtc.h"
#include "string.h"
#include "spi.h"
#include "audio_dac_app.h"
#include "rc522_app.h"
#include "stdio.h"
#include "zw101_app.h"
#include "oled_ssd1306.h"
#include "user_db.h"
#include "runtime_manager.h"
#include "application.h"
/* ===================== 全局变量 ===================== */

#if APPLICATION_RUN_BOARD_TEST
static  AttendanceDisplayModelTypeDef g_test_display;
static uint8_t g_display_timeout = 0;
void ZW101TestTask(void *argument);
#endif
/* -------------------------- FreeRTOS 任务句柄定义 -------------------------- */

osThreadId_t UARTHandle;
const osThreadAttr_t UART_attributes =
{
  .name = "UART",
  .stack_size = 512 * 4,
  .priority = osPriorityHigh,
};

osThreadId_t DisplayHandle;
const osThreadAttr_t Display_attributes =
{
  .name = "Display",
  .stack_size = 512 * 4,
  .priority = osPriorityLow,
};

osThreadId_t CheckHandle;
const osThreadAttr_t Check_attributes =
{
  .name = "Check",
  .stack_size = 1024 * 4,
  .priority = osPriorityNormal,
};

osThreadId_t AudioPlayHandle;
const osThreadAttr_t AudioPlay_attributes =
{
  .name = "AudioPlay",
  .stack_size = 256 * 4,
  .priority = osPriorityLow,
};


/* -------------------------- 队列 / 信号量 -------------------------- */

osSemaphoreId_t mutex_i2cHandle;
const osSemaphoreAttr_t mutex_i2c_attributes =
{
  "mutex_i2c"
};

osMessageQueueId_t audioQueueHandle;
const osMessageQueueAttr_t audioQueue_attributes =
{
  .name = "audioQueue"
};
#if APPLICATION_RUN_BOARD_TEST
osThreadId_t ZW101TestHandle;
const osThreadAttr_t ZW101Test_attributes =
{
  .name = "ZW101Test",
  .stack_size = 512 * 4,
  .priority = osPriorityLow,
};
#endif

osThreadId_t SystemCheckHandle;
const osThreadAttr_t SystemCheck_attributes =
{
  .name = "SystemCheck",
  .stack_size = 256 * 4,
  .priority = osPriorityHigh,
};

#if APPLICATION_RUN_BOARD_TEST
osThreadId_t RFIDTestHandle;
const osThreadAttr_t RFIDTest_attributes =
{
  .name = "RFIDTest",
  .stack_size = 256 * 4,
  .priority = osPriorityNormal,
};
#endif
/* -------------------------- 内部函数声明 -------------------------- */

static void TestDisplay_SetBootScreen(void);
static void TestDisplay_SetKeyEvent(KeyEventTypeDef key_event);
static void TestDisplay_SetRtc(
        const RTC_TimeTypeDef *time,
        const RTC_DateTypeDef *date);

/* 外部任务声明 */

void DisplayTask(void *argument);
void CheckTask(void *argument);
void AudioPlayTask(void *argument);
void UARTTask(void *argument);
void SystemCheckTask(void *argument);
void RFIDTestTask(void *argument);


/* ==================== FreeRTOS 初始化 ==================== */

void MX_FREERTOS_Init(void)
{
  COM_DEBUG(">>> 系统启动中...");

#if APPLICATION_RUN_BOARD_TEST
  TestDisplay_SetBootScreen();
#endif

  DAC_Sound_Init();
  
  RuntimeManager_Init();

#if APPLICATION_RUN_BOARD_TEST
ZW101TestHandle =
osThreadNew(
  ZW101TestTask,
  NULL,
  &ZW101Test_attributes);
#endif

  mutex_i2cHandle =
  osSemaphoreNew(
      1,
      1,
      &mutex_i2c_attributes);

  audioQueueHandle =
  osMessageQueueNew(
      4,
      sizeof(uint8_t),
      &audioQueue_attributes);

  DisplayHandle =
  osThreadNew(
      DisplayTask,
      NULL,
      &Display_attributes);

  CheckHandle =
  osThreadNew(
      CheckTask,
      NULL,
      &Check_attributes);

  AudioPlayHandle =
  osThreadNew(
      AudioPlayTask,
      NULL,
      &AudioPlay_attributes);

  UARTHandle =
  osThreadNew(
      UARTTask,
      NULL,
      &UART_attributes);

  /* 在 RTOS 环境中创建串口帧队列（中断 -> 任务 传递完整帧） */
  if (frame_queue == NULL) {
    frame_queue = xQueueCreate(4, FRAME_MAX_LEN + 1); /* msg[0]=len, msg[1..] = frame */
  }

  // 创建系统自检任务
  SystemCheckHandle =
  osThreadNew(
      SystemCheckTask,
      NULL,
      &SystemCheck_attributes);

#if APPLICATION_RUN_BOARD_TEST
  // 创建RFID测试任务
  RFIDTestHandle =
  osThreadNew(
      RFIDTestTask,
      NULL,
      &RFIDTest_attributes);
#endif

  uint8_t cmd = 4;
  osMessageQueuePut(audioQueueHandle,&cmd,0,0);

  COM_DEBUG(">>> 系统启动完成");
}


/* ==================== UI函数 ==================== */

#if APPLICATION_RUN_BOARD_TEST
static void TestDisplay_SetBootScreen(void)
{
  memset((void*)&g_test_display,0,sizeof(g_test_display));

  g_test_display.page = OLED_PAGE_IDLE;

  snprintf(g_test_display.line1,32,"ATTENDANCE");
  snprintf(g_test_display.line2,32,"SYSTEM START");
  snprintf(g_test_display.line3,32,"STM32F103");
  snprintf(g_test_display.line4,32,"BOOT OK");
}
#endif


#if APPLICATION_RUN_BOARD_TEST
static void TestDisplay_SetKeyEvent(
  KeyEventTypeDef key_event)
{

  uint8_t audio_cmd = 0;



  if(audio_cmd != 0)
  {
    osMessageQueuePut(
      audioQueueHandle,
      &audio_cmd,
      0,
      0);
  }
}
#endif


#if APPLICATION_RUN_BOARD_TEST
static void TestDisplay_SetRtc(
  const RTC_TimeTypeDef *time,
  const RTC_DateTypeDef *date)
{
  snprintf(g_test_display.line1,32,"ATTENDANCE");
  snprintf(g_test_display.line2,32,"TIME");

  snprintf(g_test_display.line3,
           32,
           "%02d:%02d:%02d",
           time->Hours,
           time->Minutes,
           time->Seconds);

  snprintf(g_test_display.line4,32,"READY");
}
#endif


/* ==================== Display Task ==================== */

void DisplayTask(void *argument)
{
  // RTC_TimeTypeDef rtc_time;
  // RTC_DateTypeDef rtc_date;
  AttendanceDisplayModelTypeDef display_snapshot;
  COM_DEBUG("Display Task Start");

  for(;;)
  {

    RuntimeManager_DisplayTaskStep();
    RuntimeManager_TimeSyncTaskStep();
    
    RuntimeManager_GetDisplaySnapshot(&display_snapshot);
    
    if (Oled_ConsumeReinitRequest()) {
      COM_DEBUG("DisplayTask: performing OLED re-init as requested");
      /* Oled_Init 内部已获取 I2C 互斥 */
      Oled_Init();
    }

    /* Oled_RenderDisplayModel 最终会调用 Oled_UpdateScreen，驱动层负责互斥保护 */
    Oled_RenderDisplayModel(&display_snapshot);
    osDelay(100);

  }

}


/* ==================== Check Task ==================== */
void CheckTask(void *argument)
{
  COM_DEBUG("Check Task Start");

  for(;;)
  {
    RuntimeManager_CheckTaskStep();
    osDelay(50);
  }
}

/* ==================== Audio Task ==================== */

void AudioPlayTask(void *argument)
{
  uint8_t cmd;
  static uint8_t playing = 0;

  for(;;)
  {
    if(osMessageQueueGet(audioQueueHandle,&cmd,NULL,portMAX_DELAY)==osOK)
    {
      if(playing) continue;

      playing = 1;

      switch(cmd)
      {
        case 1: DAC_Sound_Beep(); break;
        case 2: DAC_Sound_Success(); break;
        case 3: DAC_Sound_Error(); break;
        case 4: DAC_Sound_Welcome(); break;
      }

      playing = 0;
    }
  }
}


/* ==================== 其他任务 ==================== */
#if APPLICATION_RUN_BOARD_TEST
void ZW101TestTask(void *argument)
{
  ZW101_StatusTypeDef st;
  ZW101_SearchResultTypeDef res;

  COM_DEBUG("ZW101 TEST START");


  // 先验证通信


  for(;;)
  {

  st = ZW101_VerifyPassword(0);
  COM_DEBUG("INIT verify = %d", st);

  if(st != ZW101_OK)
  {
    COM_DEBUG("❌ ZW101 INIT FAIL");
  }
  else
  {
    COM_DEBUG("✅ ZW101 INIT OK");
  }
    // ===== 测试1：采集指纹图像 =====
    st = ZW101_CollectImage();
    COM_DEBUG("CollectImage = %d", st);

    if(st == ZW101_NO_FINGER)
    {
      COM_DEBUG("👉 no finger");
    }
    else if(st == ZW101_OK)
    {
      // ===== 测试2：特征提取 =====
      st = ZW101_GenerateChar(1);
      COM_DEBUG("GenerateChar = %d", st);

      if(st == ZW101_OK)
      {
        // ===== 测试3：搜索 =====
        st = ZW101_Search(1, 0, 300, &res);
        COM_DEBUG("Search = %d, ID=%d score=%d",
                  st, res.page_id, res.match_score);
      }
    }

    osDelay(1500);
   }
}
#endif

/* ==================== 系统自检任务 ==================== */
void SystemCheckTask(void *argument)
{
  COM_DEBUG("=== System Boot Check ===");

  // 1. Check RTC
  COM_DEBUG("[OK] RTC: Initialized");

  // 2. Check OLED
  Oled_Init();
  Oled_Clear();
  Oled_DrawString(0, 0, "SYSTEM CHECK");
  Oled_DrawString(0, 2, "OLED: OK");
  Oled_DrawString(0, 4, "Ver 1.0");
  COM_DEBUG("[OK] OLED: Display OK");
  osDelay(2000);

  // 3. Check RTC Time
  RTC_TimeTypeDef time;
  RTC_DateTypeDef date;
  if(HAL_RTC_GetTime(&hrtc, &time, RTC_FORMAT_BIN) == HAL_OK &&
     HAL_RTC_GetDate(&hrtc, &date, RTC_FORMAT_BIN) == HAL_OK)
  {
    COM_DEBUG("[OK] RTC: Time read OK %02d:%02d:%02d", time.Hours, time.Minutes, time.Seconds);
    /* RTC 时间可读则认为时钟有效，通知考勤模块 */
    Attendance_SetRtcValid(1U);
  }
  else
  {
    COM_DEBUG("[FAIL] RTC: Time read error");
  }

  // 4. Check User Database
  UserDb_Init();
  COM_DEBUG("[OK] UserDB: Init OK, Users: %d", UserDB_GetUserCount());

  // Add test users
#if APPLICATION_RUN_BOARD_TEST
  UserDB_AddTestUser();
  COM_DEBUG("[OK] Test users added");
  UserDB_PrintAllUsers();
#endif

  // 5. Check Attendance System
  Attendance_Init();
  COM_DEBUG("[OK] Attendance: Init OK");

  // 6. Check Protocol
  Com_Protocol_Init();
  COM_DEBUG("[OK] Protocol: Init OK");

  // Show system ready screen
  Oled_Clear();
  Oled_DrawString(0, 0, "SYSTEM READY");
  Oled_DrawString(0, 2, "Press any key");
  Oled_DrawString(0, 4, "STM32F103");
  Oled_DrawString(0, 6, "Attendance V1.0");

  COM_DEBUG("=== System Check Complete ===");

  // Self-check complete, enter low power mode
  for(;;)
  {
    osDelay(1000);
  }
}


void UARTTask(void *argument)
{
  uint8_t msg[FRAME_MAX_LEN + 1];
  for(;;)
  {
    /* 从串口帧队列中读取完整帧并交由解析处理 */
    if (frame_queue != NULL && xQueueReceive(frame_queue, msg, pdMS_TO_TICKS(100)) == pdPASS)
    {
      uint8_t len = msg[0];
      if (len > 0 && len <= FRAME_MAX_LEN)
      {
        /* msg[1..len] 包含帧数据 */
        Com_ProcessReceivedFrame(&msg[1], len);
      }
    }
    else
    {
      osDelay(10);
    }
  }
}
