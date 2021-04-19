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
