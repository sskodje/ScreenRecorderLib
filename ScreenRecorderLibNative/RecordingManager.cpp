#include <ppltasks.h> 
#include <ctime>   // localtime
#include <sstream> // stringstream
#include <iomanip> // put_time
#include <string>
#include <comdef.h>
#include <Mferror.h>
#include <wrl.h>
#include <concrt.h>
#include <mfidl.h>
#include <VersionHelpers.h>
#include <Wmcodecdsp.h>
#include <filesystem>
#include "LoopbackCapture.h"
#include "RecordingManager.h"
#include "AudioPrefs.h"
#include "Log.h"
#include "Cleanup.h"
#include "Screengrab.h"

#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "D3D11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "Mfuuid.lib")
#pragma comment(lib, "Mfplat.lib")
#pragma comment(lib, "evr.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "Mf.lib")
#pragma comment(lib, "wmcodecdspuuid.lib")
#pragma comment(lib, "windowsapp.lib")
#pragma comment(lib, "dwmapi.lib")

using namespace std;
using namespace std::chrono;
using namespace concurrency;
using namespace DirectX;
using namespace winrt::Windows::Graphics::DirectX;
using namespace winrt::Windows::Graphics::Capture;
#if _DEBUG
bool isLoggingEnabled = true;
int logSeverityLevel = LOG_LVL_DEBUG;
#else
bool isLoggingEnabled = false;
int logSeverityLevel = LOG_LVL_INFO;
#endif

std::wstring logFilePath;

INT64 g_LastMouseClickDurationRemaining;
INT g_MouseClickDetectionDurationMillis = 50;
UINT g_LastMouseClickButton;
// Driver types supported
D3D_DRIVER_TYPE gDriverTypes[] =
{
	D3D_DRIVER_TYPE_HARDWARE,
	D3D_DRIVER_TYPE_WARP,
	D3D_DRIVER_TYPE_REFERENCE,
};

// Feature levels supported
D3D_FEATURE_LEVEL m_FeatureLevels[] =
{
	D3D_FEATURE_LEVEL_11_0,
	D3D_FEATURE_LEVEL_10_1,
	D3D_FEATURE_LEVEL_10_0,
	D3D_FEATURE_LEVEL_9_1
};

struct RecordingManager::TaskWrapper {
	Concurrency::task<void> m_RecordTask = concurrency::task_from_result();
	Concurrency::cancellation_token_source m_RecordTaskCts;
};


RecordingManager::RecordingManager() :
	m_TaskWrapperImpl(make_unique<TaskWrapper>()),
	RecordingCompleteCallback(nullptr),
	RecordingFailedCallback(nullptr),
	RecordingSnapshotCreatedCallback(nullptr),
	RecordingStatusChangedCallback(nullptr),
	m_Mousehook(nullptr),
	m_EncoderOptions(new H264_ENCODER_OPTIONS())
{
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
}

RecordingManager::~RecordingManager()
{
	UnhookWindowsHookEx(m_Mousehook);
	if (!m_TaskWrapperImpl->m_RecordTask.is_done()) {
		LOG_WARN("Recording is in progress while destructing, cancelling recording task and waiting for completion.");
		m_TaskWrapperImpl->m_RecordTaskCts.cancel();
		m_TaskWrapperImpl->m_RecordTask.wait();
		LOG_DEBUG("Wait for recording task completed.");
	}
}

LRESULT CALLBACK MouseHookProc(int nCode, WPARAM wParam, LPARAM lParam)
{
	// MB1 click
	if (wParam == WM_LBUTTONDOWN)
	{
		g_LastMouseClickButton = VK_LBUTTON;
		g_LastMouseClickDurationRemaining = g_MouseClickDetectionDurationMillis;
	}
	else if (wParam == WM_RBUTTONDOWN)
	{
		g_LastMouseClickButton = VK_RBUTTON;
		g_LastMouseClickDurationRemaining = g_MouseClickDetectionDurationMillis;
	}
	return CallNextHookEx(0, nCode, wParam, lParam);
}
void RecordingManager::SetMouseClickDetectionDuration(int value) {
	g_MouseClickDetectionDurationMillis = value;
}
void RecordingManager::SetIsLogEnabled(bool value) {
	isLoggingEnabled = value;
}
void RecordingManager::SetLogFilePath(std::wstring value) {
	logFilePath = value;
}
void RecordingManager::SetLogSeverityLevel(int value) {
	logSeverityLevel = value;
}

std::vector<BYTE> RecordingManager::MixAudio(_In_ std::vector<BYTE> const &first, _In_ std::vector<BYTE> const &second, _In_ float firstVolume, _In_ float secondVolume)
{
	std::vector<BYTE> newvector(max(first.size(), second.size()));
	bool clipped = false;
	for (size_t i = 0; i < newvector.size(); i += 2) {
		short firstSample = first.size() > i + 1 ? static_cast<short>(first[i] | first[i + 1] << 8) : 0;
		short secondSample = second.size() > i + 1 ? static_cast<short>(second[i] | second[i + 1] << 8) : 0;
		auto out = reinterpret_cast<short *>(&newvector[i]);
		int mixedSample = int(round((firstSample)*firstVolume + (secondSample)*secondVolume));
		if (mixedSample > MAXSHORT) {
			clipped = true;
			mixedSample = MAXSHORT;
		}
		else if (mixedSample < -MAXSHORT) {
			clipped = true;
			mixedSample = -MAXSHORT;
		}
		*out = (short)mixedSample;
	}
	if (clipped) {
		LOG_WARN("Audio clipped during mixing");
	}
	return newvector;
}

std::wstring RecordingManager::GetImageExtension() {
	if (m_ImageEncoderFormat == GUID_ContainerFormatPng) {
		return L".png";
	}
	else if (m_ImageEncoderFormat == GUID_ContainerFormatJpeg) {
		return L".jpg";
	}
	else if (m_ImageEncoderFormat == GUID_ContainerFormatBmp) {
		return L".bmp";
	}
	else if (m_ImageEncoderFormat == GUID_ContainerFormatTiff) {
		return L".tiff";
	}
	else {
		LOG_WARN("Image encoder format not recognized, defaulting to .jpg extension");
		return L".jpg";
	}
}

std::wstring RecordingManager::GetVideoExtension() {
	return L".mp4";
}

HRESULT RecordingManager::ConfigureOutputDir(_In_ std::wstring path) {
	m_OutputFullPath = path;
	if (!path.empty()) {
		wstring dir = path;
		if (m_RecorderMode == MODE_SLIDESHOW) {
			if (!dir.empty() && dir.back() != '\\')
				dir += '\\';
		}
		LPWSTR directory = (LPWSTR)dir.c_str();
		PathRemoveFileSpecW(directory);
		std::error_code ec;
		if (std::filesystem::exists(directory) || std::filesystem::create_directories(directory, ec))
		{
			LOG_DEBUG(L"Video output folder is ready");
			m_OutputFolder = directory;
		}
		else
		{
			// Failed to create directory.
			LOG_ERROR(L"failed to create output folder");
			if (RecordingFailedCallback != nullptr)
				RecordingFailedCallback(L"Failed to create output folder: " + s2ws(ec.message()));
			return E_FAIL;
		}

		if (m_RecorderMode == MODE_VIDEO || m_RecorderMode == MODE_SNAPSHOT) {
			wstring ext = m_RecorderMode == MODE_VIDEO ? GetVideoExtension() : GetImageExtension();
			LPWSTR pStrExtension = PathFindExtension(path.c_str());
			if (pStrExtension == nullptr || pStrExtension[0] == 0)
			{
				m_OutputFullPath = m_OutputFolder + L"\\" + s2ws(CurrentTimeToFormattedString()) + ext;
			}
			if (IsSnapshotsWithVideoEnabled()) {
				if (m_OutputSnapshotsFolderPath.empty()) {
					// Snapshots will be saved in a folder named as video file name without extension. 
					m_OutputSnapshotsFolderPath = m_OutputFullPath.substr(0, m_OutputFullPath.find_last_of(L"."));
				}
			}
		}
	}
	if (!m_OutputSnapshotsFolderPath.empty()) {
		std::error_code ec;
		if (std::filesystem::exists(m_OutputSnapshotsFolderPath) || std::filesystem::create_directories(m_OutputSnapshotsFolderPath, ec))
		{
			LOG_DEBUG(L"Snapshot output folder is ready");
		}
		else
		{
			// Failed to create snapshot directory.
			LOG_ERROR(L"failed to create snapshot output folder");
			if (RecordingFailedCallback != nullptr)
				RecordingFailedCallback(L"Failed to create snapshot output folder: " + s2ws(ec.message()));
			return E_FAIL;
		}
	}
	return S_OK;
}

HRESULT RecordingManager::BeginRecording(_In_opt_ IStream *stream) {
	return BeginRecording(L"", stream);
}

HRESULT RecordingManager::BeginRecording(_In_opt_ std::wstring path) {
	return BeginRecording(path, nullptr);
}

HRESULT RecordingManager::BeginRecording(_In_opt_ std::wstring path, _In_opt_ IStream *stream) {
	if (m_IsRecording) {
		if (m_IsPaused) {
			m_IsPaused = false;
			if (RecordingStatusChangedCallback != nullptr) {
				RecordingStatusChangedCallback(STATUS_RECORDING);
				LOG_DEBUG("Changed Recording Status to Recording");
			}
		}
		std::wstring error = L"Recording is already in progress, aborting";
		LOG_WARN("%ls", error.c_str());
		if (RecordingFailedCallback != nullptr)
			RecordingFailedCallback(error);
		return S_FALSE;
	}
	wstring errorText;
	if (!CheckDependencies(&errorText)) {
		LOG_ERROR(L"%ls", errorText);
		if (RecordingFailedCallback != nullptr)
			RecordingFailedCallback(errorText);
		return S_FALSE;
	}
	g_LastMouseClickDurationRemaining = 0;
	m_EncoderResult = S_FALSE;
	m_LastFrameHadAudio = false;
	m_FrameDelays.clear();

	RETURN_ON_BAD_HR(ConfigureOutputDir(path));

	if (m_RecordingSources.size() == 0) {
		std::wstring error = L"No valid recording sources found in recorder parameters.";
		LOG_ERROR("%ls", error.c_str());
		if (RecordingFailedCallback != nullptr)
			RecordingFailedCallback(error);
		return S_FALSE;
	}

	m_TaskWrapperImpl->m_RecordTaskCts = cancellation_token_source();
	m_TaskWrapperImpl->m_RecordTask = concurrency::create_task([this, stream]() {
		LOG_INFO(L"Starting recording task");
		m_IsRecording = true;
		HRESULT hr = CoInitializeEx(nullptr, COINITBASE_MULTITHREADED | COINIT_DISABLE_OLE1DDE);
		RETURN_ON_BAD_HR(hr);
		RETURN_ON_BAD_HR(hr = MFStartup(MF_VERSION, MFSTARTUP_LITE));
		InitializeMouseClickDetection();
		ID3D11Debug *pDebug = nullptr;
		RETURN_ON_BAD_HR(hr = InitializeDx(&m_ImmediateContext, &m_Device, &pDebug));
#if _DEBUG
		m_Debug = pDebug;
#endif
		if (RecordingStatusChangedCallback != nullptr) {
			RecordingStatusChangedCallback(STATUS_RECORDING);
			LOG_DEBUG("Changed Recording Status to Recording");
		}

		if (m_RecorderApi == API_GRAPHICS_CAPTURE) {
			LOG_DEBUG("Starting Windows Graphics Capture recorder loop");
		}
		else if (m_RecorderApi == API_DESKTOP_DUPLICATION) {
			LOG_DEBUG("Starting Desktop Duplication recorder loop");
		}

		hr = StartRecorderLoop(m_RecordingSources, m_Overlays, stream);
		LOG_INFO("Exiting recording task");
		return hr;
		}).then([this](HRESULT recordingResult) {
			HRESULT finalizeResult = FinalizeRecording();
			if (SUCCEEDED(recordingResult) && FAILED(finalizeResult))
				return finalizeResult;
			else
				return recordingResult;
			}).then([this](concurrency::task<HRESULT> t)
				{
					m_IsRecording = false;
					CleanupResourcesAndShutDownMF();
					HRESULT hr = E_FAIL;
					try {
						hr = t.get();
						// if .get() didn't throw and the HRESULT succeeded, there are no errors.
					}
					catch (const exception &e) {
						// handle error
						LOG_ERROR(L"Exception in RecordTask: %s", e.what());
					}
					catch (...) {
						LOG_ERROR(L"Exception in RecordTask");
					}
					SetRecordingCompleteStatus(hr);
				});
			return S_OK;
}

void RecordingManager::EndRecording() {
	if (m_IsRecording) {
		m_IsPaused = false;
		m_TaskWrapperImpl->m_RecordTaskCts.cancel();
	}
}
void RecordingManager::PauseRecording() {
	if (m_IsRecording) {
		m_IsPaused = true;
		if (RecordingStatusChangedCallback != nullptr) {
			RecordingStatusChangedCallback(STATUS_PAUSED);
			LOG_DEBUG("Changed Recording Status to Paused");
		}
	}
}
void RecordingManager::ResumeRecording() {
	if (m_IsRecording) {
		if (m_IsPaused) {
			m_IsPaused = false;
			if (RecordingStatusChangedCallback != nullptr) {
				RecordingStatusChangedCallback(STATUS_RECORDING);
				LOG_DEBUG("Changed Recording Status to Recording");
			}
		}
	}
}

bool RecordingManager::SetExcludeFromCapture(HWND hwnd, bool isExcluded) {
	// The API call causes ugly black window on older builds of Windows, so skip if the contract is down-level. 
	if (winrt::Windows::Foundation::Metadata::ApiInformation::IsApiContractPresent(L"Windows.Foundation.UniversalApiContract", 9))
		return (bool)SetWindowDisplayAffinity(hwnd, isExcluded ? WDA_EXCLUDEFROMCAPTURE : WDA_NONE);
	else
		return false;
}

ScreenCaptureBase *RecordingManager::CreateCaptureSession()
{
	if (m_RecorderApi == API_DESKTOP_DUPLICATION) {
		return	new duplication_capture();
	}
	else if (m_RecorderApi == API_GRAPHICS_CAPTURE) {
		return new WindowsGraphicsCapture();
	}
	return nullptr;
}

HRESULT RecordingManager::FinalizeRecording()
{
	LOG_INFO("Cleaning up resources");
	LOG_INFO("Finalizing recording");
	HRESULT finalizeResult = S_OK;
	if (m_SinkWriter) {
		if (RecordingStatusChangedCallback != nullptr) {
			try {
				RecordingStatusChangedCallback(STATUS_FINALIZING);
			}
			catch (...) {
				LOG_ERROR(L"Exception when calling callback");
			}
		}
		finalizeResult = m_SinkWriter->Finalize();
		if (SUCCEEDED(finalizeResult) && m_FinalizeEvent) {
			WaitForSingleObject(m_FinalizeEvent, INFINITE);
			CloseHandle(m_FinalizeEvent);
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

void RecordingManager::CleanupResourcesAndShutDownMF()
{
	SafeRelease(&m_SinkWriter);
	LOG_DEBUG("Released IMFSinkWriter");
	SafeRelease(&m_ImmediateContext);
	LOG_DEBUG("Released ID3D11DeviceContext");
	SafeRelease(&m_Device);
	LOG_DEBUG("Released ID3D11Device");
#if _DEBUG
	if (m_Debug) {
		m_Debug->ReportLiveDeviceObjects(D3D11_RLDO_DETAIL);
		SafeRelease(&m_Debug);
		LOG_DEBUG("Released ID3D11Debug");
	}
#endif
	MFShutdown();
	CoUninitialize();
	UnhookWindowsHookEx(m_Mousehook);
	LOG_INFO(L"Media Foundation shut down");
}

void RecordingManager::SetRecordingCompleteStatus(_In_ HRESULT hr)
{
	std::wstring errMsg = L"";
	bool isSuccess = SUCCEEDED(hr);
	if (!isSuccess) {
		_com_error err(hr);
		errMsg = err.ErrorMessage();
	}

	if (RecordingStatusChangedCallback) {
		RecordingStatusChangedCallback(STATUS_IDLE);
		LOG_DEBUG("Changed Recording Status to Idle");
	}
	if (isSuccess) {
		if (RecordingCompleteCallback)
			RecordingCompleteCallback(m_OutputFullPath, m_FrameDelays);
		LOG_DEBUG("Sent Recording Complete callback");
	}
	else {
		if (RecordingFailedCallback) {
			if (FAILED(m_EncoderResult)) {
				_com_error encoderFailure(m_EncoderResult);
				errMsg = string_format(L"Write error (0x%lx) in video encoder: %s", m_EncoderResult, encoderFailure.ErrorMessage());
				if (m_EncoderOptions->GetIsHardwareEncodingEnabled()) {
					errMsg += L" If the problem persists, disabling hardware encoding may improve stability.";
				}
			}
			else {
				if (errMsg.empty()) {
					errMsg = GetLastErrorStdWstr();
				}
			}
			RecordingFailedCallback(errMsg);
			LOG_DEBUG("Sent Recording Failed callback");
		}
	}
}

HRESULT RecordingManager::StartRecorderLoop(_In_ std::vector<RECORDING_SOURCE> sources, _In_ std::vector<RECORDING_OVERLAY> overlays, _In_opt_ IStream *pStream)
{
	std::unique_ptr<LoopbackCapture> pLoopbackCaptureOutputDevice = nullptr;
	std::unique_ptr<LoopbackCapture> pLoopbackCaptureInputDevice = nullptr;
	CComPtr<ID3D11Texture2D> pPreviousFrameCopy = nullptr;
	CComPtr<ID3D11Texture2D> pCurrentFrameCopy = nullptr;
	CComPtr<IMFSinkWriterCallback> pCallBack = nullptr;
	PTR_INFO *pPtrInfo = nullptr;
	unique_ptr<ScreenCaptureBase> pCapture(CreateCaptureSession());
	RETURN_ON_BAD_HR(pCapture->Initialize(m_ImmediateContext, m_Device));
	HANDLE UnexpectedErrorEvent = nullptr;
	HANDLE ExpectedErrorEvent = nullptr;

	// Event used by the threads to signal an unexpected error and we want to quit the app
	UnexpectedErrorEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
	if (nullptr == UnexpectedErrorEvent) {
		LOG_ERROR(L"CreateEvent failed: last error is %u", GetLastError());
		return E_FAIL;
	}
	CloseHandleOnExit closeUnexpectedErrorEvent(UnexpectedErrorEvent);
	// Event for when a thread encounters an expected error
	ExpectedErrorEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
	if (nullptr == ExpectedErrorEvent) {
		LOG_ERROR(L"CreateEvent failed: last error is %u", GetLastError());
		return E_FAIL;
	}
	CloseHandleOnExit closeExpectedErrorEvent(ExpectedErrorEvent);

	HRESULT hr;
	RETURN_ON_BAD_HR(hr = pCapture->StartCapture(sources, overlays, UnexpectedErrorEvent, ExpectedErrorEvent));
	CaptureStopOnExit stopCaptureOnExit(pCapture.get());

	RECT videoInputFrameRect{};
	RECT videoOutputFrameRect{};
	RECT previousInputFrameRect{};
	RETURN_ON_BAD_HR(hr = InitializeRects(pCapture->GetOutputRect(), &videoInputFrameRect, &videoOutputFrameRect));
	bool isDestRectEqualToSourceRect;

	if (pCapture->IsSingleWindowCapture()) {
		//Differing input and output dimensions of the mediatype initializes the video processor with the sink writer so we can use it for resizing the input.
		//These values will be overwritten on a frame by frame basis.
		videoInputFrameRect.right -= 2;
		videoInputFrameRect.bottom -= 2;
	}

	HANDLE hMarkEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	CloseHandleOnExit closeMarkEvent(hMarkEvent);
	m_FinalizeEvent = CreateEvent(NULL, FALSE, FALSE, NULL);

	std::unique_ptr<MousePointer> pMousePointer = make_unique<MousePointer>();
	RETURN_ON_BAD_HR(hr = pMousePointer->Initialize(m_ImmediateContext, m_Device));
	SetViewPort(m_ImmediateContext, RectWidth(videoInputFrameRect), RectHeight(videoInputFrameRect));

	if (m_RecorderMode == MODE_VIDEO) {
		LoopbackCapture *outputCapture = nullptr;
		LoopbackCapture *inputCapture = nullptr;
		hr = InitializeAudioCapture(&outputCapture, &inputCapture);
		if (SUCCEEDED(hr)) {
			pLoopbackCaptureOutputDevice = std::unique_ptr<LoopbackCapture>(outputCapture);
			pLoopbackCaptureInputDevice = std::unique_ptr<LoopbackCapture>(inputCapture);
		}
		else {
			LOG_ERROR(L"Audio capture failed to start: hr = 0x%08x", hr);
		}
		CComPtr<IMFByteStream> outputStream = nullptr;
		if (pStream != nullptr) {
			RETURN_ON_BAD_HR(hr = MFCreateMFByteStreamOnStream(pStream, &outputStream));
		}
		pCallBack = new (std::nothrow)CMFSinkWriterCallback(m_FinalizeEvent, hMarkEvent);
		RETURN_ON_BAD_HR(hr = InitializeVideoSinkWriter(m_OutputFullPath, outputStream, m_Device, videoInputFrameRect, videoOutputFrameRect, DXGI_MODE_ROTATION_UNSPECIFIED, pCallBack, &m_SinkWriter, &m_VideoStreamIndex, &m_AudioStreamIndex));
	}
	if (pLoopbackCaptureInputDevice)
		pLoopbackCaptureInputDevice->ClearRecordedBytes();
	if (pLoopbackCaptureOutputDevice)
		pLoopbackCaptureOutputDevice->ClearRecordedBytes();

	std::chrono::steady_clock::time_point lastFrame = std::chrono::steady_clock::now();
	m_previousSnapshotTaken = (std::chrono::steady_clock::time_point::min)();
	INT64 videoFrameDurationMillis = 1000 / m_EncoderOptions->GetVideoFps();
	INT64 videoFrameDuration100Nanos = MillisToHundredNanos(videoFrameDurationMillis);

	INT frameNr = 0;
	INT64 lastFrameStartPos = 0;
	bool haveCachedPrematureFrame = false;
	cancellation_token token = m_TaskWrapperImpl->m_RecordTaskCts.get_token();

	while (true)
	{
		if (pCurrentFrameCopy) {
			pCurrentFrameCopy.Release();
		}
		if (token.is_canceled()) {
			LOG_DEBUG("Recording task was cancelled");
			hr = S_OK;
			break;
		}

		if (WaitForSingleObjectEx(UnexpectedErrorEvent, 0, FALSE) == WAIT_OBJECT_0) {
			return E_FAIL;
		}

		if (WaitForSingleObjectEx(ExpectedErrorEvent, 0, FALSE) == WAIT_OBJECT_0) {
			wait(10);
			pCapture.reset(CreateCaptureSession());
			stopCaptureOnExit.Reset(pCapture.get());
			ResetEvent(UnexpectedErrorEvent);
			ResetEvent(ExpectedErrorEvent);
			RETURN_ON_BAD_HR(hr = pCapture->StartCapture(sources, overlays, UnexpectedErrorEvent, ExpectedErrorEvent));
			continue;
		}
		if (m_IsPaused) {
			wait(10);
			m_previousSnapshotTaken = steady_clock::now();
			lastFrame = steady_clock::now();
			if (pLoopbackCaptureOutputDevice)
				pLoopbackCaptureOutputDevice->ClearRecordedBytes();
			if (pLoopbackCaptureInputDevice)
				pLoopbackCaptureInputDevice->ClearRecordedBytes();
			continue;
		}
		CAPTURED_FRAME capturedFrame{};
		// Get new frame
		hr = pCapture->AcquireNextFrame(
			haveCachedPrematureFrame || m_EncoderOptions->GetIsFixedFramerate() ? 0 : 100,
			&capturedFrame);

		if (SUCCEEDED(hr)) {
			pCurrentFrameCopy.Attach(capturedFrame.Frame);
			if (capturedFrame.PtrInfo) {
				pPtrInfo = capturedFrame.PtrInfo;
			}


			videoInputFrameRect.left = videoOutputFrameRect.left;
			videoInputFrameRect.top = videoOutputFrameRect.top;
			videoInputFrameRect.right = min(capturedFrame.ContentSize.cx, videoOutputFrameRect.right);
			videoInputFrameRect.bottom = min(capturedFrame.ContentSize.cy, videoOutputFrameRect.bottom);
			videoInputFrameRect = MakeRectEven(videoInputFrameRect);

			if (!EqualRect(&videoInputFrameRect, &previousInputFrameRect)) {
				isDestRectEqualToSourceRect = false;
				if (m_RecorderMode == MODE_VIDEO) {
					//A marker is placed in the stream and then we wait for the sink writer to trigger it. This ensures all pending frames are encoded before the input is resized to the new size.
					//The timeout is one frame @ 30fps, because it is preferable to avoid having the framerate drop too much if the encoder is busy.
					RETURN_ON_BAD_HR(m_SinkWriter->PlaceMarker(m_VideoStreamIndex, nullptr));
					if (WaitForSingleObject(hMarkEvent, 33) != WAIT_OBJECT_0) {
						LOG_WARN("Wait for encoder marker failed");
					}

					CComPtr<IMFVideoProcessorControl> videoProcessor = nullptr;
					GetVideoProcessor(m_SinkWriter, m_VideoStreamIndex, &videoProcessor);
					if (videoProcessor) {
						//The source rectangle is the portion of the input frame that is blitted to the destination surface.
						videoProcessor->SetSourceRectangle(&videoInputFrameRect);
						LOG_TRACE("Changing video processor surface rect: source=%dx%d, dest = %dx%d", RectWidth(videoInputFrameRect), RectHeight(videoInputFrameRect), RectWidth(videoOutputFrameRect), RectHeight(videoOutputFrameRect));
					}
					SetViewPort(m_ImmediateContext, RectWidth(videoInputFrameRect), RectHeight(videoInputFrameRect));
					previousInputFrameRect = videoInputFrameRect;
				}
			}
		}
		else if (m_RecorderMode == MODE_SLIDESHOW
			|| m_RecorderMode == MODE_SNAPSHOT
			&& (frameNr == 0 && (pCurrentFrameCopy == nullptr || capturedFrame.FrameUpdateCount == 0))) {
			continue;
		}
		else if ((!pCurrentFrameCopy && !pPreviousFrameCopy)
			|| !pCapture->IsInitialFrameWriteComplete()) {
			//There is no first frame yet, so retry.
			wait(1);
			continue;
		}

		INT64 durationSinceLastFrame100Nanos = max(duration_cast<nanoseconds>(chrono::steady_clock::now() - lastFrame).count() / 100, 0);
		INT64 durationSinceLastFrameMillis = HundredNanosToMillis(durationSinceLastFrame100Nanos);
		//Delay frames that comes quicker than selected framerate to see if we can skip them.
		if (hr == DXGI_ERROR_WAIT_TIMEOUT || durationSinceLastFrame100Nanos < videoFrameDuration100Nanos) //attempt to wait if frame timeouted or duration is under our chosen framerate
		{
			bool delayRender = false;
			long delay100Nanos = 0;
			if (frameNr == 0 //never delayMs the first frame 
				|| (m_IsMousePointerEnabled && capturedFrame.PtrInfo && capturedFrame.PtrInfo->IsPointerShapeUpdated)//and never delayMs when pointer changes if we draw pointer
				|| (IsSnapshotsWithVideoEnabled() && IsTimeToTakeSnapshot())) // Or if we need to write a snapshot 
			{
				if (capturedFrame.PtrInfo) {
					capturedFrame.PtrInfo->IsPointerShapeUpdated = false;
				}
				delayRender = false;
			}
			else if (SUCCEEDED(hr) && videoFrameDuration100Nanos > durationSinceLastFrame100Nanos) {
				if (capturedFrame.FrameUpdateCount > 0 || capturedFrame.OverlayUpdateCount > 0) {
					if (pCurrentFrameCopy != nullptr) {
						//we got a frame, but it's too soon, so we cache it and see if there are more changes.
						if (pPreviousFrameCopy == nullptr) {
							D3D11_TEXTURE2D_DESC desc;
							pCurrentFrameCopy->GetDesc(&desc);
							RETURN_ON_BAD_HR(hr = m_Device->CreateTexture2D(&desc, nullptr, &pPreviousFrameCopy));
						}
						m_ImmediateContext->CopyResource(pPreviousFrameCopy, pCurrentFrameCopy);
					}
				}
				delayRender = true;
				haveCachedPrematureFrame = true;
				delay100Nanos = videoFrameDuration100Nanos - durationSinceLastFrame100Nanos;
			}
			else if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
				if (m_EncoderOptions->GetIsFixedFramerate()) {
					if (videoFrameDuration100Nanos > durationSinceLastFrame100Nanos) {
						delayRender = true;
						delay100Nanos = max(0, videoFrameDuration100Nanos - durationSinceLastFrame100Nanos);
					}
				}
				else {
					if (haveCachedPrematureFrame && videoFrameDuration100Nanos > durationSinceLastFrame100Nanos) {
						delayRender = true;
						delay100Nanos = videoFrameDuration100Nanos - durationSinceLastFrame100Nanos;
					}
					else if (!haveCachedPrematureFrame && m_MaxFrameLength100Nanos > durationSinceLastFrame100Nanos) {
						delayRender = true;
						delay100Nanos = m_MaxFrameLength100Nanos - durationSinceLastFrame100Nanos;
					}
				}
			}
			if (delayRender) {
				if (delay100Nanos > MillisToHundredNanos(2)) {
					MeasureExecutionTime measureSleep(L"DelayRender");
					Sleep(1);
				}
				else {
					std::this_thread::yield();
				}
				continue;
			}
		}

		if (hr != DXGI_ERROR_WAIT_TIMEOUT) {
			RETURN_ON_BAD_HR(hr);
		}

		lastFrame = steady_clock::now();
		{
			if (pCurrentFrameCopy) {
				if (pPreviousFrameCopy) {
					pPreviousFrameCopy.Release();
				}
				//Copy new frame to pPreviousFrameCopy
				if (m_RecorderMode == MODE_VIDEO || m_RecorderMode == MODE_SLIDESHOW) {
					D3D11_TEXTURE2D_DESC desc;
					pCurrentFrameCopy->GetDesc(&desc);
					RETURN_ON_BAD_HR(hr = m_Device->CreateTexture2D(&desc, nullptr, &pPreviousFrameCopy));
					m_ImmediateContext->CopyResource(pPreviousFrameCopy, pCurrentFrameCopy);
				}
			}
			else if (pPreviousFrameCopy) {
				D3D11_TEXTURE2D_DESC desc;
				pPreviousFrameCopy->GetDesc(&desc);
				RETURN_ON_BAD_HR(hr = m_Device->CreateTexture2D(&desc, nullptr, &pCurrentFrameCopy));
				m_ImmediateContext->CopyResource(pCurrentFrameCopy, pPreviousFrameCopy);
			}

			if (pPtrInfo) {
				hr = DrawMousePointer(pCurrentFrameCopy, pMousePointer.get(), pPtrInfo, durationSinceLastFrame100Nanos);
				if (FAILED(hr)) {
					_com_error err(hr);
					LOG_ERROR(L"Error drawing mouse pointer: %s", err.ErrorMessage());
					//We just log the error and continue if the mouse pointer failed to draw. If there is an error with DXGI, it will be handled on the next call to AcquireNextFrame.
				}
			}
			if (token.is_canceled()) {
				LOG_DEBUG("Recording task was cancelled");
				hr = S_OK;
				break;
			}

			if ((m_RecorderMode == MODE_SLIDESHOW || m_RecorderMode == MODE_SNAPSHOT) && !isDestRectEqualToSourceRect) {
				ID3D11Texture2D *pCroppedFrameCopy;
				RETURN_ON_BAD_HR(hr = CropFrame(pCurrentFrameCopy, videoOutputFrameRect, &pCroppedFrameCopy));
				pCurrentFrameCopy.Release();
				pCurrentFrameCopy.Attach(pCroppedFrameCopy);
			}

			if (IsSnapshotsWithVideoEnabled() && IsTimeToTakeSnapshot()) {
				TakeSnapshotsWithVideo(pCurrentFrameCopy, videoOutputFrameRect);
			}

			FrameWriteModel model{};
			model.Frame = pCurrentFrameCopy;
			model.Duration = durationSinceLastFrame100Nanos;
			model.StartPos = lastFrameStartPos;
			model.Audio = GrabAudioFrame(pLoopbackCaptureOutputDevice, pLoopbackCaptureInputDevice);
			model.FrameNumber = frameNr;
			RETURN_ON_BAD_HR(hr = m_EncoderResult = RenderFrame(model));
			haveCachedPrematureFrame = false;
			if (m_RecorderMode == MODE_SNAPSHOT) {
				break;
			}
			frameNr++;
			lastFrameStartPos += durationSinceLastFrame100Nanos;
		}
	}

	//Push any last frame waiting to be recorded to the sink writer.
	if (pPreviousFrameCopy != nullptr) {
		INT64 duration = duration_cast<nanoseconds>(chrono::steady_clock::now() - lastFrame).count() / 100;
		if (pPtrInfo) {
			DrawMousePointer(pPreviousFrameCopy, pMousePointer.get(), pPtrInfo, duration);
		}
		if ((m_RecorderMode == MODE_SLIDESHOW || m_RecorderMode == MODE_SNAPSHOT) && !isDestRectEqualToSourceRect) {
			ID3D11Texture2D *pCroppedFrameCopy;
			RETURN_ON_BAD_HR(hr = CropFrame(pPreviousFrameCopy, videoOutputFrameRect, &pCroppedFrameCopy));
			pPreviousFrameCopy.Release();
			pPreviousFrameCopy.Attach(pCroppedFrameCopy);
		}
		FrameWriteModel model{};
		model.Frame = pPreviousFrameCopy;
		model.Duration = duration;
		model.StartPos = lastFrameStartPos;
		model.Audio = GrabAudioFrame(pLoopbackCaptureOutputDevice, pLoopbackCaptureInputDevice);
		model.FrameNumber = frameNr;
		hr = m_EncoderResult = RenderFrame(model);
	}
	return hr;
}

std::vector<BYTE> RecordingManager::GrabAudioFrame(_In_opt_ std::unique_ptr<LoopbackCapture> &pLoopbackCaptureOutputDevice, _In_opt_
	std::unique_ptr<LoopbackCapture> &pLoopbackCaptureInputDevice)
{
	if (m_IsOutputDeviceEnabled && m_IsInputDeviceEnabled && pLoopbackCaptureOutputDevice && pLoopbackCaptureInputDevice) {

		auto returnAudioOverflowToBuffer = [&](auto &outputDeviceData, auto &inputDeviceData) {
			if (outputDeviceData.size() > 0 && inputDeviceData.size() > 0) {
				if (outputDeviceData.size() > inputDeviceData.size()) {
					auto diff = outputDeviceData.size() - inputDeviceData.size();
					std::vector<BYTE> overflow(outputDeviceData.end() - diff, outputDeviceData.end());
					outputDeviceData.resize(outputDeviceData.size() - diff);
					pLoopbackCaptureOutputDevice->ReturnAudioBytesToBuffer(overflow);
				}
				else if (inputDeviceData.size() > outputDeviceData.size()) {
					auto diff = inputDeviceData.size() - outputDeviceData.size();
					std::vector<BYTE> overflow(inputDeviceData.end() - diff, inputDeviceData.end());
					inputDeviceData.resize(inputDeviceData.size() - diff);
					pLoopbackCaptureInputDevice->ReturnAudioBytesToBuffer(overflow);
				}
			}
		};

		std::vector<BYTE> outputDeviceData = pLoopbackCaptureOutputDevice->GetRecordedBytes();
		std::vector<BYTE> inputDeviceData = pLoopbackCaptureInputDevice->GetRecordedBytes();
		returnAudioOverflowToBuffer(outputDeviceData, inputDeviceData);
		if (inputDeviceData.size() > 0 && outputDeviceData.size() && inputDeviceData.size() != outputDeviceData.size()) {
			LOG_ERROR(L"Mixing audio byte arrays with differing sizes");
		}

		return std::move(MixAudio(outputDeviceData, inputDeviceData, m_OutputVolumeModifier, m_InputVolumeModifier));
	}
	else if (m_IsOutputDeviceEnabled && pLoopbackCaptureOutputDevice)
		return std::move(MixAudio(pLoopbackCaptureOutputDevice->GetRecordedBytes(), std::vector<BYTE>(), m_OutputVolumeModifier, 1.0));
	else if (m_IsInputDeviceEnabled && pLoopbackCaptureInputDevice)
		return std::move(MixAudio(std::vector<BYTE>(), pLoopbackCaptureInputDevice->GetRecordedBytes(), 1.0, m_InputVolumeModifier));
	else
		return std::vector<BYTE>();
}

HRESULT RecordingManager::InitializeRects(_In_ RECT outputRect, _Out_ RECT *pSourceRect, _Out_ RECT *pDestRect) {
	UINT monitorWidth = RectWidth(outputRect);
	UINT monitorHeight = RectHeight(outputRect);

	RECT sourceRect;
	sourceRect.left = 0;
	sourceRect.right = monitorWidth;
	sourceRect.top = 0;
	sourceRect.bottom = monitorHeight;

	RECT destRect = sourceRect;
	if (m_DestRect.right > m_DestRect.left
		&& m_DestRect.bottom > m_DestRect.top)
	{
		destRect = m_DestRect;
	}

	*pSourceRect = sourceRect;
	*pDestRect = destRect;
	return S_OK;
}

HRESULT RecordingManager::InitializeDx(_Outptr_ ID3D11DeviceContext **ppContext, _Outptr_ ID3D11Device **ppDevice, _Outptr_opt_result_maybenull_ ID3D11Debug **ppDebug) {
	*ppContext = nullptr;
	*ppDevice = nullptr;
	if (ppDebug) {
		*ppDebug = nullptr;
	}
	HRESULT hr(S_OK);
	DXGI_OUTDUPL_DESC OutputDuplDesc;
	RtlZeroMemory(&OutputDuplDesc, sizeof(OutputDuplDesc));
	CComPtr<ID3D11DeviceContext> pContext = nullptr;
	CComPtr<ID3D11Device> pDevice = nullptr;
	CComPtr<IDXGIOutputDuplication> pDeskDupl = nullptr;
#if _DEBUG 
	CComPtr<ID3D11Debug> pDebug = nullptr;
#endif
	D3D_FEATURE_LEVEL featureLevel;

	size_t numDriverTypes = ARRAYSIZE(gDriverTypes);
	// Create devices
	for (UINT DriverTypeIndex = 0; DriverTypeIndex < numDriverTypes; ++DriverTypeIndex)
	{
		hr = D3D11CreateDevice(
			nullptr,
			gDriverTypes[DriverTypeIndex],
			nullptr,
#if _DEBUG 
			D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_DEBUG,
#else
			D3D11_CREATE_DEVICE_BGRA_SUPPORT,
#endif
			m_FeatureLevels,
			ARRAYSIZE(m_FeatureLevels),
			D3D11_SDK_VERSION,
			&pDevice,
			&featureLevel,
			&pContext);

		if (SUCCEEDED(hr))
		{
			// Device creation success, no need to loop anymore
			break;
		}
	}
	RETURN_ON_BAD_HR(hr);

	CComPtr<ID3D10Multithread> pMulti = nullptr;
	RETURN_ON_BAD_HR(hr = pContext->QueryInterface(IID_PPV_ARGS(&pMulti)));
	pMulti->SetMultithreadProtected(TRUE);
	pMulti.Release();

#if _DEBUG 
	RETURN_ON_BAD_HR(hr = pDevice->QueryInterface(IID_PPV_ARGS(&pDebug)));
#endif

	// Return the pointer to the caller.
	*ppContext = pContext;
	(*ppContext)->AddRef();
	*ppDevice = pDevice;
	(*ppDevice)->AddRef();
#if _DEBUG
	if (ppDebug) {
		*ppDebug = pDebug;
		(*ppDebug)->AddRef();
	}
#endif
	return hr;
}

HRESULT RecordingManager::InitializeVideoSinkWriter(
	_In_ std::wstring path,
	_In_opt_ IMFByteStream *pOutStream,
	_In_ ID3D11Device *pDevice,
	_In_ RECT sourceRect,
	_In_ RECT destRect,
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

	UINT sourceWidth = max(0, RectWidth(sourceRect));
	UINT sourceHeight = max(0, RectHeight(sourceRect));

	UINT destWidth = max(0, RectWidth(destRect));
	UINT destHeight = max(0, RectHeight(destRect));

	RETURN_ON_BAD_HR(ConfigureOutputMediaTypes(destWidth, destHeight, &pVideoMediaTypeOut, &pAudioMediaTypeOut));
	RETURN_ON_BAD_HR(ConfigureInputMediaTypes(sourceWidth, sourceHeight, rotationFormat, pVideoMediaTypeOut, &pVideoMediaTypeIn, &pAudioMediaTypeIn));

	//Creates a streaming writer
	CComPtr<IMFMediaSink> pMp4StreamSink = nullptr;
	if (m_EncoderOptions->GetIsFragmentedMp4Enabled()) {
		RETURN_ON_BAD_HR(MFCreateFMPEG4MediaSink(pOutStream, pVideoMediaTypeOut, pAudioMediaTypeOut, &pMp4StreamSink));
	}
	else {
		RETURN_ON_BAD_HR(MFCreateMPEG4MediaSink(pOutStream, pVideoMediaTypeOut, pAudioMediaTypeOut, &pMp4StreamSink));
	}
	pAudioMediaTypeOut.Release();

	// Passing 6 as the argument to save re-allocations
	RETURN_ON_BAD_HR(MFCreateAttributes(&pAttributes, 7));
	RETURN_ON_BAD_HR(pAttributes->SetGUID(MF_TRANSCODE_CONTAINERTYPE, MFTranscodeContainerType_MPEG4));
	RETURN_ON_BAD_HR(pAttributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, m_EncoderOptions->GetIsHardwareEncodingEnabled()));
	RETURN_ON_BAD_HR(pAttributes->SetUINT32(MF_MPEG4SINK_MOOV_BEFORE_MDAT, m_EncoderOptions->GetIsFastStartEnabled()));
	RETURN_ON_BAD_HR(pAttributes->SetUINT32(MF_LOW_LATENCY, m_EncoderOptions->GetIsLowLatencyModeEnabled()));
	RETURN_ON_BAD_HR(pAttributes->SetUINT32(MF_SINK_WRITER_DISABLE_THROTTLING, m_EncoderOptions->GetIsThrottlingDisabled()));
	// Add device manager to attributes. This enables hardware encoding.
	RETURN_ON_BAD_HR(pAttributes->SetUnknown(MF_SINK_WRITER_D3D_MANAGER, pDeviceManager));
	RETURN_ON_BAD_HR(pAttributes->SetUnknown(MF_SINK_WRITER_ASYNC_CALLBACK, pCallback));

	RETURN_ON_BAD_HR(MFCreateSinkWriterFromMediaSink(pMp4StreamSink, pAttributes, &pSinkWriter));
	pMp4StreamSink.Release();
	videoStreamIndex = 0;
	audioStreamIndex = 1;
	RETURN_ON_BAD_HR(pSinkWriter->SetInputMediaType(videoStreamIndex, pVideoMediaTypeIn, nullptr));
	bool isAudioEnabled = m_IsAudioEnabled
		&& (m_IsOutputDeviceEnabled || m_IsInputDeviceEnabled);

	if (isAudioEnabled) {
		RETURN_ON_BAD_HR(pSinkWriter->SetInputMediaType(audioStreamIndex, pAudioMediaTypeIn, nullptr));
		pAudioMediaTypeIn.Release();
	}

	CComPtr<ICodecAPI> encoder = nullptr;
	pSinkWriter->GetServiceForStream(videoStreamIndex, GUID_NULL, IID_PPV_ARGS(&encoder));
	if (encoder) {
		RETURN_ON_BAD_HR(SetAttributeU32(encoder, CODECAPI_AVEncCommonRateControlMode, m_EncoderOptions->GetVideoBitrateMode()));
		switch (m_EncoderOptions->GetVideoBitrateMode()) {
		case eAVEncCommonRateControlMode_Quality:
			RETURN_ON_BAD_HR(SetAttributeU32(encoder, CODECAPI_AVEncCommonQuality, m_EncoderOptions->GetVideoQuality()));
			break;
		default:
			break;
		}
	}

	if (destWidth != sourceWidth || destHeight != sourceHeight) {
		CComPtr<IMFVideoProcessorControl> videoProcessor = nullptr;
		HRESULT hr = GetVideoProcessor(pSinkWriter, videoStreamIndex, &videoProcessor);
		if (SUCCEEDED(hr)) {
			//The source rectangle is the portion of the input frame that is blitted to the destination surface.
			videoProcessor->SetSourceRectangle(&destRect);
			videoProcessor->SetRotation(ROTATION_NORMAL);
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

HRESULT RecordingManager::ConfigureOutputMediaTypes(
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
	RETURN_ON_BAD_HR(pVideoMediaType->SetGUID(MF_MT_SUBTYPE, m_EncoderOptions->GetVideoEncoderFormat()));
	RETURN_ON_BAD_HR(pVideoMediaType->SetUINT32(MF_MT_AVG_BITRATE, m_EncoderOptions->GetVideoBitrate()));
	RETURN_ON_BAD_HR(pVideoMediaType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive));
	RETURN_ON_BAD_HR(pVideoMediaType->SetUINT32(MF_MT_MPEG2_PROFILE, m_EncoderOptions->GetEncoderProfile()));
	RETURN_ON_BAD_HR(pVideoMediaType->SetUINT32(MF_MT_YUV_MATRIX, MFVideoTransferMatrix_BT601));
	RETURN_ON_BAD_HR(MFSetAttributeSize(pVideoMediaType, MF_MT_FRAME_SIZE, destWidth, destHeight));
	RETURN_ON_BAD_HR(MFSetAttributeRatio(pVideoMediaType, MF_MT_FRAME_RATE, m_EncoderOptions->GetVideoFps(), 1));
	RETURN_ON_BAD_HR(MFSetAttributeRatio(pVideoMediaType, MF_MT_PIXEL_ASPECT_RATIO, 1, 1));

	bool isAudioEnabled = m_IsAudioEnabled
		&& (m_IsOutputDeviceEnabled || m_IsInputDeviceEnabled);

	if (isAudioEnabled) {
		// Set the output audio type.
		RETURN_ON_BAD_HR(MFCreateMediaType(&pAudioMediaType));
		RETURN_ON_BAD_HR(pAudioMediaType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio));
		RETURN_ON_BAD_HR(pAudioMediaType->SetGUID(MF_MT_SUBTYPE, AUDIO_ENCODING_FORMAT));
		RETURN_ON_BAD_HR(pAudioMediaType->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, m_AudioChannels));
		RETURN_ON_BAD_HR(pAudioMediaType->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, AUDIO_BITS_PER_SAMPLE));
		RETURN_ON_BAD_HR(pAudioMediaType->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, AUDIO_SAMPLES_PER_SECOND));
		RETURN_ON_BAD_HR(pAudioMediaType->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, m_AudioBitrate));

		*pAudioMediaTypeOut = pAudioMediaType;
		(*pAudioMediaTypeOut)->AddRef();
	}

	*pVideoMediaTypeOut = pVideoMediaType;
	(*pVideoMediaTypeOut)->AddRef();
	return S_OK;
}

HRESULT RecordingManager::ConfigureInputMediaTypes(
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
	// Set the input video type.
	CreateInputMediaTypeFromOutput(pVideoMediaTypeOut, VIDEO_INPUT_FORMAT, &pVideoMediaType);
	RETURN_ON_BAD_HR(MFSetAttributeSize(pVideoMediaType, MF_MT_FRAME_SIZE, sourceWidth, sourceHeight));
	pVideoMediaType->SetUINT32(MF_MT_VIDEO_ROTATION, rotationFormat);

	bool isAudioEnabled = m_IsAudioEnabled
		&& (m_IsOutputDeviceEnabled || m_IsInputDeviceEnabled);

	if (isAudioEnabled) {
		// Set the input audio type.
		RETURN_ON_BAD_HR(MFCreateMediaType(&pAudioMediaType));
		RETURN_ON_BAD_HR(pAudioMediaType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio));
		RETURN_ON_BAD_HR(pAudioMediaType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM));
		RETURN_ON_BAD_HR(pAudioMediaType->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, AUDIO_BITS_PER_SAMPLE));
		RETURN_ON_BAD_HR(pAudioMediaType->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, AUDIO_SAMPLES_PER_SECOND));
		RETURN_ON_BAD_HR(pAudioMediaType->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, m_AudioChannels));

		*pAudioMediaTypeIn = pAudioMediaType;
		(*pAudioMediaTypeIn)->AddRef();
	}

	*pVideoMediaTypeIn = pVideoMediaType;
	(*pVideoMediaTypeIn)->AddRef();
	return S_OK;
}

HRESULT RecordingManager::InitializeAudioCapture(_Outptr_result_maybenull_ LoopbackCapture **outputAudioCapture, _Outptr_result_maybenull_ LoopbackCapture **inputAudioCapture)
{
	LoopbackCapture *pLoopbackCaptureOutputDevice = nullptr;
	LoopbackCapture *pLoopbackCaptureInputDevice = nullptr;
	HRESULT hr;
	bool recordAudio = m_RecorderMode == MODE_VIDEO && m_IsAudioEnabled;
	if (recordAudio && (m_IsOutputDeviceEnabled || m_IsInputDeviceEnabled)) {
		if (m_IsOutputDeviceEnabled)
		{
			pLoopbackCaptureOutputDevice = new LoopbackCapture(L"AudioOutputDevice");
			hr = pLoopbackCaptureOutputDevice->StartCapture(AUDIO_SAMPLES_PER_SECOND, m_AudioChannels, m_AudioOutputDevice, eRender);
		}

		if (m_IsInputDeviceEnabled)
		{
			pLoopbackCaptureInputDevice = new LoopbackCapture(L"AudioInputDevice");
			hr = pLoopbackCaptureInputDevice->StartCapture(AUDIO_SAMPLES_PER_SECOND, m_AudioChannels, m_AudioInputDevice, eCapture);
		}
	}
	else {
		hr = S_FALSE;
	}
	*outputAudioCapture = pLoopbackCaptureOutputDevice;
	*inputAudioCapture = pLoopbackCaptureInputDevice;
	return hr;
}

void RecordingManager::InitializeMouseClickDetection()
{
	if (m_IsMouseClicksDetected) {
		switch (m_MouseClickDetectionMode)
		{
		default:
		case MOUSE_DETECTION_MODE_POLLING: {
			cancellation_token token = m_TaskWrapperImpl->m_RecordTaskCts.get_token();
			concurrency::create_task([this, token]() {
				LOG_INFO("Starting mouse click polling task");
				while (true) {
					if (GetKeyState(VK_LBUTTON) < 0)
					{
						//If left mouse button is held, reset the duration of click duration
						g_LastMouseClickButton = VK_LBUTTON;
						g_LastMouseClickDurationRemaining = g_MouseClickDetectionDurationMillis;
					}
					else if (GetKeyState(VK_RBUTTON) < 0)
					{
						//If right mouse button is held, reset the duration of click duration
						g_LastMouseClickButton = VK_RBUTTON;
						g_LastMouseClickDurationRemaining = g_MouseClickDetectionDurationMillis;
					}

					if (token.is_canceled()) {
						break;
					}
					wait(1);
				}
				LOG_INFO("Exiting mouse click polling task");
				});
			break;
		}
		case MOUSE_DETECTION_MODE_HOOK: {
			m_Mousehook = SetWindowsHookEx(WH_MOUSE_LL, MouseHookProc, nullptr, 0);
			break;
		}
		}
	}
}

HRESULT RecordingManager::CreateInputMediaTypeFromOutput(
	_In_ IMFMediaType *pType,    // Pointer to an encoded video type.
	_In_ const GUID &subtype,    // Uncompressed subtype (eg, RGB-32, AYUV)
	_Outptr_ IMFMediaType **ppType   // Receives a matching uncompressed video type.
)
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
	// Set the subtype.
	RETURN_ON_BAD_HR(hr = pTypeUncomp->SetGUID(MF_MT_SUBTYPE, subtype));
	// Uncompressed means all samples are independent.
	RETURN_ON_BAD_HR(hr = pTypeUncomp->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE));
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

HRESULT RecordingManager::DrawMousePointer(_In_ ID3D11Texture2D *frame, _In_ MousePointer *pMousePointer, _In_ PTR_INFO *ptrInfo, _In_ INT64 durationSinceLastFrame100Nanos)
{
	HRESULT hr = S_FALSE;
	if (g_LastMouseClickDurationRemaining > 0
		&& m_IsMouseClicksDetected)
	{
		if (g_LastMouseClickButton == VK_LBUTTON)
		{
			hr = pMousePointer->DrawMouseClick(ptrInfo, frame, m_MouseClickDetectionLMBColor, (float)m_MouseClickDetectionRadius, DXGI_MODE_ROTATION_UNSPECIFIED);
		}
		if (g_LastMouseClickButton == VK_RBUTTON)
		{
			hr = pMousePointer->DrawMouseClick(ptrInfo, frame, m_MouseClickDetectionRMBColor, (float)m_MouseClickDetectionRadius, DXGI_MODE_ROTATION_UNSPECIFIED);
		}
		INT64 millis = max(HundredNanosToMillis(durationSinceLastFrame100Nanos), 0);
		g_LastMouseClickDurationRemaining = max(g_LastMouseClickDurationRemaining - millis, 0);
		LOG_DEBUG("Drawing mouse click, duration remaining on click is %u ms", g_LastMouseClickDurationRemaining);
	}

	if (m_IsMousePointerEnabled) {
		hr = pMousePointer->DrawMousePointer(ptrInfo, frame, DXGI_MODE_ROTATION_UNSPECIFIED);
	}
	return hr;
}

HRESULT RecordingManager::CropFrame(_In_ ID3D11Texture2D *frame, _In_ RECT destRect, _Outptr_ ID3D11Texture2D **pCroppedFrame)
{
	D3D11_TEXTURE2D_DESC frameDesc;
	frame->GetDesc(&frameDesc);
	frameDesc.Width = RectWidth(destRect);
	frameDesc.Height = RectHeight(destRect);
	CComPtr<ID3D11Texture2D> pCroppedFrameCopy = nullptr;
	RETURN_ON_BAD_HR(m_Device->CreateTexture2D(&frameDesc, nullptr, &pCroppedFrameCopy));
	D3D11_BOX sourceRegion;
	RtlZeroMemory(&sourceRegion, sizeof(sourceRegion));
	sourceRegion.left = destRect.left;
	sourceRegion.right = destRect.right;
	sourceRegion.top = destRect.top;
	sourceRegion.bottom = destRect.bottom;
	sourceRegion.front = 0;
	sourceRegion.back = 1;
	m_ImmediateContext->CopySubresourceRegion(pCroppedFrameCopy, 0, 0, 0, 0, frame, 0, &sourceRegion);
	*pCroppedFrame = pCroppedFrameCopy;
	(*pCroppedFrame)->AddRef();
	return S_OK;
}

HRESULT RecordingManager::GetVideoProcessor(_In_ IMFSinkWriter *pSinkWriter, _In_ DWORD streamIndex, _Outptr_ IMFVideoProcessorControl **pVideoProcessor)
{
	HRESULT hr;
	GUID transformType;
	DWORD transformIndex = 0;
	CComPtr<IMFVideoProcessorControl> videoProcessor = nullptr;
	CComPtr<IMFSinkWriterEx>      pSinkWriterEx = nullptr;
	RETURN_ON_BAD_HR(hr = pSinkWriter->QueryInterface(&pSinkWriterEx));
	while (true) {
		CComPtr<IMFTransform> transform = nullptr;
		RETURN_ON_BAD_HR(hr = pSinkWriterEx->GetTransformForStream(streamIndex, transformIndex, &transformType, &transform));
		if (transformType == MFT_CATEGORY_VIDEO_PROCESSOR) {
			RETURN_ON_BAD_HR(hr = transform->QueryInterface(&videoProcessor));
			break;
		}
		transformIndex++;
	}
	*pVideoProcessor = videoProcessor;
	(*pVideoProcessor)->AddRef();
	return hr;
}

HRESULT RecordingManager::SetAttributeU32(_Inout_ CComPtr<ICodecAPI> &codec, _In_ const GUID &guid, _In_ UINT32 value)
{
	VARIANT val;
	val.vt = VT_UI4;
	val.uintVal = value;
	return codec->SetValue(&guid, &val);
}

HRESULT RecordingManager::RenderFrame(_In_ FrameWriteModel &model) {
	HRESULT hr(S_OK);

	if (m_RecorderMode == MODE_VIDEO) {
		hr = WriteFrameToVideo(model.StartPos, model.Duration, m_VideoStreamIndex, model.Frame);
		bool wroteAudioSample = false;
		if (FAILED(hr)) {
			_com_error err(hr);
			LOG_ERROR(L"Writing of video frame with start pos %lld ms failed: %s", (HundredNanosToMillis(model.StartPos)), err.ErrorMessage());
			return hr;//Stop recording if we fail
		}
		bool paddedAudio = false;
		bool isAudioEnabled = m_IsAudioEnabled
			&& (m_IsOutputDeviceEnabled || m_IsInputDeviceEnabled);
		/* If the audio pCaptureInstance returns no data, i.e. the source is silent, we need to pad the PCM stream with zeros to give the media sink silence as input.
		 * If we don't, the sink writer will begin throttling video frames because it expects audio samples to be delivered, and think they are delayed.
		 * We ignore every instance where the last frame had audio, due to sometimes very short frame durations due to mouse cursor changes have zero audio length,
		 * and inserting silence between two frames that has audio leads to glitching. */
		if (isAudioEnabled && model.Audio.size() == 0 && model.Duration > 0) {
			if (!m_LastFrameHadAudio) {
				int frameCount = int(ceil(AUDIO_SAMPLES_PER_SECOND * HundredNanosToMillis(model.Duration) / 1000));
				int byteCount = frameCount * (AUDIO_BITS_PER_SAMPLE / 8) * m_AudioChannels;
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
	else if (m_RecorderMode == MODE_SLIDESHOW) {
		wstring	path = m_OutputFolder + L"\\" + to_wstring(model.FrameNumber) + GetImageExtension();
		hr = WriteFrameToImage(model.Frame, path);
		INT64 startposMs = HundredNanosToMillis(model.StartPos);
		INT64 durationMs = HundredNanosToMillis(model.Duration);
		if (FAILED(hr)) {
			_com_error err(hr);
			LOG_ERROR(L"Writing of video slideshow frame with start pos %lld ms failed: %s", startposMs, err.ErrorMessage());
			return hr; //Stop recording if we fail
		}
		else {

			m_FrameDelays.insert(std::pair<wstring, int>(path, model.FrameNumber == 0 ? 0 : (int)durationMs));
			LOG_TRACE(L"Wrote video slideshow frame with start pos %lld ms and with duration %lld ms", startposMs, durationMs);
		}
	}
	else if (m_RecorderMode == MODE_SNAPSHOT) {
		hr = WriteFrameToImage(model.Frame, m_OutputFullPath);
		LOG_TRACE(L"Wrote snapshot to %s", m_OutputFullPath.c_str());
	}
	model.Frame.Release();
	return hr;
}

bool RecordingManager::CheckDependencies(_Out_ std::wstring *error)
{
	wstring errorText;
	bool result = true;
	HKEY hk;
	DWORD errorCode;
	if (m_RecorderApi == API_DESKTOP_DUPLICATION && !IsWindows8OrGreater()) {
		errorText = L"Desktop Duplication requires Windows 8 or greater.";
		result = false;
	}
	else if (m_RecorderApi == API_GRAPHICS_CAPTURE && !Graphics::Capture::Util::IsGraphicsCaptureAvailable())
	{
		errorText = L"Windows Graphics Capture requires Windows 10 version 1803 or greater.";
		result = false;
	}
	else if (errorCode = RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Setup\\WindowsFeatures\\WindowsMediaVersion", 0, KEY_READ, &hk) != ERROR_SUCCESS) {
		errorText = L"Missing dependency: Windows Media Features.";
		result = false;
	}
	*error = errorText;
	return result;
}

std::string RecordingManager::CurrentTimeToFormattedString()
{
	chrono::system_clock::time_point p = chrono::system_clock::now();
	time_t t = chrono::system_clock::to_time_t(p);
	struct tm newTime;
	auto err = localtime_s(&newTime, &t);

	std::stringstream ss;
	if (err)
		ss << "NEW";
	else
		ss << std::put_time(&newTime, "%Y-%m-%d %X");
	string time = ss.str();
	std::replace(time.begin(), time.end(), ':', '-');
	return time;
}
HRESULT RecordingManager::WriteFrameToImage(_In_ ID3D11Texture2D *pAcquiredDesktopImage, _In_ std::wstring filePath)
{
	return SaveWICTextureToFile(m_ImmediateContext, pAcquiredDesktopImage, m_ImageEncoderFormat, filePath.c_str());
}

void RecordingManager::WriteFrameToImageAsync(_In_ ID3D11Texture2D *pAcquiredDesktopImage, _In_ std::wstring filePath)
{
	pAcquiredDesktopImage->AddRef();
	concurrency::create_task([this, pAcquiredDesktopImage, filePath]() {
		return WriteFrameToImage(pAcquiredDesktopImage, filePath);
		}).then([this, filePath, pAcquiredDesktopImage](concurrency::task<HRESULT> t)
			{
				try {
					HRESULT hr = t.get();
					bool success = SUCCEEDED(hr);
					if (success) {
						LOG_TRACE(L"Wrote snapshot to %s", filePath.c_str());
						if (RecordingSnapshotCreatedCallback != nullptr) {
							RecordingSnapshotCreatedCallback(filePath);
						}
					}
					else {
						_com_error err(hr);
						LOG_ERROR("Error saving snapshot: %s", err.ErrorMessage());
					}
					// if .get() didn't throw and the HRESULT succeeded, there are no errors.
				}
				catch (const exception &e) {
					// handle error
					LOG_ERROR(L"Exception saving snapshot: %s", e.what());
				}
				pAcquiredDesktopImage->Release();
			});
}
/// <summary>
/// Take screenshots in a video recording, if video recording is file mode.
/// </summary>
HRESULT RecordingManager::TakeSnapshotsWithVideo(_In_ ID3D11Texture2D *frame, _In_ RECT destRect)
{
	if (m_OutputSnapshotsFolderPath.empty())
		return S_FALSE;

	HRESULT hr = S_OK;
	CComPtr<ID3D11Texture2D> pFrameCopyForSnapshotsWithVideo = nullptr;
	D3D11_TEXTURE2D_DESC frameDesc;
	frame->GetDesc(&frameDesc);
	int destWidth = RectWidth(destRect);
	int destHeight = RectHeight(destRect);
	if (frameDesc.Width > destWidth
		|| frameDesc.Height > destHeight) {
		////If the source frame is larger than the destionation rect, we crop it, to avoid black borders around the snapshots.
		RETURN_ON_BAD_HR(hr = CropFrame(frame, destRect, &pFrameCopyForSnapshotsWithVideo));
	}
	else {
		m_Device->CreateTexture2D(&frameDesc, nullptr, &pFrameCopyForSnapshotsWithVideo);
		// Copy the current frame for a separate thread to write it to a file asynchronously.
		m_ImmediateContext->CopyResource(pFrameCopyForSnapshotsWithVideo, frame);
	}

	m_previousSnapshotTaken = steady_clock::now();
	wstring snapshotPath = m_OutputSnapshotsFolderPath + L"\\" + s2ws(CurrentTimeToFormattedString()) + GetImageExtension();
	WriteFrameToImageAsync(pFrameCopyForSnapshotsWithVideo, snapshotPath.c_str());
	return hr;
}

HRESULT RecordingManager::WriteFrameToVideo(_In_ INT64 frameStartPos, _In_ INT64 frameDuration, _In_ DWORD streamIndex, _In_ ID3D11Texture2D *pAcquiredDesktopImage)
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
HRESULT RecordingManager::WriteAudioSamplesToVideo(_In_ INT64 frameStartPos, _In_ INT64 frameDuration, _In_ DWORD streamIndex, _In_ BYTE *pSrc, _In_ DWORD cbData)
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
		Sleep(100);
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