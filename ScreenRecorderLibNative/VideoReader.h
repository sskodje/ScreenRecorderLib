#pragma once
#include "SourceReaderBase.h"
#include "MF.util.h"

class VideoReader :public SourceReaderBase
{
public:
	VideoReader();
	virtual ~VideoReader();
	virtual inline std::wstring Name() override { return L"VideoReader"; };
protected:
	virtual HRESULT InitializeSourceReader(
		_In_ std::wstring filePath,
		_In_ std::optional<long> sourceFormatIndex,
		_Out_ long *pStreamIndex,
		_Outptr_ IMFSourceReader **ppSourceReader,
		_Outptr_ IMFMediaType **ppInputMediaType,
		_Outptr_opt_ IMFMediaType **ppOutputMediaType,
		_Outptr_opt_result_maybenull_ IMFTransform **ppMediaTransform) override;
};