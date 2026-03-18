#ifndef __COM_PROTOCOL_H
#define __COM_PROTOCOL_H

#include "stm32f1xx_hal.h"
#include "FreeRTOS.h"
#include "queue.h"

// 帧协议定义（与ESP01S完全一致）
#define FRAME_HEAD          0xAA
#define FRAME_TAIL          0x55
#define TYPE_BJ_TIME        0x01    // ESP→STM32 时间帧
#define TYPE_CHECK_DATA     0x02    // STM32→ESP 打卡帧
#define TYPE_TIME_REQ       0x03    // STM32→ESP 时间请求帧
#define TYPE_ADD_USER       0x04    // ESP→STM32 新增用户帧
#define TYPE_REMOTE_CHECKIN 0x05    // ESP→STM32 远程打卡帧
#define TYPE_RESTART_STM32  0x06    // ESP→STM32 重启帧
#define TYPE_SET_WORK_TIME  0x07    // ESP→STM32 打卡时间设置帧

#define FRAME_BUF_LEN       256     // 帧缓冲区大小
#define CRC16_INIT_VAL      0xFFFF  // CRC16初始值

// 全局变量声明（串口接收缓冲区、消息队列）
extern uint8_t uart_recv_buf[FRAME_BUF_LEN];
extern uint8_t uart_recv_idx;
extern QueueHandle_t frame_queue;  // 帧解析队列（传递完整帧）

// CRC16-MODBUS校验（查表法，与ESP01S一致）
void CRC16_Modbus_Init_Table(void);
uint16_t CRC16_Modbus_Calc(uint8_t *data, uint8_t len);

// 帧操作函数
uint8_t CheckFrameValid(uint8_t *frame, uint8_t len);  // 校验帧有效性
void SendFrameToESP(uint8_t type, uint8_t *data, uint8_t len);  // 发送帧到ESP

#endif /* __COM_PROTOCOL_H */
