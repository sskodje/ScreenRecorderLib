#pragma once
#include <mftransform.h>
#include <vector>
#include <sal.h>
#include <mfapi.h>
#include <Mferror.h>
#include <map>
#include <string>
#include <mfidl.h>

HRESULT FindDecoderEx(const GUID &subtype, BOOL bAudio, IMFTransform **ppDecoder);
HRESULT FindVideoDecoder(GUID *inputSubtype, GUID *outputSubtype, BOOL bAllowAsync, BOOL bAllowHardware, BOOL bAllowTranscode, IMFTransform **ppDecoder);
HRESULT CreateIMFTransform(_In_ DWORD streamIndex, _In_ IMFMediaType *pInputMediaType, _In_ IMFMediaType *pOutputMediaType, _Outptr_ IMFTransform **ppVideoConverter);
HRESULT EnumVideoCaptureDevices(_Out_ std::vector<IMFActivate *> *pDevices);
HRESULT CopyMediaType(_In_ IMFMediaType *pType, _Outptr_ IMFMediaType **ppType);
HRESULT EnumerateCaptureFormats(_In_ IMFMediaSource *pSource, _Out_ std::vector<IMFMediaType*> *pMediaTypes);
HRESULT GetFrameRate(_In_ IMFMediaType *pMediaType, _Out_ double *pFramerate);
HRESULT GetFrameSize(_In_ IMFAttributes *pMediaType, _Out_ SIZE *pFrameSize);
HRESULT GetDefaultStride(_In_ IMFMediaType *pType, _Out_ LONG *plStride);
bool IsVideoInfo2(_In_ IMFMediaType *pType);