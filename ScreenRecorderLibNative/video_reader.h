#pragma once
#include "source_reader_base.h"
#include "MF.util.h"
class video_reader :public source_reader_base
{
public:
	video_reader();
	virtual ~video_reader();
protected:
	virtual HRESULT InitializeSourceReader(
		_In_ std::wstring filePath, 
		_Out_ long *pStreamIndex,
		_Outptr_ IMFSourceReader **ppSourceReader,
		_Outptr_ IMFMediaType **ppInputMediaType,
		_Outptr_ IMFMediaType **ppOutputMediaType,
		_Outptr_opt_result_maybenull_ IMFTransform **ppMediaTransform) override;
};

