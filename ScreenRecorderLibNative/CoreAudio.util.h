#pragma once
#include "CommonTypes.h"
#include <mmdeviceapi.h>
#include <map>

struct AUDIO_DEVICE {
	std::wstring DeviceId;
	std::wstring FriendlyName;
	bool IsDefaultDevice;
	AUDIO_DEVICE(std::wstring id, std::wstring friendlyName,std::optional<bool> isDefaultDevice = std::nullopt) {
		DeviceId = id;
		FriendlyName = friendlyName;
		IsDefaultDevice = isDefaultDevice.value_or(false);
	}
};

HRESULT GetDefaultAudioDevice(_In_ EDataFlow flow, _Outptr_ IMMDevice **ppMMDevice);
HRESULT ListAudioDevices(_In_ EDataFlow flow, _Out_ std::vector<AUDIO_DEVICE> *devices);
HRESULT GetActiveAudioDevice(_In_ LPCWSTR szDeviceId, _In_ EDataFlow flow, _Outptr_ IMMDevice **ppMMDevice);
HRESULT GetAudioDevice(_In_ LPCWSTR szDeviceId, _Outptr_ IMMDevice **ppMMDevice);

HRESULT GetAudioDeviceFlow(_In_ IMMDevice *pMMDevice, _Out_ EDataFlow *pFlow);
HRESULT GetAudioDeviceFriendlyName(_In_ IMMDevice *pDevice, _Out_ std::wstring *deviceName);
HRESULT GetAudioDeviceFriendlyName(_In_ LPCWSTR pwstrId, _Out_ std::wstring *deviceName);
bool IsAudioClientActivationParamsAvailable();

EDataFlow AudioClientKindToDeviceFlow(AudioClientKind kind);