#include "WASAPINotify.h"
#include "CoreAudio.util.h"


// IUnknown methods -- AddRef, Release, and QueryInterface
ULONG STDMETHODCALLTYPE WASAPINotify::AddRef()
{
	return InterlockedIncrement(&m_cRef);
}

ULONG STDMETHODCALLTYPE WASAPINotify::Release()
{
	ULONG ulRef = InterlockedDecrement(&m_cRef);
	if (0 == ulRef)
	{
		delete this;
	}
	return ulRef;
}

HRESULT STDMETHODCALLTYPE WASAPINotify::QueryInterface(REFIID riid, VOID **ppvInterface)
{
	if (IID_IUnknown == riid)
	{
		AddRef();
		*ppvInterface = (IUnknown *)this;
	}
	else if (__uuidof(IMMNotificationClient) == riid)
	{
		AddRef();
		*ppvInterface = (IMMNotificationClient *)this;
	}
	else
	{
		*ppvInterface = NULL;
		return E_NOINTERFACE;
	}
	return S_OK;
}


// Callback methods for device-event notifications.
HRESULT STDMETHODCALLTYPE WASAPINotify::OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR pwstrDeviceId)
{
	wchar_t *pszFlow = L"?????";
	wchar_t *pszRole = L"?????";

	switch (role)
	{
		case eConsole:
			pszRole = L"eConsole";
			break;
		case eMultimedia:
			pszRole = L"eMultimedia";
			break;
		case eCommunications:
			pszRole = L"eCommunications";
			break;
	}
	switch (flow)
	{
		case eRender:
			pszFlow = L"eRender";
			break;
		case eCapture:
			pszFlow = L"eCapture";
			break;
	}
	if (flow == m_CaptureClient->GetFlow()) {
		m_CaptureClient->SetDefaultDevice(flow, role, pwstrDeviceId);
		std::wstring deviceName;
		GetAudioDeviceFriendlyName(pwstrDeviceId, &deviceName);

		LOG_DEBUG("New default device: name=%s, flow = %s, role = %s\n",
			   deviceName.c_str(), pszFlow, pszRole);
	}
	return S_OK;
}

HRESULT STDMETHODCALLTYPE WASAPINotify::OnDeviceAdded(LPCWSTR pwstrDeviceId)
{
	std::wstring deviceName = L"?????";
	EDataFlow flow;
	IMMDevice *pMMDevice;
	GetAudioDevice(pwstrDeviceId, &pMMDevice);
	GetAudioDeviceFriendlyName(pMMDevice, &deviceName);
	GetAudioDeviceFlow(pMMDevice, &flow);
	if (flow == m_CaptureClient->GetFlow()) {
		m_CaptureClient->SetOffline(false);
		m_CaptureClient->StartCapture();
		LOG_DEBUG(L"Added audio device %s", deviceName);
	}
	return S_OK;
};

HRESULT STDMETHODCALLTYPE WASAPINotify::OnDeviceRemoved(LPCWSTR pwstrDeviceId)
{
	std::wstring deviceName = L"?????";
	EDataFlow flow;
	IMMDevice *pMMDevice;
	GetAudioDevice(pwstrDeviceId, &pMMDevice);
	GetAudioDeviceFriendlyName(pMMDevice, &deviceName);
	GetAudioDeviceFlow(pMMDevice, &flow);
	if (flow == m_CaptureClient->GetFlow()) {
		if (pwstrDeviceId == m_CaptureClient->GetDeviceId()) {
			m_CaptureClient->SetOffline(true);
		}

		LOG_DEBUG(L"Removed audio device %s", deviceName);
	}
	return S_OK;
}

HRESULT STDMETHODCALLTYPE WASAPINotify::OnDeviceStateChanged(LPCWSTR pwstrDeviceId, DWORD dwNewState)
{
	std::wstring state = L"?????";
	std::wstring deviceName = L"?????";
	EDataFlow flow;
	IMMDevice *pMMDevice;
	GetAudioDevice(pwstrDeviceId, &pMMDevice);
	GetAudioDeviceFriendlyName(pMMDevice, &deviceName);
	GetAudioDeviceFlow(pMMDevice, &flow);
	if (flow == m_CaptureClient->GetFlow()) {
		switch (dwNewState)
		{
			case DEVICE_STATE_ACTIVE:
				state = L"ACTIVE";
				if (pwstrDeviceId == m_CaptureClient->GetDeviceId()) {
					m_CaptureClient->SetOffline(false);
					m_CaptureClient->StartCapture();
				}
				break;
			case DEVICE_STATE_DISABLED:
				state = L"DISABLED";
				if (pwstrDeviceId == m_CaptureClient->GetDeviceId()) {
					m_CaptureClient->SetOffline(true);
				}
				break;
			case DEVICE_STATE_NOTPRESENT:
				state = L"NOTPRESENT";
				if (pwstrDeviceId == m_CaptureClient->GetDeviceId()) {
					m_CaptureClient->SetOffline(true);
				}
				break;
			case DEVICE_STATE_UNPLUGGED:
				state = L"UNPLUGGED";
				if (pwstrDeviceId == m_CaptureClient->GetDeviceId()) {
					m_CaptureClient->SetOffline(true);
				}
				break;
		}

		LOG_DEBUG("New device state for audio device %s is DEVICE_STATE_%s (0x%8.8x)\n",
			   deviceName.c_str(), state.c_str(), dwNewState);
	}
	return S_OK;
}

HRESULT STDMETHODCALLTYPE WASAPINotify::OnPropertyValueChanged(LPCWSTR pwstrDeviceId, const PROPERTYKEY key) {
	return S_OK;
}