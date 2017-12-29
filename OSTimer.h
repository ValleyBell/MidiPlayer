#ifndef __TIMER_H__
#define __TIMER_H__

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdtype.h>

typedef struct _os_timer OS_TIMER;

OS_TIMER* OSTimer_Init(void);
void OSTimer_Deinit(OS_TIMER* tmr);
UINT64 OSTimer_GetFrequency(const OS_TIMER* tmr);
UINT64 OSTimer_GetTime(const OS_TIMER* tmr);

#ifdef __cplusplus
}
#endif

#endif	// __TIMER_H__
