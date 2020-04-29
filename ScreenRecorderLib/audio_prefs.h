#pragma once
// prefs.h
//https://github.com/mvaneerde/blog/tree/master/loopback-capture
#include <map>
#include <mmdeviceapi.h>
#include <string>

class CPrefs {
public:
	IMMDevice *m_pMMDevice;
	HMMIO m_hFile;
	bool m_bInt16;
	PWAVEFORMATEX m_pwfx;
	// set hr to S_FALSE to abort but return success
	CPrefs(int argc, LPCWSTR argv[], HRESULT &hr, EDataFlow flow);
	~CPrefs();
	// writes all found devices for chosen flow into devices
	static HRESULT list_devices(EDataFlow flow, std::map<std::wstring, std::wstring>* devices);
};