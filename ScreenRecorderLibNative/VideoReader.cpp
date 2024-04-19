#include "VideoReader.h"
#include "Cleanup.h"
#include "LogMediaType.h"

VideoReader::VideoReader() :SourceReaderBase()
{
}

VideoReader::~VideoReader()
{
}

HRESULT VideoReader::InitializeSourceReader(
	_In_ IStream *pSourceStream,
	_In_ std::optional<long> sourceFormatIndex,
	_Out_ long *pStreamIndex,
	_Outptr_ IMFSourceReader **ppSourceReader,
	_Outptr_ IMFMediaType **ppInputMediaType,
	_Outptr_opt_ IMFMediaType **ppOutputMediaType,
	_Outptr_opt_result_maybenull_ IMFTransform **ppMediaTransform)
{
	if (ppSourceReader) {
		*ppSourceReader = nullptr;
	}
	EnterCriticalSection(&m_CriticalSection);
	LeaveCriticalSectionOnExit leaveCriticalSection(&m_CriticalSection, L"InitializeSourceReader video reader");
	CComPtr<IMFAttributes> pAttributes = nullptr;
	HRESULT hr = CreateAttributes(&pAttributes);
	if (SUCCEEDED(hr)) {
		CComPtr<IMFSourceReader> pSourceReader = nullptr;
		if (SUCCEEDED(hr)) {
			CComPtr<IMFByteStream> pMfByteStream = nullptr;
			hr = MFCreateMFByteStreamOnStream(pSourceStream, &pMfByteStream);
			if (SUCCEEDED(hr)) {
				hr = MFCreateSourceReaderFromByteStream(pMfByteStream, pAttributes, &pSourceReader);
			}
		}
		if (SUCCEEDED(hr)) {
			hr = InitializeSourceReader(pSourceReader, sourceFormatIndex, pStreamIndex, ppInputMediaType, ppOutputMediaType, ppMediaTransform);
		}
		if (SUCCEEDED(hr)) {
			if (ppSourceReader) {
				*ppSourceReader = pSourceReader;
				(*ppSourceReader)->AddRef();
			}
		}
	}
	return hr;
}

HRESULT VideoReader::InitializeSourceReader(
	_In_ std::wstring filePath,
	_In_ std::optional<long> sourceFormatIndex,
	_Out_ long *pStreamIndex,
	_Outptr_ IMFSourceReader **ppSourceReader,
	_Outptr_ IMFMediaType **ppInputMediaType,
	_Outptr_opt_ IMFMediaType **ppOutputMediaType,
	_Outptr_opt_result_maybenull_ IMFTransform **ppMediaTransform)
{
	if (ppSourceReader) {
		*ppSourceReader = nullptr;
	}
	EnterCriticalSection(&m_CriticalSection);
	LeaveCriticalSectionOnExit leaveCriticalSection(&m_CriticalSection, L"InitializeSourceReader video reader");
	CComPtr<IMFAttributes> pAttributes = nullptr;
	HRESULT hr = CreateAttributes(&pAttributes);
	if (SUCCEEDED(hr)) {
		CComPtr<IMFSourceReader> pSourceReader = nullptr;
		if (SUCCEEDED(hr)) {
			hr = MFCreateSourceReaderFromURL(filePath.c_str(), pAttributes, &pSourceReader);
		}
		if (SUCCEEDED(hr)) {
			hr = InitializeSourceReader(pSourceReader, sourceFormatIndex, pStreamIndex, ppInputMediaType, ppOutputMediaType, ppMediaTransform);
		}
		if (SUCCEEDED(hr)) {
			if (ppSourceReader) {
				*ppSourceReader = pSourceReader;
				(*ppSourceReader)->AddRef();
			}
		}
	}
	return hr;
}


HRESULT VideoReader::InitializeSourceReader(
	_In_ IMFSourceReader *pSourceReader,
	_In_ std::optional<long> sourceFormatIndex,
	_Out_ long *pStreamIndex,
	_Outptr_ IMFMediaType **ppInputMediaType,
	_Outptr_opt_ IMFMediaType **ppOutputMediaType,
	_Outptr_opt_result_maybenull_ IMFTransform **ppMediaTransform)
{

	if (ppInputMediaType) {
		*ppInputMediaType = nullptr;
	}
	if (ppOutputMediaType) {
		*ppOutputMediaType = nullptr;
	}
	if (ppMediaTransform) {
		*ppMediaTransform = nullptr;
	}
	HRESULT hr = S_OK;

	bool noMoreMedia = false;
	bool foundValidTopology = false;
	// Try to find a suitable output type.
	if (SUCCEEDED(hr))
	{
		for (DWORD mediaTypeIndex = 0; ; mediaTypeIndex++)
		{
			for (DWORD streamIndex = 0; ; streamIndex++)
			{
				CComPtr<IMFMediaType> pInputMediaType = nullptr;
				CComPtr<IMFMediaType> pOutputMediaType = nullptr;
				CComPtr<IMFTransform> pMediaTransform = nullptr;
				hr = pSourceReader->GetNativeMediaType(streamIndex, mediaTypeIndex, &pInputMediaType);
				if (FAILED(hr))
				{
					noMoreMedia = true;
					break;
				}
				GUID inputMajorType;
				pInputMediaType->GetGUID(MF_MT_MAJOR_TYPE, &inputMajorType);
				if (inputMajorType != MFMediaType_Video) {
					continue;
				}
				GUID inputSubType;
				pInputMediaType->GetGUID(MF_MT_SUBTYPE, &inputSubType);
				LogMediaType(pInputMediaType);
				if (ppOutputMediaType) {
					SafeRelease(&pMediaTransform);
					hr = CreateIMFTransform(streamIndex, pInputMediaType, &pMediaTransform, &pOutputMediaType);
					if (FAILED(hr)) {
						LOG_INFO("Failed to create a valid media output type for video reader, attempting to create an intermediate transform");
						CComPtr<IMFActivate> pConverterActivate = NULL;
						CONTINUE_ON_BAD_HR(hr = FindVideoDecoder(&inputSubType, nullptr, false, true, true, &pConverterActivate));
						CComPtr<IMFTransform> pConverter = NULL;
						CONTINUE_ON_BAD_HR(pConverterActivate->ActivateObject(IID_PPV_ARGS(&pConverter)));
						CONTINUE_ON_BAD_HR(pConverter->SetInputType(streamIndex, pInputMediaType, 0));
						GUID guidMinor;
						GUID guidMajor;
						for (int i = 0;; i++)
						{
							SafeRelease(&pMediaTransform);
							IMFMediaType *mediaType;
							hr = pConverter->GetOutputAvailableType(streamIndex, i, &mediaType);
							if (FAILED(hr))
							{
								break;
							}
							hr = mediaType->GetGUID(MF_MT_MAJOR_TYPE, &guidMajor);
							if (guidMajor == MFMediaType_Video) {
								hr = mediaType->GetGUID(MF_MT_SUBTYPE, &guidMinor);
								IMFMediaType *pIntermediateMediaType;
								// Define the output type.
								CONTINUE_ON_BAD_HR(hr = MFCreateMediaType(&pIntermediateMediaType));
								CONTINUE_ON_BAD_HR(hr = pInputMediaType->CopyAllItems(pIntermediateMediaType));
								CONTINUE_ON_BAD_HR(hr = pIntermediateMediaType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video));
								CONTINUE_ON_BAD_HR(hr = pIntermediateMediaType->SetGUID(MF_MT_SUBTYPE, guidMinor));
								CONTINUE_ON_BAD_HR(hr = pSourceReader->SetCurrentMediaType(streamIndex, NULL, pIntermediateMediaType));
								CONTINUE_ON_BAD_HR(hr = CreateIMFTransform(streamIndex, pIntermediateMediaType, &pMediaTransform, &pOutputMediaType));
								LOG_DEBUG("Successfully created video reader intermediate media transform:");
								LogMediaType(pIntermediateMediaType);
								break;
							}
						}
						pConverterActivate->ShutdownObject();
					}
				}
				if (SUCCEEDED(hr)) {// Found an output type.
					if (ppInputMediaType) {
						*ppInputMediaType = pInputMediaType;
						(*ppInputMediaType)->AddRef();
					}
					if (ppOutputMediaType) {
						*ppOutputMediaType = pOutputMediaType;
						(*ppOutputMediaType)->AddRef();
					}

					if (ppMediaTransform) {
						CComPtr<IMFAttributes> pTransformAttributes;
						if (pMediaTransform && SUCCEEDED(pMediaTransform->GetAttributes(&pTransformAttributes))) {
							UINT32 d3d11Aware = 0;
							pTransformAttributes->GetUINT32(MF_SA_D3D11_AWARE, &d3d11Aware);
							if (d3d11Aware > 0) {
								HRESULT hr = pMediaTransform->ProcessMessage(MFT_MESSAGE_SET_D3D_MANAGER, reinterpret_cast<ULONG_PTR>(GetDeviceManager()));
								LOG_ON_BAD_HR(hr);
							}
						}
						*ppMediaTransform = pMediaTransform;
						(*ppMediaTransform)->AddRef();
					}
					*pStreamIndex = streamIndex;
					foundValidTopology = true;
					break;
				}
				else {
					LOG_ERROR("Failed to create topology for video reader");
				}
			}
			if (noMoreMedia || foundValidTopology) {
				break;
			}
		}
	}
	if (FAILED(hr))
	{
		Close();
	}
	return hr;
}

HRESULT VideoReader::CreateAttributes(_Outptr_ IMFAttributes **ppAttributes)
{
	CComPtr<IMFAttributes> pAttributes = nullptr;

	if (ppAttributes) {
		*ppAttributes = nullptr;
	}

	//Allocate attributes
	HRESULT hr = MFCreateAttributes(&pAttributes, 2);

	// Set the callback pointer.
	if (SUCCEEDED(hr))
		hr = pAttributes->SetUnknown(MF_SOURCE_READER_ASYNC_CALLBACK, this);
	//Enable hardware transforms
	if (SUCCEEDED(hr)) {
		hr = pAttributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, true);
	}
	// Add device manager to attributes. This enables hardware decoding.
	if (SUCCEEDED(hr)) {
		hr = pAttributes->SetUnknown(MF_SOURCE_READER_D3D_MANAGER, GetDeviceManager());
	}
	if (SUCCEEDED(hr)) {
		if (ppAttributes) {
			*ppAttributes = pAttributes;
			(*ppAttributes)->AddRef();
		}
	}
	return hr;
}
