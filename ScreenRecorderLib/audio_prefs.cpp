// prefs.cpp
//https://github.com/mvaneerde/blog/tree/master/loopback-capture
#include <stdio.h>

#include <windows.h>
#include <avrt.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <functiondiscoverykeys_devpkey.h>
#include "log.h"
#include "audio_prefs.h"
#include "cleanup.h"




HRESULT get_default_device(IMMDevice **ppMMDevice);
HRESULT list_devices();
HRESULT get_specific_device(LPCWSTR szLongName, IMMDevice **ppMMDevice);
HRESULT open_file(LPCWSTR szFileName, HMMIO *phFile);


CPrefs::CPrefs(int argc, LPCWSTR argv[], HRESULT &hr)
	: m_pMMDevice(NULL)
	, m_hFile(NULL)
	, m_bInt16(false)
	, m_pwfx(NULL)
{
	switch (argc) {
	default:
		// loop through arguments and parse them
		for (int i = 1; i < argc; i++) {

			// --device
			if (0 == _wcsicmp(argv[i], L"--device")) {
				if (NULL != m_pMMDevice) {
					ERR(L"%s", L"Only one --device switch is allowed");
					hr = E_INVALIDARG;
					return;
				}

				if (i++ == argc) {
					ERR(L"%s", L"--device switch requires an argument");
					hr = E_INVALIDARG;
					return;
				}

				hr = get_specific_device(argv[i], &m_pMMDevice);
				if (FAILED(hr)) {
					return;
				}

				continue;
			}

			// --int-16
			if (0 == _wcsicmp(argv[i], L"--int-16")) {
				if (m_bInt16) {
					ERR(L"%s", L"Only one --int-16 switch is allowed");
					hr = E_INVALIDARG;
					return;
				}

				m_bInt16 = true;
				continue;
			}

			ERR(L"Invalid argument %ls", argv[i]);
			hr = E_INVALIDARG;
			return;
		}

		// open default device if not specified
		if (NULL == m_pMMDevice) {
			hr = get_default_device(&m_pMMDevice);
			if (FAILED(hr)) {
				return;
			}
		}
	}
}

CPrefs::~CPrefs() {
	if (NULL != m_pMMDevice) {
		m_pMMDevice->Release();
	}

	if (NULL != m_hFile) {
		mmioClose(m_hFile, 0);
	}

	if (NULL != m_pwfx) {
		CoTaskMemFree(m_pwfx);
	}
}

HRESULT get_default_device(IMMDevice **ppMMDevice) {
	HRESULT hr = S_OK;
	IMMDeviceEnumerator *pMMDeviceEnumerator;

	// activate a device enumerator
	hr = CoCreateInstance(
		__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL,
		__uuidof(IMMDeviceEnumerator),
		(void**)&pMMDeviceEnumerator
	);
	if (FAILED(hr)) {
		ERR(L"CoCreateInstance(IMMDeviceEnumerator) failed: hr = 0x%08x", hr);
		return hr;
	}
	ReleaseOnExit releaseMMDeviceEnumerator(pMMDeviceEnumerator);

	// get the default render endpoint
	hr = pMMDeviceEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, ppMMDevice);
	if (FAILED(hr)) {
		ERR(L"IMMDeviceEnumerator::GetDefaultAudioEndpoint failed: hr = 0x%08x", hr);
		return hr;
	}

	return S_OK;
}

HRESULT list_devices() {
	HRESULT hr = S_OK;

	// get an enumerator
	IMMDeviceEnumerator *pMMDeviceEnumerator;

	hr = CoCreateInstance(
		__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL,
		__uuidof(IMMDeviceEnumerator),
		(void**)&pMMDeviceEnumerator
	);
	if (FAILED(hr)) {
		ERR(L"CoCreateInstance(IMMDeviceEnumerator) failed: hr = 0x%08x", hr);
		return hr;
	}
	ReleaseOnExit releaseMMDeviceEnumerator(pMMDeviceEnumerator);

	IMMDeviceCollection *pMMDeviceCollection;

	// get all the active render endpoints
	hr = pMMDeviceEnumerator->EnumAudioEndpoints(
		eRender, DEVICE_STATE_ACTIVE, &pMMDeviceCollection
	);
	if (FAILED(hr)) {
		ERR(L"IMMDeviceEnumerator::EnumAudioEndpoints failed: hr = 0x%08x", hr);
		return hr;
	}
	ReleaseOnExit releaseMMDeviceCollection(pMMDeviceCollection);

	UINT count;
	hr = pMMDeviceCollection->GetCount(&count);
	if (FAILED(hr)) {
		ERR(L"IMMDeviceCollection::GetCount failed: hr = 0x%08x", hr);
		return hr;
	}
	LOG(L"Active render endpoints found: %u", count);

	for (UINT i = 0; i < count; i++) {
		IMMDevice *pMMDevice;

		// get the "n"th device
		hr = pMMDeviceCollection->Item(i, &pMMDevice);
		if (FAILED(hr)) {
			ERR(L"IMMDeviceCollection::Item failed: hr = 0x%08x", hr);
			return hr;
		}
		ReleaseOnExit releaseMMDevice(pMMDevice);

		// open the property store on that device
		IPropertyStore *pPropertyStore;
		hr = pMMDevice->OpenPropertyStore(STGM_READ, &pPropertyStore);
		if (FAILED(hr)) {
			ERR(L"IMMDevice::OpenPropertyStore failed: hr = 0x%08x", hr);
			return hr;
		}
		ReleaseOnExit releasePropertyStore(pPropertyStore);

		// get the long name property
		PROPVARIANT pv; PropVariantInit(&pv);
		hr = pPropertyStore->GetValue(PKEY_Device_FriendlyName, &pv);
		if (FAILED(hr)) {
			ERR(L"IPropertyStore::GetValue failed: hr = 0x%08x", hr);
			return hr;
		}
		PropVariantClearOnExit clearPv(&pv);

		if (VT_LPWSTR != pv.vt) {
			ERR(L"PKEY_Device_FriendlyName variant type is %u - expected VT_LPWSTR", pv.vt);
			return E_UNEXPECTED;
		}

		LOG(L"    %ls", pv.pwszVal);
	}

	return S_OK;
}

HRESULT get_specific_device(LPCWSTR szLongName, IMMDevice **ppMMDevice) {
	HRESULT hr = S_OK;

	*ppMMDevice = NULL;

	// get an enumerator
	IMMDeviceEnumerator *pMMDeviceEnumerator;

	hr = CoCreateInstance(
		__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL,
		__uuidof(IMMDeviceEnumerator),
		(void**)&pMMDeviceEnumerator
	);
	if (FAILED(hr)) {
		ERR(L"CoCreateInstance(IMMDeviceEnumerator) failed: hr = 0x%08x", hr);
		return hr;
	}
	ReleaseOnExit releaseMMDeviceEnumerator(pMMDeviceEnumerator);

	IMMDeviceCollection *pMMDeviceCollection;

	// get all the active render endpoints
	hr = pMMDeviceEnumerator->EnumAudioEndpoints(
		eRender, DEVICE_STATE_ACTIVE, &pMMDeviceCollection
	);
	if (FAILED(hr)) {
		ERR(L"IMMDeviceEnumerator::EnumAudioEndpoints failed: hr = 0x%08x", hr);
		return hr;
	}
	ReleaseOnExit releaseMMDeviceCollection(pMMDeviceCollection);

	UINT count;
	hr = pMMDeviceCollection->GetCount(&count);
	if (FAILED(hr)) {
		ERR(L"IMMDeviceCollection::GetCount failed: hr = 0x%08x", hr);
		return hr;
	}

	for (UINT i = 0; i < count; i++) {
		IMMDevice *pMMDevice;

		// get the "n"th device
		hr = pMMDeviceCollection->Item(i, &pMMDevice);
		if (FAILED(hr)) {
			ERR(L"IMMDeviceCollection::Item failed: hr = 0x%08x", hr);
			return hr;
		}
		ReleaseOnExit releaseMMDevice(pMMDevice);

		// open the property store on that device
		IPropertyStore *pPropertyStore;
		hr = pMMDevice->OpenPropertyStore(STGM_READ, &pPropertyStore);
		if (FAILED(hr)) {
			ERR(L"IMMDevice::OpenPropertyStore failed: hr = 0x%08x", hr);
			return hr;
		}
		ReleaseOnExit releasePropertyStore(pPropertyStore);

		// get the long name property
		PROPVARIANT pv; PropVariantInit(&pv);
		hr = pPropertyStore->GetValue(PKEY_Device_FriendlyName, &pv);
		if (FAILED(hr)) {
			ERR(L"IPropertyStore::GetValue failed: hr = 0x%08x", hr);
			return hr;
		}
		PropVariantClearOnExit clearPv(&pv);

		if (VT_LPWSTR != pv.vt) {
			ERR(L"PKEY_Device_FriendlyName variant type is %u - expected VT_LPWSTR", pv.vt);
			return E_UNEXPECTED;
		}

		// is it a match?
		if (0 == _wcsicmp(pv.pwszVal, szLongName)) {
			// did we already find it?
			if (NULL == *ppMMDevice) {
				*ppMMDevice = pMMDevice;
				pMMDevice->AddRef();
			}
			else {
				ERR(L"Found (at least) two devices named %ls", szLongName);
				return E_UNEXPECTED;
			}
		}
	}

	if (NULL == *ppMMDevice) {
		ERR(L"Could not find a device named %ls", szLongName);
		return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
	}

	return S_OK;
}

HRESULT open_file(LPCWSTR szFileName, HMMIO *phFile) {
	MMIOINFO mi = { 0 };

	*phFile = mmioOpen(
		// some flags cause mmioOpen write to this buffer
		// but not any that we're using
		const_cast<LPWSTR>(szFileName),
		&mi,
		MMIO_WRITE | MMIO_CREATE
	);

	if (NULL == *phFile) {
		ERR(L"mmioOpen(\"%ls\", ...) failed. wErrorRet == %u", szFileName, mi.wErrorRet);
		return E_FAIL;
	}

	return S_OK;
}
