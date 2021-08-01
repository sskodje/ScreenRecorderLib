#include "WindowsGraphicsManager.h"
#include "Log.h"
#include "DX.util.h"
#include "WindowsGraphicsCapture.util.h"
#include "Cleanup.h"

using namespace winrt::Windows::Graphics::Capture;
using namespace std;

WindowsGraphicsManager::WindowsGraphicsManager() :
	m_closed{ false },
	m_item(nullptr),
	m_framePool(nullptr),
	m_session(nullptr),
	m_LastFrameRect{},
	m_Device(nullptr),
	m_DeviceContext(nullptr),
	m_PixelFormat{},
	m_TextureManager(nullptr),
	m_HaveDeliveredFirstFrame(false)
{
}

WindowsGraphicsManager::~WindowsGraphicsManager()
{
	Close();
	CleanRefs();
}

//
// Clean all references
//
void WindowsGraphicsManager::CleanRefs()
{
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
}


HRESULT WindowsGraphicsManager::Initialize(_In_ DX_RESOURCES *pData, _In_ winrt::Windows::Graphics::Capture::GraphicsCaptureItem captureItem, _In_ bool isCursorCaptureEnabled, _In_ winrt::Windows::Graphics::DirectX::DirectXPixelFormat pixelFormat)
{
	m_item = captureItem;
	m_PixelFormat = pixelFormat;
	m_Device = pData->Device;
	m_DeviceContext = pData->Context;

	m_Device->AddRef();
	m_DeviceContext->AddRef();

	m_TextureManager = make_unique<TextureManager>();
	m_TextureManager->Initialize(m_DeviceContext, m_Device);

	// Get DXGI device
	CComPtr<IDXGIDevice> DxgiDevice = nullptr;
	HRESULT hr = m_Device->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void **>(&DxgiDevice));
	if (FAILED(hr))
	{
		LOG_ERROR(L"Failed to QI for DXGI Device");
		return hr;
	}
	auto direct3DDevice = Graphics::Capture::Util::CreateDirect3DDevice(DxgiDevice);
	// Creating our frame pool with 'Create' instead of 'CreateFreeThreaded'
	// means that the frame pool's FrameArrived event is called on the thread
	// the frame pool was created on. This also means that the creating thread
	// must have a DispatcherQueue. If you use this method, it's best not to do
	// it on the UI thread. 
	m_framePool = Direct3D11CaptureFramePool::Create(direct3DDevice, m_PixelFormat, 1, m_item.Size());
	m_session = m_framePool.CreateCaptureSession(m_item);

	WINRT_ASSERT(m_session != nullptr);
	m_session.IsCursorCaptureEnabled(isCursorCaptureEnabled);
	m_session.StartCapture();
	return S_OK;
}

HRESULT WindowsGraphicsManager::ProcessFrame(_Inout_ GRAPHICS_FRAME_DATA *pData, _Inout_ ID3D11Texture2D *pSharedSurf, _In_ INT offsetX, _In_  INT offsetY, _In_ RECT &destinationRect, _In_opt_ const std::optional<RECT> &sourceRect)
{
	HRESULT hr = S_FALSE;
	if (pData->IsIconic) {
		BlankFrame(pSharedSurf, MakeRectEven(destinationRect), offsetX, offsetY);
		return hr;
	}
	CComPtr<ID3D11Texture2D> processedTexture = pData->Frame;
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

	RECT finalFrameRect = MakeRectEven(RECT
		{
			destinationRect.left,
			destinationRect.top,
			destinationRect.left + (LONG)frameDesc.Width,
			destinationRect.top + (LONG)frameDesc.Height
		});

	if (!IsRectEmpty(&m_LastFrameRect) && !EqualRect(&finalFrameRect, &m_LastFrameRect)) {
		BlankFrame(pSharedSurf, m_LastFrameRect, offsetX, offsetY);
	}

	D3D11_BOX Box;
	Box.front = 0;
	Box.back = 1;
	Box.left = 0;
	Box.top = 0;
	Box.right = MakeEven(frameDesc.Width);
	Box.bottom = MakeEven(frameDesc.Height);

	m_DeviceContext->CopySubresourceRegion(pSharedSurf, 0, finalFrameRect.left + offsetX + leftMargin, finalFrameRect.top + offsetY + topMargin, 0, processedTexture, 0, &Box);
	m_LastFrameRect = finalFrameRect;
	return S_OK;
}

HRESULT WindowsGraphicsManager::BlankFrame(_Inout_ ID3D11Texture2D *pSharedSurf, _In_ RECT rect, _In_ INT offsetX, _In_  INT offsetY) {
	int width = RectWidth(rect);
	int height = RectHeight(rect);
	D3D11_BOX Box{};
	// Copy back to shared surface
	Box.right = width;
	Box.bottom = height;
	Box.back = 1;

	CComPtr<ID3D11Texture2D> pBlankFrame;
	D3D11_TEXTURE2D_DESC desc;
	pSharedSurf->GetDesc(&desc);
	desc.MiscFlags = 0;
	desc.Width = width;
	desc.Height = height;
	HRESULT hr = m_Device->CreateTexture2D(&desc, nullptr, &pBlankFrame);
	if (SUCCEEDED(hr)) {
		m_DeviceContext->CopySubresourceRegion(pSharedSurf, 0, rect.left + offsetX, rect.top + offsetY, 0, pBlankFrame, 0, &Box);
	}
	return S_OK;
}

HRESULT WindowsGraphicsManager::GetFrame(_Inout_ GRAPHICS_FRAME_DATA *pData)
{
	MeasureExecutionTime measureGetFrame(L"WindowsGraphicsManager::GetFrame");
	Direct3D11CaptureFrame frame = m_framePool.TryGetNextFrame();
	if (frame) {
		auto surfaceTexture = Graphics::Capture::Util::GetDXGIInterfaceFromObject<ID3D11Texture2D>(frame.Surface());
		D3D11_TEXTURE2D_DESC desc;
		surfaceTexture->GetDesc(&desc);
		SafeRelease(&pData->Frame);

		HRESULT hr = m_Device->CreateTexture2D(&desc, nullptr, &pData->Frame);
		if (FAILED(hr))
		{
			return hr;
		}


		m_DeviceContext->CopyResource(pData->Frame, surfaceTexture.get());

		if (frame.ContentSize().Width != pData->ContentSize.cx
				|| frame.ContentSize().Height != pData->ContentSize.cy) {
			//The source has changed size, so we must recreate the frame pool with the new size.
			CComPtr<IDXGIDevice> DxgiDevice = nullptr;
			hr = m_Device->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void **>(&DxgiDevice));
			if (FAILED(hr))
			{
				LOG_ERROR(L"Failed to QI for DXGI Device");
				return hr;
			}
			measureGetFrame.SetName(L"WindowsGraphicsManager::GetFrame recreated");
			auto direct3DDevice = Graphics::Capture::Util::CreateDirect3DDevice(DxgiDevice);
			m_framePool.Recreate(direct3DDevice, m_PixelFormat, 1, frame.ContentSize());
			//Some times the size of the first frame is wrong when recording windows, so we just skip it and get a new after resizing the frame pool.
			if (pData->IsWindow
				&& !m_HaveDeliveredFirstFrame) {
				m_HaveDeliveredFirstFrame = true;
				return GetFrame(pData);
			}
		}
		pData->ContentSize.cx = frame.ContentSize().Width;
		pData->ContentSize.cy = frame.ContentSize().Height;
		m_HaveDeliveredFirstFrame = true;
		frame.Close();
		return S_OK;
	}
	return DXGI_ERROR_WAIT_TIMEOUT;
}

SIZE WindowsGraphicsManager::ItemSize()
{
	SIZE size{ };
	size.cx = m_item.Size().Width;
	size.cy = m_item.Size().Height;
	return size;
}

void WindowsGraphicsManager::Close()
{
	auto expected = false;
	if (m_closed.compare_exchange_strong(expected, true))
	{
		m_session.Close();
		m_framePool.Close();

		m_framePool = nullptr;
		m_session = nullptr;
	}
}