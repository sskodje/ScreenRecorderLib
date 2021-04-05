#pragma once
#include "capture_base.h"
#include <map>

class duplication_capture : public capture_base
{
public:
	duplication_capture();
	~duplication_capture();

private:
	LPTHREAD_START_ROUTINE GetCaptureThreadProc();
};
