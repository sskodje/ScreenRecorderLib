#include "GifReader.h"
#include "Cleanup.h"

using namespace std;

GifReader::GifReader()
	:
	m_RenderTarget(nullptr),
	m_RenderTexture(nullptr),
	m_pD2DFactory(nullptr),
	m_pFrameComposeRT(nullptr),
	m_pRawFrame(nullptr),
	m_pSavedFrame(nullptr),
	m_pIWICFactory(nullptr),
	m_pDecoder(nullptr),
	m_FramerateTimer(nullptr),
	m_Device(nullptr),
	m_DeviceContext(nullptr),
	m_LastSampleReceivedTimeStamp{ 0 },
	m_LastGrabTimeStamp{ 0 },
	m_cxGifImage(0),
	m_cyGifImage(0)
{
	InitializeCriticalSection(&m_CriticalSection);
	m_NewFrameEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
}

GifReader::~GifReader()
{
	StopCapture();
	SafeRelease(&m_RenderTarget);
	SafeRelease(&m_pD2DFactory);
	SafeRelease(&m_pFrameComposeRT);
	SafeRelease(&m_pRawFrame);
	SafeRelease(&m_pSavedFrame);
	SafeRelease(&m_pIWICFactory);
	SafeRelease(&m_pDecoder);
	SafeRelease(&m_RenderTexture);
	SafeRelease(&m_Device);
	SafeRelease(&m_DeviceContext);
	CloseHandle(m_NewFrameEvent);
	DeleteCriticalSection(&m_CriticalSection);
}

HRESULT GifReader::StartCapture(_In_ RECORDING_SOURCE_BASE &recordingSource)
{
	HRESULT hr;

	RETURN_ON_BAD_HR(hr = InitializeDecoder(recordingSource.SourcePath));
	RETURN_ON_BAD_HR(hr = CreateDeviceResources());
	// If we have at least one frame, start playing
	// the animation from the first frame
	if (m_cFrames > 0)
	{
		hr = StartCaptureLoop();
	}

	return hr;
}

HRESULT GifReader::GetNativeSize(_In_ RECORDING_SOURCE_BASE &recordingSource, _Out_ SIZE *nativeMediaSize)
{
	HRESULT hr = S_OK;
	if (m_cxGifImage == 0 || m_cyGifImage == 0) {
		RETURN_ON_BAD_HR(hr = InitializeDecoder(recordingSource.SourcePath));
	}
	*nativeMediaSize = SIZE{ static_cast<LONG>(m_cxGifImage) , static_cast<LONG>(m_cyGifImage) };
	return hr;
}

HRESULT GifReader::StopCapture()
{
	if (m_FramerateTimer) {
		LOG_DEBUG("Stopping GIF reader sync timer");
		HRESULT hr = m_FramerateTimer->StopTimer(true);
		if (FAILED(hr)) {
			return hr;
		}
		m_CaptureTask.wait();
	}
	return S_OK;
}

HRESULT GifReader::AcquireNextFrame(_In_ DWORD timeoutMillis, _Outptr_opt_ ID3D11Texture2D **ppFrame)
{
	DWORD result = WAIT_OBJECT_0;

	if (m_LastGrabTimeStamp.QuadPart >= m_LastSampleReceivedTimeStamp.QuadPart) {
		result = WaitForSingleObject(m_NewFrameEvent, timeoutMillis);
	}
	HRESULT hr = S_OK;
	if (result == WAIT_OBJECT_0) {
		if (ppFrame) {
			EnterCriticalSection(&m_CriticalSection);
			LeaveCriticalSectionOnExit leaveCriticalSection(&m_CriticalSection, L"GetFrameBuffer");
			CComPtr<ID3D11Texture2D> pStagingTexture = nullptr;
			D3D11_TEXTURE2D_DESC desc;
			m_RenderTexture->GetDesc(&desc);
			desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			desc.MiscFlags = 0;
			desc.Usage = D3D11_USAGE_DEFAULT;

			RETURN_ON_BAD_HR(hr = m_Device->CreateTexture2D(&desc, nullptr, &pStagingTexture));

			m_DeviceContext->CopyResource(pStagingTexture, m_RenderTexture);
			*ppFrame = pStagingTexture;
			(*ppFrame)->AddRef();
			QueryPerformanceCounter(&m_LastGrabTimeStamp);
		}
	}
	else if (result == WAIT_TIMEOUT) {
		hr = DXGI_ERROR_WAIT_TIMEOUT;
	}
	else {
		DWORD dwErr = GetLastError();
		LOG_ERROR(L"WaitForSingleObject failed: last error = %u", dwErr);
		hr = HRESULT_FROM_WIN32(dwErr);
	}
	return hr;
}

HRESULT GifReader::WriteNextFrameToSharedSurface(_In_ DWORD timeoutMillis, _Inout_ ID3D11Texture2D *pSharedSurf, INT offsetX, INT offsetY, _In_ RECT destinationRect, _In_opt_ const std::optional<RECT> &sourceRect)
{
	CComPtr<ID3D11Texture2D> processedTexture;
	HRESULT hr = AcquireNextFrame(timeoutMillis, &processedTexture);
	if (FAILED(hr)) {
		return hr;
	}
	EnterCriticalSection(&m_CriticalSection);
	LeaveCriticalSectionOnExit leaveCriticalSection(&m_CriticalSection, L"WriteNextFrameToSharedSurface");
	D3D11_TEXTURE2D_DESC frameDesc;
	processedTexture->GetDesc(&frameDesc);

	if (sourceRect.has_value()
		&& IsValidRect(sourceRect.value())
		&& (RectWidth(sourceRect.value()) != frameDesc.Width || (RectHeight(sourceRect.value()) != frameDesc.Height))) {
		ID3D11Texture2D *pCroppedTexture;
		RETURN_ON_BAD_HR(hr = m_TextureManager->CropTexture(processedTexture, sourceRect.value(), &pCroppedTexture));
		processedTexture.Release();
		processedTexture.Attach(pCroppedTexture);
	}
	processedTexture->GetDesc(&frameDesc);

	int leftMargin = 0;
	int topMargin = 0;
	if ((RectWidth(destinationRect) != frameDesc.Width || RectHeight(destinationRect) != frameDesc.Height)) {
		double widthRatio = (double)RectWidth(destinationRect) / frameDesc.Width;
		double heightRatio = (double)RectHeight(destinationRect) / frameDesc.Height;

		double resizeRatio = min(widthRatio, heightRatio);
		UINT resizedWidth = (UINT)MakeEven((LONG)round(frameDesc.Width * resizeRatio));
		UINT resizedHeight = (UINT)MakeEven((LONG)round(frameDesc.Height * resizeRatio));
		ID3D11Texture2D *resizedTexture = nullptr;
		RETURN_ON_BAD_HR(hr = m_TextureManager->ResizeTexture(processedTexture, &resizedTexture, SIZE{ static_cast<LONG>(resizedWidth), static_cast<LONG>(resizedHeight) }));
		processedTexture.Release();
		processedTexture.Attach(resizedTexture);
		leftMargin = (int)max(0, round(((double)RectWidth(destinationRect) - (double)resizedWidth)) / 2);
		topMargin = (int)max(0, round(((double)RectHeight(destinationRect) - (double)resizedHeight)) / 2);
	}

	processedTexture->GetDesc(&frameDesc);


	long left = destinationRect.left + offsetX + leftMargin;
	long top = destinationRect.top + offsetY + topMargin;
	long right = left + MakeEven(frameDesc.Width);
	long bottom = top + MakeEven(frameDesc.Height);

	D3D11_BOX Box{};
	// Copy back to shared surface
	Box.right = right-left;
	Box.bottom = bottom-top;
	Box.back = 1;

	CComPtr<ID3D11Texture2D> pBlankFrame;
	D3D11_TEXTURE2D_DESC desc;
	pSharedSurf->GetDesc(&desc);
	desc.MiscFlags = 0;
	desc.Width = right - left;
	desc.Height = bottom - top;
	hr = m_Device->CreateTexture2D(&desc, nullptr, &pBlankFrame);
	if (SUCCEEDED(hr)) {
		m_DeviceContext->CopySubresourceRegion(pSharedSurf, 0, left, top, 0, pBlankFrame, 0, &Box);
	}
	m_TextureManager->DrawTexture(pSharedSurf, processedTexture, RECT{ left,top,right,bottom });
	return hr;
}

HRESULT GifReader::Initialize(_In_ ID3D11DeviceContext *pDeviceContext, _In_ ID3D11Device *pDevice)
{
	m_Device = pDevice;
	m_DeviceContext = pDeviceContext;

	m_Device->AddRef();
	m_DeviceContext->AddRef();

	m_TextureManager = make_unique<TextureManager>();
	HRESULT hr = m_TextureManager->Initialize(m_DeviceContext, m_Device);
	return hr;
}

HRESULT GifReader::InitializeDecoder(_In_ std::wstring source)
{
	HRESULT hr = S_OK;

	// Reset the states
	m_uNextFrameIndex = 0;
	m_uFrameDisposal = DM_NONE;  // No previous frame, use disposal none
	m_uLoopNumber = 0;
	m_fHasLoop = FALSE;
	SafeRelease(&m_pSavedFrame);

	if (!m_pD2DFactory) {
		// Create D2D factory
		RETURN_ON_BAD_HR(hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory), (void **)&m_pD2DFactory));
	}
	if (!m_pIWICFactory) {
		// Create WIC factory
		hr = CoCreateInstance(
			CLSID_WICImagingFactory,
			NULL,
			CLSCTX_INPROC_SERVER,
			IID_PPV_ARGS(&m_pIWICFactory));

	}

	// Create a decoder for the gif file
	SafeRelease(&m_pDecoder);
	hr = m_pIWICFactory->CreateDecoderFromFilename(
		source.c_str(),
		NULL,
		GENERIC_READ,
		WICDecodeMetadataCacheOnLoad,
		&m_pDecoder);
	if (SUCCEEDED(hr))
	{
		hr = GetGlobalMetadata();
	}

	return hr;
}

HRESULT GifReader::CreateDeviceResources()
{
	HRESULT hr = S_OK;


	if (m_RenderTarget == NULL)
	{
		// Create the DXGI Surface Render Target.
		UINT dpi = GetDpiForSystem();
		/* RenderTargetProperties contains the description for render target */
		D2D1_RENDER_TARGET_PROPERTIES RenderTargetProperties =
			D2D1::RenderTargetProperties(
				D2D1_RENDER_TARGET_TYPE_DEFAULT,
				D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_PREMULTIPLIED),
				static_cast<float>(dpi),
				static_cast<float>(dpi)
			);


		D3D11_TEXTURE2D_DESC desc = { 0 };
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
		desc.SampleDesc.Count = 1;
		desc.SampleDesc.Quality = 0;
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_RENDER_TARGET;
		desc.Width = m_cxGifImagePixel;
		desc.Height = m_cyGifImagePixel;
		RETURN_ON_BAD_HR(hr = m_Device->CreateTexture2D(&desc, nullptr, &m_RenderTexture));

		ATL::CComPtr<IDXGISurface> pSharedSurface;
		RETURN_ON_BAD_HR(hr = m_RenderTexture->QueryInterface(__uuidof(IDXGISurface), (void **)&pSharedSurface));
		RETURN_ON_BAD_HR(hr = m_pD2DFactory->CreateDxgiSurfaceRenderTarget(pSharedSurface, RenderTargetProperties, &m_RenderTarget));

		if (SUCCEEDED(hr))
		{
			SafeRelease(&m_pFrameComposeRT);
			hr = m_RenderTarget->CreateCompatibleRenderTarget(
				D2D1::SizeF(
					static_cast<FLOAT>(m_cxGifImage),
					static_cast<FLOAT>(m_cyGifImage)),
				&m_pFrameComposeRT);
		}
	}
	return hr;
}

HRESULT GifReader::GetGlobalMetadata()
{
	PROPVARIANT propValue;
	PropVariantInit(&propValue);
	IWICMetadataQueryReader *pMetadataQueryReader = NULL;

	// Get the frame count
	HRESULT hr = m_pDecoder->GetFrameCount(&m_cFrames);
	if (SUCCEEDED(hr))
	{
		// Create a MetadataQueryReader from the decoder
		hr = m_pDecoder->GetMetadataQueryReader(
			&pMetadataQueryReader);
	}

	if (SUCCEEDED(hr))
	{
		// Get background color
		if (FAILED(GetBackgroundColor(pMetadataQueryReader)))
		{
			// Default to transparent if failed to get the color
			m_backgroundColor = D2D1::ColorF(0, 0.f);
		}
	}

	// Get global frame size
	if (SUCCEEDED(hr))
	{
		// Get width
		hr = pMetadataQueryReader->GetMetadataByName(
			L"/logscrdesc/Width",
			&propValue);
		if (SUCCEEDED(hr))
		{
			hr = (propValue.vt == VT_UI2 ? S_OK : E_FAIL);
			if (SUCCEEDED(hr))
			{
				m_cxGifImage = propValue.uiVal;
			}
			PropVariantClear(&propValue);
		}
	}

	if (SUCCEEDED(hr))
	{
		// Get height
		hr = pMetadataQueryReader->GetMetadataByName(
			L"/logscrdesc/Height",
			&propValue);
		if (SUCCEEDED(hr))
		{
			hr = (propValue.vt == VT_UI2 ? S_OK : E_FAIL);
			if (SUCCEEDED(hr))
			{
				m_cyGifImage = propValue.uiVal;
			}
			PropVariantClear(&propValue);
		}
	}

	if (SUCCEEDED(hr))
	{
		// Get pixel aspect ratio
		hr = pMetadataQueryReader->GetMetadataByName(
			L"/logscrdesc/PixelAspectRatio",
			&propValue);
		if (SUCCEEDED(hr))
		{
			hr = (propValue.vt == VT_UI1 ? S_OK : E_FAIL);
			if (SUCCEEDED(hr))
			{
				UINT uPixelAspRatio = propValue.bVal;

				if (uPixelAspRatio != 0)
				{
					// Need to calculate the ratio. The value in uPixelAspRatio 
					// allows specifying widest pixel 4:1 to the tallest pixel of 
					// 1:4 in increments of 1/64th
					FLOAT pixelAspRatio = (uPixelAspRatio + 15.f) / 64.f;

					// Calculate the image width and height in pixel based on the
					// pixel aspect ratio. Only shrink the image.
					if (pixelAspRatio > 1.f)
					{
						m_cxGifImagePixel = m_cxGifImage;
						m_cyGifImagePixel = static_cast<UINT>(m_cyGifImage / pixelAspRatio);
					}
					else
					{
						m_cxGifImagePixel = static_cast<UINT>(m_cxGifImage * pixelAspRatio);
						m_cyGifImagePixel = m_cyGifImage;
					}
				}
				else
				{
					// The value is 0, so its ratio is 1
					m_cxGifImagePixel = m_cxGifImage;
					m_cyGifImagePixel = m_cyGifImage;
				}
			}
			PropVariantClear(&propValue);
		}
	}

	// Get looping information
	if (SUCCEEDED(hr))
	{
		// First check to see if the application block in the Application Extension
		// contains "NETSCAPE2.0" and "ANIMEXTS1.0", which indicates the gif animation
		// has looping information associated with it.
		// 
		// If we fail to get the looping information, loop the animation infinitely.
		if (SUCCEEDED(pMetadataQueryReader->GetMetadataByName(
			L"/appext/application",
			&propValue)) &&
			propValue.vt == (VT_UI1 | VT_VECTOR) &&
			propValue.caub.cElems == 11 &&  // Length of the application block
			(!memcmp(propValue.caub.pElems, "NETSCAPE2.0", propValue.caub.cElems) ||
				!memcmp(propValue.caub.pElems, "ANIMEXTS1.0", propValue.caub.cElems)))
		{
			PropVariantClear(&propValue);

			hr = pMetadataQueryReader->GetMetadataByName(L"/appext/data", &propValue);
			if (SUCCEEDED(hr))
			{
				//  The data is in the following format:
				//  byte 0: extsize (must be > 1)
				//  byte 1: loopType (1 == animated gif)
				//  byte 2: loop count (least significant byte)
				//  byte 3: loop count (most significant byte)
				//  byte 4: set to zero
				if (propValue.vt == (VT_UI1 | VT_VECTOR) &&
					propValue.caub.cElems >= 4 &&
					propValue.caub.pElems[0] > 0 &&
					propValue.caub.pElems[1] == 1)
				{
					m_uTotalLoopCount = MAKEWORD(propValue.caub.pElems[2],
						propValue.caub.pElems[3]);

					// If the total loop count is not zero, we then have a loop count
					// If it is 0, then we repeat infinitely
					if (m_uTotalLoopCount != 0)
					{
						m_fHasLoop = TRUE;
					}
				}
			}
		}
	}

	PropVariantClear(&propValue);
	SafeRelease(&pMetadataQueryReader);
	return hr;
}

HRESULT GifReader::GetRawFrame(UINT uFrameIndex)
{
	IWICFormatConverter *pConverter = NULL;
	IWICBitmapFrameDecode *pWicFrame = NULL;
	IWICMetadataQueryReader *pFrameMetadataQueryReader = NULL;

	PROPVARIANT propValue;
	PropVariantInit(&propValue);

	// Retrieve the current frame
	HRESULT hr = m_pDecoder->GetFrame(uFrameIndex, &pWicFrame);
	if (SUCCEEDED(hr))
	{
		// Format convert to 32bppPBGRA which D2D expects
		hr = m_pIWICFactory->CreateFormatConverter(&pConverter);
	}

	if (SUCCEEDED(hr))
	{
		hr = pConverter->Initialize(
			pWicFrame,
			GUID_WICPixelFormat32bppPBGRA,
			WICBitmapDitherTypeNone,
			NULL,
			0.f,
			WICBitmapPaletteTypeCustom);
	}

	if (SUCCEEDED(hr))
	{
		// Create a D2DBitmap from IWICBitmapSource
		SafeRelease(&m_pRawFrame);
		hr = m_RenderTarget->CreateBitmapFromWicBitmap(
			pConverter,
			NULL,
			&m_pRawFrame);
	}

	if (SUCCEEDED(hr))
	{
		// Get Metadata Query Reader from the frame
		hr = pWicFrame->GetMetadataQueryReader(&pFrameMetadataQueryReader);
	}

	// Get the Metadata for the current frame
	if (SUCCEEDED(hr))
	{
		hr = pFrameMetadataQueryReader->GetMetadataByName(L"/imgdesc/Left", &propValue);
		if (SUCCEEDED(hr))
		{
			hr = (propValue.vt == VT_UI2 ? S_OK : E_FAIL);
			if (SUCCEEDED(hr))
			{
				m_framePosition.left = static_cast<FLOAT>(propValue.uiVal);
			}
			PropVariantClear(&propValue);
		}
	}

	if (SUCCEEDED(hr))
	{
		hr = pFrameMetadataQueryReader->GetMetadataByName(L"/imgdesc/Top", &propValue);
		if (SUCCEEDED(hr))
		{
			hr = (propValue.vt == VT_UI2 ? S_OK : E_FAIL);
			if (SUCCEEDED(hr))
			{
				m_framePosition.top = static_cast<FLOAT>(propValue.uiVal);
			}
			PropVariantClear(&propValue);
		}
	}

	if (SUCCEEDED(hr))
	{
		hr = pFrameMetadataQueryReader->GetMetadataByName(L"/imgdesc/Width", &propValue);
		if (SUCCEEDED(hr))
		{
			hr = (propValue.vt == VT_UI2 ? S_OK : E_FAIL);
			if (SUCCEEDED(hr))
			{
				m_framePosition.right = static_cast<FLOAT>(propValue.uiVal)
					+ m_framePosition.left;
			}
			PropVariantClear(&propValue);
		}
	}

	if (SUCCEEDED(hr))
	{
		hr = pFrameMetadataQueryReader->GetMetadataByName(L"/imgdesc/Height", &propValue);
		if (SUCCEEDED(hr))
		{
			hr = (propValue.vt == VT_UI2 ? S_OK : E_FAIL);
			if (SUCCEEDED(hr))
			{
				m_framePosition.bottom = static_cast<FLOAT>(propValue.uiVal)
					+ m_framePosition.top;
			}
			PropVariantClear(&propValue);
		}
	}

	if (SUCCEEDED(hr))
	{
		// Get delay from the optional Graphic Control Extension
		if (SUCCEEDED(pFrameMetadataQueryReader->GetMetadataByName(
			L"/grctlext/Delay",
			&propValue)))
		{
			hr = (propValue.vt == VT_UI2 ? S_OK : E_FAIL);
			if (SUCCEEDED(hr))
			{
				// Convert the delay retrieved in 10 ms units to a delay in 1 ms units
				hr = UIntMult(propValue.uiVal, 10, &m_uFrameDelay);
			}
			PropVariantClear(&propValue);
		}
		else
		{
			// Failed to get delay from graphic control extension. Possibly a
			// single frame image (non-animated gif)
			m_uFrameDelay = 0;
		}

		if (SUCCEEDED(hr))
		{
			// Insert an artificial delay to ensure rendering for gif with very small
			// or 0 delay.  This delay number is picked to match with most browsers' 
			// gif display speed.
			//
			// This will defeat the purpose of using zero delay intermediate frames in 
			// order to preserve compatibility. If this is removed, the zero delay 
			// intermediate frames will not be visible.
			if (m_uFrameDelay < 20)
			{
				m_uFrameDelay = 90;
			}
		}
	}

	if (SUCCEEDED(hr))
	{
		if (SUCCEEDED(pFrameMetadataQueryReader->GetMetadataByName(
			L"/grctlext/Disposal",
			&propValue)))
		{
			hr = (propValue.vt == VT_UI1) ? S_OK : E_FAIL;
			if (SUCCEEDED(hr))
			{
				m_uFrameDisposal = propValue.bVal;
			}
		}
		else
		{
			// Failed to get the disposal method, use default. Possibly a 
			// non-animated gif.
			m_uFrameDisposal = DM_UNDEFINED;
		}
	}

	PropVariantClear(&propValue);

	SafeRelease(&pConverter);
	SafeRelease(&pWicFrame);
	SafeRelease(&pFrameMetadataQueryReader);

	return hr;
}

HRESULT GifReader::GetBackgroundColor(
	IWICMetadataQueryReader *pMetadataQueryReader)
{
	DWORD dwBGColor;
	BYTE backgroundIndex = 0;
	WICColor rgColors[256];
	UINT cColorsCopied = 0;
	PROPVARIANT propVariant;
	PropVariantInit(&propVariant);
	IWICPalette *pWicPalette = NULL;

	// If we have a global palette, get the palette and background color
	HRESULT hr = pMetadataQueryReader->GetMetadataByName(
		L"/logscrdesc/GlobalColorTableFlag",
		&propVariant);
	if (SUCCEEDED(hr))
	{
		hr = (propVariant.vt != VT_BOOL || !propVariant.boolVal) ? E_FAIL : S_OK;
		PropVariantClear(&propVariant);
	}

	if (SUCCEEDED(hr))
	{
		// Background color index
		hr = pMetadataQueryReader->GetMetadataByName(
			L"/logscrdesc/BackgroundColorIndex",
			&propVariant);
		if (SUCCEEDED(hr))
		{
			hr = (propVariant.vt != VT_UI1) ? E_FAIL : S_OK;
			if (SUCCEEDED(hr))
			{
				backgroundIndex = propVariant.bVal;
			}
			PropVariantClear(&propVariant);
		}
	}

	// Get the color from the palette
	if (SUCCEEDED(hr))
	{
		hr = m_pIWICFactory->CreatePalette(&pWicPalette);
	}

	if (SUCCEEDED(hr))
	{
		// Get the global palette
		hr = m_pDecoder->CopyPalette(pWicPalette);
	}

	if (SUCCEEDED(hr))
	{
		hr = pWicPalette->GetColors(
			ARRAYSIZE(rgColors),
			rgColors,
			&cColorsCopied);
	}

	if (SUCCEEDED(hr))
	{
		// Check whether background color is outside range 
		hr = (backgroundIndex >= cColorsCopied) ? E_FAIL : S_OK;
	}

	if (SUCCEEDED(hr))
	{
		// Get the color in ARGB format
		dwBGColor = rgColors[backgroundIndex];

		// The background color is in ARGB format, and we want to 
		// extract the alpha value and convert it in FLOAT
		FLOAT alpha = (dwBGColor >> 24) / 255.f;
		m_backgroundColor = D2D1::ColorF(dwBGColor, alpha);
	}

	SafeRelease(&pWicPalette);
	return hr;
}

HRESULT GifReader::RestoreSavedFrame()
{
	HRESULT hr = S_OK;

	ID2D1Bitmap *pFrameToCopyTo = NULL;

	hr = m_pSavedFrame ? S_OK : E_FAIL;

	if (SUCCEEDED(hr))
	{
		hr = m_pFrameComposeRT->GetBitmap(&pFrameToCopyTo);
	}

	if (SUCCEEDED(hr))
	{
		// Copy the whole bitmap
		hr = pFrameToCopyTo->CopyFromBitmap(NULL, m_pSavedFrame, NULL);
	}

	SafeRelease(&pFrameToCopyTo);

	return hr;
}

HRESULT GifReader::ClearCurrentFrameArea()
{
	m_pFrameComposeRT->BeginDraw();

	// Clip the render target to the size of the raw frame
	m_pFrameComposeRT->PushAxisAlignedClip(
		&m_framePosition,
		D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);

	m_pFrameComposeRT->Clear(NULL);//m_backgroundColor);

	// Remove the clipping
	m_pFrameComposeRT->PopAxisAlignedClip();

	return m_pFrameComposeRT->EndDraw();
}

HRESULT GifReader::DisposeCurrentFrame()
{
	HRESULT hr = S_OK;

	switch (m_uFrameDisposal)
	{
		case DM_UNDEFINED:
		case DM_NONE:
			// We simply draw on the previous frames. Do nothing here.
			break;
		case DM_BACKGROUND:
			// Dispose background
			// Clear the area covered by the current raw frame with background color
			hr = ClearCurrentFrameArea();
			break;
		case DM_PREVIOUS:
			// Dispose previous
			// We restore the previous composed frame first
			hr = RestoreSavedFrame();
			break;
		default:
			// Invalid disposal method
			hr = E_FAIL;
	}

	return hr;
}

HRESULT GifReader::OverlayNextFrame()
{
	// Get Frame information
	HRESULT hr = GetRawFrame(m_uNextFrameIndex);
	if (SUCCEEDED(hr))
	{
		// For disposal 3 method, we would want to save a copy of the current
		// composed frame
		if (m_uFrameDisposal == DM_PREVIOUS)
		{
			hr = SaveComposedFrame();
		}
	}

	if (SUCCEEDED(hr))
	{
		// Start producing the next bitmap
		m_pFrameComposeRT->BeginDraw();

		// If starting a new animation loop
		if (m_uNextFrameIndex == 0)
		{
			// Draw background and increase loop count
			m_pFrameComposeRT->Clear(NULL);//m_backgroundColor);
			m_uLoopNumber++;
		}

		// Produce the next frame
		m_pFrameComposeRT->DrawBitmap(
			m_pRawFrame,
			m_framePosition);

		hr = m_pFrameComposeRT->EndDraw();
	}

	// To improve performance and avoid decoding/composing this frame in the 
	// following animation loops, the composed frame can be cached here in system 
	// or video memory.

	if (SUCCEEDED(hr))
	{
		// Increase the frame index by 1
		m_uNextFrameIndex = (++m_uNextFrameIndex) % m_cFrames;
	}

	return hr;
}

HRESULT GifReader::SaveComposedFrame()
{
	HRESULT hr = S_OK;

	ID2D1Bitmap *pFrameToBeSaved = NULL;

	hr = m_pFrameComposeRT->GetBitmap(&pFrameToBeSaved);
	if (SUCCEEDED(hr))
	{
		// Create the temporary bitmap if it hasn't been created yet 
		if (m_pSavedFrame == NULL)
		{
			D2D1_SIZE_U bitmapSize = pFrameToBeSaved->GetPixelSize();
			D2D1_BITMAP_PROPERTIES bitmapProp;
			pFrameToBeSaved->GetDpi(&bitmapProp.dpiX, &bitmapProp.dpiY);
			bitmapProp.pixelFormat = pFrameToBeSaved->GetPixelFormat();

			hr = m_pFrameComposeRT->CreateBitmap(
				bitmapSize,
				bitmapProp,
				&m_pSavedFrame);
		}
	}

	if (SUCCEEDED(hr))
	{
		// Copy the whole bitmap
		hr = m_pSavedFrame->CopyFromBitmap(NULL, pFrameToBeSaved, NULL);
	}

	SafeRelease(&pFrameToBeSaved);

	return hr;
}

HRESULT GifReader::StartCaptureLoop()
{
	m_CaptureTask = concurrency::create_task([this]() {
		if (!m_FramerateTimer) {
			m_FramerateTimer = make_unique<HighresTimer>();
		}
		do
		{
			EnterCriticalSection(&m_CriticalSection);
			ComposeNextFrame();
			CComPtr<ID2D1Bitmap> pFrameToRender = nullptr;
			HRESULT hr = m_pFrameComposeRT->GetBitmap(&pFrameToRender);
			if (SUCCEEDED(hr)) {
				m_RenderTarget->BeginDraw();
				m_RenderTarget->Clear(NULL);
				m_RenderTarget->DrawBitmap(pFrameToRender);
				m_RenderTarget->EndDraw();
			}
			LeaveCriticalSection(&m_CriticalSection);
			//Update timestamp and notify that there is a new sample available
			QueryPerformanceCounter(&m_LastSampleReceivedTimeStamp);
			SetEvent(m_NewFrameEvent);
			hr = m_FramerateTimer->WaitFor(MillisToHundredNanos(m_uFrameDelay));
			if (FAILED(hr)) {
				return;
			}
		} while (!EndOfAnimation() && m_cFrames > 1);
		});
	return S_OK;
}

HRESULT GifReader::ComposeNextFrame()
{
	HRESULT hr = S_OK;
	// Check to see if the render targets are initialized
	if (m_RenderTarget && m_pFrameComposeRT)
	{
		// Compose one frame
		hr = DisposeCurrentFrame();
		if (SUCCEEDED(hr))
		{
			hr = OverlayNextFrame();
		}

		// Keep composing frames until we see a frame with delay greater than
		// 0 (0 delay frames are the invisible intermediate frames), or until
		// we have reached the very last frame.
		while (SUCCEEDED(hr) && m_uFrameDelay == 0 && !IsLastFrame())
		{
			hr = DisposeCurrentFrame();
			if (SUCCEEDED(hr))
			{
				hr = OverlayNextFrame();
			}
		}
	}
	return hr;
}