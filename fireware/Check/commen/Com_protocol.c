#include "Com_protocol.h"
#include "usart.h"
#include "string.h"

// CRC16-MODBUS查表法全局变量
static uint16_t crc_table[256];

// 串口接收缓冲区（中断中使用）
uint8_t uart_recv_buf[FRAME_BUF_LEN] = {0};
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

// 帧有效性校验（帧头/帧尾/CRC/长度）
uint8_t CheckFrameValid(uint8_t *frame, uint8_t len)
{
    if (len < 7) return 0;  // 最小帧长度：头+类型+长度+CRC(2)+尾=7
    if (frame[0] != FRAME_HEAD || frame[len-1] != FRAME_TAIL) return 0;
    
    uint8_t data_len = frame[2];
    uint8_t actual_data_len = len - 6;  // 总长度 - 头-类型-长度-CRC(2)-尾
    if (actual_data_len != data_len) return 0;
    
    // 校验CRC
    uint16_t crc_calc = CRC16_Modbus_Calc(&frame[3], data_len);
    uint16_t crc_recv = (frame[3+data_len] << 8) | frame[3+data_len+1];
    return (crc_calc == crc_recv) ? 1 : 0;
}

// 发送帧到ESP01S（封装帧头/类型/CRC/帧尾）
void SendFrameToESP(uint8_t type, uint8_t *data, uint8_t len)
{
    if (len >= FRAME_BUF_LEN - 7) return;  // 防止缓冲区溢出
    
    uint8_t frame[FRAME_BUF_LEN] = {0};
    uint8_t idx = 0;
    
    frame[idx++] = FRAME_HEAD;    // 帧头
    frame[idx++] = type;          // 帧类型
    frame[idx++] = len;           // 数据长度
    
    if (len > 0 && data != NULL)
    {
        memcpy(&frame[idx], data, len);
        idx += len;
    }
    
    // 计算并添加CRC
    uint16_t crc = CRC16_Modbus_Calc(data, len);
    frame[idx++] = (crc >> 8) & 0xFF;  // CRC高字节
    frame[idx++] = crc & 0xFF;         // CRC低字节
    frame[idx++] = FRAME_TAIL;         // 帧尾
    
    // 串口发送（HAL库阻塞发送，FreeRTOS任务中使用安全）
    HAL_UART_Transmit(&huart1, frame, idx, 100);
}

// 串口接收中断回调函数（HAL库）
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1)
    {
        // 1. 检测帧头：如果是新帧，重置缓冲区
        if (uart_recv_buf[uart_recv_idx] == FRAME_HEAD)
        {
            uart_recv_idx = 0;
            memset(uart_recv_buf, 0, FRAME_BUF_LEN);
        }
        
        // 2. 存入缓冲区，索引自增
        uart_recv_buf[uart_recv_idx++] = huart->Instance->DR;
        
        // 3. 检测帧尾：收到帧尾则校验并放入队列
        if (uart_recv_buf[uart_recv_idx-1] == FRAME_TAIL)
        {
            if (CheckFrameValid(uart_recv_buf, uart_recv_idx))
            {
                // 将完整帧放入消息队列（传递给解析任务）
                if (frame_queue != NULL)
                {
                    xQueueSendFromISR(frame_queue, uart_recv_buf, NULL);
                }
            }
            // 重置缓冲区
            uart_recv_idx = 0;
            memset(uart_recv_buf, 0, FRAME_BUF_LEN);
        }
        
        // 4. 重新开启串口中断接收（必须！否则只接收1个字节）
        HAL_UART_Receive_IT(&huart1, &uart_recv_buf[uart_recv_idx], 1);
    }
}
