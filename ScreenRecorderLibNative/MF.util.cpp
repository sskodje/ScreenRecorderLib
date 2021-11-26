#include "MF.util.h"
#include "Cleanup.h"
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

HRESULT EnumVideoCaptureDevices(_Out_ std::map<std::wstring, std::wstring> *pDevices)
{
	*pDevices = std::map<std::wstring, std::wstring>();
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
		WCHAR *symbolicLink = NULL;
		UINT32 cchSymbolicLink;
		IMFActivate *pDevice = ppDevices[i];
		hr = pDevice->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, &symbolicLink, &cchSymbolicLink);
		CoTaskMemFreeOnExit freeSymbolicLink(symbolicLink);
		if (symbolicLink == NULL) {
			continue;
		}

		WCHAR *nameString = NULL;
		// Get the human-friendly name of the device
		UINT32 cchName;
		hr = pDevice->GetAllocatedString(
			MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME,
			&nameString, &cchName);

		if (SUCCEEDED(hr) && nameString != NULL)
		{
			//allocate a byte buffer for the raw pixel data
			std::wstring deviceName = std::wstring(nameString);
			std::wstring deviceSymbolicLink = std::wstring(symbolicLink);
			pDevices->insert(std::pair<std::wstring, std::wstring>(deviceSymbolicLink, deviceName));
		}
		CoTaskMemFree(nameString);
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