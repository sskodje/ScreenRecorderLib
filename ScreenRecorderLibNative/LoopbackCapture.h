//https://github.com/mvaneerde/blog/tree/master/loopback-capture
#pragma once
#include <windows.h>
#include <avrt.h>
#include <mmdeviceapi.h>
#include "WWMFResampler.h"
#include "AudioPrefs.h"
#include "Log.h"
#include <thread>
#include <stdio.h>
#include <audioclient.h>
#include <vector>
#pragma comment(lib, "avrt.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "winmm.lib")

class LoopbackCapture
{
public:
	LoopbackCapture(_In_opt_ std::wstring tag = L"");
	~LoopbackCapture();
	void ClearRecordedBytes();
	bool IsCapturing();
	HRESULT StartLoopbackCapture(
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
	struct TaskWrapper;
	std::unique_ptr<TaskWrapper> m_TaskWrapperImpl;

	bool m_IsCapturing = false;
	std::vector<BYTE> m_OverflowBytes = {};
	std::vector<BYTE> m_RecordedBytes = {};
	std::wstring m_Tag;
	HANDLE m_CaptureStartedEvent = nullptr;
	HANDLE m_CaptureStopEvent = nullptr;

	WWMFResampler m_Resampler;
	WWMFPcmFormat m_InputFormat;
	WWMFPcmFormat m_OutputFormat;

	bool requiresResampling();
};

