#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <d3d11.h>
#include <DirectXMath.h>
#include <dxgi1_2.h>
#include <d2d1.h>
#include <atlbase.h>

#pragma comment(lib, "D2d1.lib")

class mouse_pointer
{
public:
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

	HRESULT Initialize(ID3D11DeviceContext *ImmediateContext, ID3D11Device *Device);

	HRESULT DrawMousePointer(_In_ PTR_INFO *PtrInfo, _In_ ID3D11DeviceContext *DeviceContext, _In_ ID3D11Device *Device, _Inout_ ID3D11Texture2D* targetTexture, DXGI_MODE_ROTATION rotation, _In_ ID3D11Texture2D* desktopTexture = nullptr);
	HRESULT DrawMouseClick(_In_ PTR_INFO *PtrInfo, _In_ ID3D11Texture2D *bgTexture, std::string colorStr, float radius, DXGI_MODE_ROTATION rotation);
	HRESULT GetMouse(_Inout_ PTR_INFO *PtrInfo, _In_ DXGI_OUTDUPL_FRAME_INFO *FrameInfo, RECT screenRect, IDXGIOutputDuplication* DeskDupl);
	HRESULT GetMouse(_Inout_ PTR_INFO *PtrInfo, int offsetX, int offsetY, RECT screenRect, bool getShapeBuffer);
	void CleanupResources();
private:
#pragma region Mouse Drawing
	//monitor that last updated mouse pointer
	UINT m_OutputNumber = 0;
	ATL::CComPtr<ID3D11SamplerState> m_SamplerLinear;
	ATL::CComPtr<ID3D11BlendState> m_BlendState;
	ATL::CComPtr<ID3D11VertexShader> m_VertexShader;
	ATL::CComPtr<ID3D11PixelShader> m_PixelShader;
	ATL::CComPtr<ID3D11InputLayout> m_InputLayout;
	ATL::CComPtr<ID2D1Factory> m_D2DFactory;
#pragma endregion


	std::vector<BYTE> _InitBuffer;
	std::vector<BYTE> _DesktopBuffer;

	long ParseColorString(std::string color);
	float GetCurrentDpi();
	void GetPointerPosition(_In_ PTR_INFO *PtrInfo, DXGI_MODE_ROTATION rotation, int desktopWidth, int desktopHeight, _Out_ INT *PtrLeft, _Out_ INT *PtrTop);
	HRESULT ProcessMonoMask(_In_ ID3D11Texture2D* desktopTexture, _In_ ID3D11DeviceContext *DeviceContext, _In_ ID3D11Device *Device, DXGI_MODE_ROTATION rotation, bool IsMono, _Inout_ PTR_INFO *PtrInfo, _Out_ INT *PtrWidth, _Out_ INT *PtrHeight, _Out_ INT *PtrLeft, _Out_ INT *PtrTop, _Outptr_result_bytebuffer_(*PtrHeight * *PtrWidth * BPP) BYTE **InitBuffer, _Out_ D3D11_BOX *Box);
	HRESULT InitShaders(ID3D11DeviceContext *DeviceContext, ID3D11Device *Device);
	HRESULT InitMouseClickTexture(ID3D11DeviceContext *ImmediateContext, ID3D11Device *Device);
	HRESULT ResizeShapeBuffer(PTR_INFO* PtrInfo, int bufferSize);
};

