#include "mouse_pointer.h"
#include "log.h"
#include <comdef.h>
#include <Dxgiformat.h>
using namespace DirectX;

HRESULT mouse_pointer::Initialize(ID3D11DeviceContext *ImmediateContext, ID3D11Device *Device)
{
	// Create the sample state
	D3D11_SAMPLER_DESC SampDesc;
	RtlZeroMemory(&SampDesc, sizeof(SampDesc));
	SampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
	SampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
	SampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
	SampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
	SampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
	SampDesc.MinLOD = 0;
	SampDesc.MaxLOD = D3D11_FLOAT32_MAX;
	HRESULT hr = Device->CreateSamplerState(&SampDesc, &m_SamplerLinear);
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
	hr = Device->CreateBlendState(&BlendStateDesc, &m_BlendState);
	RETURN_ON_BAD_HR(hr);

	// Initialize shaders
	hr = InitShaders(ImmediateContext, Device);
	hr = InitMouseClickTexture(ImmediateContext, Device);

	return hr;
}

HRESULT mouse_pointer::InitMouseClickTexture(ID3D11DeviceContext *ImmediateContext, ID3D11Device *Device) {
	HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory), (void**)&m_D2DFactory);
	return hr;
}


long mouse_pointer::ParseColorString(std::string color)
{
	if (color.length() == 0)
		return D2D1::ColorF::Yellow;
	if (color.front() == '#') {
		color.replace(0, 1, "0x");
	}
	return std::strtoul(color.data(), 0, 16);
}
float mouse_pointer::GetCurrentDpi()
{
	int newDpiX(0);
	auto hDC = GetDC(NULL);
	newDpiX = GetDeviceCaps(hDC, LOGPIXELSX);
	ReleaseDC(NULL, hDC);
	return (float)newDpiX / 96.0f;
}

void mouse_pointer::GetPointerPosition(_In_ PTR_INFO *PtrInfo, DXGI_MODE_ROTATION rotation, int desktopWidth, int desktopHeight, _Out_ INT *PtrLeft, _Out_ INT *PtrTop)
{
	switch (rotation)
	{
	default:
	case DXGI_MODE_ROTATION_UNSPECIFIED:
	case DXGI_MODE_ROTATION_IDENTITY:
		*PtrLeft = PtrInfo->Position.x;
		*PtrTop = PtrInfo->Position.y;
		break;
	case DXGI_MODE_ROTATION_ROTATE90:
		*PtrLeft = PtrInfo->Position.y;
		*PtrTop = desktopHeight - PtrInfo->Position.x - (PtrInfo->ShapeInfo.Height);
		break;
	case DXGI_MODE_ROTATION_ROTATE180:
		*PtrLeft = desktopWidth - PtrInfo->Position.x - (PtrInfo->ShapeInfo.Width);
		*PtrTop = desktopHeight - PtrInfo->Position.y - (PtrInfo->ShapeInfo.Height);
		break;
	case DXGI_MODE_ROTATION_ROTATE270:
		*PtrLeft = desktopWidth - PtrInfo->Position.y - PtrInfo->ShapeInfo.Height;
		*PtrTop = PtrInfo->Position.x;
		break;
	}
}


HRESULT mouse_pointer::DrawMouseClick(_In_ PTR_INFO* PtrInfo, _In_ ID3D11Texture2D* bgTexture, std::string colorStr, float radius, DXGI_MODE_ROTATION rotation)
{
	ATL::CComPtr<IDXGISurface> pSharedSurface;
	HRESULT hr = bgTexture->QueryInterface(__uuidof(IDXGISurface), (void**)&pSharedSurface);

	// Create the DXGI Surface Render Target.
	FLOAT dpiX;
	FLOAT dpiY;
	m_D2DFactory->GetDesktopDpi(&dpiX, &dpiY);
	/* RenderTargetProperties contains the description for render target */
	D2D1_RENDER_TARGET_PROPERTIES RenderTargetProperties =
		D2D1::RenderTargetProperties(
			D2D1_RENDER_TARGET_TYPE_DEFAULT,
			D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_PREMULTIPLIED),
			dpiX,
			dpiY
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
	float dpi = GetCurrentDpi();
	INT ptrLeft, ptrTop;
	GetPointerPosition(PtrInfo, rotation, desc.Width, desc.Height, &ptrLeft, &ptrTop);

	if (rotation == DXGI_MODE_ROTATION_ROTATE90) {
		ptrTop += PtrInfo->ShapeInfo.Height;
	}
	else if (rotation == DXGI_MODE_ROTATION_ROTATE180) {
		ptrLeft += PtrInfo->ShapeInfo.Width;
		ptrTop += PtrInfo->ShapeInfo.Height;
	}
	else if (rotation == DXGI_MODE_ROTATION_ROTATE270) {
		ptrLeft += PtrInfo->ShapeInfo.Height;
	}
	mousePoint.x = ptrLeft / dpi;
	mousePoint.y = ptrTop / dpi;

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
HRESULT mouse_pointer::DrawMousePointer(_In_ PTR_INFO* PtrInfo, _In_ ID3D11DeviceContext* DeviceContext, _In_ ID3D11Device* Device, _In_ ID3D11Texture2D* bgTexture, DXGI_MODE_ROTATION rotation)
{
	if (!PtrInfo || !PtrInfo->Visible || PtrInfo->PtrShapeBuffer == nullptr)
		return S_FALSE;
	// Vars to be used
	ID3D11Texture2D* MouseTex = nullptr;
	ID3D11ShaderResourceView* ShaderRes = nullptr;
	ID3D11Buffer* VertexBufferMouse = nullptr;
	D3D11_SUBRESOURCE_DATA InitData;
	D3D11_TEXTURE2D_DESC Desc;
	D3D11_SHADER_RESOURCE_VIEW_DESC SDesc;

	D3D11_TEXTURE2D_DESC DesktopDesc;
	bgTexture->GetDesc(&DesktopDesc);
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
	BYTE* InitBuffer = nullptr;

	// Used for copying pixels
	D3D11_BOX Box;
	Box.front = 0;
	Box.back = 1;

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

	switch (PtrInfo->ShapeInfo.Type)
	{
	case DXGI_OUTDUPL_POINTER_SHAPE_TYPE_COLOR:
	{
		PtrWidth = static_cast<INT>(PtrInfo->ShapeInfo.Width);
		PtrHeight = static_cast<INT>(PtrInfo->ShapeInfo.Height);

		GetPointerPosition(PtrInfo, rotation, DesktopWidth, DesktopHeight, &PtrLeft, &PtrTop);

		break;
	}
	case DXGI_OUTDUPL_POINTER_SHAPE_TYPE_MONOCHROME:
	{
		ProcessMonoMask(bgTexture, DeviceContext, Device, rotation, true, PtrInfo, &PtrWidth, &PtrHeight, &PtrLeft, &PtrTop, &InitBuffer, &Box);
		break;
	}
	case DXGI_OUTDUPL_POINTER_SHAPE_TYPE_MASKED_COLOR:
	{
		ProcessMonoMask(bgTexture, DeviceContext, Device, rotation, false, PtrInfo, &PtrWidth, &PtrHeight, &PtrLeft, &PtrTop, &InitBuffer, &Box);
		break;
	}
	default:
		ERROR("Unrecognized mouse pointer type");
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
		Vertices[3].Pos.x = Vertices[2].Pos.x;
		Vertices[3].Pos.y = Vertices[2].Pos.y;
		Vertices[4].Pos.x = Vertices[1].Pos.x;
		Vertices[4].Pos.y = Vertices[1].Pos.y;
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
		Vertices[3].Pos.x = Vertices[2].Pos.x;
		Vertices[3].Pos.y = Vertices[2].Pos.y;
		Vertices[4].Pos.x = Vertices[1].Pos.x;
		Vertices[4].Pos.y = Vertices[1].Pos.y;
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
		Vertices[3].Pos.x = Vertices[2].Pos.x;
		Vertices[3].Pos.y = Vertices[2].Pos.y;
		Vertices[4].Pos.x = Vertices[1].Pos.x;
		Vertices[4].Pos.y = Vertices[1].Pos.y;
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
		Vertices[3].Pos.x = Vertices[2].Pos.x;
		Vertices[3].Pos.y = Vertices[2].Pos.y;
		Vertices[4].Pos.x = Vertices[1].Pos.x;
		Vertices[4].Pos.y = Vertices[1].Pos.y;
		Vertices[5].Pos.x = ((PtrLeft + PtrWidth) - CenterX) / (FLOAT)CenterX;
		Vertices[5].Pos.y = -1 * ((PtrTop + PtrHeight) - CenterY) / (FLOAT)CenterY;
	}

	// Set texture properties
	Desc.Width = PtrWidth;
	Desc.Height = PtrHeight;

	// Set up init data
	InitData.pSysMem = (PtrInfo->ShapeInfo.Type == DXGI_OUTDUPL_POINTER_SHAPE_TYPE_COLOR) ? PtrInfo->PtrShapeBuffer : InitBuffer;
	InitData.SysMemPitch = (PtrInfo->ShapeInfo.Type == DXGI_OUTDUPL_POINTER_SHAPE_TYPE_COLOR) ? PtrInfo->ShapeInfo.Pitch : PtrWidth * BPP;
	InitData.SysMemSlicePitch = 0;

	// Create mouseshape as texture
	HRESULT hr = Device->CreateTexture2D(&Desc, &InitData, &MouseTex);
	if (FAILED(hr))
	{
		_com_error err(hr);
		ERROR(L"Failed to create mouse pointer texture: %ls", err.ErrorMessage());
		return hr;
	}

	// Create shader resource from texture
	hr = Device->CreateShaderResourceView(MouseTex, &SDesc, &ShaderRes);
	if (FAILED(hr))
	{
		MouseTex->Release();
		MouseTex = nullptr;
		_com_error err(hr);
		ERROR(L"Failed to create shader resource from mouse pointer texture: %ls", err.ErrorMessage());
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
	hr = Device->CreateBuffer(&BDesc, &InitData, &VertexBufferMouse);
	if (FAILED(hr))
	{
		ShaderRes->Release();
		ShaderRes = nullptr;
		MouseTex->Release();
		MouseTex = nullptr;

		_com_error err(hr);
		ERROR(L"Failed to create mouse pointer vertex buffer: %ls", err.ErrorMessage());
		return hr;
	}
	ID3D11RenderTargetView* RTV;
	// Create a render target view
	hr = Device->CreateRenderTargetView(bgTexture, nullptr, &RTV);
	// Set resources
	FLOAT BlendFactor[4] = { 0.f, 0.f, 0.f, 0.f };
	UINT Stride = sizeof(VERTEX);
	UINT Offset = 0;
	DeviceContext->IASetVertexBuffers(0, 1, &VertexBufferMouse, &Stride, &Offset);
	DeviceContext->OMSetBlendState(m_BlendState.p, BlendFactor, 0xFFFFFFFF);
	DeviceContext->OMSetRenderTargets(1, &RTV, nullptr);
	DeviceContext->VSSetShader(m_VertexShader, nullptr, 0);
	DeviceContext->PSSetShader(m_PixelShader, nullptr, 0);
	DeviceContext->PSSetShaderResources(0, 1, &ShaderRes);
	DeviceContext->PSSetSamplers(0, 1, &m_SamplerLinear.p);
	DeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	// Draw
	DeviceContext->Draw(NUMVERTICES, 0);
	// Clean
	if (RTV) {
		RTV->Release();
		RTV = nullptr;
	}

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
HRESULT mouse_pointer::ProcessMonoMask(_In_ ID3D11Texture2D* bgTexture, _In_ ID3D11DeviceContext* DeviceContext, _In_ ID3D11Device* Device, DXGI_MODE_ROTATION rotation, bool IsMono, _Inout_ PTR_INFO* PtrInfo, _Out_ INT* PtrWidth, _Out_ INT* PtrHeight, _Out_ INT* PtrLeft, _Out_ INT* PtrTop, _Outptr_result_bytebuffer_(*PtrHeight * *PtrWidth * BPP) BYTE** InitBuffer, _Out_ D3D11_BOX* Box)
{
	D3D11_TEXTURE2D_DESC desc;
	bgTexture->GetDesc(&desc);
	//// Desktop dimensions
	INT DesktopWidth = desc.Width;
	INT DesktopHeight = desc.Height;
	// Pointer position
	INT GivenLeft = PtrInfo->Position.x;
	INT GivenTop = PtrInfo->Position.y;
	GetPointerPosition(PtrInfo, rotation, DesktopWidth, DesktopHeight, &GivenLeft, &GivenTop);

	// Figure out if any adjustment is needed for out of bound positions
	if (GivenLeft < 0)
	{
		*PtrWidth = GivenLeft + static_cast<INT>(PtrInfo->ShapeInfo.Width);
	}
	else if ((GivenLeft + static_cast<INT>(PtrInfo->ShapeInfo.Width)) > DesktopWidth)
	{
		*PtrWidth = DesktopWidth - GivenLeft;
	}
	else
	{
		*PtrWidth = static_cast<INT>(PtrInfo->ShapeInfo.Width);
	}

	if (IsMono)
	{
		PtrInfo->ShapeInfo.Height = PtrInfo->ShapeInfo.Height / 2;
	}

	if (GivenTop < 0)
	{
		*PtrHeight = GivenTop + static_cast<INT>(PtrInfo->ShapeInfo.Height);
	}
	else if ((GivenTop + static_cast<INT>(PtrInfo->ShapeInfo.Height)) > DesktopHeight)
	{
		*PtrHeight = DesktopHeight - GivenTop;
	}
	else
	{
		*PtrHeight = static_cast<INT>(PtrInfo->ShapeInfo.Height);
	}

	if (IsMono)
	{
		PtrInfo->ShapeInfo.Height = PtrInfo->ShapeInfo.Height * 2;
	}

	*PtrLeft = (GivenLeft < 0) ? 0 : GivenLeft;
	*PtrTop = (GivenTop < 0) ? 0 : GivenTop;

	// Staging buffer/texture
	D3D11_TEXTURE2D_DESC CopyBufferDesc;
	CopyBufferDesc.Width = *PtrWidth;
	CopyBufferDesc.Height = *PtrHeight;
	CopyBufferDesc.MipLevels = 1;
	CopyBufferDesc.ArraySize = 1;
	CopyBufferDesc.Format = desc.Format;
	CopyBufferDesc.SampleDesc.Count = 1;
	CopyBufferDesc.SampleDesc.Quality = 0;
	CopyBufferDesc.Usage = D3D11_USAGE_STAGING;
	CopyBufferDesc.BindFlags = 0;
	CopyBufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	CopyBufferDesc.MiscFlags = 0;

	ID3D11Texture2D* CopyBuffer = nullptr;
	HRESULT hr = Device->CreateTexture2D(&CopyBufferDesc, nullptr, &CopyBuffer);
	if (FAILED(hr))
	{
		_com_error err(hr);
		ERROR(L"Failed creating staging texture for pointer: %ls", err.ErrorMessage());
		return hr;
	}

	// Copy needed part of desktop image
	Box->left = *PtrLeft;
	Box->top = *PtrTop;
	Box->right = *PtrLeft + *PtrWidth;
	Box->bottom = *PtrTop + *PtrHeight;
	DeviceContext->CopySubresourceRegion(CopyBuffer, 0, 0, 0, 0, bgTexture, 0, Box);

	// QI for IDXGISurface
	IDXGISurface* CopySurface = nullptr;
	hr = CopyBuffer->QueryInterface(__uuidof(IDXGISurface), (void **)&CopySurface);
	CopyBuffer->Release();
	CopyBuffer = nullptr;
	if (FAILED(hr))
	{
		_com_error err(hr);
		ERROR(L"Failed to QI staging texture into IDXGISurface for pointer: %lls", err.ErrorMessage());
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
		ERROR(L"Failed to map surface for pointer: %lls", err.ErrorMessage());
		return hr;
	}
	auto bufSize = *PtrWidth * *PtrHeight * BPP;
	if (_InitBuffer.size() < bufSize)
	{
		_InitBuffer.resize(bufSize);
		_DesktopBuffer.resize(bufSize);
	}

	// New mouseshape buffer
	*InitBuffer = &(_InitBuffer[0]);

	// New temp mouseshape buffer for rotation
	BYTE* DesktopBuffer = &(_DesktopBuffer[0]);

	UINT* InitBuffer32 = reinterpret_cast<UINT*>(*InitBuffer);
	UINT* DesktopBuffer32 = reinterpret_cast<UINT*>(DesktopBuffer);
	UINT  DesktopPitchInPixels = MappedSurface.Pitch / sizeof(UINT);

	// What to skip (pixel offset)
	UINT SkipX = (GivenLeft < 0) ? (-1 * GivenLeft) : (0);
	UINT SkipY = (GivenTop < 0) ? (-1 * GivenTop) : (0);

	//Rotate background if needed
	if (rotation == DXGI_MODE_ROTATION_ROTATE90
		|| rotation == DXGI_MODE_ROTATION_ROTATE180
		|| rotation == DXGI_MODE_ROTATION_ROTATE270) {
		UINT* Desktop32 = reinterpret_cast<UINT*>(MappedSurface.pBits);
		for (INT Row = 0; Row < *PtrHeight; ++Row)
		{
			for (INT Col = 0; Col < *PtrWidth; ++Col)
			{
				int	rotatedRow = Row;
				int rotatedCol = Col;

				if (rotation == DXGI_MODE_ROTATION_ROTATE90) {
					rotatedRow = Col;
					rotatedCol = *PtrHeight - 1 - Row;
				}
				else if (rotation == DXGI_MODE_ROTATION_ROTATE180) {
					rotatedRow = *PtrHeight - 1 - Row;
					rotatedCol = *PtrWidth - 1 - Col;
				}
				else if (rotation == DXGI_MODE_ROTATION_ROTATE270) {
					rotatedRow = *PtrWidth - 1 - Col;
					rotatedCol = Row;
				}
				// Set new pixel
				DesktopBuffer32[(rotatedRow * *PtrWidth) + rotatedCol] = Desktop32[(Row * DesktopPitchInPixels) + Col];
			}
		}
	}
	else {
		DesktopBuffer32 = reinterpret_cast<UINT*>(MappedSurface.pBits);
	}

	if (IsMono)
	{
		for (INT Row = 0; Row < *PtrHeight; ++Row)
		{
			// Set mask
			BYTE Mask = 0x80;
			Mask = Mask >> (SkipX % 8);
			for (INT Col = 0; Col < *PtrWidth; ++Col)
			{
				// Get masks using appropriate offsets
				BYTE AndMask = PtrInfo->PtrShapeBuffer[((Col + SkipX) / 8) + ((Row + SkipY) * (PtrInfo->ShapeInfo.Pitch))] & Mask;
				BYTE XorMask = PtrInfo->PtrShapeBuffer[((Col + SkipX) / 8) + ((Row + SkipY + (PtrInfo->ShapeInfo.Height / 2)) * (PtrInfo->ShapeInfo.Pitch))] & Mask;
				UINT AndMask32 = (AndMask) ? 0xFFFFFFFF : 0xFF000000;
				UINT XorMask32 = (XorMask) ? 0x00FFFFFF : 0x00000000;

				// Set new pixel
				InitBuffer32[(Row * *PtrWidth) + Col] = (DesktopBuffer32[(Row * DesktopPitchInPixels) + Col] & AndMask32) ^ XorMask32;

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
		UINT* Buffer32 = reinterpret_cast<UINT*>(PtrInfo->PtrShapeBuffer);

		// Iterate through pixels
		for (INT Row = 0; Row < *PtrHeight; ++Row)
		{
			for (INT Col = 0; Col < *PtrWidth; ++Col)
			{
				// Set up mask
				UINT MaskVal = 0xFF000000 & Buffer32[(Col + SkipX) + ((Row + SkipY) * (PtrInfo->ShapeInfo.Pitch / sizeof(UINT)))];
				if (MaskVal)
				{
					// Mask was 0xFF
					InitBuffer32[(Row * *PtrWidth) + Col] = (DesktopBuffer32[(Row * DesktopPitchInPixels) + Col] ^ Buffer32[(Col + SkipX) + ((Row + SkipY) * (PtrInfo->ShapeInfo.Pitch / sizeof(UINT)))]) | 0xFF000000;
				}
				else
				{
					// Mask was 0x00
					InitBuffer32[(Row * *PtrWidth) + Col] = Buffer32[(Col + SkipX) + ((Row + SkipY) * (PtrInfo->ShapeInfo.Pitch / sizeof(UINT)))] | 0xFF000000;
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
		ERROR(L"Failed to allocate memory for new mouse shape buffer: %lls", err.ErrorMessage());
		return hr;
	}
	return hr;
}

//
// Initialize shaders for drawing to screen
//
HRESULT mouse_pointer::InitShaders(ID3D11DeviceContext* DeviceContext, ID3D11Device* Device)
{
	HRESULT hr;

	UINT Size = ARRAYSIZE(g_VS);
	hr = Device->CreateVertexShader(g_VS, Size, nullptr, &m_VertexShader);
	if (FAILED(hr))
	{
		_com_error err(hr);
		ERROR(L"Failed to create vertex shader: %lls", err.ErrorMessage());
		return hr;
	}

	D3D11_INPUT_ELEMENT_DESC Layout[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 }
	};
	UINT NumElements = ARRAYSIZE(Layout);
	hr = Device->CreateInputLayout(Layout, NumElements, g_VS, Size, &m_InputLayout);
	if (FAILED(hr))
	{
		_com_error err(hr);
		ERROR(L"Failed to create input layout: %lls", err.ErrorMessage());
		return hr;
	}
	DeviceContext->IASetInputLayout(m_InputLayout);

	Size = ARRAYSIZE(g_PS);
	hr = Device->CreatePixelShader(g_PS, Size, nullptr, &m_PixelShader);
	if (FAILED(hr))
	{
		_com_error err(hr);
		ERROR(L"Failed to create pixel shader: %lls", err.ErrorMessage());
	}
	return hr;
}

//
// Retrieves mouse info and write it into PtrInfo
//
HRESULT mouse_pointer::GetMouse(_Inout_ PTR_INFO* PtrInfo, _In_ DXGI_OUTDUPL_FRAME_INFO* FrameInfo, RECT screenRect, IDXGIOutputDuplication* DeskDupl)
{
	int offsetX = min(screenRect.left, INT_MAX);
	int offsetY = min(screenRect.top, INT_MAX);
	// A non-zero mouse update timestamp indicates that there is a mouse position update and optionally a shape change
	if (FrameInfo->LastMouseUpdateTime.QuadPart == 0)
	{
		return S_FALSE;
	}

	bool UpdatePosition = true;

	// Make sure we don't update pointer position wrongly
	// If pointer is invisible, make sure we did not get an update from another output that the last time that said pointer
	// was visible, if so, don't set it to invisible or update.
	if (!FrameInfo->PointerPosition.Visible && (PtrInfo->WhoUpdatedPositionLast != m_OutputNumber))
	{
		UpdatePosition = false;
	}

	// If two outputs both say they have a visible, only update if new update has newer timestamp
	if (FrameInfo->PointerPosition.Visible && PtrInfo->Visible && (PtrInfo->WhoUpdatedPositionLast != m_OutputNumber) && (PtrInfo->LastTimeStamp.QuadPart > FrameInfo->LastMouseUpdateTime.QuadPart))
	{
		UpdatePosition = false;
	}

	// Update position
	if (UpdatePosition)
	{
		PtrInfo->Position.x = FrameInfo->PointerPosition.Position.x + screenRect.left - offsetX;
		PtrInfo->Position.y = FrameInfo->PointerPosition.Position.y + screenRect.top - offsetY;
		PtrInfo->WhoUpdatedPositionLast = m_OutputNumber;
		PtrInfo->LastTimeStamp = FrameInfo->LastMouseUpdateTime;
		PtrInfo->Visible = FrameInfo->PointerPosition.Visible != 0;
	}

	// No new shape
	if (FrameInfo->PointerShapeBufferSize == 0)
	{
		return S_FALSE;
	}

	// Old buffer too small
	if (FrameInfo->PointerShapeBufferSize > PtrInfo->BufferSize)
	{
		if (PtrInfo->PtrShapeBuffer)
		{
			delete[] PtrInfo->PtrShapeBuffer;
			PtrInfo->PtrShapeBuffer = nullptr;
		}
		PtrInfo->PtrShapeBuffer = new (std::nothrow) BYTE[FrameInfo->PointerShapeBufferSize];
		if (!PtrInfo->PtrShapeBuffer)
		{
			PtrInfo->BufferSize = 0;
			ERROR(L"Failed to allocate memory for pointer shape in DUPLICATIONMANAGER");
			return E_OUTOFMEMORY;
		}

		// Update buffer size
		PtrInfo->BufferSize = FrameInfo->PointerShapeBufferSize;
	}

	// Get shape
	UINT BufferSizeRequired;
	HRESULT hr = DeskDupl->GetFramePointerShape(FrameInfo->PointerShapeBufferSize, reinterpret_cast<VOID*>(PtrInfo->PtrShapeBuffer), &BufferSizeRequired, &(PtrInfo->ShapeInfo));
	if (FAILED(hr))
	{
		delete[] PtrInfo->PtrShapeBuffer;
		PtrInfo->PtrShapeBuffer = nullptr;
		PtrInfo->BufferSize = 0;
		_com_error err(hr);
		ERROR(L"Failed to get frame pointer shape in DUPLICATIONMANAGER: %lls", err.ErrorMessage());
		return hr;
	}

	return S_OK;
}

void mouse_pointer::CleanupResources()
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
}
