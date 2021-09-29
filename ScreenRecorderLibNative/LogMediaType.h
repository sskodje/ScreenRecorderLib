#pragma once
#include <Windows.h>
#include <mfobjects.h>
#include <mfapi.h>
#include <wincodec.h>
#include <Mferror.h>
#include <strsafe.h>

#ifndef IF_EQUAL_RETURN
#define IF_EQUAL_RETURN(param, val) if(val == param) return L#val
#endif

LPCWSTR GetGUIDNameConst(const GUID &guid);
HRESULT GetGUIDName(const GUID &guid, WCHAR **ppwsz);

HRESULT LogAttributeValueByIndex(IMFAttributes *pAttr, DWORD index);
HRESULT SpecialCaseAttributeValue(GUID guid, const PROPVARIANT &var);

void DBGMSG(PCWSTR format, ...);

HRESULT LogMediaType(IMFMediaType *pType);
