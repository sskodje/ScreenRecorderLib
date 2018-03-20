#pragma once
#include <windows.h>
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
	HRESULT BeginRecording(IStream *stream);
	void EndRecording();
	void PauseRecording();
	void ResumeRecording();
	void SetVideoFps(UINT32 fps);
	void SetVideoBitrate(UINT32 bitrate);
	void SetAudioBitrate(UINT32 bitrate);
	void SetAudioChannels(UINT32 channels);
	void SetAudioEnabled(bool value);
	void SetMousePointerEnabled(bool value);
	void SetDestRectangle(RECT rect);
	void SetDisplayOutput(UINT32 output);
	void SetRecorderMode(UINT32 mode);
	void SetFixedFramerate(bool value);
	void SetIsThrottlingDisabled(bool value);
	void SetH264EncoderProfile(UINT32 value);
	void SetIsFastStartEnabled(bool value);
	void SetIsHardwareEncodingEnabled(bool value);
	void SetIsLowLatencyModeEnabled(bool value);
private:
	UINT32 m_InputAudioSamplesPerSecond;
	HRESULT BeginRecording(std::wstring path, IStream *stream);
	std::string NowToString();
	HRESULT ConfigureOutputDir(std::wstring path);
	void SetDebugName(ID3D11DeviceChild* child, const std::string& name);
	void SetViewPort(ID3D11DeviceContext *deviceContext, UINT Width, UINT Height);
	void EnqueueFrame(FrameWriteModel *model);
	HRESULT InitializeDx(ID3D11DeviceContext **ppContext, ID3D11Device **ppDevice, IDXGIOutputDuplication **ppDesktopDupl, DXGI_OUTDUPL_DESC *pOutputDuplDesc);
	HRESULT InitializeDesktopDupl(ID3D11Device *pDevice, IDXGIOutputDuplication **ppDesktopDupl, DXGI_OUTDUPL_DESC *pOutputDuplDesc);
	HRESULT InitializeVideoSinkWriter(std::wstring path, IMFByteStream *outStream, ID3D11Device* pDevice, RECT sourceRect, RECT destRect, IMFSinkWriter **ppWriter, DWORD *pVideoStreamIndex, DWORD *pAudioStreamIndex);
	HRESULT WriteFrameToVideo(ULONGLONG frameStartPos, ULONGLONG frameDuration, DWORD streamIndex, ID3D11Texture2D* pAcquiredDesktopImage);
	HRESULT WriteFrameToImage(ID3D11Texture2D* pAcquiredDesktopImage, LPCWSTR filePath);
	HRESULT WriteAudioSamplesToVideo(ULONGLONG frameStartPos, ULONGLONG frameDuration, DWORD streamIndex, BYTE *pSrc, DWORD cbData);
};