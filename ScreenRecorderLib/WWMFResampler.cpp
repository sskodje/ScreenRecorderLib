//#pragma warning(disable:4127)  // Disable warning C4127: conditional expression is constant

//#define WINVER _WIN32_WINNT_WIN7

#include "WWMFResampler.h"
#include "WWUtil.h"
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
CreateAudioMediaType(const WWMFPcmFormat &fmt, IMFMediaType** ppMediaType)
{
	HRESULT hr;
	IMFMediaType *pMediaType = NULL;
	*ppMediaType = NULL;

	HRG(MFCreateMediaType(&pMediaType));
	HRG(pMediaType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio));
	HRG(pMediaType->SetGUID(MF_MT_SUBTYPE,
		(fmt.sampleFormat == WWMFBitFormatInt) ? MFAudioFormat_PCM : MFAudioFormat_Float));
	HRG(pMediaType->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, fmt.nChannels));
	HRG(pMediaType->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, fmt.sampleRate));
	HRG(pMediaType->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, fmt.FrameBytes()));
	HRG(pMediaType->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, fmt.BytesPerSec()));
	HRG(pMediaType->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, fmt.bits));
	HRG(pMediaType->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE));
	if (0 != fmt.dwChannelMask) {
		HRG(pMediaType->SetUINT32(MF_MT_AUDIO_CHANNEL_MASK, fmt.dwChannelMask));
	}
	if (fmt.bits != fmt.validBitsPerSample) {
		HRG(pMediaType->SetUINT32(MF_MT_AUDIO_VALID_BITS_PER_SAMPLE, fmt.validBitsPerSample));
	}

	*ppMediaType = pMediaType;
	pMediaType = NULL; //< prevent release

end:
	SafeRelease(&pMediaType);
	return hr;
}

static HRESULT
CreateResamplerMFT(
	const WWMFPcmFormat &fmtIn,
	const WWMFPcmFormat &fmtOut,
	int halfFilterLength,
	IMFTransform **ppTransform)
{
	HRESULT hr = S_OK;
	CComPtr<IMFMediaType> spInputType;
	CComPtr<IMFMediaType> spOutputType;
	CComPtr<IUnknown> spTransformUnk;
	CComPtr<IWMResamplerProps> spResamplerProps;
	IMFTransform *pTransform = NULL;
	assert(ppTransform);
	*ppTransform = NULL;

	HRG(CoCreateInstance(CLSID_CResamplerMediaObject, NULL, CLSCTX_INPROC_SERVER,
		IID_IUnknown, (void**)&spTransformUnk));

	HRG(spTransformUnk->QueryInterface(IID_PPV_ARGS(&pTransform)));

	HRG(CreateAudioMediaType(fmtIn, &spInputType));
	HRG(pTransform->SetInputType(0, spInputType, 0));

	HRG(CreateAudioMediaType(fmtOut, &spOutputType));
	HRG(pTransform->SetOutputType(0, spOutputType, 0));

	HRG(spTransformUnk->QueryInterface(IID_PPV_ARGS(&spResamplerProps)));
	HRG(spResamplerProps->SetHalfFilterLength(halfFilterLength));

	*ppTransform = pTransform;
	pTransform = NULL; //< prevent release

end:
	SafeRelease(&pTransform);
	return hr;
}

HRESULT
WWMFResampler::Initialize(const WWMFPcmFormat &inputFormat, const WWMFPcmFormat &outputFormat, int halfFilterLength)
{
	HRESULT hr = S_OK;
	m_inputFormat = inputFormat;
	m_outputFormat = outputFormat;
	assert(m_pTransform == NULL);

	m_inputFrameTotal = 0;
	m_outputFrameTotal = 0;

	HRG(MFStartup(MF_VERSION, MFSTARTUP_NOSOCKET));
	m_isMFStartuped = true;

	HRG(CreateResamplerMFT(m_inputFormat, m_outputFormat, halfFilterLength, &m_pTransform));

	HRG(m_pTransform->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, NULL));
	HRG(m_pTransform->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, NULL));
	HRG(m_pTransform->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, NULL));

end:
	return hr;
}

HRESULT
WWMFResampler::ConvertWWSampleDataToMFSample(WWMFSampleData &sampleData, IMFSample **ppSample)
{
	HRESULT hr = S_OK;
	IMFSample *pSample = NULL;
	CComPtr<IMFMediaBuffer> spBuffer;
	BYTE  *pByteBufferTo = NULL;
	//LONGLONG hnsSampleDuration;
	//LONGLONG hnsSampleTime;
	int frameCount;

	assert(ppSample);
	*ppSample = NULL;

	HRG(MFCreateMemoryBuffer(sampleData.bytes, &spBuffer));
	HRG(spBuffer->Lock(&pByteBufferTo, NULL, NULL));

	memcpy(pByteBufferTo, sampleData.data, sampleData.bytes);

	pByteBufferTo = NULL;
	HRG(spBuffer->Unlock());
	HRG(spBuffer->SetCurrentLength(sampleData.bytes));

	HRG(MFCreateSample(&pSample));
	HRG(pSample->AddBuffer(spBuffer));

	frameCount = sampleData.bytes / m_inputFormat.FrameBytes();
	/*
	hnsSampleDuration = (LONGLONG)(10.0 * 1000 * 1000 * frameCount        / m_inputFormat.sampleRate);
	hnsSampleTime     = (LONGLONG)(10.0 * 1000 * 1000 * m_inputFrameTotal / m_inputFormat.sampleRate);
	HRG(pSample->SetSampleDuration(hnsSampleDuration));
	HRG(pSample->SetSampleTime(hnsSampleTime));
	*/

	m_inputFrameTotal += frameCount;

	// succeeded.

	*ppSample = pSample;
	pSample = NULL; //< prevent release

end:
	SafeRelease(&pSample);
	return hr;
}

HRESULT
WWMFResampler::ConvertMFSampleToWWSampleData(IMFSample *pSample, WWMFSampleData *sampleData_return)
{
	HRESULT hr = E_FAIL;
	CComPtr<IMFMediaBuffer> spBuffer;
	BYTE  *pByteBuffer = NULL;
	assert(pSample);
	DWORD cbBytes = 0;

	assert(sampleData_return);
	assert(NULL == sampleData_return->data);

	HRG(pSample->ConvertToContiguousBuffer(&spBuffer));
	HRG(spBuffer->GetCurrentLength(&cbBytes));
	if (0 == cbBytes) {
		sampleData_return->bytes = 0;
		sampleData_return->data = NULL;
		hr = S_OK;
		goto end;
	}

	HRG(spBuffer->Lock(&pByteBuffer, NULL, NULL));

	assert(NULL == sampleData_return->data);
	sampleData_return->data = new BYTE[cbBytes];
	if (NULL == sampleData_return->data) {
		printf("no memory\n");
		goto end;
	}
	memcpy(sampleData_return->data, pByteBuffer, cbBytes);
	sampleData_return->bytes = cbBytes;

	m_outputFrameTotal += cbBytes / m_outputFormat.FrameBytes();

	pByteBuffer = NULL;
	HRG(spBuffer->Unlock());

end:
	return hr;
}

HRESULT
WWMFResampler::GetSampleDataFromMFTransform(WWMFSampleData *sampleData_return)
{
	HRESULT hr = S_OK;
	IMFMediaBuffer *pBuffer = NULL;
	MFT_OUTPUT_STREAM_INFO streamInfo;
	MFT_OUTPUT_DATA_BUFFER outputDataBuffer;
	DWORD dwStatus;
	memset(&streamInfo, 0, sizeof streamInfo);
	memset(&outputDataBuffer, 0, sizeof outputDataBuffer);

	assert(sampleData_return);
	assert(NULL == sampleData_return->data);

	HRG(MFCreateSample(&(outputDataBuffer.pSample)));
	HRG(MFCreateMemoryBuffer(sampleData_return->bytes, &pBuffer));
	HRG(outputDataBuffer.pSample->AddBuffer(pBuffer));
	outputDataBuffer.dwStreamID = 0;
	outputDataBuffer.dwStatus = 0;
	outputDataBuffer.pEvents = NULL;

	hr = m_pTransform->ProcessOutput(0, 1, &outputDataBuffer, &dwStatus);
	if (FAILED(hr)) {
		goto end;
	}

	HRG(ConvertMFSampleToWWSampleData(outputDataBuffer.pSample, sampleData_return));

end:
	SafeRelease(&pBuffer);
	SafeRelease(&outputDataBuffer.pSample);
	return hr;
}

HRESULT
WWMFResampler::Resample(const BYTE *buff, DWORD bytes, WWMFSampleData *sampleData_return)
{
	HRESULT hr = E_FAIL;
	IMFSample *pSample = NULL;
	WWMFSampleData tmpData;
	WWMFSampleData inputData((BYTE*)buff, bytes);
	DWORD dwStatus;
	DWORD cbOutputBytes = (DWORD)((int64_t)bytes * m_outputFormat.BytesPerSec() / m_inputFormat.BytesPerSec());
	// cbOutputBytes must be product of frambytes
	cbOutputBytes = (cbOutputBytes + (m_outputFormat.FrameBytes() - 1)) / m_outputFormat.FrameBytes() * m_outputFormat.FrameBytes();
	// add extra receive size
	cbOutputBytes += 16 * m_outputFormat.FrameBytes();

	assert(sampleData_return);
	assert(NULL == sampleData_return->data);

	HRG(ConvertWWSampleDataToMFSample(inputData, &pSample));

	HRG(m_pTransform->GetInputStatus(0, &dwStatus));
	if (MFT_INPUT_STATUS_ACCEPT_DATA != dwStatus) {
		dprintf("E: ApplyTransform() pTransform->GetInputStatus() not accept data.\n");
		hr = E_FAIL;
		goto end;
	}

	HRG(m_pTransform->ProcessInput(0, pSample, 0));

	// set sampleData_return->bytes = 0
	sampleData_return->Forget();
	for (;;) {
		tmpData.bytes = cbOutputBytes;
		hr = GetSampleDataFromMFTransform(&tmpData);
		if (MF_E_TRANSFORM_NEED_MORE_INPUT == hr) {
			hr = S_OK;
			goto end;
		}
		if (FAILED(hr)) {
			goto end;
		}
		sampleData_return->MoveAdd(tmpData);
		tmpData.Release();
	}

end:
	tmpData.Release();
	inputData.Forget();
	SafeRelease(&pSample);
	return hr;
}

HRESULT
WWMFResampler::Drain(DWORD resampleInputBytes, WWMFSampleData *sampleData_return)
{
	HRESULT hr = S_OK;
	WWMFSampleData tmpData;
	DWORD cbOutputBytes = (DWORD)((int64_t)resampleInputBytes * m_outputFormat.BytesPerSec() / m_inputFormat.BytesPerSec());
	// cbOutputBytes must be product of frambytes
	cbOutputBytes = (cbOutputBytes + (m_outputFormat.FrameBytes() - 1)) / m_outputFormat.FrameBytes() * m_outputFormat.FrameBytes();

	assert(sampleData_return);
	assert(NULL == sampleData_return->data);

	HRG(m_pTransform->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, NULL));
	HRG(m_pTransform->ProcessMessage(MFT_MESSAGE_COMMAND_DRAIN, NULL));

	// set sampleData_return->bytes = 0
	sampleData_return->Forget();
	for (;;) {
		tmpData.bytes = cbOutputBytes;
		hr = GetSampleDataFromMFTransform(&tmpData);
		if (MF_E_TRANSFORM_NEED_MORE_INPUT == hr) {
			// end
			HRG(m_pTransform->ProcessMessage(MFT_MESSAGE_NOTIFY_END_STREAMING, NULL));
			goto end;
		}
		if (FAILED(hr)) {
			goto end;
		}
		sampleData_return->MoveAdd(tmpData);
		tmpData.Release();
	}

end:
	tmpData.Release();
	return hr;
}

void
WWMFResampler::Finalize(void)
{
	SafeRelease(&m_pTransform);
	if (m_isMFStartuped) {
		MFShutdown();
		m_isMFStartuped = false;
	}
}

WWMFResampler::~WWMFResampler(void)
{
	assert(NULL == m_pTransform);
	assert(false == m_isMFStartuped);
}