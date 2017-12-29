// POSIX Timer
// -----------
// using clock_gettime

#include <stdlib.h>
//#include <stdio.h>
#include <stddef.h>

#include <time.h>

#include <stdtype.h>
#include "OSTimer.h"

//typedef struct _os_timer OS_TIMER;
struct _os_timer
{
	clockid_t clkType;
	UINT64 tmrFreq;
	UINT64 tmrRes;
};

static UINT64 TimeSpec2Int64(const struct timespec* ts)
{
	return (UINT64)ts->tv_sec * 1000000000 + ts->tv_nsec;
}

OS_TIMER* OSTimer_Init(void)
{
	OS_TIMER* tmr;
	struct timespec clkTS;
	int retVal;
	
	tmr = (OS_TIMER*)calloc(1, sizeof(OS_TIMER));
	if (tmr == NULL)
		return NULL;
	
	tmr->clkType = CLOCK_MONOTONIC;
	tmr->tmrFreq = 1000000000;	// the resulting value is in nanoseconds
	retVal = clock_getres(tmr->clkType, &clkTS);
	tmr->tmrRes = TimeSpec2Int64(&clkTS);	// save actual timer resolution
	//printf("POSIX Clock: Type %u, Tick Hz = %lu, Resolution = %lu\n", tmr->clkType, tmr->tmrFreq, tmr->tmrRes);
	
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
	struct timespec clkTS;
	int retVal;
	
	retVal = clock_gettime(tmr->clkType, &clkTS);
	return TimeSpec2Int64(&clkTS);
}
