#pragma once
#include "capture_base.h"
#include <map>

class duplication_capture : public capture_base
{
public:
	duplication_capture(_In_ ID3D11Device *pDevice, _In_ ID3D11DeviceContext *pImmediateContext);
	~duplication_capture();
	HRESULT StartCapture(_In_ std::vector<RECORDING_SOURCE> sources, _In_ std::vector<RECORDING_OVERLAY> overlays, _In_  HANDLE hUnexpectedErrorEvent, _In_  HANDLE hExpectedErrorEvent);
	HRESULT AcquireNextFrame(_In_ DWORD timeoutMillis, _Inout_ CAPTURED_FRAME *pFrame);
};
