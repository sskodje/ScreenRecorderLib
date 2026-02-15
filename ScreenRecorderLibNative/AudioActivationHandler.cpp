#include <mmdeviceapi.h>
#include <audioclient.h>
#include <wrl.h>
#include <atomic>
#include <atlbase.h>

class AudioActivationHandler final
	: public Microsoft::WRL::RuntimeClass<
	Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
	Microsoft::WRL::FtmBase,
	IAgileObject,
	IActivateAudioInterfaceCompletionHandler >
{
public:
	AudioActivationHandler()
		//: _event(CreateEvent(nullptr, FALSE, FALSE, nullptr))
	{
	}

	void FinalRelease()
	{
		if (_event)
		{
			CloseHandle(_event);
			_event = nullptr;
		}
	}

	// Called by MakeAndInitialize (or manually)
	HRESULT RuntimeClassInitialize()
	{
		_event = CreateEvent(nullptr, FALSE, FALSE, nullptr);
		return _event ? S_OK : HRESULT_FROM_WIN32(GetLastError());
	}

	STDMETHODIMP ActivateCompleted(
		IActivateAudioInterfaceAsyncOperation *operation) override
	{
		HRESULT hr = operation->GetActivateResult(&_hr, &_activatedInterface);
		SetEvent(_event);
		return S_OK;
	}

	HRESULT WaitAndGetResult(REFIID iid, void **ppv)
	{
		WaitForSingleObject(_event, INFINITE);

		if (FAILED(_hr))
			return _hr;

		return _activatedInterface->QueryInterface(iid, ppv);
	}

	// IUnknown
	STDMETHODIMP QueryInterface(REFIID riid, void **ppv) override
	{
		if (riid == __uuidof(IUnknown) ||
			riid == __uuidof(IActivateAudioInterfaceCompletionHandler))
		{
			*ppv = static_cast<IActivateAudioInterfaceCompletionHandler *>(this);
			AddRef();
			return S_OK;
		}
		*ppv = nullptr;
		return E_NOINTERFACE;
	}

	STDMETHODIMP_(ULONG) AddRef() override
	{
		return InterlockedIncrement(&_refCount);
	}

	STDMETHODIMP_(ULONG) Release() override
	{
		ULONG r = InterlockedDecrement(&_refCount);
		if (r == 0) delete this;
		return r;
	}

private:
	LONG _refCount{ 1 };
	HANDLE _event = nullptr;
	HRESULT _hr = E_FAIL;
	CComPtr<IUnknown> _activatedInterface;
};