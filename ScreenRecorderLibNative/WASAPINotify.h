#pragma once
#include <mmdeviceapi.h>
#include "WASAPICapture.h"

class WASAPINotify : public IMMNotificationClient {
public:
	//Will get increased to 1 by CComPtr
	LONG m_cRef = 0;
	WASAPICapture *m_CaptureClient;

	WASAPINotify(WASAPICapture *client) : m_CaptureClient(client) {}

	// IUnknown methods -- AddRef, Release, and QueryInterface
	ULONG STDMETHODCALLTYPE AddRef();
	ULONG STDMETHODCALLTYPE Release();
	HRESULT STDMETHODCALLTYPE QueryInterface(
								REFIID riid, VOID **ppvInterface);
private:
	// IMMNotificationClient methods
	virtual HRESULT STDMETHODCALLTYPE IMMNotificationClient::OnDeviceStateChanged(LPCWSTR, DWORD);
	virtual HRESULT STDMETHODCALLTYPE IMMNotificationClient::OnDeviceAdded(LPCWSTR);
	virtual HRESULT STDMETHODCALLTYPE IMMNotificationClient::OnDeviceRemoved(LPCWSTR);
	virtual HRESULT STDMETHODCALLTYPE IMMNotificationClient::OnDefaultDeviceChanged(EDataFlow, ERole, LPCWSTR);
	virtual HRESULT STDMETHODCALLTYPE IMMNotificationClient::OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY);
};