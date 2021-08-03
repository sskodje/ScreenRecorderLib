#pragma once
#include "ScreenCaptureBase.h"

class duplication_capture : public ScreenCaptureBase
{
public:
	duplication_capture();
	~duplication_capture();

private:
	LPTHREAD_START_ROUTINE GetCaptureThreadProc();
};
