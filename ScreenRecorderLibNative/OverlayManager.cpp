#include "OverlayManager.h"
#include "cleanup.h"
#include "CameraCapture.h"
#include "VideoReader.h"
#include "GifReader.h"
using namespace std;

DWORD WINAPI OverlayProc(_In_ void *Param);
HRESULT ReadMedia(CaptureBase &reader, OVERLAY_THREAD_DATA *pData);
HRESULT ReadImage(OVERLAY_THREAD_DATA *pData);

OverlayManager::OverlayManager() :
	m_Device(nullptr),
	m_DeviceContext(nullptr),
	m_TerminateThreadsEvent(nullptr),
	m_OverlayThreadCount(0),
	m_OverlayThreadHandles(nullptr),
	m_OverlayThreadData(nullptr),
	m_TextureManager(nullptr),
	m_LastAcquiredFrameTimeStamp{},
	m_IsCapturing(false)
{
	// Event to tell spawned threads to quit
	m_TerminateThreadsEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
}

OverlayManager::~OverlayManager()
{
	if (m_IsCapturing) {
		StopCapture();
	}
	Clean();
}

//
// Initialize shaders for drawing to screen
//
HRESULT OverlayManager::Initialize(_In_ ID3D11DeviceContext *pDeviceContext, _In_ ID3D11Device *pDevice)
{
	m_Device = pDevice;
	m_DeviceContext = pDeviceContext;

	m_TextureManager = make_unique<TextureManager>();
	HRESULT hr = m_TextureManager->Initialize(m_DeviceContext, m_Device);

	return hr;
}

HRESULT OverlayManager::StartCapture(std::vector<RECORDING_OVERLAY> overlays, HANDLE hUnexpectedErrorEvent, HANDLE hExpectedErrorEvent)
{
	HRESULT hr = S_OK;
	ResetEvent(m_TerminateThreadsEvent);
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
		m_OverlayThreadData[i].RecordingOverlay = new RECORDING_OVERLAY_DATA(overlay);
		m_OverlayThreadData[i].RecordingOverlay->FrameInfo = new FRAME_BASE{};
		RtlZeroMemory(&m_OverlayThreadData[i].RecordingOverlay->DxRes, sizeof(DX_RESOURCES));
		m_OverlayThreadData[i].RecordingOverlay->DxRes.Context = m_DeviceContext;
		m_OverlayThreadData[i].RecordingOverlay->DxRes.Device = m_Device;
		//hr = InitializeDx(nullptr, &m_OverlayThreadData[i].RecordingOverlay->DxRes);
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

HRESULT OverlayManager::StopCapture()
{
	if (!SetEvent(m_TerminateThreadsEvent)) {
		LOG_ERROR("Could not terminate capture thread");
		return E_FAIL;
	}
	WaitForThreadTermination();
	m_IsCapturing = false;
	return S_OK;
}

bool OverlayManager::IsUpdatedFramesAvailable()
{
	for (UINT i = 0; i < m_OverlayThreadCount; ++i)
	{
		if (m_OverlayThreadData[i].LastUpdateTimeStamp.QuadPart > m_LastAcquiredFrameTimeStamp.QuadPart) {
			return true;
		}
	}
	return false;
}

HRESULT OverlayManager::ProcessOverlays(_Inout_ ID3D11Texture2D *pBackgroundFrame, _Out_ int *updateCount)
{
	HRESULT hr = S_FALSE;
	int count = 0;

	D3D11_TEXTURE2D_DESC desc;
	pBackgroundFrame->GetDesc(&desc);
	SIZE canvasSize = SIZE{ static_cast<LONG>(desc.Width),static_cast<LONG>(desc.Height) };

	for (UINT i = 0; i < m_OverlayThreadCount; ++i)
	{
		if (m_OverlayThreadData[i].RecordingOverlay) {
			RECORDING_OVERLAY_DATA *pOverlayData = m_OverlayThreadData[i].RecordingOverlay;
			FRAME_BASE *pFrameInfo = m_OverlayThreadData[i].RecordingOverlay->FrameInfo;
			if (pOverlayData && pFrameInfo && pFrameInfo->Frame) {
				//DrawOverlay(pBackgroundFrame, pOverlay);
				D3D11_TEXTURE2D_DESC desc;
				pFrameInfo->Frame->GetDesc(&desc);
				SIZE textureSize = SIZE{ static_cast<LONG>(desc.Width),static_cast<LONG>(desc.Height) };
				hr = m_TextureManager->DrawTexture(pBackgroundFrame, pFrameInfo->Frame, GetOverlayRect(canvasSize, textureSize, pOverlayData));
				if (m_OverlayThreadData[i].LastUpdateTimeStamp.QuadPart > m_LastAcquiredFrameTimeStamp.QuadPart) {
					count++;
				}
			}
		}
	}
	if (count > 0) {
		QueryPerformanceCounter(&m_LastAcquiredFrameTimeStamp);
	}
	*updateCount = count;
	return hr;
}


RECT OverlayManager::GetOverlayRect(_In_ SIZE canvasSize, _In_ SIZE overlayTextureSize, _In_ RECORDING_OVERLAY_DATA *pOverlay)
{
	LONG backgroundWidth = canvasSize.cx;
	LONG backgroundHeight = canvasSize.cy;
	// Clipping adjusted coordinates / dimensions
	LONG overlayWidth = pOverlay->Size.cx;
	LONG overlayHeight = pOverlay->Size.cy;
	if (overlayWidth == 0 && overlayHeight == 0) {
		overlayWidth = overlayTextureSize.cx;
		overlayHeight = overlayTextureSize.cy;
	}
	if (overlayWidth == 0) {
		overlayWidth = (LONG)(overlayTextureSize.cx * ((FLOAT)overlayHeight / overlayTextureSize.cy));
	}
	if (overlayHeight == 0) {
		overlayHeight = (LONG)(overlayTextureSize.cx * ((FLOAT)overlayWidth / overlayTextureSize.cx));
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
// Clean up resources
//
void OverlayManager::Clean()
{
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
				//CleanDx(&m_OverlayThreadData[i].RecordingOverlay->DxRes);
				//if (m_OverlayThreadData[i].RecordingOverlay->FrameInfo->PtrFrameBuffer) {
				//	delete[] m_OverlayThreadData[i].RecordingOverlay->FrameInfo->PtrFrameBuffer;
				//	m_OverlayThreadData[i].RecordingOverlay->FrameInfo->PtrFrameBuffer = nullptr;
				//}
				SafeRelease(&m_OverlayThreadData[i].RecordingOverlay->FrameInfo->Frame);
				delete m_OverlayThreadData[i].RecordingOverlay->FrameInfo;
				delete m_OverlayThreadData[i].RecordingOverlay;
				m_OverlayThreadData[i].RecordingOverlay = nullptr;
			}
		}
		delete[] m_OverlayThreadData;
		m_OverlayThreadData = nullptr;
	}

	m_OverlayThreadCount = 0;

	CloseHandle(m_TerminateThreadsEvent);
}

void OverlayManager::WaitForThreadTermination()
{
	if (m_OverlayThreadCount != 0)
	{
		WaitForMultipleObjectsEx(m_OverlayThreadCount, m_OverlayThreadHandles, TRUE, INFINITE, FALSE);
	}
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
	case RecordingOverlayType::Picture: {
		std::string signature = ReadFileSignature(pOverlay->Source.c_str());
		ImageFileType imageType = getImageTypeByMagic(signature.c_str());
		if (imageType == ImageFileType::IMAGE_FILE_GIF) {
			GifReader gifReader{};
			hr = ReadMedia(gifReader, pData);
		}
		else {
			hr = ReadImage(pData);
		}
		break;
	}
	case RecordingOverlayType::Video: {
		VideoReader videoReader{};
		hr = ReadMedia(videoReader, pData);
		break;
	}
	case RecordingOverlayType::CameraCapture: {
		CameraCapture videoCapture{};
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

	FRAME_BASE *pFrame = pOverlay->FrameInfo;
	//pFrame->BufferSize = bitmapSize;
	//pFrame->Stride = stride;
	//pFrame->Width = width;
	//pFrame->Height = height;
	//if (pFrame->PtrFrameBuffer)
	//{
	//	delete[] pFrame->PtrFrameBuffer;
	//	pFrame->PtrFrameBuffer = nullptr;
	//}
	//pFrame->PtrFrameBuffer = new (std::nothrow) BYTE[pFrame->BufferSize];
	//if (!pFrame->PtrFrameBuffer)
	//{
	//	pFrame->BufferSize = 0;
	//	LOG_ERROR(L"Failed to allocate memory for frame");
	//	return E_OUTOFMEMORY;
	//}
	BYTE *pFrameBuffer = new (std::nothrow) BYTE[bitmapSize];
	DeleteArrayOnExit deleteOnExit(pFrameBuffer);
	RETURN_ON_BAD_HR(hr = pBitmap->CopyPixels(nullptr, stride, bitmapSize, pFrameBuffer));
	std::unique_ptr<TextureManager> pTextureManager = make_unique<TextureManager>();
	pTextureManager->Initialize(pData->RecordingOverlay->DxRes.Context, pData->RecordingOverlay->DxRes.Device);
	ID3D11Texture2D *pTexture;
	RETURN_ON_BAD_HR(pTextureManager->CreateTextureFromBuffer(pFrameBuffer, stride, width, height, &pTexture));
	QueryPerformanceCounter(&pData->LastUpdateTimeStamp);
	pFrame->Frame = pTexture;
	pFrame->Frame->AddRef();
	return hr;
}

HRESULT ReadMedia(CaptureBase &reader, OVERLAY_THREAD_DATA *pData) {
	RECORDING_OVERLAY_DATA *pOverlay = pData->RecordingOverlay;
	HRESULT	hr = reader.Initialize(pOverlay->DxRes.Context, pOverlay->DxRes.Device);
	hr = reader.StartCapture(pOverlay->Source);
	if (FAILED(hr))
	{
		return hr;
	}
	//// D3D objects
	//ID3D11Texture2D *SharedSurf = nullptr;
	//IDXGIKeyedMutex *KeyMutex = nullptr;
	FRAME_BASE *pFrameInfo = pOverlay->FrameInfo;
	//// Obtain handle to sync shared Surface
	//hr = pOverlay->DxRes.Device->OpenSharedResource(pData->TexSharedHandle, __uuidof(ID3D11Texture2D), reinterpret_cast<void **>(&SharedSurf));
	//if (FAILED(hr))
	//{
	//	LOG_ERROR(L"Opening shared texture failed");
	//	return hr;
	//}
	//ReleaseOnExit releaseSharedSurf(SharedSurf);
	//hr = SharedSurf->QueryInterface(__uuidof(IDXGIKeyedMutex), reinterpret_cast<void **>(&KeyMutex));
	//if (FAILED(hr))
	//{
	//	LOG_ERROR(L"Failed to get keyed mutex interface in spawned thread");
	//	return hr;
	//}
	//ReleaseOnExit releaseMutex(KeyMutex);

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
		CComPtr<ID3D11Texture2D> pTexture;
		hr = reader.AcquireNextFrame(10, &pTexture);
		if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
			continue;
		}
		else if (FAILED(hr)) {
			break;
		}
		QueryPerformanceCounter(&pData->LastUpdateTimeStamp);
		//{
		//	MeasureExecutionTime measure(L"ReadMedia wait for sync");
		//	// Try to acquire keyed mutex in order to access shared surface
		//	hr = KeyMutex->AcquireSync(0, 1000);
		//}

		//if (hr == static_cast<HRESULT>(WAIT_TIMEOUT))
		//{
		//	// Can't use shared surface right now, try again later
		//	continue;
		//}
		//else if (FAILED(hr))
		//{
		//	// Generic unknown failure
		//	LOG_ERROR(L"Unexpected error acquiring KeyMutex");
		//	break;
		//}

		if (pFrameInfo->Frame == nullptr) {
			D3D11_TEXTURE2D_DESC desc;
			pTexture->GetDesc(&desc);
			desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
			desc.SampleDesc.Count = 1;
			desc.Usage = D3D11_USAGE_DEFAULT;
			desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
			desc.CPUAccessFlags = 0;
			desc.MiscFlags = 0;
			//SafeRelease(&pData->FrameInfo->Frame);
			//CComPtr<ID3D11Texture2D> tex;
			pOverlay->DxRes.Device->CreateTexture2D(&desc, nullptr, &pFrameInfo->Frame);
		}
		pOverlay->DxRes.Context->CopyResource(pFrameInfo->Frame, pTexture);
		//KeyMutex->ReleaseSync(1);
	}
	return hr;
}