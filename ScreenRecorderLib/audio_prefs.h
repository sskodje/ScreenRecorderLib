// prefs.h
//https://github.com/mvaneerde/blog/tree/master/loopback-capture
class CPrefs {
public:
	IMMDevice *m_pMMDevice;
	HMMIO m_hFile;
	bool m_bInt16;
	PWAVEFORMATEX m_pwfx;
	// set hr to S_FALSE to abort but return success
	CPrefs(int argc, LPCWSTR argv[], HRESULT &hr);
	~CPrefs();

};
