#pragma once
#include "SourceReaderBase.h"
#include "MF.util.h"
namespace ScreenRecorderLib::Overlays {
	class CameraCapture : public SourceReaderBase
	{
	public:
		CameraCapture();
		virtual ~CameraCapture();
		virtual inline std::wstring Name() override { return L"CameraCapture"; };
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
}