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
	graphics_capture(_In_ ID3D11Device* pDevice, _In_ ID3D11DeviceContext *pDeviceContext);
	~graphics_capture();
	HRESULT StartCapture(_In_ std::vector<RECORDING_SOURCE> sources, _In_ std::vector<RECORDING_OVERLAY> overlays, _In_  HANDLE hUnexpectedErrorEvent, _In_  HANDLE hExpectedErrorEvent);
};