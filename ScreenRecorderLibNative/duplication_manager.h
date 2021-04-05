#pragma once
#include "common_types.h"

class duplication_manager
{
public:
	duplication_manager();
	~duplication_manager();
	HRESULT GetFrame(_Out_ DUPL_FRAME_DATA * Data);
	HRESULT ReleaseFrame();
	HRESULT Initialize(_In_ DX_RESOURCES* Data, std::wstring Output);
	void GetOutputDesc(_Out_ DXGI_OUTPUT_DESC * DescPtr);
	IDXGIOutputDuplication* GetOutputDuplication() { return m_DeskDupl; }
	ID3D11Device* GetDevice() { return m_Device; }
	HRESULT ProcessFrame(_In_ DUPL_FRAME_DATA* Data, _Inout_ ID3D11Texture2D* SharedSurf, INT OffsetX, INT OffsetY, _In_ DXGI_OUTPUT_DESC* DeskDesc);
private:
	static const int NUMVERTICES = 6;

	// methods
	HRESULT CopyDirty(_In_ ID3D11Texture2D* SrcSurface, _Inout_ ID3D11Texture2D* SharedSurf, _In_reads_(DirtyCount) RECT* DirtyBuffer, UINT DirtyCount, INT OffsetX, INT OffsetY, _In_ DXGI_OUTPUT_DESC* DeskDesc);
	HRESULT CopyMove(_Inout_ ID3D11Texture2D* SharedSurf, _In_reads_(MoveCount) DXGI_OUTDUPL_MOVE_RECT* MoveBuffer, UINT MoveCount, INT OffsetX, INT OffsetY, _In_ DXGI_OUTPUT_DESC* DeskDesc, INT TexWidth, INT TexHeight);
	void SetDirtyVert(_Out_writes_(NUMVERTICES) VERTEX* Vertices, _In_ RECT* Dirty, INT OffsetX, INT OffsetY, _In_ DXGI_OUTPUT_DESC* DeskDesc, _In_ D3D11_TEXTURE2D_DESC* FullDesc, _In_ D3D11_TEXTURE2D_DESC* ThisDesc);
	void SetMoveRect(_Out_ RECT* SrcRect, _Out_ RECT* DestRect, _In_ DXGI_OUTPUT_DESC* DeskDesc, _In_ DXGI_OUTDUPL_MOVE_RECT* MoveRect, INT TexWidth, INT TexHeight);
	void CleanRefs();

	// vars
	IDXGIOutputDuplication* m_DeskDupl;
	ID3D11Texture2D* m_AcquiredDesktopImage;
	_Field_size_bytes_(m_MetaDataSize) BYTE* m_MetaDataBuffer;
	UINT m_MetaDataSize;
	std::wstring m_OutputName;
	DXGI_OUTPUT_DESC m_OutputDesc;
	ID3D11Device* m_Device;
	ID3D11DeviceContext* m_DeviceContext;
	ID3D11Texture2D* m_MoveSurf;
	ID3D11VertexShader* m_VertexShader;
	ID3D11PixelShader* m_PixelShader;
	ID3D11InputLayout* m_InputLayout;
	ID3D11RenderTargetView* m_RTV;
	ID3D11SamplerState* m_SamplerLinear;
	BYTE* m_DirtyVertexBufferAlloc;
	UINT m_DirtyVertexBufferAllocSize;
};
