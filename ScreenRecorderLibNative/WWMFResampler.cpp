//#pragma warning(disable:4127)  // Disable warning C4127: conditional expression is constant

//#define WINVER _WIN32_WINNT_WIN7

#include "WWMFResampler.h"
#include "Util.h"
#include "Cleanup.h"
#include <windows.h>
#include <atlbase.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mferror.h>
#include <wmcodecdsp.h>
#include <stdio.h>
#include <assert.h>
#include <stdint.h>

#pragma comment(lib, "mfplat")
#pragma comment(lib, "mf")
#pragma comment(lib, "mfuuid")
#pragma comment(lib, "wmcodecdspuuid")

enum WWAvailableType {
	WWAvailableInput,
	WWAvailableOutput,
};

static HRESULT
CreateAudioMediaType(const WWMFPcmFormat &fmt, IMFMediaType **ppMediaType)
{
	CComPtr<IMFMediaType> pMediaType = NULL;
	*ppMediaType = NULL;

	RETURN_ON_BAD_HR(MFCreateMediaType(&pMediaType));
	RETURN_ON_BAD_HR(pMediaType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio));
	RETURN_ON_BAD_HR(pMediaType->SetGUID(MF_MT_SUBTYPE,
		(fmt.sampleFormat == WWMFBitFormatType::WWMFBitFormatInt) ? MFAudioFormat_PCM : MFAudioFormat_Float));
	RETURN_ON_BAD_HR(pMediaType->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, fmt.nChannels));
	RETURN_ON_BAD_HR(pMediaType->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, fmt.sampleRate));
	RETURN_ON_BAD_HR(pMediaType->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, fmt.FrameBytes()));
	RETURN_ON_BAD_HR(pMediaType->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, fmt.BytesPerSec()));
	RETURN_ON_BAD_HR(pMediaType->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, fmt.bits));
	RETURN_ON_BAD_HR(pMediaType->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE));
	if (0 != fmt.dwChannelMask) {
		RETURN_ON_BAD_HR(pMediaType->SetUINT32(MF_MT_AUDIO_CHANNEL_MASK, fmt.dwChannelMask));
	}
	if (fmt.bits != fmt.validBitsPerSample) {
		RETURN_ON_BAD_HR(pMediaType->SetUINT32(MF_MT_AUDIO_VALID_BITS_PER_SAMPLE, fmt.validBitsPerSample));
	}

	*ppMediaType = pMediaType;
	(*ppMediaType)->AddRef();

	return S_OK;
}

static HRESULT
CreateResamplerMFT(
	const WWMFPcmFormat &fmtIn,
	const WWMFPcmFormat &fmtOut,
	int halfFilterLength,
	IMFTransform **ppTransform)
{
	CComPtr<IMFMediaType> spInputType;
	CComPtr<IMFMediaType> spOutputType;
	CComPtr<IUnknown> spTransformUnk;
	CComPtr<IWMResamplerProps> spResamplerProps;
	CComPtr<IMFTransform> pTransform = NULL;

	assert(ppTransform);
	*ppTransform = NULL;

	RETURN_ON_BAD_HR(CoCreateInstance(CLSID_CResamplerMediaObject, NULL, CLSCTX_INPROC_SERVER,
		IID_IUnknown, (void **)&spTransformUnk));

	RETURN_ON_BAD_HR(spTransformUnk->QueryInterface(IID_PPV_ARGS(&pTransform)));

	RETURN_ON_BAD_HR(CreateAudioMediaType(fmtIn, &spInputType));
	RETURN_ON_BAD_HR(pTransform->SetInputType(0, spInputType, 0));

	RETURN_ON_BAD_HR(CreateAudioMediaType(fmtOut, &spOutputType));
	RETURN_ON_BAD_HR(pTransform->SetOutputType(0, spOutputType, 0));

	RETURN_ON_BAD_HR(spTransformUnk->QueryInterface(IID_PPV_ARGS(&spResamplerProps)));
	RETURN_ON_BAD_HR(spResamplerProps->SetHalfFilterLength(halfFilterLength));

	*ppTransform = pTransform;
	(*ppTransform)->AddRef();

	return S_OK;
}

HRESULT
WWMFResampler::Initialize(const WWMFPcmFormat &inputFormat, const WWMFPcmFormat &outputFormat, int halfFilterLength)
{
	m_inputFormat = inputFormat;
	m_outputFormat = outputFormat;
	assert(m_pTransform == NULL);

	m_inputFrameTotal = 0;
	m_outputFrameTotal = 0;

	RETURN_ON_BAD_HR(CreateResamplerMFT(m_inputFormat, m_outputFormat, halfFilterLength, &m_pTransform));

	RETURN_ON_BAD_HR(m_pTransform->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, NULL));
	RETURN_ON_BAD_HR(m_pTransform->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, NULL));
	RETURN_ON_BAD_HR(m_pTransform->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, NULL));

	return S_OK;
}

HRESULT
WWMFResampler::ConvertWWSampleDataToMFSample(WWMFSampleData &sampleData, IMFSample **ppSample)
{
	CComPtr<IMFSample> pSample;

	CComPtr<IMFMediaBuffer> spBuffer;
	BYTE *pByteBufferTo = NULL;

	int frameCount;

	assert(ppSample);
	*ppSample = NULL;

	RETURN_ON_BAD_HR(MFCreateMemoryBuffer(sampleData.bytes, &spBuffer));
	RETURN_ON_BAD_HR(spBuffer->Lock(&pByteBufferTo, NULL, NULL));

	memcpy(pByteBufferTo, sampleData.data, sampleData.bytes);

	pByteBufferTo = NULL;
	RETURN_ON_BAD_HR(spBuffer->Unlock());
	RETURN_ON_BAD_HR(spBuffer->SetCurrentLength(sampleData.bytes));

	RETURN_ON_BAD_HR(MFCreateSample(&pSample));
	RETURN_ON_BAD_HR(pSample->AddBuffer(spBuffer));

	frameCount = sampleData.bytes / m_inputFormat.FrameBytes();

	m_inputFrameTotal += frameCount;

	// succeeded.

	*ppSample = pSample;
	(*ppSample)->AddRef();

	return S_OK;
}

HRESULT
WWMFResampler::ConvertMFSampleToWWSampleData(IMFSample *pSample, WWMFSampleData *sampleData_return)
{
	CComPtr<IMFMediaBuffer> spBuffer;
	BYTE *pByteBuffer = NULL;
	assert(pSample);
	DWORD cbBytes = 0;

	assert(sampleData_return);
	assert(NULL == sampleData_return->data);

	RETURN_ON_BAD_HR(pSample->ConvertToContiguousBuffer(&spBuffer));
	RETURN_ON_BAD_HR(spBuffer->GetCurrentLength(&cbBytes));
	if (0 == cbBytes) {
		sampleData_return->bytes = 0;
		sampleData_return->data = NULL;
		return S_OK;
	}

	RETURN_ON_BAD_HR(spBuffer->Lock(&pByteBuffer, NULL, NULL));

	assert(NULL == sampleData_return->data);
	sampleData_return->data = new BYTE[cbBytes];
	if (NULL == sampleData_return->data) {
		printf("no memory\n");
		return E_FAIL;
	}
	memcpy(sampleData_return->data, pByteBuffer, cbBytes);
	sampleData_return->bytes = cbBytes;

	m_outputFrameTotal += cbBytes / m_outputFormat.FrameBytes();

	pByteBuffer = NULL;
	RETURN_ON_BAD_HR(spBuffer->Unlock());

	return S_OK;
}

HRESULT
WWMFResampler::GetSampleDataFromMFTransform(WWMFSampleData *sampleData_return)
{
	IMFMediaBuffer *pBuffer = NULL;
	MFT_OUTPUT_STREAM_INFO streamInfo;
	MFT_OUTPUT_DATA_BUFFER outputDataBuffer;

	DWORD dwStatus;
	memset(&streamInfo, 0, sizeof streamInfo);
	memset(&outputDataBuffer, 0, sizeof outputDataBuffer);

	assert(sampleData_return);
	assert(NULL == sampleData_return->data);

	RETURN_ON_BAD_HR(MFCreateSample(&(outputDataBuffer.pSample)));
	RETURN_ON_BAD_HR(MFCreateMemoryBuffer(sampleData_return->bytes, &pBuffer));
	RETURN_ON_BAD_HR(outputDataBuffer.pSample->AddBuffer(pBuffer));
	ReleaseOnExit releaseBuffer(pBuffer);
	ReleaseOnExit releaseOutputDataBufferSample(outputDataBuffer.pSample);
	outputDataBuffer.dwStreamID = 0;
	outputDataBuffer.dwStatus = 0;
	outputDataBuffer.pEvents = NULL;

	HRESULT hr = m_pTransform->ProcessOutput(0, 1, &outputDataBuffer, &dwStatus);
	if (FAILED(hr))
	{
		return hr;
	}


	RETURN_ON_BAD_HR(ConvertMFSampleToWWSampleData(outputDataBuffer.pSample, sampleData_return));

	return S_OK;
}

HRESULT
WWMFResampler::Resample(const BYTE *buff, DWORD bytes, WWMFSampleData *sampleData_return)
{
	IMFSample *pSample = NULL;
	WWMFSampleData tmpData;
	ReleaseWWMFSampleDataOnExit releaseTmpData(&tmpData);
	WWMFSampleData inputData((BYTE *)buff, bytes);
	ForgetWWMFSampleDataOnExit forgetTmpData(&inputData);
	DWORD dwStatus;
	DWORD cbOutputBytes = (DWORD)((int64_t)bytes * m_outputFormat.BytesPerSec() / m_inputFormat.BytesPerSec());
	// cbOutputBytes must be product of frambytes
	cbOutputBytes = (cbOutputBytes + (m_outputFormat.FrameBytes() - 1)) / m_outputFormat.FrameBytes() * m_outputFormat.FrameBytes();
	// add extra receive size
	cbOutputBytes += 16 * m_outputFormat.FrameBytes();

	assert(sampleData_return);
	assert(NULL == sampleData_return->data);

	RETURN_ON_BAD_HR(ConvertWWSampleDataToMFSample(inputData, &pSample));
	ReleaseOnExit releaseSample(pSample);

	RETURN_ON_BAD_HR(m_pTransform->GetInputStatus(0, &dwStatus));
	if (MFT_INPUT_STATUS_ACCEPT_DATA != dwStatus) {
		LOG_ERROR("ApplyTransform() pTransform->GetInputStatus() not accept data.\n");
		return E_FAIL;
	}

	RETURN_ON_BAD_HR(m_pTransform->ProcessInput(0, pSample, 0));

	// set sampleData_return->bytes = 0
	sampleData_return->Forget();
	for (;;) {
		tmpData.bytes = cbOutputBytes;
		HRESULT hr = GetSampleDataFromMFTransform(&tmpData);
		if (MF_E_TRANSFORM_NEED_MORE_INPUT == hr) {
			return S_OK;
		}
		if (FAILED(hr)) {
			return hr;
		}
		sampleData_return->MoveAdd(tmpData);
		tmpData.Release();
	}

	return S_OK;
}

HRESULT
WWMFResampler::Drain(DWORD resampleInputBytes, WWMFSampleData *sampleData_return)
{
	DWORD cbOutputBytes = (DWORD)((int64_t)resampleInputBytes * m_outputFormat.BytesPerSec() / m_inputFormat.BytesPerSec());
	// cbOutputBytes must be product of frambytes
	cbOutputBytes = (cbOutputBytes + (m_outputFormat.FrameBytes() - 1)) / m_outputFormat.FrameBytes() * m_outputFormat.FrameBytes();

	assert(sampleData_return);
	assert(NULL == sampleData_return->data);

	RETURN_ON_BAD_HR(m_pTransform->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, NULL));
	RETURN_ON_BAD_HR(m_pTransform->ProcessMessage(MFT_MESSAGE_COMMAND_DRAIN, NULL));

	// set sampleData_return->bytes = 0
	sampleData_return->Forget();
	for (;;) {
		WWMFSampleData tmpData;
		tmpData.bytes = cbOutputBytes;
		HRESULT hr = GetSampleDataFromMFTransform(&tmpData);
		ReleaseWWMFSampleDataOnExit releaseTmpData(&tmpData);
		if (MF_E_TRANSFORM_NEED_MORE_INPUT == hr) {
			// end
			RETURN_ON_BAD_HR(m_pTransform->ProcessMessage(MFT_MESSAGE_NOTIFY_END_STREAMING, NULL));
		}
		RETURN_ON_BAD_HR(hr);
		sampleData_return->MoveAdd(tmpData);
		tmpData.Release();
	}

	return S_OK;
}

void
WWMFResampler::Finalize(void)
{
	SafeRelease(&m_pTransform);
}

WWMFResampler::~WWMFResampler(void)
{
	Finalize();
}