#ifndef APP_BOARD_H
#define APP_BOARD_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include "spi.h"
#include "usart.h"
#include "dac.h"
#include "Com_debug.h"

#define APP_RC522_SPI_HANDLE           hspi2
#define APP_RC522_CS_GPIO_Port         RC522_CS_GPIO_Port
#define APP_RC522_CS_Pin               RC522_CS_Pin
#define APP_RC522_RST_GPIO_Port        RC522_RST_GPIO_Port
#define APP_RC522_RST_Pin              RC522_RST_Pin

#define APP_ZW101_UART_HANDLE          huart2
#define APP_ZW101_IRQ_GPIO_Port        IRQ_GPIO_Port
#define APP_ZW101_IRQ_Pin              IRQ_Pin

#define APP_AUDIO_DAC_HANDLE           hdac
#define APP_AUDIO_DAC_CHANNEL          DAC_CHANNEL_1

#define APP_SPI_TIMEOUT_MS             100U
#define APP_UART_TIMEOUT_MS            500U

#ifdef __cplusplus
}
#endif

#endif
