#pragma once

//#define WINVER _WIN32_WINNT_WIN7

#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <assert.h>

/// sample data type. int or float
/// it is compatible to WWBitFormatType on WasapiUser.h
enum WWMFBitFormatType {
	WWMFBitFormatUnknown = -1,
	WWMFBitFormatInt,
	WWMFBitFormatFloat,
	WWMFBitFormatNUM
};

struct WWMFPcmFormat {
	WWMFBitFormatType sampleFormat;
	WORD  nChannels;
	WORD  bits;
	DWORD sampleRate;
	DWORD dwChannelMask;
	WORD  validBitsPerSample;

	WWMFPcmFormat(void) {
		sampleFormat = WWMFBitFormatUnknown;
		nChannels = 0;
		bits = 0;
		sampleRate = 0;
		dwChannelMask = 0;
		validBitsPerSample = 0;
	}

	WWMFPcmFormat(WWMFBitFormatType aSampleFormat, WORD aNChannels, WORD aBits,
		DWORD aSampleRate, DWORD aDwChannelMask, WORD aValidBitsPerSample) {
		sampleFormat = aSampleFormat;
		nChannels = aNChannels;
		bits = aBits;
		sampleRate = aSampleRate;
		dwChannelMask = aDwChannelMask;
		validBitsPerSample = aValidBitsPerSample;
	}

	WORD FrameBytes(void) const {
		return (WORD)(nChannels * bits / 8U);
	}

	DWORD BytesPerSec(void) const {
		return sampleRate * FrameBytes();
	}
};

/// WWMFSampleData contains new[] ed byte buffer pointer(data) and buffer size(bytes).
struct WWMFSampleData {
	DWORD  bytes;
	BYTE  *data;

	WWMFSampleData(void) : bytes(0), data(NULL) { }

	/// @param aData must point new[] ed memory address
	WWMFSampleData(BYTE *aData, int aBytes) {
		data = aData;
		bytes = aBytes;
	}

	~WWMFSampleData(void) {
		assert(NULL == data);
	}

	void Release(void) {
		delete[] data;
		data = NULL;
		bytes = 0;
	}

	void Forget(void) {
		data = NULL;
		bytes = 0;
	}

	HRESULT Add(WWMFSampleData &rhs) {
		BYTE *buff = new BYTE[bytes + rhs.bytes];
		if (NULL == buff) {
			return E_FAIL;
		}

		memcpy(buff, data, bytes);
		memcpy(&buff[bytes], rhs.data, rhs.bytes);

		delete[] data;
		data = buff;
		bytes += rhs.bytes;
		return S_OK;
	}

	/**
	 * If this instance is not empty, rhs content is concatenated to this instance. rhs remains untouched.
	 * If this instance is empty, rhs content moves to this instance. rhs becomes empty.
	 * rhs.Release() must be called to release memory either way!
	 */
	HRESULT MoveAdd(WWMFSampleData &rhs) {
		if (bytes != 0) {
			return Add(rhs);
		}

		assert(NULL == data);
		*this = rhs; //< Just copy 8 bytes. It's way faster than Add()
		rhs.Forget();

		return S_OK;
	}
};

class WWMFResampler {
public:
	WWMFResampler(void) :
		m_pTransform(NULL),
		m_isMFStartuped(false),
		m_inputFrameTotal(0),
		m_outputFrameTotal(0)
	{

	}
	~WWMFResampler(void);

	/// @param halfFilterLength conversion quality. 1(min) to 60 (max)
	HRESULT Initialize(const WWMFPcmFormat &inputFormat, const WWMFPcmFormat &outputFormat, int halfFilterLength);

	/// @bytes buffer bytes. must be smaller than approx. 512KB to convert 44100Hz to 192000Hz
	HRESULT Resample(const BYTE *buff, DWORD bytes, WWMFSampleData *sampleData_return);

	/// @param resampleInputBytes input buffer bytes of Resample(). this arg is used to calculate expected output buffer size
	/// @param sampleData_return [out] set fresh (its data shold not be allocated yet) WWMFSampleData instance as this arg
	HRESULT Drain(DWORD resampleInputBytes, WWMFSampleData *sampleData_return);

	/// Finalize must be called even when Initialize() is failed
	void Finalize(void);

	INT64 GetOutputFrameTotal(void) const {
		return m_outputFrameTotal;
	}

	INT64 GetInputFrameTotal(void) const {
		return m_inputFrameTotal;
	}

private:
	IMFTransform *m_pTransform;
	WWMFPcmFormat m_inputFormat;
	WWMFPcmFormat m_outputFormat;
	bool          m_isMFStartuped;
	INT64      m_inputFrameTotal;
	INT64      m_outputFrameTotal;


	HRESULT ConvertWWSampleDataToMFSample(WWMFSampleData &sampleData, IMFSample **ppSample);
	HRESULT ConvertMFSampleToWWSampleData(IMFSample *pSample, WWMFSampleData *sampleData_return);
	HRESULT GetSampleDataFromMFTransform(WWMFSampleData *sampleData_return);
};