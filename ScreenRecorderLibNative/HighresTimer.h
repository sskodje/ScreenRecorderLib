#pragma once
#include "CommonTypes.h"
#include "Util.h"
class HighresTimer
{
public:
	HighresTimer();
	~HighresTimer();
	inline HANDLE GetTickEvent() { return m_TickEvent; }
	HRESULT StartRecurringTimer(INT64 msInterval);
	HRESULT StopTimer(bool waitForCompletion);
	HRESULT WaitForNextTick();
	HRESULT WaitFor(INT64 interval100Nanos);
	INT64 GetMillisUntilNextTick();
	inline INT64 GetTickCount() { return m_TickCount; }
private:
	bool m_IsActive;
	INT64 m_TickCount;
	INT64 m_LastTick;
	INT64 m_Interval;
	UINT m_TimerResolution;
	HANDLE m_TickEvent;
	HANDLE m_StopEvent;
	HANDLE m_EventArray[2];
};