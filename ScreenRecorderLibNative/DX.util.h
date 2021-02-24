#pragma once
#include "common_types.h"

HRESULT InitializeDx(_Out_ DX_RESOURCES *Data);
HRESULT GetOutputForDeviceName(_In_ std::wstring deviceName, _Outptr_opt_result_maybenull_ IDXGIOutput **ppOutput);
HRESULT GetMainOutput(_Outptr_result_maybenull_ IDXGIOutput **ppOutput);
HRESULT GetOutputDescsForDeviceNames(_In_ std::vector<std::wstring> deviceNames, _Out_ std::vector<DXGI_OUTPUT_DESC> *pOutputDescs);
void EnumOutputs(_Out_ std::vector<IDXGIOutput*> *pOutputs);
void GetCombinedRects(_In_ std::vector<RECT> inputs, _Out_ RECT *pOutRect, _Out_opt_ std::vector<SIZE> *pOffsets);
std::wstring GetMonitorName(HMONITOR monitor);
std::vector<IDXGIAdapter*> EnumDisplayAdapters();
void CleanDx(_Inout_ DX_RESOURCES* Data);
void SetViewPort(_In_ ID3D11DeviceContext *deviceContext, _In_ UINT Width, _In_ UINT Height);
void SetDebugName(_In_ ID3D11DeviceChild *child, _In_ const std::string &name);

// Compares two output descs according to desktop coordinates
inline bool compareOutputDesc(DXGI_OUTPUT_DESC desc1, DXGI_OUTPUT_DESC desc2)
{
	if (desc1.DesktopCoordinates.left != desc2.DesktopCoordinates.left) {
		return (desc1.DesktopCoordinates.left < desc2.DesktopCoordinates.left);
	}
	return (desc1.DesktopCoordinates.top < desc2.DesktopCoordinates.bottom);
}

// Compares two rects according
inline bool compareRects(RECT rect1, RECT rect2)
{
	if (rect1.left != rect2.left) {
		return (rect1.left < rect2.left);
	}
	return (rect1.top < rect2.bottom);
}