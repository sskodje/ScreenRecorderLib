#pragma once
#define _CRTDBG_MAP_ALLOC
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
#include "MF.util.h"
#include "CMFSinkWriterCallback.h"

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

	bool IsRecording() { return m_IsRecording; }

	void SetSourceRectangle(RECT rect) {
		m_SourceRect = MakeRectEven(rect);
	}

	static bool SetExcludeFromCapture(HWND hwnd, bool isExcluded);
	void SetRecordingSources(std::vector<RECORDING_SOURCE> sources) { m_RecordingSources = sources; }
	void SetRecorderMode(UINT32 mode) { m_RecorderMode = mode; }
	void SetRecorderApi(UINT32 api) { m_RecorderApi = api; }


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
	void SetSnapshotOptions(SNAPSHOT_OPTIONS *options) { m_SnapshotOptions.reset(options); }
	std::shared_ptr<SNAPSHOT_OPTIONS> GetSnapshotOptions() { return m_SnapshotOptions; }
private:
	struct TaskWrapper;
	std::unique_ptr<TaskWrapper> m_TaskWrapperImpl;

#if _DEBUG 
	ID3D11Debug *m_Debug = nullptr;
#endif
	ID3D11DeviceContext *m_DeviceContext = nullptr;
	IMFSinkWriter *m_SinkWriter = nullptr;
	ID3D11Device *m_Device = nullptr;

	std::unique_ptr<TextureManager> m_TextureManager;

	bool m_LastFrameHadAudio = false;
	HRESULT m_EncoderResult = S_FALSE;


	std::wstring m_OutputFolder = L"";
	std::wstring m_OutputFullPath = L"";

	nlohmann::fifo_map<std::wstring, int> m_FrameDelays;
	DWORD m_VideoStreamIndex = 0;
	DWORD m_AudioStreamIndex = 0;
	HANDLE m_FinalizeEvent = nullptr;

	INT64 m_MaxFrameLength100Nanos = MillisToHundredNanos(500); //500 milliseconds in 100 nanoseconds measure.

	UINT32 m_RecorderMode = MODE_VIDEO;
	UINT32 m_RecorderApi = API_DESKTOP_DUPLICATION;
	std::vector<RECORDING_SOURCE> m_RecordingSources{};
	std::vector<RECORDING_OVERLAY> m_Overlays;
	RECT m_SourceRect{};
	bool m_IsPaused = false;
	bool m_IsRecording = false;

	std::shared_ptr<ENCODER_OPTIONS> m_EncoderOptions;
	std::shared_ptr<AUDIO_OPTIONS> m_AudioOptions;
	std::shared_ptr<MOUSE_OPTIONS> m_MouseOptions;
	std::shared_ptr<SNAPSHOT_OPTIONS> m_SnapshotOptions;


	bool CheckDependencies(_Out_ std::wstring *error);
	ScreenCaptureBase *CreateCaptureSession();


	/// <summary>
	/// Creates adjusted source and output rects from a recording frame rect. The source rect is normalized to start on [0,0], and the output is adjusted for any cropping.
	/// </summary>
	/// <param name="outputSize">The recording output size</param>
	/// <param name="pAdjustedSourceRect">The source rect adjusted to start on [0,0] and with custom cropping if any</param>
	/// <param name="pAdjustedOutputFrameSize">The destination rect adjusted to start on [0,0] and with custom render size if any</param>
	/// <returns></returns>
	HRESULT InitializeRects(_In_ SIZE outputSize, _Out_ RECT *pAdjustedSourceRect, _Out_ SIZE *pAdjustedOutputFrameSize);
	HRESULT InitializeVideoSinkWriter(_In_ std::wstring path, _In_opt_ IMFByteStream *pOutStream, _In_ ID3D11Device *pDevice, _In_ RECT sourceRect, _In_ SIZE outputFrameSize, _In_ DXGI_MODE_ROTATION rotation,_In_ IMFSinkWriterCallback *pCallback, _Outptr_ IMFSinkWriter **ppWriter, _Out_ DWORD *pVideoStreamIndex, _Out_ DWORD *pAudioStreamIndex);
	HRESULT ConfigureOutputMediaTypes(_In_ UINT destWidth, _In_ UINT destHeight, _Outptr_ IMFMediaType **pVideoMediaTypeOut, _Outptr_result_maybenull_ IMFMediaType **pAudioMediaTypeOut);
	HRESULT ConfigureInputMediaTypes(_In_ UINT sourceWidth, _In_ UINT sourceHeight, _In_ MFVideoRotationFormat rotationFormat, _In_ IMFMediaType *pVideoMediaTypeOut, _Outptr_ IMFMediaType **pVideoMediaTypeIn, _Outptr_result_maybenull_ IMFMediaType **pAudioMediaTypeIn);
	HRESULT ConfigureOutputDir(_In_ std::wstring path);
	HRESULT StartRecorderLoop(_In_ std::vector<RECORDING_SOURCE> sources, _In_ std::vector<RECORDING_OVERLAY> overlays, _In_opt_ IStream *pStream);
	HRESULT RenderFrame(_In_ FrameWriteModel &model);
	HRESULT FinalizeRecording();
	HRESULT WriteFrameToVideo(_In_ INT64 frameStartPos, _In_ INT64 frameDuration, _In_ DWORD streamIndex, _In_ ID3D11Texture2D *pAcquiredDesktopImage);
	HRESULT WriteFrameToImage(_In_ ID3D11Texture2D *pAcquiredDesktopImage, _In_ std::wstring filePath);
	HRESULT WriteAudioSamplesToVideo(_In_ INT64 frameStartPos, _In_ INT64 frameDuration, _In_ DWORD streamIndex, _In_ BYTE *pSrc, _In_ DWORD cbData);
	void WriteTextureToImageAsync(_In_ ID3D11Texture2D *pAcquiredDesktopImage, _In_ std::wstring filePath);
	/// <summary>
	/// Save texture as image to video snapshot folder.
	/// </summary>
	/// <param name="texture">The texture to save to a snapshot</param>
	/// <param name="sourceRect">The area of the texture to save. If the texture is larger, it will be cropped to these coordinates.</param>
	/// <returns></returns>
	HRESULT SaveTextureAsVideoSnapshot(_In_ ID3D11Texture2D *pTexture, _In_ RECT sourceRect);
	void CleanupResourcesAndShutDownMF();
	void SetRecordingCompleteStatus(_In_ HRESULT hr);

};