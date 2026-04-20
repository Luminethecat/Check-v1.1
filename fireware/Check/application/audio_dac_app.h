#ifndef __AUDIO_DAC_APP_H__
#define __AUDIO_DAC_APP_H__

#include <stdint.h>

void DAC_Sound_Init(void);
void DAC_Sound_Beep(void);
void DAC_Sound_Success(void);
void DAC_Sound_Error(void);
void DAC_Sound_Welcome(void);

#endif /* __AUDIO_DAC_APP_H__ */