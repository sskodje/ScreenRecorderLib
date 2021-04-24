#pragma once
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_2.h>
#include <DirectXMath.h>

#include "PixelShader.h"
#include "VertexShader.h"

class Resizer
{
public:
    //
    // A vertex with a position and texture coordinate
    //
    typedef struct _VERTEX
    {
        DirectX::XMFLOAT3 Pos;
        DirectX::XMFLOAT2 TexCoord;
    } VERTEX;

	Resizer();
	~Resizer();
    HRESULT Initialize(ID3D11DeviceContext* ImmediateContext, ID3D11Device* Device);
    HRESULT Resize(ID3D11Texture2D* orgTexture, ID3D11Texture2D** pResizedTexture, UINT targetWidth, UINT targetHeight, double viewPortRatio_width = 1.0, double viewPortRatio_height = 1.0);

private:
    void SetViewPort(UINT width, UINT height, double viewPortRatio_width, double viewPortRatio_height);
    HRESULT InitShaders();
    HRESULT InitializeDesc(_In_ UINT width, _In_ UINT height, _Out_ D3D11_TEXTURE2D_DESC* pTargetDesc);
    void CleanRefs();

    ID3D11Device* m_Device;
    ID3D11DeviceContext* m_DeviceContext;
    ID3D11SamplerState* m_SamplerLinear;
    ID3D11BlendState* m_BlendState;
    ID3D11VertexShader* m_VertexShader;
    ID3D11PixelShader* m_PixelShader;
    ID3D11InputLayout* m_InputLayout;
};

