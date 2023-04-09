#pragma once
#include <DirectXMath.h>
#include "CommonTypes.h"
#include "DX.util.h"
#include <unordered_map>

using namespace std;

class TextureManager
{
public:
	TextureManager();
	~TextureManager();
	HRESULT Initialize(_In_ ID3D11DeviceContext *pDeviceContext, _In_ ID3D11Device *Device);
	HRESULT ResizeTexture(_In_ ID3D11Texture2D *pOrgTexture, _In_  SIZE targetSize, _In_ TextureStretchMode stretch, _Outptr_ ID3D11Texture2D **ppResizedTexture, _Out_opt_ RECT *pContentRect = nullptr);
	HRESULT RotateTexture(_In_ ID3D11Texture2D *pOrgTexture, _In_ DXGI_MODE_ROTATION rotation, _Outptr_ ID3D11Texture2D **ppRotatedTexture);
	HRESULT DrawTexture(_Inout_ ID3D11Texture2D *pCanvasTexture, _In_ ID3D11Texture2D *pTexture, _In_ RECT rect);
	/// <summary>
	/// Crops a texture to the given rectangle.
	/// </summary>
	/// <param name="pTexture">The texture to crop</param>
	/// <param name="cropRect">A rectangle to crop the texture to</param>
	/// <param name="pCroppedFrame">The cropped texture</param>
	/// <returns>S_OK if successful, S_FALSE is crop rect is larger than texture, error code on failure</returns>
	HRESULT CropTexture(_In_ ID3D11Texture2D *pTexture, _In_ RECT cropRect, _Outptr_ ID3D11Texture2D **pCroppedFrame);
	/// <summary>
	/// Copy a texture via the CPU. This can be used to copy a texture created on one physical device to be rendered on another.
	/// </summary>
	/// <param name="pDevice">The device with which to create the texture copy</param>
	/// <param name="pTexture">The texture to copy</param>
	/// <param name="ppTextureCopy">The copied texture</param>
	HRESULT CopyTextureWithCPU(_In_ ID3D11Device *pDevice, _In_ ID3D11Texture2D *pTexture, _Outptr_ ID3D11Texture2D **ppTextureCopy);
	HRESULT CreateTexture(_In_ UINT width, _In_ UINT height, _Outptr_ ID3D11Texture2D **ppTexture, UINT miscFlag = 0, UINT bindFlag = 0);
	HRESULT CreateTextureFromBuffer(_In_ BYTE *pFrameBuffer, _In_ LONG stride, _In_ UINT width, _In_ UINT height, _Outptr_ ID3D11Texture2D **ppTexture, UINT miscFlag = 0, UINT bindFlag = 0);
	HRESULT BlankTexture(_Inout_ ID3D11Texture2D *pTexture, _In_ RECT rect, _In_ INT OffsetX = 0, _In_  INT OffsetY = 0);
private:
	HRESULT InitializeDesc(_In_ UINT width, _In_ UINT height, _Out_ D3D11_TEXTURE2D_DESC *pTargetDesc);
	HRESULT GetOrCreateTexture(_In_ D3D11_TEXTURE2D_DESC desc, _Outptr_ ID3D11Texture2D **ppTexture);
	void ConfigureRotationVertices(_Inout_ VERTEX(&vertices)[6], _In_ RECT textureRect, _In_opt_ DXGI_MODE_ROTATION rotation = DXGI_MODE_ROTATION_UNSPECIFIED);
	void CleanRefs();

	ID3D11Device *m_Device;
	ID3D11DeviceContext *m_DeviceContext;
	ID3D11SamplerState *m_SamplerLinear;
	ID3D11BlendState *m_BlendState;
	ID3D11VertexShader *m_VertexShader;
	ID3D11PixelShader *m_PixelShader;
	ID3D11InputLayout *m_InputLayout;


	struct TextureDescHasher {
		std::size_t operator()(const D3D11_TEXTURE2D_DESC &desc) const noexcept {
			std::string temp =
				to_string(desc.Format)
				.append(to_string(desc.ArraySize))
				.append(to_string(desc.BindFlags))
				.append(to_string(desc.CPUAccessFlags))
				.append(to_string(desc.Format))
				.append(to_string(desc.Height))
				.append(to_string(desc.MipLevels))
				.append(to_string(desc.MiscFlags))
				.append(to_string(desc.SampleDesc.Count))
				.append(to_string(desc.SampleDesc.Quality))
				.append(to_string(desc.Width))
				.append(to_string(desc.Usage));
			return hash<string>{}(temp);
		};
	};

	struct TextureDescComparator {
		bool operator()(const D3D11_TEXTURE2D_DESC &A,
						const D3D11_TEXTURE2D_DESC &B) const noexcept {
			return A.Format == B.Format
				&& A.ArraySize == B.ArraySize
				&& A.BindFlags == B.BindFlags
				&& A.CPUAccessFlags == B.CPUAccessFlags
				&& A.Format == B.Format
				&& A.Height == B.Height
				&& A.MipLevels == B.MipLevels
				&& A.MiscFlags == B.MiscFlags
				&& A.SampleDesc.Count == B.SampleDesc.Count
				&& A.SampleDesc.Quality == B.SampleDesc.Quality
				&& A.Usage == B.Usage
				&& A.Width == B.Width;
		};
	};
	std::unordered_map<D3D11_TEXTURE2D_DESC, ID3D11Texture2D *, TextureDescHasher, TextureDescComparator> m_TextureCache;
};
