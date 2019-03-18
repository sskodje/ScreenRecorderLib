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

typedef void(__stdcall *CallbackCompleteFunction)(std::wstring, nlohmann::fifo_map<std::wstring, int>);
typedef void(__stdcall *CallbackStatusChangedFunction)(int);
typedef void(__stdcall *CallbackErrorFunction)(std::wstring);
#define MODE_VIDEO 0
#define MODE_SLIDESHOW 1
#define MODE_SNAPSHOT 2

#define STATUS_IDLE 0
#define STATUS_RECORDING 1
#define STATUS_PAUSED 2
#define STATUS_FINALIZING 3

typedef struct
{
	UINT FrameNumber;
	ULONGLONG StartPos;
	ULONGLONG Duration;
	std::vector<BYTE> Audio;
	CComPtr<ID3D11Texture2D> Frame;
}FrameWriteModel;

class internal_recorder
{

public:

	internal_recorder();
	~internal_recorder();
	CallbackErrorFunction RecordingFailedCallback;
	CallbackCompleteFunction RecordingCompleteCallback;
	CallbackStatusChangedFunction RecordingStatusChangedCallback;
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
	void SetAudioEnabled(bool value);
	void SetMousePointerEnabled(bool value);
	void SetDestRectangle(RECT rect);
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

private:
	// Format constants
	const GUID   VIDEO_ENCODING_FORMAT = MFVideoFormat_H264;
	const GUID	 AUDIO_ENCODING_FORMAT = MFAudioFormat_AAC;
	const UINT32 AUDIO_BITS_PER_SAMPLE = 16; //Audio bits per sample must be 16.
	const UINT32 AUDIO_SAMPLES_PER_SECOND = 44100;//Audio samples per seconds must be 44100.
	const GUID   VIDEO_INPUT_FORMAT = MFVideoFormat_ARGB32;
	const GUID   VIDEO_INPUT_FORMAT_CONVERTED = MFVideoFormat_NV12;
	const GUID   IMAGE_ENCODER_FORMAT = GUID_ContainerFormatPng;

	struct TaskWrapper;
	std::unique_ptr<TaskWrapper> m_TaskWrapperImpl;

#if _DEBUG 
	ID3D11Debug *m_Debug;
#endif
	ID3D11DeviceContext *m_ImmediateContext;
	IMFSinkWriter *m_SinkWriter;
	std::queue<FrameWriteModel> m_WriteQueue;
	std::chrono::high_resolution_clock::time_point m_LastFrame;
	bool m_IsDestructed = false;
	UINT32 m_RecorderMode = MODE_VIDEO;
	DWORD m_VideoStreamIndex = 0;
	DWORD m_AudioStreamIndex = 0;
	UINT m_DisplayOutput = 0; //Display output, where 0 is primary display.
	std::wstring m_DisplayOutputName = L""; //Display output device name, e.g. \\.\DISPLAY1
	RECT m_DestRect = { 0,0,0,0 };
	std::wstring m_OutputFolder = L"";
	std::wstring m_OutputFullPath = L"";
	nlohmann::fifo_map<std::wstring, int> m_FrameDelays;
	UINT32 m_VideoFps = 30;
	UINT32 m_VideoBitrate = 4000 * 1000;//Bitrate in bits per second
	UINT32 m_VideoQuality = 70;//Video quality from 1 to 100. Is only used with eAVEncCommonRateControlMode_Quality.
	UINT32 m_H264Profile = eAVEncH264VProfile_Main; //Supported H264 profiles for the encoder are Baseline, Main and High.
	UINT32 m_AudioBitrate = (96 / 8) * 1000; //Bitrate in bytes per second. Only 96,128,160 and 192kbps is supported.
	UINT32 m_AudioChannels = 2; //Number of audio channels. 1,2 and 6 is supported. 6 only on windows 8 and up.
	UINT32 m_InputAudioSamplesPerSecond = AUDIO_SAMPLES_PER_SECOND;
	UINT32 m_VideoBitrateControlMode = eAVEncCommonRateControlMode_Quality;
	bool m_IsMousePointerEnabled = true;
	bool m_IsAudioEnabled = false;
	bool m_IsFixedFramerate = false;
	bool m_IsThrottlingDisabled = false;
	bool m_IsLowLatencyModeEnabled = false;
	bool m_IsMp4FastStartEnabled = true;
	bool m_IsFragmentedMp4Enabled = false;
	bool m_IsHardwareEncodingEnabled = true;
	bool m_IsPaused = false;
	bool m_IsRecording = false;
	bool m_IsEncoderFailure = false;
	UINT64 m_LastEncodedSampleCount = 0;
	std::string NowToString();
	HRESULT ConfigureOutputDir(std::wstring path);
	void SetDebugName(ID3D11DeviceChild* child, const std::string& name);
	void SetViewPort(ID3D11DeviceContext *deviceContext, UINT Width, UINT Height);
	void EnqueueFrame(FrameWriteModel model);
	void PrintCodec(LPWSTR subtype, GUID inputTypeGuid);
	HRESULT InitializeDx(IDXGIOutput *pOutput,ID3D11DeviceContext **ppContext, ID3D11Device **ppDevice, IDXGIOutputDuplication **ppDesktopDupl, DXGI_OUTDUPL_DESC *pOutputDuplDesc);
	HRESULT InitializeDesktopDupl(ID3D11Device *pDevice, IDXGIOutput *pOutput, IDXGIOutputDuplication **ppDesktopDupl, DXGI_OUTDUPL_DESC *pOutputDuplDesc);
	HRESULT InitializeVideoSinkWriter(std::wstring path, IMFByteStream *outStream, ID3D11Device* pDevice, RECT sourceRect, RECT destRect, IMFSinkWriter **ppWriter, DWORD *pVideoStreamIndex, DWORD *pAudioStreamIndex);
	HRESULT WriteFrameToVideo(ULONGLONG frameStartPos, ULONGLONG frameDuration, DWORD streamIndex, ID3D11Texture2D* pAcquiredDesktopImage);
	HRESULT WriteFrameToImage(ID3D11Texture2D* pAcquiredDesktopImage, LPCWSTR filePath);
	HRESULT WriteAudioSamplesToVideo(ULONGLONG frameStartPos, ULONGLONG frameDuration, DWORD streamIndex, BYTE *pSrc, DWORD cbData);
	HRESULT GetOutputForDeviceName(std::wstring deviceName, IDXGIOutput **adapter);
	HRESULT SetAttributeU32(ATL::CComPtr<ICodecAPI>& codec, const GUID& guid, UINT32 value);
};