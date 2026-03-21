#include "Com_protocol.h"
#include "Com_debug.h"
#include "usart.h"
#include "main.h"
#include "attendance_app.h"
#include "string.h"
#include <stdio.h>

extern RTC_HandleTypeDef hrtc;

static uint8_t Com_SetRTCTimeFromString(const char *timeStr)
{
    if (timeStr == NULL) return 0;
    int year, month, day, hour, min, sec;
    if (sscanf(timeStr, "%d-%d-%d %d:%d:%d", &year, &month, &day, &hour, &min, &sec) != 6) {
        return 0;
    }
    if (year < 2000 || year > 2099 || month < 1 || month > 12 || day < 1 || day > 31 ||
        hour < 0 || hour > 23 || min < 0 || min > 59 || sec < 0 || sec > 59) {
        return 0;
    }
    RTC_DateTypeDef sDate;
    RTC_TimeTypeDef sTime;
    sDate.Year = year - 2000;
    sDate.Month = month;
    sDate.Date = day;
    // 不设置周几
    sDate.WeekDay = 0;
    sTime.Hours = hour;
    sTime.Minutes = min;
    sTime.Seconds = sec;


    if (HAL_RTC_SetTime(&hrtc, &sTime, RTC_FORMAT_BIN) != HAL_OK) {
        return 0;
    }
    if (HAL_RTC_SetDate(&hrtc, &sDate, RTC_FORMAT_BIN) != HAL_OK) {
        return 0;
    }
    return 1;
}

// CRC16-MODBUS查表法全局变量
static uint16_t crc_table[256];

// 串口接收缓冲区（中断中使用）
uint8_t uart_recv_buf[FRAME_MAX_LEN] = {0};
uint8_t uart_recv_idx = 0;

// 帧解析消息队列（用于中断→任务的数据传递）
QueueHandle_t frame_queue = NULL;

// CRC16-MODBUS查表初始化（与ESP01S完全一致）
void CRC16_Modbus_Init_Table(void)
{
    uint16_t crc, poly = 0xA001;
    for (int i = 0; i < 256; i++)
    {
        crc = i;
        for (int j = 0; j < 8; j++)
        {
            crc = (crc & 0x0001) ? (crc >> 1) ^ poly : crc >> 1;
        }
        crc_table[i] = crc;
    }
}

// CRC16-MODBUS计算（高字节在前）
uint16_t CRC16_Modbus_Calc(uint8_t *data, uint8_t len)
{
    uint16_t crc = CRC16_INIT_VAL;
    for (int i = 0; i < len; i++)
    {
        crc = (crc >> 8) ^ crc_table[(crc & 0xFF) ^ data[i]];
    }
    return crc;
}

uint16_t Com_CRC16_Modbus_Table(uint8_t* data, uint8_t len)
{
    return CRC16_Modbus_Calc(data, len);
}

// 帧有效性校验（帧头/帧尾/CRC/长度）
uint8_t CheckFrameValid(uint8_t *frame, uint8_t len)
{
    if (len < 7) return 0;  // 最小帧长度：头+类型+长度+CRC(2)+尾=7
    if (frame[0] != FRAME_HEAD || frame[len-1] != FRAME_TAIL) return 0;

    uint8_t data_len = frame[2];
    if (data_len > FRAME_MAX_LEN - 6) return 0;
    uint8_t actual_data_len = len - 6;  // 总长度 - 头-类型-长度-CRC(2)-尾
    if (actual_data_len != data_len) return 0;

    uint16_t crc_calc = CRC16_Modbus_Calc(&frame[3], data_len);
    uint16_t crc_recv = (frame[3+data_len] << 8) | frame[4+data_len];
    return (crc_calc == crc_recv) ? 1 : 0;
}

uint8_t Com_Frame_Parse(uint8_t *buf, uint8_t len, FrameStruct_t *frame)
{
    if (len < 7 || buf[0] != FRAME_HEAD || buf[len-1] != FRAME_TAIL) return 0;
    uint8_t data_len = buf[2];
    if (data_len > FRAME_MAX_LEN - 6) return 0;
    if ((uint8_t)(len - 6) != data_len) return 0;

    frame->frame_type = buf[1];
    frame->data_len = data_len;
    if (data_len > 0)
    {
        memcpy(frame->data, &buf[3], data_len);
    }
    if (data_len < sizeof(frame->data))
    {
        frame->data[data_len] = 0;
    }
    frame->crc = (buf[3 + data_len] << 8) | buf[4 + data_len];

    uint16_t crc_calc = Com_CRC16_Modbus_Table(frame->data, frame->data_len);
    return (crc_calc == frame->crc) ? 1 : 0;
}

uint8_t Com_Frame_Pack(uint8_t *send_buf, FrameStruct_t *frame)
{
    if (frame == NULL || send_buf == NULL) return 0;
    if (frame->data_len > FRAME_MAX_LEN - 6) return 0;

    uint8_t idx = 0;
    send_buf[idx++] = FRAME_HEAD;
    send_buf[idx++] = frame->frame_type;
    send_buf[idx++] = frame->data_len;
    if (frame->data_len > 0)
    {
        memcpy(&send_buf[idx], frame->data, frame->data_len);
    }
    idx += frame->data_len;

    uint16_t crc = Com_CRC16_Modbus_Table(frame->data, frame->data_len);
    send_buf[idx++] = (crc >> 8) & 0xFF;
    send_buf[idx++] = crc & 0xFF;
    send_buf[idx++] = FRAME_TAIL;

    return idx;
}

// 发送帧到ESP01S（封装帧头/类型/CRC/帧尾）
void SendFrameToESP(uint8_t type, uint8_t *data, uint8_t len)
{
    uint8_t temp_frame[FRAME_MAX_LEN] = {0};
    FrameStruct_t frame;
    frame.frame_type = type;
    frame.data_len = len;
    if (frame.data_len > 0 && data != NULL)
    {
        memcpy(frame.data, data, len);
    }
    frame.crc = Com_CRC16_Modbus_Table(frame.data, frame.data_len);

    uint8_t pack_len = Com_Frame_Pack(temp_frame, &frame);
    if (pack_len == 0 || pack_len > FRAME_MAX_LEN) return;

    HAL_UART_Transmit(&huart1, temp_frame, pack_len, 100);
}

void Com_Protocol_Init(void)
{
    CRC16_Modbus_Init_Table();
    uart_recv_idx = 0;
    memset(uart_recv_buf, 0, FRAME_MAX_LEN);
    HAL_UART_Receive_IT(&huart1, &uart_recv_buf[uart_recv_idx], 1);
}

uint8_t Com_ProcessReceivedFrame(uint8_t *frame_buf, uint8_t len)
{
    FrameStruct_t frame = {0};
    uint8_t ok = Com_Frame_Parse(frame_buf, len, &frame);
    if (ok == 0) {
        COM_DEBUG("Parse failed for frame len=%d", len);
        return 0;
    }
    COM_DEBUG("Parse OK type=0x%02X data_len=%d", frame.frame_type, frame.data_len);
    return Com_HandleFrame(&frame);
}

uint8_t Com_HandleFrame(FrameStruct_t *frame)
{
    if (frame == NULL) return 0;
    COM_DEBUG("Handle frame type=0x%02X len=%d", frame->frame_type, frame->data_len);
    switch (frame->frame_type)
    {
        case TYPE_BJ_TIME:
        {
            // ESP 下发北京时间，STM32同步 RTC
            COM_DEBUG("TYPE_BJ_TIME received data_len=%d: %.*s", frame->data_len, frame->data_len, frame->data);
            if (Com_SetRTCTimeFromString((const char*)frame->data)) {
                Attendance_SetRtcValid(1U);
                COM_DEBUG("RTC time updated from BJ time");
            } else {
                COM_DEBUG("RTC time parse failed, ignore");
            }
            break;
        }
        case TYPE_ADD_USER:
        {
            COM_DEBUG("TYPE_ADD_USER received data_len=%d", frame->data_len);
            // 新增用户处理
            break;
        }
        case TYPE_REMOTE_CHECKIN:
        {
            COM_DEBUG("TYPE_REMOTE_CHECKIN received data_len=%d", frame->data_len);
            // 远程打卡处理
            break;
        }
        case TYPE_RESTART_STM32:
        {
            COM_DEBUG("TYPE_RESTART_STM32 received, rebooting");
            NVIC_SystemReset();
            break;
        }
        case TYPE_SET_WORK_TIME:
        {
            COM_DEBUG("TYPE_SET_WORK_TIME received: %.*s", frame->data_len, frame->data);
            if (Attendance_SetScheduleFromString((const char*)frame->data)) {
                COM_DEBUG("work schedule updated");
            } else {
                COM_DEBUG("work schedule parse failed");
            }
            // 设置上下班时间处理
            break;
        }
        case TYPE_TIME_REQ:
        {
            // STM32不应该收到该帧，忽略
            COM_DEBUG("TYPE_TIME_REQ received unexpectedly, ignore");
            break;
        }
        case TYPE_CHECK_DATA:
        {
            // STM32发给ESP的帧，不在本端接收处理
            COM_DEBUG("TYPE_CHECK_DATA received unexpectedly, ignore");
            break;
        }
        default:
            COM_DEBUG("Unknown frame type=0x%02X", frame->frame_type);
            break;
    }
    return 1;
}

// 串口接收中断回调函数（HAL库）
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1)
    {
        // 1. 检测帧头：如果是新帧，重置缓冲区
        if (uart_recv_idx == 0 && uart_recv_buf[0] != FRAME_HEAD)
        {
            // 还未收到头，继续接收
            HAL_UART_Receive_IT(&huart1, &uart_recv_buf[uart_recv_idx], 1);
            return;
        }
        if (uart_recv_buf[uart_recv_idx] == FRAME_HEAD)
        {
            uart_recv_idx = 0;
            memset(uart_recv_buf, 0, FRAME_MAX_LEN);
        }

        // 2. 存入缓冲区，索引自增
        uart_recv_buf[uart_recv_idx++] = huart->Instance->DR;

        if (uart_recv_idx >= FRAME_MAX_LEN)
        {
            // 溢出则重置
            uart_recv_idx = 0;
            memset(uart_recv_buf, 0, FRAME_MAX_LEN);
        }

        // 3. 检测帧尾：收到帧尾则校验并放入队列
        if (uart_recv_buf[uart_recv_idx-1] == FRAME_TAIL)
        {
            if (CheckFrameValid(uart_recv_buf, uart_recv_idx))
            {
                COM_DEBUG("收到完整帧，长度=%d，类型=0x%02X", uart_recv_idx, uart_recv_buf[1]);
                if (frame_queue != NULL)
                {
                    uint8_t msg[FRAME_MAX_LEN + 1] = {0};
                    msg[0] = uart_recv_idx;
                    memcpy(&msg[1], uart_recv_buf, uart_recv_idx);
                    xQueueSendFromISR(frame_queue, msg, NULL);
                }
            }
            else
            {
                COM_DEBUG("帧校验失败：长度=%d，头=0x%02X 尾=0x%02X", uart_recv_idx, uart_recv_buf[0], uart_recv_buf[uart_recv_idx-1]);
            }
            // 重置缓冲区
            uart_recv_idx = 0;
            memset(uart_recv_buf, 0, FRAME_MAX_LEN);
        }

        // 4. 重新开启串口中断接收（必须！否则只接收1个字节）
        if (uart_recv_idx < FRAME_MAX_LEN)
        {
            HAL_UART_Receive_IT(&huart1, &uart_recv_buf[uart_recv_idx], 1);
        }
        else
        {
            uart_recv_idx = 0;
            HAL_UART_Receive_IT(&huart1, &uart_recv_buf[uart_recv_idx], 1);
        }
    }
}
