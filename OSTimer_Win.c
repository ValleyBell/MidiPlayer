// Windows Timer
// -------------
// using QueryPerformanceCounter or timeGetTime

#include <stdlib.h>
//#include <stdio.h>
#include <stddef.h>

#include <Windows.h>

#include <stdtype.h>
#include "OSTimer.h"

// timer modes
#define TMODE_SYSTEM	0x00	// use system clock timeGetTime()
#define TMODE_HIGH_PERF	0x01	// use QueryPerformanceCounter()

#define SYSCLK_MULT		1000	// system clock multiplicator (for higher "virtual" precision)

//typedef struct _os_timer OS_TIMER;
struct _os_timer
{
	UINT8 mode;
	UINT64 tmrFreq;
};

OS_TIMER* OSTimer_Init(void)
{
	OS_TIMER* tmr;
	LARGE_INTEGER TempLInt;
	BOOL retValB;
	
	tmr = (OS_TIMER*)calloc(1, sizeof(OS_TIMER));
	if (tmr == NULL)
		return NULL;
	
	retValB = QueryPerformanceFrequency(&TempLInt);
	if (retValB)
	{
		tmr->mode = TMODE_HIGH_PERF;
		tmr->tmrFreq = TempLInt.QuadPart;
	}
	else
	{
		tmr->mode = TMODE_SYSTEM;
		tmr->tmrFreq = 1000 * SYSCLK_MULT;
	}
	//printf("Win Clock: Type %s, Tick Hz = %lu\n", (tmr->mode ? "Hi-Res" : "System"), tmr->tmrFreq);
	
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
	if (tmr->mode == TMODE_SYSTEM)
	{
		return (UINT64)timeGetTime() * SYSCLK_MULT;
	}
	else
	{
		LARGE_INTEGER lgInt;
		
		QueryPerformanceCounter(&lgInt);
		return (UINT64)lgInt.QuadPart;
	}
}
