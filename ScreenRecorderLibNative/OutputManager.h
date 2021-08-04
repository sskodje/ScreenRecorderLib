#pragma once
#include "CommonTypes.h"
#include "Log.h"
#include "Util.h"
#include "MF.util.h"
#include "CMFSinkWriterCallback.h"
#include "cleanup.h"
#include "fifo_map.h"
#include <mfreadwrite.h>

struct FrameWriteModel
{
	//Timestamp of the start of the frame, in 100 nanosecond units.
	INT64 StartPos;
	//Duration of the frame, in 100 nanosecond units.
	INT64 Duration;
	//The audio sample bytes for this frame.
	std::vector<BYTE> Audio;
	//The frame texture.
	CComPtr<ID3D11Texture2D> Frame;
};

class OutputManager
{
public:
	OutputManager();
	~OutputManager();
	HRESULT Initialize(_In_ ID3D11DeviceContext *pDeviceContext, _In_ ID3D11Device *pDevice, _In_ std::shared_ptr<ENCODER_OPTIONS> &pEncoderOptions, _In_ std::shared_ptr<AUDIO_OPTIONS> pAudioOptions, _In_ std::shared_ptr<SNAPSHOT_OPTIONS> pSnapshotOptions);
	HRESULT BeginRecording(_In_ std::wstring outputPath, _In_ SIZE videoOutputFrameSize, _In_ RecorderModeInternal recorderMode, _In_opt_ IStream *pStream = nullptr);
	HRESULT FinalizeRecording();
	HRESULT RenderFrame(_In_ FrameWriteModel &model);
	void WriteTextureToImageAsync(_In_ ID3D11Texture2D *pAcquiredDesktopImage, _In_ std::wstring filePath, _In_opt_ std::function<void(HRESULT)> onCompletion = nullptr);
	inline nlohmann::fifo_map<std::wstring, int> GetFrameDelays() { return m_FrameDelays; }
	inline UINT64 GetRenderedFrameCount() { return m_RenderedFrameCount; }
private:
	ID3D11DeviceContext *m_DeviceContext = nullptr;
	ID3D11Device *m_Device = nullptr;

	std::shared_ptr<ENCODER_OPTIONS> m_EncoderOptions;
	std::shared_ptr<AUDIO_OPTIONS> m_AudioOptions;
	std::shared_ptr<SNAPSHOT_OPTIONS> m_SnapshotOptions;

	RecorderModeInternal m_RecorderMode;
	nlohmann::fifo_map<std::wstring, int> m_FrameDelays;

	CComPtr<IMFSinkWriter> m_SinkWriter;
	CComPtr<IMFSinkWriterCallback> m_CallBack;
	DWORD m_VideoStreamIndex;
	DWORD m_AudioStreamIndex;
	HANDLE m_FinalizeEvent;
	std::wstring m_OutputFolder;
	std::wstring m_OutputFullPath;
	bool m_LastFrameHadAudio;
	UINT64 m_RenderedFrameCount;
	std::chrono::steady_clock::time_point m_PreviousSnapshotTaken;

	std::shared_ptr<AUDIO_OPTIONS> GetAudioOptions() { return m_AudioOptions; }
	std::shared_ptr<ENCODER_OPTIONS> GetEncoderOptions() { return m_EncoderOptions; }
	std::shared_ptr<SNAPSHOT_OPTIONS> GetSnapshotOptions() { return m_SnapshotOptions; }

	HRESULT ConfigureOutputMediaTypes(_In_ UINT destWidth, _In_ UINT destHeight, _Outptr_ IMFMediaType **pVideoMediaTypeOut, _Outptr_result_maybenull_ IMFMediaType **pAudioMediaTypeOut);
	HRESULT ConfigureInputMediaTypes(_In_ UINT sourceWidth, _In_ UINT sourceHeight, _In_ MFVideoRotationFormat rotationFormat, _In_ IMFMediaType *pVideoMediaTypeOut, _Outptr_ IMFMediaType **pVideoMediaTypeIn, _Outptr_result_maybenull_ IMFMediaType **pAudioMediaTypeIn);
	HRESULT InitializeVideoSinkWriter(_In_ std::wstring path, _In_opt_ IMFByteStream *pOutStream, _In_ ID3D11Device *pDevice, _In_ RECT sourceRect, _In_ SIZE outputFrameSize, _In_ DXGI_MODE_ROTATION rotation, _In_ IMFSinkWriterCallback *pCallback, _Outptr_ IMFSinkWriter **ppWriter, _Out_ DWORD *pVideoStreamIndex, _Out_ DWORD *pAudioStreamIndex);

	HRESULT WriteFrameToVideo(_In_ INT64 frameStartPos, _In_ INT64 frameDuration, _In_ DWORD streamIndex, _In_ ID3D11Texture2D *pAcquiredDesktopImage);
	HRESULT WriteFrameToImage(_In_ ID3D11Texture2D *pAcquiredDesktopImage, _In_ std::wstring filePath);
	HRESULT WriteAudioSamplesToVideo(_In_ INT64 frameStartPos, _In_ INT64 frameDuration, _In_ DWORD streamIndex, _In_ BYTE *pSrc, _In_ DWORD cbData);
};

