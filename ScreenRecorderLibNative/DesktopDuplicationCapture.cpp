#include "DesktopDuplicationCapture.h"
#include "Cleanup.h"
#include "MouseManager.h"
#include "PixelShader.h"
#include "VertexShader.h"

using namespace std::chrono;
using namespace std;
using namespace DirectX;

DesktopDuplicationCapture::DesktopDuplicationCapture() :
	CaptureBase(),
	m_IsInitialized(false),
	m_DeskDupl(nullptr),
	m_MetaDataBuffer(nullptr),
	m_MetaDataSize(0),
	m_MoveSurf(nullptr),
	m_VertexShader(nullptr),
	m_PixelShader(nullptr),
	m_InputLayout(nullptr),
	m_RTV(nullptr),
	m_SamplerLinear(nullptr),
	m_DirtyVertexBufferAlloc(nullptr),
	m_DirtyVertexBufferAllocSize(0),
	m_OutputIsOnSeparateGraphicsAdapter(false),
	m_LastSampleUpdatedTimeStamp{ 0 },
	m_CursorOffsetX(0),
	m_CursorOffsetY(0),
	m_CursorScaleX(1.0),
	m_CursorScaleY(1.0),
	m_BitmapDataCallbackTexture(nullptr),
	m_BitmapDataCallbackTextureDesc{}
{
	RtlZeroMemory(&m_CurrentData, sizeof(m_CurrentData));
	RtlZeroMemory(&m_OutputDesc, sizeof(m_OutputDesc));
}

DesktopDuplicationCapture::~DesktopDuplicationCapture()
{
	SafeRelease(&m_DeskDupl);
	SafeRelease(&m_MoveSurf);
	SafeRelease(&m_VertexShader);
	SafeRelease(&m_PixelShader);
	SafeRelease(&m_InputLayout);
	SafeRelease(&m_SamplerLinear);
	SafeRelease(&m_RTV);
	SafeRelease(&m_CurrentData.Frame);
	SafeRelease(&m_BitmapDataCallbackTexture);

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

HRESULT DesktopDuplicationCapture::Initialize(_In_ ID3D11DeviceContext *pDeviceContext, _In_ ID3D11Device *pDevice)
{
	m_Device = pDevice;
	m_DeviceContext = pDeviceContext;

	m_Device->AddRef();
	m_DeviceContext->AddRef();

	m_MouseManager = make_unique<MouseManager>();
	HRESULT hr = m_MouseManager->Initialize(pDeviceContext, pDevice, std::make_shared<MOUSE_OPTIONS>());
	RETURN_ON_BAD_HR(hr);
	m_TextureManager = make_unique<TextureManager>();
	RETURN_ON_BAD_HR(hr = m_TextureManager->Initialize(pDeviceContext, pDevice));
	if (SUCCEEDED(hr)) {
		m_IsInitialized = true;
	}
	return hr;
}

HRESULT DesktopDuplicationCapture::AcquireNextFrame(_In_ DWORD timeoutMillis, _Outptr_opt_ ID3D11Texture2D **ppFrame)
{
	HRESULT hr = GetNextFrame(timeoutMillis, &m_CurrentData);

	if (SUCCEEDED(hr) && ppFrame) {
		D3D11_TEXTURE2D_DESC desc;
		m_CurrentData.Frame->GetDesc(&desc);
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		desc.MiscFlags = 0;
		ID3D11Texture2D *pFrame = nullptr;
		hr = m_Device->CreateTexture2D(&desc, nullptr, &pFrame);
		if (SUCCEEDED(hr)) {
			m_DeviceContext->CopyResource(pFrame, m_CurrentData.Frame);
			QueryPerformanceCounter(&m_LastGrabTimeStamp);
		}
		*ppFrame = pFrame;
	}

	return hr;
}

HRESULT DesktopDuplicationCapture::WriteNextFrameToSharedSurface(_In_ DWORD timeoutMillis, _Inout_ ID3D11Texture2D *pSharedSurf, INT offsetX, INT offsetY, _In_ RECT destinationRect)
{
	HRESULT hr = S_OK;
	if (m_LastGrabTimeStamp.QuadPart >= m_LastSampleUpdatedTimeStamp.QuadPart) {
		hr = GetNextFrame(timeoutMillis, &m_CurrentData);
	}

	if (SUCCEEDED(hr)) {
		DXGI_MODE_ROTATION rotation = m_OutputDesc.Rotation;
		D3D11_TEXTURE2D_DESC frameDesc;
		m_CurrentData.Frame->GetDesc(&frameDesc);
		if (m_CurrentData.FrameInfo.AccumulatedFrames > 0)
		{
			TextureStretchMode stretch = m_RecordingSource->Stretch;
			MeasureExecutionTime measure(L"Duplication WriteFrameUpdatesToSurface");
			RECORDING_SOURCE *recordingSource = dynamic_cast<RECORDING_SOURCE *>(m_RecordingSource);
			if (!recordingSource) {
				LOG_ERROR("Recording source cannot be NULL");
				return E_FAIL;
			}
			if (recordingSource->SourceRect.has_value()
				&& !EqualRect(&recordingSource->SourceRect.value(), &destinationRect)
					|| (RectWidth(destinationRect) != frameDesc.Width
						|| RectHeight(destinationRect) != frameDesc.Height)) {
				CComPtr<ID3D11Texture2D> pProcessedTexture = m_CurrentData.Frame;
				D3D11_TEXTURE2D_DESC frameDesc;
				pProcessedTexture->GetDesc(&frameDesc);

				if (rotation != DXGI_MODE_ROTATION_IDENTITY && rotation != DXGI_MODE_ROTATION_UNSPECIFIED) {
					ID3D11Texture2D *pRotatedTexture = nullptr;
					RETURN_ON_BAD_HR(hr = m_TextureManager->RotateTexture(pProcessedTexture, rotation, &pRotatedTexture));
					pProcessedTexture.Attach(pRotatedTexture);
					pProcessedTexture->GetDesc(&frameDesc);
				}
				int cursorOffsetX = 0;
				int cursorOffsetY = 0;
				float cursorScaleX = 1.0;
				float cursorScaleY = 1.0;

				if (recordingSource->SourceRect.has_value()
					&& IsValidRect(recordingSource->SourceRect.value())
					&& (RectWidth(recordingSource->SourceRect.value()) != frameDesc.Width || (RectHeight(recordingSource->SourceRect.value()) != frameDesc.Height))) {
					ID3D11Texture2D *pCroppedTexture;
					RETURN_ON_BAD_HR(hr = m_TextureManager->CropTexture(pProcessedTexture, recordingSource->SourceRect.value(), &pCroppedTexture));
					if (hr == S_OK) {
						pProcessedTexture.Release();
						pProcessedTexture.Attach(pCroppedTexture);
					}
					pProcessedTexture->GetDesc(&frameDesc);
					cursorOffsetX = 0 - recordingSource->SourceRect.value().left;
					cursorOffsetY = 0 - recordingSource->SourceRect.value().top;
				}

				RECT contentRect = destinationRect;
				if (RectWidth(destinationRect) != frameDesc.Width || RectHeight(destinationRect) != frameDesc.Height) {
					ID3D11Texture2D *pResizedTexture;
					RETURN_ON_BAD_HR(hr = m_TextureManager->ResizeTexture(pProcessedTexture, SIZE{ RectWidth(destinationRect),RectHeight(destinationRect) }, stretch, &pResizedTexture, &contentRect));
					int prescaleWidth = frameDesc.Width;
					int prescaleHeight = frameDesc.Height;
					pProcessedTexture.Release();
					pProcessedTexture.Attach(pResizedTexture);
					pProcessedTexture->GetDesc(&frameDesc);
					cursorScaleX = (float)RectWidth(contentRect) / prescaleWidth;
					cursorScaleY = (float)RectHeight(contentRect) / prescaleHeight;
				}

				D3D11_TEXTURE2D_DESC processedFrameDesc;
				pProcessedTexture->GetDesc(&processedFrameDesc);

				SIZE contentOffset = GetContentOffset(m_RecordingSource->Anchor, destinationRect, contentRect);
				cursorOffsetX += static_cast<int>(round(contentOffset.cx / cursorScaleX));
				cursorOffsetY += static_cast<int>(round(contentOffset.cy / cursorScaleY));

				D3D11_BOX Box{};
				Box.back = 1;
				Box.right = MakeEven(RectWidth(contentRect));
				Box.bottom = MakeEven(RectHeight(contentRect));

				int dstX = destinationRect.left + offsetX + contentOffset.cx;
				int dstY = destinationRect.top + offsetY + contentOffset.cy;

				m_DeviceContext->CopySubresourceRegion(pSharedSurf, 0, dstX, dstY, 0, pProcessedTexture, 0, &Box);

				SendBitmapCallback(pSharedSurf, SIZE{ offsetX,offsetY }, contentOffset, destinationRect);

				m_CursorOffsetX = cursorOffsetX;
				m_CursorOffsetY = cursorOffsetY;
				m_CursorScaleX = cursorScaleX;
				m_CursorScaleY = cursorScaleY;
			}
			else
			{
				// Process dirties and moves
				if (m_CurrentData.MoveCount)
				{
					RETURN_ON_BAD_HR(hr = CopyMove(pSharedSurf, reinterpret_cast<DXGI_OUTDUPL_MOVE_RECT *>(m_CurrentData.MetaData), m_CurrentData.MoveCount, offsetX, offsetY, destinationRect, rotation));
				}
				if (m_CurrentData.DirtyCount)
				{
					RETURN_ON_BAD_HR(hr = CopyDirty(m_CurrentData.Frame, pSharedSurf, reinterpret_cast<RECT *>(m_CurrentData.MetaData + (m_CurrentData.MoveCount * sizeof(DXGI_OUTDUPL_MOVE_RECT))), m_CurrentData.DirtyCount, offsetX, offsetY, destinationRect, rotation));
				}
				SendBitmapCallback(pSharedSurf, SIZE{ offsetX,offsetY }, SIZE{ 0,0 }, destinationRect);
			}
		}
		else if (m_LastGrabTimeStamp.QuadPart > 0
			&& m_CurrentData.FrameInfo.LastMouseUpdateTime.QuadPart > m_LastGrabTimeStamp.QuadPart) {
			hr = S_OK;
		}
		else {
			hr = S_FALSE;
		}

		if (hr == S_OK) {
			QueryPerformanceCounter(&m_LastGrabTimeStamp);
		}
	}
	return hr;
}

HRESULT DesktopDuplicationCapture::SendBitmapCallback(_In_ ID3D11Texture2D *pSharedSurf, _In_ SIZE frameOffset, _In_ SIZE contentOffset, _In_ RECT destinationRect) {
	if (m_RecordingSource->IsVideoFramePreviewEnabled && m_RecordingSource->HasRegisteredCallbacks())
	{
		int width = MakeEven(RectWidth(destinationRect));
		int height = MakeEven(RectHeight(destinationRect));

		if (m_BitmapDataCallbackTextureDesc.Width != width || m_BitmapDataCallbackTextureDesc.Height != height) {
			m_CurrentData.Frame->GetDesc(&m_BitmapDataCallbackTextureDesc);
			m_BitmapDataCallbackTextureDesc.Width = width;
			m_BitmapDataCallbackTextureDesc.Height = height;
			m_BitmapDataCallbackTextureDesc.CPUAccessFlags = 0;
			m_BitmapDataCallbackTextureDesc.MiscFlags = 0;
			m_BitmapDataCallbackTextureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
			RETURN_ON_BAD_HR(m_Device->CreateTexture2D(&m_BitmapDataCallbackTextureDesc, nullptr, &m_BitmapDataCallbackTexture));
		}

		D3D11_BOX Box{};
		Box.back = 1;
		Box.right = MakeEven(width);
		Box.bottom = MakeEven(height);
		m_DeviceContext->CopySubresourceRegion(m_BitmapDataCallbackTexture, 0, frameOffset.cx + contentOffset.cx, frameOffset.cy + contentOffset.cy, 0, pSharedSurf, 0, &Box);
		PTR_INFO ptrInfo;
		RtlZeroMemory(&ptrInfo, sizeof(ptrInfo));
		HRESULT hr = GetMouse(&ptrInfo, destinationRect, frameOffset.cx, frameOffset.cy);
		hr = m_MouseManager->ProcessMousePointer(m_BitmapDataCallbackTexture, &ptrInfo);
		delete[] ptrInfo.PtrShapeBuffer;
		return CaptureBase::SendBitmapCallback(m_BitmapDataCallbackTexture);
	}
	else {
		return S_FALSE;
	}
}

HRESULT DesktopDuplicationCapture::StartCapture(_In_ RECORDING_SOURCE_BASE &recordingSource)
{
	m_RecordingSource = &recordingSource;
	return InitializeDesktopDuplication(recordingSource.SourcePath);
}

HRESULT DesktopDuplicationCapture::GetNativeSize(_In_ RECORDING_SOURCE_BASE &recordingSource, _Out_ SIZE *size)
{
	RtlZeroMemory(size, sizeof(size));
	CComPtr<IDXGIOutput> output;
	HRESULT hr = GetOutputForDeviceName(recordingSource.SourcePath, &output);
	if (output) {
		DXGI_OUTPUT_DESC desc;
		output->GetDesc(&desc);
		*size = SIZE{ RectWidth(desc.DesktopCoordinates),RectHeight(desc.DesktopCoordinates) };
	}
	return hr;
}
HRESULT DesktopDuplicationCapture::GetMouse(_Inout_ PTR_INFO *pPtrInfo, _In_ RECT frameCoordinates, _In_ int offsetX, _In_ int offsetY)
{
	HRESULT hr = m_MouseManager->GetMouse(pPtrInfo, true, &m_CurrentData.FrameInfo, frameCoordinates, m_DeskDupl, offsetX, offsetY);
	pPtrInfo->Scale = SIZE_F{ m_CursorScaleX, m_CursorScaleY };
	pPtrInfo->Offset = POINT{ m_CursorOffsetX, m_CursorOffsetY };
	return hr;
}

HRESULT DesktopDuplicationCapture::InitializeDesktopDuplication(std::wstring deviceName)
{
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
		{ "SV_POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 }
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
	CComPtr<IDXGIDevice> DxgiDevice = nullptr;
	hr = m_Device->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void **>(&DxgiDevice));
	if (FAILED(hr))
	{
		_com_error err(hr);
		LOG_ERROR(L"Failed to QI for DXGI Device: %ls", err.ErrorMessage());
		return hr;
	}

	// Get output
	CComPtr<IDXGIOutput> DxgiOutput = nullptr;
	hr = GetOutputForDeviceName(deviceName, &DxgiOutput);
	if (FAILED(hr))
	{
		_com_error err(hr);
		LOG_ERROR(L"Failed to get output %ls in DesktopDuplicationCapture: %ls", deviceName.c_str(), err.ErrorMessage());
		return hr;
	}
	DxgiOutput->GetDesc(&m_OutputDesc);

	// QI for Output 1
	CComPtr<IDXGIOutput1> DxgiOutput1 = nullptr;
	hr = DxgiOutput->QueryInterface(__uuidof(DxgiOutput1), reinterpret_cast<void **>(&DxgiOutput1));
	if (FAILED(hr))
	{
		_com_error err(hr);
		LOG_ERROR(L"Failed to QI for DxgiOutput1 in DesktopDuplicationCapture: %ls", err.ErrorMessage());
		return hr;
	}

	// Create desktop duplication
	CComPtr<ID3D11Device> duplicationDevice = m_Device;

	CComPtr<IDXGIAdapter> pSharedAdapter;
	GetAdapterForDevice(m_Device, &pSharedAdapter);
	DXGI_ADAPTER_DESC sharedDesc;
	pSharedAdapter->GetDesc(&sharedDesc);

	CComPtr<IDXGIAdapter> outputAdapter = nullptr;
	hr = GetAdapterForDeviceName(deviceName, &outputAdapter);
	DXGI_ADAPTER_DESC outputDeviceDesc;
	outputAdapter->GetDesc(&outputDeviceDesc);

	if (outputDeviceDesc.AdapterLuid.LowPart != sharedDesc.AdapterLuid.LowPart) {
		DX_RESOURCES outputAdapterResources{};
		hr = InitializeDx(outputAdapter, &outputAdapterResources);
		duplicationDevice = outputAdapterResources.Device;
		m_OutputIsOnSeparateGraphicsAdapter = true;
		CleanDx(&outputAdapterResources);
	}

	hr = DxgiOutput1->DuplicateOutput(duplicationDevice, &m_DeskDupl);
	if (FAILED(hr))
	{
		_com_error err(hr);
		LOG_ERROR(L"Failed to get duplicate output in DesktopDuplicationCapture: %ls", err.ErrorMessage());
		return hr;
	}

	return hr;
}

//
// Get next frame and write it into Data
//
HRESULT DesktopDuplicationCapture::GetNextFrame(_In_ DWORD timeoutMillis, _Inout_ DUPL_FRAME_DATA *pData)
{
	IDXGIResource *DesktopResource = nullptr;
	DXGI_OUTDUPL_FRAME_INFO FrameInfo;

	// If holding old frame, release it
	if (pData->Frame) {
		pData->Frame->Release();
		pData->Frame = nullptr;
	}
	HRESULT hr = m_DeskDupl->ReleaseFrame();
	//DXGI_ERROR_INVALID_CALL means the frame is already released, so just ignore it. Return on other errors.
	if (FAILED(hr) && hr != DXGI_ERROR_INVALID_CALL)
	{
		return hr;
	}

	// Get new frame
	hr = m_DeskDupl->AcquireNextFrame(timeoutMillis, &FrameInfo, &DesktopResource);
	if (FAILED(hr))
	{
		return hr;
	}

	ID3D11Texture2D *pAcquiredDesktopImage;
	// QI for IDXGIResource
	hr = DesktopResource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void **>(&pAcquiredDesktopImage));
	DesktopResource->Release();
	DesktopResource = nullptr;
	if (FAILED(hr))
	{
		LOG_ERROR(L"Failed to QI for ID3D11Texture2D from acquired IDXGIResource in DesktopDuplicationCapture");
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
				pData->MoveCount = 0;
				pData->DirtyCount = 0;
				LOG_ERROR(L"Failed to allocate memory for metadata in DesktopDuplicationCapture");
				return E_OUTOFMEMORY;
			}
			m_MetaDataSize = FrameInfo.TotalMetadataBufferSize;
		}

		UINT BufSize = FrameInfo.TotalMetadataBufferSize;

		// Get move rectangles
		hr = m_DeskDupl->GetFrameMoveRects(BufSize, reinterpret_cast<DXGI_OUTDUPL_MOVE_RECT *>(m_MetaDataBuffer), &BufSize);
		if (FAILED(hr))
		{
			pData->MoveCount = 0;
			pData->DirtyCount = 0;
			LOG_ERROR(L"Failed to get frame move rects in DesktopDuplicationCapture");
			return hr;
		}
		pData->MoveCount = BufSize / sizeof(DXGI_OUTDUPL_MOVE_RECT);

		BYTE *DirtyRects = m_MetaDataBuffer + BufSize;
		BufSize = FrameInfo.TotalMetadataBufferSize - BufSize;

		// Get dirty rectangles
		hr = m_DeskDupl->GetFrameDirtyRects(BufSize, reinterpret_cast<RECT *>(DirtyRects), &BufSize);
		if (FAILED(hr))
		{
			pData->MoveCount = 0;
			pData->DirtyCount = 0;
			LOG_ERROR(L"Failed to get frame dirty rects in DesktopDuplicationCapture");
			return hr;
		}
		pData->DirtyCount = BufSize / sizeof(RECT);
		pData->MetaData = m_MetaDataBuffer;
	}

	pData->Frame = pAcquiredDesktopImage;
	pData->FrameInfo = FrameInfo;
	QueryPerformanceCounter(&m_LastSampleUpdatedTimeStamp);
	return S_OK;
}

//
// Set appropriate source and destination rects for move rects
//
void DesktopDuplicationCapture::SetMoveRect(_Out_ RECT *pSrcRect, _Out_ RECT *pDestRect, _In_ DXGI_MODE_ROTATION rotation, _In_ DXGI_OUTDUPL_MOVE_RECT *pMoveRect, INT texWidth, INT texHeight)
{
	switch (rotation)
	{
		case DXGI_MODE_ROTATION_UNSPECIFIED:
		case DXGI_MODE_ROTATION_IDENTITY:
		{
			pSrcRect->left = pMoveRect->SourcePoint.x;
			pSrcRect->top = pMoveRect->SourcePoint.y;
			pSrcRect->right = pMoveRect->SourcePoint.x + RectWidth(pMoveRect->DestinationRect);
			pSrcRect->bottom = pMoveRect->SourcePoint.y + RectHeight(pMoveRect->DestinationRect);

			*pDestRect = pMoveRect->DestinationRect;
			break;
		}
		case DXGI_MODE_ROTATION_ROTATE90:
		{
			pSrcRect->left = texHeight - (pMoveRect->SourcePoint.y + RectHeight(pMoveRect->DestinationRect));
			pSrcRect->top = pMoveRect->SourcePoint.x;
			pSrcRect->right = texHeight - pMoveRect->SourcePoint.y;
			pSrcRect->bottom = pMoveRect->SourcePoint.x + RectWidth(pMoveRect->DestinationRect);

			pDestRect->left = texHeight - pMoveRect->DestinationRect.bottom;
			pDestRect->top = pMoveRect->DestinationRect.left;
			pDestRect->right = texHeight - pMoveRect->DestinationRect.top;
			pDestRect->bottom = pMoveRect->DestinationRect.right;
			break;
		}
		case DXGI_MODE_ROTATION_ROTATE180:
		{
			pSrcRect->left = texWidth - (pMoveRect->SourcePoint.x + RectWidth(pMoveRect->DestinationRect));
			pSrcRect->top = texHeight - (pMoveRect->SourcePoint.y + RectHeight(pMoveRect->DestinationRect));
			pSrcRect->right = texWidth - pMoveRect->SourcePoint.x;
			pSrcRect->bottom = texHeight - pMoveRect->SourcePoint.y;

			pDestRect->left = texWidth - pMoveRect->DestinationRect.right;
			pDestRect->top = texHeight - pMoveRect->DestinationRect.bottom;
			pDestRect->right = texWidth - pMoveRect->DestinationRect.left;
			pDestRect->bottom = texHeight - pMoveRect->DestinationRect.top;
			break;
		}
		case DXGI_MODE_ROTATION_ROTATE270:
		{
			pSrcRect->left = pMoveRect->SourcePoint.x;
			pSrcRect->top = texWidth - (pMoveRect->SourcePoint.x + RectWidth(pMoveRect->DestinationRect));
			pSrcRect->right = pMoveRect->SourcePoint.y + RectHeight(pMoveRect->DestinationRect);
			pSrcRect->bottom = texWidth - pMoveRect->SourcePoint.x;

			pDestRect->left = pMoveRect->DestinationRect.top;
			pDestRect->top = texWidth - pMoveRect->DestinationRect.right;
			pDestRect->right = pMoveRect->DestinationRect.bottom;
			pDestRect->bottom = texWidth - pMoveRect->DestinationRect.left;
			break;
		}
		default:
		{
			RtlZeroMemory(pDestRect, sizeof(RECT));
			RtlZeroMemory(pSrcRect, sizeof(RECT));
			break;
		}
	}
}

//
// Copy move rectangles
//
HRESULT DesktopDuplicationCapture::CopyMove(_Inout_ ID3D11Texture2D *pSharedSurf, _In_reads_(moveCount) DXGI_OUTDUPL_MOVE_RECT *pMoveBuffer, UINT moveCount, INT offsetX, INT offsetY, _In_ RECT desktopCoordinates, _In_ DXGI_MODE_ROTATION rotation)
{
	D3D11_TEXTURE2D_DESC FullDesc;
	pSharedSurf->GetDesc(&FullDesc);
	int TexWidth = RectWidth(desktopCoordinates);
	int TexHeight = RectHeight(desktopCoordinates);
	// Make new intermediate surface to copy into for moving
	if (!m_MoveSurf)
	{
		D3D11_TEXTURE2D_DESC MoveDesc;
		MoveDesc = FullDesc;
		MoveDesc.Width = RectWidth(desktopCoordinates);
		MoveDesc.Height = RectHeight(desktopCoordinates);
		MoveDesc.BindFlags = D3D11_BIND_RENDER_TARGET;
		MoveDesc.MiscFlags = 0;
		HRESULT hr = m_Device->CreateTexture2D(&MoveDesc, nullptr, &m_MoveSurf);
		if (FAILED(hr))
		{
			LOG_ERROR(L"Failed to create staging texture for move rects");
			return hr;
		}
	}

	for (UINT i = 0; i < moveCount; ++i)
	{
		RECT SrcRect;
		RECT DestRect;

		SetMoveRect(&SrcRect, &DestRect, rotation, &(pMoveBuffer[i]), TexWidth, TexHeight);

		// Copy rect out of shared surface
		D3D11_BOX Box;
		Box.left = SrcRect.left + desktopCoordinates.left + offsetX;
		Box.top = SrcRect.top + desktopCoordinates.top + offsetY;
		Box.front = 0;
		Box.right = SrcRect.right + desktopCoordinates.left + offsetX;
		Box.bottom = SrcRect.bottom + desktopCoordinates.top + offsetY;
		Box.back = 1;
		m_DeviceContext->CopySubresourceRegion(m_MoveSurf, 0, SrcRect.left, SrcRect.top, 0, pSharedSurf, 0, &Box);

		// Copy back to shared surface
		Box.left = SrcRect.left;
		Box.top = SrcRect.top;
		Box.front = 0;
		Box.right = SrcRect.right;
		Box.bottom = SrcRect.bottom;
		Box.back = 1;
		m_DeviceContext->CopySubresourceRegion(pSharedSurf, 0, DestRect.left + desktopCoordinates.left + offsetX, DestRect.top + desktopCoordinates.top + offsetY, 0, m_MoveSurf, 0, &Box);
	}

	return S_OK;
}

//
// Sets up vertices for dirty rects for rotated desktops
//
#pragma warning(push)
#pragma warning(disable:__WARNING_USING_UNINIT_VAR) // false positives in SetDirtyVert due to tool bug

void DesktopDuplicationCapture::SetDirtyVert(_Out_writes_(NUMVERTICES) VERTEX *pVertices, _In_ RECT *pDirty, INT offsetX, INT offsetY, _In_ RECT desktopCoordinates, _In_ DXGI_MODE_ROTATION rotation, _In_ D3D11_TEXTURE2D_DESC *pFullDesc, _In_ D3D11_TEXTURE2D_DESC *pThisDesc)
{
	INT CenterX = pFullDesc->Width / 2;
	INT CenterY = pFullDesc->Height / 2;

	INT Width = RectWidth(desktopCoordinates);
	INT Height = RectHeight(desktopCoordinates);

	// Rotation compensated destination rect
	RECT DestDirty = *pDirty;

	// Set appropriate coordinates compensated for rotation
	switch (rotation)
	{
		case DXGI_MODE_ROTATION_ROTATE90:
		{
			DestDirty.left = Width - pDirty->bottom;
			DestDirty.top = pDirty->left;
			DestDirty.right = Width - pDirty->top;
			DestDirty.bottom = pDirty->right;

			pVertices[0].TexCoord = XMFLOAT2(pDirty->right / static_cast<FLOAT>(pThisDesc->Width), pDirty->bottom / static_cast<FLOAT>(pThisDesc->Height));
			pVertices[1].TexCoord = XMFLOAT2(pDirty->left / static_cast<FLOAT>(pThisDesc->Width), pDirty->bottom / static_cast<FLOAT>(pThisDesc->Height));
			pVertices[2].TexCoord = XMFLOAT2(pDirty->right / static_cast<FLOAT>(pThisDesc->Width), pDirty->top / static_cast<FLOAT>(pThisDesc->Height));
			pVertices[5].TexCoord = XMFLOAT2(pDirty->left / static_cast<FLOAT>(pThisDesc->Width), pDirty->top / static_cast<FLOAT>(pThisDesc->Height));
			break;
		}
		case DXGI_MODE_ROTATION_ROTATE180:
		{
			DestDirty.left = Width - pDirty->right;
			DestDirty.top = Height - pDirty->bottom;
			DestDirty.right = Width - pDirty->left;
			DestDirty.bottom = Height - pDirty->top;

			pVertices[0].TexCoord = XMFLOAT2(pDirty->right / static_cast<FLOAT>(pThisDesc->Width), pDirty->top / static_cast<FLOAT>(pThisDesc->Height));
			pVertices[1].TexCoord = XMFLOAT2(pDirty->right / static_cast<FLOAT>(pThisDesc->Width), pDirty->bottom / static_cast<FLOAT>(pThisDesc->Height));
			pVertices[2].TexCoord = XMFLOAT2(pDirty->left / static_cast<FLOAT>(pThisDesc->Width), pDirty->top / static_cast<FLOAT>(pThisDesc->Height));
			pVertices[5].TexCoord = XMFLOAT2(pDirty->left / static_cast<FLOAT>(pThisDesc->Width), pDirty->bottom / static_cast<FLOAT>(pThisDesc->Height));
			break;
		}
		case DXGI_MODE_ROTATION_ROTATE270:
		{
			DestDirty.left = pDirty->top;
			DestDirty.top = Height - pDirty->right;
			DestDirty.right = pDirty->bottom;
			DestDirty.bottom = Height - pDirty->left;

			pVertices[0].TexCoord = XMFLOAT2(pDirty->left / static_cast<FLOAT>(pThisDesc->Width), pDirty->top / static_cast<FLOAT>(pThisDesc->Height));
			pVertices[1].TexCoord = XMFLOAT2(pDirty->right / static_cast<FLOAT>(pThisDesc->Width), pDirty->top / static_cast<FLOAT>(pThisDesc->Height));
			pVertices[2].TexCoord = XMFLOAT2(pDirty->left / static_cast<FLOAT>(pThisDesc->Width), pDirty->bottom / static_cast<FLOAT>(pThisDesc->Height));
			pVertices[5].TexCoord = XMFLOAT2(pDirty->right / static_cast<FLOAT>(pThisDesc->Width), pDirty->bottom / static_cast<FLOAT>(pThisDesc->Height));
			break;
		}
		case DXGI_MODE_ROTATION_UNSPECIFIED:
		case DXGI_MODE_ROTATION_IDENTITY:
		{
			pVertices[0].TexCoord = XMFLOAT2(pDirty->left / static_cast<FLOAT>(pThisDesc->Width), pDirty->bottom / static_cast<FLOAT>(pThisDesc->Height));
			pVertices[1].TexCoord = XMFLOAT2(pDirty->left / static_cast<FLOAT>(pThisDesc->Width), pDirty->top / static_cast<FLOAT>(pThisDesc->Height));
			pVertices[2].TexCoord = XMFLOAT2(pDirty->right / static_cast<FLOAT>(pThisDesc->Width), pDirty->bottom / static_cast<FLOAT>(pThisDesc->Height));
			pVertices[5].TexCoord = XMFLOAT2(pDirty->right / static_cast<FLOAT>(pThisDesc->Width), pDirty->top / static_cast<FLOAT>(pThisDesc->Height));
			break;
		}
		default:
			assert(false);
	}

	// Set positions
	pVertices[0].Pos = XMFLOAT3((DestDirty.left + desktopCoordinates.left + offsetX - CenterX) / static_cast<FLOAT>(CenterX),
		-1 * (DestDirty.bottom + desktopCoordinates.top + offsetY - CenterY) / static_cast<FLOAT>(CenterY),
		0.0f);
	pVertices[1].Pos = XMFLOAT3((DestDirty.left + desktopCoordinates.left + offsetX - CenterX) / static_cast<FLOAT>(CenterX),
		-1 * (DestDirty.top + desktopCoordinates.top + offsetY - CenterY) / static_cast<FLOAT>(CenterY),
		0.0f);
	pVertices[2].Pos = XMFLOAT3((DestDirty.right + desktopCoordinates.left + offsetX - CenterX) / static_cast<FLOAT>(CenterX),
		-1 * (DestDirty.bottom + desktopCoordinates.top + offsetY - CenterY) / static_cast<FLOAT>(CenterY),
		0.0f);
	pVertices[3].Pos = pVertices[2].Pos;
	pVertices[4].Pos = pVertices[1].Pos;
	pVertices[5].Pos = XMFLOAT3((DestDirty.right + desktopCoordinates.left + offsetX - CenterX) / static_cast<FLOAT>(CenterX),
		-1 * (DestDirty.top + desktopCoordinates.top + offsetY - CenterY) / static_cast<FLOAT>(CenterY),
		0.0f);

	pVertices[3].TexCoord = pVertices[2].TexCoord;
	pVertices[4].TexCoord = pVertices[1].TexCoord;
}

#pragma warning(pop) // re-enable __WARNING_USING_UNINIT_VAR

//
// Copies dirty rectangles
//
HRESULT DesktopDuplicationCapture::CopyDirty(_In_ ID3D11Texture2D *pSrcSurface, _Inout_ ID3D11Texture2D *pSharedSurf, _In_reads_(dirtyCount) RECT *pDirtyBuffer, UINT dirtyCount, INT offsetX, INT OffsetY, _In_ RECT desktopCoordinates, _In_ DXGI_MODE_ROTATION rotation)
{
	HRESULT hr;

	D3D11_TEXTURE2D_DESC FullDesc;
	pSharedSurf->GetDesc(&FullDesc);

	D3D11_TEXTURE2D_DESC ThisDesc;
	pSrcSurface->GetDesc(&ThisDesc);

	if (!m_RTV)
	{
		hr = m_Device->CreateRenderTargetView(pSharedSurf, nullptr, &m_RTV);
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
	ID3D11ShaderResourceView *ShaderResource = nullptr;

	if (m_OutputIsOnSeparateGraphicsAdapter) {
		CComPtr<ID3D11Texture2D> pTextureCopy;
		m_TextureManager->CopyTextureWithCPU(m_Device, pSrcSurface, &pTextureCopy);
		RETURN_ON_BAD_HR(hr = m_Device->CreateShaderResourceView(pTextureCopy, &ShaderDesc, &ShaderResource));
	}
	else {
		RETURN_ON_BAD_HR(hr = m_Device->CreateShaderResourceView(pSrcSurface, &ShaderDesc, &ShaderResource));
	}

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
	UINT BytesNeeded = sizeof(VERTEX) * NUMVERTICES * dirtyCount;
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
	VERTEX *DirtyVertex = reinterpret_cast<VERTEX *>(m_DirtyVertexBufferAlloc);
	for (UINT i = 0; i < dirtyCount; ++i, DirtyVertex += NUMVERTICES)
	{
		SetDirtyVert(DirtyVertex, &(pDirtyBuffer[i]), offsetX, OffsetY, desktopCoordinates, rotation, &FullDesc, &ThisDesc);
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

	ID3D11Buffer *VertBuf = nullptr;
	hr = m_Device->CreateBuffer(&BufferDesc, &InitData, &VertBuf);
	if (FAILED(hr))
	{
		LOG_ERROR(L"Failed to create vertex buffer in dirty rect processing");
		return hr;
	}
	UINT Stride = sizeof(VERTEX);
	UINT Offset = 0;
	m_DeviceContext->IASetVertexBuffers(0, 1, &VertBuf, &Stride, &Offset);

	// Save current view port so we can restore later
	D3D11_VIEWPORT VP;
	UINT numViewports = 1;
	m_DeviceContext->RSGetViewports(&numViewports, &VP);

	SetViewPort(m_DeviceContext, static_cast<float>(FullDesc.Width), static_cast<float>(FullDesc.Height));

	m_DeviceContext->Draw(NUMVERTICES * dirtyCount, 0);

	// Restore view port
	m_DeviceContext->RSSetViewports(1, &VP);

	// Clear shader resource
	ID3D11ShaderResourceView *null[] = { nullptr, nullptr };
	m_DeviceContext->PSSetShaderResources(0, 1, null);

	VertBuf->Release();
	VertBuf = nullptr;

	ShaderResource->Release();
	ShaderResource = nullptr;

	return hr;
}
