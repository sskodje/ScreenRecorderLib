#pragma once
#include "source_reader_base.h"
#include "MF.util.h"
class device_capture : public source_reader_base
{
public:
	device_capture();
	virtual ~device_capture();

protected:
	virtual HRESULT InitializeSourceReader(
		_In_ std::wstring source,
		_Out_ long *pStreamIndex,
		_Outptr_ IMFSourceReader **ppSourceReader,
		_Outptr_ IMFMediaType **ppInputMediaType,
		_Outptr_ IMFMediaType **ppOutputMediaType,
		_Outptr_opt_result_maybenull_ IMFTransform **ppMediaTransform) override;
private:
	HRESULT InitializeMediaSource(
		_In_ IMFActivate *pDevice,
		_Out_ long *pStreamIndex,
		_Outptr_ IMFSourceReader **ppSourceReader,
		_Outptr_ IMFMediaType **ppInputMediaType,
		_Outptr_ IMFMediaType **ppOutputMediaType,
		_Outptr_opt_result_maybenull_ IMFTransform **ppMediaTransform
	);
	std::wstring m_DeviceName;
	std::wstring m_DeviceSymbolicLink;
};

