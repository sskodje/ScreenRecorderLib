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
// FRAME_DATA holds information about an acquired frame
//
typedef struct _FRAME_DATA
{
    ID3D11Texture2D* Frame;
    DXGI_OUTDUPL_FRAME_INFO FrameInfo;
    _Field_size_bytes_((MoveCount * sizeof(DXGI_OUTDUPL_MOVE_RECT)) + (DirtyCount * sizeof(RECT))) BYTE* MetaData;
    UINT DirtyCount;
    UINT MoveCount;
} FRAME_DATA;

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

    // Used to indicate a transition event occurred e.g. PnpStop, PnpStart, mode change, TDR, desktop switch and the application needs to recreate the duplication interface
    HANDLE ExpectedErrorEvent;

    // Used by WinProc to signal to threads to exit
    HANDLE TerminateThreadsEvent;

    HANDLE TexSharedHandle;
    std::wstring Output;
    INT OffsetX;
    INT OffsetY;
    PTR_INFO* PtrInfo;
    DX_RESOURCES DxRes;
    LARGE_INTEGER LastUpdateTimeStamp;
    INT UpdatedFrameCount;
    HRESULT ThreadResult;
} THREAD_DATA;