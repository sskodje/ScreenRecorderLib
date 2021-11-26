#pragma once
#include "CaptureBase.h"
#include "CommonTypes.h"
class ScreenCaptureBase abstract: public CaptureBase
{
public:
	ScreenCaptureBase() {};
	virtual ~ScreenCaptureBase() {};
	virtual HRESULT WriteNextFrameToSharedSurface(_In_ DWORD timeoutMillis, _Inout_ ID3D11Texture2D *pSharedSurf, INT offsetX, INT offsetY, _In_ RECT destinationRect, _In_opt_ const std::optional<RECT> &sourceRect = std::nullopt) abstract;
};