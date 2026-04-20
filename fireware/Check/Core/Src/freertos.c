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
/* ===================== 全局变量 ===================== */

static  AttendanceDisplayModelTypeDef g_test_display;
static uint8_t g_display_timeout = 0;
void ZW101TestTask(void *argument);
/* -------------------------- FreeRTOS 任务句柄定义 -------------------------- */

osThreadId_t Mqtt_ReportHandle;
const osThreadAttr_t Mqtt_Report_attributes =
{
  .name = "Mqtt_Report",
  .stack_size = 512 * 4,
  .priority = osPriorityNormal,
};

osThreadId_t DebugHandle;
const osThreadAttr_t Debug_attributes =
{
  .name = "Debug",
  .stack_size = 256 * 4,
  .priority = osPriorityLow1,
};

osThreadId_t UARTHandle;
const osThreadAttr_t UART_attributes =
{
  .name = "UART",
  .stack_size = 1024 * 4,
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
osThreadId_t ZW101TestHandle;
const osThreadAttr_t ZW101Test_attributes =
{
  .name = "ZW101Test",
  .stack_size = 512 * 4,
  .priority = osPriorityLow,
};
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
void DebugTask(void *argument);
void Mqtt_ReportTask(void *argument);


/* ==================== FreeRTOS 初始化 ==================== */

void MX_FREERTOS_Init(void)
{
  COM_DEBUG(">>> 系统启动中...");

  TestDisplay_SetBootScreen();

  DAC_Sound_Init();

ZW101TestHandle =
osThreadNew(
    ZW101TestTask,
    NULL,
    &ZW101Test_attributes);

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

  //CheckHandle =
  // osThreadNew(
  //     CheckTask,
  //     NULL,
  //     &Check_attributes);

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

  DebugHandle =
  osThreadNew(
      DebugTask,
      NULL,
      &Debug_attributes);

  Mqtt_ReportHandle =
  osThreadNew(
      Mqtt_ReportTask,
      NULL,
      &Mqtt_Report_attributes);


  uint8_t cmd = 4;
  osMessageQueuePut(audioQueueHandle,&cmd,0,0);

  COM_DEBUG(">>> 系统启动完成");
}


/* ==================== UI函数 ==================== */

static void TestDisplay_SetBootScreen(void)
{
  memset((void*)&g_test_display,0,sizeof(g_test_display));

  g_test_display.page = OLED_PAGE_IDLE;

  snprintf(g_test_display.line1,32,"ATTENDANCE");
  snprintf(g_test_display.line2,32,"SYSTEM START");
  snprintf(g_test_display.line3,32,"STM32F103");
  snprintf(g_test_display.line4,32,"BOOT OK");
}


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


/* ==================== Display Task ==================== */

void DisplayTask(void *argument)
{
  RTC_TimeTypeDef rtc_time;
  RTC_DateTypeDef rtc_date;

  COM_DEBUG("Display Task Start");

  for(;;)
  {
    if(g_test_display.page == OLED_PAGE_IDLE)
    {
      HAL_RTC_GetTime(
            &hrtc,
            &rtc_time,
            RTC_FORMAT_BIN);

      HAL_RTC_GetDate(
            &hrtc,
            &rtc_date,
            RTC_FORMAT_BIN);

      TestDisplay_SetRtc(
            &rtc_time,
            &rtc_date);
    }

    if(g_display_timeout > 0)
    {
      g_display_timeout--;

      if(g_display_timeout == 0)
      {
        g_test_display.page =
            OLED_PAGE_IDLE;
      }
    }

    if(osSemaphoreAcquire(
          mutex_i2cHandle,
          pdMS_TO_TICKS(50))
          == osOK)
    {
      Oled_RenderDisplayModel(
            &g_test_display);

      osSemaphoreRelease(
            mutex_i2cHandle);
    }

    osDelay(100);
  }

}


/* ==================== Check Task ==================== */
void CheckTask(void *argument)
{
  KeyEventTypeDef key_event;
  static uint8_t key_lock = 0;

  RC522_CardInfoTypeDef card;
  char uid_str[32];
  ZW101_SearchResultTypeDef finger_res;
  ZW101_StatusTypeDef st;

  COM_DEBUG("Check Task Start");

  RC522_Init();
  ZW101_Init();
 // ZW101_VerifyPassword(0x00000000); // 必须

  for(;;)
  {
    key_event = KeyInput_Scan();

    if(key_event != KEY_EVENT_NONE && key_lock == 0)
    {
      key_lock = 1;

      // ==================== 长按OK：注册指纹 ====================
      if(key_event == KEY_EVENT_OK_LONG)
      {
        COM_DEBUG("开始注册指纹 ID1");

        snprintf(g_test_display.line1,32,"PUT FINGER 1");
        snprintf(g_test_display.line2,32,"PRESS & HOLD");
        g_display_timeout = 50;

        // 第一次采集
        st = ZW101_CollectImage();
        if(st != ZW101_OK) goto enroll_fail;

        st = ZW101_GenerateChar(1);
        if(st != ZW101_OK) goto enroll_fail;

        // 提示抬手
        snprintf(g_test_display.line1,32,"LIFT FINGER!");
        osDelay(1000); // 必须等抬手

        // 第二次采集
        snprintf(g_test_display.line1,32,"PUT FINGER 2");
        osDelay(500);

        st = ZW101_CollectImage();
        if(st != ZW101_OK) goto enroll_fail;

        st = ZW101_GenerateChar(2);
        if(st != ZW101_OK) goto enroll_fail;

        // 生成模板
        st = ZW101_CreateModel();
        if(st != ZW101_OK) goto enroll_fail;

        // 保存
        st = ZW101_StoreModel(1, 1);
        if(st == ZW101_OK)
        {
          COM_DEBUG("✅ 注册成功");
          snprintf(g_test_display.line1,32,"ENROLL OK");
          snprintf(g_test_display.line2,32,"ID:1");
          uint8_t cmd = 2;
          osMessageQueuePut(audioQueueHandle,&cmd,0,0);
          goto enroll_end;
        }

      enroll_fail:
        COM_DEBUG("❌ 注册失败 code:%d", st);
        snprintf(g_test_display.line1,32,"ENROLL ERR");
        snprintf(g_test_display.line2,32,"CODE:%d", st);
        uint8_t cmd = 3;
        osMessageQueuePut(audioQueueHandle,&cmd,0,0);

      enroll_end:
        g_display_timeout = 50;
      }

      // ==================== 短按OK：识别指纹 ====================
      if(key_event == KEY_EVENT_OK_SHORT)
      {
        st = ZW101_Identify(&finger_res);

        if(st == ZW101_OK)
        {
          COM_DEBUG("✅ 指纹匹配 ID:%d", finger_res.page_id);
          snprintf(g_test_display.line1,32,"FINGER OK");
          snprintf(g_test_display.line2,32,"ID:%d", finger_res.page_id);
          uint8_t cmd = 2;
          osMessageQueuePut(audioQueueHandle,&cmd,0,0);
        }
        else
        {
          COM_DEBUG("❌ 识别失败 code:%d", st);
          snprintf(g_test_display.line1,32,"FINGER ERR");
          snprintf(g_test_display.line2,32,"CODE:%d", st);
          uint8_t cmd = 3;
          osMessageQueuePut(audioQueueHandle,&cmd,0,0);
        }
        g_display_timeout = 50;
      }
    }

    if(key_event == KEY_EVENT_NONE)
    {
      key_lock = 0;
    }

    // 刷卡
    if(RC522_ReadCard(&card) == RC522_OK)
    {
      sprintf(uid_str,"%02X %02X %02X %02X", card.uid[0],card.uid[1],card.uid[2],card.uid[3]);
      snprintf(g_test_display.line1,32,"CARD OK");
      snprintf(g_test_display.line2,32,uid_str);
      g_display_timeout = 30;
      uint8_t cmd = 2;
      osMessageQueuePut(audioQueueHandle,&cmd,0,0);
      osDelay(500);
    }

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

void DebugTask(void *argument)
{
  for(;;)
  {


 // ZW101_Init();
  // ZW101_StatusTypeDef t;

  // t = ZW101_VerifyPassword(0);

  // COM_DEBUG("verify = %d", t);

  // // st = ZW101_CollectImage();
  // // COM_DEBUG("img = %d", st);

  // // st = ZW101_GenerateChar(1);
  // // COM_DEBUG("char = %d", st);
  osDelay(1);
}
}

void Mqtt_ReportTask(void *argument)
{
  for(;;)
  {
    osDelay(1);
  }
}

void UARTTask(void *argument)
{
  for(;;)
  {
    osDelay(10);
  }
}
