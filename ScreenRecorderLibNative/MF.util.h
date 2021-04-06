#pragma once
#include <mftransform.h>
#include <vector>
#include <sal.h>
#include <mfapi.h>
#include <Mferror.h>
#include <map>
#include <string>

HRESULT FindDecoderEx(const GUID &subtype, BOOL bAudio, IMFTransform **ppDecoder);
HRESULT FindVideoDecoder(GUID *inputSubtype, GUID *outputSubtype, BOOL bAllowAsync, BOOL bAllowHardware, BOOL bAllowTranscode, IMFTransform **ppDecoder);
HRESULT CreateIMFTransform(_In_ DWORD streamIndex, _In_ IMFMediaType *pInputMediaType, _In_ IMFMediaType *pOutputMediaType, _Outptr_ IMFTransform **ppVideoConverter);
HRESULT EnumVideoCaptureDevices(_Out_ std::map<std::wstring, std::wstring> *devices);