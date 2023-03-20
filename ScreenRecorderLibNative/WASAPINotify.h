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
	virtual HRESULT IMMNotificationClient::OnDeviceStateChanged(LPCWSTR, DWORD);
	virtual HRESULT IMMNotificationClient::OnDeviceAdded(LPCWSTR);
	virtual HRESULT IMMNotificationClient::OnDeviceRemoved(LPCWSTR);
	virtual HRESULT IMMNotificationClient::OnDefaultDeviceChanged(EDataFlow, ERole, LPCWSTR);
	virtual HRESULT IMMNotificationClient::OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY);
};