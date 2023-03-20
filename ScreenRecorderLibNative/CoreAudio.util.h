#pragma once
#include "CommonTypes.h"
#include <mmdeviceapi.h>
#include <map>

HRESULT GetDefaultAudioDevice(_In_ EDataFlow flow, _Outptr_ IMMDevice **ppMMDevice);
HRESULT ListAudioDevices(_In_ EDataFlow flow, _Out_ std::map<std::wstring, std::wstring> *devices);
HRESULT GetActiveAudioDevice(_In_ LPCWSTR szDeviceId, _In_ EDataFlow flow, _Outptr_ IMMDevice **ppMMDevice);
HRESULT GetAudioDevice(_In_ LPCWSTR szDeviceId, _Outptr_ IMMDevice **ppMMDevice);

HRESULT GetAudioDeviceFlow(_In_ IMMDevice *pMMDevice, _Out_ EDataFlow *pFlow);
HRESULT GetAudioDeviceFriendlyName(_In_ IMMDevice *pDevice, _Out_ std::wstring *deviceName);
HRESULT GetAudioDeviceFriendlyName(_In_ LPCWSTR pwstrId, _Out_ std::wstring *deviceName);