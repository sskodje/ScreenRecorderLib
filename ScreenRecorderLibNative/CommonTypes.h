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
	UINT WhoUpdatedPositionLast;
	LARGE_INTEGER LastTimeStamp;
};

//
// FRAME_INFO holds information about an acquired generic frame
//
struct FRAME_INFO
{
	_Field_size_bytes_(BufferSize) BYTE *PtrFrameBuffer;
	UINT BufferSize;
	LONG Stride;
	UINT Width;
	UINT Height;
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
	bool IsIconic;
};

//
// Structure that holds D3D resources not directly tied to any one thread
//
struct DX_RESOURCES
{
	ID3D11Device *Device;
	ID3D11DeviceContext *Context;
};

//
// CAPTURED_FRAME holds information about a generic captured frame
//
struct CAPTURED_FRAME
{
	PTR_INFO *PtrInfo;
	ID3D11Texture2D *Frame;
	//The number of updates written to the current frame since last fetch.
	int FrameUpdateCount;
	//The number of updates written to the frame overlays since last fetch.
	int OverlayUpdateCount;
	LARGE_INTEGER Timestamp;
	SIZE ContentSize;
};

enum class OverlayAnchor {
	TopLeft,
	TopRight,
	BottomLeft,
	BottomRight
};
enum class OverlayType {
	Picture,
	Video,
	CameraCapture
};
enum class RecordingSourceType {
	Display,
	Window
};

struct RECORDING_SOURCE
{
	std::wstring CaptureDevice;
	HWND WindowHandle;
	RecordingSourceType Type;
	bool IsCursorCaptureEnabled{};
	RECORDING_SOURCE() :
		CaptureDevice(L""),
		WindowHandle(NULL),
		Type(RecordingSourceType::Display),
		IsCursorCaptureEnabled(false) {}

	RECORDING_SOURCE(const RECORDING_SOURCE &source) :
		CaptureDevice(source.CaptureDevice),
		WindowHandle(source.WindowHandle),
		Type(source.Type),
		IsCursorCaptureEnabled(source.IsCursorCaptureEnabled) {}

	friend bool operator< (const RECORDING_SOURCE &a, const RECORDING_SOURCE &b) {
		switch (a.Type)
		{
		case RecordingSourceType::Display: {
			if (b.Type == RecordingSourceType::Window) {
				return true;
			}
			return (a.CaptureDevice.compare(b.CaptureDevice) < 0);
		}
		case RecordingSourceType::Window: {
			if (b.Type == RecordingSourceType::Display) {
				return false;
			}
			return a.WindowHandle < b.WindowHandle;
		}
		default:
			return 0;
			break;
		}
	}
	friend bool operator== (const RECORDING_SOURCE &a, const RECORDING_SOURCE &b) {
		switch (a.Type)
		{
		case RecordingSourceType::Display: {
			return a.Type == b.Type && a.CaptureDevice == b.CaptureDevice;
		}
		case RecordingSourceType::Window: {
			return a.Type == b.Type && a.WindowHandle == b.WindowHandle;
		}
		default:
			return 0;
			break;
		}
	}
};

struct RECORDING_SOURCE_DATA :RECORDING_SOURCE {
	INT OffsetX{};
	INT OffsetY{};
	DX_RESOURCES DxRes{};

	RECORDING_SOURCE_DATA() :
		OffsetX(0),
		OffsetY(0),
		DxRes{}{}
	RECORDING_SOURCE_DATA(const RECORDING_SOURCE &source) :RECORDING_SOURCE(source) {}
};


struct RECORDING_OVERLAY
{
	std::wstring Source;
	OverlayType Type;
	POINT Offset;
	SIZE Size;
	OverlayAnchor Anchor;

	RECORDING_OVERLAY() :
		Source(L""),
		Type(OverlayType::Picture),
		Offset(POINT()),
		Size(SIZE()),
		Anchor(OverlayAnchor::BottomLeft) {}

	RECORDING_OVERLAY(const RECORDING_OVERLAY &overlay) :
		Source(overlay.Source),
		Type(overlay.Type),
		Offset(overlay.Offset),
		Size(overlay.Size),
		Anchor(overlay.Anchor) {}
};

struct RECORDING_OVERLAY_DATA :RECORDING_OVERLAY
{
	FRAME_INFO *FrameInfo{};
	DX_RESOURCES DxRes{};
	RECORDING_OVERLAY_DATA() {}
	RECORDING_OVERLAY_DATA(const RECORDING_OVERLAY &overlay) :RECORDING_OVERLAY(overlay) {}
};

//
// Structure to pass to a new thread
//
struct THREAD_DATA_BASE
{
	// Used to indicate abnormal error condition
	HANDLE UnexpectedErrorEvent{};
	// Used to indicate a transition event occurred e.g. PnpStop, PnpStart, mode change, TDR, desktop switch and the application needs to recreate the capture interface
	HANDLE ExpectedErrorEvent{};
	// Used by WinProc to signal to threads to exit
	HANDLE TerminateThreadsEvent{};
	LARGE_INTEGER LastUpdateTimeStamp{};
	HRESULT ThreadResult{ E_FAIL };
	//Handle to shared texture
	HANDLE TexSharedHandle{};
};

//
// Structure to pass to a new thread
//
struct CAPTURE_THREAD_DATA :THREAD_DATA_BASE
{

	RECORDING_SOURCE_DATA *RecordingSource{};
	INT UpdatedFrameCountSinceLastWrite{};
	INT64 TotalUpdatedFrameCount{};
	RECT ContentFrameRect{};
	PTR_INFO *PtrInfo{};
};

//
// Structure to pass to a new thread
//
struct OVERLAY_THREAD_DATA :THREAD_DATA_BASE
{
	RECORDING_OVERLAY_DATA *RecordingOverlay{};
};


struct ENCODER_OPTIONS abstract {
protected:
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

	virtual GUID GetVideoEncoderFormat() abstract;
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