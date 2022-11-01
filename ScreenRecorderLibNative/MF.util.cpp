#include "MF.util.h"
#include "Cleanup.h"
#include <uuids.h>

#pragma comment(lib, "strmiids.lib")
HRESULT FindDecoderEx(const GUID &subtype, BOOL bAudio, IMFTransform **ppDecoder)
{
	HRESULT hr = S_OK;
	UINT32 count = 0;

	IMFActivate **ppActivate = NULL;

	MFT_REGISTER_TYPE_INFO info = { 0 };

	info.guidMajorType = bAudio ? MFMediaType_Audio : MFMediaType_Video;
	info.guidSubtype = subtype;

	hr = MFTEnumEx(
		bAudio ? MFT_CATEGORY_AUDIO_DECODER : MFT_CATEGORY_VIDEO_DECODER,
		MFT_ENUM_FLAG_SYNCMFT | MFT_ENUM_FLAG_LOCALMFT | MFT_ENUM_FLAG_SORTANDFILTER,
		&info,      // Input type
		NULL,       // Output type
		&ppActivate,
		&count
	);

	if (SUCCEEDED(hr) && count == 0)
	{
		hr = MF_E_TOPO_CODEC_NOT_FOUND;
	}

	// Create the first decoder in the list.

	if (SUCCEEDED(hr))
	{
		hr = ppActivate[0]->ActivateObject(IID_PPV_ARGS(ppDecoder));
	}

	for (UINT32 i = 0; i < count; i++)
	{
		ppActivate[i]->Release();
	}
	CoTaskMemFree(ppActivate);

	return hr;
}

HRESULT FindVideoDecoder(
	GUID *inputSubtype,
	GUID *outputSubtype,
	BOOL bAllowAsync,
	BOOL bAllowHardware,
	BOOL bAllowTranscode,
	IMFTransform **ppDecoder
)
{
	HRESULT hr = S_OK;
	IMFActivate **ppActivate = NULL;
	UINT32 count = 0;

	MFT_REGISTER_TYPE_INFO *inputInfo = nullptr;
	if (inputSubtype) {
		inputInfo = new MFT_REGISTER_TYPE_INFO{ MFMediaType_Video, *inputSubtype };
	}
	MFT_REGISTER_TYPE_INFO *outputInfo = nullptr;
	if (outputSubtype) {
		outputInfo = new MFT_REGISTER_TYPE_INFO{ MFMediaType_Video, *outputSubtype };
	}


	UINT32 unFlags = MFT_ENUM_FLAG_SYNCMFT | MFT_ENUM_FLAG_LOCALMFT |
		MFT_ENUM_FLAG_SORTANDFILTER;

	if (bAllowAsync)
	{
		unFlags |= MFT_ENUM_FLAG_ASYNCMFT;
	}
	if (bAllowHardware)
	{
		unFlags |= MFT_ENUM_FLAG_HARDWARE;
	}
	if (bAllowTranscode)
	{
		unFlags |= MFT_ENUM_FLAG_TRANSCODE_ONLY;
	}

	hr = MFTEnumEx(MFT_CATEGORY_VIDEO_DECODER,
		unFlags,
		inputInfo,      // Input type
		outputInfo,       // Output type
		&ppActivate,
		&count);

	delete inputInfo;
	delete outputInfo;

	if (SUCCEEDED(hr) && count == 0)
	{
		hr = MF_E_TOPO_CODEC_NOT_FOUND;
	}

	// Create the first decoder in the list.
	if (SUCCEEDED(hr))
	{
		hr = ppActivate[0]->ActivateObject(IID_PPV_ARGS(ppDecoder));
	}

	for (UINT32 i = 0; i < count; i++)
	{
		ppActivate[i]->Release();
	}
	CoTaskMemFree(ppActivate);

	return hr;
}
HRESULT CreateIMFTransform(_In_ DWORD streamIndex, _In_ IMFMediaType *pInputMediaType, _In_ IMFMediaType *pOutputMediaType, _Outptr_ IMFTransform **ppVideoConverter)
{
	if (ppVideoConverter) {
		*ppVideoConverter = nullptr;
	}

	CComPtr<IMFTransform> pConverter = NULL;
	HRESULT hr;
	GUID outputMinor;
	RETURN_ON_BAD_HR(pOutputMediaType->GetGUID(MF_MT_SUBTYPE, &outputMinor));
	RETURN_ON_BAD_HR(hr = CoCreateInstance(CLSID_VideoProcessorMFT, NULL, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pConverter)));
	RETURN_ON_BAD_HR(hr = pConverter->SetInputType(streamIndex, pInputMediaType, 0));
	std::vector<GUID> supportedOutputTypes;
	GUID guidMinor;
	GUID guidMajor;
	for (int i = 0;; i++)
	{
		IMFMediaType *mediaType;
		hr = pConverter->GetOutputAvailableType(streamIndex, i, &mediaType);
		if (FAILED(hr))
		{
			break;
		}
		hr = mediaType->GetGUID(MF_MT_MAJOR_TYPE, &guidMajor);
		if (guidMajor == MFMediaType_Video) {
			//LogMediaType(mediaType);
			hr = mediaType->GetGUID(MF_MT_SUBTYPE, &guidMinor);
			if (guidMinor == outputMinor) {
				break;
			}
		}
		mediaType->Release();
	}
	if (FAILED(hr)) {
		return hr;
	}
	RETURN_ON_BAD_HR(hr = pConverter->SetOutputType(streamIndex, pOutputMediaType, 0));
	if (ppVideoConverter) {
		*ppVideoConverter = pConverter;
		(*ppVideoConverter)->AddRef();
	}
	return hr;
}

HRESULT EnumVideoCaptureDevices(_Out_ std::vector<IMFActivate*> *pDevices)
{
	*pDevices = std::vector<IMFActivate *>();
	HRESULT hr = S_OK;
	CComPtr<IMFAttributes> pAttributes = nullptr;
	UINT32 count = 0;
	IMFActivate **ppDevices = NULL;
	ReleaseCOMArrayOnExit releaseDevicesOnExit((IUnknown **)ppDevices, count);
	// Create an attribute store to specify enumeration parameters.
	RETURN_ON_BAD_HR(hr = MFCreateAttributes(&pAttributes, 1));

	//The attribute to be requested is devices that can capture video
	RETURN_ON_BAD_HR(hr = pAttributes->SetGUID(
		MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
		MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID
	));
	//Enummerate the video capture devices
	RETURN_ON_BAD_HR(hr = MFEnumDeviceSources(pAttributes, &ppDevices, &count));
	if (count == 0) {
		LOG_ERROR("No video capture devices found");
		hr = E_FAIL;
	}
	// Try to find a suitable output type.
	for (UINT32 i = 0; i < count; i++)
	{
		IMFActivate *pDevice = ppDevices[i];

			pDevice->AddRef();
			pDevices->push_back(pDevice);
	}
	return hr;
}

HRESULT CopyMediaType(_In_ IMFMediaType *pType, _Outptr_ IMFMediaType **ppType)
{
	CComPtr<IMFMediaType> pTypeUncomp = nullptr;

	HRESULT hr = S_OK;
	GUID majortype = { 0 };
	MFRatio par = { 0 };

	hr = pType->GetMajorType(&majortype);
	if (majortype != MFMediaType_Video)
	{
		return MF_E_INVALIDMEDIATYPE;
	}
	// Create a new media type and copy over all of the items.
	// This ensures that extended color information is retained.
	RETURN_ON_BAD_HR(hr = MFCreateMediaType(&pTypeUncomp));
	RETURN_ON_BAD_HR(hr = pType->CopyAllItems(pTypeUncomp));

	// Fix up PAR if not set on the original type.
	if (SUCCEEDED(hr))
	{
		hr = MFGetAttributeRatio(
			pTypeUncomp,
			MF_MT_PIXEL_ASPECT_RATIO,
			(UINT32 *)&par.Numerator,
			(UINT32 *)&par.Denominator
		);

		// Default to square pixels.
		if (FAILED(hr))
		{
			hr = MFSetAttributeRatio(
				pTypeUncomp,
				MF_MT_PIXEL_ASPECT_RATIO,
				1, 1
			);
		}
	}

	if (SUCCEEDED(hr))
	{
		*ppType = pTypeUncomp;
		(*ppType)->AddRef();
	}

	return hr;
}

HRESULT EnumerateCaptureFormats(_In_ IMFMediaSource *pSource, _Out_ std::vector<IMFMediaType*> *pMediaTypes)
{
	MeasureExecutionTime measure(L"EnumerateCaptureFormats");
	IMFPresentationDescriptor *pPD = NULL;
	IMFStreamDescriptor *pSD = NULL;
	IMFMediaTypeHandler *pHandler = NULL;
	IMFMediaType *pType = NULL;
	*pMediaTypes = std::vector<IMFMediaType*>();


	HRESULT hr = pSource->CreatePresentationDescriptor(&pPD);
	if (FAILED(hr))
	{
		goto done;
	}

	BOOL fSelected;
	hr = pPD->GetStreamDescriptorByIndex(0, &fSelected, &pSD);
	if (FAILED(hr))
	{
		goto done;
	}

	hr = pSD->GetMediaTypeHandler(&pHandler);
	if (FAILED(hr))
	{
		goto done;
	}

	DWORD cTypes = 0;
	hr = pHandler->GetMediaTypeCount(&cTypes);
	if (FAILED(hr))
	{
		goto done;
	}
	//LOG_TRACE("Camera capture formats:")
	for (DWORD i = 0; i < cTypes; i++)
	{
		hr = pHandler->GetMediaTypeByIndex(i, &pType);
		if (FAILED(hr))
		{
			goto done;
		}

		//LogMediaType(pType);
		pMediaTypes->push_back(pType);
		SafeRelease(&pType);
	}

done:
	SafeRelease(&pPD);
	SafeRelease(&pSD);
	SafeRelease(&pHandler);
	SafeRelease(&pType);
	return hr;
}

HRESULT GetFrameSize(_In_ IMFAttributes *pMediaType, _Out_ SIZE *pFrameSize)
{
	UINT32 width;
	UINT32 height;
	//Get width and height
	RETURN_ON_BAD_HR(MFGetAttributeSize(pMediaType, MF_MT_FRAME_SIZE, &width, &height));
	*pFrameSize = SIZE{ (LONG)width,(LONG)height };
	return S_OK;
}

HRESULT GetFrameRate(_In_ IMFMediaType *pMediaType, _Out_ double *pFramerate)
{
	UINT32 numerator;
	UINT32 denominator;
	RETURN_ON_BAD_HR(MFGetAttributeRatio(
		pMediaType,
		MF_MT_FRAME_RATE,
		&numerator,
		&denominator
	));
	double framerate = (double)numerator / denominator;
	*pFramerate = framerate;
	return S_OK;
}

//Calculates the default stride based on the format and size of the frames
HRESULT GetDefaultStride(_In_ IMFMediaType *type, _Out_ LONG *stride)
{
	LONG tempStride = 0;

	// Try to get the default stride from the media type.
	HRESULT hr = type->GetUINT32(MF_MT_DEFAULT_STRIDE, (UINT32 *)&tempStride);
	if (FAILED(hr))
	{
		//Setting this atribute to NULL we can obtain the default stride
		GUID subtype = GUID_NULL;

		UINT32 width = 0;
		UINT32 height = 0;

		// Obtain the subtype
		hr = type->GetGUID(MF_MT_SUBTYPE, &subtype);
		//obtain the width and height
		if (SUCCEEDED(hr))
			hr = MFGetAttributeSize(type, MF_MT_FRAME_SIZE, &width, &height);
		//Calculate the stride based on the subtype and width
		if (SUCCEEDED(hr))
			hr = MFGetStrideForBitmapInfoHeader(subtype.Data1, width, &tempStride);
		// set the attribute so it can be read
		if (SUCCEEDED(hr))
			(void)type->SetUINT32(MF_MT_DEFAULT_STRIDE, UINT32(tempStride));
	}

	if (SUCCEEDED(hr))
		*stride = tempStride;
	return hr;
}

bool IsVideoInfo2(_In_ IMFMediaType *pType)
{
	GUID mediaFormatType;
	pType->GetGUID(MF_MT_AM_FORMAT_TYPE, &mediaFormatType);
	if (mediaFormatType == FORMAT_VideoInfo2) {
		return true;
	}
	return false;
}
