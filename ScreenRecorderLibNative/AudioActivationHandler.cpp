#include "AudioActivationHandler.h"




// Called by MakeAndInitialize (or manually)
HRESULT AudioActivationHandler::RuntimeClassInitialize()
{
	_event.reset(CreateEvent(nullptr, FALSE, FALSE, nullptr));
	return _event.get() ? S_OK : HRESULT_FROM_WIN32(GetLastError());
}

STDMETHODIMP AudioActivationHandler::ActivateCompleted(
	IActivateAudioInterfaceAsyncOperation *operation)
{
	HRESULT hr = operation->GetActivateResult(&_hr, &_activatedInterface);
	SetEvent(_event.get());
	return S_OK;
}

HRESULT AudioActivationHandler::WaitAndGetResult(REFIID iid, void **ppv)
{
	WaitForSingleObject(_event.get(), INFINITE);

	if (FAILED(_hr))
		return _hr;

	return _activatedInterface->QueryInterface(iid, ppv);
}

