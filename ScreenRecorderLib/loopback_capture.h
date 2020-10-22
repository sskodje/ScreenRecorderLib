//https://github.com/mvaneerde/blog/tree/master/loopback-capture
#pragma once
#include <windows.h>
#include <avrt.h>
#include <mutex>
#include <ppltasks.h> 
#include <mmdeviceapi.h>
#include "WWMFResampler.h"
#include "audio_prefs.h"

#pragma comment(lib, "avrt.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "winmm.lib")

class loopback_capture
{
public:
	loopback_capture(std::wstring tag);
	loopback_capture();
	~loopback_capture();
	void ClearRecordedBytes();
	bool IsCapturing();
	HRESULT LoopbackCapture(
		IMMDevice *pMMDevice,
		HMMIO hFile,
		bool bInt16,
		HANDLE hStartedEvent,
		HANDLE hStopEvent,
		EDataFlow flow,
		UINT32 samplerate,
		UINT32 channels
	);
	std::vector<BYTE> loopback_capture::PeakRecordedBytes();
	std::vector<BYTE> loopback_capture::GetRecordedBytes();
	std::vector<BYTE> loopback_capture::GetRecordedBytes(int byteCount);
	HRESULT StartCapture(UINT32 audioChannels, std::wstring device) { return StartCapture(0, audioChannels, device); }
	HRESULT StartCapture(UINT32 sampleRate, UINT32 audioChannels, std::wstring device);
	HRESULT StopCapture();
	UINT32 GetInputSampleRate();

private:
	Concurrency::task<void> m_CaptureTask = concurrency::task_from_result();
	bool m_IsCapturing = false;
	std::vector<BYTE> m_RecordedBytes = {};
	std::wstring m_Tag;
	UINT32 m_SamplesPerSec = 0;
	std::mutex m_Mutex;           // mutex for critical section
	HANDLE m_CaptureStartedEvent = nullptr;
	HANDLE m_CaptureStopEvent = nullptr;

	WWMFResampler m_Resampler;
	WWMFPcmFormat m_InputFormat;
	WWMFPcmFormat m_OutputFormat;
	WWMFSampleData m_SampleData;

	bool requiresResampling();
};

