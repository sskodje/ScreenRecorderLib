#include <ppltasks.h> 
#include <concrt.h>
#include <mfidl.h>
#include <VersionHelpers.h>
#include <filesystem>
#include <WinSDKVer.h>
#include "Util.h"
#include "MF.util.h"
#include "LoopbackCapture.h"
#include "RecordingManager.h"
#include "TextureManager.h"
#include "ScreenCaptureManager.h"
#include "WindowsGraphicsCapture.util.h"
#include "Cleanup.h"
#include "Screengrab.h"
#include "DynamicWait.h"
#include "HighresTimer.h"
#include "AudioPrefs.h"

#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "D3D11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "Mfuuid.lib")
#pragma comment(lib, "Mfplat.lib")
#pragma comment(lib, "evr.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "Mf.lib")
#pragma comment(lib, "wmcodecdspuuid.lib")
#pragma comment(lib, "dwmapi.lib")

using namespace std;
using namespace std::chrono;
using namespace concurrency;
using namespace DirectX;
using namespace winrt::Windows::Graphics::DirectX;
using namespace winrt::Windows::Graphics::Capture;
#if _DEBUG
static std::mutex m_DxDebugMutex{};
bool isLoggingEnabled = true;
int logSeverityLevel = LOG_LVL_TRACE;
#else
bool isLoggingEnabled = false;
int logSeverityLevel = LOG_LVL_INFO;
#endif

std::wstring logFilePath;

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
	RecordingFrameNumberChangedCallback(nullptr),
	m_TextureManager(nullptr),
	m_OutputManager(nullptr),
	m_EncoderOptions(new H264_ENCODER_OPTIONS()),
	m_AudioOptions(new AUDIO_OPTIONS),
	m_MouseOptions(new MOUSE_OPTIONS),
	m_SnapshotOptions(new SNAPSHOT_OPTIONS),
	m_OutputOptions(new OUTPUT_OPTIONS),
	m_IsDestructing(false),
	m_RecordingSources{},
	m_DxResources{}
{
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
	m_MfStartupResult = MFStartup(MF_VERSION, MFSTARTUP_LITE);
	if (SUCCEEDED(m_MfStartupResult)) {
		LOG_INFO(L"Media Foundation started");
	}
	else {
		LOG_ERROR("Media foundation failed to start: hr = 0x%08x", m_MfStartupResult);
	}
}

RecordingManager::~RecordingManager()
{
	if (!m_TaskWrapperImpl->m_RecordTask.is_done()) {
		m_IsDestructing = true;
		LOG_WARN("Recording is in progress while destructing, cancelling recording task and waiting for completion.");
		m_TaskWrapperImpl->m_RecordTaskCts.cancel();
		m_TaskWrapperImpl->m_RecordTask.wait();
		LOG_DEBUG("Wait for recording task completed.");
	}
	ClearRecordingSources();
	ClearOverlays();
	CleanDx(&m_DxResources);
	MFShutdown();
	LOG_INFO(L"Media Foundation shut down");
}

void RecordingManager::SetLogEnabled(bool value) {
	isLoggingEnabled = value;
}
void RecordingManager::SetLogFilePath(std::wstring value) {
	logFilePath = value;
}
void RecordingManager::SetLogSeverityLevel(int value) {
	logSeverityLevel = value;
}


HRESULT RecordingManager::ConfigureOutputDir(_In_ std::wstring path) {
	m_OutputFullPath = path;
	auto recorderMode = GetOutputOptions()->GetRecorderMode();
	if (!path.empty()) {
		wstring dir = path;
		if (recorderMode == RecorderModeInternal::Slideshow) {
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
				RecordingFailedCallback(L"Failed to create output folder: " + s2ws(ec.message()), L"");
			return E_FAIL;
		}

		if (recorderMode == RecorderModeInternal::Video || recorderMode == RecorderModeInternal::Screenshot) {
			wstring ext = recorderMode == RecorderModeInternal::Video ? m_EncoderOptions->GetVideoExtension() : m_SnapshotOptions->GetImageExtension();
			LPWSTR pStrExtension = PathFindExtension(path.c_str());
			if (pStrExtension == nullptr || pStrExtension[0] == 0)
			{
				m_OutputFullPath = m_OutputFolder + L"\\" + s2ws(CurrentTimeToFormattedString()) + ext;
			}
			if (m_SnapshotOptions->IsSnapshotWithVideoEnabled() && m_SnapshotOptions->GetSnapshotsDirectory().empty()) {
				// Snapshots will be saved in a folder named as video file name without extension. 
				m_SnapshotOptions->SetSnapshotDirectory(m_OutputFullPath.substr(0, m_OutputFullPath.find_last_of(L".")));
			}
		}
	}
	if (!m_SnapshotOptions->GetSnapshotsDirectory().empty()) {
		std::error_code ec;
		if (std::filesystem::exists(m_SnapshotOptions->GetSnapshotsDirectory()) || std::filesystem::create_directories(m_SnapshotOptions->GetSnapshotsDirectory(), ec))
		{
			LOG_DEBUG(L"Snapshot output folder is ready");
		}
		else
		{
			// Failed to create snapshot directory.
			LOG_ERROR(L"failed to create snapshot output folder");
			if (RecordingFailedCallback != nullptr)
				RecordingFailedCallback(L"Failed to create snapshot output folder: " + s2ws(ec.message()), L"");
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
			RecordingFailedCallback(error, L"");
		return S_FALSE;
	}
	wstring errorText;
	if (!CheckDependencies(&errorText)) {
		LOG_ERROR(L"%ls", errorText);
		if (RecordingFailedCallback != nullptr)
			RecordingFailedCallback(errorText, L"");
		return S_FALSE;
	}
	m_EncoderResult = S_FALSE;
	RETURN_ON_BAD_HR(ConfigureOutputDir(path));

	if (m_RecordingSources.size() == 0) {
		std::wstring error = L"No valid recording sources found in recorder parameters.";
		LOG_ERROR("%ls", error.c_str());
		if (RecordingFailedCallback != nullptr)
			RecordingFailedCallback(error, L"");
		return S_FALSE;
	}
	m_TaskWrapperImpl->m_RecordTaskCts = cancellation_token_source();
	m_TaskWrapperImpl->m_RecordTask = concurrency::create_task([this, stream]() {
		LOG_INFO(L"Starting recording task");
		m_IsRecording = true;
		REC_RESULT result{};
		RETURN_RESULT_ON_BAD_HR(m_MfStartupResult, L"Media Foundation failed to initialize");
		HRESULT hr = CoInitializeEx(nullptr, COINITBASE_MULTITHREADED | COINIT_DISABLE_OLE1DDE);
		RETURN_RESULT_ON_BAD_HR(hr, L"CoInitializeEx failed");
		RETURN_RESULT_ON_BAD_HR(hr = InitializeDx(nullptr, &m_DxResources), L"Failed to initialize DirectX");

		m_TextureManager = make_unique<TextureManager>();
		m_TextureManager->Initialize(m_DxResources.Context, m_DxResources.Device);
		m_OutputManager = make_unique<OutputManager>();
		m_OutputManager->Initialize(m_DxResources.Context, m_DxResources.Device, GetEncoderOptions(), GetAudioOptions(), GetSnapshotOptions(), GetOutputOptions());

		result = StartRecorderLoop(m_RecordingSources, m_Overlays, stream);
		if (RecordingStatusChangedCallback != nullptr && !m_IsDestructing) {
			RecordingStatusChangedCallback(STATUS_FINALIZING);
		}
		result.FinalizeResult = m_OutputManager->FinalizeRecording();
		CoUninitialize();

		LOG_INFO("Exiting recording task");
		return result;
		}).then([this](concurrency::task<REC_RESULT> t)
				{
					m_IsRecording = false;
					REC_RESULT result{ };
					try {
						result = t.get();
						// if .get() didn't throw and the HRESULT succeeded, there are no errors.
					}
					catch (const exception &e) {
						// handle error
						LOG_ERROR(L"Exception in RecordTask: %s", e.what());
					}
					catch (...) {
						LOG_ERROR(L"Exception in RecordTask");
					}
					CleanupDxResources();
					if (!m_IsDestructing) {
						nlohmann::fifo_map<std::wstring, int> delays{};
						if (m_OutputManager) {
							delays = m_OutputManager->GetFrameDelays();
						}
						SetRecordingCompleteStatus(result, delays);
					}
				});
		return S_OK;
}

void RecordingManager::EndRecording() {
	if (m_IsRecording) {
		m_IsPaused = false;
		m_TaskWrapperImpl->m_RecordTaskCts.cancel();
		LOG_DEBUG(L"Stopped recording task");
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

void RecordingManager::CleanupDxResources()
{
	SafeRelease(&m_DxResources.Context);
	SafeRelease(&m_DxResources.Device);
#if _DEBUG
	if (m_DxResources.Debug) {
		m_DxDebugMutex.lock();
		m_DxResources.Debug->ReportLiveDeviceObjects(D3D11_RLDO_DETAIL | D3D11_RLDO_IGNORE_INTERNAL);
		m_DxDebugMutex.unlock();
		SafeRelease(&m_DxResources.Debug);
	}
#endif
}

void RecordingManager::SetRecordingCompleteStatus(_In_ REC_RESULT result, nlohmann::fifo_map<std::wstring, int> frameDelays)
{
	std::wstring errMsg = L"";
	bool isSuccess = SUCCEEDED(result.RecordingResult) && SUCCEEDED(result.FinalizeResult);
	if (!isSuccess) {
		if (SUCCEEDED(result.RecordingResult) && FAILED(result.FinalizeResult)) {
			_com_error err(result.FinalizeResult);
			errMsg = err.ErrorMessage();
		}
		else {
			_com_error err(result.RecordingResult);
			errMsg = err.ErrorMessage();
		}
		if (!result.Error.empty()) {
			errMsg = string_format(L"%ls : %ls", result.Error.c_str(), errMsg.c_str());
		}
	}

	if (RecordingStatusChangedCallback) {
		RecordingStatusChangedCallback(STATUS_IDLE);
		LOG_DEBUG("Changed Recording Status to Idle");
	}
	if (isSuccess) {
		if (RecordingCompleteCallback)
			RecordingCompleteCallback(m_OutputFullPath, frameDelays);
		LOG_DEBUG("Sent Recording Complete callback");
	}
	else {
		if (RecordingFailedCallback) {
			if (FAILED(m_EncoderResult)) {
				_com_error encoderFailure(m_EncoderResult);
				errMsg = string_format(L"Write error (0x%lx) in video encoder: %s", m_EncoderResult, encoderFailure.ErrorMessage());
				if (GetEncoderOptions()->GetIsHardwareEncodingEnabled()) {
					errMsg += L" If the problem persists, disabling hardware encoding may improve stability.";
				}
			}
			else {
				if (errMsg.empty()) {
					errMsg = GetLastErrorStdWstr();
				}
			}
			if (SUCCEEDED(result.FinalizeResult)) {
				RecordingFailedCallback(errMsg, m_OutputFullPath);
			}
			else {
				RecordingFailedCallback(errMsg, L"");
			}

			LOG_DEBUG("Sent Recording Failed callback");
		}
	}
}

REC_RESULT RecordingManager::StartRecorderLoop(_In_ const std::vector<RECORDING_SOURCE *> &sources, _In_ const std::vector<RECORDING_OVERLAY *> &overlays, _In_opt_ IStream *pStream)
{
	CComPtr<ID3D11Texture2D> pPreviousFrameCopy = nullptr;
	CComPtr<ID3D11Texture2D> pCurrentFrameCopy = nullptr;
	std::optional<PTR_INFO> pPtrInfo = std::nullopt;
	unique_ptr<ScreenCaptureManager> pCapture = make_unique<ScreenCaptureManager>();
	HRESULT hr = pCapture->Initialize(m_DxResources.Context, m_DxResources.Device, GetOutputOptions());
	RETURN_RESULT_ON_BAD_HR(hr, L"Failed to initialize ScreenCaptureManager");
	auto recorderMode = GetOutputOptions()->GetRecorderMode();

	// Event for when a thread encounters an error
	HANDLE ErrorEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
	if (nullptr == ErrorEvent) {
		LOG_ERROR(L"CreateEvent failed: last error is %u", GetLastError());
		return CAPTURE_RESULT(E_FAIL, L"Failed to create event");
	}
	CloseHandleOnExit closeExpectedErrorEvent(ErrorEvent);

	RETURN_RESULT_ON_BAD_HR(hr = pCapture->StartCapture(sources, overlays, ErrorEvent), L"Failed to start capture");

	CaptureStopOnExit stopCaptureOnExit(pCapture.get());

	RECT videoInputFrameRect{};
	SIZE videoOutputFrameSize{};
	RETURN_RESULT_ON_BAD_HR(hr = InitializeRects(pCapture->GetOutputSize(), &videoInputFrameRect, &videoOutputFrameSize), L"Failed to initialize frame rects");
	if (pStream) {
		RETURN_RESULT_ON_BAD_HR(hr = m_OutputManager->BeginRecording(pStream, videoOutputFrameSize), L"Failed to initialize video sink writer");
	}
	else {
		RETURN_RESULT_ON_BAD_HR(hr = m_OutputManager->BeginRecording(m_OutputFullPath, videoOutputFrameSize), L"Failed to initialize video sink writer");
	}

	std::unique_ptr<AudioManager> pAudioManager = make_unique<AudioManager>();
	std::unique_ptr<MouseManager> pMouseManager = make_unique<MouseManager>();
	RETURN_RESULT_ON_BAD_HR(hr = pMouseManager->Initialize(m_DxResources.Context, m_DxResources.Device, GetMouseOptions()), L"Failed to initialize mouse manager");
	SetViewPort(m_DxResources.Context, static_cast<float>(videoOutputFrameSize.cx), static_cast<float>(videoOutputFrameSize.cy));

	if (recorderMode == RecorderModeInternal::Video) {
		hr = pAudioManager->Initialize(GetAudioOptions());
		if (FAILED(hr)) {
			LOG_ERROR(L"Audio capture failed to start: hr = 0x%08x", hr);
		}
	}

	std::chrono::steady_clock::time_point lastFrame = std::chrono::steady_clock::now();
	std::chrono::steady_clock::time_point previousSnapshotTaken = (std::chrono::steady_clock::time_point::min)();
	INT64 videoFrameDurationMillis = 0;
	if (recorderMode == RecorderModeInternal::Video) {
		videoFrameDurationMillis = 1000 / GetEncoderOptions()->GetVideoFps();
	}
	else if (recorderMode == RecorderModeInternal::Slideshow) {
		videoFrameDurationMillis = GetSnapshotOptions()->GetSnapshotsInterval().count();
	}
	INT64 videoFrameDuration100Nanos = MillisToHundredNanos(videoFrameDurationMillis);

	int frameNr = 0;
	INT64 lastFrameStartPos100Nanos = 0;
	bool havePrematureFrame = false;
	cancellation_token token = m_TaskWrapperImpl->m_RecordTaskCts.get_token();
	INT64 minimumTimeForDelay100Nanons = 5000;//0.5ms
	INT64 maxFrameLengthMillis = HundredNanosToMillis(m_MaxFrameLength100Nanos);
	DynamicWait DynamicWait;


	auto IsTimeToTakeSnapshot([&]()
	{
		// The first condition is needed since (now - min) yields negative value because of overflow...
		return previousSnapshotTaken == (std::chrono::steady_clock::time_point::min)() ||
			(std::chrono::steady_clock::now() - previousSnapshotTaken) > GetSnapshotOptions()->GetSnapshotsInterval();
	});

	auto ShouldSkipDelay([&](CAPTURED_FRAME capturedFrame)
	{
		if (m_OutputManager->GetRenderedFrameCount() == 0) {
			return true;
		}

		if (recorderMode == RecorderModeInternal::Video)
		{
			if (!GetEncoderOptions()->GetIsFixedFramerate()
				&& (GetMouseOptions()->IsMousePointerEnabled() && capturedFrame.PtrInfo && capturedFrame.PtrInfo->IsPointerShapeUpdated)//and never delay when pointer changes if we draw pointer
				|| (GetSnapshotOptions()->IsSnapshotWithVideoEnabled() && IsTimeToTakeSnapshot())) // Or if we need to write a snapshot 
			{
				return true;
			}
		}
		return false;
	});
	auto PrepareAndRenderFrame([&](CComPtr<ID3D11Texture2D> pTextureToRender, INT64 duration100Nanos)->HRESULT {
		HRESULT renderHr = E_FAIL;
		if (pPtrInfo) {
			renderHr = pMouseManager->ProcessMousePointer(pTextureToRender, &pPtrInfo.value());
			if (FAILED(renderHr)) {
				_com_error err(renderHr);
				LOG_ERROR(L"Error drawing mouse pointer: %s", err.ErrorMessage());
				//We just log the error and continue if the mouse pointer failed to draw. If there is an error with DXGI, it will be handled on the next call to AcquireNextFrame.
			}
		}
		if (IsValidRect(GetOutputOptions()->GetSourceRectangle())) {
			RETURN_ON_BAD_HR(hr = InitializeRects(pCapture->GetOutputSize(), &videoInputFrameRect, nullptr));
		}
		CComPtr<ID3D11Texture2D> processedTexture;
		RETURN_ON_BAD_HR(renderHr = ProcessTextureTransforms(pTextureToRender, &processedTexture, videoInputFrameRect, videoOutputFrameSize));
		if (renderHr == S_OK) {
			pTextureToRender.Release();
			pTextureToRender.Attach(processedTexture);
			(*pTextureToRender).AddRef();
		}
		if (recorderMode == RecorderModeInternal::Video) {
			if (GetSnapshotOptions()->IsSnapshotWithVideoEnabled() && IsTimeToTakeSnapshot()) {
				if (SUCCEEDED(renderHr = SaveTextureAsVideoSnapshot(pTextureToRender, videoInputFrameRect))) {
					previousSnapshotTaken = steady_clock::now();
				}
				else {
					_com_error err(renderHr);
					LOG_ERROR("Error saving video snapshot: %ls", err.ErrorMessage());
				}
			}
		}


		FrameWriteModel model{};
		model.Frame = pTextureToRender;
		model.Duration = duration100Nanos;
		model.StartPos = lastFrameStartPos100Nanos;
		model.Audio = pAudioManager->GrabAudioFrame();
		RETURN_ON_BAD_HR(renderHr = m_EncoderResult = m_OutputManager->RenderFrame(model));
		frameNr++;
		if (RecordingFrameNumberChangedCallback != nullptr && !m_IsDestructing) {
			INT64 timestamp = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
			RecordingFrameNumberChangedCallback(frameNr, timestamp);
		}
		havePrematureFrame = false;
		lastFrameStartPos100Nanos += duration100Nanos;
		return renderHr;
	});

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

		if (WaitForSingleObjectEx(ErrorEvent, 0, FALSE) == WAIT_OBJECT_0) {
			std::vector<CAPTURE_RESULT *> results;
			for each (CAPTURE_THREAD_DATA threadData in pCapture->GetCaptureThreadData())
			{
				results.push_back(threadData.ThreadResult);
			}
			for each (OVERLAY_THREAD_DATA threadData in pCapture->GetOverlayThreadData())
			{
				results.push_back(threadData.ThreadResult);
			}
			for each (CAPTURE_RESULT * result in results)
			{
				if (FAILED(result->RecordingResult)) {
					if (result->IsRecoverableError) {
						//Release texture created on the stale device
						if (pPreviousFrameCopy) {
							pPreviousFrameCopy.Release();
						}

						//Reinitialize and restart capture
						hr = pCapture->StopCapture();
						if (SUCCEEDED(hr)) {
							CleanDx(&m_DxResources);
							pCapture.reset(new ScreenCaptureManager());
							stopCaptureOnExit.Reset(pCapture.get());
							// As we have encountered an error due to a system transition we wait before trying again, using this dynamic wait
							// the wait periods will get progressively long to avoid wasting too much system resource if this state lasts a long time
							DynamicWait.Wait();

							hr = InitializeDx(nullptr, &m_DxResources);
						}
						if (SUCCEEDED(hr)) {
							SetViewPort(m_DxResources.Context, static_cast<float>(videoOutputFrameSize.cx), static_cast<float>(videoOutputFrameSize.cy));
							hr = m_TextureManager->Initialize(m_DxResources.Context, m_DxResources.Device);
						}
						if (SUCCEEDED(hr)) {
							hr = m_OutputManager->Initialize(m_DxResources.Context, m_DxResources.Device, GetEncoderOptions(), GetAudioOptions(), GetSnapshotOptions(), GetOutputOptions());
						}
						if (SUCCEEDED(hr)) {
							hr = pMouseManager->Initialize(m_DxResources.Context, m_DxResources.Device, GetMouseOptions());
						}
						if (SUCCEEDED(hr)) {
							hr = pCapture->Initialize(m_DxResources.Context, m_DxResources.Device, GetOutputOptions());
						}
						if (SUCCEEDED(hr)) {
							ResetEvent(ErrorEvent);
							hr = pCapture->StartCapture(sources, overlays, ErrorEvent);
						}
						pPtrInfo.reset();
						if (FAILED(hr)) {
							CAPTURE_RESULT captureResult{};
							ProcessCaptureHRESULT(hr, &captureResult, m_DxResources.Device);
							if (captureResult.IsRecoverableError) {
								SetEvent(ErrorEvent);
								LOG_INFO("Recoverable error while reinitializing capture, retrying..");
								continue;
							}
							else {
								LOG_ERROR("Fatal error while reinitializing capture, exiting..");
								return captureResult;
							}
						}
						continue;
					}
					else {
						return *result;
					}
				}
			}
		}
		if (m_IsPaused) {
			wait(10);
			previousSnapshotTaken = steady_clock::now();
			lastFrame = steady_clock::now();
			if (pAudioManager)
				pAudioManager->ClearRecordedBytes();
			continue;
		}
		CAPTURED_FRAME capturedFrame{};
		// Get new frame
		hr = pCapture->AcquireNextFrame(
			havePrematureFrame || GetEncoderOptions()->GetIsFixedFramerate() ? 0 : maxFrameLengthMillis,
			&capturedFrame);

		if (SUCCEEDED(hr)) {
			pCurrentFrameCopy.Attach(capturedFrame.Frame);
			if (capturedFrame.PtrInfo) {
				pPtrInfo = capturedFrame.PtrInfo.value();
			}
		}

		INT64 durationSinceLastFrame100Nanos = max(duration_cast<nanoseconds>(chrono::steady_clock::now() - lastFrame).count() / 100, 0);

		if ((recorderMode == RecorderModeInternal::Slideshow
			|| recorderMode == RecorderModeInternal::Screenshot)
		   && (!pCapture->IsInitialFrameWriteComplete() || !pCapture->IsInitialOverlayWriteComplete())
		   && durationSinceLastFrame100Nanos < max(videoFrameDuration100Nanos, m_MaxFrameLength100Nanos)) {
			continue;
		}
		else if (((!pCurrentFrameCopy && !pPreviousFrameCopy) || !pCapture->IsInitialFrameWriteComplete())
			&& durationSinceLastFrame100Nanos < max(videoFrameDuration100Nanos, m_MaxFrameLength100Nanos)) {
			//There is no first frame yet, so retry.
			wait(1);
			continue;
		}
		else if (durationSinceLastFrame100Nanos < videoFrameDuration100Nanos) {
			//attempt to wait if frame timeouted or duration is under our chosen framerate
			bool cacheCurrentFrame = false;
			INT64 delay100Nanos = 0;
			if (ShouldSkipDelay(capturedFrame)) {
				if (capturedFrame.PtrInfo) {
					capturedFrame.PtrInfo->IsPointerShapeUpdated = false;
				}
			}
			else if (SUCCEEDED(hr) && videoFrameDuration100Nanos > durationSinceLastFrame100Nanos) {
				if (pCurrentFrameCopy != nullptr && (capturedFrame.FrameUpdateCount > 0 || capturedFrame.OverlayUpdateCount > 0)) {
					cacheCurrentFrame = true;
				}
				delay100Nanos = max(0, videoFrameDuration100Nanos - durationSinceLastFrame100Nanos);
			}
			else if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
				if (GetEncoderOptions()->GetIsFixedFramerate() || recorderMode == RecorderModeInternal::Slideshow) {
					delay100Nanos = max(0, videoFrameDuration100Nanos - durationSinceLastFrame100Nanos);
				}
				else if (havePrematureFrame && videoFrameDuration100Nanos > durationSinceLastFrame100Nanos) {
					delay100Nanos = max(0, videoFrameDuration100Nanos - durationSinceLastFrame100Nanos);
				}
				else if (!havePrematureFrame && m_MaxFrameLength100Nanos > durationSinceLastFrame100Nanos) {
					delay100Nanos = max(0, m_MaxFrameLength100Nanos - durationSinceLastFrame100Nanos);
				}
			}
			if (delay100Nanos > minimumTimeForDelay100Nanons) {
				if (cacheCurrentFrame) {
					//we got a frame, but it's too soon, so we cache it and continue to see if there are more changes.
					if (pPreviousFrameCopy == nullptr) {
						D3D11_TEXTURE2D_DESC desc;
						pCurrentFrameCopy->GetDesc(&desc);
						RETURN_RESULT_ON_BAD_HR(hr = m_DxResources.Device->CreateTexture2D(&desc, nullptr, &pPreviousFrameCopy), L"Failed to create texture");
					}
					m_DxResources.Context->CopyResource(pPreviousFrameCopy, pCurrentFrameCopy);
					havePrematureFrame = true;
				}

				if (delay100Nanos > MillisToHundredNanos(1)) {
					//We recommend that you use Flush when the CPU waits for an arbitrary amount of time(such as when you call the Sleep function).
					//https://docs.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11devicecontext-flush
					m_DxResources.Context->Flush();
					std::this_thread::yield();
					Sleep(1);
				}
				else {
					std::this_thread::yield();
				}
				continue;
			}
		}

		if (hr != DXGI_ERROR_WAIT_TIMEOUT) {
			RETURN_RESULT_ON_BAD_HR(hr, L"");
		}

		if (!pCurrentFrameCopy && !pPreviousFrameCopy) {
			m_TextureManager->CreateTexture(videoOutputFrameSize.cx, videoOutputFrameSize.cy, &pCurrentFrameCopy, 0, D3D11_BIND_RENDER_TARGET| D3D11_BIND_SHADER_RESOURCE);
		}

		lastFrame = steady_clock::now();

		if (pCurrentFrameCopy) {
			if (pPreviousFrameCopy) {
				pPreviousFrameCopy.Release();
			}
			//Copy new frame to pPreviousFrameCopy
			if (recorderMode == RecorderModeInternal::Video || recorderMode == RecorderModeInternal::Slideshow) {
				D3D11_TEXTURE2D_DESC desc;
				pCurrentFrameCopy->GetDesc(&desc);
				RETURN_RESULT_ON_BAD_HR(hr = m_DxResources.Device->CreateTexture2D(&desc, nullptr, &pPreviousFrameCopy), L"");
				m_DxResources.Context->CopyResource(pPreviousFrameCopy, pCurrentFrameCopy);
			}
		}
		else if (pPreviousFrameCopy) {
			D3D11_TEXTURE2D_DESC desc;
			pPreviousFrameCopy->GetDesc(&desc);
			RETURN_RESULT_ON_BAD_HR(hr = m_DxResources.Device->CreateTexture2D(&desc, nullptr, &pCurrentFrameCopy), L"");
			m_DxResources.Context->CopyResource(pCurrentFrameCopy, pPreviousFrameCopy);
		}

		if (token.is_canceled()) {
			LOG_DEBUG("Recording task was cancelled");
			hr = S_OK;
			break;
		}
		if (frameNr == 0) {
			if (RecordingStatusChangedCallback != nullptr) {
				RecordingStatusChangedCallback(STATUS_RECORDING);
				LOG_DEBUG("Changed Recording Status to Recording");
			}
		}
		RETURN_RESULT_ON_BAD_HR(hr = PrepareAndRenderFrame(pCurrentFrameCopy, durationSinceLastFrame100Nanos), L"Failed to render frame");
		if (recorderMode == RecorderModeInternal::Screenshot) {
			break;
		}
	}

	//Push any last frame waiting to be recorded to the sink writer.
	if (pPreviousFrameCopy != nullptr) {
		INT64 duration = duration_cast<nanoseconds>(chrono::steady_clock::now() - lastFrame).count() / 100;
		RETURN_RESULT_ON_BAD_HR(hr = PrepareAndRenderFrame(pPreviousFrameCopy, duration), L"Failed to render frame");
	}
	return CAPTURE_RESULT(hr);
}

HRESULT RecordingManager::InitializeRects(_In_ SIZE captureFrameSize, _Out_opt_ RECT *pAdjustedSourceRect, _Out_opt_ SIZE *pAdjustedOutputFrameSize) {

	RECT adjustedSourceRect = RECT{ 0,0, MakeEven(captureFrameSize.cx), MakeEven(captureFrameSize.cy) };
	SIZE adjustedOutputFrameSize = SIZE{ MakeEven(captureFrameSize.cx), MakeEven(captureFrameSize.cy) };
	if (IsValidRect(GetOutputOptions()->GetSourceRectangle()))
	{
		adjustedSourceRect = GetOutputOptions()->GetSourceRectangle();
		adjustedOutputFrameSize = SIZE{ MakeEven(RectWidth(adjustedSourceRect)), MakeEven(RectHeight(adjustedSourceRect)) };
	}
	if (pAdjustedSourceRect) {
		*pAdjustedSourceRect = MakeRectEven(adjustedSourceRect);
	}
	if (pAdjustedOutputFrameSize) {
		auto outputRect = GetOutputOptions()->GetFrameSize();
		if (outputRect.cx > 0
		&& outputRect.cy > 0)
		{
			adjustedOutputFrameSize = SIZE{ MakeEven(outputRect.cx), MakeEven(outputRect.cy) };
		}
		*pAdjustedOutputFrameSize = adjustedOutputFrameSize;
	}
	return S_OK;
}

HRESULT RecordingManager::ProcessTextureTransforms(_In_ ID3D11Texture2D *pTexture, _Out_ ID3D11Texture2D **ppProcessedTexture, RECT videoInputFrameRect, SIZE videoOutputFrameSize)
{
	D3D11_TEXTURE2D_DESC desc;
	pTexture->GetDesc(&desc);
	HRESULT hr = S_FALSE;
	CComPtr<ID3D11Texture2D> pProcessedTexture = pTexture;
	if (RectWidth(videoInputFrameRect) < static_cast<long>(desc.Width)
		|| RectHeight(videoInputFrameRect) < static_cast<long>(round(desc.Height))) {
		ID3D11Texture2D *pCroppedFrameCopy;
		RETURN_ON_BAD_HR(hr = m_TextureManager->CropTexture(pTexture, videoInputFrameRect, &pCroppedFrameCopy));
		pProcessedTexture.Release();
		pProcessedTexture.Attach(pCroppedFrameCopy);
	}
	if (RectWidth(videoInputFrameRect) != videoOutputFrameSize.cx
		|| RectHeight(videoInputFrameRect) != videoOutputFrameSize.cy) {
		RECT contentRect;
		ID3D11Texture2D *pResizedFrameCopy;
		RETURN_ON_BAD_HR(hr = m_TextureManager->ResizeTexture(pProcessedTexture, videoOutputFrameSize, GetOutputOptions()->GetStretch(), &pResizedFrameCopy, &contentRect));

		pResizedFrameCopy->GetDesc(&desc);
		desc.Width = videoOutputFrameSize.cx;
		desc.Height = videoOutputFrameSize.cy;
		ID3D11Texture2D *pCanvas;
		RETURN_ON_BAD_HR(hr = m_DxResources.Device->CreateTexture2D(&desc, nullptr, &pCanvas));
		int leftMargin = (int)max(0, round(((double)videoOutputFrameSize.cx - (double)RectWidth(contentRect))) / 2);
		int topMargin = (int)max(0, round(((double)videoOutputFrameSize.cy - (double)RectHeight(contentRect))) / 2);

		D3D11_BOX Box;
		Box.front = 0;
		Box.back = 1;
		Box.left = 0;
		Box.top = 0;
		Box.right = RectWidth(contentRect);
		Box.bottom = RectHeight(contentRect);
		m_DxResources.Context->CopySubresourceRegion(pCanvas, 0, leftMargin, topMargin, 0, pResizedFrameCopy, 0, &Box);
		pResizedFrameCopy->Release();
		pProcessedTexture.Release();
		pProcessedTexture.Attach(pCanvas);
	}
	if (ppProcessedTexture) {
		*ppProcessedTexture = pProcessedTexture;
		(*ppProcessedTexture)->AddRef();
	}
	return hr;
}

bool RecordingManager::CheckDependencies(_Out_ std::wstring *error)
{
	wstring errorText;
	bool result = true;
	HKEY hk;
	DWORD errorCode;

	if (errorCode = RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Setup\\WindowsFeatures\\WindowsMediaVersion", 0, KEY_READ, &hk) != ERROR_SUCCESS) {
		errorText = L"Missing dependency: Windows Media Features.";
		result = false;
	}
	else {
		for each (auto * source in m_RecordingSources)
		{
			if (source->SourceApi.has_value() && source->SourceApi == RecordingSourceApi::DesktopDuplication && !IsWindows8OrGreater()) {
				errorText = L"Desktop Duplication requires Windows 8 or greater.";
				result = false;
				break;
			}
			else if (source->SourceApi.has_value() && source->SourceApi == RecordingSourceApi::WindowsGraphicsCapture && !Graphics::Capture::Util::IsGraphicsCaptureAvailable())
			{
				errorText = L"Windows Graphics Capture requires Windows 10 version 1903 or greater.";
				result = false;
				break;
			}
		}
	}
	*error = errorText;
	return result;
}


HRESULT RecordingManager::SaveTextureAsVideoSnapshot(_In_ ID3D11Texture2D *pTexture, _In_ RECT destRect)
{
	if (GetSnapshotOptions()->GetSnapshotsDirectory().empty())
		return S_FALSE;

	HRESULT hr = S_OK;
	CComPtr<ID3D11Texture2D> pProcessedTexture = nullptr;
	D3D11_TEXTURE2D_DESC frameDesc;
	pTexture->GetDesc(&frameDesc);
	int destWidth = RectWidth(destRect);
	int destHeight = RectHeight(destRect);
	if ((int)frameDesc.Width > RectWidth(destRect)
		|| (int)frameDesc.Height > RectHeight(destRect)) {
		//If the source frame is larger than the destionation rect, we crop it, to avoid black borders around the snapshots.
		RETURN_ON_BAD_HR(hr = m_TextureManager->CropTexture(pTexture, destRect, &pProcessedTexture));
	}
	else {
		RETURN_ON_BAD_HR(hr = m_DxResources.Device->CreateTexture2D(&frameDesc, nullptr, &pProcessedTexture));
		// Copy the current frame for a separate thread to write it to a file asynchronously.
		m_DxResources.Context->CopyResource(pProcessedTexture, pTexture);
	}

	wstring snapshotPath = GetSnapshotOptions()->GetSnapshotsDirectory() + L"\\" + s2ws(CurrentTimeToFormattedString()) + GetSnapshotOptions()->GetImageExtension();
	m_OutputManager->WriteTextureToImageAsync(pProcessedTexture, snapshotPath.c_str(), ([this, snapshotPath](HRESULT hr) {
		if (!m_TaskWrapperImpl->m_RecordTaskCts.get_token().is_canceled()) {
			bool success = SUCCEEDED(hr);
			if (success) {
				LOG_TRACE(L"Wrote snapshot to %s", snapshotPath.c_str());
				if (RecordingSnapshotCreatedCallback != nullptr) {
					RecordingSnapshotCreatedCallback(snapshotPath);
				}
			}
			else {
				_com_error err(hr);
				LOG_ERROR("Error saving snapshot: %s", err.ErrorMessage());
			}
		}
		}));
	return hr;
}