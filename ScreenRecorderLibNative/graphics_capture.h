#pragma once
#include <functional>
#include <d3d11_4.h>
#include <dxgi1_6.h>
#include <d2d1_3.h>
#include <wincodec.h>
#include <Unknwn.h>
#include <inspectable.h>

#include "common_types.h"
#include "DX.util.h"
#include "mouse_pointer.h"
#include "capture_base.h"

class graphics_capture : public capture_base
{
public:
	graphics_capture();
	~graphics_capture();
	RECT GetOutputRect();
private:
	LPTHREAD_START_ROUTINE GetCaptureThreadProc();
};