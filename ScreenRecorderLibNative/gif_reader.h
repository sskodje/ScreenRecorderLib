#pragma once
#include <d2d1.h>
#include <wincodec.h>
#include <concrt.h>
#include <ppltasks.h> 
#include "common_types.h"
#include "DX.util.h"
#include "highres_timer.h"
#include "reader_base.h"
class gif_reader : public reader_base
{
public:

	gif_reader();
	~gif_reader();
	virtual HRESULT StartCapture(_In_ std::wstring source) override;
	virtual HRESULT StopCapture();
	virtual HRESULT GetFrame(_Inout_ FRAME_INFO *pFrameInfo, _In_ int timeoutMs) override;
	virtual HRESULT Initialize(_In_ DX_RESOURCES* Data) override;

private:
	enum DISPOSAL_METHODS
	{
		DM_UNDEFINED = 0,
		DM_NONE = 1,
		DM_BACKGROUND = 2,
		DM_PREVIOUS = 3
	};
	HRESULT Initialize();
	HRESULT CreateDeviceResources();
	HRESULT ResizeFrameBuffer(FRAME_INFO* FrameInfo, int bufferSize);

	HRESULT GetRawFrame(UINT uFrameIndex);
	HRESULT GetGlobalMetadata();
	HRESULT GetBackgroundColor(IWICMetadataQueryReader *pMetadataQueryReader);

	HRESULT StartCaptureLoop();
	HRESULT ComposeNextFrame();
	HRESULT DisposeCurrentFrame();
	HRESULT OverlayNextFrame();

	HRESULT SaveComposedFrame();
	HRESULT RestoreSavedFrame();
	HRESULT ClearCurrentFrameArea();

	BOOL IsLastFrame()
	{
		return (m_uNextFrameIndex == 0);
	}

	BOOL EndOfAnimation()
	{
		return m_fHasLoop && IsLastFrame() && m_uLoopNumber == m_uTotalLoopCount + 1;
	}

private:
	HANDLE m_NewFrameEvent;
	CRITICAL_SECTION m_CriticalSection;
	Concurrency::task<void> m_CaptureTask = concurrency::task_from_result();
	highres_timer *m_FramerateTimer;
	ID3D11Device *m_Device;
	ID3D11DeviceContext *m_DeviceContext;
	LARGE_INTEGER m_LastGrabTimeStamp;


	ID3D11Texture2D            *m_RenderTexture;
	ID2D1Factory               *m_pD2DFactory;
	ID2D1BitmapRenderTarget    *m_pFrameComposeRT;
	ID2D1RenderTarget          *m_RenderTarget;
	ID2D1Bitmap                *m_pRawFrame;
	ID2D1Bitmap                *m_pSavedFrame;          // The temporary bitmap used for disposal 3 method
	D2D1_COLOR_F                m_backgroundColor;

	IWICImagingFactory         *m_pIWICFactory;
	IWICBitmapDecoder          *m_pDecoder;

	UINT    m_uNextFrameIndex;
	UINT    m_uTotalLoopCount;  // The number of loops for which the animation will be played
	UINT    m_uLoopNumber;      // The current animation loop number (e.g. 1 when the animation is first played)
	BOOL    m_fHasLoop;         // Whether the gif has a loop
	UINT    m_cFrames;
	UINT    m_uFrameDisposal;
	UINT    m_uFrameDelay;
	UINT    m_cxGifImage;
	UINT    m_cyGifImage;
	UINT    m_cxGifImagePixel;  // Width of the displayed image in pixel calculated using pixel aspect ratio
	UINT    m_cyGifImagePixel;  // Height of the displayed image in pixel calculated using pixel aspect ratio
	D2D1_RECT_F m_framePosition;
};