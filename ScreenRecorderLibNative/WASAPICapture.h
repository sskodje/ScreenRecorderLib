//https://github.com/mvaneerde/blog/tree/master/loopback-capture
#pragma once
#include "WWMFResampler.h"
#include "Log.h"
#include "CommonTypes.h"
#include "DynamicWait.h"
#include "AudioClientContext.h"
#include <windows.h>
#include <avrt.h>
#include <thread>
#include <stdio.h>
#include <audioclient.h>
#include <vector>
#include <functional>
#include <atlbase.h>
#include <initguid.h>
#include <mmdeviceapi.h>
#include <mutex>
#include <deque>

#pragma comment(lib, "avrt.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "winmm.lib")

static constexpr short SILENCE_THRESHOLD = 1;

struct AudioPacket
{
	std::vector<BYTE> data;
	UINT32 frameCount;
	LONGLONG timestamp100ns;


	AudioPacket() = default;

	AudioPacket(const std::vector<BYTE> &data, const UINT32 &frameCount, const LONGLONG &timestamp100ns)
		: data(data), frameCount(frameCount), timestamp100ns(timestamp100ns)
	{

	}
};

class WASAPICapture
{
public:
	WASAPICapture(_In_ std::shared_ptr<AUDIO_OPTIONS> &audioOptions, _In_ AUDIO_SOURCE *source);
	~WASAPICapture();
	void ClearRecordedBytes();
	bool IsCapturing();
	bool IsPaused();
	inline bool IsDefaultDevice() const { return m_IsDefaultDevice; }
	int GetNextFrameCount();
	INT64 WASAPICapture::GetQueuedDuration100Nanos();
	std::vector<BYTE> PeakRecordedBytes();
	std::vector<BYTE> GetRecordedBytesByDuration(UINT64 duration100Nanos, _Out_ UINT64 *qpcTimestamp);
	std::vector<BYTE> GetRecordedBytesByFrameCount(int requestedFrameCount, _Out_ UINT64 *qpcTimestamp);
	HRESULT Initialize(_In_ std::wstring endpointID, _In_ AudioClientKind kind);
	HRESULT StartCapture();
	HRESULT StopCapture();
	HRESULT PauseCapture();
	HRESULT ResumeCapture();
	void SetDefaultDevice(EDataFlow flow, ERole role, LPCWSTR id);
	void SetOffline(bool isOffline);
	inline EDataFlow GetFlow() const { return m_Flow; }
	inline std::wstring GetDeviceFriendlyName() const { return m_DeviceFriendlyName; }
	inline std::wstring GetDeviceName() const { return m_DeviceName; }
	inline WWMFPcmFormat GetInputFormat() const { return m_InputFormat; }
	inline AUDIO_SOURCE *GetAudioCaptureSource() { return m_AudioCaptureSource; }
	inline bool NeedSync() const { return m_NeedSync; }
	inline static std::mutex StaticMutex{};

private:
	const long AUDIO_CLIENT_BUFFER_100_NS = 200 * 10000;
	HRESULT GetWaveFormat(
		_In_ AudioClientKind kind,
		_In_ CComPtr<IAudioClient> client,
		_In_ bool bInt16,
		_Out_ WAVEFORMATEX **ppWaveFormat) const;
	HRESULT InitializeAudioClient(
		_In_ std::wstring deviceId,
		_In_ AudioClientKind kind,
		_Outptr_ AudioClientContext **ppAudioClient);

	HRESULT InitializeResampler(
		_In_ UINT32 samplerate,
		_In_ UINT32 nChannels,
		_In_ AudioClientContext *pAudioClientContext,
		_Out_ WWMFPcmFormat *pInputFormat,
		_Out_ WWMFPcmFormat *pOutputFormat,
		_Outptr_result_maybenull_ WWMFResampler **ppResampler);

	HRESULT StartCaptureLoop(
		_In_ AudioClientContext *pAudioClientContext,
		_In_ HANDLE hStartedEvent,
		_In_ HANDLE hStopEvent,
		_In_ HANDLE hRestartEvent
	);

	bool StartListeners();
	bool StopListeners();

	HRESULT StopReconnectThread();

	struct TaskWrapper;
	std::unique_ptr<TaskWrapper> m_TaskWrapperImpl;
	std::wstring m_DefaultDeviceName;
	std::wstring m_DeviceName;
	std::wstring m_DeviceFriendlyName;
	EDataFlow m_Flow;
	AudioClientKind m_Kind;
	DynamicWait m_RetryWait;

	bool m_IsRegisteredForEndpointNotifications = false;
	bool m_IsDefaultDevice = false;
	bool m_NeedSync = false;
	std::atomic<bool> m_IsPaused = false;
	std::atomic<bool> m_IsCapturing = false;
	std::atomic<bool> m_IsOffline = false;
	std::deque<AudioPacket> m_RecordedAudioPackets = {};
	HANDLE m_CaptureStartedEvent = nullptr;
	HANDLE m_CaptureStopEvent = nullptr;
	HANDLE m_CaptureRestartEvent = nullptr;
	HANDLE m_CaptureReconnectEvent = nullptr;
	HANDLE m_ReconnectThreadStopEvent = nullptr;

	HRESULT ReconnectThreadLoop();

	CComPtr<IMMDeviceEnumerator> m_pEnumerator;
	std::unique_ptr<AudioClientContext> m_AudioClientContext;
	std::unique_ptr<WWMFResampler> m_Resampler;
	WWMFPcmFormat m_InputFormat;
	WWMFPcmFormat m_OutputFormat;

	AUDIO_OPTIONS *m_AudioOptions;
	AUDIO_SOURCE *m_AudioCaptureSource;
};

