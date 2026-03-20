#ifndef __COM_PROTOCOL_H
#define __COM_PROTOCOL_H

#include "stm32f1xx_hal.h"
#include "FreeRTOS.h"
#include "queue.h"
#include <stdint.h>

// ========== 严格对齐你的ESP协议定义 ==========
#define FRAME_HEAD          0xAA    // 帧头
#define FRAME_TAIL          0x55    // 帧尾
#define TYPE_BJ_TIME        0x01    // ESP→STM32：北京时间帧
#define TYPE_CHECK_DATA     0x02    // STM32→ESP：打卡数据帧
#define TYPE_TIME_REQ       0x03    // STM32→ESP：时间请求帧
#define TYPE_ADD_USER       0x04    // ESP→STM32：新增用户帧
#define TYPE_REMOTE_CHECKIN 0x05    // ESP→STM32：远程打卡帧
#define TYPE_RESTART_STM32  0x06    // ESP→STM32：STM32重启帧
#define TYPE_SET_WORK_TIME  0x07    // ESP→STM32：打卡时间设置帧
#define FRAME_MAX_LEN       256     // 与ESP一致的缓冲区大小
#define FRAME_BUF_LEN       FRAME_MAX_LEN
#define CRC16_INIT_VAL      0xFFFF  // CRC16初始值

// 帧结构体
typedef struct {
    uint8_t frame_type;    // 帧类型
    uint8_t data_len;      // 数据长度
    uint8_t data[FRAME_MAX_LEN - 6]; // 数据段（6=头+类型+长度+CRC+尾）
    uint16_t crc;          // CRC16校验值
} FrameStruct_t;

// 全局变量声明（串口接收缓冲区、消息队列）
extern uint8_t uart_recv_buf[FRAME_MAX_LEN];
extern uint8_t uart_recv_idx;
extern QueueHandle_t frame_queue;  // 帧解析队列（传递完整帧）

// CRC16-MODBUS校验
void CRC16_Modbus_Init_Table(void);
uint16_t CRC16_Modbus_Calc(uint8_t *data, uint8_t len);
uint16_t Com_CRC16_Modbus_Table(uint8_t* data, uint8_t len); // 对齐查表法

// 帧操作函数
uint8_t CheckFrameValid(uint8_t *frame, uint8_t len);  // 校验帧有效性
uint8_t Com_Frame_Parse(uint8_t *buf, uint8_t len, FrameStruct_t *frame);
uint8_t Com_Frame_Pack(uint8_t *send_buf, FrameStruct_t *frame);
uint8_t Com_ProcessReceivedFrame(uint8_t *frame_buf, uint8_t len);
uint8_t Com_HandleFrame(FrameStruct_t *frame);
void Com_Protocol_Init(void);
void SendFrameToESP(uint8_t type, uint8_t *data, uint8_t len);  // 发送帧到ESP

#endif /* __COM_PROTOCOL_H */
