#pragma once
#include "common_types.h"
#include <chrono>
#include <thread>
#include "utilities.h"
class highres_timer
{
public:
	highres_timer();
	~highres_timer();
	inline HANDLE GetTickEvent() { return m_TickEvent; }
	HRESULT StartRecurringTimer(UINT msInterval);
	HRESULT StopTimer(bool waitForCompletion);
	HRESULT WaitForNextTick();
	HRESULT WaitFor(UINT64 interval100Nanos);
	double GetMillisUntilNextTick();
	inline UINT64 GetTickCount() { return m_TickCount; }
private:
	bool m_IsActive;
	UINT64 m_TickCount;
	std::chrono::steady_clock::time_point m_LastTick;
	UINT m_Interval;
	UINT m_TimerResolution;
	HANDLE m_TickEvent;
	HANDLE m_StopEvent;
	HANDLE m_EventArray[2];
};