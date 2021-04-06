#include "device_capture.h"
#include "cleanup.h"
#include "video_reader.h"
#include "gif_reader.h"
#include <chrono>

using namespace DirectX;
using namespace std::chrono;

DWORD WINAPI OverlayProc(_In_ void *Param);
HRESULT ReadMedia(reader_base &reader, OVERLAY_THREAD_DATA *pData);
HRESULT ReadImage(OVERLAY_THREAD_DATA *pData);

capture_base::capture_base() :
	m_Device(nullptr),
	m_DeviceContext(nullptr),
	m_TerminateThreadsEvent(nullptr),
	m_LastAcquiredFrameTimeStamp{},
	m_OutputRect{},
	m_SharedSurf(nullptr),
	m_KeyMutex(nullptr),
	m_OverlayThreadCount(0),
	m_OverlayThreadHandles(nullptr),
	m_OverlayThreadData(nullptr),
	m_CaptureThreadCount(0),
	m_CaptureThreadHandles(nullptr),
	m_CaptureThreadData(nullptr),
	m_SamplerLinear(nullptr),
	m_BlendState(nullptr),
	m_VertexShader(nullptr),
	m_PixelShader(nullptr),
	m_InputLayout(nullptr)
{
	RtlZeroMemory(&m_PtrInfo, sizeof(m_PtrInfo));
	// Event to tell spawned threads to quit
	m_TerminateThreadsEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
}

capture_base::~capture_base()
{
	Clean();
}

//
// Initialize shaders for drawing to screen
//
HRESULT capture_base::Initialize(_In_ ID3D11DeviceContext *pDeviceContext, _In_ ID3D11Device *pDevice)
{
	CleanDX();
	m_Device = pDevice;
	m_DeviceContext = pDeviceContext;

	HRESULT hr;

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
	hr = pDevice->CreateSamplerState(&SampDesc, &m_SamplerLinear);
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

	UINT Size = ARRAYSIZE(g_VS);
	hr = pDevice->CreateVertexShader(g_VS, Size, nullptr, &m_VertexShader);
	if (FAILED(hr))
	{
		_com_error err(hr);
		LOG_ERROR(L"Failed to create vertex shader: %lls", err.ErrorMessage());
		return hr;
	}

	D3D11_INPUT_ELEMENT_DESC Layout[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 }
	};
	UINT NumElements = ARRAYSIZE(Layout);
	hr = pDevice->CreateInputLayout(Layout, NumElements, g_VS, Size, &m_InputLayout);
	if (FAILED(hr))
	{
		_com_error err(hr);
		LOG_ERROR(L"Failed to create input layout: %lls", err.ErrorMessage());
		return hr;
	}
	pDeviceContext->IASetInputLayout(m_InputLayout);

	Size = ARRAYSIZE(g_PS);
	hr = pDevice->CreatePixelShader(g_PS, Size, nullptr, &m_PixelShader);
	if (FAILED(hr))
	{
		_com_error err(hr);
		LOG_ERROR(L"Failed to create pixel shader: %lls", err.ErrorMessage());
	}
	return hr;
}

//
// Start up threads for DDA
//
HRESULT capture_base::StartCapture(_In_ std::vector<RECORDING_SOURCE> sources, _In_ std::vector<RECORDING_OVERLAY> overlays, _In_  HANDLE hUnexpectedErrorEvent, _In_  HANDLE hExpectedErrorEvent)
{
	ResetEvent(m_TerminateThreadsEvent);

	HRESULT hr;
	std::vector<RECORDING_SOURCE_DATA *> CreatedOutputs{};
	RETURN_ON_BAD_HR(hr = CreateSharedSurf(sources, &CreatedOutputs, &m_OutputRect));
	m_CaptureThreadCount = (UINT)(CreatedOutputs.size());
	m_CaptureThreadHandles = new (std::nothrow) HANDLE[m_CaptureThreadCount]{};
	m_CaptureThreadData = new (std::nothrow) CAPTURE_THREAD_DATA[m_CaptureThreadCount]{};
	if (!m_CaptureThreadHandles || !m_CaptureThreadData)
	{
		return E_OUTOFMEMORY;
	}

	HANDLE sharedHandle = GetSharedHandle();
	// Create appropriate # of threads for duplication

	for (UINT i = 0; i < m_CaptureThreadCount; i++)
	{
		RECORDING_SOURCE_DATA *data = CreatedOutputs.at(i);
		m_CaptureThreadData[i].UnexpectedErrorEvent = hUnexpectedErrorEvent;
		m_CaptureThreadData[i].ExpectedErrorEvent = hExpectedErrorEvent;
		m_CaptureThreadData[i].TerminateThreadsEvent = m_TerminateThreadsEvent;
		m_CaptureThreadData[i].TexSharedHandle = sharedHandle;
		m_CaptureThreadData[i].PtrInfo = &m_PtrInfo;

		m_CaptureThreadData[i].RecordingSource = data;

		RtlZeroMemory(&m_CaptureThreadData[i].RecordingSource->DxRes, sizeof(DX_RESOURCES));
		RETURN_ON_BAD_HR(hr = InitializeDx(&m_CaptureThreadData[i].RecordingSource->DxRes));

		DWORD ThreadId;
		m_CaptureThreadHandles[i] = CreateThread(nullptr, 0, GetCaptureThreadProc(), &m_CaptureThreadData[i], 0, &ThreadId);
		if (m_CaptureThreadHandles[i] == nullptr)
		{
			return E_FAIL;
		}
	}

	m_OverlayThreadCount = (UINT)(overlays.size());
	m_OverlayThreadHandles = new (std::nothrow) HANDLE[m_OverlayThreadCount]{};
	m_OverlayThreadData = new (std::nothrow) OVERLAY_THREAD_DATA[m_OverlayThreadCount]{};
	if (!m_OverlayThreadHandles || !m_OverlayThreadData)
	{
		return E_OUTOFMEMORY;
	}
	for (UINT i = 0; i < m_OverlayThreadCount; i++)
	{
		auto overlay = overlays.at(i);
		m_OverlayThreadData[i].UnexpectedErrorEvent = hUnexpectedErrorEvent;
		m_OverlayThreadData[i].ExpectedErrorEvent = hExpectedErrorEvent;
		m_OverlayThreadData[i].TerminateThreadsEvent = m_TerminateThreadsEvent;
		m_OverlayThreadData[i].TexSharedHandle = sharedHandle;
		m_OverlayThreadData[i].RecordingOverlay = new RECORDING_OVERLAY_DATA(overlay);
		m_OverlayThreadData[i].RecordingOverlay->FrameInfo = new FRAME_INFO();
		RtlZeroMemory(&m_OverlayThreadData[i].RecordingOverlay->DxRes, sizeof(DX_RESOURCES));
		HRESULT hr = InitializeDx(&m_OverlayThreadData[i].RecordingOverlay->DxRes);
		if (FAILED(hr))
		{
			return hr;
		}

		DWORD ThreadId;
		m_OverlayThreadHandles[i] = CreateThread(nullptr, 0, OverlayProc, &m_OverlayThreadData[i], 0, &ThreadId);
		if (m_OverlayThreadHandles[i] == nullptr)
		{
			return E_FAIL;
		}
	}
	m_IsCapturing = true;
	return hr;
}

HRESULT capture_base::StopCapture()
{
	if (!SetEvent(m_TerminateThreadsEvent)) {
		LOG_ERROR("Could not terminate capture thread");
		return E_FAIL;
	}
	WaitForThreadTermination();
	m_IsCapturing = false;
	return S_OK;
}

HRESULT capture_base::AcquireNextFrame(_In_  DWORD timeoutMillis, _Inout_ CAPTURED_FRAME *pFrame)
{
	HRESULT hr;
	// Try to acquire keyed mutex in order to access shared surface
	{
		MeasureExecutionTime measure(L"AcquireNextFrame wait for sync");
		hr = m_KeyMutex->AcquireSync(1, timeoutMillis);
	}
	if (hr == static_cast<HRESULT>(WAIT_TIMEOUT))
	{
		return DXGI_ERROR_WAIT_TIMEOUT;
	}
	else if (FAILED(hr))
	{
		return hr;
	}
	ID3D11Texture2D *pDesktopFrame = nullptr;
	{
		MeasureExecutionTime measure(L"AcquireNextFrame lock");

		ReleaseKeyedMutexOnExit releaseMutex(m_KeyMutex, 0);

		bool haveNewFrameData = IsUpdatedFramesAvailable() && IsInitialFrameWriteComplete();
		if (!haveNewFrameData) {
			LOG_TRACE("No new frames available");
			return DXGI_ERROR_WAIT_TIMEOUT;
		}
		D3D11_TEXTURE2D_DESC desc;
		m_SharedSurf->GetDesc(&desc);
		desc.MiscFlags = 0;
		RETURN_ON_BAD_HR(hr = m_Device->CreateTexture2D(&desc, nullptr, &pDesktopFrame));
		m_DeviceContext->CopyResource(pDesktopFrame, m_SharedSurf);
	}
	UINT updatedFrameCount = GetUpdatedFrameCount(true);
	int updatedOverlaysCount = 0;
	{
		MeasureExecutionTime measure(L"ProcessOverlays");
		RETURN_ON_BAD_HR(hr = ProcessOverlays(pDesktopFrame, &updatedOverlaysCount));
		measure.SetName(string_format(L"Updated %d capture sources and %d overlays", updatedFrameCount, updatedOverlaysCount));
	}
	SIZE contentSize;
	if (IsSingleWindowCapture()) {
		contentSize = GetContentSize();
	}
	else {
		contentSize = SIZE{ RectWidth(m_OutputRect),RectHeight(m_OutputRect) };
	}


	if (updatedFrameCount > 0 || updatedOverlaysCount > 0) {
		QueryPerformanceCounter(&m_LastAcquiredFrameTimeStamp);
	}
	pFrame->Frame = pDesktopFrame;
	pFrame->PtrInfo = &m_PtrInfo;
	pFrame->FrameUpdateCount = updatedFrameCount;
	pFrame->Timestamp = m_LastAcquiredFrameTimeStamp;
	pFrame->OverlayUpdateCount = updatedOverlaysCount;
	pFrame->ContentSize = contentSize;
	return hr;
}

//
// Clean up resources
//
void capture_base::Clean()
{
	if (m_SharedSurf) {
		m_SharedSurf->Release();
		m_SharedSurf = nullptr;
	}
	if (m_KeyMutex)
	{
		m_KeyMutex->Release();
		m_KeyMutex = nullptr;
	}
	if (m_PtrInfo.PtrShapeBuffer)
	{
		delete[] m_PtrInfo.PtrShapeBuffer;
		m_PtrInfo.PtrShapeBuffer = nullptr;
	}
	RtlZeroMemory(&m_PtrInfo, sizeof(m_PtrInfo));

	if (m_OverlayThreadHandles) {
		for (UINT i = 0; i < m_OverlayThreadCount; ++i)
		{
			if (m_OverlayThreadHandles[i])
			{
				CloseHandle(m_OverlayThreadHandles[i]);
			}
		}
		delete[] m_OverlayThreadHandles;
		m_OverlayThreadHandles = nullptr;
	}

	if (m_OverlayThreadData)
	{
		for (UINT i = 0; i < m_OverlayThreadCount; ++i)
		{
			if (m_OverlayThreadData[i].RecordingOverlay) {
				CleanDx(&m_OverlayThreadData[i].RecordingOverlay->DxRes);
				if (m_OverlayThreadData[i].RecordingOverlay->FrameInfo->PtrFrameBuffer) {
					delete[] m_OverlayThreadData[i].RecordingOverlay->FrameInfo->PtrFrameBuffer;
					m_OverlayThreadData[i].RecordingOverlay->FrameInfo->PtrFrameBuffer = nullptr;
				}
				delete m_OverlayThreadData[i].RecordingOverlay->FrameInfo;
				delete m_OverlayThreadData[i].RecordingOverlay;
				m_OverlayThreadData[i].RecordingOverlay = nullptr;
			}
		}
		delete[] m_OverlayThreadData;
		m_OverlayThreadData = nullptr;
	}

	m_OverlayThreadCount = 0;

	if (m_CaptureThreadHandles) {
		for (UINT i = 0; i < m_CaptureThreadCount; ++i)
		{
			if (m_CaptureThreadHandles[i])
			{
				CloseHandle(m_CaptureThreadHandles[i]);
			}
		}
		delete[] m_CaptureThreadHandles;
		m_CaptureThreadHandles = nullptr;
	}

	if (m_CaptureThreadData)
	{
		for (UINT i = 0; i < m_CaptureThreadCount; ++i)
		{
			if (m_CaptureThreadData[i].RecordingSource) {
				CleanDx(&m_CaptureThreadData[i].RecordingSource->DxRes);
				delete m_CaptureThreadData[i].RecordingSource;
				m_CaptureThreadData[i].RecordingSource = nullptr;
			}
		}
		delete[] m_CaptureThreadData;
		m_CaptureThreadData = nullptr;
	}

	m_CaptureThreadCount = 0;

	CleanDX();

	CloseHandle(m_TerminateThreadsEvent);
}

void capture_base::CleanDX()
{
	SafeRelease(&m_SamplerLinear);
	SafeRelease(&m_BlendState);
	SafeRelease(&m_InputLayout);
	SafeRelease(&m_VertexShader);
	SafeRelease(&m_PixelShader);
}

//
// Waits infinitely for all spawned threads to terminate
//
void capture_base::WaitForThreadTermination()
{
	if (m_OverlayThreadCount != 0)
	{
		WaitForMultipleObjectsEx(m_OverlayThreadCount, m_OverlayThreadHandles, TRUE, INFINITE, FALSE);
	}
	if (m_CaptureThreadCount != 0) {
		WaitForMultipleObjectsEx(m_CaptureThreadCount, m_CaptureThreadHandles, TRUE, INFINITE, FALSE);
	}
}

void capture_base::ConfigureVertices(_Inout_ VERTEX(&vertices)[NUMVERTICES], _In_ RECORDING_OVERLAY_DATA *pOverlay, _In_ FRAME_INFO *pFrameInfo, _In_opt_ DXGI_MODE_ROTATION rotation)
{
	RECT backgroundRect = GetOutputRect();
	LONG backgroundWidth = RectWidth(backgroundRect);
	LONG backgroundHeight = RectHeight(backgroundRect);

	RECT overlayRect = GetOverlayRect(backgroundRect, pOverlay);
	LONG overlayLeft = overlayRect.left;
	LONG overlayTop = overlayRect.top;
	LONG overlayWidth = RectWidth(overlayRect);
	LONG overlayHeight = RectHeight(overlayRect);
	// Center of desktop dimensions
	FLOAT centerX = ((FLOAT)backgroundWidth / 2);
	FLOAT centerY = ((FLOAT)backgroundHeight / 2);


	// VERTEX creation
	if (rotation == DXGI_MODE_ROTATION_UNSPECIFIED
		|| rotation == DXGI_MODE_ROTATION_IDENTITY) {
		vertices[0].Pos.x = (overlayLeft - centerX) / centerX;
		vertices[0].Pos.y = -1 * ((overlayTop + overlayHeight) - centerY) / centerY;
		vertices[1].Pos.x = (overlayLeft - centerX) / centerX;
		vertices[1].Pos.y = -1 * (overlayTop - centerY) / centerY;
		vertices[2].Pos.x = ((overlayLeft + overlayWidth) - centerX) / centerX;
		vertices[2].Pos.y = -1 * ((overlayTop + overlayHeight) - centerY) / centerY;
		vertices[5].Pos.x = ((overlayLeft + overlayWidth) - centerX) / centerX;
		vertices[5].Pos.y = -1 * (overlayTop - centerY) / centerY;
	}	//Flip pointer 90 degrees counterclockwise
	else if (rotation == DXGI_MODE_ROTATION_ROTATE90) {
		vertices[0].Pos.x = ((overlayLeft + overlayWidth) - centerX) / centerX;
		vertices[0].Pos.y = -1 * ((overlayTop + overlayHeight) - centerY) / centerY;
		vertices[1].Pos.x = (overlayLeft - centerX) / centerX;
		vertices[1].Pos.y = -1 * ((overlayTop + overlayHeight) - centerY) / centerY;
		vertices[2].Pos.x = ((overlayLeft + overlayWidth) - centerX) / centerX;
		vertices[2].Pos.y = -1 * (overlayTop - centerY) / centerY;
		vertices[5].Pos.x = (overlayLeft - centerX) / centerX;
		vertices[5].Pos.y = -1 * (overlayTop - centerY) / centerY;
	}	//Turn pointer upside down
	else if (rotation == DXGI_MODE_ROTATION_ROTATE180) {
		vertices[0].Pos.x = ((overlayLeft + overlayWidth) - centerX) / centerX;
		vertices[0].Pos.y = -1 * (overlayTop - centerY) / centerY;
		vertices[1].Pos.x = ((overlayLeft + overlayWidth) - centerX) / centerX;
		vertices[1].Pos.y = -1 * ((overlayTop + overlayHeight) - centerY) / centerY;
		vertices[2].Pos.x = (overlayLeft - centerX) / centerX;
		vertices[2].Pos.y = -1 * (overlayTop - centerY) / centerY;
		vertices[5].Pos.x = (overlayLeft - centerX) / centerX;
		vertices[5].Pos.y = -1 * ((overlayTop + overlayHeight) - centerY) / centerY;
	}	//Flip pointer 90 degrees clockwise
	else if (rotation == DXGI_MODE_ROTATION_ROTATE270) {
		vertices[0].Pos.x = (overlayLeft - centerX) / centerX;
		vertices[0].Pos.y = -1 * (overlayTop - centerY) / centerY;
		vertices[1].Pos.x = ((overlayLeft + overlayWidth) - centerX) / centerX;
		vertices[1].Pos.y = -1 * (overlayTop - centerY) / centerY;
		vertices[2].Pos.x = (overlayLeft - centerX) / centerX;
		vertices[2].Pos.y = -1 * ((overlayTop + overlayHeight) - centerY) / centerY;
		vertices[5].Pos.x = ((overlayLeft + overlayWidth) - centerX) / centerX;
		vertices[5].Pos.y = -1 * ((overlayTop + overlayHeight) - centerY) / centerY;
	}

	vertices[3].Pos.x = vertices[2].Pos.x;
	vertices[3].Pos.y = vertices[2].Pos.y;
	vertices[4].Pos.x = vertices[1].Pos.x;
	vertices[4].Pos.y = vertices[1].Pos.y;
}

_Ret_maybenull_ CAPTURE_THREAD_DATA *capture_base::GetCaptureDataForRect(RECT rect)
{
	POINT pt{ rect.left,rect.top };
	for (UINT i = 0; i < m_CaptureThreadCount; ++i)
	{
		if (m_CaptureThreadData[i].RecordingSource) {
			if (PtInRect(&m_CaptureThreadData[i].ContentFrameRect, pt)) {
				return &m_CaptureThreadData[i];
			}
		}
	}
	return nullptr;
}

RECT capture_base::GetOverlayRect(_In_ RECT background, _In_ RECORDING_OVERLAY_DATA *pOverlay)
{
	LONG backgroundWidth = RectWidth(background);
	LONG backgroundHeight = RectHeight(background);

	// Clipping adjusted coordinates / dimensions
	LONG overlayWidth = pOverlay->Size.cx;
	LONG overlayHeight = pOverlay->Size.cy;
	if (overlayWidth == 0 && overlayHeight == 0) {
		overlayWidth = pOverlay->FrameInfo->Width;
		overlayHeight = pOverlay->FrameInfo->Height;
	}
	if (overlayWidth == 0) {
		overlayWidth = (LONG)(pOverlay->FrameInfo->Width * ((FLOAT)overlayHeight / pOverlay->FrameInfo->Height));
	}
	if (overlayHeight == 0) {
		overlayHeight = (LONG)(pOverlay->FrameInfo->Height * ((FLOAT)overlayWidth / pOverlay->FrameInfo->Width));
	}
	LONG overlayLeft = 0;
	LONG overlayTop = 0;

	switch (pOverlay->Anchor)
	{
	case OverlayAnchor::TopLeft: {
		overlayLeft = pOverlay->Offset.x;
		overlayTop = pOverlay->Offset.y;
		break;
	}
	case OverlayAnchor::TopRight: {
		overlayLeft = backgroundWidth - overlayWidth - pOverlay->Offset.x;
		overlayTop = pOverlay->Offset.y;
		break;
	}
	case OverlayAnchor::BottomLeft: {
		overlayLeft = pOverlay->Offset.x;
		overlayTop = backgroundHeight - overlayHeight - pOverlay->Offset.y;
		break;
	}
	case OverlayAnchor::BottomRight: {
		overlayLeft = backgroundWidth - overlayWidth - pOverlay->Offset.x;
		overlayTop = backgroundHeight - overlayHeight - pOverlay->Offset.y;
		break;
	}
	default:
		break;
	}
	return RECT{ overlayLeft,overlayTop,overlayLeft + overlayWidth,overlayTop + overlayHeight };
}

//
// Returns shared handle
//
_Ret_maybenull_ HANDLE capture_base::GetSharedHandle()
{
	if (!m_SharedSurf) {
		return nullptr;
	}
	HANDLE Hnd = nullptr;

	// QI IDXGIResource interface to synchronized shared surface.
	IDXGIResource *DXGIResource = nullptr;
	HRESULT hr = m_SharedSurf->QueryInterface(__uuidof(IDXGIResource), reinterpret_cast<void **>(&DXGIResource));
	if (SUCCEEDED(hr))
	{
		// Obtain handle to IDXGIResource object.
		DXGIResource->GetSharedHandle(&Hnd);
		DXGIResource->Release();
		DXGIResource = nullptr;
	}

	return Hnd;
}

bool capture_base::IsUpdatedFramesAvailable()
{
	for (UINT i = 0; i < m_OverlayThreadCount; ++i)
	{
		if (m_OverlayThreadData[i].LastUpdateTimeStamp.QuadPart > m_LastAcquiredFrameTimeStamp.QuadPart) {
			return true;
		}
	}
	for (UINT i = 0; i < m_CaptureThreadCount; ++i)
	{
		if (m_CaptureThreadData[i].LastUpdateTimeStamp.QuadPart > m_LastAcquiredFrameTimeStamp.QuadPart) {
			return true;
		}
	}
	return false;
}

bool capture_base::IsInitialFrameWriteComplete()
{
	for (UINT i = 0; i < m_CaptureThreadCount; ++i)
	{
		if (m_CaptureThreadData[i].RecordingSource) {
			if (m_CaptureThreadData[i].TotalUpdatedFrameCount == 0) {
				//If any of the recordings have not yet written a frame, we return and wait for them.
				return false;
			}
		}
	}
	return true;
}

UINT capture_base::GetUpdatedFrameCount(_In_ bool resetUpdatedFrameCounts)
{
	int updatedFrameCount = 0;

	for (UINT i = 0; i < m_CaptureThreadCount; ++i)
	{
		if (m_CaptureThreadData[i].LastUpdateTimeStamp.QuadPart > m_LastAcquiredFrameTimeStamp.QuadPart) {
			updatedFrameCount += m_CaptureThreadData[i].UpdatedFrameCountSinceLastWrite;
			if (resetUpdatedFrameCounts) {
				m_CaptureThreadData[i].UpdatedFrameCountSinceLastWrite = 0;
			}
		}
	}
	return updatedFrameCount;
}

HRESULT capture_base::CreateSharedSurf(_In_ std::vector<RECORDING_SOURCE> sources, _Out_ std::vector<RECORDING_SOURCE_DATA *> *pCreatedOutputs, _Out_ RECT *pDeskBounds)
{
	*pCreatedOutputs = std::vector<RECORDING_SOURCE_DATA *>();
	std::vector<std::pair<RECORDING_SOURCE, RECT>> validOutputs;
	HRESULT hr = GetOutputRectsForRecordingSources(sources, &validOutputs);
	if (FAILED(hr)) {
		LOG_ERROR(L"Failed to calculate output rects for recording sources");
		return hr;
	}

	std::vector<RECT> outputRects{};
	for each (auto pair in validOutputs)
	{
		outputRects.push_back(pair.second);
	}
	std::vector<SIZE> outputOffsets{};
	GetCombinedRects(outputRects, pDeskBounds, &outputOffsets);

	pDeskBounds = &MakeRectEven(*pDeskBounds);
	for (int i = 0; i < validOutputs.size(); i++)
	{
		RECORDING_SOURCE source = validOutputs.at(i).first;
		RECT sourceRect = validOutputs.at(i).second;
		RECORDING_SOURCE_DATA *data = new RECORDING_SOURCE_DATA(source);

		if (source.Type == SourceType::Monitor) {
			data->OffsetX -= pDeskBounds->left;
			data->OffsetY -= pDeskBounds->top;
		}
		else if (source.Type == SourceType::Window) {
			data->OffsetX += sourceRect.left;
		}
		data->OffsetX -= outputOffsets.at(i).cx;
		data->OffsetY -= outputOffsets.at(i).cy;
		pCreatedOutputs->push_back(data);
	}

	// Set created outputs
	return capture_base::CreateSharedSurf(*pDeskBounds);
}

HRESULT capture_base::CreateSharedSurf(_In_ RECT desktopRect)
{
	// Create shared texture for all capture threads to draw into
	D3D11_TEXTURE2D_DESC DeskTexD;
	RtlZeroMemory(&DeskTexD, sizeof(D3D11_TEXTURE2D_DESC));
	DeskTexD.Width = RectWidth(desktopRect);
	DeskTexD.Height = RectHeight(desktopRect);
	DeskTexD.MipLevels = 1;
	DeskTexD.ArraySize = 1;
	DeskTexD.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	DeskTexD.SampleDesc.Count = 1;
	DeskTexD.Usage = D3D11_USAGE_DEFAULT;
	DeskTexD.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
	DeskTexD.CPUAccessFlags = 0;
	DeskTexD.MiscFlags = D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;

	HRESULT hr = m_Device->CreateTexture2D(&DeskTexD, nullptr, &m_SharedSurf);
	if (FAILED(hr))
	{
		LOG_ERROR(L"Failed to create shared texture");
		return hr;
	}
	// Get keyed mutex
	hr = m_SharedSurf->QueryInterface(__uuidof(IDXGIKeyedMutex), reinterpret_cast<void **>(&m_KeyMutex));
	if (FAILED(hr))
	{
		LOG_ERROR(L"Failed to query for keyed mutex in OUTPUTMANAGER");
		return hr;
	}

	return hr;
}

SIZE capture_base::GetContentSize()
{
	RECT combinedRect = GetContentRect();
	return SIZE{ RectWidth(combinedRect),RectHeight(combinedRect) };
}

RECT capture_base::GetContentRect()
{
	RECT combinedRect{};
	std::vector<RECT> contentRects{};
	for (UINT i = 0; i < m_CaptureThreadCount; ++i)
	{
		contentRects.push_back(m_CaptureThreadData[i].ContentFrameRect);
	}
	GetCombinedRects(contentRects, &combinedRect, nullptr);
	return combinedRect;
}

HRESULT capture_base::ProcessOverlays(_Inout_ ID3D11Texture2D *pBackgroundFrame, _Out_ int *updateCount)
{
	int count = 0;
	for (UINT i = 0; i < m_OverlayThreadCount; ++i)
	{
		if (m_OverlayThreadData[i].RecordingOverlay) {
			RECORDING_OVERLAY_DATA *pOverlay = m_OverlayThreadData[i].RecordingOverlay;
			if (pOverlay->FrameInfo && pOverlay->FrameInfo->PtrFrameBuffer) {
				DrawOverlay(pBackgroundFrame, pOverlay);
				if (pOverlay->FrameInfo->LastTimeStamp.QuadPart > m_LastAcquiredFrameTimeStamp.QuadPart) {
					count++;
				}
			}
		}
	}
	*updateCount = count;
	return S_OK;
}

HRESULT capture_base::DrawOverlay(_Inout_ ID3D11Texture2D *pBackgroundFrame, _In_ RECORDING_OVERLAY_DATA *pOverlay)
{
	FRAME_INFO *pFrameInfo = pOverlay->FrameInfo;
	if (!pFrameInfo || pFrameInfo->PtrFrameBuffer == nullptr || pFrameInfo->BufferSize == 0)
		return S_FALSE;

	// Used for copying pixels
	D3D11_BOX Box = { 0 };
	Box.front = 0;
	Box.back = 1;

	D3D11_TEXTURE2D_DESC desc = { 0 };
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	desc.SampleDesc.Count = 1;
	desc.SampleDesc.Quality = 0;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	desc.CPUAccessFlags = 0;
	desc.MiscFlags = 0;

	VERTEX vertices[NUMVERTICES] =
	{
		{ XMFLOAT3(-1.0f, -1.0f, 0), XMFLOAT2(0.0f, 1.0f) },
		{ XMFLOAT3(-1.0f, 1.0f, 0), XMFLOAT2(0.0f, 0.0f) },
		{ XMFLOAT3(1.0f, -1.0f, 0), XMFLOAT2(1.0f, 1.0f) },
		{ XMFLOAT3(1.0f, -1.0f, 0), XMFLOAT2(1.0f, 1.0f) },
		{ XMFLOAT3(-1.0f, 1.0f, 0), XMFLOAT2(0.0f, 0.0f) },
		{ XMFLOAT3(1.0f, 1.0f, 0), XMFLOAT2(1.0f, 0.0f) },
	};

	ConfigureVertices(vertices, pOverlay, pFrameInfo);

	// Set texture properties
	desc.Width = pFrameInfo->Width;
	desc.Height = pFrameInfo->Height;

	// Set up init data
	D3D11_SUBRESOURCE_DATA initData = { 0 };
	initData.pSysMem = pFrameInfo->PtrFrameBuffer;
	initData.SysMemPitch = abs(pFrameInfo->Stride);
	initData.SysMemSlicePitch = 0;

	// Create mouseshape as texture
	CComPtr<ID3D11Texture2D> overlayTex;
	HRESULT hr = m_Device->CreateTexture2D(&desc, &initData, &overlayTex);
	if (FAILED(hr))
	{
		_com_error err(hr);
		LOG_ERROR(L"Failed to create overlay texture: %ls", err.ErrorMessage());
		return hr;
	}
	// Set shader resource properties
	D3D11_SHADER_RESOURCE_VIEW_DESC shaderDesc;
	shaderDesc.Format = desc.Format;
	shaderDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	shaderDesc.Texture2D.MostDetailedMip = desc.MipLevels - 1;
	shaderDesc.Texture2D.MipLevels = desc.MipLevels;

	// Create shader resource from texture
	CComPtr<ID3D11ShaderResourceView> shaderRes;
	hr = m_Device->CreateShaderResourceView(overlayTex, &shaderDesc, &shaderRes);
	if (FAILED(hr))
	{
		_com_error err(hr);
		LOG_ERROR(L"Failed to create shader resource from overlay texture: %ls", err.ErrorMessage());
		return hr;
	}

	D3D11_BUFFER_DESC bufferDesc;
	ZeroMemory(&bufferDesc, sizeof(D3D11_BUFFER_DESC));
	bufferDesc.Usage = D3D11_USAGE_DEFAULT;
	bufferDesc.ByteWidth = sizeof(VERTEX) * NUMVERTICES;
	bufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	bufferDesc.CPUAccessFlags = 0;

	ZeroMemory(&initData, sizeof(D3D11_SUBRESOURCE_DATA));
	initData.pSysMem = vertices;

	// Create vertex buffer
	CComPtr<ID3D11Buffer> VertexBuffer;
	hr = m_Device->CreateBuffer(&bufferDesc, &initData, &VertexBuffer);
	if (FAILED(hr))
	{
		_com_error err(hr);
		LOG_ERROR(L"Failed to create overlay vertex buffer: %ls", err.ErrorMessage());
		return hr;
	}
	CComPtr<ID3D11RenderTargetView> RTV;
	// Create a render target view
	hr = m_Device->CreateRenderTargetView(pBackgroundFrame, nullptr, &RTV);
	// Set resources
	FLOAT BlendFactor[4] = { 0.f, 0.f, 0.f, 0.f };
	UINT Stride = sizeof(VERTEX);
	UINT Offset = 0;
	m_DeviceContext->IASetVertexBuffers(0, 1, &VertexBuffer.p, &Stride, &Offset);
	m_DeviceContext->OMSetBlendState(m_BlendState, BlendFactor, 0xFFFFFFFF);
	m_DeviceContext->OMSetRenderTargets(1, &RTV.p, nullptr);
	m_DeviceContext->VSSetShader(m_VertexShader, nullptr, 0);
	m_DeviceContext->PSSetShader(m_PixelShader, nullptr, 0);
	m_DeviceContext->PSSetShaderResources(0, 1, &shaderRes.p);
	m_DeviceContext->PSSetSamplers(0, 1, &m_SamplerLinear);
	m_DeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	// Draw
	m_DeviceContext->Draw(NUMVERTICES, 0);
	return hr;
}

bool capture_base::IsSingleWindowCapture()
{
	return m_CaptureThreadCount == 1 && m_CaptureThreadData[0].RecordingSource->WindowHandle;
}

DWORD WINAPI OverlayProc(_In_ void *Param) {
	HRESULT hr = S_OK;
	// Data passed in from thread creation
	OVERLAY_THREAD_DATA *pData = reinterpret_cast<OVERLAY_THREAD_DATA *>(Param);

	bool isExpectedError = false;
	bool isUnexpectedError = false;
	RECORDING_OVERLAY_DATA *pOverlay = pData->RecordingOverlay;
	switch (pOverlay->Type)
	{
	case OverlayType::Picture: {
		std::string signature = ReadFileSignature(pOverlay->Source.c_str());
		ImageFileType imageType = getImageTypeByMagic(signature.c_str());
		if (imageType == ImageFileType::IMAGE_FILE_GIF) {
			gif_reader gifReader{};
			hr = ReadMedia(gifReader, pData);
		}
		else {
			hr = ReadImage(pData);
		}
		break;
	}
	case OverlayType::Video: {
		video_reader videoReader{};
		hr = ReadMedia(videoReader, pData);
		break;
	}
	case OverlayType::CameraCapture: {
		device_capture videoCapture{};
		hr = ReadMedia(videoCapture, pData);
		break;
	}
	default:
		break;
	}

	pData->ThreadResult = hr;
	if (isExpectedError) {
		SetEvent(pData->ExpectedErrorEvent);
	}
	else if (isUnexpectedError) {
		SetEvent(pData->UnexpectedErrorEvent);
	}
	return 0;
}

HRESULT ReadImage(OVERLAY_THREAD_DATA *pData) {
	RECORDING_OVERLAY_DATA *pOverlay = pData->RecordingOverlay;

	CComPtr<IWICBitmapSource> pBitmap;
	HRESULT hr = CreateWICBitmapFromFile(pOverlay->Source.c_str(), GUID_WICPixelFormat32bppBGRA, &pBitmap);
	if (FAILED(hr)) {
		return hr;
	}
	// Copy the 32bpp RGBA image to a buffer for further processing.
	UINT width, height;
	RETURN_ON_BAD_HR(hr = pBitmap->GetSize(&width, &height));

	const unsigned bytesPerPixel = 4;
	const unsigned stride = width * bytesPerPixel;
	const unsigned bitmapSize = width * height * bytesPerPixel;

	FRAME_INFO *pFrame = pOverlay->FrameInfo;
	pFrame->BufferSize = bitmapSize;
	pFrame->Stride = stride;
	pFrame->Width = width;
	pFrame->Height = height;
	if (pFrame->PtrFrameBuffer)
	{
		delete[] pFrame->PtrFrameBuffer;
		pFrame->PtrFrameBuffer = nullptr;
	}
	pFrame->PtrFrameBuffer = new (std::nothrow) BYTE[pFrame->BufferSize];
	if (!pFrame->PtrFrameBuffer)
	{
		pFrame->BufferSize = 0;
		LOG_ERROR(L"Failed to allocate memory for frame");
		return E_OUTOFMEMORY;
	}
	RETURN_ON_BAD_HR(hr = pBitmap->CopyPixels(nullptr, stride, pFrame->BufferSize, pFrame->PtrFrameBuffer));
	QueryPerformanceCounter(&pData->LastUpdateTimeStamp);
	return hr;
}

HRESULT ReadMedia(reader_base &reader, OVERLAY_THREAD_DATA *pData) {
	RECORDING_OVERLAY_DATA *pOverlay = pData->RecordingOverlay;
	HRESULT	hr = reader.Initialize(&pOverlay->DxRes);
	hr = reader.StartCapture(pOverlay->Source);
	if (FAILED(hr))
	{
		return hr;
	}
	// D3D objects
	ID3D11Texture2D *SharedSurf = nullptr;
	IDXGIKeyedMutex *KeyMutex = nullptr;

	// Obtain handle to sync shared Surface
	hr = pOverlay->DxRes.Device->OpenSharedResource(pData->TexSharedHandle, __uuidof(ID3D11Texture2D), reinterpret_cast<void **>(&SharedSurf));
	if (FAILED(hr))
	{
		LOG_ERROR(L"Opening shared texture failed");
		return hr;
	}
	ReleaseOnExit releaseSharedSurf(SharedSurf);
	hr = SharedSurf->QueryInterface(__uuidof(IDXGIKeyedMutex), reinterpret_cast<void **>(&KeyMutex));
	if (FAILED(hr))
	{
		LOG_ERROR(L"Failed to get keyed mutex interface in spawned thread");
		return hr;
	}
	ReleaseOnExit releaseMutex(KeyMutex);

	// Main capture loop
	CComPtr<IMFMediaBuffer> pFrameBuffer = nullptr;
	while (true)
	{
		if (WaitForSingleObjectEx(pData->TerminateThreadsEvent, 0, FALSE) == WAIT_OBJECT_0) {
			hr = S_OK;
			break;
		}
		if (pFrameBuffer)
			pFrameBuffer.Release();
		// Get new frame from video capture
		hr = reader.GetFrame(pOverlay->FrameInfo, 10);
		if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
			continue;
		}
		else if (FAILED(hr)) {
			break;
		}
		QueryPerformanceCounter(&pData->LastUpdateTimeStamp);
		{
			MeasureExecutionTime measure(L"ReadMedia wait for sync");
			// Try to acquire keyed mutex in order to access shared surface
			hr = KeyMutex->AcquireSync(0, 10);
		}

		if (hr == static_cast<HRESULT>(WAIT_TIMEOUT))
		{
			// Can't use shared surface right now, try again later
			continue;
		}
		else if (FAILED(hr))
		{
			// Generic unknown failure
			LOG_ERROR(L"Unexpected error acquiring KeyMutex");
			break;
		}
		KeyMutex->ReleaseSync(1);
	}
	return hr;
}