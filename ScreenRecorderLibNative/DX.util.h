#pragma once
#include "common_types.h"

HRESULT InitializeDx(_Out_ DX_RESOURCES *Data);
HRESULT GetOutputForDeviceName(_In_ std::wstring deviceName, _Outptr_result_maybenull_ IDXGIOutput * *ppOutput);
std::vector<IDXGIAdapter*> EnumDisplayAdapters();
void CleanDx(_Inout_ DX_RESOURCES* Data);
void SetViewPort(_In_ ID3D11DeviceContext * deviceContext, _In_ UINT Width, _In_ UINT Height);
void SetDebugName(_In_ ID3D11DeviceChild * child, _In_ const std::string & name);
HRESULT GetOutputForDeviceName(_In_ std::wstring deviceName, _Outptr_opt_result_maybenull_ IDXGIOutput **ppOutput);