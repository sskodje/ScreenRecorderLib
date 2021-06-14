#pragma once
#include <functional>
#include <d3d11_4.h>
#include <dxgi1_6.h>
#include <d2d1_3.h>
#include <wincodec.h>
#include <Unknwn.h>
#include <inspectable.h>

#include "CommonTypes.h"
#include "DX.util.h"
#include "MouseManager.h"
#include "ScreenCaptureBase.h"

class WindowsGraphicsCapture : public ScreenCaptureBase
{
public:
	WindowsGraphicsCapture();
	~WindowsGraphicsCapture();
	RECT GetOutputRect();
private:
	LPTHREAD_START_ROUTINE GetCaptureThreadProc();
};