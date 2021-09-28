#include "TextureManager.h"
#include "screengrab.h"
#include "util.h"
#include <atlbase.h>

using namespace DirectX;

TextureManager::TextureManager() :
	m_Device(nullptr),
	m_DeviceContext(nullptr),
	m_SamplerLinear(nullptr),
	m_BlendState(nullptr),
	m_VertexShader(nullptr),
	m_PixelShader(nullptr),
	m_InputLayout(nullptr)
{
}

TextureManager::~TextureManager()
{
	CleanRefs();
}

HRESULT TextureManager::Initialize(_In_ ID3D11DeviceContext *pDeviceContext, _In_ ID3D11Device *pDevice)
{
	m_Device = pDevice;
	m_DeviceContext = pDeviceContext;

	HRESULT hr = S_OK;

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
	hr = m_Device->CreateSamplerState(&SampDesc, &m_SamplerLinear);
	RETURN_ON_BAD_HR(hr);

	// Create the blend state
	D3D11_BLEND_DESC BlendStateDesc;
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
	hr = m_Device->CreateBlendState(&BlendStateDesc, &m_BlendState);
	RETURN_ON_BAD_HR(hr);

	// Initialize shaders
	hr = InitShaders(pDevice, &m_PixelShader, &m_VertexShader, &m_InputLayout);
	RETURN_ON_BAD_HR(hr);

	return hr;
}

HRESULT TextureManager::ResizeTexture(_In_ ID3D11Texture2D *pOrgTexture, _Outptr_ ID3D11Texture2D **ppResizedTexture, _In_opt_ std::optional<SIZE> targetSize, _In_opt_ std::optional<double> scale)
{
	HRESULT hr;


	// Create shader resource from texture of the original frame
	D3D11_TEXTURE2D_DESC desktopDesc = {};
	pOrgTexture->GetDesc(&desktopDesc);
	UINT targetWidth;
	UINT targetHeight;
	if (targetSize.has_value()) {
		targetWidth = targetSize.value().cx;
		targetHeight = targetSize.value().cy;
	}
	else {
		targetWidth = desktopDesc.Width;
		targetHeight = desktopDesc.Height;
	}
	D3D11_SHADER_RESOURCE_VIEW_DESC SDesc = {};
	SDesc.Format = desktopDesc.Format;
	SDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	SDesc.Texture2D.MostDetailedMip = desktopDesc.MipLevels - 1;
	SDesc.Texture2D.MipLevels = desktopDesc.MipLevels;
	ID3D11ShaderResourceView *srcSRV;
	hr = m_Device->CreateShaderResourceView(pOrgTexture, &SDesc, &srcSRV);
	if (FAILED(hr))
	{
		_com_error err(hr);
		LOG_ERROR(L"Failed to create shader resource from original frame texture: %ls", err.ErrorMessage());
		return hr;
	}

	// Create target texture
	CComPtr<ID3D11Texture2D> pResizedFrame = nullptr;
	D3D11_TEXTURE2D_DESC targetDesc;
	InitializeDesc(targetWidth, targetHeight, &targetDesc);
	hr = m_Device->CreateTexture2D(&targetDesc, nullptr, &pResizedFrame);
	RETURN_ON_BAD_HR(hr);
	*ppResizedTexture = pResizedFrame;
	(*ppResizedTexture)->AddRef();

	// Save current view port so we can restore later
	D3D11_VIEWPORT VP;
	UINT numViewports = 1;
	m_DeviceContext->RSGetViewports(&numViewports, &VP);

	// Set view port
	UINT viewportWidth = static_cast<UINT>(round(targetWidth * scale.value_or(1.0)));
	UINT viewportHeight = static_cast<UINT>(round(targetHeight * scale.value_or(1.0)));
	SetViewPort(m_DeviceContext, viewportWidth, viewportHeight);

	// Vertices for drawing whole texture
	VERTEX Vertices[] =
	{
		{XMFLOAT3(-1.0f, -1.0f, 0), XMFLOAT2(0.0f, 1.0f)},
		{XMFLOAT3(-1.0f, 1.0f, 0), XMFLOAT2(0.0f, 0.0f)},
		{XMFLOAT3(1.0f, -1.0f, 0), XMFLOAT2(1.0f, 1.0f)},
		{XMFLOAT3(1.0f, 1.0f, 0), XMFLOAT2(1.0f, 0.0f)},
		//{XMFLOAT3(-1.0f, 1.0f, 0), XMFLOAT2(0.0f, 0.0f)},
		//{XMFLOAT3(1.0f, 1.0f, 0), XMFLOAT2(1.0f, 0.0f)},
	};

	// Make new render target view
	ID3D11RenderTargetView *RTV;
	hr = m_Device->CreateRenderTargetView(pResizedFrame, nullptr, &RTV);
	RETURN_ON_BAD_HR(hr);

	// Set resources
	UINT Stride = sizeof(VERTEX);
	UINT Offset = 0;
	FLOAT blendFactor[4] = { 0.f, 0.f, 0.f, 0.f };
	m_DeviceContext->OMSetBlendState(nullptr, blendFactor, 0xffffffff);
	m_DeviceContext->OMSetRenderTargets(1, &RTV, nullptr);
	m_DeviceContext->VSSetShader(m_VertexShader, nullptr, 0);
	m_DeviceContext->PSSetShader(m_PixelShader, nullptr, 0);
	m_DeviceContext->PSSetShaderResources(0, 1, &srcSRV);
	m_DeviceContext->PSSetSamplers(0, 1, &m_SamplerLinear);
	m_DeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

	D3D11_BUFFER_DESC BufferDesc;
	RtlZeroMemory(&BufferDesc, sizeof(BufferDesc));
	BufferDesc.Usage = D3D11_USAGE_DEFAULT;
	BufferDesc.ByteWidth = sizeof(VERTEX) * _countof(Vertices);
	BufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	BufferDesc.CPUAccessFlags = 0;
	D3D11_SUBRESOURCE_DATA InitData;
	RtlZeroMemory(&InitData, sizeof(InitData));
	InitData.pSysMem = Vertices;

	ID3D11Buffer *VertexBuffer = nullptr;

	// Create vertex buffer
	hr = m_Device->CreateBuffer(&BufferDesc, &InitData, &VertexBuffer);
	if (FAILED(hr))
	{
		srcSRV->Release();
		srcSRV = nullptr;
		return S_FALSE;
	}
	m_DeviceContext->IASetVertexBuffers(0, 1, &VertexBuffer, &Stride, &Offset);

	// Draw textured quad onto render target
	m_DeviceContext->Draw(_countof(Vertices), 0);

	// Restore view port
	m_DeviceContext->RSSetViewports(1, &VP);

	// Clear shader resource
	ID3D11ShaderResourceView *null[] = { nullptr, nullptr };
	m_DeviceContext->PSSetShaderResources(0, 1, null);

	// Clean up
	VertexBuffer->Release();
	VertexBuffer = nullptr;

	srcSRV->Release();
	srcSRV = nullptr;

	RTV->Release();
	RTV = nullptr;

	return hr;
}

HRESULT TextureManager::RotateTexture(_In_ ID3D11Texture2D *pOrgTexture, _Outptr_ ID3D11Texture2D **ppRotatedTexture, _In_ DXGI_MODE_ROTATION rotation)
{
	HRESULT hr;
	// Create shader resource from texture of the original frame
	D3D11_TEXTURE2D_DESC textureDesc = {};
	pOrgTexture->GetDesc(&textureDesc);
	D3D11_SHADER_RESOURCE_VIEW_DESC SDesc = {};
	SDesc.Format = textureDesc.Format;
	SDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	SDesc.Texture2D.MostDetailedMip = textureDesc.MipLevels - 1;
	SDesc.Texture2D.MipLevels = textureDesc.MipLevels;
	ID3D11ShaderResourceView *srcSRV;
	hr = m_Device->CreateShaderResourceView(pOrgTexture, &SDesc, &srcSRV);
	if (FAILED(hr))
	{
		_com_error err(hr);
		LOG_ERROR(L"Failed to create shader resource from original frame texture: %ls", err.ErrorMessage());
		return hr;
	}

	LONG rotatedWidth = textureDesc.Width;
	LONG rotatedHeight = textureDesc.Height;

	switch (rotation)
	{
	case DXGI_MODE_ROTATION_ROTATE90:
	case DXGI_MODE_ROTATION_ROTATE270:
		rotatedWidth = textureDesc.Height;
		rotatedHeight = textureDesc.Width;
		break;
	}

	// Create target texture
	CComPtr<ID3D11Texture2D> pRotatedFrame = nullptr;
	D3D11_TEXTURE2D_DESC targetDesc;
	InitializeDesc(rotatedWidth, rotatedHeight, &targetDesc);
	hr = m_Device->CreateTexture2D(&targetDesc, nullptr, &pRotatedFrame);
	RETURN_ON_BAD_HR(hr);
	*ppRotatedTexture = pRotatedFrame;
	(*ppRotatedTexture)->AddRef();

	// Save current view port so we can restore later
	D3D11_VIEWPORT VP;
	UINT numViewports = 1;
	m_DeviceContext->RSGetViewports(&numViewports, &VP);

	// Set view port
	SetViewPort(m_DeviceContext, targetDesc.Width, targetDesc.Height);

	// Vertices for drawing whole texture
	VERTEX Vertices[6] =
	{
		{ XMFLOAT3(-1.0f, -1.0f, 0), XMFLOAT2(0.0f, 1.0f) },
		{ XMFLOAT3(-1.0f, 1.0f, 0), XMFLOAT2(0.0f, 0.0f) },
		{ XMFLOAT3(1.0f, -1.0f, 0), XMFLOAT2(1.0f, 1.0f) },
		{ XMFLOAT3(1.0f, -1.0f, 0), XMFLOAT2(1.0f, 1.0f) },
		{ XMFLOAT3(-1.0f, 1.0f, 0), XMFLOAT2(0.0f, 0.0f) },
		{ XMFLOAT3(1.0f, 1.0f, 0), XMFLOAT2(1.0f, 0.0f) },
	};

	ConfigureRotationVertices(Vertices, RECT{ 0,0,(long)textureDesc.Width,(long)textureDesc.Height }, rotation);

	// Make new render target view
	ID3D11RenderTargetView *RTV;
	hr = m_Device->CreateRenderTargetView(pRotatedFrame, nullptr, &RTV);
	RETURN_ON_BAD_HR(hr);

	// Set resources
	UINT Stride = sizeof(VERTEX);
	UINT Offset = 0;
	FLOAT blendFactor[4] = { 0.f, 0.f, 0.f, 0.f };
	m_DeviceContext->OMSetBlendState(nullptr, blendFactor, 0xffffffff);
	m_DeviceContext->OMSetRenderTargets(1, &RTV, nullptr);
	m_DeviceContext->VSSetShader(m_VertexShader, nullptr, 0);
	m_DeviceContext->PSSetShader(m_PixelShader, nullptr, 0);
	m_DeviceContext->PSSetShaderResources(0, 1, &srcSRV);
	m_DeviceContext->PSSetSamplers(0, 1, &m_SamplerLinear);
	m_DeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	D3D11_BUFFER_DESC BufferDesc;
	RtlZeroMemory(&BufferDesc, sizeof(BufferDesc));
	BufferDesc.Usage = D3D11_USAGE_DEFAULT;
	BufferDesc.ByteWidth = sizeof(VERTEX) * _countof(Vertices);
	BufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	BufferDesc.CPUAccessFlags = 0;
	D3D11_SUBRESOURCE_DATA InitData;
	RtlZeroMemory(&InitData, sizeof(InitData));
	InitData.pSysMem = Vertices;

	ID3D11Buffer *VertexBuffer = nullptr;

	// Create vertex buffer
	hr = m_Device->CreateBuffer(&BufferDesc, &InitData, &VertexBuffer);
	if (FAILED(hr))
	{
		srcSRV->Release();
		srcSRV = nullptr;
		return S_FALSE;
	}
	m_DeviceContext->IASetVertexBuffers(0, 1, &VertexBuffer, &Stride, &Offset);

	// Draw textured quad onto render target
	m_DeviceContext->Draw(_countof(Vertices), 0);

	// Restore view port
	m_DeviceContext->RSSetViewports(1, &VP);

	// Clear shader resource
	ID3D11ShaderResourceView *null[] = { nullptr, nullptr };
	m_DeviceContext->PSSetShaderResources(0, 1, null);

	// Clean up
	VertexBuffer->Release();
	VertexBuffer = nullptr;

	srcSRV->Release();
	srcSRV = nullptr;

	RTV->Release();
	RTV = nullptr;

	return hr;
}

HRESULT TextureManager::DrawTexture(_Inout_ ID3D11Texture2D *pCanvasTexture, _In_ ID3D11Texture2D *pTexture, _In_ RECT rect)
{
	HRESULT hr = S_FALSE;
	D3D11_TEXTURE2D_DESC desktopDesc = {};
	pCanvasTexture->GetDesc(&desktopDesc);
	D3D11_TEXTURE2D_DESC overlayDesc = {};
	pTexture->GetDesc(&overlayDesc);

	// Save current view port so we can restore later
	D3D11_VIEWPORT VP;
	UINT numViewports = 1;
	m_DeviceContext->RSGetViewports(&numViewports, &VP);

	// Set view port
	SetViewPort(m_DeviceContext, desktopDesc.Width, desktopDesc.Height);

	VERTEX Vertices[] =
	{
		{ XMFLOAT3(-1.0f, -1.0f, 0), XMFLOAT2(0.0f, 1.0f) },
		{ XMFLOAT3(-1.0f, 1.0f, 0), XMFLOAT2(0.0f, 0.0f) },
		{ XMFLOAT3(1.0f, -1.0f, 0), XMFLOAT2(1.0f, 1.0f) },
		{ XMFLOAT3(1.0f, -1.0f, 0), XMFLOAT2(1.0f, 1.0f) },
		{ XMFLOAT3(-1.0f, 1.0f, 0), XMFLOAT2(0.0f, 0.0f) },
		{ XMFLOAT3(1.0f, 1.0f, 0), XMFLOAT2(1.0f, 0.0f) },
	};

	LONG overlayLeft = rect.left;
	LONG overlayTop = rect.top;
	LONG overlayWidth = RectWidth(rect);
	LONG overlayHeight = RectHeight(rect);
	// Center of desktop dimensions
	FLOAT centerX = ((FLOAT)desktopDesc.Width / 2);
	FLOAT centerY = ((FLOAT)desktopDesc.Height / 2);

	Vertices[0].Pos.x = (overlayLeft - centerX) / centerX;
	Vertices[0].Pos.y = -1 * ((overlayTop + overlayHeight) - centerY) / centerY;
	Vertices[1].Pos.x = (overlayLeft - centerX) / centerX;
	Vertices[1].Pos.y = -1 * (overlayTop - centerY) / centerY;
	Vertices[2].Pos.x = ((overlayLeft + overlayWidth) - centerX) / centerX;
	Vertices[2].Pos.y = -1 * ((overlayTop + overlayHeight) - centerY) / centerY;
	Vertices[5].Pos.x = ((overlayLeft + overlayWidth) - centerX) / centerX;
	Vertices[5].Pos.y = -1 * (overlayTop - centerY) / centerY;


	Vertices[3].Pos.x = Vertices[2].Pos.x;
	Vertices[3].Pos.y = Vertices[2].Pos.y;
	Vertices[4].Pos.x = Vertices[1].Pos.x;
	Vertices[4].Pos.y = Vertices[1].Pos.y;

	// Set shader resource properties
	D3D11_SHADER_RESOURCE_VIEW_DESC shaderDesc;
	shaderDesc.Format = overlayDesc.Format;
	shaderDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	shaderDesc.Texture2D.MostDetailedMip = overlayDesc.MipLevels - 1;
	shaderDesc.Texture2D.MipLevels = overlayDesc.MipLevels;

	// Create shader resource from texture
	ID3D11ShaderResourceView *srcSRV;
	hr = m_Device->CreateShaderResourceView(pTexture, &shaderDesc, &srcSRV);
	if (FAILED(hr))
	{
		_com_error err(hr);
		LOG_ERROR(L"Failed to create shader resource from overlay texture: %ls", err.ErrorMessage());
		return hr;
	}
	D3D11_BUFFER_DESC bufferDesc;
	ZeroMemory(&bufferDesc, sizeof(D3D11_BUFFER_DESC));
	bufferDesc.Usage = D3D11_USAGE_DEFAULT;
	bufferDesc.ByteWidth = sizeof(VERTEX) * _countof(Vertices);
	bufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	bufferDesc.CPUAccessFlags = 0;

	D3D11_SUBRESOURCE_DATA initData;
	ZeroMemory(&initData, sizeof(D3D11_SUBRESOURCE_DATA));
	initData.pSysMem = Vertices;

	// Create vertex buffer
	ID3D11Buffer *VertexBuffer;
	hr = m_Device->CreateBuffer(&bufferDesc, &initData, &VertexBuffer);
	if (FAILED(hr))
	{
		_com_error err(hr);
		LOG_ERROR(L"Failed to create overlay vertex buffer: %ls", err.ErrorMessage());
		return hr;
	}
	ID3D11RenderTargetView *RTV;
	// Create a render target view
	hr = m_Device->CreateRenderTargetView(pCanvasTexture, nullptr, &RTV);
	if (FAILED(hr))
	{
		_com_error err(hr);
		LOG_ERROR(L"Failed to create render target view: %ls", err.ErrorMessage());
		return hr;
	}
	// Set resources
	FLOAT BlendFactor[4] = { 0.f, 0.f, 0.f, 0.f };
	UINT Stride = sizeof(VERTEX);
	UINT Offset = 0;
	m_DeviceContext->IASetVertexBuffers(0, 1, &VertexBuffer, &Stride, &Offset);
	m_DeviceContext->OMSetBlendState(m_BlendState, BlendFactor, 0xFFFFFFFF);
	m_DeviceContext->OMSetRenderTargets(1, &RTV, nullptr);
	m_DeviceContext->VSSetShader(m_VertexShader, nullptr, 0);
	m_DeviceContext->PSSetShader(m_PixelShader, nullptr, 0);
	m_DeviceContext->PSSetShaderResources(0, 1, &srcSRV);
	m_DeviceContext->PSSetSamplers(0, 1, &m_SamplerLinear);
	m_DeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	// Draw
	m_DeviceContext->Draw(_countof(Vertices), 0);

	// Restore view port
	m_DeviceContext->RSSetViewports(1, &VP);
	// Clear shader resource
	ID3D11ShaderResourceView *nullShader[] = { nullptr };
	m_DeviceContext->PSSetShaderResources(0, 1, nullShader);

	// Clean up
	VertexBuffer->Release();
	VertexBuffer = nullptr;

	srcSRV->Release();
	srcSRV = nullptr;

	RTV->Release();
	RTV = nullptr;
	return hr;
}

void TextureManager::ConfigureRotationVertices(_Inout_ VERTEX(&vertices)[6], _In_ RECT textureRect, _In_opt_ DXGI_MODE_ROTATION rotation)
{
	LONG textureLeft = textureRect.left;
	LONG textureTop = textureRect.top;
	LONG textureWidth = RectWidth(textureRect);
	LONG textureHeight = RectHeight(textureRect);

	LONG rotatedWidth = textureWidth;
	LONG rotatedHeight = textureHeight;

	switch (rotation)
	{
	case DXGI_MODE_ROTATION_ROTATE90:
	case DXGI_MODE_ROTATION_ROTATE270:
		rotatedWidth = textureHeight;
		rotatedHeight = textureWidth;
		break;
	}

	// Center of desktop dimensions
	FLOAT centerX = ((FLOAT)rotatedWidth / 2);
	FLOAT centerY = ((FLOAT)rotatedHeight / 2);

	// Rotation compensated destination rect
	RECT rotatedDestRect = textureRect;

	// Set appropriate coordinates compensated for rotation
	switch (rotation)
	{
	case DXGI_MODE_ROTATION_ROTATE90:
	{
		rotatedDestRect.left = rotatedWidth - textureRect.bottom;
		rotatedDestRect.top = textureRect.left;
		rotatedDestRect.right = rotatedWidth - textureRect.top;
		rotatedDestRect.bottom = textureRect.right;

		vertices[0].TexCoord = XMFLOAT2(textureRect.right / static_cast<FLOAT>(textureWidth), textureRect.bottom / static_cast<FLOAT>(textureHeight));
		vertices[1].TexCoord = XMFLOAT2(textureRect.left / static_cast<FLOAT>(textureWidth), textureRect.bottom / static_cast<FLOAT>(textureHeight));
		vertices[2].TexCoord = XMFLOAT2(textureRect.right / static_cast<FLOAT>(textureWidth), textureRect.top / static_cast<FLOAT>(textureHeight));
		vertices[5].TexCoord = XMFLOAT2(textureRect.left / static_cast<FLOAT>(textureWidth), textureRect.top / static_cast<FLOAT>(textureHeight));
		break;
	}
	case DXGI_MODE_ROTATION_ROTATE180:
	{
		rotatedDestRect.left = rotatedWidth - textureRect.right;
		rotatedDestRect.top = rotatedHeight - textureRect.bottom;
		rotatedDestRect.right = rotatedWidth - textureRect.left;
		rotatedDestRect.bottom = rotatedHeight - textureRect.top;

		vertices[0].TexCoord = XMFLOAT2(textureRect.right / static_cast<FLOAT>(textureWidth), textureRect.top / static_cast<FLOAT>(textureHeight));
		vertices[1].TexCoord = XMFLOAT2(textureRect.right / static_cast<FLOAT>(textureWidth), textureRect.bottom / static_cast<FLOAT>(textureHeight));
		vertices[2].TexCoord = XMFLOAT2(textureRect.left / static_cast<FLOAT>(textureWidth), textureRect.top / static_cast<FLOAT>(textureHeight));
		vertices[5].TexCoord = XMFLOAT2(textureRect.left / static_cast<FLOAT>(textureWidth), textureRect.bottom / static_cast<FLOAT>(textureHeight));
		break;
	}
	case DXGI_MODE_ROTATION_ROTATE270:
	{
		rotatedDestRect.left = textureRect.top;
		rotatedDestRect.top = rotatedHeight - textureRect.right;
		rotatedDestRect.right = textureRect.bottom;
		rotatedDestRect.bottom = rotatedHeight - textureRect.left;

		vertices[0].TexCoord = XMFLOAT2(textureRect.left / static_cast<FLOAT>(textureWidth), textureRect.top / static_cast<FLOAT>(textureHeight));
		vertices[1].TexCoord = XMFLOAT2(textureRect.right / static_cast<FLOAT>(textureWidth), textureRect.top / static_cast<FLOAT>(textureHeight));
		vertices[2].TexCoord = XMFLOAT2(textureRect.left / static_cast<FLOAT>(textureWidth), textureRect.bottom / static_cast<FLOAT>(textureHeight));
		vertices[5].TexCoord = XMFLOAT2(textureRect.right / static_cast<FLOAT>(textureWidth), textureRect.bottom / static_cast<FLOAT>(textureHeight));
		break;
	}
	case DXGI_MODE_ROTATION_UNSPECIFIED:
	case DXGI_MODE_ROTATION_IDENTITY:
	{
		vertices[0].TexCoord = XMFLOAT2(textureRect.left / static_cast<FLOAT>(textureWidth), textureRect.bottom / static_cast<FLOAT>(textureHeight));
		vertices[1].TexCoord = XMFLOAT2(textureRect.left / static_cast<FLOAT>(textureWidth), textureRect.top / static_cast<FLOAT>(textureHeight));
		vertices[2].TexCoord = XMFLOAT2(textureRect.right / static_cast<FLOAT>(textureWidth), textureRect.bottom / static_cast<FLOAT>(textureHeight));
		vertices[5].TexCoord = XMFLOAT2(textureRect.right / static_cast<FLOAT>(textureWidth), textureRect.top / static_cast<FLOAT>(textureHeight));
		break;
	}
	default:
		assert(false);
	}

	// Set positions
	vertices[0].Pos = XMFLOAT3((rotatedDestRect.left - centerX) / static_cast<FLOAT>(centerX),
		-1 * (rotatedDestRect.bottom - centerY) / static_cast<FLOAT>(centerY),
		0.0f);
	vertices[1].Pos = XMFLOAT3((rotatedDestRect.left - centerX) / static_cast<FLOAT>(centerX),
		-1 * (rotatedDestRect.top - centerY) / static_cast<FLOAT>(centerY),
		0.0f);
	vertices[2].Pos = XMFLOAT3((rotatedDestRect.right - centerX) / static_cast<FLOAT>(centerX),
		-1 * (rotatedDestRect.bottom - centerY) / static_cast<FLOAT>(centerY),
		0.0f);
	vertices[3].Pos = vertices[2].Pos;
	vertices[4].Pos = vertices[1].Pos;
	vertices[5].Pos = XMFLOAT3((rotatedDestRect.right - centerX) / static_cast<FLOAT>(centerX),
		-1 * (rotatedDestRect.top - centerY) / static_cast<FLOAT>(centerY),
		0.0f);

	vertices[3].TexCoord = vertices[2].TexCoord;
	vertices[4].TexCoord = vertices[1].TexCoord;
}

HRESULT TextureManager::InitializeDesc(_In_ UINT width, _In_ UINT height, _Out_ D3D11_TEXTURE2D_DESC *pTargetDesc)
{
	// Create shared texture for the target view
	RtlZeroMemory(pTargetDesc, sizeof(D3D11_TEXTURE2D_DESC));
	pTargetDesc->Width = width;
	pTargetDesc->Height = height;
	pTargetDesc->MipLevels = 1;
	pTargetDesc->ArraySize = 1;
	pTargetDesc->Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	pTargetDesc->SampleDesc.Count = 1;
	pTargetDesc->Usage = D3D11_USAGE_DEFAULT;
	pTargetDesc->BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
	pTargetDesc->CPUAccessFlags = 0;
	pTargetDesc->MiscFlags = 0;

	return S_OK;
}

HRESULT TextureManager::CropTexture(_In_ ID3D11Texture2D *pTexture, _In_ RECT cropRect, _Outptr_ ID3D11Texture2D **pCroppedFrame)
{
	D3D11_TEXTURE2D_DESC frameDesc;
	pTexture->GetDesc(&frameDesc);
	frameDesc.Width = RectWidth(cropRect);
	frameDesc.Height = RectHeight(cropRect);
	frameDesc.MiscFlags = 0;
	CComPtr<ID3D11Device> pDevice;
	pTexture->GetDevice(&pDevice);
	CComPtr<ID3D11Texture2D> pCroppedFrameCopy = nullptr;
	RETURN_ON_BAD_HR(pDevice->CreateTexture2D(&frameDesc, nullptr, &pCroppedFrameCopy));
	D3D11_BOX sourceRegion;
	RtlZeroMemory(&sourceRegion, sizeof(sourceRegion));
	sourceRegion.left = cropRect.left;
	sourceRegion.right = cropRect.right;
	sourceRegion.top = cropRect.top;
	sourceRegion.bottom = cropRect.bottom;
	sourceRegion.front = 0;
	sourceRegion.back = 1;
	CComPtr<ID3D11DeviceContext> context;
	pDevice->GetImmediateContext(&context);
	context->CopySubresourceRegion(pCroppedFrameCopy, 0, 0, 0, 0, pTexture, 0, &sourceRegion);
	*pCroppedFrame = pCroppedFrameCopy;
	(*pCroppedFrame)->AddRef();
	return S_OK;
}

HRESULT TextureManager::CopyTextureWithCPU(_In_ ID3D11Device *pDevice, _In_ ID3D11Texture2D *pSourceTexture, _Outptr_ ID3D11Texture2D **ppTextureCopy)
{
	HRESULT hr = E_FAIL;
	CComPtr<ID3D11Device> pSourceDevice = nullptr;
	pSourceTexture->GetDevice(&pSourceDevice);
	CComPtr<ID3D11DeviceContext> pDuplicationDeviceContext = nullptr;
	pSourceDevice->GetImmediateContext(&pDuplicationDeviceContext);

	//Create a new staging texture on the source device that supports CPU access.
	CComPtr<ID3D11Texture2D> pStagingTexture;
	D3D11_TEXTURE2D_DESC stagingDesc;
	pSourceTexture->GetDesc(&stagingDesc);
	stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	stagingDesc.Usage = D3D11_USAGE_STAGING;
	stagingDesc.MiscFlags = 0;
	stagingDesc.BindFlags = D3D11_BIND_RENDER_TARGET;
	RETURN_ON_BAD_HR(hr = pSourceDevice->CreateTexture2D(&stagingDesc, nullptr, &pStagingTexture));
	//Copy the source surface to the new staging texture.
	pDuplicationDeviceContext->CopyResource(pStagingTexture, pSourceTexture);
	D3D11_MAPPED_SUBRESOURCE mapped{};
	//Map the staging texture to get access to the texture data.
	RETURN_ON_BAD_HR(hr = pDuplicationDeviceContext->Map(pStagingTexture, 0, D3D11_MAP_READ, 0, &mapped));
	BYTE *data = (BYTE *)mapped.pData;
	LONG stride = mapped.RowPitch;
	pDuplicationDeviceContext->Unmap(pStagingTexture, 0);
	// Set up init data and create new texture from the mapped data.
	D3D11_SUBRESOURCE_DATA initData = { 0 };
	initData.pSysMem = data;
	initData.SysMemPitch = abs(stride);
	initData.SysMemSlicePitch = 0;
	D3D11_TEXTURE2D_DESC mappedTextureDesc;
	pSourceTexture->GetDesc(&mappedTextureDesc);
	mappedTextureDesc.MiscFlags = 0;
	mappedTextureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	CComPtr<ID3D11Texture2D> pTextureCopy;
	RETURN_ON_BAD_HR(hr = pDevice->CreateTexture2D(&mappedTextureDesc, &initData, &pTextureCopy));
	*ppTextureCopy = pTextureCopy;
	(*ppTextureCopy)->AddRef();
	return hr;
}

HRESULT TextureManager::CreateTextureFromBuffer(_In_ BYTE *pFrameBuffer, _In_ LONG stride, _In_ UINT width, _In_ UINT height, _Outptr_ ID3D11Texture2D **ppTexture, std::optional<D3D11_RESOURCE_MISC_FLAG> miscFlag)
{
	D3D11_TEXTURE2D_DESC desc = { 0 };
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	desc.SampleDesc.Count = 1;
	desc.SampleDesc.Quality = 0;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	desc.CPUAccessFlags = 0;
	if (miscFlag.has_value()) {
		desc.MiscFlags = miscFlag.value();
	}
	else {
		desc.MiscFlags = 0;
	}


	// Set texture properties
	desc.Width = width;
	desc.Height = height;

	// Set up init data
	D3D11_SUBRESOURCE_DATA initData;
	ZeroMemory(&initData, sizeof(D3D11_SUBRESOURCE_DATA));
	initData.pSysMem = pFrameBuffer;
	initData.SysMemPitch = abs(stride);
	initData.SysMemSlicePitch = 0;

	// Create overlay as texture

	HRESULT hr = m_Device->CreateTexture2D(&desc, &initData, ppTexture);
	return hr;
}

HRESULT TextureManager::BlankTexture(_Inout_ ID3D11Texture2D *pTexture, _In_ RECT rect, _In_ INT offsetX, _In_  INT offsetY) {
	int width = RectWidth(rect);
	int height = RectHeight(rect);
	D3D11_BOX Box{};
	// Copy back to shared surface
	Box.right = width;
	Box.bottom = height;
	Box.back = 1;

	CComPtr<ID3D11Texture2D> pBlankFrame;
	D3D11_TEXTURE2D_DESC desc;
	pTexture->GetDesc(&desc);
	desc.MiscFlags = 0;
	desc.Width = width;
	desc.Height = height;
	HRESULT hr = m_Device->CreateTexture2D(&desc, nullptr, &pBlankFrame);
	if (SUCCEEDED(hr)) {
		m_DeviceContext->CopySubresourceRegion(pTexture, 0, rect.left + offsetX, rect.top + offsetY, 0, pBlankFrame, 0, &Box);
	}
	return S_OK;
}

//
// Releases all references
//
void TextureManager::CleanRefs()
{
	if (m_VertexShader)
	{
		m_VertexShader->Release();
		m_VertexShader = nullptr;
	}

	if (m_PixelShader)
	{
		m_PixelShader->Release();
		m_PixelShader = nullptr;
	}

	if (m_InputLayout)
	{
		m_InputLayout->Release();
		m_InputLayout = nullptr;
	}

	if (m_SamplerLinear)
	{
		m_SamplerLinear->Release();
		m_SamplerLinear = nullptr;
	}

	if (m_BlendState)
	{
		m_BlendState->Release();
		m_BlendState = nullptr;
	}
}
