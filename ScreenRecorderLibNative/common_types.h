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
    ID3D11VertexShader* VertexShader;
    ID3D11PixelShader* PixelShader;
    ID3D11InputLayout* InputLayout;
    ID3D11SamplerState* SamplerLinear;
} DX_RESOURCES;

//
// Structure to pass to a new thread
//
typedef struct _THREAD_DATA
{
    // Used to indicate abnormal error condition
    HANDLE UnexpectedErrorEvent;

    // Used to indicate a transition event occurred e.g. PnpStop, PnpStart, mode change, TDR, desktop switch and the application needs to recreate the capture interface
    HANDLE ExpectedErrorEvent;

    // Used by WinProc to signal to threads to exit
    HANDLE TerminateThreadsEvent;

    HANDLE TexSharedHandle;
    std::wstring OutputMonitor;
    bool IsCursorCaptureEnabled;
    HWND OutputWindow;
    INT OffsetX;
    INT OffsetY;
    SIZE ContentSize;
    PTR_INFO* PtrInfo;
    DX_RESOURCES DxRes;
    LARGE_INTEGER LastUpdateTimeStamp;
    INT UpdatedFrameCount;
    HRESULT ThreadResult;
} THREAD_DATA;

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