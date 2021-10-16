#include "ImageReader.h"
#include "screengrab.h"
#include "util.h"
#include "cleanup.h"

using namespace std;

ImageReader::ImageReader() :
	m_TextureManager(nullptr),
	m_Texture(nullptr),
	m_NativeSize{},
	m_LastGrabTimeStamp{},
	m_Device(nullptr),
	m_DeviceContext(nullptr),
	m_RecordingSource(nullptr)
{
}

ImageReader::~ImageReader()
{
	SafeRelease(&m_Device);
	SafeRelease(&m_DeviceContext);
}

HRESULT ImageReader::Initialize(_In_ ID3D11DeviceContext *pDeviceContext, _In_ ID3D11Device *pDevice)
{
	m_Device = pDevice;
	m_DeviceContext = pDeviceContext;

	m_Device->AddRef();
	m_DeviceContext->AddRef();

	m_TextureManager = make_unique<TextureManager>();
	return m_TextureManager->Initialize(pDeviceContext, pDevice);
}

HRESULT ImageReader::StartCapture(_In_ RECORDING_SOURCE_BASE &source)
{
	m_RecordingSource = &source;
	return InitializeDecoder(source.SourcePath);
}

HRESULT ImageReader::GetNativeSize(_In_ RECORDING_SOURCE_BASE &recordingSource, _Out_ SIZE *nativeMediaSize)
{
	HRESULT hr = S_OK;
	MeasureExecutionTime measure(L"ImageReader GetNativeSize");
	if (!m_Texture) {
		CComPtr<IWICBitmapSource> pBitmap;
		HRESULT hr = CreateWICBitmapFromFile(recordingSource.SourcePath.c_str(), GUID_WICPixelFormat32bppBGRA, &pBitmap);
		if (FAILED(hr)) {
			return hr;
		}

		// Copy the 32bpp RGBA image to a buffer for further processing.
		UINT width, height;
		RETURN_ON_BAD_HR(hr = pBitmap->GetSize(&width, &height));
		*nativeMediaSize = SIZE{ static_cast<long>(width),static_cast<long>(height) };
	}
	else {
		*nativeMediaSize = m_NativeSize;
	}
	return hr;
}

HRESULT ImageReader::AcquireNextFrame(_In_ DWORD timeoutMillis, _Outptr_opt_ ID3D11Texture2D **ppFrame)
{
	if (m_Texture && m_LastGrabTimeStamp.QuadPart == 0) {
		if (ppFrame) {
			CComPtr<ID3D11Texture2D> pStagingTexture = nullptr;
			D3D11_TEXTURE2D_DESC desc;
			m_Texture->GetDesc(&desc);
			desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			desc.MiscFlags = 0;
			desc.Usage = D3D11_USAGE_DEFAULT;

			RETURN_ON_BAD_HR(m_Device->CreateTexture2D(&desc, nullptr, &pStagingTexture));
			m_DeviceContext->CopyResource(pStagingTexture, m_Texture);
			*ppFrame = pStagingTexture;
			(*ppFrame)->AddRef();
			QueryPerformanceCounter(&m_LastGrabTimeStamp);
		}
		return S_OK;
	}
	else {
		return E_ABORT;
	}
}

HRESULT ImageReader::WriteNextFrameToSharedSurface(_In_ DWORD timeoutMillis, _Inout_ ID3D11Texture2D *pSharedSurf, INT offsetX, INT offsetY, _In_ RECT destinationRect)
{
	CComPtr<ID3D11Texture2D> pProcessedTexture;
	HRESULT hr = AcquireNextFrame(timeoutMillis, &pProcessedTexture);
	RETURN_ON_BAD_HR(hr);
	D3D11_TEXTURE2D_DESC frameDesc;
	pProcessedTexture->GetDesc(&frameDesc);
	RECORDING_SOURCE *recordingSource = dynamic_cast<RECORDING_SOURCE *>(m_RecordingSource);
	if (recordingSource && recordingSource->SourceRect.has_value()
		&& IsValidRect(recordingSource->SourceRect.value())
		&& (RectWidth(recordingSource->SourceRect.value()) != frameDesc.Width || (RectHeight(recordingSource->SourceRect.value()) != frameDesc.Height))) {
		ID3D11Texture2D *pCroppedTexture;
		RETURN_ON_BAD_HR(hr = m_TextureManager->CropTexture(pProcessedTexture, recordingSource->SourceRect.value(), &pCroppedTexture));
		if (hr == S_OK) {
			pProcessedTexture.Release();
			pProcessedTexture.Attach(pCroppedTexture);
		}
	}
	pProcessedTexture->GetDesc(&frameDesc);

	int leftMargin = 0;
	int topMargin = 0;
	RECT contentRect = destinationRect;
	if (RectWidth(destinationRect) != frameDesc.Width || RectHeight(destinationRect) != frameDesc.Height) {
		ID3D11Texture2D *pResizedTexture;
		RETURN_ON_BAD_HR(hr = m_TextureManager->ResizeTexture(pProcessedTexture, SIZE{ RectWidth(destinationRect),RectHeight(destinationRect) }, m_RecordingSource->Stretch, &pResizedTexture, &contentRect));
		pProcessedTexture.Release();
		pProcessedTexture.Attach(pResizedTexture);
	}

	long left = destinationRect.left + offsetX + leftMargin;
	long top = destinationRect.top + offsetY + topMargin;
	long right = left + MakeEven(frameDesc.Width);
	long bottom = top + MakeEven(frameDesc.Height);
	m_TextureManager->DrawTexture(pSharedSurf, pProcessedTexture, RECT{ left,top,right,bottom });
	return hr;
}


HRESULT ImageReader::InitializeDecoder(_In_ std::wstring source)
{
	CComPtr<IWICBitmapSource> pBitmap;
	HRESULT hr = CreateWICBitmapFromFile(source.c_str(), GUID_WICPixelFormat32bppBGRA, &pBitmap);
	if (FAILED(hr)) {
		return hr;
	}

	// Copy the 32bpp RGBA image to a buffer for further processing.
	UINT width, height;
	RETURN_ON_BAD_HR(hr = pBitmap->GetSize(&width, &height));

	const unsigned bytesPerPixel = 4;
	const unsigned stride = width * bytesPerPixel;
	const unsigned bitmapSize = width * height * bytesPerPixel;
	if (bitmapSize <= 0) {
		return E_FAIL;
	}
	BYTE *pFrameBuffer = new (std::nothrow) BYTE[bitmapSize];
	DeleteArrayOnExit deleteOnExit(pFrameBuffer);
	RETURN_ON_BAD_HR(hr = pBitmap->CopyPixels(nullptr, stride, bitmapSize, pFrameBuffer));
	RETURN_ON_BAD_HR(m_TextureManager->CreateTextureFromBuffer(pFrameBuffer, stride, width, height, &m_Texture));
	m_NativeSize = SIZE{ static_cast<long>(width),static_cast<long>(height) };
	return hr;
}