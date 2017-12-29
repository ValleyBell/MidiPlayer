// Mac OS X Timer
// --------------
// using mach_absolute_time

// see also here https://gist.github.com/alfwatt/3588c5aa1f7a1ef7a3bb
#include <stdlib.h>
//#include <stdio.h>
#include <stddef.h>

#include <mach/mach_time.h>

#include <stdtype.h>
#include "OSTimer.h"

//typedef struct _os_timer OS_TIMER;
struct _os_timer
{
	UINT64 tmrFreq;
	mach_timebase_info_data_t tmrBase;
};

OS_TIMER* OSTimer_Init(void)
{
	OS_TIMER* tmr;
	
	tmr = (OS_TIMER*)calloc(1, sizeof(OS_TIMER));
	if (tmr == NULL)
		return NULL;
	
	tmr->tmrFreq = 1000000000;	// the resulting value is in nanoseconds
	mach_timebase_info(&tmr->tmrBase);
	//printf("Apple Clock: Tick Hz = %lu, Base: %lu / %lu\n", tmr->tmrFreq, tmr->tmrBase.numer, tmr->tmrBase.denom);
	
	return tmr;
}

void OSTimer_Deinit(OS_TIMER* tmr)
{
	free(tmr);
	
	return;
}

UINT64 OSTimer_GetFrequency(const OS_TIMER* tmr)
{
	return tmr->tmrFreq;
}

UINT64 OSTimer_GetTime(const OS_TIMER* tmr)
{
	UINT64 time;
	
	time = mach_absolute_time();
	return time * tmr->tmrBase.numer / tmr->tmrBase.denom;
}
