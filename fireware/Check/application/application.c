#include "application.h"

#include "attendance_app.h"
#include "audio_dac_app.h"
#include "key_input.h"
#include "oled_ssd1306.h"
#include "rc522_app.h"
#include "zw101_app.h"

/* 应用层统一初始化入口：
 * 这里按业务依赖顺序初始化各模块，后续 main 只需要调用一次即可。 */
void Application_Init(void)
{
  Attendance_Init();
  RC522_Init();
  ZW101_Init();

  Oled_Init();
  KeyInput_Init();
}
