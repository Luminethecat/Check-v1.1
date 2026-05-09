#ifndef OLED_SSD1306_H
#define OLED_SSD1306_H

#ifdef __cplusplus
extern "C" {
#endif

#include "attendance_app.h"
#include "stm32f1xx_hal.h"

#define OLED_WIDTH                        128U
#define OLED_HEIGHT                       64U

/* SSD1306 0.96 寸 OLED 基础接口。 */
void Oled_Init(void);
void Oled_Clear(void);
void Oled_UpdateScreen(void);
void Oled_DrawChar(uint8_t x, uint8_t y, char ch);
void Oled_DrawString(uint8_t x, uint8_t y, const char *text);
void Oled_RenderDisplayModel(const AttendanceDisplayModelTypeDef *display);
void Oled_TriggerReinit(void);
uint8_t Oled_ConsumeReinitRequest(void);
#ifdef __cplusplus
}
#endif

#endif