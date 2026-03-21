#include "key_input.h"

#include "main.h"

#define KEY_ACTIVE_LEVEL               GPIO_PIN_SET
#define KEY_LONGPRESS_MS               3000U
#define KEY_DEBOUNCE_MS                30U

/* 三个按键都走统一的去抖 + 长按判定，
 * 这样业务层只拿事件，不直接关心 GPIO 电平细节。 */
typedef struct
{
  GPIO_TypeDef *port;
  uint16_t pin;
  uint8_t stable_state;
  uint8_t pressed;
  uint32_t tick_change;
  uint32_t tick_press;
} KeyStateTypeDef;

static KeyStateTypeDef g_keys[3];

static uint8_t KeyInput_Read(KeyStateTypeDef *key)
{
  return (HAL_GPIO_ReadPin(key->port, key->pin) == KEY_ACTIVE_LEVEL) ? 1U : 0U;
}

void KeyInput_Init(void)
{
  g_keys[0].port = KEY1_GPIO_Port;
  g_keys[0].pin = KEY1_Pin;
  g_keys[1].port = KEY2_GPIO_Port;
  g_keys[1].pin = KEY2_Pin;
  g_keys[2].port = KEY3_GPIO_Port;
  g_keys[2].pin = KEY3_Pin;
}

KeyEventTypeDef KeyInput_Scan(void)
{
  uint32_t now = HAL_GetTick();
  uint8_t idx;
  uint8_t raw;

  for (idx = 0U; idx < 3U; idx++)
  {
    raw = KeyInput_Read(&g_keys[idx]);

    if (raw != g_keys[idx].stable_state)
    {
      if ((now - g_keys[idx].tick_change) >= KEY_DEBOUNCE_MS)
      {
        g_keys[idx].stable_state = raw;
        g_keys[idx].tick_change = now;

        if (raw != 0U)
        {
          g_keys[idx].pressed = 1U;
          g_keys[idx].tick_press = now;
        }
        else if (g_keys[idx].pressed != 0U)
        {
          /* 松手时才输出事件，避免按住过程中重复触发。 */
          g_keys[idx].pressed = 0U;
          if ((now - g_keys[idx].tick_press) >= KEY_LONGPRESS_MS)
          {
            return (idx == 0U) ? KEY_EVENT_OK_LONG : KEY_EVENT_NONE;
          }

          if (idx == 0U) return KEY_EVENT_OK_SHORT;
          if (idx == 1U) return KEY_EVENT_UP_SHORT;
          return KEY_EVENT_DOWN_SHORT;
        }
      }
    }
    else
    {
      g_keys[idx].tick_change = now;
    }
  }

  return KEY_EVENT_NONE;
}
