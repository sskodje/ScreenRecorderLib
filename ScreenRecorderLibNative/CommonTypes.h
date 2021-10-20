#pragma once
#include <d3d11.h>
#include <dxgi1_2.h>
#include <sal.h>
#include <DirectXMath.h>
#include <string>
#include <vector>
#include <strsafe.h>
#include "PixelShader.h"
#include "VertexShader.h"
#include <codecapi.h>
#include <mfapi.h>
#include <optional>
#include <wincodec.h>
#include <chrono>
#include "util.h"
//
// Holds info about the pointer/cursor
//
struct PTR_INFO
{
	_Field_size_bytes_(BufferSize) BYTE *PtrShapeBuffer;
	DXGI_OUTDUPL_POINTER_SHAPE_INFO ShapeInfo;
	POINT Position;
	bool Visible;
	bool IsPointerShapeUpdated;
	UINT BufferSize;
	RECT WhoUpdatedPositionLast;
	LARGE_INTEGER LastTimeStamp;
};

//
// A vertex with a position and texture coordinate
//
struct VERTEX
{
	DirectX::XMFLOAT3 Pos;
	DirectX::XMFLOAT2 TexCoord;
};

//
// DUPL_FRAME_DATA holds information about an acquired Desktop Duplication frame
//
struct DUPL_FRAME_DATA
{
	ID3D11Texture2D *Frame;
	DXGI_OUTDUPL_FRAME_INFO FrameInfo;
	_Field_size_bytes_((MoveCount * sizeof(DXGI_OUTDUPL_MOVE_RECT)) + (DirtyCount * sizeof(RECT))) BYTE *MetaData;
	UINT DirtyCount;
	UINT MoveCount;
};

//
// GRAPHICS_FRAME_DATA holds information about an acquired Windows Graphics Capture frame
//
struct GRAPHICS_FRAME_DATA
{
	ID3D11Texture2D *Frame;
	SIZE ContentSize;
	LARGE_INTEGER Timestamp;
};

//
// Structure that holds D3D resources not directly tied to any one thread
//
struct DX_RESOURCES
{
	ID3D11Device *Device;
	ID3D11DeviceContext *Context;
	ID3D11Debug *Debug;
};


//
// CAPTURED_FRAME holds information about a merged output frame with overlays
//
struct CAPTURED_FRAME
{
	ID3D11Texture2D *Frame;
	PTR_INFO *PtrInfo;
	//The number of updates written to the current frame since last fetch.
	int FrameUpdateCount;
	//The number of updates written to the frame overlays since last fetch.
	int OverlayUpdateCount;
};

enum class RecorderModeInternal {
	///<summary>Record to mp4 container in H.264/AVC or H.265/HEVC format. </summary>
	Video = 0,
	///<summary>Record a slideshow of pictures. </summary>
	Slideshow = 1,
	///<summary>Create a single screenshot.</summary>
	Screenshot = 2
};

enum class TextureStretchMode {
	///<summary>The content preserves its original size. </summary>
	None,
	///<summary>The content is resized to fill the destination dimensions. The aspect ratio is not preserved. </summary>
	Fill,
	///<summary>The content is resized to fit in the destination dimensions while it preserves its native aspect ratio.</summary>
	Uniform,
	///<summary>
	//     The content is resized to fill the destination dimensions while it preserves
	//     its native aspect ratio. If the aspect ratio of the destination rectangle differs
	//     from the source, the source content is clipped to fit in the destination dimensions.
	///</summary>
	UniformToFill
};

enum class ContentAnchor {
	TopLeft,
	TopRight,
	Center,
	BottomLeft,
	BottomRight
};

enum class RecordingSourceType {
	Display,
	Window,
	CameraCapture,
	Picture,
	Video
};

enum class RecordingSourceApi {
	DesktopDuplication,
	WindowsGraphicsCapture
};

struct RECORDING_SOURCE_BASE abstract {
	std::wstring SourcePath;
	HWND SourceWindow;
	RecordingSourceType Type;
	std::wstring ID;
	/// <summary>
	/// Stretch mode for the frame
	/// </summary>
	TextureStretchMode Stretch;
	/// <summary>
	/// Optional custom output size of the source frame. May be both smaller or larger than the source.
	/// </summary>
	std::optional<SIZE> OutputSize;
	/// <summary>
	/// The anchor position for the content inside the parent frame.
	/// </summary>
	ContentAnchor Anchor;
	RECORDING_SOURCE_BASE() :
		Type(RecordingSourceType::Display),
		SourceWindow(nullptr),
		SourcePath(L""),
		OutputSize{ std::nullopt },
		ID(L""),
		Stretch(TextureStretchMode::Uniform),
		Anchor(ContentAnchor::TopLeft)
	{

	}
	virtual ~RECORDING_SOURCE_BASE() {

	}

	SIZE GetMargins() {

	}
};

struct RECORDING_OVERLAY :RECORDING_SOURCE_BASE
{
	std::wstring ID;
	/// <summary>
	/// Optional custom offset for the source frame.
	/// </summary>
	std::optional<SIZE> Offset;
	RECORDING_OVERLAY() :
		RECORDING_SOURCE_BASE(),
		Offset{ std::nullopt },
		ID(L"")
	{

	}
	friend bool operator== (const RECORDING_OVERLAY &a, const RECORDING_OVERLAY &b) {
		return a.ID == b.ID;
	}
};

struct RECORDING_OVERLAY_DATA
{
	DX_RESOURCES DxRes{};
	RECORDING_OVERLAY *RecordingOverlay;
	RECORDING_OVERLAY_DATA() {}
	RECORDING_OVERLAY_DATA(RECORDING_OVERLAY *overlay)
		:RecordingOverlay{ overlay }
	{

	}
};

struct RECORDING_SOURCE : RECORDING_SOURCE_BASE
{
	std::optional<RecordingSourceApi> SourceApi;
	std::optional<bool> IsCursorCaptureEnabled{};
	/// <summary>
	/// An optional custom area of the source to record. Must be equal or smaller than the source area. A smaller area will crop the source.
	/// </summary>
	std::optional<RECT> SourceRect;
	/// <summary>
	/// Optional custom position for the source frame.
	/// </summary>
	std::optional<POINT> Position;

	RECORDING_SOURCE() :
		RECORDING_SOURCE_BASE(),
		IsCursorCaptureEnabled(std::nullopt),
		SourceRect{ std::nullopt },
		Position{ std::nullopt },
		SourceApi(std::nullopt)
	{
		RECORDING_SOURCE_BASE::Anchor = ContentAnchor::Center;
	}

	friend bool operator< (const RECORDING_SOURCE &a, const RECORDING_SOURCE &b) {
		return std::tie(a.Type, a.SourceWindow, a.SourcePath) < std::tie(b.Type, b.SourceWindow, b.SourcePath);
	}
	friend bool operator== (const RECORDING_SOURCE &a, const RECORDING_SOURCE &b) {
		return a.ID == b.ID;
	}
};

struct RECORDING_SOURCE_DATA {
	INT OffsetX;
	INT OffsetY;
	/// <summary>
	/// Describes the position and size of this recording source within the recording surface.
	/// </summary>
	RECT FrameCoordinates;
	DX_RESOURCES DxRes;
	RECORDING_SOURCE *RecordingSource;
	RECORDING_SOURCE_DATA(RECORDING_SOURCE *recordingSource) :
		OffsetX(0),
		OffsetY(0),
		DxRes{},
		FrameCoordinates{},
		RecordingSource{ recordingSource }
	{

	}
};

//
// Structure to pass to a new thread
//
struct THREAD_DATA_BASE
{
	////Handle to shared surface texture
	HANDLE CanvasTexSharedHandle{ nullptr };
	// Used to indicate abnormal error condition
	HANDLE UnexpectedErrorEvent{};
	// Used to indicate a transition event occurred e.g. PnpStop, PnpStart, mode change, TDR, desktop switch and the application needs to recreate the capture interface
	HANDLE ExpectedErrorEvent{};
	// Used by WinProc to signal to threads to exit
	HANDLE TerminateThreadsEvent{};
	LARGE_INTEGER LastUpdateTimeStamp{};
	HRESULT ThreadResult{ E_FAIL };
};

//
// Structure to pass to a new thread
//
struct CAPTURE_THREAD_DATA :THREAD_DATA_BASE
{
	RECORDING_SOURCE_DATA *RecordingSource{ nullptr };
	INT UpdatedFrameCountSinceLastWrite{};
	INT64 TotalUpdatedFrameCount{};
	PTR_INFO *PtrInfo{ nullptr };
};

//
// Structure to pass to a new thread
//
struct OVERLAY_THREAD_DATA :THREAD_DATA_BASE
{
	////Handle to shared overlay texture
	HANDLE OverlayTexSharedHandle{ nullptr };
	RECORDING_OVERLAY_DATA *RecordingOverlay{};
};

struct MOUSE_OPTIONS {
protected:
	bool m_IsMouseClicksDetected = false;
	bool m_IsMousePointerEnabled = true;
	std::string m_MouseClickDetectionLMBColor = "#FFFF00";
	std::string m_MouseClickDetectionRMBColor = "#FFFF00";
	UINT32 m_MouseClickDetectionRadius = 20;
	UINT32 m_MouseClickDetectionMode = MOUSE_DETECTION_MODE_POLLING;
	UINT32 m_MouseClickDetectionDurationMillis = 50;
public:
	static const UINT32 MOUSE_DETECTION_MODE_POLLING = 0;
	static const UINT32 MOUSE_DETECTION_MODE_HOOK = 1;

	void SetMousePointerEnabled(bool value) { m_IsMousePointerEnabled = value; }
	void SetDetectMouseClicks(bool value) { m_IsMouseClicksDetected = value; }
	void SetMouseClickDetectionLMBColor(std::string value) { m_MouseClickDetectionLMBColor = value; }
	void SetMouseClickDetectionRMBColor(std::string value) { m_MouseClickDetectionRMBColor = value; }
	void SetMouseClickDetectionRadius(int value) { m_MouseClickDetectionRadius = value; }
	void SetMouseClickDetectionMode(UINT32 value) { m_MouseClickDetectionMode = value; }
	void SetMouseClickDetectionDuration(int value) { m_MouseClickDetectionDurationMillis = value; }

	bool IsMouseClicksDetected() { return m_IsMouseClicksDetected; }
	bool IsMousePointerEnabled() { return m_IsMousePointerEnabled; }
	std::string GetMouseClickDetectionLMBColor() { return m_MouseClickDetectionLMBColor; }
	std::string GetMouseClickDetectionRMBColor() { return m_MouseClickDetectionRMBColor; }
	UINT32 GetMouseClickDetectionRadius() { return  m_MouseClickDetectionRadius; }
	UINT32 GetMouseClickDetectionMode() { return m_MouseClickDetectionMode; }
	UINT32 GetMouseClickDetectionDurationMillis() { return m_MouseClickDetectionDurationMillis; }
};

struct AUDIO_OPTIONS {
protected:
#pragma region Format constants
	const GUID	 AUDIO_ENCODING_FORMAT = MFAudioFormat_AAC;
	const UINT32 AUDIO_BITS_PER_SAMPLE = 16; //Audio bits per sample must be 16.
	const UINT32 AUDIO_SAMPLES_PER_SECOND = 48000;//Audio samples per seconds must be 44100 or 48000.
#pragma endregion

	std::wstring m_AudioOutputDevice = L"";
	std::wstring m_AudioInputDevice = L"";
	bool m_IsAudioEnabled = false;
	bool m_IsOutputDeviceEnabled = true;
	bool m_IsInputDeviceEnabled = true;
	UINT32 m_AudioBitrate = (96 / 8) * 1000; //Bitrate in bytes per second. Only 96,128,160 and 192kbps is supported.
	UINT32 m_AudioChannels = 2; //Number of audio channels. 1,2 and 6 is supported. 6 only on windows 8 and up.
	float m_OutputVolumeModifier = 1;
	float m_InputVolumeModifier = 1;
public:
	void SetInputVolume(float volume) { m_InputVolumeModifier = volume; }
	void SetOutputVolume(float volume) { m_OutputVolumeModifier = volume; }
	void SetAudioBitrate(UINT32 bitrate) { m_AudioBitrate = bitrate; }
	void SetAudioChannels(UINT32 channels) { m_AudioChannels = channels; }
	void SetOutputDevice(std::wstring string) { m_AudioOutputDevice = string; }
	void SetInputDevice(std::wstring string) { m_AudioInputDevice = string; }
	void SetAudioEnabled(bool value) { m_IsAudioEnabled = value; }
	void SetOutputDeviceEnabled(bool value) { m_IsOutputDeviceEnabled = value; }
	void SetInputDeviceEnabled(bool value) { m_IsInputDeviceEnabled = value; }

	std::wstring GetAudioOutputDevice() { return m_AudioOutputDevice; }
	std::wstring GetAudioInputDevice() { return m_AudioInputDevice; }
	bool IsAudioEnabled() { return m_IsAudioEnabled; }
	UINT32 GetAudioBitrate() { return m_AudioBitrate; }
	UINT32 GetAudioChannels() { return m_AudioChannels; }
	float GetOutputVolume() { return m_OutputVolumeModifier; }
	float GetInputVolume() { return m_InputVolumeModifier; }
	bool IsOutputDeviceEnabled() { return m_IsOutputDeviceEnabled; }
	bool IsInputDeviceEnabled() { return m_IsInputDeviceEnabled; }
	GUID GetAudioEncoderFormat() { return AUDIO_ENCODING_FORMAT; }
	UINT32 GetAudioBitsPerSample() { return AUDIO_BITS_PER_SAMPLE; }
	UINT32 GetAudioSamplesPerSecond() { return AUDIO_SAMPLES_PER_SECOND; }
};

struct OUTPUT_OPTIONS {
protected:
	SIZE m_FrameSize{};
	RECT m_SourceRect{};
	TextureStretchMode m_Stretch = TextureStretchMode::Uniform;
public:
	SIZE GetFrameSize() { return m_FrameSize; }
	void SetFrameSize(SIZE size) { m_FrameSize = size; }
	void SetSourceRectangle(RECT rect) { m_SourceRect = MakeRectEven(rect); }
	RECT GetSourceRectangle() { return m_SourceRect; }
	void SetStretch(TextureStretchMode stretch) { m_Stretch = stretch; }
	TextureStretchMode GetStretch() { return m_Stretch; }
};

struct ENCODER_OPTIONS abstract {
protected:
#pragma region Format constants
	const GUID VIDEO_INPUT_FORMAT = MFVideoFormat_ARGB32;
#pragma endregion
	UINT32 m_VideoFps = 30;
	UINT32 m_VideoBitrate = 4000 * 1000;//Bitrate in bits per second
	UINT32 m_VideoQuality = 70;//Video quality from 1 to 100. Is only used with eAVEncCommonRateControlMode_Quality.
	bool m_IsFixedFramerate = false;
	bool m_IsThrottlingDisabled = false;
	bool m_IsLowLatencyModeEnabled = false;
	bool m_IsMp4FastStartEnabled = true;
	bool m_IsFragmentedMp4Enabled = false;
	bool m_IsHardwareEncodingEnabled = true;
	UINT32 m_VideoBitrateControlMode = eAVEncCommonRateControlMode_Quality;
	UINT32 m_EncoderProfile = eAVEncH264VProfile_High;
public:
	void SetVideoFps(UINT32 fps) { m_VideoFps = fps; }
	void SetVideoBitrate(UINT32 bitrate) { m_VideoBitrate = bitrate; }
	void SetVideoQuality(UINT32 quality) { m_VideoQuality = quality; }
	void SetIsFixedFramerate(bool value) { m_IsFixedFramerate = value; }
	void SetIsThrottlingDisabled(bool value) { m_IsThrottlingDisabled = value; }
	void SetIsFastStartEnabled(bool value) { m_IsMp4FastStartEnabled = value; }
	void SetIsFragmentedMp4Enabled(bool value) { m_IsFragmentedMp4Enabled = value; }
	void SetIsHardwareEncodingEnabled(bool value) { m_IsHardwareEncodingEnabled = value; }
	void SetIsLowLatencyModeEnabled(bool value) { m_IsLowLatencyModeEnabled = value; }
	void SetVideoBitrateMode(UINT32 bitrateMode) { m_VideoBitrateControlMode = bitrateMode; }
	void SetEncoderProfile(UINT32 profile) { m_EncoderProfile = profile; }

	UINT32 GetVideoFps() { return m_VideoFps; }
	UINT32 GetVideoBitrate() { return m_VideoBitrate; }
	UINT32 GetVideoQuality() { return m_VideoQuality; }
	bool GetIsFixedFramerate() { return  m_IsFixedFramerate; }
	bool GetIsThrottlingDisabled() { return  m_IsThrottlingDisabled; }
	bool GetIsFastStartEnabled() { return m_IsMp4FastStartEnabled; }
	bool GetIsFragmentedMp4Enabled() { return m_IsFragmentedMp4Enabled; }
	bool GetIsHardwareEncodingEnabled() { return m_IsHardwareEncodingEnabled; }
	bool GetIsLowLatencyModeEnabled() { return m_IsLowLatencyModeEnabled; }
	UINT32 GetVideoBitrateMode() { return m_VideoBitrateControlMode; }
	UINT32 GetEncoderProfile() { return m_EncoderProfile; }
	GUID GetVideoInputFormat() { return VIDEO_INPUT_FORMAT; }

	virtual GUID GetVideoEncoderFormat() abstract;
	virtual std::wstring GetVideoExtension() {
		return L".mp4";
	}
};

struct H264_ENCODER_OPTIONS :ENCODER_OPTIONS {
	H264_ENCODER_OPTIONS() {
		SetEncoderProfile(eAVEncH264VProfile_High);
	}

	virtual GUID GetVideoEncoderFormat() override { return MFVideoFormat_H264; }
};

struct H265_ENCODER_OPTIONS :ENCODER_OPTIONS {
public:
	H265_ENCODER_OPTIONS() {
		SetEncoderProfile(eAVEncH265VProfile_Main_420_8);
	}
	virtual GUID GetVideoEncoderFormat() override { return MFVideoFormat_HEVC; }
};

struct SNAPSHOT_OPTIONS {
protected:
	std::wstring m_OutputSnapshotsFolderPath = L"";
	std::chrono::milliseconds m_SnapshotsInterval = std::chrono::milliseconds(10000);
	bool m_TakesSnapshotsWithVideo = false;
	GUID m_ImageEncoderFormat = GUID_ContainerFormatPng;
public:
	void SetTakeSnapshotsWithVideo(bool isEnabled) { m_TakesSnapshotsWithVideo = isEnabled; }
	void SetSnapshotsWithVideoInterval(UINT32 value) { m_SnapshotsInterval = std::chrono::milliseconds(value); }
	void SetSnapshotDirectory(std::wstring string) { m_OutputSnapshotsFolderPath = string; }
	void SetSnapshotSaveFormat(GUID value) { m_ImageEncoderFormat = value; }

	bool IsSnapshotWithVideoEnabled() {
		return m_TakesSnapshotsWithVideo;
	}
	std::chrono::milliseconds GetSnapshotsInterval() {
		return m_SnapshotsInterval;
	}
	std::wstring GetSnapshotsDirectory() {
		return m_OutputSnapshotsFolderPath;
	}
	GUID GetSnapshotEncoderFormat() {
		return m_ImageEncoderFormat;
	}


	std::wstring GetImageExtension() {
		if (m_ImageEncoderFormat == GUID_ContainerFormatPng) {
			return L".png";
		}
		else if (m_ImageEncoderFormat == GUID_ContainerFormatJpeg) {
			return L".jpg";
		}
		else if (m_ImageEncoderFormat == GUID_ContainerFormatBmp) {
			return L".bmp";
		}
		else if (m_ImageEncoderFormat == GUID_ContainerFormatTiff) {
			return L".tiff";
		}
		else {
			return L".jpg";
		}
	}
};