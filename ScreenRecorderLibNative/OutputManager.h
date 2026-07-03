#pragma once
#include "CommonTypes.h"
#include "Log.h"
#include "Util.h"
#include "MF.util.h"
#include "CMFSinkWriterCallback.h"
#include "cleanup.h"
#include "fifo_map.h"
#include <mfreadwrite.h>
#include <atlbase.h>
#include "TimelineManager.h"
#include "AudioDriftCorrector.h"

struct FrameWriteModel
{
	//Timestamp of the start of the frame, in 100 nanosecond units.
	INT64 VideoStartPos;
	//Timestamp of the start of the audio, in 100 nanosecond units.
	INT64 AudioStartPos;
	//Duration of the frame, in 100 nanosecond units.
	INT64 VideoDuration;
	//Duration of the audio, in 100 nanosecond units.
	INT64 AudioDuration;
	//The number of padded bytes, if any.
	int PaddedBytes;
	//The audio sample bytes for this frame.
	std::vector<BYTE> Audio;
	//The frame texture.
	CComPtr<ID3D11Texture2D> Frame;
	//The QPC (query performance counter) timestamp of the beginning of the audio packet as reported by the audio device.
	UINT64 AudioQpcPosition;
};

class OutputManager
{
public:
	OutputManager();
	~OutputManager();
	HRESULT Initialize(
		_In_ ID3D11DeviceContext *pDeviceContext,
		_In_ ID3D11Device *pDevice,
		_In_ TimelineManager *pTimelineManager,
		_In_ std::shared_ptr<ENCODER_OPTIONS> &pEncoderOptions,
		_In_ std::shared_ptr<AUDIO_OPTIONS> pAudioOptions,
		_In_ std::shared_ptr<SNAPSHOT_OPTIONS> pSnapshotOptions,
		_In_ std::shared_ptr<OUTPUT_OPTIONS> pOutputOptions);

	HRESULT BeginRecording(_In_ std::wstring outputPath, _In_ SIZE videoOutputFrameSizer);
	HRESULT BeginRecording(_In_ IStream *pStream, _In_ SIZE videoOutputFrameSize);
	HRESULT FinalizeRecording();
	HRESULT RenderFrame(_In_ FrameWriteModel model);
	HRESULT WriteFrameToImage(_In_ ID3D11Texture2D *pAcquiredDesktopImage, _In_ std::wstring filePath);
	HRESULT WriteFrameToImage(_In_ ID3D11Texture2D *pAcquiredDesktopImage, _In_ IStream *pStream);
	inline nlohmann::fifo_map<std::wstring, int> GetFrameDelays() { return m_FrameDelays; }
private:
	ID3D11DeviceContext *m_DeviceContext = nullptr;
	ID3D11Device *m_Device = nullptr;

	TimelineManager *m_TimelineManager = nullptr;

	std::shared_ptr<ENCODER_OPTIONS> m_EncoderOptions;
	std::shared_ptr<AUDIO_OPTIONS> m_AudioOptions;
	std::shared_ptr<SNAPSHOT_OPTIONS> m_SnapshotOptions;
	std::shared_ptr<OUTPUT_OPTIONS> m_OutputOptions;

	nlohmann::fifo_map<std::wstring, int> m_FrameDelays;

	CComPtr<IMFSinkWriter> m_SinkWriter;
	CComPtr<IMFSinkWriterCallback> m_CallBack;
	CComPtr<IMFTransform> m_MediaTransform;
	CComPtr<IMFDXGIDeviceManager> m_DeviceManager;

	UINT m_ResetToken;
	IStream *m_OutStream;
	DWORD m_VideoStreamIndex;
	DWORD m_AudioStreamIndex;
	HANDLE m_FinalizeEvent;
	std::wstring m_OutputFolder;
	std::wstring m_OutputFullPath;
	CRITICAL_SECTION m_CriticalSection;
	bool m_UseManualNV12Converter;

	inline std::shared_ptr<AUDIO_OPTIONS> GetAudioOptions() { return m_AudioOptions; }
	inline std::shared_ptr<ENCODER_OPTIONS> GetEncoderOptions() { return m_EncoderOptions; }
	inline std::shared_ptr<SNAPSHOT_OPTIONS> GetSnapshotOptions() { return m_SnapshotOptions; }
	inline std::shared_ptr<OUTPUT_OPTIONS> GetOutputOptions() { return m_OutputOptions; }

	inline TimelineManager *GetTimelineManager() { return m_TimelineManager; }

	HRESULT ConfigureOutputMediaTypes(_In_ UINT destWidth, _In_ UINT destHeight, _Outptr_ IMFMediaType **pVideoMediaTypeOut, _Outptr_result_maybenull_ IMFMediaType **pAudioMediaTypeOut);
	HRESULT ConfigureInputMediaTypes(_In_ UINT sourceWidth, _In_ UINT sourceHeight, _In_ MFVideoRotationFormat rotationFormat, _In_ IMFMediaType *pVideoMediaTypeOut, _Outptr_ IMFMediaType **pVideoMediaTypeIn, _Outptr_result_maybenull_ IMFMediaType **pAudioMediaTypeIn);
	HRESULT InitializeVideoSinkWriter(_In_ IMFByteStream *pOutStream, _In_ RECT sourceRect, _In_ SIZE outputFrameSize, _In_ DXGI_MODE_ROTATION rotation, _In_ IMFSinkWriterCallback *pCallback, _Outptr_ IMFSinkWriter **ppWriter, _Out_ DWORD *pVideoStreamIndex, _Out_ DWORD *pAudioStreamIndex);
	HRESULT WriteFrameToVideo(_In_ INT64 frameStartPos, _In_ INT64 frameDuration, _In_ DWORD streamIndex, _In_ ID3D11Texture2D *pAcquiredDesktopImage);

	HRESULT WriteAudioSamplesToVideo(_In_ INT64 frameStartPos, _In_ INT64 frameDuration, _In_ DWORD streamIndex, _In_ BYTE *pSrc, _In_ DWORD cbData);
};

