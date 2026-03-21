#ifndef KEY_INPUT_H
#define KEY_INPUT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f1xx_hal.h"

typedef enum
{
  KEY_EVENT_NONE = 0,
  KEY_EVENT_OK_SHORT,
  KEY_EVENT_OK_LONG,
  KEY_EVENT_UP_SHORT,
  KEY_EVENT_DOWN_SHORT,
} KeyEventTypeDef;

/* 按键输入层：
 * 将 GPIO 电平转换成统一的短按/长按事件。 */
void KeyInput_Init(void);
KeyEventTypeDef KeyInput_Scan(void);

#ifdef __cplusplus
}
#endif

#endif
