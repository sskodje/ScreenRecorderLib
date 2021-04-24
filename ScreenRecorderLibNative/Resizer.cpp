#include "Resizer.h"
#include "screengrab.h"
#include "utilities.h"
#include <wincodec.h>
#include <atlbase.h>

using namespace DirectX;

Resizer::Resizer() :
	m_Device(nullptr),
	m_DeviceContext(nullptr),
	m_SamplerLinear(nullptr),
	m_BlendState(nullptr),
	m_VertexShader(nullptr),
	m_PixelShader(nullptr),
	m_InputLayout(nullptr)
{
}

Resizer::~Resizer()
{
    CleanRefs();
}

HRESULT Resizer::Initialize(ID3D11DeviceContext* ImmediateContext, ID3D11Device* Device)
{
    m_Device = Device;
    m_DeviceContext = ImmediateContext;

    HRESULT hr;

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
    hr = InitShaders();
    RETURN_ON_BAD_HR(hr);

    return S_OK;
}

HRESULT Resizer::Resize(ID3D11Texture2D* orgTexture, ID3D11Texture2D** pResizedTexture, UINT targetWidth, UINT targetHeight, double viewPortRatio_width, double viewPortRatio_height)
{
    HRESULT hr;

    // Create shader resource from texture of the original frame
    D3D11_TEXTURE2D_DESC desktopDesc = {};
    orgTexture->GetDesc(&desktopDesc);
    D3D11_SHADER_RESOURCE_VIEW_DESC SDesc = {};
    SDesc.Format = desktopDesc.Format;
    SDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    SDesc.Texture2D.MostDetailedMip = desktopDesc.MipLevels - 1;
    SDesc.Texture2D.MipLevels = desktopDesc.MipLevels;
    ID3D11ShaderResourceView* srcSRV;
    hr = m_Device->CreateShaderResourceView(orgTexture, &SDesc, &srcSRV);
    if (FAILED(hr))
    {
        _com_error err(hr);
        ERROR(L"Failed to create shader resource from original frame's texture: %ls", err.ErrorMessage());
        return hr;
    }

    // Create target texture
    CComPtr<ID3D11Texture2D> pResizedFrame = nullptr;
    D3D11_TEXTURE2D_DESC targetDesc;
    InitializeDesc(targetWidth, targetHeight, &targetDesc);
    hr = m_Device->CreateTexture2D(&targetDesc, nullptr, &pResizedFrame);
    RETURN_ON_BAD_HR(hr);
    *pResizedTexture = pResizedFrame;
    (*pResizedTexture)->AddRef();

    // Make new render target view
    ID3D11RenderTargetView* RTV;
    hr = m_Device->CreateRenderTargetView(pResizedFrame, nullptr, &RTV);
    RETURN_ON_BAD_HR(hr);

    m_DeviceContext->OMSetRenderTargets(1, &RTV, nullptr);

    // Set view port
    SetViewPort(targetWidth, targetHeight, viewPortRatio_width, viewPortRatio_height);

    // Vertices for drawing whole texture
    VERTEX Vertices[] =
    {
        {XMFLOAT3(-1.0f, -1.0f, 0), XMFLOAT2(0.0f, 1.0f)},
        {XMFLOAT3(-1.0f, 1.0f, 0), XMFLOAT2(0.0f, 0.0f)},
        {XMFLOAT3(1.0f, -1.0f, 0), XMFLOAT2(1.0f, 1.0f)},
        {XMFLOAT3(1.0f, 1.0f, 0), XMFLOAT2(1.0f, 0.0f)},
    };

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
    m_DeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);    // For 4 vertices

    D3D11_BUFFER_DESC BufferDesc;
    RtlZeroMemory(&BufferDesc, sizeof(BufferDesc));
    BufferDesc.Usage = D3D11_USAGE_DEFAULT;
    BufferDesc.ByteWidth = sizeof(VERTEX) * _countof(Vertices);
    BufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    BufferDesc.CPUAccessFlags = 0;
    D3D11_SUBRESOURCE_DATA InitData;
    RtlZeroMemory(&InitData, sizeof(InitData));
    InitData.pSysMem = Vertices;

    ID3D11Buffer* VertexBuffer = nullptr;

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

    // Clean up
    VertexBuffer->Release();
    VertexBuffer = nullptr;

    srcSRV->Release();
    srcSRV = nullptr;

    RTV->Release();
    RTV = nullptr;

    return S_OK;
}

HRESULT Resizer::InitializeDesc(_In_ UINT width, _In_ UINT height, _Out_ D3D11_TEXTURE2D_DESC* pTargetDesc)
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

void Resizer::SetViewPort(UINT width, UINT height, double viewPortRatio_width, double viewPortRatio_height)
{
    D3D11_VIEWPORT VP;
    VP.Width = static_cast<FLOAT>(width * viewPortRatio_width);
    VP.Height = static_cast<FLOAT>(height * viewPortRatio_height);
    VP.MinDepth = 0.0f;
    VP.MaxDepth = 1.0f;
    VP.TopLeftX = 0;
    VP.TopLeftY = 0;
    m_DeviceContext->RSSetViewports(1, &VP);
}
//
// Initialize shaders for drawing to screen
//
HRESULT Resizer::InitShaders()
{
    HRESULT hr;

    UINT Size = ARRAYSIZE(g_VS);
    hr = m_Device->CreateVertexShader(g_VS, Size, nullptr, &m_VertexShader);
    RETURN_ON_BAD_HR(hr);

    D3D11_INPUT_ELEMENT_DESC Layout[] =
    {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0}
    };
    UINT NumElements = ARRAYSIZE(Layout);
    hr = m_Device->CreateInputLayout(Layout, NumElements, g_VS, Size, &m_InputLayout);
    RETURN_ON_BAD_HR(hr);

    m_DeviceContext->IASetInputLayout(m_InputLayout);

    Size = ARRAYSIZE(g_PS);
    hr = m_Device->CreatePixelShader(g_PS, Size, nullptr, &m_PixelShader);
    RETURN_ON_BAD_HR(hr);

    return S_OK;
}
//
// Releases all references
//
void Resizer::CleanRefs()
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
