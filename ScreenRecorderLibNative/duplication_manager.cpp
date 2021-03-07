#include "duplication_manager.h"
#include "log.h"
#include "cleanup.h"
#include <suppress.h>
#include "DX.util.h"
using namespace DirectX;

#define NUMVERTICES 6

//
// Constructor sets up references / variables
//
duplication_manager::duplication_manager() :
m_DeskDupl(nullptr),
m_AcquiredDesktopImage(nullptr),
m_MetaDataBuffer(nullptr),
m_MetaDataSize(0),
m_OutputName(L""),
m_Device(nullptr),
m_DeviceContext(nullptr),
m_MoveSurf(nullptr),
m_VertexShader(nullptr),
m_PixelShader(nullptr),
m_InputLayout(nullptr),
m_RTV(nullptr),
m_SamplerLinear(nullptr),
m_DirtyVertexBufferAlloc(nullptr),
m_DirtyVertexBufferAllocSize(0)
{
	RtlZeroMemory(&m_OutputDesc, sizeof(m_OutputDesc));
}

//
// Destructor simply calls CleanRefs to destroy everything
//
duplication_manager::~duplication_manager()
{
	CleanRefs();

	if (m_MetaDataBuffer)
	{
		delete[] m_MetaDataBuffer;
		m_MetaDataBuffer = nullptr;
	}

	if (m_DirtyVertexBufferAlloc)
	{
		delete[] m_DirtyVertexBufferAlloc;
		m_DirtyVertexBufferAlloc = nullptr;
	}
}

//
// Initialize duplication interfaces
//
HRESULT duplication_manager::Initialize(_In_ DX_RESOURCES* Data, std::wstring Output)
{
	m_OutputName = Output;

	m_Device = Data->Device;
	m_DeviceContext = Data->Context;
	//m_VertexShader = Data->VertexShader;
	//m_PixelShader = Data->PixelShader;
	//m_InputLayout = Data->InputLayout;
	//m_SamplerLinear = Data->SamplerLinear;

	m_Device->AddRef();
	m_DeviceContext->AddRef();
	//m_VertexShader->AddRef();
	//m_PixelShader->AddRef();
	//m_InputLayout->AddRef();
	//m_SamplerLinear->AddRef();

	// VERTEX shader
	UINT Size = ARRAYSIZE(g_VS);
	HRESULT hr = m_Device->CreateVertexShader(g_VS, Size, nullptr, &m_VertexShader);
	if (FAILED(hr))
	{
		return hr;
	}

	// Input layout
	D3D11_INPUT_ELEMENT_DESC Layout[] =
	{
		{"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
		{"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0}
	};
	UINT NumElements = ARRAYSIZE(Layout);
	hr = m_Device->CreateInputLayout(Layout, NumElements, g_VS, Size, &m_InputLayout);
	if (FAILED(hr))
	{
		return hr;
	}
	m_DeviceContext->IASetInputLayout(m_InputLayout);

	// Pixel shader
	Size = ARRAYSIZE(g_PS);
	hr = m_Device->CreatePixelShader(g_PS, Size, nullptr, &m_PixelShader);
	if (FAILED(hr))
	{
		return hr;
	}

	// Set up sampler
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
	if (FAILED(hr))
	{
		return hr;
	}
	// Get DXGI device
	IDXGIDevice* DxgiDevice = nullptr;
	hr = m_Device->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&DxgiDevice));
	if (FAILED(hr))
	{
		_com_error err(hr);
		LOG_ERROR(L"Failed to QI for DXGI Device: %ls", err.ErrorMessage());
		return hr;
	}

	// Get output
	IDXGIOutput* DxgiOutput = nullptr;
	hr = GetOutputForDeviceName(m_OutputName, &DxgiOutput);
	if (FAILED(hr))
	{
		_com_error err(hr);
		LOG_ERROR(L"Failed to get specified output in DUPLICATIONMANAGER: %ls", err.ErrorMessage());
		return hr;
	}
	DxgiOutput->GetDesc(&m_OutputDesc);

	// QI for Output 1
	IDXGIOutput1* DxgiOutput1 = nullptr;
	hr = DxgiOutput->QueryInterface(__uuidof(DxgiOutput1), reinterpret_cast<void**>(&DxgiOutput1));
	DxgiOutput->Release();
	DxgiOutput = nullptr;
	if (FAILED(hr))
	{
		_com_error err(hr);
		LOG_ERROR(L"Failed to QI for DxgiOutput1 in DUPLICATIONMANAGER: %ls", err.ErrorMessage());
		return hr;
	}

	// Create desktop duplication
	hr = DxgiOutput1->DuplicateOutput(m_Device, &m_DeskDupl);
	DxgiOutput1->Release();
	DxgiOutput1 = nullptr;
	if (FAILED(hr))
	{
		_com_error err(hr);
		LOG_ERROR(L"Failed to get duplicate output in DUPLICATIONMANAGER: %ls",err.ErrorMessage());
		return hr;
	}

	return S_OK;
}

//
// Get next frame and write it into Data
//
HRESULT duplication_manager::GetFrame(_Out_ DUPL_FRAME_DATA * Data)
{
	IDXGIResource* DesktopResource = nullptr;
	DXGI_OUTDUPL_FRAME_INFO FrameInfo;

	// Get new frame
	HRESULT hr = m_DeskDupl->AcquireNextFrame(10, &FrameInfo, &DesktopResource);
	if (FAILED(hr))
	{
		return hr;
	}
	// If still holding old frame, destroy it
	if (m_AcquiredDesktopImage)
	{
		m_AcquiredDesktopImage->Release();
		m_AcquiredDesktopImage = nullptr;
	}

	// QI for IDXGIResource
	hr = DesktopResource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void **>(&m_AcquiredDesktopImage));
	DesktopResource->Release();
	DesktopResource = nullptr;
	if (FAILED(hr))
	{
		LOG_ERROR(L"Failed to QI for ID3D11Texture2D from acquired IDXGIResource in DUPLICATIONMANAGER");
		return hr;
	}

	// Get metadata
	if (FrameInfo.TotalMetadataBufferSize)
	{
		// Old buffer too small
		if (FrameInfo.TotalMetadataBufferSize > m_MetaDataSize)
		{
			if (m_MetaDataBuffer)
			{
				delete[] m_MetaDataBuffer;
				m_MetaDataBuffer = nullptr;
			}
			m_MetaDataBuffer = new (std::nothrow) BYTE[FrameInfo.TotalMetadataBufferSize];
			if (!m_MetaDataBuffer)
			{
				m_MetaDataSize = 0;
				Data->MoveCount = 0;
				Data->DirtyCount = 0;
				LOG_ERROR(L"Failed to allocate memory for metadata in DUPLICATIONMANAGER");
				return E_OUTOFMEMORY;
			}
			m_MetaDataSize = FrameInfo.TotalMetadataBufferSize;
		}

		UINT BufSize = FrameInfo.TotalMetadataBufferSize;

		// Get move rectangles
		hr = m_DeskDupl->GetFrameMoveRects(BufSize, reinterpret_cast<DXGI_OUTDUPL_MOVE_RECT*>(m_MetaDataBuffer), &BufSize);
		if (FAILED(hr))
		{
			Data->MoveCount = 0;
			Data->DirtyCount = 0;
			LOG_ERROR(L"Failed to get frame move rects in DUPLICATIONMANAGER");
			return hr;
		}
		Data->MoveCount = BufSize / sizeof(DXGI_OUTDUPL_MOVE_RECT);

		BYTE* DirtyRects = m_MetaDataBuffer + BufSize;
		BufSize = FrameInfo.TotalMetadataBufferSize - BufSize;

		// Get dirty rectangles
		hr = m_DeskDupl->GetFrameDirtyRects(BufSize, reinterpret_cast<RECT*>(DirtyRects), &BufSize);
		if (FAILED(hr))
		{
			Data->MoveCount = 0;
			Data->DirtyCount = 0;
			LOG_ERROR(L"Failed to get frame dirty rects in DUPLICATIONMANAGER");
			return hr;
		}
		Data->DirtyCount = BufSize / sizeof(RECT);
		Data->MetaData = m_MetaDataBuffer;
	}

	Data->Frame = m_AcquiredDesktopImage;
	Data->FrameInfo = FrameInfo;
	return S_OK;
}

//
// Release frame
//
HRESULT duplication_manager::ReleaseFrame()
{
	HRESULT hr = m_DeskDupl->ReleaseFrame();
	if (FAILED(hr))
	{
		LOG_ERROR(L"Failed to release frame in DUPLICATIONMANAGER");
		return hr;
	}

	return hr;
}

//
// Gets output desc into DescPtr
//
void duplication_manager::GetOutputDesc(_Out_ DXGI_OUTPUT_DESC * DescPtr)
{
	*DescPtr = m_OutputDesc;
}

//
// Process a given frame and its metadata
//
HRESULT duplication_manager::ProcessFrame(_In_ DUPL_FRAME_DATA* Data, _Inout_ ID3D11Texture2D* SharedSurf, INT OffsetX, INT OffsetY, _In_ DXGI_OUTPUT_DESC* DeskDesc)
{
	HRESULT hr = S_OK;

	// Process dirties and moves
	if (Data->FrameInfo.TotalMetadataBufferSize)
	{
		D3D11_TEXTURE2D_DESC Desc;
		Data->Frame->GetDesc(&Desc);

		if (Data->MoveCount)
		{
			hr = CopyMove(SharedSurf, reinterpret_cast<DXGI_OUTDUPL_MOVE_RECT*>(Data->MetaData), Data->MoveCount, OffsetX, OffsetY, DeskDesc, Desc.Width, Desc.Height);
			if (FAILED(hr))
			{
				return hr;
			}
		}

		if (Data->DirtyCount)
		{
			hr = CopyDirty(Data->Frame, SharedSurf, reinterpret_cast<RECT*>(Data->MetaData + (Data->MoveCount * sizeof(DXGI_OUTDUPL_MOVE_RECT))), Data->DirtyCount, OffsetX, OffsetY, DeskDesc);
		}
	}

	return hr;
}

//
// Set appropriate source and destination rects for move rects
//
void duplication_manager::SetMoveRect(_Out_ RECT* SrcRect, _Out_ RECT* DestRect, _In_ DXGI_OUTPUT_DESC* DeskDesc, _In_ DXGI_OUTDUPL_MOVE_RECT* MoveRect, INT TexWidth, INT TexHeight)
{
	switch (DeskDesc->Rotation)
	{
	case DXGI_MODE_ROTATION_UNSPECIFIED:
	case DXGI_MODE_ROTATION_IDENTITY:
	{
		SrcRect->left = MoveRect->SourcePoint.x;
		SrcRect->top = MoveRect->SourcePoint.y;
		SrcRect->right = MoveRect->SourcePoint.x + MoveRect->DestinationRect.right - MoveRect->DestinationRect.left;
		SrcRect->bottom = MoveRect->SourcePoint.y + MoveRect->DestinationRect.bottom - MoveRect->DestinationRect.top;

		*DestRect = MoveRect->DestinationRect;
		break;
	}
	case DXGI_MODE_ROTATION_ROTATE90:
	{
		SrcRect->left = TexHeight - (MoveRect->SourcePoint.y + MoveRect->DestinationRect.bottom - MoveRect->DestinationRect.top);
		SrcRect->top = MoveRect->SourcePoint.x;
		SrcRect->right = TexHeight - MoveRect->SourcePoint.y;
		SrcRect->bottom = MoveRect->SourcePoint.x + MoveRect->DestinationRect.right - MoveRect->DestinationRect.left;

		DestRect->left = TexHeight - MoveRect->DestinationRect.bottom;
		DestRect->top = MoveRect->DestinationRect.left;
		DestRect->right = TexHeight - MoveRect->DestinationRect.top;
		DestRect->bottom = MoveRect->DestinationRect.right;
		break;
	}
	case DXGI_MODE_ROTATION_ROTATE180:
	{
		SrcRect->left = TexWidth - (MoveRect->SourcePoint.x + MoveRect->DestinationRect.right - MoveRect->DestinationRect.left);
		SrcRect->top = TexHeight - (MoveRect->SourcePoint.y + MoveRect->DestinationRect.bottom - MoveRect->DestinationRect.top);
		SrcRect->right = TexWidth - MoveRect->SourcePoint.x;
		SrcRect->bottom = TexHeight - MoveRect->SourcePoint.y;

		DestRect->left = TexWidth - MoveRect->DestinationRect.right;
		DestRect->top = TexHeight - MoveRect->DestinationRect.bottom;
		DestRect->right = TexWidth - MoveRect->DestinationRect.left;
		DestRect->bottom = TexHeight - MoveRect->DestinationRect.top;
		break;
	}
	case DXGI_MODE_ROTATION_ROTATE270:
	{
		SrcRect->left = MoveRect->SourcePoint.x;
		SrcRect->top = TexWidth - (MoveRect->SourcePoint.x + MoveRect->DestinationRect.right - MoveRect->DestinationRect.left);
		SrcRect->right = MoveRect->SourcePoint.y + MoveRect->DestinationRect.bottom - MoveRect->DestinationRect.top;
		SrcRect->bottom = TexWidth - MoveRect->SourcePoint.x;

		DestRect->left = MoveRect->DestinationRect.top;
		DestRect->top = TexWidth - MoveRect->DestinationRect.right;
		DestRect->right = MoveRect->DestinationRect.bottom;
		DestRect->bottom = TexWidth - MoveRect->DestinationRect.left;
		break;
	}
	default:
	{
		RtlZeroMemory(DestRect, sizeof(RECT));
		RtlZeroMemory(SrcRect, sizeof(RECT));
		break;
	}
	}
}

//
// Copy move rectangles
//
HRESULT duplication_manager::CopyMove(_Inout_ ID3D11Texture2D* SharedSurf, _In_reads_(MoveCount) DXGI_OUTDUPL_MOVE_RECT* MoveBuffer, UINT MoveCount, INT OffsetX, INT OffsetY, _In_ DXGI_OUTPUT_DESC* DeskDesc, INT TexWidth, INT TexHeight)
{
	D3D11_TEXTURE2D_DESC FullDesc;
	SharedSurf->GetDesc(&FullDesc);

	// Make new intermediate surface to copy into for moving
	if (!m_MoveSurf)
	{
		D3D11_TEXTURE2D_DESC MoveDesc;
		MoveDesc = FullDesc;
		MoveDesc.Width = DeskDesc->DesktopCoordinates.right - DeskDesc->DesktopCoordinates.left;
		MoveDesc.Height = DeskDesc->DesktopCoordinates.bottom - DeskDesc->DesktopCoordinates.top;
		MoveDesc.BindFlags = D3D11_BIND_RENDER_TARGET;
		MoveDesc.MiscFlags = 0;
		HRESULT hr = m_Device->CreateTexture2D(&MoveDesc, nullptr, &m_MoveSurf);
		if (FAILED(hr))
		{
			LOG_ERROR(L"Failed to create staging texture for move rects");
			return hr;
		}
	}

	for (UINT i = 0; i < MoveCount; ++i)
	{
		RECT SrcRect;
		RECT DestRect;

		SetMoveRect(&SrcRect, &DestRect, DeskDesc, &(MoveBuffer[i]), TexWidth, TexHeight);

		// Copy rect out of shared surface
		D3D11_BOX Box;
		Box.left = SrcRect.left + DeskDesc->DesktopCoordinates.left - OffsetX;
		Box.top = SrcRect.top + DeskDesc->DesktopCoordinates.top - OffsetY;
		Box.front = 0;
		Box.right = SrcRect.right + DeskDesc->DesktopCoordinates.left - OffsetX;
		Box.bottom = SrcRect.bottom + DeskDesc->DesktopCoordinates.top - OffsetY;
		Box.back = 1;
		m_DeviceContext->CopySubresourceRegion(m_MoveSurf, 0, SrcRect.left, SrcRect.top, 0, SharedSurf, 0, &Box);

		// Copy back to shared surface
		Box.left = SrcRect.left;
		Box.top = SrcRect.top;
		Box.front = 0;
		Box.right = SrcRect.right;
		Box.bottom = SrcRect.bottom;
		Box.back = 1;
		m_DeviceContext->CopySubresourceRegion(SharedSurf, 0, DestRect.left + DeskDesc->DesktopCoordinates.left - OffsetX, DestRect.top + DeskDesc->DesktopCoordinates.top - OffsetY, 0, m_MoveSurf, 0, &Box);
	}

	return S_OK;
}

//
// Sets up vertices for dirty rects for rotated desktops
//
#pragma warning(push)
#pragma warning(disable:__WARNING_USING_UNINIT_VAR) // false positives in SetDirtyVert due to tool bug

void duplication_manager::SetDirtyVert(_Out_writes_(NUMVERTICES) VERTEX* Vertices, _In_ RECT* Dirty, INT OffsetX, INT OffsetY, _In_ DXGI_OUTPUT_DESC* DeskDesc, _In_ D3D11_TEXTURE2D_DESC* FullDesc, _In_ D3D11_TEXTURE2D_DESC* ThisDesc)
{
	INT CenterX = FullDesc->Width / 2;
	INT CenterY = FullDesc->Height / 2;

	INT Width = DeskDesc->DesktopCoordinates.right - DeskDesc->DesktopCoordinates.left;
	INT Height = DeskDesc->DesktopCoordinates.bottom - DeskDesc->DesktopCoordinates.top;

	// Rotation compensated destination rect
	RECT DestDirty = *Dirty;

	// Set appropriate coordinates compensated for rotation
	switch (DeskDesc->Rotation)
	{
	case DXGI_MODE_ROTATION_ROTATE90:
	{
		DestDirty.left = Width - Dirty->bottom;
		DestDirty.top = Dirty->left;
		DestDirty.right = Width - Dirty->top;
		DestDirty.bottom = Dirty->right;

		Vertices[0].TexCoord = XMFLOAT2(Dirty->right / static_cast<FLOAT>(ThisDesc->Width), Dirty->bottom / static_cast<FLOAT>(ThisDesc->Height));
		Vertices[1].TexCoord = XMFLOAT2(Dirty->left / static_cast<FLOAT>(ThisDesc->Width), Dirty->bottom / static_cast<FLOAT>(ThisDesc->Height));
		Vertices[2].TexCoord = XMFLOAT2(Dirty->right / static_cast<FLOAT>(ThisDesc->Width), Dirty->top / static_cast<FLOAT>(ThisDesc->Height));
		Vertices[5].TexCoord = XMFLOAT2(Dirty->left / static_cast<FLOAT>(ThisDesc->Width), Dirty->top / static_cast<FLOAT>(ThisDesc->Height));
		break;
	}
	case DXGI_MODE_ROTATION_ROTATE180:
	{
		DestDirty.left = Width - Dirty->right;
		DestDirty.top = Height - Dirty->bottom;
		DestDirty.right = Width - Dirty->left;
		DestDirty.bottom = Height - Dirty->top;

		Vertices[0].TexCoord = XMFLOAT2(Dirty->right / static_cast<FLOAT>(ThisDesc->Width), Dirty->top / static_cast<FLOAT>(ThisDesc->Height));
		Vertices[1].TexCoord = XMFLOAT2(Dirty->right / static_cast<FLOAT>(ThisDesc->Width), Dirty->bottom / static_cast<FLOAT>(ThisDesc->Height));
		Vertices[2].TexCoord = XMFLOAT2(Dirty->left / static_cast<FLOAT>(ThisDesc->Width), Dirty->top / static_cast<FLOAT>(ThisDesc->Height));
		Vertices[5].TexCoord = XMFLOAT2(Dirty->left / static_cast<FLOAT>(ThisDesc->Width), Dirty->bottom / static_cast<FLOAT>(ThisDesc->Height));
		break;
	}
	case DXGI_MODE_ROTATION_ROTATE270:
	{
		DestDirty.left = Dirty->top;
		DestDirty.top = Height - Dirty->right;
		DestDirty.right = Dirty->bottom;
		DestDirty.bottom = Height - Dirty->left;

		Vertices[0].TexCoord = XMFLOAT2(Dirty->left / static_cast<FLOAT>(ThisDesc->Width), Dirty->top / static_cast<FLOAT>(ThisDesc->Height));
		Vertices[1].TexCoord = XMFLOAT2(Dirty->right / static_cast<FLOAT>(ThisDesc->Width), Dirty->top / static_cast<FLOAT>(ThisDesc->Height));
		Vertices[2].TexCoord = XMFLOAT2(Dirty->left / static_cast<FLOAT>(ThisDesc->Width), Dirty->bottom / static_cast<FLOAT>(ThisDesc->Height));
		Vertices[5].TexCoord = XMFLOAT2(Dirty->right / static_cast<FLOAT>(ThisDesc->Width), Dirty->bottom / static_cast<FLOAT>(ThisDesc->Height));
		break;
	}
	case DXGI_MODE_ROTATION_UNSPECIFIED:
	case DXGI_MODE_ROTATION_IDENTITY:
	{
		Vertices[0].TexCoord = XMFLOAT2(Dirty->left / static_cast<FLOAT>(ThisDesc->Width), Dirty->bottom / static_cast<FLOAT>(ThisDesc->Height));
		Vertices[1].TexCoord = XMFLOAT2(Dirty->left / static_cast<FLOAT>(ThisDesc->Width), Dirty->top / static_cast<FLOAT>(ThisDesc->Height));
		Vertices[2].TexCoord = XMFLOAT2(Dirty->right / static_cast<FLOAT>(ThisDesc->Width), Dirty->bottom / static_cast<FLOAT>(ThisDesc->Height));
		Vertices[5].TexCoord = XMFLOAT2(Dirty->right / static_cast<FLOAT>(ThisDesc->Width), Dirty->top / static_cast<FLOAT>(ThisDesc->Height));
		break;
	}
	default:
		assert(false);
	}

	// Set positions
	Vertices[0].Pos = XMFLOAT3((DestDirty.left + DeskDesc->DesktopCoordinates.left - OffsetX - CenterX) / static_cast<FLOAT>(CenterX),
		-1 * (DestDirty.bottom + DeskDesc->DesktopCoordinates.top - OffsetY - CenterY) / static_cast<FLOAT>(CenterY),
		0.0f);
	Vertices[1].Pos = XMFLOAT3((DestDirty.left + DeskDesc->DesktopCoordinates.left - OffsetX - CenterX) / static_cast<FLOAT>(CenterX),
		-1 * (DestDirty.top + DeskDesc->DesktopCoordinates.top - OffsetY - CenterY) / static_cast<FLOAT>(CenterY),
		0.0f);
	Vertices[2].Pos = XMFLOAT3((DestDirty.right + DeskDesc->DesktopCoordinates.left - OffsetX - CenterX) / static_cast<FLOAT>(CenterX),
		-1 * (DestDirty.bottom + DeskDesc->DesktopCoordinates.top - OffsetY - CenterY) / static_cast<FLOAT>(CenterY),
		0.0f);
	Vertices[3].Pos = Vertices[2].Pos;
	Vertices[4].Pos = Vertices[1].Pos;
	Vertices[5].Pos = XMFLOAT3((DestDirty.right + DeskDesc->DesktopCoordinates.left - OffsetX - CenterX) / static_cast<FLOAT>(CenterX),
		-1 * (DestDirty.top + DeskDesc->DesktopCoordinates.top - OffsetY - CenterY) / static_cast<FLOAT>(CenterY),
		0.0f);

	Vertices[3].TexCoord = Vertices[2].TexCoord;
	Vertices[4].TexCoord = Vertices[1].TexCoord;
}

#pragma warning(pop) // re-enable __WARNING_USING_UNINIT_VAR

//
// Copies dirty rectangles
//
HRESULT duplication_manager::CopyDirty(_In_ ID3D11Texture2D* SrcSurface, _Inout_ ID3D11Texture2D* SharedSurf, _In_reads_(DirtyCount) RECT* DirtyBuffer, UINT DirtyCount, INT OffsetX, INT OffsetY, _In_ DXGI_OUTPUT_DESC* DeskDesc)
{
	HRESULT hr;

	D3D11_TEXTURE2D_DESC FullDesc;
	SharedSurf->GetDesc(&FullDesc);

	D3D11_TEXTURE2D_DESC ThisDesc;
	SrcSurface->GetDesc(&ThisDesc);

	if (!m_RTV)
	{
		hr = m_Device->CreateRenderTargetView(SharedSurf, nullptr, &m_RTV);
		if (FAILED(hr))
		{
			LOG_ERROR(L"Failed to create render target view for dirty rects");
			return hr;
		}
	}

	D3D11_SHADER_RESOURCE_VIEW_DESC ShaderDesc;
	ShaderDesc.Format = ThisDesc.Format;
	ShaderDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	ShaderDesc.Texture2D.MostDetailedMip = ThisDesc.MipLevels - 1;
	ShaderDesc.Texture2D.MipLevels = ThisDesc.MipLevels;

	// Create new shader resource view
	ID3D11ShaderResourceView* ShaderResource = nullptr;
	hr = m_Device->CreateShaderResourceView(SrcSurface, &ShaderDesc, &ShaderResource);
	if (FAILED(hr))
	{
		LOG_ERROR(L"Failed to create shader resource view for dirty rects");
		return hr;
	}

	FLOAT BlendFactor[4] = { 0.f, 0.f, 0.f, 0.f };
	m_DeviceContext->OMSetBlendState(nullptr, BlendFactor, 0xFFFFFFFF);
	m_DeviceContext->OMSetRenderTargets(1, &m_RTV, nullptr);
	m_DeviceContext->VSSetShader(m_VertexShader, nullptr, 0);
	m_DeviceContext->PSSetShader(m_PixelShader, nullptr, 0);
	m_DeviceContext->PSSetShaderResources(0, 1, &ShaderResource);
	m_DeviceContext->PSSetSamplers(0, 1, &m_SamplerLinear);
	m_DeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// Create space for vertices for the dirty rects if the current space isn't large enough
	UINT BytesNeeded = sizeof(VERTEX) * NUMVERTICES * DirtyCount;
	if (BytesNeeded > m_DirtyVertexBufferAllocSize)
	{
		if (m_DirtyVertexBufferAlloc)
		{
			delete[] m_DirtyVertexBufferAlloc;
		}

		m_DirtyVertexBufferAlloc = new (std::nothrow) BYTE[BytesNeeded];
		if (!m_DirtyVertexBufferAlloc)
		{
			m_DirtyVertexBufferAllocSize = 0;
			LOG_ERROR(L"Failed to allocate memory for dirty vertex buffer.");
			return E_OUTOFMEMORY;
		}

		m_DirtyVertexBufferAllocSize = BytesNeeded;
	}

	// Fill them in
	VERTEX* DirtyVertex = reinterpret_cast<VERTEX*>(m_DirtyVertexBufferAlloc);
	for (UINT i = 0; i < DirtyCount; ++i, DirtyVertex += NUMVERTICES)
	{
		SetDirtyVert(DirtyVertex, &(DirtyBuffer[i]), OffsetX, OffsetY, DeskDesc, &FullDesc, &ThisDesc);
	}

	// Create vertex buffer
	D3D11_BUFFER_DESC BufferDesc;
	RtlZeroMemory(&BufferDesc, sizeof(BufferDesc));
	BufferDesc.Usage = D3D11_USAGE_DEFAULT;
	BufferDesc.ByteWidth = BytesNeeded;
	BufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	BufferDesc.CPUAccessFlags = 0;
	D3D11_SUBRESOURCE_DATA InitData;
	RtlZeroMemory(&InitData, sizeof(InitData));
	InitData.pSysMem = m_DirtyVertexBufferAlloc;

	ID3D11Buffer* VertBuf = nullptr;
	hr = m_Device->CreateBuffer(&BufferDesc, &InitData, &VertBuf);
	if (FAILED(hr))
	{
		LOG_ERROR(L"Failed to create vertex buffer in dirty rect processing");
		return hr;
	}
	UINT Stride = sizeof(VERTEX);
	UINT Offset = 0;
	m_DeviceContext->IASetVertexBuffers(0, 1, &VertBuf, &Stride, &Offset);

	D3D11_VIEWPORT VP;
	VP.Width = static_cast<FLOAT>(FullDesc.Width);
	VP.Height = static_cast<FLOAT>(FullDesc.Height);
	VP.MinDepth = 0.0f;
	VP.MaxDepth = 1.0f;
	VP.TopLeftX = 0.0f;
	VP.TopLeftY = 0.0f;
	m_DeviceContext->RSSetViewports(1, &VP);

	m_DeviceContext->Draw(NUMVERTICES * DirtyCount, 0);

	VertBuf->Release();
	VertBuf = nullptr;

	ShaderResource->Release();
	ShaderResource = nullptr;

	return hr;
}


//
// Clean all references
//
void duplication_manager::CleanRefs()
{
	if (m_DeskDupl)
	{
		m_DeskDupl->Release();
		m_DeskDupl = nullptr;
	}

	if (m_AcquiredDesktopImage)
	{
		m_AcquiredDesktopImage->Release();
		m_AcquiredDesktopImage = nullptr;
	}

	if (m_DeviceContext)
	{
		m_DeviceContext->Release();
		m_DeviceContext = nullptr;
	}

	if (m_Device)
	{
		m_Device->Release();
		m_Device = nullptr;
	}

	if (m_MoveSurf)
	{
		m_MoveSurf->Release();
		m_MoveSurf = nullptr;
	}

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

	if (m_RTV)
	{
		m_RTV->Release();
		m_RTV = nullptr;
	}
}

