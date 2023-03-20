//https://github.com/mvaneerde/blog/tree/master/loopback-capture
#pragma once
#include "WWMFResampler.h"
#include "Log.h"
#include "CommonTypes.h"
#include "DynamicWait.h"
#include <windows.h>
#include <avrt.h>
#include <mmdeviceapi.h>
#include <thread>
#include <stdio.h>
#include <audioclient.h>
#include <vector>
#include <functional>
#include <atlbase.h>

#pragma comment(lib, "avrt.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "winmm.lib")

class WASAPICapture
{
public:
	WASAPICapture(_In_ std::shared_ptr<AUDIO_OPTIONS> &audioOptions, _In_opt_ std::wstring tag = L"");
	~WASAPICapture();
	void ClearRecordedBytes();
	bool IsCapturing();
	std::vector<BYTE> PeakRecordedBytes();
	std::vector<BYTE> GetRecordedBytes(UINT64 duration100Nanos);
	HRESULT Initialize(_In_ std::wstring deviceId, _In_ EDataFlow flow);
	HRESULT StartCapture();
	HRESULT StopCapture();
	void ReturnAudioBytesToBuffer(std::vector<BYTE> bytes);
	void SetDefaultDevice(EDataFlow flow, ERole role, LPCWSTR id);
	void SetOffline(bool isOffline);
	inline EDataFlow GetFlow() { return m_Flow; }
	inline std::wstring GetTag() { return m_Tag; }
	inline std::wstring GetDeviceName() { return m_DeviceName; }
	inline std::wstring GetDeviceId() { return m_DeviceId; }

private:
	const long AUDIO_CLIENT_BUFFER_100_NS = 200 * 10000;
	HRESULT GetWaveFormat(
		_In_ IAudioClient *pAudioClient,
		_In_ bool bInt16,
		_Out_ WAVEFORMATEX **ppWaveFormat);
	HRESULT InitializeAudioClient(
		_In_ IMMDevice *pMMDevice,
		_Outptr_ IAudioClient **ppAudioClient);

	HRESULT InitializeResampler(
		_In_ UINT32 samplerate,
		_In_ UINT32 nChannels,
		_In_ IAudioClient *pAudioClient,
		_Out_ WWMFPcmFormat *pInputFormat,
		_Out_ WWMFPcmFormat *pOutputFormat,
		_Outptr_result_maybenull_ WWMFResampler **ppResampler);

	HRESULT StartCaptureLoop(
		_In_ IAudioClient *pAudioClient,
		_In_ HANDLE hStartedEvent,
		_In_ HANDLE hStopEvent,
		_In_ HANDLE hRestartEvent
	);

	bool StartListeners();
	bool StopListeners();

	HRESULT StopReconnectThread();

	struct TaskWrapper;
	std::unique_ptr<TaskWrapper> m_TaskWrapperImpl;
	std::wstring m_DefaultDeviceId;
	std::wstring m_DeviceId;
	std::wstring m_DeviceName;
	std::wstring m_Tag;
	EDataFlow m_Flow;
	DynamicWait m_RetryWait;

	bool m_IsRegisteredForEndpointNotifications = false;
	bool m_IsDefaultDevice = false;
	std::atomic<bool> m_IsCapturing = false;
	std::atomic<bool> m_IsOffline = false;
	std::vector<BYTE> m_OverflowBytes = {};
	std::vector<BYTE> m_RecordedBytes = {};
	HANDLE m_CaptureStartedEvent = nullptr;
	HANDLE m_CaptureStopEvent = nullptr;
	HANDLE m_CaptureRestartEvent = nullptr;
	HANDLE m_CaptureReconnectEvent = nullptr;
	HANDLE m_ReconnectThreadStopEvent = nullptr;

	HRESULT ReconnectThreadLoop();

	CComPtr<IMMDeviceEnumerator> m_pEnumerator;
	CComPtr<IAudioClient> m_AudioClient;
	std::unique_ptr<WWMFResampler> m_Resampler;
	WWMFPcmFormat m_InputFormat;
	WWMFPcmFormat m_OutputFormat;

	std::shared_ptr<AUDIO_OPTIONS> m_AudioOptions;
};

