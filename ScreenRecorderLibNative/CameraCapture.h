#pragma once
#include "SourceReaderBase.h"
#include "MF.util.h"

class CameraCapture : public SourceReaderBase
{
public:
	CameraCapture();
	virtual ~CameraCapture();
	virtual inline std::wstring Name() override { return L"CameraCapture"; };
protected:
	virtual HRESULT InitializeSourceReader(
		_In_ std::wstring source,
		_In_ std::optional<long> sourceFormatIndex,
		_Out_ long *pStreamIndex,
		_Outptr_ IMFSourceReader **ppSourceReader,
		_Outptr_ IMFMediaType **ppInputMediaType,
		_Outptr_opt_ IMFMediaType **ppOutputMediaType,
		_Outptr_opt_result_maybenull_ IMFTransform **ppMediaTransform) override;

	virtual HRESULT InitializeSourceReader(
		_In_ IStream *pSourceStream,
		_In_ std::optional<long> sourceFormatIndex,
		_Out_ long *pStreamIndex,
		_Outptr_ IMFSourceReader **ppSourceReader,
		_Outptr_ IMFMediaType **ppInputMediaType,
		_Outptr_opt_ IMFMediaType **ppOutputMediaType,
		_Outptr_opt_result_maybenull_ IMFTransform **ppMediaTransform) override;
private:
	HRESULT InitializeMediaSource(
		_In_ CComPtr<IMFMediaSource> pDevice,
		_In_ std::optional<long> sourceFormatIndex,
		_Out_ long *pStreamIndex,
		_Outptr_ IMFSourceReader **ppSourceReader,
		_Outptr_ IMFMediaType **ppInputMediaType,
		_Outptr_opt_ IMFMediaType **ppOutputMediaType,
		_Outptr_opt_result_maybenull_ IMFTransform **ppMediaTransform
	);
	HRESULT SetDeviceFormat(_In_ CComPtr<IMFMediaSource> pDevice, _In_ DWORD dwFormatIndex);
	std::wstring m_DeviceName;
	std::wstring m_DeviceSymbolicLink;
};
