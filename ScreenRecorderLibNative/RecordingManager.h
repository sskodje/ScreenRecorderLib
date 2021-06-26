#pragma once
#define _CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>
#include <windows.h>
#include <queue>
#include <Codecapi.h>
#include <atlbase.h>
#include <winnt.h>
#include <wincodec.h>
#include <chrono>
#include <iostream>
#include <dxgi1_2.h>
#include <d3d11.h>
#include <mfapi.h>
#include <vector>
#include <map>
#include <evr.h>
#include <DirectXMath.h>
#include <mfreadwrite.h>
#include <winrt/Windows.Graphics.Capture.h>
#include <winrt/Windows.Foundation.Metadata.h>

#include "WindowsGraphicsCapture.h"
#include "WindowsGraphicsCapture.util.h"
#include "DesktopDuplicationCapture.h"
#include "fifo_map.h"
#include "AudioPrefs.h"
#include "MouseManager.h"
#include "AudioManager.h"
#include "Log.h"
#include "Util.h"
#include "HighresTimer.h"

typedef void(__stdcall *CallbackCompleteFunction)(std::wstring, nlohmann::fifo_map<std::wstring, int>);
typedef void(__stdcall *CallbackStatusChangedFunction)(int);
typedef void(__stdcall *CallbackErrorFunction)(std::wstring);
typedef void(__stdcall *CallbackSnapshotFunction)(std::wstring);

#define MODE_VIDEO 0
#define MODE_SLIDESHOW 1
#define MODE_SCREENSHOT 2

#define STATUS_IDLE 0
#define STATUS_RECORDING 1
#define STATUS_PAUSED 2
#define STATUS_FINALIZING 3

#define API_DESKTOP_DUPLICATION 0
#define API_GRAPHICS_CAPTURE 1

class CMFSinkWriterCallback : public IMFSinkWriterCallback {

public:
	CMFSinkWriterCallback(HANDLE hFinalizeEvent, HANDLE hMarkerEvent) :
		m_nRefCount(0),
		m_hFinalizeEvent(hFinalizeEvent),
		m_hMarkerEvent(hMarkerEvent) {}
	virtual ~CMFSinkWriterCallback()
	{
	}
	// IMFSinkWriterCallback methods
	STDMETHODIMP OnFinalize(HRESULT hrStatus) {
		LOG_DEBUG(L"CMFSinkWriterCallback::OnFinalize");
		if (m_hFinalizeEvent != NULL) {
			SetEvent(m_hFinalizeEvent);
		}
		return hrStatus;
	}

	STDMETHODIMP OnMarker(DWORD dwStreamIndex, LPVOID pvContext) {
		LOG_DEBUG(L"CMFSinkWriterCallback::OnMarker");
		if (m_hMarkerEvent != NULL) {
			SetEvent(m_hMarkerEvent);
		}
		return S_OK;
	}

	// IUnknown methods
	STDMETHODIMP QueryInterface(REFIID riid, void **ppv) {
		static const QITAB qit[] = {
			QITABENT(CMFSinkWriterCallback, IMFSinkWriterCallback),
		{0}
		};
		return QISearch(this, qit, riid, ppv);
	}

	STDMETHODIMP_(ULONG) AddRef() {
		return InterlockedIncrement(&m_nRefCount);
	}

	STDMETHODIMP_(ULONG) Release() {
		ULONG refCount = InterlockedDecrement(&m_nRefCount);
		if (refCount == 0) {
			delete this;
		}
		return refCount;
	}

private:
	volatile long m_nRefCount;
	HANDLE m_hFinalizeEvent;
	HANDLE m_hMarkerEvent;
};

struct FrameWriteModel
{
	INT FrameNumber;
	//Timestamp of the start of the frame, in 100 nanosecond units.
	INT64 StartPos;
	//Duration of the frame, in 100 nanosecond units.
	INT64 Duration;
	std::vector<BYTE> Audio;
	CComPtr<ID3D11Texture2D> Frame;
};

class RecordingManager
{
public:
	RecordingManager();
	~RecordingManager();
	CallbackErrorFunction RecordingFailedCallback;
	CallbackCompleteFunction RecordingCompleteCallback;
	CallbackStatusChangedFunction RecordingStatusChangedCallback;
	CallbackSnapshotFunction RecordingSnapshotCreatedCallback;
	HRESULT BeginRecording(_In_opt_ std::wstring path);
	HRESULT BeginRecording(_In_opt_ std::wstring path, _In_opt_ IStream *stream);
	HRESULT BeginRecording(_In_opt_ IStream *stream);
	void EndRecording();
	void PauseRecording();
	void ResumeRecording();



	void SetDestRectangle(RECT rect) {
		m_DestRect = MakeRectEven(rect);
	}

	void SetTakeSnapshotsWithVideo(bool isEnabled) { m_TakesSnapshotsWithVideo = isEnabled; }
	void SetSnapshotsWithVideoInterval(UINT32 value) { m_SnapshotsWithVideoInterval = std::chrono::milliseconds(value); }
	void SetSnapshotDirectory(std::wstring string) { m_OutputSnapshotsFolderPath = string; }
	static bool SetExcludeFromCapture(HWND hwnd, bool isExcluded);
	void SetRecordingSources(std::vector<RECORDING_SOURCE> sources) { m_RecordingSources = sources; }
	void SetRecorderMode(UINT32 mode) { m_RecorderMode = mode; }
	void SetRecorderApi(UINT32 api) { m_RecorderApi = api; }


	void SetSnapshotSaveFormat(GUID value) { m_ImageEncoderFormat = value; }
	void SetIsLogEnabled(bool value);
	void SetLogFilePath(std::wstring value);
	void SetLogSeverityLevel(int value);
	void SetOverlays(std::vector<RECORDING_OVERLAY> overlays) { m_Overlays = overlays; };
	void SetEncoderOptions(ENCODER_OPTIONS *options) { m_EncoderOptions.reset(options); }
	std::shared_ptr<ENCODER_OPTIONS> GetEncoderOptions() { return m_EncoderOptions; }
	void SetAudioOptions(AUDIO_OPTIONS *options) { m_AudioOptions.reset(options); }
	std::shared_ptr<AUDIO_OPTIONS> GetAudioOptions() { return m_AudioOptions; }
	void SetMouseOptions(MOUSE_OPTIONS *options) { m_MouseOptions.reset(options); }
	std::shared_ptr<MOUSE_OPTIONS> GetMouseOptions() { return m_MouseOptions; }
	bool IsRecording();

private:
	struct TaskWrapper;
	std::unique_ptr<TaskWrapper> m_TaskWrapperImpl;

#if _DEBUG 
	ID3D11Debug *m_Debug = nullptr;
#endif
	ID3D11DeviceContext *m_ImmediateContext = nullptr;
	IMFSinkWriter *m_SinkWriter = nullptr;
	ID3D11Device *m_Device = nullptr;

	bool m_LastFrameHadAudio = false;
	HRESULT m_EncoderResult = S_FALSE;


	std::wstring m_OutputFolder = L"";
	std::wstring m_OutputFullPath = L"";
	std::wstring m_OutputSnapshotsFolderPath = L"";
	nlohmann::fifo_map<std::wstring, int> m_FrameDelays;
	DWORD m_VideoStreamIndex = 0;
	DWORD m_AudioStreamIndex = 0;
	HANDLE m_FinalizeEvent = nullptr;
	std::chrono::steady_clock::time_point m_previousSnapshotTaken = (std::chrono::steady_clock::time_point::min)();
	INT64 m_MaxFrameLength100Nanos = MillisToHundredNanos(500); //500 milliseconds in 100 nanoseconds measure.

	UINT32 m_RecorderMode = MODE_VIDEO;
	UINT32 m_RecorderApi = API_DESKTOP_DUPLICATION;
	std::vector<RECORDING_SOURCE> m_RecordingSources{};
	std::vector<RECORDING_OVERLAY> m_Overlays;
	RECT m_DestRect = { 0,0,0,0 };
	bool m_IsPaused = false;
	bool m_IsRecording = false;

	std::shared_ptr<ENCODER_OPTIONS> m_EncoderOptions;
	std::shared_ptr<AUDIO_OPTIONS> m_AudioOptions;
	std::shared_ptr<MOUSE_OPTIONS> m_MouseOptions;
	std::chrono::milliseconds m_SnapshotsWithVideoInterval = std::chrono::milliseconds(10000);
	bool m_TakesSnapshotsWithVideo = false;
	GUID m_ImageEncoderFormat = GUID_ContainerFormatPng;

	std::string CurrentTimeToFormattedString();
	bool CheckDependencies(_Out_ std::wstring *error);
	ScreenCaptureBase *CreateCaptureSession();

	std::wstring GetImageExtension();
	std::wstring GetVideoExtension();
	inline bool IsSnapshotsWithVideoEnabled() { return (m_RecorderMode == MODE_VIDEO) && m_TakesSnapshotsWithVideo; }
	inline bool IsTimeToTakeSnapshot()
	{
		// The first condition is needed since (now - min) yields negative value because of overflow...
		return m_previousSnapshotTaken == (std::chrono::steady_clock::time_point::min)() ||
			(std::chrono::steady_clock::now() - m_previousSnapshotTaken) > m_SnapshotsWithVideoInterval;
	}
	HRESULT FinalizeRecording();
	void CleanupResourcesAndShutDownMF();
	void SetRecordingCompleteStatus(_In_ HRESULT hr);
	HRESULT StartRecorderLoop(_In_ std::vector<RECORDING_SOURCE> sources, _In_ std::vector<RECORDING_OVERLAY> overlays, _In_opt_ IStream *pStream);
	HRESULT RenderFrame(_In_ FrameWriteModel &model);
	HRESULT ConfigureOutputDir(_In_ std::wstring path);
	HRESULT InitializeRects(_In_ RECT outputRect, _Out_ RECT *pSourceRect, _Out_ RECT *pDestRect);
	HRESULT InitializeVideoSinkWriter(_In_ std::wstring path, _In_opt_ IMFByteStream *pOutStream, _In_ ID3D11Device *pDevice, _In_ RECT sourceRect, _In_ RECT destRect, _In_ DXGI_MODE_ROTATION rotation, _In_ IMFSinkWriterCallback *pCallback, _Outptr_ IMFSinkWriter **ppWriter, _Out_ DWORD *pVideoStreamIndex, _Out_ DWORD *pAudioStreamIndex);
	HRESULT ConfigureOutputMediaTypes(_In_ UINT destWidth, _In_ UINT destHeight, _Outptr_ IMFMediaType **pVideoMediaTypeOut, _Outptr_result_maybenull_ IMFMediaType **pAudioMediaTypeOut);
	HRESULT ConfigureInputMediaTypes(_In_ UINT sourceWidth, _In_ UINT sourceHeight, _In_ MFVideoRotationFormat rotationFormat, _In_ IMFMediaType *pVideoMediaTypeOut, _Outptr_ IMFMediaType **pVideoMediaTypeIn, _Outptr_result_maybenull_ IMFMediaType **pAudioMediaTypeIn);

	HRESULT WriteFrameToVideo(_In_ INT64 frameStartPos, _In_ INT64 frameDuration, _In_ DWORD streamIndex, _In_ ID3D11Texture2D *pAcquiredDesktopImage);
	HRESULT WriteFrameToImage(_In_ ID3D11Texture2D *pAcquiredDesktopImage, _In_ std::wstring filePath);
	void WriteFrameToImageAsync(_In_ ID3D11Texture2D *pAcquiredDesktopImage, _In_ std::wstring filePath);
	HRESULT TakeSnapshotsWithVideo(_In_ ID3D11Texture2D *frame, _In_ RECT destRect);
	HRESULT WriteAudioSamplesToVideo(_In_ INT64 frameStartPos, _In_ INT64 frameDuration, _In_ DWORD streamIndex, _In_ BYTE *pSrc, _In_ DWORD cbData);
	HRESULT SetAttributeU32(_Inout_ ATL::CComPtr<ICodecAPI> &codec, _In_ const GUID &guid, _In_ UINT32 value);
	HRESULT CreateInputMediaTypeFromOutput(_In_ IMFMediaType *pType, _In_ const GUID &subtype, _Outptr_ IMFMediaType **ppType);
	HRESULT CropFrame(_In_ ID3D11Texture2D *frame, _In_ RECT destRect, _Outptr_ ID3D11Texture2D **pCroppedFrame);
	HRESULT GetVideoProcessor(_In_ IMFSinkWriter *pSinkWriter, _In_ DWORD streamIndex, _Outptr_ IMFVideoProcessorControl **pVideoProcessor);
};