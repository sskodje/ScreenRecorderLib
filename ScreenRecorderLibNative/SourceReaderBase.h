#pragma once
#include <Wmcodecdsp.h>
#include <Windows.h>
#include <mfidl.h> 
#include <Mfapi.h> 
#include <Mfreadwrite.h>
#include <propvarutil.h>
#include <Shlwapi.h>
#include <atlbase.h>
#include "CommonTypes.h"
#include "HighresTimer.h"
#include "LogMediaType.h"
#include "CaptureBase.h"

class SourceReaderBase abstract : public CaptureBase, public IMFSourceReaderCallback  //this class inherits from IMFSourceReaderCallback
{
public:
	virtual void Close();
	SourceReaderBase();
	virtual ~SourceReaderBase();
	virtual HRESULT StartCapture(_In_ std::wstring source) override;

	virtual HRESULT GetFrame(_Inout_ FRAME_INFO *pFrameInfo, _In_ int timeoutMs) override;
	virtual HRESULT Initialize(_In_ DX_RESOURCES *Data) override;

	//  the class must implement the methods from IMFSourceReaderCallback 
	STDMETHODIMP OnReadSample(HRESULT status, DWORD streamIndex, DWORD streamFlags, LONGLONG timeStamp, IMFSample *sample);
	STDMETHODIMP OnEvent(DWORD, IMFMediaEvent *);
	STDMETHODIMP OnFlush(DWORD);
	// the class must implement the methods from IUnknown 
	STDMETHODIMP QueryInterface(REFIID iid, void **ppv);
	STDMETHODIMP_(ULONG) AddRef();
	STDMETHODIMP_(ULONG) Release();

protected:
	virtual HRESULT InitializeSourceReader(
		_In_ std::wstring source,
		_Out_ long *pStreamIndex,
		_Outptr_ IMFSourceReader **ppSourceReader,
		_Outptr_ IMFMediaType **ppInputMediaType,
		_Outptr_ IMFMediaType **ppOutputMediaType,
		_Outptr_opt_result_maybenull_ IMFTransform **ppMediaTransform) abstract;
	virtual HRESULT GetFrameRate(_In_ IMFMediaType *pMediaType, _Out_ double *pFramerate);
	virtual HRESULT GetFrameSize(_In_ IMFMediaType *pMediaType, _Out_ SIZE *pFrameSize);
	virtual HRESULT GetDefaultStride(_In_ IMFMediaType *pType, _Out_ LONG *plStride);
	virtual HRESULT CreateOutputMediaType(_In_ SIZE frameSize, _Outptr_ IMFMediaType **pType, _Out_ LONG *stride);
	virtual HRESULT CreateIMFTransform(_In_ DWORD streamIndex, _In_ IMFMediaType *pInputMediaType, _Outptr_ IMFTransform **pColorConverter, _Outptr_ IMFMediaType **ppOutputMediaType);
	virtual HRESULT SourceReaderBase::ResizeFrameBuffer(FRAME_INFO *FrameInfo, int bufferSize);
	CRITICAL_SECTION m_CriticalSection;
private:
	long m_ReferenceCount;
	HANDLE m_NewFrameEvent;
	HANDLE m_CaptureStoppedEvent;
	LARGE_INTEGER m_LastSampleReceivedTimeStamp;
	LARGE_INTEGER m_LastGrabTimeStamp;
	IMFMediaBuffer *m_Sample;
	ID3D11Device *m_Device;
	ID3D11DeviceContext *m_DeviceContext;
	HighresTimer *m_FramerateTimer;
	IMFMediaType *m_OutputMediaType;
	IMFMediaType *m_InputMediaType;
	IMFSourceReader *m_SourceReader;
	IMFTransform *m_MediaTransform;
	LONG m_Stride;
	SIZE m_FrameSize;
	double m_FrameRate;
};