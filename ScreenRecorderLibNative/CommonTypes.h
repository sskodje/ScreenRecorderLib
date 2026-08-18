#pragma once
#include <d3d11.h>
#include <dxgi1_2.h>
#include <sal.h>
#include <DirectXMath.h>
#include <string>
#include <vector>
#include <strsafe.h>
#include <codecapi.h>
#include <mfapi.h>
#include <optional>
#include <wincodec.h>
#include "util.h"

typedef void(__stdcall *CallbackNewFrameDataFunction)(int, byte *, int, int, int);

struct FRAME_BITMAP_DATA {
	int Stride;
	int Width;
	int Height;
	byte *Data;
	int Length;
	FRAME_BITMAP_DATA() :
		Stride(0),
		Width(0),
		Height(0),
		Data(nullptr),
		Length(0) {
	}
	FRAME_BITMAP_DATA(int stride, byte *data, int length, int width, int height) :FRAME_BITMAP_DATA() {
		Stride = stride;
		Data = data;
		Length = length;
		Width = width;
		Height = height;
	}
};

struct FRAME_AUDIO_SOURCE {
	std::wstring Id;
	double Volume;
	std::vector<BYTE> Data;
	FRAME_AUDIO_SOURCE() :
		Id(L""),
		Volume(0) {
	}
	FRAME_AUDIO_SOURCE(std::wstring id, double volume) :FRAME_AUDIO_SOURCE() {
		Id = id;
		Volume = volume;
	}
	FRAME_AUDIO_SOURCE(std::wstring id, double volume, const std::vector<BYTE> &data) :FRAME_AUDIO_SOURCE(id, volume) {
		Data = data;
	}
};
struct FRAME_AUDIO_INFO {
	double Gain;
	std::vector<BYTE> Data;
	std::vector<FRAME_AUDIO_SOURCE> Sources;
	size_t PaddedBytes;
	FRAME_AUDIO_INFO() :
		Sources{},
		Gain(0),
		PaddedBytes(0) {
	}
	FRAME_AUDIO_INFO(double gain, const std::vector<FRAME_AUDIO_SOURCE> &sources) :FRAME_AUDIO_INFO() {
		Sources = sources;
		Gain = gain;
	}
};

struct FRAME_AUDIO_DATA {
public:
	std::vector<BYTE> Data;
	std::unique_ptr<FRAME_AUDIO_INFO> Info;
	UINT64 QpcTimestamp;

	FRAME_AUDIO_DATA() :
		QpcTimestamp(0),
		Data{},
		Info(nullptr) {
	}
	FRAME_AUDIO_DATA(const std::vector<BYTE> &data, FRAME_AUDIO_INFO *info, const UINT64 qpcTimestamp) :
		Data(data), Info(std::move(info)), QpcTimestamp(qpcTimestamp)
	{

	}
	~FRAME_AUDIO_DATA() {

	}
};

struct REC_RESULT {
	HRESULT RecordingResult;
	HRESULT FinalizeResult;
	std::wstring Error;
	REC_RESULT() :
		RecordingResult(E_FAIL),
		FinalizeResult(E_FAIL),
		Error(L"")
	{

	}
	REC_RESULT(HRESULT recordingResult, std::wstring error = L"") :
		REC_RESULT()
	{
		RecordingResult = recordingResult;
		Error = error;
	}
};
struct CAPTURE_RESULT :REC_RESULT {
	// Used to indicate a transition event occurred e.g. PnpStop, PnpStart, mode change, TDR, desktop switch, and the application needs to recreate the capture interface
	bool IsRecoverableError;
	// Used to indicate that the D3D11 device no longer is valid, and the application should destroy and recreate it.
	bool IsDeviceError;
	// Number of times to retry a recoverable error.
	int NumberOfRetries;
	CAPTURE_RESULT() :
		IsRecoverableError(false),
		IsDeviceError(false),
		NumberOfRetries(INFINITE),
		REC_RESULT()
	{

	}
	CAPTURE_RESULT(HRESULT recordingResult, std::wstring error = L"") :
		IsRecoverableError(false),
		IsDeviceError(false),
		NumberOfRetries(INFINITE),
		REC_RESULT(recordingResult, error)
	{

	}
};
struct SIZE_F {
	float cx;
	float cy;
};
//
// Holds info about the pointer/cursor
//
struct PTR_INFO
{
	_Field_size_bytes_(BufferSize) BYTE *PtrShapeBuffer;
	DXGI_OUTDUPL_POINTER_SHAPE_INFO ShapeInfo;
	POINT Position;
	POINT Offset;
	SIZE_F Scale;
	bool Visible;
	bool IsPointerShapeUpdated;
	UINT BufferSize;
	RECT WhoUpdatedPositionLast;
	LARGE_INTEGER LastTimeStamp;

	PTR_INFO() :
		Position{},
		Offset{},
		Scale{ 1.0, 1.0 },
		Visible(false),
		IsPointerShapeUpdated(false),
		BufferSize(0),
		WhoUpdatedPositionLast{},
		LastTimeStamp{},
		PtrShapeBuffer(nullptr)
	{
		RtlZeroMemory(&ShapeInfo, sizeof(ShapeInfo));
	}
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
	//Contains the mouse cursor info for the frame, if any.
	std::optional<PTR_INFO> PtrInfo;
	//The number of updates written to the current frame since last fetch.
	int FrameUpdateCount;
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

enum class AudioClientKind
{
	Endpoint,
	EndpointLoopback,
	ProcessLoopback
};

struct RECORDING_SOURCE_BASE abstract {
private:
	std::vector<CallbackNewFrameDataFunction> m_NewFrameDataCallbacks;
public:
	std::wstring SourcePath;
	IStream *SourceStream;
	HWND SourceWindow;
	RecordingSourceType Type;
	std::wstring ID;
	/// <summary>
	/// Stretch mode for the frame
	/// </summary>
	TextureStretchMode Stretch;
	/// <summary>
	/// The index for a MediaType describing a capture format. This is used to select e.g resolution from cameras.
	/// </summary>
	std::optional<int> CaptureFormatIndex;
	/// <summary>
	/// Optional custom output size of the source frame. May be both smaller or larger than the source.
	/// </summary>
	std::optional<SIZE> OutputSize;
	/// <summary>
	/// The anchor position for the content inside the parent frame.
	/// </summary>
	ContentAnchor Anchor;
	/// <summary>
	/// Determines if the source is capturing video. If false, it will be blacked out.
	/// </summary>
	std::optional<bool> IsVideoCaptureEnabled;
	/// <summary>
	/// Determines if the source is capturing mouse cursors. If false, it will be hidden.
	/// </summary>
	std::optional<bool> IsCursorCaptureEnabled;
	/// <summary>
	/// Toggles the display of a yellow border around recorded displays and windows when using Windows Graphics Capture on Windows 10 2104 or newer. If false, it will be hidden.
	/// </summary>
	std::optional<bool> IsBorderRequired;
	/// <summary>
	/// Toggles video frame preview on and off for this source.
	/// </summary>
	std::optional<bool> IsVideoFramePreviewEnabled;
	/// <summary>
	/// The requested dimensions of the frame preview bitmap
	/// </summary>
	std::optional<SIZE> VideoFramePreviewSize;

	RECORDING_SOURCE_BASE() :
		Type(RecordingSourceType::Display),
		SourceWindow(nullptr),
		SourcePath(L""),
		SourceStream(nullptr),
		OutputSize{ std::nullopt },
		ID(L""),
		Stretch(TextureStretchMode::Uniform),
		Anchor(ContentAnchor::TopLeft),
		IsVideoCaptureEnabled(std::nullopt),
		IsCursorCaptureEnabled(std::nullopt),
		IsBorderRequired(std::nullopt),
		IsVideoFramePreviewEnabled(std::nullopt),
		VideoFramePreviewSize(std::nullopt),
		m_NewFrameDataCallbacks{}
	{

	}
	virtual ~RECORDING_SOURCE_BASE() {

	}
	void RegisterCallback(CallbackNewFrameDataFunction callback)
	{
		this->m_NewFrameDataCallbacks.push_back(callback);
	}

	void UnregisterCallback(CallbackNewFrameDataFunction callback)
	{
		m_NewFrameDataCallbacks.erase(std::remove(m_NewFrameDataCallbacks.begin(), m_NewFrameDataCallbacks.end(), callback), m_NewFrameDataCallbacks.end());
	}

	void NotifyNewFrameDataCallbacks(int stride, byte *data, int len, int width, int height) {
		for each (CallbackNewFrameDataFunction callback in std::vector(m_NewFrameDataCallbacks))
		{
			callback(stride, data, len, width, height);
		}
	}
	bool HasRegisteredCallbacks() {
		return m_NewFrameDataCallbacks.size() > 0;
	}
};

struct RECORDING_OVERLAY :RECORDING_SOURCE_BASE
{
	/// <summary>
	/// Optional custom offset for the source frame.
	/// </summary>
	std::optional<SIZE> Offset;
	RECORDING_OVERLAY() :
		RECORDING_SOURCE_BASE(),
		Offset{ std::nullopt }
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
	RECORDING_OVERLAY_DATA() :RecordingOverlay{ nullptr } {}
	RECORDING_OVERLAY_DATA(RECORDING_OVERLAY *overlay)
		:RecordingOverlay{ overlay }
	{

	}
};

struct RECORDING_SOURCE : RECORDING_SOURCE_BASE
{
	std::optional<RecordingSourceApi> SourceApi;
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
	// Used to signal an error in the ongoing capture
	HANDLE ErrorEvent{};
	// Used to signal capture has started
	HANDLE StartedEvent{};
	// Used by WinProc to signal to threads to exit
	HANDLE TerminateThreadsEvent{};
	LARGE_INTEGER LastUpdateTimeStamp{};
	CAPTURE_RESULT *ThreadResult{ };
};

//
// Structure to pass to a new thread
//
struct CAPTURE_THREAD_DATA :THREAD_DATA_BASE
{
	RECORDING_SOURCE_DATA *RecordingSource{ nullptr };
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

struct CAPTURE_THREAD {
	HANDLE ThreadHandle{ nullptr };
	CAPTURE_THREAD_DATA *ThreadData{ nullptr };
};

struct OVERLAY_THREAD {
	HANDLE ThreadHandle{ nullptr };
	OVERLAY_THREAD_DATA *ThreadData{ nullptr };
};

struct AUDIO_SOURCE {
	std::wstring ID;
	std::wstring DeviceName;
	AudioClientKind Kind;
	bool IsEnabled;
	float OutputVolumeModifier;
	bool ForceMono;
	int MasterChannel;

	AUDIO_SOURCE() :
		Kind(AudioClientKind::EndpointLoopback),
		DeviceName(L""),
		IsEnabled(true),
		OutputVolumeModifier(1.0),
		ForceMono(false),
		MasterChannel(0) {

	}

	AUDIO_SOURCE(std::wstring id, AudioClientKind kind) :AUDIO_SOURCE() {
		ID = id;
		Kind = kind;
	}

	friend bool operator== (const AUDIO_SOURCE &a, const AUDIO_SOURCE &b) {
		return a.ID == b.ID;
	}
	friend bool operator!= (const AUDIO_SOURCE &a, const AUDIO_SOURCE &b) {
		return !(a.ID == b.ID);
	}
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

	bool IsMouseClicksDetected() const { return m_IsMouseClicksDetected; }
	bool IsMousePointerEnabled() const { return m_IsMousePointerEnabled; }
	std::string GetMouseClickDetectionLMBColor() const { return m_MouseClickDetectionLMBColor; }
	std::string GetMouseClickDetectionRMBColor() const { return m_MouseClickDetectionRMBColor; }
	UINT32 GetMouseClickDetectionRadius() const { return  m_MouseClickDetectionRadius; }
	UINT32 GetMouseClickDetectionMode() const { return m_MouseClickDetectionMode; }
	UINT32 GetMouseClickDetectionDurationMillis() const { return m_MouseClickDetectionDurationMillis; }
};

struct AUDIO_OPTIONS {
protected:
#pragma region Format constants
	const GUID	 AUDIO_ENCODING_FORMAT = MFAudioFormat_AAC;
	const UINT32 AUDIO_BITS_PER_SAMPLE = 16; //Audio bits per sample must be 16.
	const UINT32 AUDIO_SAMPLES_PER_SECOND = 48000;//Audio samples per seconds must be 44100 or 48000.
#pragma endregion

	std::vector<AUDIO_SOURCE *> m_AudioSources;
	bool m_IsAudioEnabled = false;
	UINT32 m_AudioBitrate = (96 / 8) * 1000;	//Bitrate in bytes per second. Only 96,128,160 and 192kbps is supported.
	UINT32 m_AudioChannels = 2;					//Number of audio channels. 1,2 and 6 is supported. 6 only on windows 8 and up.
	float m_MasterVolumeModifier = 1;
	UINT32 m_InputMasterChannel = 0;
	bool m_IsAudioDataPreviewEnabled = false;

	void Notify(HANDLE h) {
		SetEvent(h);
	}
	inline void ClearAudioSources() {
		for each (AUDIO_SOURCE * source in m_AudioSources)
		{
			delete source;
		}
		m_AudioSources.clear();
	}
public:
	HANDLE OnPropertyChangedEvent;
	AUDIO_OPTIONS() {
		OnPropertyChangedEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	}
	~AUDIO_OPTIONS() {
		ClearAudioSources();
		CloseHandle(OnPropertyChangedEvent);
	}
	void Notify() { Notify(OnPropertyChangedEvent); }
	void SetMasterVolume(float volume) { m_MasterVolumeModifier = volume; }
	void SetAudioBitrate(UINT32 bitrate) { m_AudioBitrate = bitrate; Notify(OnPropertyChangedEvent); }
	void SetAudioChannels(UINT32 channels) { m_AudioChannels = channels; Notify(OnPropertyChangedEvent); }
	void SetAudioEnabled(bool value) { m_IsAudioEnabled = value; Notify(OnPropertyChangedEvent); }
	void SetInputDeviceMasterChannel(int value) { m_InputMasterChannel = value; Notify(OnPropertyChangedEvent); }
	void SetAudioDataPreviewEnabled(bool value) { m_IsAudioDataPreviewEnabled = value; }
	void SetAudioSources(std::vector<AUDIO_SOURCE> &sources, std::optional<bool> notify = true) {

		bool itemsChanged = false;
		if (sources.size() != m_AudioSources.size()) {
			itemsChanged = true;
		}
		else {
			for each (AUDIO_SOURCE * source in m_AudioSources)
			{
				for (size_t i = 0; i < sources.size(); i++)
					if (*m_AudioSources[i] != sources[i])
					{
						itemsChanged = true;
						break;
					}
			}
		}
		ClearAudioSources();
		for each (AUDIO_SOURCE source in sources)
		{
			m_AudioSources.push_back(new AUDIO_SOURCE(source));
		}
		if (notify.value_or(true) && itemsChanged) {
			Notify(OnPropertyChangedEvent);
		}
	}

	bool IsAudioEnabled() const { return m_IsAudioEnabled; }
	UINT32 GetAudioBitrate() const { return m_AudioBitrate; }
	UINT32 GetAudioChannels() const { return m_AudioChannels; }
	float GetMasterVolume() const { return m_MasterVolumeModifier; }
	GUID GetAudioEncoderFormat() const { return AUDIO_ENCODING_FORMAT; }
	UINT32 GetAudioBitsPerSample() const { return AUDIO_BITS_PER_SAMPLE; }
	UINT32 GetAudioSamplesPerSecond() const { return AUDIO_SAMPLES_PER_SECOND; }
	UINT32 GetInputMasterChannel() const { return m_InputMasterChannel; }
	bool IsAudioDataPreviewEnabled() const { return m_IsAudioDataPreviewEnabled; }
	std::vector<AUDIO_SOURCE *> &GetAudioSources() { return m_AudioSources; }
};

struct OUTPUT_OPTIONS {
protected:
	std::optional<SIZE> m_FrameSize{};
	std::optional<RECT> m_SourceRect{};
	TextureStretchMode m_Stretch = TextureStretchMode::Uniform;
	RecorderModeInternal m_RecorderMode = RecorderModeInternal::Video;
	bool m_IsVideoCaptureEnabled = true;
	bool m_IsVideoFramePreviewEnabled = false;
	std::optional<SIZE> m_VideoFramePreviewSize{};
public:
	std::optional<SIZE> GetFrameSize() const { return m_FrameSize; }
	void SetFrameSize(SIZE size) { m_FrameSize = size; }
	void SetSourceRectangle(RECT rect) { m_SourceRect = MakeRectEven(rect); }
	std::optional<RECT> GetSourceRectangle() const { return m_SourceRect; }
	void SetStretch(TextureStretchMode stretch) { m_Stretch = stretch; }
	TextureStretchMode GetStretch() const { return m_Stretch; }
	RecorderModeInternal GetRecorderMode() const { return m_RecorderMode; }
	void SetRecorderMode(RecorderModeInternal recorderMode) { m_RecorderMode = recorderMode; }
	bool IsVideoCaptureEnabled() const { return m_IsVideoCaptureEnabled; }
	void SetVideoCaptureEnabled(bool value) { m_IsVideoCaptureEnabled = value; }
	void SetVideoFramePreviewEnabled(bool value) { m_IsVideoFramePreviewEnabled = value; }
	void SetVideoFramePreviewSize(SIZE value) { m_VideoFramePreviewSize = value; }
	bool IsVideoFramePreviewEnabled() const { return m_IsVideoFramePreviewEnabled; }
	std::optional<SIZE> GetVideoFramePreviewSize() const { return m_VideoFramePreviewSize; }
};

struct ENCODER_OPTIONS abstract {
protected:
#pragma region Format constants
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
	void SetFixedFramerate(bool value) { m_IsFixedFramerate = value; }
	void SetThrottlingDisabled(bool value) { m_IsThrottlingDisabled = value; }
	void SetFastStartEnabled(bool value) { m_IsMp4FastStartEnabled = value; }
	void SetFragmentedMp4Enabled(bool value) { m_IsFragmentedMp4Enabled = value; }
	void SetHardwareEncodingEnabled(bool value) { m_IsHardwareEncodingEnabled = value; }
	void SetLowLatencyModeEnabled(bool value) { m_IsLowLatencyModeEnabled = value; }
	void SetVideoBitrateMode(UINT32 bitrateMode) { m_VideoBitrateControlMode = bitrateMode; }
	void SetEncoderProfile(UINT32 profile) { m_EncoderProfile = profile; }

	UINT32 GetVideoFps() const { return m_VideoFps; }
	UINT32 GetVideoBitrate() const { return m_VideoBitrate; }
	UINT32 GetVideoQuality() const { return m_VideoQuality; }
	bool IsFixedFramerate() const { return  m_IsFixedFramerate; }
	bool IsThrottlingDisabled() const { return  m_IsThrottlingDisabled; }
	bool IsFastStartEnabled() const { return m_IsMp4FastStartEnabled; }
	bool IsFragmentedMp4Enabled() const { return m_IsFragmentedMp4Enabled; }
	bool IsHardwareEncodingEnabled() const { return m_IsHardwareEncodingEnabled; }
	bool IsLowLatencyModeEnabled() const { return m_IsLowLatencyModeEnabled; }
	UINT32 GetVideoBitrateMode() const { return m_VideoBitrateControlMode; }
	UINT32 GetEncoderProfile() const { return m_EncoderProfile; }

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
	UINT32 m_SnapshotsInterval = 10000;
	bool m_TakesSnapshotsWithVideo = false;
	GUID m_ImageEncoderFormat = GUID_ContainerFormatPng;
public:
	void SetTakeSnapshotsWithVideo(bool isEnabled) { m_TakesSnapshotsWithVideo = isEnabled; }
	void SetSnapshotsWithVideoInterval(UINT32 intervalMillis) { m_SnapshotsInterval = intervalMillis; }
	void SetSnapshotDirectory(std::wstring string) { m_OutputSnapshotsFolderPath = string; }
	void SetSnapshotSaveFormat(GUID value) { m_ImageEncoderFormat = value; }

	bool IsSnapshotWithVideoEnabled() const {
		return m_TakesSnapshotsWithVideo;
	}
	UINT32 GetSnapshotsInterval() const {
		return m_SnapshotsInterval;
	}
	std::wstring GetSnapshotsDirectory() const {
		return m_OutputSnapshotsFolderPath;
	}
	GUID GetSnapshotEncoderFormat() const {
		return m_ImageEncoderFormat;
	}


	std::wstring GetImageExtension() const {
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