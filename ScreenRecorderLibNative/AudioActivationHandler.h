#include <mmdeviceapi.h>
#include <audioclient.h>
#include <wrl.h>
#include <atomic>
#include <atlbase.h>
#include <memory>
using unique_handle =
std::unique_ptr<std::remove_pointer<HANDLE>::type, decltype(&CloseHandle)>;

class AudioActivationHandler final
	: public Microsoft::WRL::RuntimeClass<
	Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,
	Microsoft::WRL::FtmBase,
	IAgileObject,
	IActivateAudioInterfaceCompletionHandler>
{
public:
	HRESULT RuntimeClassInitialize();
	HRESULT WaitAndGetResult(REFIID iid, void **ppv);
	void FinalRelease();

private:
	unique_handle _event = unique_handle(nullptr, &CloseHandle);
	HRESULT _hr = E_FAIL;
	Microsoft::WRL::ComPtr<IUnknown> _activatedInterface;
	STDMETHODIMP ActivateCompleted(
	IActivateAudioInterfaceAsyncOperation *operation);

};