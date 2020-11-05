#pragma once
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
#include "fifo_map.h"
#include "audio_prefs.h"
#include "mouse_pointer.h"
#include "log.h"
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
	CMFSinkWriterCallback(HANDLE hFinalizeEvent, HANDLE hMarkerEvent) : m_nRefCount(1), m_hFinalizeEvent(hFinalizeEvent), m_hMarkerEvent(hMarkerEvent){}
	virtual ~CMFSinkWriterCallback() {}
	// IMFSinkWriterCallback methods
	STDMETHODIMP OnFinalize(HRESULT hrStatus) {
		DEBUG(L"CMFSinkWriterCallback::OnFinalize");
		if (m_hFinalizeEvent != NULL) {
			SetEvent(m_hFinalizeEvent);
		}
		return hrStatus;
	}

	STDMETHODIMP OnMarker(DWORD dwStreamIndex, LPVOID pvContext) {
		DEBUG(L"CMFSinkWriterCallback::OnMarker");
		if (m_hMarkerEvent != NULL) {
			SetEvent(m_hMarkerEvent);
		}
		return S_OK;
	}

	// IUnknown methods
	STDMETHODIMP QueryInterface(REFIID riid, void** ppv) {
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

typedef struct
{
	INT FrameNumber;
	//Timestamp of the start of the frame, in 100 nanosecond units.
	INT64 StartPos;
	//Duration of the frame, in 100 nanosecond units.
	INT64 Duration;
	std::vector<BYTE> Audio;
	CComPtr<ID3D11Texture2D> Frame;
}FrameWriteModel;

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
	HRESULT BeginRecording(std::wstring path);
	HRESULT BeginRecording(std::wstring path, IStream * stream);
	HRESULT BeginRecording(IStream *stream);
	std::vector<ATL::CComPtr<IDXGIAdapter>> EnumDisplayAdapters();
	void EndRecording();
	void PauseRecording();
	void ResumeRecording();

	void SetVideoFps(UINT32 fps) { m_VideoFps = fps; }
	void SetVideoBitrate(UINT32 bitrate) { m_VideoBitrate = bitrate; }
	void SetVideoQuality(UINT32 quality) { m_VideoQuality = quality; }
	void SetVideoBitrateMode(UINT32 mode) { m_VideoBitrateControlMode = mode; }
	void SetAudioBitrate(UINT32 bitrate) { m_AudioBitrate = bitrate; }
	void SetAudioChannels(UINT32 channels) { m_AudioChannels = channels; }
	void SetOutputDevice(std::wstring& string) { m_AudioOutputDevice = string; }
	void SetInputDevice(std::wstring& string) { m_AudioInputDevice = string; }
	void SetAudioEnabled(bool value) { m_IsAudioEnabled = value; }
	void SetOutputDeviceEnabled(bool value) { m_IsOutputDeviceEnabled = value; }
	void SetInputDeviceEnabled(bool value) { m_IsInputDeviceEnabled = value; }
	void SetMousePointerEnabled(bool value) { m_IsMousePointerEnabled = value; }
	void SetDestRectangle(RECT rect) {
		m_DestRect = MakeRectEven(rect);
	}
	void SetInputVolume(float volume) { m_InputVolumeModifier = volume; }
	void SetOutputVolume(float volume) { m_OutputVolumeModifier = volume; }
	void SetTakeSnapthotsWithVideo(bool isEnabled) { m_TakesSnapshotsWithVideo = isEnabled; }
	void SetSnapthotsWithVideoInterval(UINT32 value) { m_SnapshotsWithVideoInterval = std::chrono::seconds(value); }

	[[deprecated]]
	void SetDisplayOutput(UINT32 output) { m_DisplayOutput = output; }
	void SetDisplayOutput(std::wstring output) { m_DisplayOutputName = output; }
	void SetWindowHandle(HWND handle) { m_WindowHandle = handle; }
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
private:
	// Format constants
	const GUID   VIDEO_ENCODING_FORMAT = MFVideoFormat_H264;
	const GUID	 AUDIO_ENCODING_FORMAT = MFAudioFormat_AAC;
	const UINT32 AUDIO_BITS_PER_SAMPLE = 16; //Audio bits per sample must be 16.
	const UINT32 AUDIO_SAMPLES_PER_SECOND = 44100;//Audio samples per seconds must be 44100.
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
	UINT32 m_MaxFrameLength100Nanos = 1000 * 1000 * 10; //1 second in 100 nanoseconds measure.
	UINT32 m_RecorderMode = MODE_VIDEO;
	UINT32 m_RecorderApi = API_DESKTOP_DUPLICATION;
	[[deprecated]]
	UINT m_DisplayOutput = 0; //Display output, where 0 is primary display.
	std::wstring m_DisplayOutputName = L""; //Display output device name, e.g. \\.\DISPLAY1
	HWND m_WindowHandle = nullptr;
	RECT m_DestRect = { 0,0,0,0 };
	std::wstring m_AudioOutputDevice = L"";
	std::wstring m_AudioInputDevice = L"";
	UINT32 m_VideoFps = 30;
	UINT32 m_VideoBitrate = 4000 * 1000;//Bitrate in bits per second
	UINT32 m_VideoQuality = 70;//Video quality from 1 to 100. Is only used with eAVEncCommonRateControlMode_Quality.
	UINT32 m_H264Profile = eAVEncH264VProfile_Main; //Supported H264 profiles for the encoder are Baseline, Main and High.
	UINT32 m_AudioBitrate = (96 / 8) * 1000; //Bitrate in bytes per second. Only 96,128,160 and 192kbps is supported.
	UINT32 m_AudioChannels = 2; //Number of audio channels. 1,2 and 6 is supported. 6 only on windows 8 and up.
	UINT32 m_InputAudioSamplesPerSecond = AUDIO_SAMPLES_PER_SECOND;
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
	//functions
	std::string CurrentTimeToFormattedString();
	std::vector<BYTE> GrabAudioFrame(std::unique_ptr<loopback_capture>& pLoopbackCaptureOutputDevice, std::unique_ptr<loopback_capture>& pLoopbackCaptureInputDevice);
	std::vector<BYTE> MixAudio(std::vector<BYTE> const &first, std::vector<BYTE> const &second, float firstVolume, float secondVolume);
	void SetDebugName(ID3D11DeviceChild* child, const std::string& name);
	void SetViewPort(ID3D11DeviceContext *deviceContext, UINT Width, UINT Height);
	RECT MakeRectEven(RECT rect);
	std::wstring GetImageExtension();
	std::wstring GetVideoExtension();
	bool IsSnapshotsWithVideoEnabled() { return (m_RecorderMode == MODE_VIDEO) && m_TakesSnapshotsWithVideo; }

	HRESULT StartGraphicsCaptureRecorderLoop(IStream *pStream);
	HRESULT StartDesktopDuplicationRecorderLoop(IStream *pStream, IDXGIOutput *pSelectedOutput);
	HRESULT RenderFrame(FrameWriteModel& model);
	HRESULT ConfigureOutputDir(std::wstring path);
	HRESULT InitializeDesc(DXGI_OUTDUPL_DESC outputDuplDesc, _Out_ D3D11_TEXTURE2D_DESC *pSourceFrameDesc, _Out_ D3D11_TEXTURE2D_DESC *pDestFrameDesc, _Out_ RECT *pSourceRect, _Out_ RECT *pDestRect);
	HRESULT InitializeDx(_In_opt_ IDXGIOutput *pDxgiOutput, _Outptr_ ID3D11DeviceContext **ppContext, _Outptr_ ID3D11Device **ppDevice);
	HRESULT InitializeDesktopDupl(_In_ ID3D11Device *pDevice, _In_opt_ IDXGIOutput *pDxgiOutput, _Outptr_ IDXGIOutputDuplication **ppDesktopDupl, _Out_ DXGI_OUTDUPL_DESC *pOutputDuplDesc);
	HRESULT InitializeVideoSinkWriter(std::wstring path, _In_opt_ IMFByteStream *pOutStream, _In_ ID3D11Device* pDevice, RECT sourceRect, RECT destRect, DXGI_MODE_ROTATION rotation, _In_ IMFSinkWriterCallback *pCallback, _Outptr_ IMFSinkWriter **ppWriter, _Out_ DWORD *pVideoStreamIndex, _Out_ DWORD *pAudioStreamIndex);
	HRESULT ConfigureOutputMediaTypes(_In_ UINT destWidth, _In_ UINT destHeight, _Outptr_ IMFMediaType **pVideoMediaTypeOut, _Outptr_ IMFMediaType **pAudioMediaTypeOut);
	HRESULT ConfigureInputMediaTypes(_In_ UINT sourceWidth, _In_ UINT sourceHeight, MFVideoRotationFormat rotationFormat, _In_ IMFMediaType *pVideoMediaTypeOut, _Outptr_ IMFMediaType **pVideoMediaTypeIn, _Outptr_ IMFMediaType **pAudioMediaTypeIn);
	HRESULT InitializeAudioCapture(_Outptr_ loopback_capture **outputAudioCapture, _Outptr_ loopback_capture **inputAudioCapture);
	void InitializeMouseClickDetection();
	HRESULT WriteFrameToVideo(INT64 frameStartPos, INT64 frameDuration, DWORD streamIndex, _In_ ID3D11Texture2D* pAcquiredDesktopImage);
	HRESULT WriteFrameToImage(_In_ ID3D11Texture2D* pAcquiredDesktopImage, std::wstring filePath);
	void WriteFrameToImageAsync(_In_ ID3D11Texture2D* pAcquiredDesktopImage, std::wstring filePath);
	HRESULT WriteAudioSamplesToVideo(INT64 frameStartPos, INT64 frameDuration, DWORD streamIndex, _In_ BYTE *pSrc, DWORD cbData);
	HRESULT GetOutputForDeviceName(std::wstring deviceName, _Outptr_opt_result_maybenull_ IDXGIOutput **adapter);
	HRESULT SetAttributeU32(_Inout_ ATL::CComPtr<ICodecAPI>& codec, const GUID& guid, UINT32 value);
	HRESULT CreateInputMediaTypeFromOutput(_In_ IMFMediaType *pType, const GUID& subtype, _Outptr_ IMFMediaType **ppType);
	HRESULT DrawMousePointer(ID3D11Texture2D *frame, mouse_pointer *pointer, mouse_pointer::PTR_INFO ptrInfo, DXGI_MODE_ROTATION screenRotation, INT64 durationSinceLastFrame100Nanos);
	ID3D11Texture2D* CropFrame(ID3D11Texture2D *frame, D3D11_TEXTURE2D_DESC frameDesc, RECT destRect);
	HRESULT CreateCaptureItem(_Out_ winrt::Windows::Graphics::Capture::GraphicsCaptureItem *captureItem);
	HRESULT GetVideoProcessor(IMFSinkWriter *pSinkWriter, DWORD streamIndex, IMFVideoProcessorControl **pVideoProcessor);
};