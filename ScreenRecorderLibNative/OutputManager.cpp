#include "OutputManager.h"
#include "screengrab.h"
#include <ppltasks.h> 
#include <concrt.h>
#include <filesystem>
using namespace std;
using namespace concurrency;

OutputManager::OutputManager() :
	m_Device(nullptr),
	m_DeviceContext(nullptr),
	m_CallBack(nullptr),
	m_FinalizeEvent(nullptr),
	m_SinkWriter(nullptr),
	m_EncoderOptions(nullptr),
	m_AudioOptions(nullptr),
	m_SnapshotOptions(nullptr),
	m_VideoStreamIndex(0),
	m_AudioStreamIndex(0),
	m_OutputFolder(L""),
	m_OutputFullPath(L""),
	m_LastFrameHadAudio(false),
	m_RenderedFrameCount(0),
	m_RecorderMode(RecorderModeInternal::Video)
{
	m_FinalizeEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
}

OutputManager::~OutputManager()
{
	CloseHandle(m_FinalizeEvent);
	m_FinalizeEvent = nullptr;
}

HRESULT OutputManager::Initialize(_In_ ID3D11DeviceContext *pDeviceContext, _In_ ID3D11Device *pDevice, _In_ std::shared_ptr<ENCODER_OPTIONS> &pEncoderOptions, _In_ std::shared_ptr<AUDIO_OPTIONS> pAudioOptions, _In_ std::shared_ptr<SNAPSHOT_OPTIONS> pSnapshotOptions)
{
	m_DeviceContext = pDeviceContext;
	m_Device = pDevice;
	m_EncoderOptions = pEncoderOptions;
	m_AudioOptions = pAudioOptions;
	m_SnapshotOptions = pSnapshotOptions;
	return S_OK;
}

HRESULT OutputManager::BeginRecording(_In_ std::wstring outputPath, _In_ SIZE videoOutputFrameSize, _In_ RecorderModeInternal recorderMode, _In_opt_ IStream *pStream)
{
	HRESULT hr = S_FALSE;
	m_OutputFullPath = outputPath;
	std::filesystem::path filePath = outputPath;
	m_OutputFolder = filePath.has_extension() ? filePath.parent_path().wstring() : filePath.wstring();
	m_RecorderMode = recorderMode;
	ResetEvent(m_FinalizeEvent);
	if (m_RecorderMode == RecorderModeInternal::Video) {
		CComPtr<IMFByteStream> outputStream = nullptr;
		if (pStream != nullptr) {
			RETURN_ON_BAD_HR(hr = MFCreateMFByteStreamOnStream(pStream, &outputStream));
		}
		if (m_FinalizeEvent) {
			m_CallBack = new (std::nothrow)CMFSinkWriterCallback(m_FinalizeEvent, nullptr);
		}
		RECT inputMediaFrameRect = RECT{ 0,0,videoOutputFrameSize.cx,videoOutputFrameSize.cy };
		RETURN_ON_BAD_HR(hr = InitializeVideoSinkWriter(m_OutputFullPath, outputStream, m_Device, inputMediaFrameRect, videoOutputFrameSize, DXGI_MODE_ROTATION_UNSPECIFIED, m_CallBack, &m_SinkWriter, &m_VideoStreamIndex, &m_AudioStreamIndex));
	}
	return hr;
}


HRESULT OutputManager::FinalizeRecording()
{
	LOG_INFO("Cleaning up resources");
	LOG_INFO("Finalizing recording");
	HRESULT finalizeResult = S_OK;
	if (m_SinkWriter) {

		finalizeResult = m_SinkWriter->Finalize();
		if (SUCCEEDED(finalizeResult) && m_FinalizeEvent) {
			WaitForSingleObject(m_FinalizeEvent, INFINITE);
		}
		if (FAILED(finalizeResult)) {
			LOG_ERROR("Failed to finalize sink writer");
		}
		//Dispose of MPEG4MediaSink 
		IMFMediaSink *pSink;
		if (SUCCEEDED(m_SinkWriter->GetServiceForStream(MF_SINK_WRITER_MEDIASINK, GUID_NULL, IID_PPV_ARGS(&pSink)))) {
			finalizeResult = pSink->Shutdown();
			if (FAILED(finalizeResult)) {
				LOG_ERROR("Failed to shut down IMFMediaSink");
			}
			else {
				LOG_DEBUG("Shut down IMFMediaSink");
			}
		};
	}
	return finalizeResult;
}

HRESULT OutputManager::RenderFrame(_In_ FrameWriteModel &model) {
	HRESULT hr(S_OK);

	if (m_RecorderMode == RecorderModeInternal::Video) {
		hr = WriteFrameToVideo(model.StartPos, model.Duration, m_VideoStreamIndex, model.Frame);
		bool wroteAudioSample = false;
		if (FAILED(hr)) {
			_com_error err(hr);
			LOG_ERROR(L"Writing of video frame with start pos %lld ms failed: %s", (HundredNanosToMillis(model.StartPos)), err.ErrorMessage());
			return hr;//Stop recording if we fail
		}
		bool paddedAudio = false;

		/* If the audio pCaptureInstance returns no data, i.e. the source is silent, we need to pad the PCM stream with zeros to give the media sink silence as input.
		 * If we don't, the sink writer will begin throttling video frames because it expects audio samples to be delivered, and think they are delayed.
		 * We ignore every instance where the last frame had audio, due to sometimes very short frame durations due to mouse cursor changes have zero audio length,
		 * and inserting silence between two frames that has audio leads to glitching. */
		if (GetAudioOptions()->IsAudioEnabled() && model.Audio.size() == 0 && model.Duration > 0) {
			if (!m_LastFrameHadAudio) {
				int frameCount = int(ceil(GetAudioOptions()->GetAudioSamplesPerSecond() * HundredNanosToMillis(model.Duration) / 1000));
				int byteCount = frameCount * (GetAudioOptions()->GetAudioBitsPerSample() / 8) * GetAudioOptions()->GetAudioChannels();
				model.Audio.insert(model.Audio.end(), byteCount, 0);
				paddedAudio = true;
			}
			m_LastFrameHadAudio = false;
		}
		else {
			m_LastFrameHadAudio = true;
		}

		if (model.Audio.size() > 0) {
			hr = WriteAudioSamplesToVideo(model.StartPos, model.Duration, m_AudioStreamIndex, &(model.Audio)[0], (DWORD)model.Audio.size());
			if (FAILED(hr)) {
				_com_error err(hr);
				LOG_ERROR(L"Writing of audio sample with start pos %lld ms failed: %s", (HundredNanosToMillis(model.StartPos)), err.ErrorMessage());
				return hr;//Stop recording if we fail
			}
			else {
				wroteAudioSample = true;
			}
		}
		auto frameInfoStr = wroteAudioSample ? (paddedAudio ? L"video sample and audio padding" : L"video and audio sample") : L"video sample";
		LOG_TRACE(L"Wrote %s with duration %.2f ms", frameInfoStr, HundredNanosToMillisDouble(model.Duration));
	}
	else if (m_RecorderMode == RecorderModeInternal::Slideshow) {
		wstring	path = m_OutputFolder + L"\\" + to_wstring(m_RenderedFrameCount) + GetSnapshotOptions()->GetImageExtension();
		hr = WriteFrameToImage(model.Frame, path);
		INT64 startposMs = HundredNanosToMillis(model.StartPos);
		INT64 durationMs = HundredNanosToMillis(model.Duration);
		if (FAILED(hr)) {
			_com_error err(hr);
			LOG_ERROR(L"Writing of slideshow frame with start pos %lld ms failed: %s", startposMs, err.ErrorMessage());
			return hr; //Stop recording if we fail
		}
		else {

			m_FrameDelays.insert(std::pair<wstring, int>(path, m_RenderedFrameCount == 0 ? 0 : (int)durationMs));
			LOG_TRACE(L"Wrote video slideshow frame with start pos %lld ms and with duration %lld ms", startposMs, durationMs);
		}
	}
	else if (m_RecorderMode == RecorderModeInternal::Screenshot) {
		hr = WriteFrameToImage(model.Frame, m_OutputFullPath);
		LOG_TRACE(L"Wrote snapshot to %s", m_OutputFullPath.c_str());
	}
	model.Frame.Release();
	m_RenderedFrameCount++;
	return hr;
}

HRESULT OutputManager::WriteFrameToImage(_In_ ID3D11Texture2D *pAcquiredDesktopImage, _In_ std::wstring filePath)
{
	return SaveWICTextureToFile(m_DeviceContext, pAcquiredDesktopImage, GetSnapshotOptions()->GetSnapshotEncoderFormat(), filePath.c_str());
}

void OutputManager::WriteTextureToImageAsync(_In_ ID3D11Texture2D *pAcquiredDesktopImage, _In_ std::wstring filePath, _In_opt_ std::function<void(HRESULT)> onCompletion)
{
	pAcquiredDesktopImage->AddRef();
	Concurrency::create_task([this, pAcquiredDesktopImage, filePath, onCompletion]() {
		return WriteFrameToImage(pAcquiredDesktopImage, filePath);
	   }).then([this, filePath, pAcquiredDesktopImage, onCompletion](concurrency::task<HRESULT> t)
		   {
			   HRESULT hr;
			   try {
				   hr = t.get();
				   // if .get() didn't throw and the HRESULT succeeded, there are no errors.
			   }
			   catch (const exception &e) {
				   // handle error
				   LOG_ERROR(L"Exception saving snapshot: %s", e.what());
				   hr = E_FAIL;
			   }
			   pAcquiredDesktopImage->Release();
			   if (onCompletion) {
				   std::invoke(onCompletion, hr);
			   }
			   return hr;
		   });
}


HRESULT OutputManager::ConfigureOutputMediaTypes(
	_In_ UINT destWidth,
	_In_ UINT destHeight,
	_Outptr_ IMFMediaType **pVideoMediaTypeOut,
	_Outptr_result_maybenull_ IMFMediaType **pAudioMediaTypeOut)
{
	*pVideoMediaTypeOut = nullptr;
	*pAudioMediaTypeOut = nullptr;
	CComPtr<IMFMediaType> pVideoMediaType = nullptr;
	CComPtr<IMFMediaType> pAudioMediaType = nullptr;
	// Set the output video type.
	RETURN_ON_BAD_HR(MFCreateMediaType(&pVideoMediaType));
	RETURN_ON_BAD_HR(pVideoMediaType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video));
	RETURN_ON_BAD_HR(pVideoMediaType->SetGUID(MF_MT_SUBTYPE, GetEncoderOptions()->GetVideoEncoderFormat()));
	RETURN_ON_BAD_HR(pVideoMediaType->SetUINT32(MF_MT_AVG_BITRATE, GetEncoderOptions()->GetVideoBitrate()));
	RETURN_ON_BAD_HR(pVideoMediaType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive));
	RETURN_ON_BAD_HR(pVideoMediaType->SetUINT32(MF_MT_MPEG2_PROFILE, GetEncoderOptions()->GetEncoderProfile()));
	RETURN_ON_BAD_HR(pVideoMediaType->SetUINT32(MF_MT_YUV_MATRIX, MFVideoTransferMatrix_BT601));
	RETURN_ON_BAD_HR(MFSetAttributeSize(pVideoMediaType, MF_MT_FRAME_SIZE, destWidth, destHeight));
	RETURN_ON_BAD_HR(MFSetAttributeRatio(pVideoMediaType, MF_MT_FRAME_RATE, GetEncoderOptions()->GetVideoFps(), 1));
	RETURN_ON_BAD_HR(MFSetAttributeRatio(pVideoMediaType, MF_MT_PIXEL_ASPECT_RATIO, 1, 1));

	if (GetAudioOptions()->IsAudioEnabled()) {
		// Set the output audio type.
		RETURN_ON_BAD_HR(MFCreateMediaType(&pAudioMediaType));
		RETURN_ON_BAD_HR(pAudioMediaType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio));
		RETURN_ON_BAD_HR(pAudioMediaType->SetGUID(MF_MT_SUBTYPE, GetAudioOptions()->GetAudioEncoderFormat()));
		RETURN_ON_BAD_HR(pAudioMediaType->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, GetAudioOptions()->GetAudioChannels()));
		RETURN_ON_BAD_HR(pAudioMediaType->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, GetAudioOptions()->GetAudioBitsPerSample()));
		RETURN_ON_BAD_HR(pAudioMediaType->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, GetAudioOptions()->GetAudioSamplesPerSecond()));
		RETURN_ON_BAD_HR(pAudioMediaType->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, GetAudioOptions()->GetAudioBitrate()));

		*pAudioMediaTypeOut = pAudioMediaType;
		(*pAudioMediaTypeOut)->AddRef();
	}

	*pVideoMediaTypeOut = pVideoMediaType;
	(*pVideoMediaTypeOut)->AddRef();
	return S_OK;
}

HRESULT OutputManager::ConfigureInputMediaTypes(
	_In_ UINT sourceWidth,
	_In_ UINT sourceHeight,
	_In_ MFVideoRotationFormat rotationFormat,
	_In_ IMFMediaType *pVideoMediaTypeOut,
	_Outptr_ IMFMediaType **pVideoMediaTypeIn,
	_Outptr_result_maybenull_ IMFMediaType **pAudioMediaTypeIn)
{
	*pVideoMediaTypeIn = nullptr;
	*pAudioMediaTypeIn = nullptr;
	CComPtr<IMFMediaType> pVideoMediaType = nullptr;
	CComPtr<IMFMediaType> pAudioMediaType = nullptr;
	// Copy the output media type
	CopyMediaType(pVideoMediaTypeOut, &pVideoMediaType);
	// Set the subtype.
	RETURN_ON_BAD_HR(pVideoMediaType->SetGUID(MF_MT_SUBTYPE, GetEncoderOptions()->GetVideoInputFormat()));
	// Uncompressed means all samples are independent.
	RETURN_ON_BAD_HR(pVideoMediaType->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE));
	RETURN_ON_BAD_HR(MFSetAttributeSize(pVideoMediaType, MF_MT_FRAME_SIZE, sourceWidth, sourceHeight));
	pVideoMediaType->SetUINT32(MF_MT_VIDEO_ROTATION, rotationFormat);

	if (GetAudioOptions()->IsAudioEnabled()) {
		// Set the input audio type.
		RETURN_ON_BAD_HR(MFCreateMediaType(&pAudioMediaType));
		RETURN_ON_BAD_HR(pAudioMediaType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio));
		RETURN_ON_BAD_HR(pAudioMediaType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM));
		RETURN_ON_BAD_HR(pAudioMediaType->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, GetAudioOptions()->GetAudioBitsPerSample()));
		RETURN_ON_BAD_HR(pAudioMediaType->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, GetAudioOptions()->GetAudioSamplesPerSecond()));
		RETURN_ON_BAD_HR(pAudioMediaType->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, GetAudioOptions()->GetAudioChannels()));

		*pAudioMediaTypeIn = pAudioMediaType;
		(*pAudioMediaTypeIn)->AddRef();
	}

	*pVideoMediaTypeIn = pVideoMediaType;
	(*pVideoMediaTypeIn)->AddRef();
	return S_OK;
}

HRESULT OutputManager::InitializeVideoSinkWriter(
	_In_ std::wstring path,
	_In_opt_ IMFByteStream *pOutStream,
	_In_ ID3D11Device *pDevice,
	_In_ RECT sourceRect,
	_In_ SIZE outputFrameSize,
	_In_ DXGI_MODE_ROTATION rotation,
	_In_ IMFSinkWriterCallback *pCallback,
	_Outptr_ IMFSinkWriter **ppWriter,
	_Out_ DWORD *pVideoStreamIndex,
	_Out_ DWORD *pAudioStreamIndex)
{
	*ppWriter = nullptr;
	*pVideoStreamIndex = 0;
	*pAudioStreamIndex = 0;

	UINT pResetToken;
	CComPtr<IMFDXGIDeviceManager> pDeviceManager = nullptr;
	CComPtr<IMFSinkWriter>        pSinkWriter = nullptr;
	CComPtr<IMFMediaType>         pVideoMediaTypeOut = nullptr;
	CComPtr<IMFMediaType>         pAudioMediaTypeOut = nullptr;
	CComPtr<IMFMediaType>         pVideoMediaTypeIn = nullptr;
	CComPtr<IMFMediaType>         pAudioMediaTypeIn = nullptr;
	CComPtr<IMFAttributes>        pAttributes = nullptr;

	MFVideoRotationFormat rotationFormat = MFVideoRotationFormat_0;
	if (rotation == DXGI_MODE_ROTATION_ROTATE90) {
		rotationFormat = MFVideoRotationFormat_90;
	}
	else if (rotation == DXGI_MODE_ROTATION_ROTATE180) {
		rotationFormat = MFVideoRotationFormat_180;
	}
	else if (rotation == DXGI_MODE_ROTATION_ROTATE270) {
		rotationFormat = MFVideoRotationFormat_270;
	}

	DWORD audioStreamIndex;
	DWORD videoStreamIndex;
	RETURN_ON_BAD_HR(MFCreateDXGIDeviceManager(&pResetToken, &pDeviceManager));
	RETURN_ON_BAD_HR(pDeviceManager->ResetDevice(pDevice, pResetToken));
	const wchar_t *pathString = nullptr;
	if (!path.empty()) {
		pathString = path.c_str();
	}

	if (pOutStream == nullptr)
	{
		RETURN_ON_BAD_HR(MFCreateFile(MF_ACCESSMODE_READWRITE, MF_OPENMODE_FAIL_IF_EXIST, MF_FILEFLAGS_NONE, pathString, &pOutStream));
	};

	UINT sourceWidth = RectWidth(sourceRect);
	UINT sourceHeight = RectHeight(sourceRect);

	UINT destWidth = max(0, outputFrameSize.cx);
	UINT destHeight = max(0, outputFrameSize.cy);

	RETURN_ON_BAD_HR(ConfigureOutputMediaTypes(destWidth, destHeight, &pVideoMediaTypeOut, &pAudioMediaTypeOut));
	RETURN_ON_BAD_HR(ConfigureInputMediaTypes(sourceWidth, sourceHeight, rotationFormat, pVideoMediaTypeOut, &pVideoMediaTypeIn, &pAudioMediaTypeIn));

	//Creates a streaming writer
	CComPtr<IMFMediaSink> pMp4StreamSink = nullptr;
	if (GetEncoderOptions()->GetIsFragmentedMp4Enabled()) {
		RETURN_ON_BAD_HR(MFCreateFMPEG4MediaSink(pOutStream, pVideoMediaTypeOut, pAudioMediaTypeOut, &pMp4StreamSink));
	}
	else {
		RETURN_ON_BAD_HR(MFCreateMPEG4MediaSink(pOutStream, pVideoMediaTypeOut, pAudioMediaTypeOut, &pMp4StreamSink));
	}
	pAudioMediaTypeOut.Release();

	RETURN_ON_BAD_HR(MFCreateAttributes(&pAttributes, 7));
	RETURN_ON_BAD_HR(pAttributes->SetGUID(MF_TRANSCODE_CONTAINERTYPE, GetEncoderOptions()->GetIsFragmentedMp4Enabled() ? MFTranscodeContainerType_FMPEG4 : MFTranscodeContainerType_MPEG4));
	RETURN_ON_BAD_HR(pAttributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, GetEncoderOptions()->GetIsHardwareEncodingEnabled()));
	RETURN_ON_BAD_HR(pAttributes->SetUINT32(MF_MPEG4SINK_MOOV_BEFORE_MDAT, GetEncoderOptions()->GetIsFastStartEnabled()));
	RETURN_ON_BAD_HR(pAttributes->SetUINT32(MF_LOW_LATENCY, GetEncoderOptions()->GetIsLowLatencyModeEnabled()));
	RETURN_ON_BAD_HR(pAttributes->SetUINT32(MF_SINK_WRITER_DISABLE_THROTTLING, GetEncoderOptions()->GetIsThrottlingDisabled()));
	// Add device manager to attributes. This enables hardware encoding.
	RETURN_ON_BAD_HR(pAttributes->SetUnknown(MF_SINK_WRITER_D3D_MANAGER, pDeviceManager));
	RETURN_ON_BAD_HR(pAttributes->SetUnknown(MF_SINK_WRITER_ASYNC_CALLBACK, pCallback));

	RETURN_ON_BAD_HR(MFCreateSinkWriterFromMediaSink(pMp4StreamSink, pAttributes, &pSinkWriter));
	pMp4StreamSink.Release();
	videoStreamIndex = 0;
	audioStreamIndex = 1;
	RETURN_ON_BAD_HR(pSinkWriter->SetInputMediaType(videoStreamIndex, pVideoMediaTypeIn, nullptr));
	if (pAudioMediaTypeIn) {
		RETURN_ON_BAD_HR(pSinkWriter->SetInputMediaType(audioStreamIndex, pAudioMediaTypeIn, nullptr));
	}

	auto SetAttributeU32([](_Inout_ CComPtr<ICodecAPI> &codec, _In_ const GUID &guid, _In_ UINT32 value)
	{
		VARIANT val;
		val.vt = VT_UI4;
		val.uintVal = value;
		return codec->SetValue(&guid, &val);
	});

	CComPtr<ICodecAPI> encoder = nullptr;
	pSinkWriter->GetServiceForStream(videoStreamIndex, GUID_NULL, IID_PPV_ARGS(&encoder));
	if (encoder) {
		RETURN_ON_BAD_HR(SetAttributeU32(encoder, CODECAPI_AVEncCommonRateControlMode, GetEncoderOptions()->GetVideoBitrateMode()));
		switch (GetEncoderOptions()->GetVideoBitrateMode()) {
			case eAVEncCommonRateControlMode_Quality:
				RETURN_ON_BAD_HR(SetAttributeU32(encoder, CODECAPI_AVEncCommonQuality, GetEncoderOptions()->GetVideoQuality()));
				break;
			default:
				break;
		}
	}

	// Tell the sink writer to start accepting data.
	RETURN_ON_BAD_HR(pSinkWriter->BeginWriting());

	// Return the pointer to the caller.
	*ppWriter = pSinkWriter;
	(*ppWriter)->AddRef();
	*pVideoStreamIndex = videoStreamIndex;
	*pAudioStreamIndex = audioStreamIndex;
	return S_OK;
}

HRESULT OutputManager::WriteFrameToVideo(_In_ INT64 frameStartPos, _In_ INT64 frameDuration, _In_ DWORD streamIndex, _In_ ID3D11Texture2D *pAcquiredDesktopImage)
{
	IMFMediaBuffer *pMediaBuffer;
	HRESULT hr = MFCreateDXGISurfaceBuffer(__uuidof(ID3D11Texture2D), pAcquiredDesktopImage, 0, FALSE, &pMediaBuffer);
	IMF2DBuffer *p2DBuffer;
	if (SUCCEEDED(hr))
	{
		hr = pMediaBuffer->QueryInterface(__uuidof(IMF2DBuffer), reinterpret_cast<void **>(&p2DBuffer));
	}
	DWORD length;
	if (SUCCEEDED(hr))
	{
		hr = p2DBuffer->GetContiguousLength(&length);
	}
	if (SUCCEEDED(hr))
	{
		hr = pMediaBuffer->SetCurrentLength(length);
	}
	IMFSample *pSample;
	if (SUCCEEDED(hr))
	{
		hr = MFCreateSample(&pSample);
	}
	if (SUCCEEDED(hr))
	{
		hr = pSample->AddBuffer(pMediaBuffer);
	}
	if (SUCCEEDED(hr))
	{
		hr = pSample->SetSampleTime(frameStartPos);
	}
	if (SUCCEEDED(hr))
	{
		hr = pSample->SetSampleDuration(frameDuration);
	}
	if (SUCCEEDED(hr))
	{
		// Send the sample to the Sink Writer.
		hr = m_SinkWriter->WriteSample(streamIndex, pSample);
	}
	SafeRelease(&pSample);
	SafeRelease(&p2DBuffer);
	SafeRelease(&pMediaBuffer);
	return hr;
}
HRESULT OutputManager::WriteAudioSamplesToVideo(_In_ INT64 frameStartPos, _In_ INT64 frameDuration, _In_ DWORD streamIndex, _In_ BYTE *pSrc, _In_ DWORD cbData)
{
	IMFMediaBuffer *pBuffer = nullptr;
	BYTE *pData = nullptr;
	// Create the media buffer.
	HRESULT hr = MFCreateMemoryBuffer(
		cbData,   // Amount of memory to allocate, in bytes.
		&pBuffer
	);
	//once in awhile, things get behind and we get an out of memory error when trying to create the buffer
	//so, just check, wait and try again if necessary
	int counter = 0;
	while (!SUCCEEDED(hr) && counter++ < 100) {
		Sleep(10);
		hr = MFCreateMemoryBuffer(cbData, &pBuffer);
	}
	// Lock the buffer to get a pointer to the memory.
	if (SUCCEEDED(hr))
	{
		hr = pBuffer->Lock(&pData, nullptr, nullptr);
	}

	if (SUCCEEDED(hr))
	{
		memcpy_s(pData, cbData, pSrc, cbData);
	}

	// Update the current length.
	if (SUCCEEDED(hr))
	{
		hr = pBuffer->SetCurrentLength(cbData);
	}

	// Unlock the buffer.
	if (pData)
	{
		hr = pBuffer->Unlock();
	}

	IMFSample *pSample;
	if (SUCCEEDED(hr))
	{
		hr = MFCreateSample(&pSample);
	}
	if (SUCCEEDED(hr))
	{
		hr = pSample->AddBuffer(pBuffer);
	}
	if (SUCCEEDED(hr))
	{
		INT64 start = frameStartPos;
		hr = pSample->SetSampleTime(start);
	}
	if (SUCCEEDED(hr))
	{
		INT64 duration = frameDuration;
		hr = pSample->SetSampleDuration(duration);
	}
	if (SUCCEEDED(hr))
	{
		// Send the sample to the Sink Writer.
		hr = m_SinkWriter->WriteSample(streamIndex, pSample);
	}
	SafeRelease(&pSample);
	SafeRelease(&pBuffer);
	return hr;
}