#include "CaptureBase.h"
#include "cleanup.h"

using namespace std;

CaptureBase::CaptureBase() :
	m_LastGrabTimeStamp{},
	m_Device(nullptr),
	m_DeviceContext(nullptr),
	m_RecordingSource(nullptr),
	m_TextureManager(nullptr),
	m_FrameDataCallbackTexture(nullptr)
{
	RtlZeroMemory(&m_FrameDataCallbackTextureDesc, sizeof(m_FrameDataCallbackTextureDesc));
}
CaptureBase::~CaptureBase()
{
	SafeRelease(&m_Device);
	SafeRelease(&m_DeviceContext);
	SafeRelease(&m_FrameDataCallbackTexture);
}
SIZE CaptureBase::GetContentOffset(_In_ ContentAnchor anchor, _In_ RECT parentRect, _In_ RECT contentRect)
{
	int leftMargin;
	int topMargin;
	switch (anchor)
	{
		case ContentAnchor::TopLeft:
		default: {
			leftMargin = 0;
			topMargin = 0;
			break;
		}
		case ContentAnchor::TopRight: {
			leftMargin = (int)max(0.0, round(((double)RectWidth(parentRect) - (double)RectWidth(contentRect))));
			topMargin = 0;
			break;
		}
		case ContentAnchor::Center: {
			leftMargin = (int)max(0.0, round(((double)RectWidth(parentRect) - (double)RectWidth(contentRect))) / 2);
			topMargin = (int)max(0.0, round(((double)RectHeight(parentRect) - (double)RectHeight(contentRect))) / 2);
			break;
		}
		case ContentAnchor::BottomLeft: {
			leftMargin = 0;
			topMargin = (int)max(0.0, round(((double)RectHeight(parentRect) - (double)RectHeight(contentRect))));
			break;
		}
		case ContentAnchor::BottomRight: {
			leftMargin = (int)max(0.0, round(((double)RectWidth(parentRect) - (double)RectWidth(contentRect))));
			topMargin = (int)max(0.0, round(((double)RectHeight(parentRect) - (double)RectHeight(contentRect))));
			break;
		}
	}
	return SIZE{ leftMargin,topMargin };
}

HRESULT CaptureBase::SendBitmapCallback(_In_ ID3D11Texture2D *pTexture) {
	HRESULT hr = S_FALSE;
	ID3D11Texture2D *pProcessedTexture = nullptr;
	if (m_RecordingSource->IsVideoFramePreviewEnabled.value_or(false) && m_RecordingSource->HasRegisteredCallbacks()) {
		D3D11_TEXTURE2D_DESC textureDesc;
		pTexture->GetDesc(&textureDesc);
		if (m_RecordingSource->VideoFramePreviewSize.has_value()) {
			long cx = m_RecordingSource->VideoFramePreviewSize.value().cx;
			long cy = m_RecordingSource->VideoFramePreviewSize.value().cy;
			if (cx > 0 && cy == 0) {
				cy = static_cast<long>(round((static_cast<double>(textureDesc.Height) / static_cast<double>(textureDesc.Width)) * cx));
			}
			else if (cx == 0 && cy > 0) {
				cx = static_cast<long>(round((static_cast<double>(textureDesc.Width) / static_cast<double>(textureDesc.Height)) * cy));
			}
			RETURN_ON_BAD_HR(hr = m_TextureManager->ResizeTexture(pTexture, SIZE{ cx,cy }, TextureStretchMode::Uniform, &pProcessedTexture));
			pProcessedTexture->GetDesc(&textureDesc);
		}
		else {
			pProcessedTexture = pTexture;
			pProcessedTexture->AddRef();
		}
		ReleaseOnExit releaseProcessedTexture(pProcessedTexture);
		int width = textureDesc.Width;
		int height = textureDesc.Height;

		if (m_FrameDataCallbackTextureDesc.Width != width || m_FrameDataCallbackTextureDesc.Height != height) {
			SafeRelease(&m_FrameDataCallbackTexture);
			textureDesc.Usage = D3D11_USAGE_STAGING;
			textureDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
			textureDesc.MiscFlags = 0;
			textureDesc.BindFlags = 0;
			RETURN_ON_BAD_HR(m_Device->CreateTexture2D(&textureDesc, nullptr, &m_FrameDataCallbackTexture));
			m_FrameDataCallbackTextureDesc = textureDesc;
		}

		m_DeviceContext->CopyResource(m_FrameDataCallbackTexture, pProcessedTexture);
		D3D11_MAPPED_SUBRESOURCE map;
		m_DeviceContext->Map(m_FrameDataCallbackTexture, 0, D3D11_MAP_READ, 0, &map);

		int bytesPerPixel = map.RowPitch / width;
		int len = map.DepthPitch;
		int stride = map.RowPitch;
		BYTE *data = static_cast<BYTE *>(map.pData);

		m_RecordingSource->NotifyNewFrameDataCallbacks(abs(stride), data, len, width, height);
		m_DeviceContext->Unmap(m_FrameDataCallbackTexture, 0);
	}
	return hr;
}