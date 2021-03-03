#pragma once
#include <d3d11.h>
#include <dxgi1_2.h>
#include <sal.h>
#include <DirectXMath.h>
#include "PixelShader.h"
#include "VertexShader.h"
#include <string>
#include <vector>

#define NUMVERTICES 6
#define BPP         4
//
// Holds info about the pointer/cursor
//
typedef struct _PTR_INFO
{
	_Field_size_bytes_(BufferSize) BYTE* PtrShapeBuffer;
	DXGI_OUTDUPL_POINTER_SHAPE_INFO ShapeInfo;
	POINT Position;
	bool Visible;
	bool IsPointerShapeUpdated;
	UINT BufferSize;
	UINT WhoUpdatedPositionLast;
	LARGE_INTEGER LastTimeStamp;
} PTR_INFO;

//
// FRAME_INFO holds information about an acquired generic video frame
//
typedef struct _FRAME_INFO
{
	_Field_size_bytes_(BufferSize) BYTE* PtrFrameBuffer;
	UINT BufferSize;
	LONG Stride;
	UINT Width;
	UINT Height;
	LARGE_INTEGER LastTimeStamp;
} FRAME_INFO;

//
// A vertex with a position and texture coordinate
//
typedef struct _VERTEX
{
	DirectX::XMFLOAT3 Pos;
	DirectX::XMFLOAT2 TexCoord;
} VERTEX;

//
// DUPL_FRAME_DATA holds information about an acquired Desktop Duplication frame
//
typedef struct _DUPL_FRAME_DATA
{
	ID3D11Texture2D* Frame;
	DXGI_OUTDUPL_FRAME_INFO FrameInfo;
	_Field_size_bytes_((MoveCount * sizeof(DXGI_OUTDUPL_MOVE_RECT)) + (DirtyCount * sizeof(RECT))) BYTE* MetaData;
	UINT DirtyCount;
	UINT MoveCount;
} DUPL_FRAME_DATA;

//
// GRAPHICS_FRAME_DATA holds information about an acquired Windows Graphics Capture frame
//
typedef struct _GRAPHICS_FRAME_DATA
{
	ID3D11Texture2D* Frame;
	SIZE ContentSize;
} GRAPHICS_FRAME_DATA;

//
// Structure that holds D3D resources not directly tied to any one thread
//
typedef struct _DX_RESOURCES
{
	ID3D11Device* Device;
	ID3D11DeviceContext* Context;
} DX_RESOURCES;

//
// CAPTURED_FRAME holds information about a generic captured frame
//
typedef struct _CAPTURED_FRAME
{
	PTR_INFO* PtrInfo;
	ID3D11Texture2D* Frame;
	//The number of updates written to the current frame since last fetch.
	int UpdateCount;
	LARGE_INTEGER Timestamp;
	SIZE ContentSize;
} CAPTURED_FRAME;

enum class OverlayType {
	Picture,
	Video,
	VideoCapture
};
enum class SourceType {
	Monitor,
	Window
};
typedef struct _RECORDING_OVERLAY
{
	std::wstring CaptureDevice;
	OverlayType Type;
	POINT Position;
	SIZE Size;
} RECORDING_OVERLAY;

typedef struct _RECORDING_SOURCE
{
	std::wstring CaptureDevice;
	HWND WindowHandle;
	SourceType Type;
} RECORDING_SOURCE;

typedef struct _RECORDING_SOURCE_DATA {
	std::wstring OutputMonitor{};
	bool IsCursorCaptureEnabled{};
	INT64 TotalUpdatedFrameCount{};
	HWND OutputWindow{};
	INT OffsetX{};
	INT OffsetY{};
	SIZE ContentSize{};
	PTR_INFO* PtrInfo{};
	DX_RESOURCES DxRes{};
} RECORDING_SOURCE_DATA;

typedef struct _RECORDING_OVERLAY_DATA
{
	std::wstring CaptureDevice;
	OverlayType Type;
	POINT Position{};
	SIZE Size{};
	FRAME_INFO *FrameInfo{};
	DX_RESOURCES DxRes{};
} RECORDING_OVERLAY_DATA;

//
// Structure to pass to a new thread
//
typedef struct _THREAD_DATA
{
	// Used to indicate abnormal error condition
	HANDLE UnexpectedErrorEvent{};
	// Used to indicate a transition event occurred e.g. PnpStop, PnpStart, mode change, TDR, desktop switch and the application needs to recreate the capture interface
	HANDLE ExpectedErrorEvent{};
	// Used by WinProc to signal to threads to exit
	HANDLE TerminateThreadsEvent{};
	//Handle to shared texture
	HANDLE TexSharedHandle{};
	RECORDING_SOURCE_DATA *RecordingSource{};
	RECORDING_OVERLAY_DATA *RecordingOverlay{};
	LARGE_INTEGER LastUpdateTimeStamp{};
	INT UpdatedFrameCount{};
	HRESULT ThreadResult{ E_FAIL };
} THREAD_DATA;
