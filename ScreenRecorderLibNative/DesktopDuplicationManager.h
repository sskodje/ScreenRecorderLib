#pragma once
#include "CommonTypes.h"
#include "TextureManager.h"
#include <memory>
class DesktopDuplicationManager
{
public:
	DesktopDuplicationManager();
	~DesktopDuplicationManager();
	HRESULT GetFrame(_In_ DWORD timeoutMillis, _Inout_ DUPL_FRAME_DATA *pData);
	HRESULT ReleaseFrame();
	HRESULT Initialize(_In_ ID3D11DeviceContext *pDeviceContext, _In_ ID3D11Device *pDevice, std::wstring Output);
	void GetOutputDesc(_Out_ DXGI_OUTPUT_DESC *pOutputDesc);
	IDXGIOutputDuplication *GetOutputDuplication() { return m_DeskDupl; }
	ID3D11Device *GetDevice() { return m_Device; }
	HRESULT ProcessFrame(_In_ DUPL_FRAME_DATA *pData, _Inout_ ID3D11Texture2D *pSharedSurf, INT offsetX, INT offsetY, _In_ RECT destinationRect, _In_opt_ const std::optional<RECT> &sourceRect = std::nullopt);
private:
	static const int NUMVERTICES = 6;

	// methods
	HRESULT CopyDirty(_In_ ID3D11Texture2D *pSrcSurface, _Inout_ ID3D11Texture2D *pSharedSurf, _In_reads_(dirtyCount) RECT *pDirtyBuffer, UINT dirtyCount, INT offsetX, INT offsetY, _In_ RECT desktopCoordinates, _In_ DXGI_MODE_ROTATION rotation);
	HRESULT CopyMove(_Inout_ ID3D11Texture2D *pSharedSurf, _In_reads_(moveCount) DXGI_OUTDUPL_MOVE_RECT *pMoveBuffer, UINT moveCount, INT offsetX, INT offsetY, _In_ RECT desktopCoordinates, _In_ DXGI_MODE_ROTATION rotation);
	void SetDirtyVert(_Out_writes_(NUMVERTICES) VERTEX *pVertices, _In_ RECT *pDirty, INT offsetX, INT offsetY, _In_ RECT desktopCoordinates, _In_ DXGI_MODE_ROTATION rotation, _In_ D3D11_TEXTURE2D_DESC *pFullDesc, _In_ D3D11_TEXTURE2D_DESC *pThisDesc);
	void SetMoveRect(_Out_ RECT *SrcRect, _Out_ RECT *pDestRect, _In_ DXGI_MODE_ROTATION rotation, _In_ DXGI_OUTDUPL_MOVE_RECT *pMoveRect, INT texWidth, INT texHeight);
	void CleanRefs();

	// vars
	std::unique_ptr<TextureManager> m_TextureManager;
	bool m_OutputIsOnSeparateGraphicsAdapter;
	IDXGIOutputDuplication *m_DeskDupl;
	ID3D11Texture2D *m_AcquiredDesktopImage;
	_Field_size_bytes_(m_MetaDataSize) BYTE *m_MetaDataBuffer;
	UINT m_MetaDataSize;
	std::wstring m_OutputName;
	DXGI_OUTPUT_DESC m_OutputDesc;
	ID3D11Device *m_Device;
	ID3D11DeviceContext *m_DeviceContext;
	ID3D11Texture2D *m_MoveSurf;
	ID3D11VertexShader *m_VertexShader;
	ID3D11PixelShader *m_PixelShader;
	ID3D11InputLayout *m_InputLayout;
	ID3D11RenderTargetView *m_RTV;
	ID3D11SamplerState *m_SamplerLinear;
	BYTE *m_DirtyVertexBufferAlloc;
	UINT m_DirtyVertexBufferAllocSize;
};
