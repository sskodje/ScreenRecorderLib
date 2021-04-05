#include "highres_timer.h"
#include "log.h"

using namespace std::chrono;
highres_timer::highres_timer() :
	m_TimerResolution(0),
	m_TickEvent(nullptr),
	m_StopEvent(nullptr),
	m_EventArray{},
	m_LastTick{},
	m_Interval(0),
	m_TickCount(0),
	m_IsActive(false)
{
	TIMECAPS tc;
	UINT targetResolutionMs = 1;
	if (timeGetDevCaps(&tc, sizeof(TIMECAPS)) == TIMERR_NOERROR)
	{
		m_TimerResolution = min(max(tc.wPeriodMin, targetResolutionMs), tc.wPeriodMax);
		timeBeginPeriod(m_TimerResolution);
	}
	m_TickEvent = CreateWaitableTimerEx(NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
	m_StopEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
	HANDLE eventArray[2]{ m_StopEvent ,m_TickEvent };

	m_EventArray[0] = m_StopEvent;
	m_EventArray[1] = m_TickEvent;
}

highres_timer::~highres_timer()
{
	if (m_TimerResolution > 0) {
		timeEndPeriod(m_TimerResolution);
	}
	CloseHandle(m_TickEvent);
	CloseHandle(m_StopEvent);
}

HRESULT highres_timer::StartRecurringTimer(UINT msInterval)
{
	if (NULL == m_TickEvent) {
		DWORD dwErr = GetLastError();
		LOG_ERROR(L"CreateWaitableTimer failed: last error = %u", dwErr);
		return HRESULT_FROM_WIN32(dwErr);
	}

	m_Interval = msInterval;
	ResetEvent(m_TickEvent);
	ResetEvent(m_StopEvent);

	m_LastTick = std::chrono::steady_clock::now();

	LARGE_INTEGER liFirstFire;
	liFirstFire.QuadPart = -0; // negative means relative time
	BOOL bOK = SetWaitableTimer(
		m_TickEvent,
		&liFirstFire,
		msInterval,
		NULL, NULL, FALSE
	);
	if (!bOK) {
		DWORD dwErr = GetLastError();
		LOG_ERROR(L"SetWaitableTimer failed on OnReadSample: last error = %u", dwErr);
		return HRESULT_FROM_WIN32(dwErr);
	}
	m_IsActive = true;
	return S_OK;
}

HRESULT highres_timer::StopTimer(bool waitForCompletion)
{
	SetEvent(m_StopEvent);
	if (waitForCompletion && m_IsActive) {
		if (WaitForSingleObject(m_TickEvent, INFINITE) != WAIT_OBJECT_0) {
			LOG_ERROR("Failed to wait for timer tick");
			return E_FAIL;
		}
	}
	m_IsActive = false;
	CancelWaitableTimer(m_TickEvent);
	return S_OK;
}

HRESULT highres_timer::WaitForNextTick()
{
	//WAIT_OBJECT_0 means the first handle in the array, the stop event, signaled the stop, so exit.
	if (WaitForMultipleObjects(ARRAYSIZE(m_EventArray), m_EventArray, FALSE, INFINITE) == WAIT_OBJECT_0) {
		LOG_TRACE("Framerate timer was canceled");
		return E_FAIL;
	}

	m_LastTick = std::chrono::steady_clock::now();
	m_TickCount++;
	return S_OK;
}

HRESULT highres_timer::WaitFor(UINT64 interval100Nanos)
{
	LARGE_INTEGER liFirstFire;
	liFirstFire.QuadPart = -interval100Nanos; // negative means relative time
	BOOL bOK = SetWaitableTimer(
		m_TickEvent,
		&liFirstFire,
		0,
		NULL, NULL, FALSE
	);
	if (!bOK) {
		return E_FAIL;
	}
	m_IsActive = true;
	//WAIT_OBJECT_0 means the first handle in the array, the stop event, signaled the stop, so exit.
	if (WaitForMultipleObjects(ARRAYSIZE(m_EventArray), m_EventArray, FALSE, INFINITE) == WAIT_OBJECT_0) {
		LOG_TRACE("Framerate timer was canceled");
		m_IsActive = false;
		return E_FAIL;
	}
	m_IsActive = false;
	m_LastTick = std::chrono::steady_clock::now();
	m_TickCount++;
	return S_OK;
}

double highres_timer::GetMillisUntilNextTick()
{
	if (m_TickCount == 0)
		return 0;
	return max(0, (m_Interval - std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - m_LastTick).count()));
}