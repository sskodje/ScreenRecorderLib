//https://github.com/mvaneerde/blog/tree/master/loopback-capture
#pragma once
#include <windows.h>
#include <avrt.h>
#include <mutex>
#include <mmdeviceapi.h>
#include "WWMFResampler.h"

#pragma comment(lib, "avrt.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "winmm.lib")

struct LoopbackCaptureThreadFunctionArguments {
	IMMDevice *pMMDevice;
	void *pCaptureInstance;
	bool bInt16;
	HMMIO hFile;
	HANDLE hStartedEvent;
	HANDLE hStopEvent;
	UINT32 nFrames;
	HRESULT hr;
	EDataFlow flow;
	UINT32 samplerate;
};

DWORD WINAPI LoopbackCaptureThreadFunction(LPVOID pContext);
class loopback_capture
{
public:
	loopback_capture();
	~loopback_capture();
	void ClearRecordedBytes();
	void Cleanup();
	HRESULT LoopbackCapture(
		IMMDevice *pMMDevice,
		HMMIO hFile,
		bool bInt16,
		HANDLE hStartedEvent,
		HANDLE hStopEvent,
		PUINT32 pnFrames,
		EDataFlow flow,
		UINT32 samplerate = 0
	);

	std::vector<BYTE> loopback_capture::GetRecordedBytes();
	UINT32 GetInputSampleRate();
private:
	bool m_IsDestructed = false;
	std::vector<BYTE> m_RecordedBytes = {};
	UINT32 m_SamplesPerSec = 0;
	std::mutex mtx;           // mutex for critical section
	
	WWMFResampler resampler;
	WWMFPcmFormat inputFormat;
	WWMFPcmFormat outputFormat;
	WWMFSampleData sampleData;
};

