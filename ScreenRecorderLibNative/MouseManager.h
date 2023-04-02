#pragma once
#include <DirectXMath.h>
#include <d2d1.h>
#include <atlbase.h>
#include <memory>
#include "CommonTypes.h"
#include "TextureManager.h"

LRESULT CALLBACK MouseHookProc(int nCode, WPARAM wParam, LPARAM lParam);

class MouseManager
{
public:
	MouseManager();
	~MouseManager();

	HRESULT Initialize(_In_ ID3D11DeviceContext *pDeviceContext, _In_ ID3D11Device *pDevice, _In_ std::shared_ptr<MOUSE_OPTIONS> &pOptions);
	void InitializeMouseClickDetection();
	void StopMouseClickDetection();
	HRESULT ProcessMousePointer(_In_ ID3D11Texture2D *pFrame, _In_ PTR_INFO *pPtrInfo);
	HRESULT GetMouse(_Inout_ PTR_INFO *pPtrInfo, _In_ bool getShapeBuffer, _In_ DXGI_OUTDUPL_FRAME_INFO *pFrameInfo, _In_ RECT screenRect, _In_ IDXGIOutputDuplication *pDeskDupl, _In_ int offsetX, _In_ int offsetY);
	HRESULT GetMouse(_Inout_ PTR_INFO *pPtrInfo, _In_ bool getShapeBuffer, _In_ int offsetX, _In_ int offsetY);
	void CleanDX();
protected:
	HRESULT DrawMousePointer(_In_ PTR_INFO *pPtrInfo, _Inout_ ID3D11Texture2D *pBbgTexture, DXGI_MODE_ROTATION rotation);
	HRESULT DrawMouseClick(_In_ PTR_INFO *pPtrInfo, _In_ ID3D11Texture2D *pBgTexture, std::string colorStr, float radius, DXGI_MODE_ROTATION rotation);
private:
	static const UINT TRANSPARENT_WHITE = 0x00FFFFFF;
	static const UINT TRANSPARENT_BLACK = 0x00000000;
	static const UINT OPAQUE_WHITE = 0xFFFFFFFF;
	static const UINT OPAQUE_BLACK = 0xFF000000;
	static const int NUMVERTICES = 6;
	static const int BPP = 4;

	ATL::CComPtr<ID3D11SamplerState> m_SamplerLinear;
	ATL::CComPtr<ID3D11BlendState> m_BlendState;
	ATL::CComPtr<ID3D11VertexShader> m_VertexShader;
	ATL::CComPtr<ID3D11PixelShader> m_PixelShader;
	ATL::CComPtr<ID3D11InputLayout> m_InputLayout;
	ATL::CComPtr<ID2D1Factory> m_D2DFactory;

	std::unique_ptr<TextureManager> m_TextureManager;
	std::shared_ptr<MOUSE_OPTIONS> m_MouseOptions;
	ID3D11DeviceContext *m_DeviceContext;
	ID3D11Device *m_Device;

	CRITICAL_SECTION m_CriticalSection;
	bool m_IsCapturingMouseClicks;
	std::chrono::steady_clock::time_point m_LastMouseDrawTimeStamp;
	HANDLE m_MouseHookThread;
	DWORD m_MouseHookThreadId;
	std::vector<BYTE> _InitBuffer;
	std::vector<BYTE> _DesktopBuffer;
	HANDLE m_StopPollingTaskEvent;

	long ParseColorString(std::string color);
	void GetPointerPosition(_In_ PTR_INFO *pPtrInfo, DXGI_MODE_ROTATION rotation, int desktopWidth, int desktopHeight, _Out_ INT *PtrLeft, _Out_ INT *PtrTop);
	HRESULT ProcessMonoMask(_In_ ID3D11Texture2D *pBgTexture, _In_ DXGI_MODE_ROTATION rotation, _In_ bool IsMono, _Inout_ PTR_INFO *PtrInfo, _Out_ INT *PtrWidth, _Out_ INT *PtrHeight, _Out_ INT *PtrLeft, _Out_ INT *PtrTop, _Outptr_result_bytebuffer_(*PtrHeight **PtrWidth *BPP) BYTE **pInitBuffer);

	HRESULT InitMouseClickTexture(_In_ ID3D11DeviceContext *pDeviceContext, _In_ ID3D11Device *pDevice);
	HRESULT ResizeShapeBuffer(_Inout_ PTR_INFO *pPtrInfo, _In_ int bufferSize);
};

