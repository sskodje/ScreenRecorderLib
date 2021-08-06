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
	std::vector<BYTE> PeakRecordedBytes();
	std::vector<BYTE> GetRecordedBytes();
	HRESULT StartCapture(UINT32 audioChannels, std::wstring device, EDataFlow flow) { return StartCapture(0, audioChannels, device, flow); }
	HRESULT StartCapture(UINT32 sampleRate, UINT32 audioChannels, std::wstring device, EDataFlow flow);
	HRESULT StopCapture();
	void ReturnAudioBytesToBuffer(std::vector<BYTE> bytes);

private:
	Concurrency::task<void> m_CaptureTask = concurrency::task_from_result();
	bool m_IsCapturing = false;
	std::vector<BYTE> m_OverflowBytes = {};
	std::vector<BYTE> m_RecordedBytes = {};
	std::wstring m_Tag;
	std::mutex m_Mutex;           // mutex for critical section
	HANDLE m_CaptureStartedEvent = nullptr;
	HANDLE m_CaptureStopEvent = nullptr;

	WWMFResampler m_Resampler;
	WWMFPcmFormat m_InputFormat;
	WWMFPcmFormat m_OutputFormat;

	bool requiresResampling();
};

