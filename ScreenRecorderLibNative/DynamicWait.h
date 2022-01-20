#pragma once
#include <Windows.h>

#define WAIT_BAND_COUNT 3
#define WAIT_BAND_STOP 0

struct WAIT_BAND
{
    UINT    WaitTime;
    UINT    WaitCount;
};

class DynamicWait
{
public:
    DynamicWait();
    ~DynamicWait();

    void Wait();

private:

    static const WAIT_BAND   m_WaitBands[WAIT_BAND_COUNT];

    // Period in seconds that a new wait call is considered part of the same wait sequence
    static const UINT       m_WaitSequenceTimeInSeconds = 2;

    UINT                    m_CurrentWaitBandIdx;
    UINT                    m_WaitCountInCurrentBand;
    LARGE_INTEGER           m_QPCFrequency;
    LARGE_INTEGER           m_LastWakeUpTime;
    BOOL                    m_QPCValid;
};

