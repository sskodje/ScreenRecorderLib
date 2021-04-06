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

#include "graphics_capture.h"
#include "graphics_capture.util.h"
#include "duplication_capture.h"
#include "fifo_map.h"
#include "audio_prefs.h"
#include "mouse_pointer.h"
#include "log.h"
#include "utilities.h"
#include "string_format.h"
#include "highres_timer.h"

typedef void(__stdcall *CallbackCompleteFunction)(std::wstring, nlohmann::fifo_map<std::wstring, int>);
typedef void(__stdcall *CallbackStatusChangedFunction)(int);
typedef void(__stdcall *CallbackErrorFunction)(std::wstring);
typedef void(__stdcall *CallbackSnapshotFunction)(std::wstring);

#define MODE_VIDEO 0
#define MODE_SLIDESHOW 1
#define MODE_SNAPSHOT 2

#define STATUS_IDLE 0
#define STATUS_RECORDING 1
#define STATUS_PAUSED 2
#define STATUS_FINALIZING 3

#define MOUSE_DETECTION_MODE_POLLING 0
#define MOUSE_DETECTION_MODE_HOOK 1

#define API_DESKTOP_DUPLICATION 0
#define API_GRAPHICS_CAPTURE 1

class loopback_capture;

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

LRESULT CALLBACK MouseHookProc(int nCode, WPARAM wParam, LPARAM lParam);

class internal_recorder
{
public:
	internal_recorder();
	~internal_recorder();
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

	void SetVideoFps(UINT32 fps) { m_VideoFps = fps; }
	void SetVideoBitrate(UINT32 bitrate) { m_VideoBitrate = bitrate; }
	void SetVideoQuality(UINT32 quality) { m_VideoQuality = quality; }
	void SetVideoBitrateMode(UINT32 mode) { m_VideoBitrateControlMode = mode; }
	void SetAudioBitrate(UINT32 bitrate) { m_AudioBitrate = bitrate; }
	void SetAudioChannels(UINT32 channels) { m_AudioChannels = channels; }
	void SetOutputDevice(std::wstring string) { m_AudioOutputDevice = string; }
	void SetInputDevice(std::wstring string) { m_AudioInputDevice = string; }
	void SetAudioEnabled(bool value) { m_IsAudioEnabled = value; }
	void SetOutputDeviceEnabled(bool value) { m_IsOutputDeviceEnabled = value; }
	void SetInputDeviceEnabled(bool value) { m_IsInputDeviceEnabled = value; }
	void SetMousePointerEnabled(bool value) { m_IsMousePointerEnabled = value; }
	void SetDestRectangle(RECT rect) {
		m_DestRect = MakeRectEven(rect);
	}
	void SetInputVolume(float volume) { m_InputVolumeModifier = volume; }
	void SetOutputVolume(float volume) { m_OutputVolumeModifier = volume; }
	void SetTakeSnapshotsWithVideo(bool isEnabled) { m_TakesSnapshotsWithVideo = isEnabled; }
	void SetSnapshotsWithVideoInterval(UINT32 value) { m_SnapshotsWithVideoInterval = std::chrono::seconds(value); }
	void SetSnapshotDirectory(std::wstring string) { m_OutputSnapshotsFolderPath = string; }
	static bool SetExcludeFromCapture(HWND hwnd, bool isExcluded);
	void SetDisplayOutput(std::wstring output) { m_DisplayOutputDevices = { output }; }
	void SetDisplayOutput(std::vector<std::wstring> outputs) { m_DisplayOutputDevices = outputs; }
	void SetWindowHandles(std::vector<HWND> handles) { m_WindowHandles = handles; }
	void SetRecorderMode(UINT32 mode) { m_RecorderMode = mode; }
	void SetRecorderApi(UINT32 api) { m_RecorderApi = api; }
	void SetFixedFramerate(bool value) { m_IsFixedFramerate = value; }
	void SetIsThrottlingDisabled(bool value) { m_IsThrottlingDisabled = value; }
	void SetH264EncoderProfile(UINT32 value) { m_H264Profile = value; }
	void SetIsFastStartEnabled(bool value) { m_IsMp4FastStartEnabled = value; }
	void SetIsFragmentedMp4Enabled(bool value) { m_IsFragmentedMp4Enabled = value; }
	void SetIsHardwareEncodingEnabled(bool value) { m_IsHardwareEncodingEnabled = value; }
	void SetIsLowLatencyModeEnabled(bool value) { m_IsLowLatencyModeEnabled = value; }
	void SetDetectMouseClicks(bool value) { m_IsMouseClicksDetected = value; }
	void SetMouseClickDetectionLMBColor(std::string value) { m_MouseClickDetectionLMBColor = value; }
	void SetMouseClickDetectionRMBColor(std::string value) { m_MouseClickDetectionRMBColor = value; }
	void SetMouseClickDetectionRadius(int value) { m_MouseClickDetectionRadius = value; }
	void SetMouseClickDetectionDuration(int value);
	void SetMouseClickDetectionMode(UINT32 value) { m_MouseClickDetectionMode = value; }
	void SetSnapshotSaveFormat(GUID value) { m_ImageEncoderFormat = value; }
	void SetIsLogEnabled(bool value);
	void SetLogFilePath(std::wstring value);
	void SetLogSeverityLevel(int value);
	void SetOverlays(std::vector<RECORDING_OVERLAY> overlays) { m_Overlays = overlays; };
private:
	// Format constants
	const GUID   VIDEO_ENCODING_FORMAT = MFVideoFormat_H264;
	const GUID	 AUDIO_ENCODING_FORMAT = MFAudioFormat_AAC;
	const UINT32 AUDIO_BITS_PER_SAMPLE = 16; //Audio bits per sample must be 16.
	const UINT32 AUDIO_SAMPLES_PER_SECOND = 48000;//Audio samples per seconds must be 44100 or 48000.
	const GUID   VIDEO_INPUT_FORMAT = MFVideoFormat_ARGB32;

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
	HHOOK m_Mousehook;

	std::wstring m_OutputFolder = L"";
	std::wstring m_OutputFullPath = L"";
	std::wstring m_OutputSnapshotsFolderPath = L"";
	nlohmann::fifo_map<std::wstring, int> m_FrameDelays;
	DWORD m_VideoStreamIndex = 0;
	DWORD m_AudioStreamIndex = 0;
	HANDLE m_FinalizeEvent = nullptr;
	//Config
	UINT32 m_MaxFrameLength100Nanos = MillisToHundredNanos(500); //500 milliseconds in 100 nanoseconds measure.
	UINT32 m_RecorderMode = MODE_VIDEO;
	UINT32 m_RecorderApi = API_DESKTOP_DUPLICATION;
	std::vector<std::wstring> m_DisplayOutputDevices = {};
	std::vector<HWND> m_WindowHandles = {};
	RECT m_DestRect = { 0,0,0,0 };
	std::wstring m_AudioOutputDevice = L"";
	std::wstring m_AudioInputDevice = L"";
	UINT32 m_VideoFps = 30;
	UINT32 m_VideoBitrate = 4000 * 1000;//Bitrate in bits per second
	UINT32 m_VideoQuality = 70;//Video quality from 1 to 100. Is only used with eAVEncCommonRateControlMode_Quality.
	UINT32 m_H264Profile = eAVEncH264VProfile_Main; //Supported H264 profiles for the encoder are Baseline, Main and High.
	UINT32 m_AudioBitrate = (96 / 8) * 1000; //Bitrate in bytes per second. Only 96,128,160 and 192kbps is supported.
	UINT32 m_AudioChannels = 2; //Number of audio channels. 1,2 and 6 is supported. 6 only on windows 8 and up.
	UINT32 m_VideoBitrateControlMode = eAVEncCommonRateControlMode_Quality;
	std::chrono::seconds m_SnapshotsWithVideoInterval = std::chrono::seconds(10);
	bool m_IsMousePointerEnabled = true;
	bool m_IsAudioEnabled = false;
	bool m_IsOutputDeviceEnabled = true;
	bool m_IsInputDeviceEnabled = true;
	bool m_IsFixedFramerate = false;
	bool m_IsThrottlingDisabled = false;
	bool m_IsLowLatencyModeEnabled = false;
	bool m_IsMp4FastStartEnabled = true;
	bool m_IsFragmentedMp4Enabled = false;
	bool m_IsHardwareEncodingEnabled = true;
	bool m_IsPaused = false;
	bool m_IsRecording = false;
	bool m_IsMouseClicksDetected = false;
	bool m_TakesSnapshotsWithVideo = false;
	std::string m_MouseClickDetectionLMBColor = "#FFFF00";
	std::string m_MouseClickDetectionRMBColor = "#FFFF00";
	UINT32 m_MouseClickDetectionRadius = 20;
	UINT32 m_MouseClickDetectionMode = MOUSE_DETECTION_MODE_POLLING;
	GUID m_ImageEncoderFormat = GUID_ContainerFormatPng;
	float m_InputVolumeModifier = 1;
	float m_OutputVolumeModifier = 1;
	std::chrono::steady_clock::time_point m_previousSnapshotTaken = (std::chrono::steady_clock::time_point::min)();
	std::vector<RECORDING_OVERLAY> m_Overlays;

	//functions
	std::string CurrentTimeToFormattedString();
	bool CheckDependencies(_Out_ std::wstring *error);
	capture_base *CreateCaptureSession();
	std::vector<RECORDING_SOURCE> CreateRecordingSources();
	std::vector<RECORDING_OVERLAY> CreateRecordingOverlays();
	std::vector<BYTE> GrabAudioFrame(_In_opt_ std::unique_ptr<loopback_capture> &pLoopbackCaptureOutputDevice, _In_opt_ std::unique_ptr<loopback_capture> &pLoopbackCaptureInputDevice);
	std::vector<BYTE> MixAudio(_In_ std::vector<BYTE> const &first, _In_ std::vector<BYTE> const &second, _In_ float firstVolume, _In_ float secondVolume);
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
	HRESULT InitializeDx(_Outptr_ ID3D11DeviceContext **ppContext, _Outptr_ ID3D11Device **ppDevice, _Outptr_opt_result_maybenull_ ID3D11Debug **ppDebug);
	HRESULT InitializeVideoSinkWriter(_In_ std::wstring path, _In_opt_ IMFByteStream *pOutStream, _In_ ID3D11Device *pDevice, _In_ RECT sourceRect, _In_ RECT destRect, _In_ DXGI_MODE_ROTATION rotation, _In_ IMFSinkWriterCallback *pCallback, _Outptr_ IMFSinkWriter **ppWriter, _Out_ DWORD *pVideoStreamIndex, _Out_ DWORD *pAudioStreamIndex);
	HRESULT ConfigureOutputMediaTypes(_In_ UINT destWidth, _In_ UINT destHeight, _Outptr_ IMFMediaType **pVideoMediaTypeOut, _Outptr_result_maybenull_ IMFMediaType **pAudioMediaTypeOut);
	HRESULT ConfigureInputMediaTypes(_In_ UINT sourceWidth, _In_ UINT sourceHeight, _In_ MFVideoRotationFormat rotationFormat, _In_ IMFMediaType *pVideoMediaTypeOut, _Outptr_ IMFMediaType **pVideoMediaTypeIn, _Outptr_result_maybenull_ IMFMediaType **pAudioMediaTypeIn);
	HRESULT InitializeAudioCapture(_Outptr_result_maybenull_ loopback_capture **outputAudioCapture, _Outptr_result_maybenull_ loopback_capture **inputAudioCapture);
	void InitializeMouseClickDetection();
	HRESULT WriteFrameToVideo(_In_ INT64 frameStartPos, _In_ INT64 frameDuration, _In_ DWORD streamIndex, _In_ ID3D11Texture2D *pAcquiredDesktopImage);
	HRESULT WriteFrameToImage(_In_ ID3D11Texture2D *pAcquiredDesktopImage, _In_ std::wstring filePath);
	void WriteFrameToImageAsync(_In_ ID3D11Texture2D *pAcquiredDesktopImage, _In_ std::wstring filePath);
	HRESULT TakeSnapshotsWithVideo(_In_ ID3D11Texture2D *frame, _In_ RECT destRect);
	HRESULT WriteAudioSamplesToVideo(_In_ INT64 frameStartPos, _In_ INT64 frameDuration, _In_ DWORD streamIndex, _In_ BYTE *pSrc, _In_ DWORD cbData);
	HRESULT SetAttributeU32(_Inout_ ATL::CComPtr<ICodecAPI> &codec, _In_ const GUID &guid, _In_ UINT32 value);
	HRESULT CreateInputMediaTypeFromOutput(_In_ IMFMediaType *pType, _In_ const GUID &subtype, _Outptr_ IMFMediaType **ppType);
	HRESULT DrawMousePointer(_In_ ID3D11Texture2D *frame, _In_ mouse_pointer *pointer, _In_ PTR_INFO *ptrInfo, _In_ INT64 durationSinceLastFrame100Nanos);
	HRESULT CropFrame(_In_ ID3D11Texture2D *frame, _In_ RECT destRect, _Outptr_ ID3D11Texture2D **pCroppedFrame);
	HRESULT GetVideoProcessor(_In_ IMFSinkWriter *pSinkWriter, _In_ DWORD streamIndex, _Outptr_ IMFVideoProcessorControl **pVideoProcessor);
};