#include "CoreAudio.util.h"
#include "cleanup.h"
#include <functiondiscoverykeys_devpkey.h>

HRESULT GetDefaultAudioDevice(_In_ EDataFlow flow, _Outptr_ IMMDevice **ppMMDevice) {
	HRESULT hr = S_OK;
	CComPtr<IMMDeviceEnumerator> pMMDeviceEnumerator;

	// activate a device enumerator
	hr = CoCreateInstance(
		__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL,
		__uuidof(IMMDeviceEnumerator),
		(void **)&pMMDeviceEnumerator
	);
	if (FAILED(hr)) {
		LOG_ERROR(L"CoCreateInstance(IMMDeviceEnumerator) failed: hr = 0x%08x", hr);
		return hr;
	}
	// get the default endpoint for chosen flow (should be either eCapture or eRender)
	hr = pMMDeviceEnumerator->GetDefaultAudioEndpoint(flow, eConsole, ppMMDevice);
	if (hr == E_NOTFOUND) {
		//No default audio device found on system
		return S_FALSE;
	}
	else if (FAILED(hr)) {
		LOG_ERROR(L"IMMDeviceEnumerator::GetDefaultAudioEndpoint failed: hr = 0x%08x", hr);
		return hr;
	}
	return S_OK;
}

HRESULT ListAudioDevices(_In_ EDataFlow flow, _Out_ std::map<std::wstring, std::wstring> *devices) {
	HRESULT hr = S_OK;
	*devices = std::map<std::wstring, std::wstring>();
	// get an enumerator
	CComPtr<IMMDeviceEnumerator> pMMDeviceEnumerator;

	hr = CoCreateInstance(
		__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL,
		__uuidof(IMMDeviceEnumerator),
		(void **)&pMMDeviceEnumerator
	);

	if (FAILED(hr)) {
		LOG_ERROR(L"CoCreateInstance(IMMDeviceEnumerator) failed: hr = 0x%08x", hr);
		return hr;
	}

	CComPtr<IMMDeviceCollection> pMMDeviceCollection;
	// get all the active endpoints for chosen flow
	hr = pMMDeviceEnumerator->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, &pMMDeviceCollection);

	if (FAILED(hr)) {
		LOG_ERROR(L"IMMDeviceEnumerator::EnumAudioEndpoints failed: hr = 0x%08x", hr);
		return hr;
	}

	UINT count;
	hr = pMMDeviceCollection->GetCount(&count);
	if (FAILED(hr)) {
		LOG_ERROR(L"IMMDeviceCollection::GetCount failed: hr = 0x%08x", hr);
		return hr;
	}
	LOG_INFO(L"Active render endpoints found: %u", count);

	for (UINT i = 0; i < count; i++) {
		CComPtr<IMMDevice> pMMDevice;

		// get the "n"th device
		hr = pMMDeviceCollection->Item(i, &pMMDevice);
		if (FAILED(hr)) {
			LOG_ERROR(L"IMMDeviceCollection::Item failed: hr = 0x%08x", hr);
			return hr;
		}

		// open the property store on that device
		CComPtr<IPropertyStore> pPropertyStore;
		hr = pMMDevice->OpenPropertyStore(STGM_READ, &pPropertyStore);
		if (FAILED(hr)) {
			LOG_ERROR(L"IMMDevice::OpenPropertyStore failed: hr = 0x%08x", hr);
			return hr;
		}

		// get the long name property
		PROPVARIANT pv; PropVariantInit(&pv);
		hr = pPropertyStore->GetValue(PKEY_Device_FriendlyName, &pv);
		if (FAILED(hr)) {
			LOG_ERROR(L"IPropertyStore::GetValue failed: hr = 0x%08x", hr);
			return hr;
		}
		PropVariantClearOnExit clearPv(&pv);

		LPWSTR deviceID;
		hr = pMMDevice->GetId(&deviceID);
		if (FAILED(hr)) {
			LOG_ERROR(L"IMMDevice->GetId(deviceID) failed: hr = 0x%08x", hr);
			return hr;
		}

		if (VT_LPWSTR != pv.vt) {
			LOG_ERROR(L"PKEY_Device_FriendlyName variant type is %u - expected VT_LPWSTR", pv.vt);
			return E_UNEXPECTED;
		}

		LOG_INFO(L"    %ls", pv.pwszVal);

		devices->insert(std::pair<std::wstring, std::wstring>(deviceID, pv.pwszVal));
	}
	return S_OK;
}

HRESULT GetActiveAudioDevice(_In_ LPCWSTR szDeviceId, _In_ EDataFlow flow, _Outptr_ IMMDevice **ppMMDevice) {
	HRESULT hr = S_OK;
	*ppMMDevice = NULL;
	// get an enumerator
	CComPtr<IMMDeviceEnumerator> pMMDeviceEnumerator;
	hr = CoCreateInstance(
		__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL,
		__uuidof(IMMDeviceEnumerator),
		(void **)&pMMDeviceEnumerator
	);
	if (FAILED(hr)) {
		LOG_ERROR(L"CoCreateInstance(IMMDeviceEnumerator) failed: hr = 0x%08x", hr);
		return hr;
	}
	CComPtr<IMMDeviceCollection> pMMDeviceCollection;
	// get all the active endpoints
	hr = pMMDeviceEnumerator->EnumAudioEndpoints(
		flow, DEVICE_STATE_ACTIVE, &pMMDeviceCollection
	);
	if (FAILED(hr)) {
		LOG_ERROR(L"IMMDeviceEnumerator::EnumAudioEndpoints failed: hr = 0x%08x", hr);
		return hr;
	}

	UINT count;
	hr = pMMDeviceCollection->GetCount(&count);
	if (FAILED(hr)) {
		LOG_ERROR(L"IMMDeviceCollection::GetCount failed: hr = 0x%08x", hr);
		return hr;
	}

	for (UINT i = 0; i < count; i++) {
		CComPtr<IMMDevice> pMMDevice;

		// get the "n"th device
		hr = pMMDeviceCollection->Item(i, &pMMDevice);
		if (FAILED(hr)) {
			LOG_ERROR(L"IMMDeviceCollection::Item failed: hr = 0x%08x", hr);
			return hr;
		}

		// get device id
		LPWSTR deviceID;
		hr = pMMDevice->GetId(&deviceID);
		if (FAILED(hr)) {
			LOG_ERROR(L"IMMDevice->GetId(deviceID) failed: hr = 0x%08x", hr);
			return hr;
		}

		// is it a match?
		if (0 == _wcsicmp(deviceID, szDeviceId)) {
			// did we already find it?
			if (NULL == *ppMMDevice) {
				*ppMMDevice = pMMDevice;
				(*ppMMDevice)->AddRef();
			}
			else {
				LOG_ERROR(L"Found (at least) two devices named %ls", szDeviceId);
				return E_UNEXPECTED;
			}
		}
	}

	if (NULL == *ppMMDevice) {
		LOG_ERROR(L"Could not find a device named %ls", szDeviceId);
		return E_NOTFOUND;
	}

	return S_OK;
}

HRESULT GetAudioDevice(_In_ LPCWSTR pwstrId, _Outptr_ IMMDevice **ppMMDevice) {
	HRESULT hr = S_OK;
	CComPtr<IMMDevice> pDevice = NULL;

	// get an enumerator
	CComPtr<IMMDeviceEnumerator> pMMDeviceEnumerator;
	hr = CoCreateInstance(
		__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL,
		__uuidof(IMMDeviceEnumerator),
		(void **)&pMMDeviceEnumerator
	);
	if (FAILED(hr)) {
		LOG_ERROR(L"CoCreateInstance(IMMDeviceEnumerator) failed: hr = 0x%08x", hr);
		return hr;
	}
	RETURN_ON_BAD_HR(hr = pMMDeviceEnumerator->GetDevice(pwstrId, &pDevice));

	*ppMMDevice = pDevice;
	(*ppMMDevice)->AddRef();
	return hr;
}

HRESULT GetAudioDeviceFlow(_In_ IMMDevice *pMMDevice, _Out_ EDataFlow *pFlow)
{
	HRESULT hr = S_OK;
	CComPtr<IMMEndpoint> pEndpoint;
	RETURN_ON_BAD_HR(hr = pMMDevice->QueryInterface(__uuidof(IMMEndpoint), (void **)&pEndpoint));
	RETURN_ON_BAD_HR(hr = pEndpoint->GetDataFlow(pFlow));
	return hr;
}

HRESULT GetAudioDeviceFriendlyName(_In_ IMMDevice *pDevice, _Out_ std::wstring *deviceName) {
	*deviceName = L"";
	HRESULT hr = S_OK;
	IPropertyStore *pProps = NULL;

	ReleaseOnExit releaseProps(pProps);

	PROPVARIANT varString;
	PropVariantInit(&varString);
	PropVariantClearOnExit clearVarString(&varString);

	RETURN_ON_BAD_HR(hr = pDevice->OpenPropertyStore(STGM_READ, &pProps));
	// Get the endpoint device's friendly-name property.
	RETURN_ON_BAD_HR(hr = pProps->GetValue(PKEY_Device_FriendlyName, &varString));

	*deviceName = std::wstring(varString.pwszVal);
	return hr;
}

HRESULT GetAudioDeviceFriendlyName(_In_ LPCWSTR pwstrId, _Out_ std::wstring *deviceName) {
	*deviceName = L"";
	HRESULT hr = S_OK;
	IMMDevice *pDevice = NULL;
	ReleaseOnExit releaseDevice(pDevice);

	// get an enumerator
	CComPtr<IMMDeviceEnumerator> pMMDeviceEnumerator;
	hr = CoCreateInstance(
		__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL,
		__uuidof(IMMDeviceEnumerator),
		(void **)&pMMDeviceEnumerator
	);
	if (FAILED(hr)) {
		LOG_ERROR(L"CoCreateInstance(IMMDeviceEnumerator) failed: hr = 0x%08x", hr);
		return hr;
	}
	RETURN_ON_BAD_HR(hr = pMMDeviceEnumerator->GetDevice(pwstrId, &pDevice));
	return GetAudioDeviceFriendlyName(pDevice, deviceName);
}