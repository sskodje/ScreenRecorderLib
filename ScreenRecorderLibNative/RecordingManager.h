#pragma once
#define _CRTDBG_MAP_ALLOC
#include "MouseManager.h"
#include "AudioManager.h"
#include "OutputManager.h"
#include "Log.h"
#include "fifo_map.h"
typedef void(__stdcall *CallbackCompleteFunction)(std::wstring, nlohmann::fifo_map<std::wstring, int>);
typedef void(__stdcall *CallbackStatusChangedFunction)(int);
typedef void(__stdcall *CallbackErrorFunction)(std::wstring, std::wstring);
typedef void(__stdcall *CallbackSnapshotFunction)(std::wstring);
typedef void(__stdcall *CallbackFrameNumberChangedFunction)(int);

#define STATUS_IDLE 0
#define STATUS_RECORDING 1
#define STATUS_PAUSED 2
#define STATUS_FINALIZING 3

#define API_DESKTOP_DUPLICATION 0
#define API_GRAPHICS_CAPTURE 1

class RecordingManager
{
public:
	RecordingManager();
	~RecordingManager();
	CallbackErrorFunction RecordingFailedCallback;
	CallbackCompleteFunction RecordingCompleteCallback;
	CallbackStatusChangedFunction RecordingStatusChangedCallback;
	CallbackSnapshotFunction RecordingSnapshotCreatedCallback;
	CallbackFrameNumberChangedFunction RecordingFrameNumberChangedCallback;
	HRESULT BeginRecording(_In_opt_ std::wstring path);
	HRESULT BeginRecording(_In_opt_ std::wstring path, _In_opt_ IStream *stream);
	HRESULT BeginRecording(_In_opt_ IStream *stream);
	void EndRecording();
	void PauseRecording();
	void ResumeRecording();

	bool IsRecording() { return m_IsRecording; }

	static bool SetExcludeFromCapture(HWND hwnd, bool isExcluded);

	inline void SetRecordingSources(std::vector<RECORDING_SOURCE> sources) 
	{
		m_RecordingSources.clear();
		for each (RECORDING_SOURCE source in sources)
		{
			m_RecordingSources.push_back(new RECORDING_SOURCE(source));
		}
	}
	inline std::vector<RECORDING_SOURCE *> GetRecordingSources() {
		return m_RecordingSources;
	}

	inline void SetOverlays(std::vector<RECORDING_OVERLAY> overlays) {
		m_Overlays.clear();
		for each (RECORDING_OVERLAY overlay in overlays)
		{
			m_Overlays.push_back(new RECORDING_OVERLAY(overlay));
		}
	}
	inline std::vector<RECORDING_OVERLAY *> GetRecordingOverlays() {
		return m_Overlays;
	}


	void SetLogEnabled(bool value);
	void SetLogFilePath(std::wstring value);
	void SetLogSeverityLevel(int value);

	void SetEncoderOptions(ENCODER_OPTIONS *options) { m_EncoderOptions.reset(options); }
	std::shared_ptr<ENCODER_OPTIONS> GetEncoderOptions() { return m_EncoderOptions; }
	void SetAudioOptions(AUDIO_OPTIONS *options) { m_AudioOptions.reset(options); }
	std::shared_ptr<AUDIO_OPTIONS> GetAudioOptions() { return m_AudioOptions; }
	void SetMouseOptions(MOUSE_OPTIONS *options) { m_MouseOptions.reset(options); }
	std::shared_ptr<MOUSE_OPTIONS> GetMouseOptions() { return m_MouseOptions; }
	void SetSnapshotOptions(SNAPSHOT_OPTIONS *options) { m_SnapshotOptions.reset(options); }
	std::shared_ptr<SNAPSHOT_OPTIONS> GetSnapshotOptions() { return m_SnapshotOptions; }
	void SetOutputOptions(OUTPUT_OPTIONS *options) { m_OutputOptions.reset(options); }
	std::shared_ptr<OUTPUT_OPTIONS> GetOutputOptions() { return m_OutputOptions; }
private:
	bool m_IsDestructing;
	struct TaskWrapper;
	std::unique_ptr<TaskWrapper> m_TaskWrapperImpl;

	DX_RESOURCES m_DxResources;

	std::unique_ptr<TextureManager> m_TextureManager;
	std::unique_ptr<OutputManager> m_OutputManager;
	HRESULT m_EncoderResult = E_FAIL;
	HRESULT m_MfStartupResult = E_FAIL;
	std::wstring m_OutputFolder = L"";
	std::wstring m_OutputFullPath = L"";
	INT64 m_MaxFrameLength100Nanos = MillisToHundredNanos(500); //500 milliseconds in 100 nanoseconds measure.

	std::vector<RECORDING_SOURCE*> m_RecordingSources;
	std::vector<RECORDING_OVERLAY*> m_Overlays;
	bool m_IsPaused = false;
	bool m_IsRecording = false;

	std::shared_ptr<ENCODER_OPTIONS> m_EncoderOptions;
	std::shared_ptr<AUDIO_OPTIONS> m_AudioOptions;
	std::shared_ptr<MOUSE_OPTIONS> m_MouseOptions;
	std::shared_ptr<SNAPSHOT_OPTIONS> m_SnapshotOptions;
	std::shared_ptr<OUTPUT_OPTIONS> m_OutputOptions;

	bool CheckDependencies(_Out_ std::wstring *error);
	HRESULT ConfigureOutputDir(_In_ std::wstring path);
	REC_RESULT StartRecorderLoop(_In_ const std::vector<RECORDING_SOURCE*> &sources, _In_ const std::vector<RECORDING_OVERLAY*> &overlays, _In_opt_ IStream *pStream);

	/// <summary>
	/// Creates adjusted source and output rects from a recording frame rect. The source rect is normalized to start on [0,0], and the output is adjusted for any cropping.
	/// </summary>
	/// <param name="outputSize">The recording output size</param>
	/// <param name="pAdjustedSourceRect">The source rect adjusted to start on [0,0] and with custom cropping if any</param>
	/// <param name="pAdjustedOutputFrameSize">The destination rect adjusted to start on [0,0] and with custom render size if any</param>
	/// <returns></returns>
	HRESULT InitializeRects(_In_ SIZE outputSize, _Out_opt_ RECT *pAdjustedSourceRect, _Out_opt_ SIZE *pAdjustedOutputFrameSize);

	/// <summary>
	/// Save texture as image to video snapshot folder.
	/// </summary>
	/// <param name="pTexture">The texture to save to a snapshot</param>
	/// <param name="sourceRect">The area of the texture to save. If the texture is larger, it will be cropped to these coordinates.</param>
	/// <returns></returns>
	HRESULT SaveTextureAsVideoSnapshot(_In_ ID3D11Texture2D *pTexture, _In_ RECT sourceRect);

	/// <summary>
	/// Perform cropping and resizing on texture if needed.
	/// </summary>
	/// <param name="pTexture">The texture to process</param>
	/// <param name="ppProcessedTexture">The output texture. If no transformations are done, the original texture is returned.</param>
	/// <param name="videoInputFrameRect">The source rectangle. The texture will be cropped to these coordinates if larger.</param>
	/// <param name="videoOutputFrameSize">The output dimensions. The texture will be resized to these coordinates if differing.</param>
	/// <returns>S_OK if any processing has been done, S_FALSE if no changes, else an error code</returns>
	HRESULT ProcessTextureTransforms(_In_ ID3D11Texture2D *pTexture,_Out_ ID3D11Texture2D **ppProcessedTexture, RECT videoInputFrameRect, SIZE videoOutputFrameSize);

	/// <summary>
	/// Releases DirectX resources and reports any leaks
	/// </summary>
	void CleanupDxResources();

	/// <summary>
	///	Calls the RecordingComplete or RecordingFailed callbacks depending on the success of the recording result.
	/// </summary>
	/// <param name="result">The recording result.</param>
	/// <param name="frameDelays">A map of paths to saved frames with corresponding delay between them. Only used for Slideshow mode.</param>
	void SetRecordingCompleteStatus(_In_ REC_RESULT result, nlohmann::fifo_map<std::wstring, int> frameDelays);
};