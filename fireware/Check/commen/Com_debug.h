#ifndef __COM_DEBUG_H
#define __COM_DEBUG_H

#include "stdarg.h"
#include "stdio.h"
#include "usart.h"



#ifdef __GNUC__
  #define PUTCHAR_PROTOTYPE int __io_putchar(int ch)
  #else
  #define PUTCHAR_PROTOTYPE int fputc(int ch, FILE *f)
  #endif


// 日志打印开关
#define COM_DEBUG_EN 1
#if COM_DEBUG_EN
#define COM_DEBUG(format, ...) printf("[%s:%d] " format "\r\n", __FILE__, __LINE__, ## __VA_ARGS__)
#else
#define COM_DEBUG(...)
#endif /* COM_DEBUG_EN */

 

#endif /* COM_DEBUG_H */

