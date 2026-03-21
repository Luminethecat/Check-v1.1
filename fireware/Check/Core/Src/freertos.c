/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : Code for freertos applications
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "Com_debug.h"
#include "Com_protocol.h"
#include "oled_ssd1306.h"
#include "runtime_manager.h"
#include "usart.h"
#include "string.h"

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */

/* USER CODE END Variables */
/* Definitions for Mqtt_Report */
osThreadId_t Mqtt_ReportHandle;
const osThreadAttr_t Mqtt_Report_attributes = {
  .name = "Mqtt_Report",
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for Debug */
osThreadId_t DebugHandle;
const osThreadAttr_t Debug_attributes = {
  .name = "Debug",
  .stack_size = 256 * 4,
  .priority = (osPriority_t) osPriorityLow1,
};
/* Definitions for UART */
osThreadId_t UARTHandle;
const osThreadAttr_t UART_attributes = {
  .name = "UART",
  .stack_size = 1024 * 4,
  .priority = (osPriority_t) osPriorityHigh,
};
/* Definitions for Display */
osThreadId_t DisplayHandle;
const osThreadAttr_t Display_attributes = {
  .name = "Display",
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityLow,
};
/* Definitions for Check */
osThreadId_t CheckHandle;
const osThreadAttr_t Check_attributes = {
  .name = "Check",
  .stack_size = 1024 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for TimeSync */
osThreadId_t TimeSyncHandle;
const osThreadAttr_t TimeSync_attributes = {
  .name = "TimeSync",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for queue_checkin_data */
osMessageQueueId_t queue_checkin_dataHandle;
const osMessageQueueAttr_t queue_checkin_data_attributes = {
  .name = "queue_checkin_data"
};
/* Definitions for sem_serial_frame */
osSemaphoreId_t sem_serial_frameHandle;
const osSemaphoreAttr_t sem_serial_frame_attributes = {
  .name = "sem_serial_frame"
};
/* Definitions for mutex_spi */
osSemaphoreId_t mutex_spiHandle;
const osSemaphoreAttr_t mutex_spi_attributes = {
  .name = "mutex_spi"
};
/* Definitions for mutex_i2c */
osSemaphoreId_t mutex_i2cHandle;
const osSemaphoreAttr_t mutex_i2c_attributes = {
  .name = "mutex_i2c"
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */

/* USER CODE END FunctionPrototypes */

void Mqtt_ReportTask(void *argument);
void DebugTask(void *argument);
void UARTTask(void *argument);
void DisplayTask(void *argument);
void CheckTask(void *argument);
void TimeSyncTask(void *argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */
  RuntimeManager_Init();

  /* USER CODE END Init */

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* Create the semaphores(s) */
  /* creation of sem_serial_frame */
  sem_serial_frameHandle = osSemaphoreNew(1, 0, &sem_serial_frame_attributes);

  /* creation of mutex_spi */
  mutex_spiHandle = osSemaphoreNew(1, 1, &mutex_spi_attributes);

  /* creation of mutex_i2c */
  mutex_i2cHandle = osSemaphoreNew(1, 1, &mutex_i2c_attributes);

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* Create the queue(s) */
  /* creation of queue_checkin_data */
  queue_checkin_dataHandle = osMessageQueueNew (5, 28, &queue_checkin_data_attributes);

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  // 新增：创建帧解析队列（长度10，每个元素为FRAME_MAX_LEN+1字节，第一字节为长度）
  frame_queue = xQueueCreate(10, FRAME_MAX_LEN + 1);
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of Mqtt_Report */
  Mqtt_ReportHandle = osThreadNew(Mqtt_ReportTask, NULL, &Mqtt_Report_attributes);

  /* creation of Debug */
  DebugHandle = osThreadNew(DebugTask, NULL, &Debug_attributes);

  /* creation of UART */
  UARTHandle = osThreadNew(UARTTask, NULL, &UART_attributes);

  /* creation of Display */
  DisplayHandle = osThreadNew(DisplayTask, NULL, &Display_attributes);

  /* creation of Check */
  CheckHandle = osThreadNew(CheckTask, NULL, &Check_attributes);

  /* creation of TimeSync */
  TimeSyncHandle = osThreadNew(TimeSyncTask, NULL, &TimeSync_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

}

/* USER CODE BEGIN Header_Mqtt_ReportTask */
/**
  * @brief  Function implementing the Mqtt_Report thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_Mqtt_ReportTask */
void Mqtt_ReportTask(void *argument)
{
  /* USER CODE BEGIN Mqtt_ReportTask */
  /* Infinite loop */
  for(;;)
  {
    osDelay(1);
  }
  /* USER CODE END Mqtt_ReportTask */
}

/* USER CODE BEGIN Header_DebugTask */
/**
* @brief Function implementing the Debug thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_DebugTask */
void DebugTask(void *argument)
{
  /* USER CODE BEGIN DebugTask */
  /* Infinite loop */
  for(;;)
  {
    //COM_DEBUG("Debug Task is running");
    osDelay(1000); // Delay for 1 second (1000 ms)
  }
  /* USER CODE END DebugTask */
}

/* USER CODE BEGIN Header_UARTTask */
/**
* @brief Function implementing the UART thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_UARTTask */
void UARTTask(void *argument)
{
  /* USER CODE BEGIN UARTTask */
  uint8_t queue_msg[FRAME_MAX_LEN + 1];
  for(;;)
  {
    if (xQueueReceive(frame_queue, queue_msg, portMAX_DELAY) == pdTRUE)
    {
      uint8_t frame_len = queue_msg[0];
      if (frame_len > 0 && frame_len <= FRAME_MAX_LEN)
      {
        Com_ProcessReceivedFrame(&queue_msg[1], frame_len);
      }
    }
  }
  /* USER CODE END UARTTask */
}

/* USER CODE BEGIN Header_DisplayTask */
/**
* @brief Function implementing the Display thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_DisplayTask */
void DisplayTask(void *argument)
{
  /* USER CODE BEGIN DisplayTask */
  AttendanceDisplayModelTypeDef display;
  /* Infinite loop */
  for(;;)
  {
    RuntimeManager_DisplayTaskStep();
    RuntimeManager_GetDisplaySnapshot(&display);
    Oled_RenderDisplayModel(&display);
    osDelay(200);
  }
  /* USER CODE END DisplayTask */
}

/* USER CODE BEGIN Header_CheckTask */
/**
* @brief Function implementing the Check thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_CheckTask */
void CheckTask(void *argument)
{
  /* USER CODE BEGIN CheckTask */
  /* Infinite loop */
  for(;;)
  {
    RuntimeManager_CheckTaskStep();
    osDelay(100);
  }
  /* USER CODE END CheckTask */
}

/* USER CODE BEGIN Header_TimeSyncTask */
/**
* @brief Function implementing the TimeSync thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_TimeSyncTask */
void TimeSyncTask(void *argument)
{
  /* USER CODE BEGIN TimeSyncTask */
  /* Infinite loop */
  for(;;)
  {
    RuntimeManager_TimeSyncTaskStep();
    osDelay(1000);
  }
  /* USER CODE END TimeSyncTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/* USER CODE END Application */

