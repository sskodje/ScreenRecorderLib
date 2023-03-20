#pragma once
#include <Windows.h>
#include <vector>

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

    inline void SetWaitBands(std::vector<WAIT_BAND> bands) {
        m_WaitBands = bands;
    }
    void Wait();
    void Cancel();

private:

    std::vector<WAIT_BAND>   m_WaitBands;
    HANDLE m_CancelEvent;
    // Period in seconds that a new wait call is considered part of the same wait sequence
    static const UINT       m_WaitSequenceTimeInSeconds = 2;

    UINT                    m_CurrentWaitBandIdx;
    UINT                    m_WaitCountInCurrentBand;
    LARGE_INTEGER           m_QPCFrequency;
    LARGE_INTEGER           m_LastWakeUpTime;
    BOOL                    m_QPCValid;
};

