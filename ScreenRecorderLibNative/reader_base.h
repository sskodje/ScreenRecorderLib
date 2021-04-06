#pragma once

class reader_base abstract
{
public:
	virtual HRESULT StartCapture(_In_ std::wstring source) abstract;
	virtual HRESULT GetFrame(_Inout_ FRAME_INFO *pFrameInfo, _In_ int timeoutMs) abstract;
	virtual HRESULT Initialize(_In_ DX_RESOURCES *Data) abstract;
};