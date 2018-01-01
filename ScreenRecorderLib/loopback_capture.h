#pragma once
#include <windows.h>
#include <avrt.h>
#include <mutex>
#include <mmdeviceapi.h>

#pragma comment(lib, "avrt.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "winmm.lib")

struct LoopbackCaptureThreadFunctionArguments {
	IMMDevice *pMMDevice;
	bool bInt16;
	HMMIO hFile;
	HANDLE hStartedEvent;
	HANDLE hStopEvent;
	UINT32 nFrames;
	HRESULT hr;
};
HRESULT LoopbackCapture(
	IMMDevice *pMMDevice,
	HMMIO hFile,
	bool bInt16,
	HANDLE hStartedEvent,
	HANDLE hStopEvent,
	PUINT32 pnFrames
);
DWORD WINAPI LoopbackCaptureThreadFunction(LPVOID pContext);
class loopback_capture
{
public:
	loopback_capture();
	~loopback_capture();
	void ClearRecordedBytes();
	void Cleanup();
	std::vector<BYTE> loopback_capture::GetRecordedBytes();
private:

};

