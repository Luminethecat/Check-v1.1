#ifndef STORAGE_LAYOUT_H
#define STORAGE_LAYOUT_H

#ifdef __cplusplus
extern "C" {
#endif

/* STM32F103RCT6: 256KB internal Flash, top address is 0x0803FFFF.
 * Reserve the last pages for parameters and users, and a fixed region below
 * them for attendance records. This avoids overwriting the application code. */
#define STORAGE_ADDR_SYS_PARAM_BASE        0x0803F800UL
#define STORAGE_ADDR_USER_BASE             0x0803F000UL
#define STORAGE_ADDR_RECORD_BASE           0x08030000UL

#define STORAGE_MAX_USER_COUNT             20UL
#define STORAGE_MAX_RECORD_COUNT           1920UL

#ifdef __cplusplus
}
#endif

#endif
