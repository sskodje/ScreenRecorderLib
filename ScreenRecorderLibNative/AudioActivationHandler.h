#include <mmdeviceapi.h>
#include <audioclient.h>
#include <wrl.h>
#include <atomic>
#include <atlbase.h>

class AudioActivationHandler final
	: public IActivateAudioInterfaceCompletionHandler
{
public:
	AudioActivationHandler();

	~AudioActivationHandler();


	STDMETHODIMP ActivateCompleted(
		IActivateAudioInterfaceAsyncOperation *operation);

	HRESULT WaitAndGetResult(REFIID iid, void **ppv);


private:
	LONG _refCount{ 1 };
	HANDLE _event = nullptr;
	HRESULT _hr = E_FAIL;
	CComPtr<IUnknown> _activatedInterface;
};