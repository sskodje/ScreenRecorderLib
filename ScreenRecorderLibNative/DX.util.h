#pragma once
#include "CommonTypes.h"

//UID to identify "all monitors". This is a random value.
constexpr auto ALL_MONITORS_ID = L"ALL_MONITORS";

HRESULT InitializeDx(_In_opt_ IDXGIAdapter *adapter, _Out_ DX_RESOURCES *Data);
HRESULT GetAdapterForDevice(_In_ ID3D11Device *pDevice, _Outptr_ IDXGIAdapter **ppAdapter);
HRESULT GetAdapterForDeviceName(_In_ std::wstring deviceName, _Outptr_opt_result_maybenull_ IDXGIAdapter **ppAdapter);
HRESULT GetOutputForDeviceName(_In_ std::wstring deviceName, _Outptr_opt_result_maybenull_ IDXGIOutput **ppOutput);
HRESULT GetMainOutput(_Outptr_result_maybenull_ IDXGIOutput **ppOutput);
HRESULT GetOutputDescsForDeviceNames(_In_ std::vector<std::wstring> deviceNames, _Out_ std::vector<DXGI_OUTPUT_DESC> *pOutputDescs);
HRESULT GetOutputRectsForRecordingSources(_In_ std::vector<RECORDING_SOURCE> sources, _Out_ std::vector<std::pair<RECORDING_SOURCE, RECT>> *outputs);
void EnumOutputs(_Out_ std::vector<IDXGIOutput *> *pOutputs);
void GetCombinedRects(_In_ std::vector<RECT> inputs, _Out_ RECT *pOutRect, _Out_opt_ std::vector<SIZE> *pOffsets);
std::wstring GetMonitorName(HMONITOR monitor);
std::vector<IDXGIAdapter *> EnumDisplayAdapters();
void CleanDx(_Inout_ DX_RESOURCES *Data);
void SetViewPort(_In_ ID3D11DeviceContext *deviceContext, _In_ UINT Width, _In_ UINT Height);
void SetDebugName(_In_ ID3D11DeviceChild *child, _In_ const std::string &name);
