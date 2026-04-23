#ifndef RUNTIME_MANAGER_H
#define RUNTIME_MANAGER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "attendance_app.h"

/* 运行时状态机接口：
 * 提供给 FreeRTOS 各任务周期调用。 */
void RuntimeManager_Init(void);
void RuntimeManager_CheckTaskStep(void);
void RuntimeManager_DisplayTaskStep(void);
void RuntimeManager_TimeSyncTaskStep(void);
void RuntimeManager_GetDisplaySnapshot(AttendanceDisplayModelTypeDef *display);
uint8_t RuntimeManager_RemoteCheckInByUserId(uint32_t user_id);

#ifdef __cplusplus
}
#endif

#endif
