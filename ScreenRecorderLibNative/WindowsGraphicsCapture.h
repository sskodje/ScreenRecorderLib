#pragma once
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