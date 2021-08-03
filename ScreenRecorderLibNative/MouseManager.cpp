#pragma warning (disable : 26451)
#include "MouseManager.h"
#include "Log.h"
#include "Util.h"
#include "Cleanup.h"
#include <concrt.h>
#include <ppltasks.h>

using namespace DirectX;
using namespace Concurrency;

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "D2d1.lib")

INT64 g_LastMouseClickDurationRemaining = 0;
INT g_MouseClickDetectionDurationMillis = 50;
UINT g_LastMouseClickButton = 0;

concurrency::task<void> pollingTask = concurrency::task_from_result();

LRESULT CALLBACK MouseHookProc(int nCode, WPARAM wParam, LPARAM lParam)
{
	// MB1 click
	if (wParam == WM_LBUTTONDOWN)
	{
		g_LastMouseClickButton = VK_LBUTTON;
		g_LastMouseClickDurationRemaining = g_MouseClickDetectionDurationMillis;
	}
	else if (wParam == WM_RBUTTONDOWN)
	{
		g_LastMouseClickButton = VK_RBUTTON;
		g_LastMouseClickDurationRemaining = g_MouseClickDetectionDurationMillis;
	}
	return CallNextHookEx(0, nCode, wParam, lParam);
}

MouseManager::MouseManager() :
	m_MouseOptions(nullptr),
	m_DeviceContext(nullptr),
	m_Device(nullptr),
	m_Mousehook(nullptr),
	m_StopPollingTaskEvent(nullptr),
	m_LastMouseDrawTimeStamp(std::chrono::steady_clock::now())
{

}

MouseManager::~MouseManager()
{
	UnhookWindowsHookEx(m_Mousehook);
	SetEvent(m_StopPollingTaskEvent);
	pollingTask.wait();
	CloseHandle(m_StopPollingTaskEvent);
}

HRESULT MouseManager::Initialize(_In_ ID3D11DeviceContext *pDeviceContext, _In_ ID3D11Device *pDevice, _In_ std::shared_ptr<MOUSE_OPTIONS> &pOptions)
{
	CleanDX();
	// Create the sample state
	D3D11_SAMPLER_DESC SampDesc;
	RtlZeroMemory(&SampDesc, sizeof(SampDesc));
	SampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
	SampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
	SampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
	SampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
	SampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
	SampDesc.MinLOD = 0;
	SampDesc.MaxLOD = D3D11_FLOAT32_MAX;
	HRESULT hr = pDevice->CreateSamplerState(&SampDesc, &m_SamplerLinear);
	RETURN_ON_BAD_HR(hr);

	// Create the blend state
	D3D11_BLEND_DESC BlendStateDesc;
	RtlZeroMemory(&BlendStateDesc, sizeof(BlendStateDesc));
	BlendStateDesc.AlphaToCoverageEnable = FALSE;
	BlendStateDesc.IndependentBlendEnable = FALSE;
	BlendStateDesc.RenderTarget[0].BlendEnable = TRUE;
	BlendStateDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
	BlendStateDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
	BlendStateDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
	BlendStateDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
	BlendStateDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
	BlendStateDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
	BlendStateDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
	hr = pDevice->CreateBlendState(&BlendStateDesc, &m_BlendState);
	RETURN_ON_BAD_HR(hr);

	// Initialize shaders
	hr = InitShaders(pDevice, &m_PixelShader, &m_VertexShader, &m_InputLayout);
	hr = InitMouseClickTexture(pDeviceContext, pDevice);
	m_Device = pDevice;
	m_DeviceContext = pDeviceContext;
	m_MouseOptions = pOptions;
	m_StopPollingTaskEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
	InitializeMouseClickDetection();
	return hr;
}

void MouseManager::InitializeMouseClickDetection()
{
	if (m_MouseOptions->IsMouseClicksDetected()) {
		switch (m_MouseOptions->GetMouseClickDetectionMode())
		{
		default:
		case MOUSE_OPTIONS::MOUSE_DETECTION_MODE_POLLING: {
			pollingTask = create_task([this]() {
				LOG_INFO("Starting mouse click polling task");
				while (true) {
					if (GetKeyState(VK_LBUTTON) < 0)
					{
						//If left mouse button is held, reset the duration of click duration
						g_LastMouseClickButton = VK_LBUTTON;
						g_LastMouseClickDurationRemaining = g_MouseClickDetectionDurationMillis;
					}
					else if (GetKeyState(VK_RBUTTON) < 0)
					{
						//If right mouse button is held, reset the duration of click duration
						g_LastMouseClickButton = VK_RBUTTON;
						g_LastMouseClickDurationRemaining = g_MouseClickDetectionDurationMillis;
					}

					if (WaitForSingleObjectEx(m_StopPollingTaskEvent, 0, FALSE) == WAIT_OBJECT_0) {
						break;
					}
					wait(1);
				}
				LOG_INFO("Exiting mouse click polling task");
				});
			break;
		}
		case MOUSE_OPTIONS::MOUSE_DETECTION_MODE_HOOK: {
			m_Mousehook = SetWindowsHookEx(WH_MOUSE_LL, MouseHookProc, nullptr, 0);
			break;
		}
		}
	}
}

HRESULT MouseManager::InitMouseClickTexture(_In_ ID3D11DeviceContext *pDeviceContext, _In_ ID3D11Device *pDevice) {
	HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory), (void **)&m_D2DFactory);
	return hr;
}


long MouseManager::ParseColorString(std::string color)
{
	if (color.length() == 0)
		return D2D1::ColorF::Yellow;
	if (color.front() == '#') {
		color.replace(0, 1, "0x");
	}
	return std::strtoul(color.data(), 0, 16);
}

void MouseManager::GetPointerPosition(_In_ PTR_INFO *pPtrInfo, DXGI_MODE_ROTATION rotation, int desktopWidth, int desktopHeight, _Out_ INT *PtrLeft, _Out_ INT *PtrTop)
{
	switch (rotation)
	{
	default:
	case DXGI_MODE_ROTATION_UNSPECIFIED:
	case DXGI_MODE_ROTATION_IDENTITY:
		*PtrLeft = pPtrInfo->Position.x;
		*PtrTop = pPtrInfo->Position.y;
		break;
	case DXGI_MODE_ROTATION_ROTATE90:
		*PtrLeft = pPtrInfo->Position.y;
		*PtrTop = desktopHeight - pPtrInfo->Position.x - (pPtrInfo->ShapeInfo.Height);
		break;
	case DXGI_MODE_ROTATION_ROTATE180:
		*PtrLeft = desktopWidth - pPtrInfo->Position.x - (pPtrInfo->ShapeInfo.Width);
		*PtrTop = desktopHeight - pPtrInfo->Position.y - (pPtrInfo->ShapeInfo.Height);
		break;
	case DXGI_MODE_ROTATION_ROTATE270:
		*PtrLeft = desktopWidth - pPtrInfo->Position.y - pPtrInfo->ShapeInfo.Height;
		*PtrTop = pPtrInfo->Position.x;
		break;
	}
}

HRESULT MouseManager::ProcessMousePointer(_In_ ID3D11Texture2D *pFrame, _In_ PTR_INFO *pPtrInfo)
{
	HRESULT hr = S_FALSE;
	if (g_LastMouseClickDurationRemaining > 0
		&& m_MouseOptions->IsMouseClicksDetected())
	{
		if (g_LastMouseClickButton == VK_LBUTTON)
		{
			hr = DrawMouseClick(pPtrInfo, pFrame, m_MouseOptions->GetMouseClickDetectionLMBColor(), (float)m_MouseOptions->GetMouseClickDetectionRadius(), DXGI_MODE_ROTATION_UNSPECIFIED);
		}
		if (g_LastMouseClickButton == VK_RBUTTON)
		{
			hr = DrawMouseClick(pPtrInfo, pFrame, m_MouseOptions->GetMouseClickDetectionRMBColor(), (float)m_MouseOptions->GetMouseClickDetectionRadius(), DXGI_MODE_ROTATION_UNSPECIFIED);
		}
		INT64 millisSinceLastMouseDraw = (INT64)max(0, (std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - m_LastMouseDrawTimeStamp).count()));
		g_LastMouseClickDurationRemaining = max(g_LastMouseClickDurationRemaining - millisSinceLastMouseDraw, 0);
		LOG_TRACE("Drawing mouse click, duration remaining on click is %u ms", g_LastMouseClickDurationRemaining);
	}

	if (m_MouseOptions->IsMousePointerEnabled()) {
		hr = DrawMousePointer(pPtrInfo, pFrame, DXGI_MODE_ROTATION_UNSPECIFIED);
	}
	m_LastMouseDrawTimeStamp = std::chrono::steady_clock::now();
	return hr;
}

HRESULT MouseManager::DrawMouseClick(_In_ PTR_INFO *pPtrInfo, _In_ ID3D11Texture2D *pBgTexture, std::string colorStr, float radius, DXGI_MODE_ROTATION rotation)
{
	ATL::CComPtr<IDXGISurface> pSharedSurface;
	HRESULT hr = pBgTexture->QueryInterface(__uuidof(IDXGISurface), (void **)&pSharedSurface);

	// Create the DXGI Surface Render Target.
	UINT dpi = GetDpiForSystem();
	/* RenderTargetProperties contains the description for render target */
	D2D1_RENDER_TARGET_PROPERTIES RenderTargetProperties =
		D2D1::RenderTargetProperties(
			D2D1_RENDER_TARGET_TYPE_DEFAULT,
			D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_PREMULTIPLIED),
			(float)dpi,
			(float)dpi
		);

	ATL::CComPtr<ID2D1RenderTarget> pRenderTarget;
	hr = m_D2DFactory->CreateDxgiSurfaceRenderTarget(pSharedSurface, RenderTargetProperties, &pRenderTarget);

	long colorValue = ParseColorString(colorStr);

	ATL::CComPtr<ID2D1SolidColorBrush> color;

	if (FAILED(pRenderTarget->CreateSolidColorBrush(
		D2D1::ColorF(colorValue, 0.7f), &color))) {

		pRenderTarget->CreateSolidColorBrush(
			D2D1::ColorF(D2D1::ColorF::Yellow, 0.7f), &color);
	}
	DXGI_SURFACE_DESC desc;
	pSharedSurface->GetDesc(&desc);
	D2D1_ELLIPSE ellipse;
	D2D1_POINT_2F mousePoint;
	float dpiScale = dpi / 96.0f;
	INT ptrLeft, ptrTop;
	GetPointerPosition(pPtrInfo, rotation, desc.Width, desc.Height, &ptrLeft, &ptrTop);

	if (rotation == DXGI_MODE_ROTATION_ROTATE90) {
		ptrTop += pPtrInfo->ShapeInfo.Height;
	}
	else if (rotation == DXGI_MODE_ROTATION_ROTATE180) {
		ptrLeft += pPtrInfo->ShapeInfo.Width;
		ptrTop += pPtrInfo->ShapeInfo.Height;
	}
	else if (rotation == DXGI_MODE_ROTATION_ROTATE270) {
		ptrLeft += pPtrInfo->ShapeInfo.Height;
	}
	ptrLeft += pPtrInfo->ShapeInfo.HotSpot.x;
	ptrTop += pPtrInfo->ShapeInfo.HotSpot.y;
	mousePoint.x = ptrLeft / dpiScale;
	mousePoint.y = ptrTop / dpiScale;

	ellipse.point = mousePoint;
	ellipse.radiusX = radius;
	ellipse.radiusY = radius;
	pRenderTarget->BeginDraw();

	pRenderTarget->FillEllipse(ellipse, color);
	pRenderTarget->EndDraw();

	return S_OK;
}

//
// Draw mouse provided in buffer to backbuffer
//
HRESULT MouseManager::DrawMousePointer(_In_ PTR_INFO *pPtrInfo, _Inout_ ID3D11Texture2D *pBgTexture, DXGI_MODE_ROTATION rotation)
{
	if (!pPtrInfo || !pPtrInfo->Visible || pPtrInfo->PtrShapeBuffer == nullptr)
		return S_FALSE;
	// Vars to be used
	ID3D11Texture2D *MouseTex = nullptr;
	ID3D11ShaderResourceView *ShaderRes = nullptr;
	ID3D11Buffer *VertexBufferMouse = nullptr;
	D3D11_SUBRESOURCE_DATA InitData = { 0 };
	D3D11_TEXTURE2D_DESC Desc = { 0 };
	D3D11_SHADER_RESOURCE_VIEW_DESC SDesc;
	D3D11_TEXTURE2D_DESC DesktopDesc = { 0 };
	pBgTexture->GetDesc(&DesktopDesc);
	// Position will be changed based on mouse position
	VERTEX Vertices[NUMVERTICES] =
	{
		{ XMFLOAT3(-1.0f, -1.0f, 0), XMFLOAT2(0.0f, 1.0f) },
		{ XMFLOAT3(-1.0f, 1.0f, 0), XMFLOAT2(0.0f, 0.0f) },
		{ XMFLOAT3(1.0f, -1.0f, 0), XMFLOAT2(1.0f, 1.0f) },
		{ XMFLOAT3(1.0f, -1.0f, 0), XMFLOAT2(1.0f, 1.0f) },
		{ XMFLOAT3(-1.0f, 1.0f, 0), XMFLOAT2(0.0f, 0.0f) },
		{ XMFLOAT3(1.0f, 1.0f, 0), XMFLOAT2(1.0f, 0.0f) },
	};

	INT DesktopWidth = DesktopDesc.Width;
	INT DesktopHeight = DesktopDesc.Height;

	// Center of desktop dimensions
	INT CenterX = (DesktopWidth / 2);
	INT CenterY = (DesktopHeight / 2);

	// Clipping adjusted coordinates / dimensions
	INT PtrWidth = 0;
	INT PtrHeight = 0;
	INT PtrLeft = 0;
	INT PtrTop = 0;

	// Buffer used if necessary (in case of monochrome or masked pointer)
	BYTE *InitBuffer = nullptr;

	Desc.MipLevels = 1;
	Desc.ArraySize = 1;
	Desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	Desc.SampleDesc.Count = 1;
	Desc.SampleDesc.Quality = 0;
	Desc.Usage = D3D11_USAGE_DEFAULT;
	Desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	Desc.CPUAccessFlags = 0;
	Desc.MiscFlags = 0;

	// Set shader resource properties
	SDesc.Format = Desc.Format;
	SDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	SDesc.Texture2D.MostDetailedMip = Desc.MipLevels - 1;
	SDesc.Texture2D.MipLevels = Desc.MipLevels;

	switch (pPtrInfo->ShapeInfo.Type)
	{
	case DXGI_OUTDUPL_POINTER_SHAPE_TYPE_COLOR:
	{
		PtrWidth = static_cast<INT>(pPtrInfo->ShapeInfo.Width);
		PtrHeight = static_cast<INT>(pPtrInfo->ShapeInfo.Height);

		GetPointerPosition(pPtrInfo, rotation, DesktopWidth, DesktopHeight, &PtrLeft, &PtrTop);

		break;
	}
	case DXGI_OUTDUPL_POINTER_SHAPE_TYPE_MONOCHROME:
	{
		ProcessMonoMask(pBgTexture, rotation, true, pPtrInfo, &PtrWidth, &PtrHeight, &PtrLeft, &PtrTop, &InitBuffer);
		break;
	}
	case DXGI_OUTDUPL_POINTER_SHAPE_TYPE_MASKED_COLOR:
	{
		ProcessMonoMask(pBgTexture, rotation, false, pPtrInfo, &PtrWidth, &PtrHeight, &PtrLeft, &PtrTop, &InitBuffer);
		break;
	}
	default:
		LOG_ERROR("Unrecognized mouse pointer type");
		return E_FAIL;
	}
	// VERTEX creation
	if (rotation == DXGI_MODE_ROTATION_UNSPECIFIED
		|| rotation == DXGI_MODE_ROTATION_IDENTITY) {
		Vertices[0].Pos.x = (PtrLeft - CenterX) / (FLOAT)CenterX;
		Vertices[0].Pos.y = -1 * ((PtrTop + PtrHeight) - CenterY) / (FLOAT)CenterY;
		Vertices[1].Pos.x = (PtrLeft - CenterX) / (FLOAT)CenterX;
		Vertices[1].Pos.y = -1 * (PtrTop - CenterY) / (FLOAT)CenterY;
		Vertices[2].Pos.x = ((PtrLeft + PtrWidth) - CenterX) / (FLOAT)CenterX;
		Vertices[2].Pos.y = -1 * ((PtrTop + PtrHeight) - CenterY) / (FLOAT)CenterY;
		Vertices[5].Pos.x = ((PtrLeft + PtrWidth) - CenterX) / (FLOAT)CenterX;
		Vertices[5].Pos.y = -1 * (PtrTop - CenterY) / (FLOAT)CenterY;
	}	//Flip pointer 90 degrees counterclockwise
	else if (rotation == DXGI_MODE_ROTATION_ROTATE90) {
		Vertices[0].Pos.x = ((PtrLeft + PtrWidth) - CenterX) / (FLOAT)CenterX;
		Vertices[0].Pos.y = -1 * ((PtrTop + PtrHeight) - CenterY) / (FLOAT)CenterY;
		Vertices[1].Pos.x = (PtrLeft - CenterX) / (FLOAT)CenterX;
		Vertices[1].Pos.y = -1 * ((PtrTop + PtrHeight) - CenterY) / (FLOAT)CenterY;
		Vertices[2].Pos.x = ((PtrLeft + PtrWidth) - CenterX) / (FLOAT)CenterX;
		Vertices[2].Pos.y = -1 * (PtrTop - CenterY) / (FLOAT)CenterY;
		Vertices[5].Pos.x = (PtrLeft - CenterX) / (FLOAT)CenterX;
		Vertices[5].Pos.y = -1 * (PtrTop - CenterY) / (FLOAT)CenterY;
	}	//Turn pointer upside down
	else if (rotation == DXGI_MODE_ROTATION_ROTATE180) {
		Vertices[0].Pos.x = ((PtrLeft + PtrWidth) - CenterX) / (FLOAT)CenterX;
		Vertices[0].Pos.y = -1 * (PtrTop - CenterY) / (FLOAT)CenterY;
		Vertices[1].Pos.x = ((PtrLeft + PtrWidth) - CenterX) / (FLOAT)CenterX;
		Vertices[1].Pos.y = -1 * ((PtrTop + PtrHeight) - CenterY) / (FLOAT)CenterY;
		Vertices[2].Pos.x = (PtrLeft - CenterX) / (FLOAT)CenterX;
		Vertices[2].Pos.y = -1 * (PtrTop - CenterY) / (FLOAT)CenterY;
		Vertices[5].Pos.x = (PtrLeft - CenterX) / (FLOAT)CenterX;
		Vertices[5].Pos.y = -1 * ((PtrTop + PtrHeight) - CenterY) / (FLOAT)CenterY;
	}	//Flip pointer 90 degrees clockwise
	else if (rotation == DXGI_MODE_ROTATION_ROTATE270) {
		Vertices[0].Pos.x = (PtrLeft - CenterX) / (FLOAT)CenterX;
		Vertices[0].Pos.y = -1 * (PtrTop - CenterY) / (FLOAT)CenterY;
		Vertices[1].Pos.x = ((PtrLeft + PtrWidth) - CenterX) / (FLOAT)CenterX;
		Vertices[1].Pos.y = -1 * (PtrTop - CenterY) / (FLOAT)CenterY;
		Vertices[2].Pos.x = (PtrLeft - CenterX) / (FLOAT)CenterX;
		Vertices[2].Pos.y = -1 * ((PtrTop + PtrHeight) - CenterY) / (FLOAT)CenterY;
		Vertices[5].Pos.x = ((PtrLeft + PtrWidth) - CenterX) / (FLOAT)CenterX;
		Vertices[5].Pos.y = -1 * ((PtrTop + PtrHeight) - CenterY) / (FLOAT)CenterY;
	}

	Vertices[3].Pos.x = Vertices[2].Pos.x;
	Vertices[3].Pos.y = Vertices[2].Pos.y;
	Vertices[4].Pos.x = Vertices[1].Pos.x;
	Vertices[4].Pos.y = Vertices[1].Pos.y;

	// Set texture properties
	Desc.Width = PtrWidth;
	Desc.Height = PtrHeight;

	// Set up init data
	InitData.pSysMem = (pPtrInfo->ShapeInfo.Type == DXGI_OUTDUPL_POINTER_SHAPE_TYPE_COLOR) ? pPtrInfo->PtrShapeBuffer : InitBuffer;
	InitData.SysMemPitch = (pPtrInfo->ShapeInfo.Type == DXGI_OUTDUPL_POINTER_SHAPE_TYPE_COLOR) ? pPtrInfo->ShapeInfo.Pitch : PtrWidth * BPP;
	InitData.SysMemSlicePitch = 0;

	// Create mouseshape as texture
	HRESULT hr = m_Device->CreateTexture2D(&Desc, &InitData, &MouseTex);
	if (FAILED(hr))
	{
		_com_error err(hr);
		LOG_ERROR(L"Failed to create mouse pointer texture: %ls", err.ErrorMessage());
		return hr;
	}

	// Create shader resource from texture
	hr = m_Device->CreateShaderResourceView(MouseTex, &SDesc, &ShaderRes);
	if (FAILED(hr))
	{
		MouseTex->Release();
		MouseTex = nullptr;
		_com_error err(hr);
		LOG_ERROR(L"Failed to create shader resource from mouse pointer texture: %ls", err.ErrorMessage());
		return hr;
	}

	D3D11_BUFFER_DESC BDesc;
	ZeroMemory(&BDesc, sizeof(D3D11_BUFFER_DESC));
	BDesc.Usage = D3D11_USAGE_DEFAULT;
	BDesc.ByteWidth = sizeof(VERTEX) * NUMVERTICES;
	BDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	BDesc.CPUAccessFlags = 0;

	ZeroMemory(&InitData, sizeof(D3D11_SUBRESOURCE_DATA));
	InitData.pSysMem = Vertices;

	// Create vertex buffer
	hr = m_Device->CreateBuffer(&BDesc, &InitData, &VertexBufferMouse);
	if (FAILED(hr))
	{
		ShaderRes->Release();
		ShaderRes = nullptr;
		MouseTex->Release();
		MouseTex = nullptr;

		_com_error err(hr);
		LOG_ERROR(L"Failed to create mouse pointer vertex buffer: %ls", err.ErrorMessage());
		return hr;
	}
	ID3D11RenderTargetView *RTV;
	// Create a render target view
	hr = m_Device->CreateRenderTargetView(pBgTexture, nullptr, &RTV);
	// Set resources
	FLOAT BlendFactor[4] = { 0.f, 0.f, 0.f, 0.f };
	UINT Stride = sizeof(VERTEX);
	UINT Offset = 0;
	m_DeviceContext->IASetVertexBuffers(0, 1, &VertexBufferMouse, &Stride, &Offset);
	m_DeviceContext->OMSetBlendState(m_BlendState.p, BlendFactor, 0xFFFFFFFF);
	m_DeviceContext->OMSetRenderTargets(1, &RTV, nullptr);
	m_DeviceContext->VSSetShader(m_VertexShader, nullptr, 0);
	m_DeviceContext->PSSetShader(m_PixelShader, nullptr, 0);
	m_DeviceContext->PSSetShaderResources(0, 1, &ShaderRes);
	m_DeviceContext->PSSetSamplers(0, 1, &m_SamplerLinear.p);
	m_DeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	// Draw
	m_DeviceContext->Draw(NUMVERTICES, 0);
	// Clean
	if (RTV) {
		RTV->Release();
		RTV = nullptr;
	}
	// Clear shader resource
	ID3D11ShaderResourceView *null[] = { nullptr, nullptr };
	m_DeviceContext->PSSetShaderResources(0, 1, null);

	if (VertexBufferMouse)
	{
		VertexBufferMouse->Release();
		VertexBufferMouse = nullptr;
	}
	if (ShaderRes)
	{
		ShaderRes->Release();
		ShaderRes = nullptr;
	}
	if (MouseTex)
	{
		MouseTex->Release();
		MouseTex = nullptr;
	}

	return hr;
}

//
// Process both masked and monochrome pointers
//
HRESULT MouseManager::ProcessMonoMask(
	_In_ ID3D11Texture2D *pBgTexture,
	_In_ DXGI_MODE_ROTATION rotation,
	_In_ bool IsMono,
	_Inout_ PTR_INFO *pPtrInfo,
	_Out_ INT *ptrWidth,
	_Out_ INT *ptrHeight,
	_Out_ INT *ptrLeft,
	_Out_ INT *ptrTop,
	_Outptr_result_bytebuffer_(*ptrHeight **ptrWidth *BPP) BYTE **pInitBuffer)
{
	D3D11_TEXTURE2D_DESC desc;
	pBgTexture->GetDesc(&desc);
	//// Desktop dimensions
	INT DesktopWidth = desc.Width;
	INT DesktopHeight = desc.Height;
	// Pointer position
	INT GivenLeft = pPtrInfo->Position.x;
	INT GivenTop = pPtrInfo->Position.y;
	GetPointerPosition(pPtrInfo, rotation, DesktopWidth, DesktopHeight, &GivenLeft, &GivenTop);

	// Figure out if any adjustment is needed for out of bound positions
	if (GivenLeft < 0)
	{
		*ptrWidth = GivenLeft + static_cast<INT>(pPtrInfo->ShapeInfo.Width);
	}
	else if ((GivenLeft + static_cast<INT>(pPtrInfo->ShapeInfo.Width)) > DesktopWidth)
	{
		*ptrWidth = DesktopWidth - GivenLeft;
	}
	else
	{
		*ptrWidth = static_cast<INT>(pPtrInfo->ShapeInfo.Width);
	}

	if (IsMono)
	{
		pPtrInfo->ShapeInfo.Height = pPtrInfo->ShapeInfo.Height / 2;
	}

	if (GivenTop < 0)
	{
		*ptrHeight = GivenTop + static_cast<INT>(pPtrInfo->ShapeInfo.Height);
	}
	else if ((GivenTop + static_cast<INT>(pPtrInfo->ShapeInfo.Height)) > DesktopHeight)
	{
		*ptrHeight = DesktopHeight - GivenTop;
	}
	else
	{
		*ptrHeight = static_cast<INT>(pPtrInfo->ShapeInfo.Height);
	}

	if (IsMono)
	{
		pPtrInfo->ShapeInfo.Height = pPtrInfo->ShapeInfo.Height * 2;
	}

	*ptrLeft = (GivenLeft < 0) ? 0 : GivenLeft;
	*ptrTop = (GivenTop < 0) ? 0 : GivenTop;

	// Staging buffer/texture
	D3D11_TEXTURE2D_DESC CopyBufferDesc;
	CopyBufferDesc.Width = *ptrWidth;
	CopyBufferDesc.Height = *ptrHeight;
	CopyBufferDesc.MipLevels = 1;
	CopyBufferDesc.ArraySize = 1;
	CopyBufferDesc.Format = desc.Format;
	CopyBufferDesc.SampleDesc.Count = 1;
	CopyBufferDesc.SampleDesc.Quality = 0;
	CopyBufferDesc.Usage = D3D11_USAGE_STAGING;
	CopyBufferDesc.BindFlags = 0;
	CopyBufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	CopyBufferDesc.MiscFlags = 0;

	ID3D11Texture2D *CopyBuffer = nullptr;
	HRESULT hr = m_Device->CreateTexture2D(&CopyBufferDesc, nullptr, &CopyBuffer);
	if (FAILED(hr))
	{
		_com_error err(hr);
		LOG_ERROR(L"Failed creating staging texture for pointer: %ls", err.ErrorMessage());
		return hr;
	}
	D3D11_BOX Box{};
	// Copy needed part of desktop image
	Box.left = *ptrLeft;
	Box.top = *ptrTop;
	Box.right = *ptrLeft + *ptrWidth;
	Box.bottom = *ptrTop + *ptrHeight;
	Box.back = 1;
	m_DeviceContext->CopySubresourceRegion(CopyBuffer, 0, 0, 0, 0, pBgTexture, 0, &Box);

	// QI for IDXGISurface
	IDXGISurface *CopySurface = nullptr;
	hr = CopyBuffer->QueryInterface(__uuidof(IDXGISurface), (void **)&CopySurface);
	CopyBuffer->Release();
	CopyBuffer = nullptr;
	if (FAILED(hr))
	{
		_com_error err(hr);
		LOG_ERROR(L"Failed to QI staging texture into IDXGISurface for pointer: %lls", err.ErrorMessage());
		return hr;
	}

	// Map pixels
	DXGI_MAPPED_RECT MappedSurface;
	hr = CopySurface->Map(&MappedSurface, DXGI_MAP_READ);
	if (FAILED(hr))
	{
		CopySurface->Release();
		CopySurface = nullptr;
		_com_error err(hr);
		LOG_ERROR(L"Failed to map surface for pointer: %lls", err.ErrorMessage());
		return hr;
	}
	auto bufSize = *ptrWidth * *ptrHeight * BPP;
	if ((int)_InitBuffer.size() < bufSize)
	{
		_InitBuffer.resize(bufSize);
		_DesktopBuffer.resize(bufSize);
	}

	// New mouseshape buffer
	*pInitBuffer = &(_InitBuffer[0]);

	// New temp mouseshape buffer for rotation
	BYTE *DesktopBuffer = &(_DesktopBuffer[0]);

	UINT *InitBuffer32 = reinterpret_cast<UINT *>(*pInitBuffer);
	UINT *DesktopBuffer32 = reinterpret_cast<UINT *>(DesktopBuffer);
	UINT  DesktopPitchInPixels = MappedSurface.Pitch / sizeof(UINT);

	// What to skip (pixel offset)
	UINT SkipX = (GivenLeft < 0) ? (-1 * GivenLeft) : (0);
	UINT SkipY = (GivenTop < 0) ? (-1 * GivenTop) : (0);

	//Rotate background if needed
	if (rotation == DXGI_MODE_ROTATION_ROTATE90
		|| rotation == DXGI_MODE_ROTATION_ROTATE180
		|| rotation == DXGI_MODE_ROTATION_ROTATE270) {
		UINT *Desktop32 = reinterpret_cast<UINT *>(MappedSurface.pBits);
		for (INT Row = 0; Row < *ptrHeight; ++Row)
		{
			for (INT Col = 0; Col < *ptrWidth; ++Col)
			{
				int	rotatedRow = Row;
				int rotatedCol = Col;

				if (rotation == DXGI_MODE_ROTATION_ROTATE90) {
					rotatedRow = Col;
					rotatedCol = *ptrHeight - 1 - Row;
				}
				else if (rotation == DXGI_MODE_ROTATION_ROTATE180) {
					rotatedRow = *ptrHeight - 1 - Row;
					rotatedCol = *ptrWidth - 1 - Col;
				}
				else if (rotation == DXGI_MODE_ROTATION_ROTATE270) {
					rotatedRow = *ptrWidth - 1 - Col;
					rotatedCol = Row;
				}
				// Set new pixel
				DesktopBuffer32[(rotatedRow * *ptrWidth) + rotatedCol] = Desktop32[(Row * DesktopPitchInPixels) + Col];
			}
		}
	}
	else {
		DesktopBuffer32 = reinterpret_cast<UINT *>(MappedSurface.pBits);
	}

	if (IsMono)
	{
		for (INT Row = 0; Row < *ptrHeight; ++Row)
		{
			// Set mask
			BYTE Mask = 0x80;
			Mask = Mask >> (SkipX % 8);
			for (INT Col = 0; Col < *ptrWidth; ++Col)
			{
				// Get masks using appropriate offsets
				BYTE AndMask = pPtrInfo->PtrShapeBuffer[((Col + SkipX) / 8) + ((Row + SkipY) * (pPtrInfo->ShapeInfo.Pitch))] & Mask;
				BYTE XorMask = pPtrInfo->PtrShapeBuffer[((Col + SkipX) / 8) + ((Row + SkipY + (pPtrInfo->ShapeInfo.Height / 2)) * (pPtrInfo->ShapeInfo.Pitch))] & Mask;
				UINT AndMask32 = (AndMask) ? 0xFFFFFFFF : 0xFF000000;
				UINT XorMask32 = (XorMask) ? 0x00FFFFFF : 0x00000000;

				// Set new pixel
				InitBuffer32[(Row * *ptrWidth) + Col] = (DesktopBuffer32[(Row * DesktopPitchInPixels) + Col] & AndMask32) ^ XorMask32;

				// Adjust mask
				if (Mask == 0x01)
				{
					Mask = 0x80;
				}
				else
				{
					Mask = Mask >> 1;
				}
			}
		}
	}
	else
	{
		UINT *Buffer32 = reinterpret_cast<UINT *>(pPtrInfo->PtrShapeBuffer);

		// Iterate through pixels
		for (INT Row = 0; Row < *ptrHeight; ++Row)
		{
			for (INT Col = 0; Col < *ptrWidth; ++Col)
			{
				// Set up mask
				UINT MaskVal = 0xFF000000 & Buffer32[(Col + SkipX) + ((Row + SkipY) * (pPtrInfo->ShapeInfo.Pitch / sizeof(UINT)))];
				if (MaskVal)
				{
					// Mask was 0xFF
					InitBuffer32[(Row * *ptrWidth) + Col] = (DesktopBuffer32[(Row * DesktopPitchInPixels) + Col] ^ Buffer32[(Col + SkipX) + ((Row + SkipY) * (pPtrInfo->ShapeInfo.Pitch / sizeof(UINT)))]) | 0xFF000000;
				}
				else
				{
					// Mask was 0x00
					InitBuffer32[(Row * *ptrWidth) + Col] = Buffer32[(Col + SkipX) + ((Row + SkipY) * (pPtrInfo->ShapeInfo.Pitch / sizeof(UINT)))] | 0xFF000000;
				}
			}
		}
	}

	// Done with resource
	hr = CopySurface->Unmap();
	CopySurface->Release();
	CopySurface = nullptr;
	if (FAILED(hr))
	{
		_com_error err(hr);
		LOG_ERROR(L"Failed to allocate memory for new mouse shape buffer: %lls", err.ErrorMessage());
		return hr;
	}
	return hr;
}





HRESULT MouseManager::ResizeShapeBuffer(_Inout_ PTR_INFO *pPtrInfo, _In_ int bufferSize) {
	// Old buffer too small
	if (bufferSize > (int)pPtrInfo->BufferSize)
	{
		if (pPtrInfo->PtrShapeBuffer)
		{
			delete[] pPtrInfo->PtrShapeBuffer;
			pPtrInfo->PtrShapeBuffer = nullptr;
		}
		pPtrInfo->PtrShapeBuffer = new (std::nothrow) BYTE[bufferSize];
		if (!pPtrInfo->PtrShapeBuffer)
		{
			pPtrInfo->BufferSize = 0;
			LOG_ERROR(L"Failed to allocate memory for pointer shape in DUPLICATIONMANAGER");
			return E_OUTOFMEMORY;
		}

		// Update buffer size
		pPtrInfo->BufferSize = bufferSize;
	}
	return S_OK;
}

//
// Retrieves mouse info and write it into pPtrInfo
//
HRESULT MouseManager::GetMouse(_Inout_ PTR_INFO *pPtrInfo, _In_ bool getShapeBuffer, _In_ DXGI_OUTDUPL_FRAME_INFO *pFrameInfo, _In_ RECT screenRect, _In_ IDXGIOutputDuplication *pDeskDupl, _In_ int offsetX, _In_ int offsetY)
{
	pPtrInfo->IsPointerShapeUpdated = false;
	// A non-zero mouse update timestamp indicates that there is a mouse position update and optionally a shape change
	if (pFrameInfo->LastMouseUpdateTime.QuadPart == 0)
	{
		return S_FALSE;
	}

	bool UpdatePosition = true;

	// Make sure we don't update pointer position wrongly
	// If pointer is invisible, make sure we did not get an update from another output that the last time that said pointer
	// was visible, if so, don't set it to invisible or update.
	if (!pFrameInfo->PointerPosition.Visible && (pPtrInfo->WhoUpdatedPositionLast != m_OutputNumber))
	{
		UpdatePosition = false;
	}

	// If two outputs both say they have a visible, only update if new update has newer timestamp
	if (pFrameInfo->PointerPosition.Visible && pPtrInfo->Visible && (pPtrInfo->WhoUpdatedPositionLast != m_OutputNumber) && (pPtrInfo->LastTimeStamp.QuadPart > pFrameInfo->LastMouseUpdateTime.QuadPart))
	{
		UpdatePosition = false;
	}

	// Update position
	if (UpdatePosition)
	{
		pPtrInfo->Position.x = pFrameInfo->PointerPosition.Position.x + screenRect.left + offsetX;
		pPtrInfo->Position.y = pFrameInfo->PointerPosition.Position.y + screenRect.top + offsetY;
		pPtrInfo->WhoUpdatedPositionLast = m_OutputNumber;
		pPtrInfo->LastTimeStamp = pFrameInfo->LastMouseUpdateTime;
		pPtrInfo->Visible = pFrameInfo->PointerPosition.Visible != 0;
	}

	// No new shape
	if (pFrameInfo->PointerShapeBufferSize == 0 || !getShapeBuffer)
	{
		return S_FALSE;
	}

	RETURN_ON_BAD_HR(ResizeShapeBuffer(pPtrInfo, pFrameInfo->PointerShapeBufferSize));

	// Get shape
	UINT BufferSizeRequired;
	HRESULT hr = pDeskDupl->GetFramePointerShape(pFrameInfo->PointerShapeBufferSize, reinterpret_cast<VOID *>(pPtrInfo->PtrShapeBuffer), &BufferSizeRequired, &(pPtrInfo->ShapeInfo));
	if (FAILED(hr))
	{
		delete[] pPtrInfo->PtrShapeBuffer;
		pPtrInfo->PtrShapeBuffer = nullptr;
		pPtrInfo->BufferSize = 0;
		_com_error err(hr);
		LOG_ERROR(L"Failed to get pFrame pointer shape in DUPLICATIONMANAGER: %lls", err.ErrorMessage());
		return hr;
	}
	pPtrInfo->IsPointerShapeUpdated = true;
	return S_OK;
}

HRESULT MouseManager::GetMouse(_Inout_ PTR_INFO *pPtrInfo, _In_ bool getShapeBuffer, _In_ int offsetX, _In_ int offsetY)
{
	pPtrInfo->IsPointerShapeUpdated = false;
	CURSORINFO cursorInfo = { 0 };
	cursorInfo.cbSize = sizeof(CURSORINFO);
	if (!GetCursorInfo(&cursorInfo)) {
		return E_FAIL;
	}

	ICONINFO iconInfo = { 0 };
	if (!cursorInfo.hCursor || !GetIconInfo(cursorInfo.hCursor, &iconInfo)) {
		return E_FAIL;
	}
	bool isVisible = cursorInfo.flags == CURSOR_SHOWING;
	LONG width = 0;
	LONG height = 0;
	LONG widthBytes = 0;
	LONG cursorType = 0;
	DeleteGdiObjectOnExit deleteColor(iconInfo.hbmColor);
	DeleteGdiObjectOnExit deleteMask(iconInfo.hbmMask);

	if (iconInfo.hbmColor) {
		BITMAP cursorBitmap;
		GetObject(iconInfo.hbmColor, sizeof(cursorBitmap), &cursorBitmap);
		cursorType = DXGI_OUTDUPL_POINTER_SHAPE_TYPE_COLOR;
		width = cursorBitmap.bmWidth;
		height = cursorBitmap.bmHeight;
		widthBytes = cursorBitmap.bmWidthBytes;
		int colorBits = cursorBitmap.bmBitsPixel;
		if (getShapeBuffer) {
			HDC dc = GetDC(NULL);
			ReleaseDCOnExit ReleaseDC(dc);

			BITMAPINFO bmInfo = { 0 };
			bmInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
			bmInfo.bmiHeader.biBitCount = 0;    // don't get the color table  
			if (!GetDIBits(dc, iconInfo.hbmColor, 0, 0, NULL, &bmInfo, DIB_RGB_COLORS))
			{
				return E_FAIL;
			}

			// Allocate size of bitmap info header plus space for color table:
			int nBmInfoSize = sizeof(BITMAPINFOHEADER);
			if (colorBits < 24)
			{
				nBmInfoSize += sizeof(RGBQUAD) * (int)(1 << colorBits);
			}

			CAutoVectorPtr<UCHAR> bitmapInfo;
			bitmapInfo.Allocate(nBmInfoSize);
			BITMAPINFO *pBmInfo = (BITMAPINFO *)(UCHAR *)bitmapInfo;
			memcpy(pBmInfo, &bmInfo, sizeof(BITMAPINFOHEADER));

			// Get bitmap data:
			RETURN_ON_BAD_HR(ResizeShapeBuffer(pPtrInfo, bmInfo.bmiHeader.biSizeImage));
			pBmInfo->bmiHeader.biBitCount = colorBits;
			pBmInfo->bmiHeader.biCompression = BI_RGB;
			pBmInfo->bmiHeader.biHeight = -bmInfo.bmiHeader.biHeight;
			if (!GetDIBits(dc, iconInfo.hbmColor, 0, bmInfo.bmiHeader.biHeight, pPtrInfo->PtrShapeBuffer, pBmInfo, DIB_RGB_COLORS))
			{
				return E_FAIL;
			}
			pPtrInfo->IsPointerShapeUpdated = true;
		}
	}
	else {
		BITMAP cursorBitmap;
		GetObject(iconInfo.hbmMask, sizeof(cursorBitmap), &cursorBitmap);
		cursorType = DXGI_OUTDUPL_POINTER_SHAPE_TYPE_MONOCHROME;
		width = cursorBitmap.bmWidth;
		height = cursorBitmap.bmHeight;
		widthBytes = cursorBitmap.bmWidthBytes;
		int colorBits = cursorBitmap.bmBitsPixel;
		if (getShapeBuffer) {
			HDC dc = GetDC(NULL);
			ReleaseDCOnExit ReleaseDC(dc);

			BITMAPINFO maskInfo = { 0 };
			maskInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
			maskInfo.bmiHeader.biBitCount = 0;  // don't get the color table     
			if (!GetDIBits(dc, iconInfo.hbmMask, 0, 0, NULL, &maskInfo, DIB_RGB_COLORS))
			{
				return E_FAIL;
			}

			RETURN_ON_BAD_HR(ResizeShapeBuffer(pPtrInfo, maskInfo.bmiHeader.biSizeImage));
			CAutoVectorPtr<UCHAR> maskInfoBytes;
			maskInfoBytes.Allocate(sizeof(BITMAPINFO) + 2 * sizeof(RGBQUAD));
			BITMAPINFO *pMaskInfo = (BITMAPINFO *)(UCHAR *)maskInfoBytes;
			memcpy(pMaskInfo, &maskInfo, sizeof(maskInfo));
			pMaskInfo->bmiHeader.biBitCount = colorBits;
			pMaskInfo->bmiHeader.biCompression = BI_RGB;
			pMaskInfo->bmiHeader.biHeight = -maskInfo.bmiHeader.biHeight;
			if (!GetDIBits(dc, iconInfo.hbmMask, 0, maskInfo.bmiHeader.biHeight, pPtrInfo->PtrShapeBuffer, pMaskInfo, DIB_RGB_COLORS))
			{
				return E_FAIL;
			}
			pPtrInfo->IsPointerShapeUpdated = true;
		}
	}

	DXGI_OUTDUPL_POINTER_SHAPE_INFO shapeInfo = { 0 };
	POINT hotSpot = { 0 };
	hotSpot.x = iconInfo.xHotspot;
	hotSpot.y = iconInfo.yHotspot;

	shapeInfo.HotSpot = hotSpot;
	shapeInfo.Width = width;
	shapeInfo.Height = height;
	shapeInfo.Type = cursorType;
	shapeInfo.Pitch = widthBytes;

	cursorInfo.ptScreenPos.x = cursorInfo.ptScreenPos.x + offsetX - hotSpot.x;
	cursorInfo.ptScreenPos.y = cursorInfo.ptScreenPos.y + offsetY - hotSpot.y;
	pPtrInfo->Position = cursorInfo.ptScreenPos;
	pPtrInfo->ShapeInfo = shapeInfo;
	pPtrInfo->Visible = isVisible;
	QueryPerformanceCounter(&pPtrInfo->LastTimeStamp);
	return S_OK;
}

void MouseManager::CleanDX()
{
	if (m_SamplerLinear)
		m_SamplerLinear.Release();
	if (m_BlendState)
		m_BlendState.Release();
	if (m_InputLayout)
		m_InputLayout.Release();
	if (m_VertexShader)
		m_VertexShader.Release();
	if (m_PixelShader)
		m_PixelShader.Release();
	if (m_D2DFactory)
		m_D2DFactory.Release();
}
