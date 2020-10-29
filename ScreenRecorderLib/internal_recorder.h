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
#include "fifo_map.h"
#include "audio_prefs.h"
#include "mouse_pointer.h"
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

class loopback_capture;

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
	void SetVideoFps(UINT32 fps);
	void SetVideoBitrate(UINT32 bitrate);
	void SetVideoQuality(UINT32 quality);
	void SetVideoBitrateMode(UINT32 mode);
	void SetAudioBitrate(UINT32 bitrate);
	void SetAudioChannels(UINT32 channels);
	void SetOutputDevice(std::wstring& string);
	void SetInputDevice(std::wstring& string);
	void SetAudioEnabled(bool value);
	void SetOutputDeviceEnabled(bool value);
	void SetInputDeviceEnabled(bool value);
	void SetMousePointerEnabled(bool value);
	void SetDestRectangle(RECT rect);
	void SetInputVolume(float volume);
	void SetOutputVolume(float volume);
	void SetTakeSnapthotsWithVideo(bool isEnabled);
	void SetSnapthotsWithVideoInterval(UINT32 value);
	
	[[deprecated]]
	void SetDisplayOutput(UINT32 output);
	void SetDisplayOutput(std::wstring output);
	void SetRecorderMode(UINT32 mode);
	void SetFixedFramerate(bool value);
	void SetIsThrottlingDisabled(bool value);
	void SetH264EncoderProfile(UINT32 value);
	void SetIsFastStartEnabled(bool value);
	void SetIsFragmentedMp4Enabled(bool value);
	void SetIsHardwareEncodingEnabled(bool value);
	void SetIsLowLatencyModeEnabled(bool value);
	void SetDetectMouseClicks(bool value);
	void SetMouseClickDetectionLMBColor(std::string value);
	void SetMouseClickDetectionRMBColor(std::string value);
	void SetMouseClickDetectionRadius(int value);
	void SetMouseClickDetectionDuration(int value);
	void SetMouseClickDetectionMode(UINT32 value);
	void SetSnapshotSaveFormat(GUID value);
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

	//Config
	UINT32 m_MaxFrameLength100Nanos = 1000 * 1000 * 10; //1 second in 100 nanoseconds measure.
	UINT32 m_RecorderMode = MODE_VIDEO;
	[[deprecated]]
	UINT m_DisplayOutput = 0; //Display output, where 0 is primary display.
	std::wstring m_DisplayOutputName = L""; //Display output device name, e.g. \\.\DISPLAY1
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
	std::vector<BYTE> GrabAudioFrame(loopback_capture *pLoopbackCaptureOutputDevice, loopback_capture *pLoopbackCaptureInputDevice);
	std::vector<BYTE> MixAudio(std::vector<BYTE> &first, std::vector<BYTE> &second, float firstVolume, float secondVolume);
	void SetDebugName(ID3D11DeviceChild* child, const std::string& name);
	void SetViewPort(ID3D11DeviceContext *deviceContext, UINT Width, UINT Height);
	std::wstring GetImageExtension();
	std::wstring GetVideoExtension();
	bool IsSnapshotsWithVideoEnabled() { return (m_RecorderMode == MODE_VIDEO) && m_TakesSnapshotsWithVideo; }
	HRESULT RenderFrame(FrameWriteModel& model);
	HRESULT ConfigureOutputDir(std::wstring path);
	HRESULT initializeDesc(DXGI_OUTDUPL_DESC outputDuplDesc, _Out_ D3D11_TEXTURE2D_DESC *pSourceFrameDesc, _Out_ D3D11_TEXTURE2D_DESC *pDestFrameDesc, _Out_ RECT *pSourceRect, _Out_ RECT *pDestRect);
	HRESULT InitializeDx(_In_opt_ IDXGIOutput *pDxgiOutput, _Outptr_ ID3D11DeviceContext **ppContext, _Outptr_ ID3D11Device **ppDevice);
	HRESULT InitializeDesktopDupl(_In_ ID3D11Device *pDevice, _In_opt_ IDXGIOutput *pDxgiOutput, _Outptr_ IDXGIOutputDuplication **ppDesktopDupl, _Out_ DXGI_OUTDUPL_DESC *pOutputDuplDesc);
	HRESULT InitializeVideoSinkWriter(std::wstring path, _In_opt_ IMFByteStream *pOutStream, _In_ ID3D11Device* pDevice, RECT sourceRect, RECT destRect, DXGI_MODE_ROTATION rotation, _Outptr_ IMFSinkWriter **ppWriter, _Out_ DWORD *pVideoStreamIndex, _Out_ DWORD *pAudioStreamIndex);
	HRESULT WriteFrameToVideo(INT64 frameStartPos, INT64 frameDuration, DWORD streamIndex, _In_ ID3D11Texture2D* pAcquiredDesktopImage);
	HRESULT WriteFrameToImage(_In_ ID3D11Texture2D* pAcquiredDesktopImage, std::wstring filePath);
	void WriteFrameToImageAsync(_In_ ID3D11Texture2D* pAcquiredDesktopImage, std::wstring filePath);
	HRESULT WriteAudioSamplesToVideo(INT64 frameStartPos, INT64 frameDuration, DWORD streamIndex, _In_ BYTE *pSrc, DWORD cbData);
	HRESULT GetOutputForDeviceName(std::wstring deviceName, _Outptr_opt_result_maybenull_ IDXGIOutput **adapter);
	HRESULT SetAttributeU32(_Inout_ ATL::CComPtr<ICodecAPI>& codec, const GUID& guid, UINT32 value);
	HRESULT CreateInputMediaTypeFromOutput(_In_ IMFMediaType *pType, const GUID& subtype, _Outptr_ IMFMediaType **ppType);
	HRESULT DrawMousePointer(ID3D11Texture2D *frame, mouse_pointer *pointer, mouse_pointer::PTR_INFO ptrInfo, DXGI_MODE_ROTATION screenRotation, INT64 durationSinceLastFrame100Nanos);
	ID3D11Texture2D* CropFrame(ID3D11Texture2D *frame, D3D11_TEXTURE2D_DESC frameDesc, RECT destRect);
};