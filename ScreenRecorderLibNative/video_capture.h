#pragma once
#include <Wmcodecdsp.h>
#include <Windows.h>
#include <mfidl.h> 
#include <Mfapi.h> 
#include <Mfreadwrite.h>
#include <Shlwapi.h>
#include "common_types.h"

class video_capture : public IMFSourceReaderCallback //this class inhertis from IMFSourceReaderCallback
{
public:
	HRESULT Close();
	video_capture();
	~video_capture();
	HRESULT StartCapture(_In_ std::wstring deviceName);
	HRESULT GetFrame(_In_ SIZE size, _Outptr_ ID3D11Texture2D **pFrame);
	HRESULT GetFrameBuffer(_Inout_ FRAME_INFO *pFrameInfo);
	HRESULT Initialize(_In_ DX_RESOURCES* Data);
	inline SIZE GetFrameSize()
	{
		return SIZE{ (LONG)m_Width,(LONG)m_Height };
	}
	inline LONG GetStride() {
		return m_Stride;
	}

	// the class must implement the methods from IUnknown 
	STDMETHODIMP QueryInterface(REFIID iid, void** ppv);
	STDMETHODIMP_(ULONG) AddRef();
	STDMETHODIMP_(ULONG) Release();

	//  the class must implement the methods from IMFSourceReaderCallback 
	STDMETHODIMP OnReadSample(HRESULT status, DWORD streamIndex, DWORD streamFlags, LONGLONG timeStamp, IMFSample *sample);
	STDMETHODIMP OnEvent(DWORD, IMFMediaEvent *);
	STDMETHODIMP OnFlush(DWORD);

private:
	HRESULT InitializeSourceReader(_In_ IMFActivate *pDevice);
	HRESULT IsMediaTypeSupported(_In_ IMFMediaType* type);
	HRESULT GetDefaultStride(_In_ IMFMediaType *pType, _Out_ LONG *plStride);
	HRESULT CreateOutputMediaType(_Outptr_ IMFMediaType **pType);
	HRESULT CreateColorConverter(_Outptr_ IMFTransform **pColorConverter);
	HRESULT video_capture::ResizeFrameBuffer(FRAME_INFO* FrameInfo, int bufferSize);

	CRITICAL_SECTION criticalSection;
	long referenceCount;


	LONG m_Stride;
	int m_BytesPerPixel;
	GUID m_InputVideoFormat;
	UINT m_Height;
	UINT m_Width;
	std::wstring m_DeviceName;
	std::wstring m_DeviceSymbolicLink;


	IMFSourceReader* m_SourceReader;
	IMFMediaBuffer *m_Sample;
	LARGE_INTEGER m_LastSampleReceivedTimeStamp;
	LARGE_INTEGER m_LastGrabTimeStamp;
	ID3D11Device* m_Device;
	ID3D11DeviceContext* m_DeviceContext;
	IMFMediaType *m_InputMediaType;
	IMFMediaType *m_OutputMediaType;
	IMFTransform *m_ColorConverter;
};