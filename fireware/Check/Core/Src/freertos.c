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
/* Definitions for defaultTask */
osThreadId_t defaultTaskHandle;
const osThreadAttr_t defaultTask_attributes = {
  .name = "defaultTask",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for Debug */
osThreadId_t DebugHandle;
const osThreadAttr_t Debug_attributes = {
  .name = "Debug",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityLow,
};
/* Definitions for UART_Task */
osThreadId_t UART_TaskHandle;
const osThreadAttr_t UART_Task_attributes = {
  .name = "UART_Task",
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityLow,
};
/* Definitions for App_Main_Task */
osThreadId_t App_Main_TaskHandle;
const osThreadAttr_t App_Main_Task_attributes = {
  .name = "App_Main_Task",
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityLow,
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */

/* USER CODE END FunctionPrototypes */

void StartDefaultTask(void *argument);
void DebugTask(void *argument);
void StartUART_Task(void *argument);
void StartAppMainTask(void *argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  // 新增：创建帧解析队列（长度10，每个元素为FRAME_BUF_LEN字节）
  frame_queue = xQueueCreate(10, FRAME_BUF_LEN);
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of defaultTask */
  defaultTaskHandle = osThreadNew(StartDefaultTask, NULL, &defaultTask_attributes);

  /* creation of Debug */
  DebugHandle = osThreadNew(DebugTask, NULL, &Debug_attributes);

  /* creation of UART_Task */
  UART_TaskHandle = osThreadNew(StartUART_Task, NULL, &UART_Task_attributes);

  /* creation of App_Main_Task */
  App_Main_TaskHandle = osThreadNew(StartAppMainTask, NULL, &App_Main_Task_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

}

/* USER CODE BEGIN Header_StartDefaultTask */
/**
  * @brief  Function implementing the defaultTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartDefaultTask */
void StartDefaultTask(void *argument)
{
  /* USER CODE BEGIN StartDefaultTask */
  /* Infinite loop */
  for(;;)
  {
    osDelay(1);
  }
  /* USER CODE END StartDefaultTask */
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

/* USER CODE BEGIN Header_StartUART_Task */
/**
* @brief Function implementing the UART_Task thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartUART_Task */
void StartUART_Task(void *argument)
{
  /* USER CODE BEGIN StartUART_Task */
/* USER CODE BEGIN StartUARTParseTask */
  uint8_t recv_frame[FRAME_BUF_LEN] = {0};
  
  // 初始化CRC16表
  CRC16_Modbus_Init_Table();
  
  // 开启串口中断接收（必须！否则无法接收ESP数据）
  HAL_UART_Receive_IT(&huart1, &uart_recv_buf[0], 1);
  
  /* Infinite loop */
  for(;;)
  {
    // 从队列中读取ESP发来的完整帧（阻塞等待，超时100ms）
    if (xQueueReceive(frame_queue, recv_frame, 100 / portTICK_PERIOD_MS) == pdTRUE)
    {
      uint8_t frame_type = recv_frame[1];
      uint8_t data_len = recv_frame[2];
      uint8_t *frame_data = &recv_frame[3];
      // 新增：打印data_len，消除“未使用变量”警告
      COM_DEBUG("当前帧数据长度：%d\r\n", data_len);
      
      // 解析不同类型的帧（根据ESP01S的帧类型）
      switch(frame_type)
      {
        case TYPE_BJ_TIME:  // 接收ESP发来的北京时间
        {
          char bj_time[21] = {0};
          memcpy(bj_time, frame_data, 20);
          bj_time[20] = '\0';
          // 这里可以添加时间处理逻辑（比如更新STM32 RTC）
          COM_DEBUG("收到北京时间：%s\r\n", bj_time);
          break;
        }
        case TYPE_REMOTE_CHECKIN:  // 接收远程打卡指令
        {
          // 解析用户ID（4字节）
          uint32_t user_id = (frame_data[0]<<24) | (frame_data[1]<<16) | (frame_data[2]<<8) | frame_data[3];
          // 新增：打印user_id，消除“未使用变量”警告
          COM_DEBUG("收到远程打卡指令，用户ID：%lu\r\n", user_id);
          // 这里添加远程打卡逻辑（比如模拟打卡成功，发送TYPE_CHECK_DATA帧给ESP）
          break;
        }
        case TYPE_SET_WORK_TIME:  // 接收打卡时间设置指令
        {
          char work_time[8] = {0}, off_time[8] = {0};
          char *p = (char*)frame_data;
          strncpy(work_time, p, strchr(p, '|') - p);
          strncpy(off_time, strchr(p, '|')+1, 7);
          // 这里添加保存打卡时间的逻辑
          break;
        }
        default:
          break;
      }
      
      memset(recv_frame, 0, FRAME_BUF_LEN);  // 清空帧缓冲区
    }
    osDelay(10);
  }
  /* USER CODE END StartUART_Task */
}

/* USER CODE BEGIN Header_StartAppMainTask */
/**
* @brief Function implementing the App_Main_Task thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartAppMainTask */
void StartAppMainTask(void *argument)
{
  /* USER CODE BEGIN StartAppMainTask */
  /* Infinite loop */
// 测试：每隔5秒向ESP发送时间请求帧
  uint8_t test_data[1] = {0};
  
  /* Infinite loop */
  for(;;)
  {
    // 发送时间请求帧（TYPE_TIME_REQ）
    SendFrameToESP(TYPE_TIME_REQ, test_data, 1);
    
    // 模拟打卡数据（测试用）
    uint8_t check_data[25] = {0};
    uint32_t user_id = 1001;  // 测试用户ID
    char check_time[20] = "2026-02-10 09:00:00";  // 测试打卡时间
    
    // 填充用户ID（4字节）
    check_data[0] = (user_id >> 24) & 0xFF;
    check_data[1] = (user_id >> 16) & 0xFF;
    check_data[2] = (user_id >> 8) & 0xFF;
    check_data[3] = user_id & 0xFF;
    
    // 填充打卡时间（20字节）
    memcpy(&check_data[4], check_time, 20);
    
    // 填充打卡类型（0x01=上班打卡）
    check_data[24] = 0x01;
    
    // 发送打卡帧到ESP
    SendFrameToESP(TYPE_CHECK_DATA, check_data, 25);
    
    osDelay(5000);  // 5秒间隔
  }
  /* USER CODE END StartAppMainTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/* USER CODE END Application */

