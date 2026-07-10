#define _CRTDBG_MAP_ALLOC
#include <crtdbg.h>
#include <ppltasks.h> 
#include <concrt.h>
#include <mfidl.h>
#include <VersionHelpers.h>
#include <filesystem>
#include <WinSDKVer.h>
#include "Util.h"
#include "MF.util.h"
#include "RecordingManager.h"
#include "TextureManager.h"
#include "ScreenCaptureManager.h"
#include "WindowsGraphicsCapture.util.h"
#include "Cleanup.h"
#include "Screengrab.h"
#include "DynamicWait.h"
#include "HighresTimer.h"
#include "TimelineManager.h"
#include "Concurrency.util.h"

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
#pragma comment(lib, "windowsapp.lib")

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
	std::atomic<bool> m_RecordTaskActive{ false };
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
	m_CaptureManager(nullptr),
	m_MouseManager(nullptr),
	m_AudioManager(nullptr),
	m_EncoderOptions(new H264_ENCODER_OPTIONS()),
	m_AudioOptions(new AUDIO_OPTIONS),
	m_MouseOptions(new MOUSE_OPTIONS),
	m_SnapshotOptions(new SNAPSHOT_OPTIONS),
	m_OutputOptions(new OUTPUT_OPTIONS),
	m_IsDestructing(false),
	m_RecordingSources{},
	m_DxResources{},
	m_FrameDataCallbackTexture(nullptr),
	m_TimerResolution(0),
	m_DynamicWait(nullptr),
	m_IsPaused(false),
	m_IsRecording(false),
	m_LastFrameHadAudio(false)
{
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
	m_MfStartupResult = MFStartup(MF_VERSION, MFSTARTUP_LITE);
	TIMECAPS tc;
	UINT targetResolutionMs = 1;
	if (timeGetDevCaps(&tc, sizeof(TIMECAPS)) == TIMERR_NOERROR)
	{
		m_TimerResolution = min(max(tc.wPeriodMin, targetResolutionMs), tc.wPeriodMax);
		timeBeginPeriod(m_TimerResolution);
	}
	RtlZeroMemory(&m_FrameDataCallbackTextureDesc, sizeof(m_FrameDataCallbackTextureDesc));
}

RecordingManager::~RecordingManager()
{
	if (m_TaskWrapperImpl->m_RecordTaskActive) {
		m_IsDestructing = true;
		LOG_WARN("Recording is in progress while destructing, cancelling recording task and waiting for completion.");
		m_TaskWrapperImpl->m_RecordTaskCts.cancel();
		m_TaskWrapperImpl->m_RecordTask.wait();
		LOG_DEBUG("Wait for recording task completed.");
	}

	if (m_TimerResolution > 0) {
		timeEndPeriod(m_TimerResolution);
	}
	SafeRelease(&m_FrameDataCallbackTexture);
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


HRESULT RecordingManager::ConfigureOutputDir(_In_ std::wstring path, _Out_ std::wstring *error) {
	m_OutputFullPath = path;
	*error = L"";
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
			*error = s2ws(ec.message());
			return E_FAIL;
		}

		if (recorderMode == RecorderModeInternal::Video || recorderMode == RecorderModeInternal::Screenshot) {
			wstring ext = recorderMode == RecorderModeInternal::Video ? GetEncoderOptions()->GetVideoExtension() : GetSnapshotOptions()->GetImageExtension();
			LPWSTR pStrExtension = PathFindExtension(path.c_str());
			if (pStrExtension == nullptr || pStrExtension[0] == 0)
			{
				m_OutputFullPath = m_OutputFolder + L"\\" + s2ws(CurrentTimeToFormattedString(true)) + ext;
			}
			if (GetSnapshotOptions()->GetSnapshotsDirectory().empty()) {
				// Snapshots will be saved in a folder named as video file name without extension. 
				GetSnapshotOptions()->SetSnapshotDirectory(m_OutputFullPath.substr(0, m_OutputFullPath.find_last_of(L".")));
			}
		}
	}

	return S_OK;
}

HRESULT RecordingManager::TakeSnapshot(_In_ std::wstring path)
{
	if (path.empty()) {
		if (!GetSnapshotOptions()->GetSnapshotsDirectory().empty())
		{
			path = GetSnapshotOptions()->GetSnapshotsDirectory() + L"\\" + s2ws(CurrentTimeToFormattedString(true)) + GetSnapshotOptions()->GetImageExtension();
		}
	}
	return TakeSnapshot(path, nullptr);
}

HRESULT RecordingManager::TakeSnapshot(_In_ IStream *stream)
{
	return TakeSnapshot(L"", stream);
}

HRESULT RecordingManager::TakeSnapshot(_In_opt_ std::wstring path, _In_opt_ IStream *stream, _In_opt_ ID3D11Texture2D *pTexture) {
	if (!m_IsRecording) {
		return E_NOT_VALID_STATE;
	}
	HRESULT hr = E_FAIL;
	CComPtr<ID3D11Texture2D> processedTexture;
	if (!pTexture) {
		CAPTURED_FRAME capturedFrame{};
		if (m_IsPaused) {
			hr = m_CaptureManager->AcquireNextFrame(0, m_MaxFrameLengthMillis, cancellation_token::none(), &capturedFrame);
			if (SUCCEEDED(hr)) {
				capturedFrame.Frame->AddRef();
			}
		}
		else {
			hr = m_CaptureManager->CopyCurrentFrame(&capturedFrame);
		}

		RETURN_ON_BAD_HR(hr);
		hr = ProcessTexture(capturedFrame.Frame, &processedTexture, capturedFrame.PtrInfo);
		SafeRelease(&capturedFrame.Frame);
	}
	else {
		processedTexture = pTexture;
	}
	RECT videoInputFrameRect{};
	RETURN_ON_BAD_HR(hr = InitializeRects(m_CaptureManager->GetOutputSize(), &videoInputFrameRect, nullptr));

	if (!path.empty()) {
		std::wstring directory = std::filesystem::path(path).parent_path().wstring();
		if (!std::filesystem::exists(directory))
		{
			std::error_code ec;
			if (std::filesystem::create_directories(directory, ec)) {
				LOG_DEBUG(L"Snapshot output folder created");
			}
			else {
				// Failed to create snapshot directory.
				LOG_ERROR(L"failed to create snapshot output folder");
				return E_FAIL;
			}
		}
		RETURN_ON_BAD_HR(hr = SaveTextureAsVideoSnapshot(processedTexture, path, videoInputFrameRect));
		LOG_TRACE(L"Wrote snapshot to %s", path.c_str());
		if (RecordingSnapshotCreatedCallback != nullptr) {
			RecordingSnapshotCreatedCallback(path);
		}
	}
	else if (stream) {
		RETURN_ON_BAD_HR(hr = SaveTextureAsVideoSnapshot(processedTexture, stream, videoInputFrameRect));
		LOG_TRACE(L"Wrote snapshot to stream");
		if (RecordingSnapshotCreatedCallback != nullptr) {
			RecordingSnapshotCreatedCallback(L"");
		}
	}
	else {
		LOG_ERROR("Snapshot failed: No valid stream or path provided.");
		hr = E_INVALIDARG;
	}
	m_TimelineManager->UpdateLastSnapshotTime();
	return hr;
}

HRESULT RecordingManager::BeginRecording(_In_ IStream *stream) {
	return BeginRecording(L"", stream);
}

HRESULT RecordingManager::BeginRecording(_In_ std::wstring path) {
	return BeginRecording(path, nullptr);
}

HRESULT RecordingManager::BeginRecording(_In_opt_ std::wstring path, _In_opt_ IStream *stream) {
	if (m_IsRecording.exchange(true)) {
		if (m_IsPaused) {
			ResumeRecording();
		}
		else {
			std::wstring error = L"Recording is already in progress, aborting";
			LOG_WARN("%ls", error.c_str());
			SetRecordingCompleteStatus(REC_RESULT(E_FAIL, error));
		}
		return S_FALSE;
	}
	if (m_TaskWrapperImpl->m_RecordTaskActive.exchange(true))
	{
		auto guarded = cancel_after_timeout(m_TaskWrapperImpl->m_RecordTask, m_TaskWrapperImpl->m_RecordTaskCts, 1000 /* ms */);
		try {
			guarded.get();
		}
		catch (const task_canceled &) {
			// timed out
			return E_FAIL;
		}
	}

	HRESULT hr;
	wstring errorText;
	if (!CheckDependencies(&errorText)) {
		LOG_ERROR(L"%ls", errorText);
		SetRecordingCompleteStatus(REC_RESULT(E_FAIL, errorText));
		return E_FAIL;
	}
	if (FAILED(hr = ConfigureOutputDir(path, &errorText))) {
		if (RecordingFailedCallback != nullptr)
			SetRecordingCompleteStatus(REC_RESULT(E_FAIL, errorText));
	}

	if (m_RecordingSources.size() == 0) {
		std::wstring error = L"No valid recording sources found in recorder parameters.";
		LOG_ERROR("%ls", error.c_str());
		SetRecordingCompleteStatus(REC_RESULT(E_FAIL, error));
		return E_FAIL;
	}
	m_AudioManager = make_unique<AudioManager>();

	m_EncoderResult = S_FALSE;
	m_TaskWrapperImpl->m_RecordTaskCts = cancellation_token_source();
	m_TaskWrapperImpl->m_RecordTask = concurrency::create_task([this, stream]() {
		LOG_INFO(L"Starting recording task");
		REC_RESULT result{};
		HRESULT hr = CoInitializeEx(nullptr, COINITBASE_MULTITHREADED | COINIT_DISABLE_OLE1DDE);
		RETURN_RESULT_ON_BAD_HR(hr, L"CoInitializeEx failed");
		CoUninitializeOnExit coUninitialize;
		RETURN_RESULT_ON_BAD_HR(hr = InitializeDx(nullptr, &m_DxResources), L"Failed to initialize DirectX");

		RecorderModeInternal recorderMode = GetOutputOptions()->GetRecorderMode();
		double targetVideoFrameDurationMillis = 0;
		if (recorderMode == RecorderModeInternal::Video) {
			targetVideoFrameDurationMillis = (double)1000 / GetEncoderOptions()->GetVideoFps();
		}
		else if (recorderMode == RecorderModeInternal::Slideshow) {
			targetVideoFrameDurationMillis = (double)GetSnapshotOptions()->GetSnapshotsInterval();
		}

		m_TextureManager = make_unique<TextureManager>();
		RETURN_RESULT_ON_BAD_HR(hr = m_TextureManager->Initialize(m_DxResources.Context, m_DxResources.Device), L"Failed to initialize TextureManager");
		m_TimelineManager = make_unique<TimelineManager>();
		RETURN_RESULT_ON_BAD_HR(m_TimelineManager->Initialize(targetVideoFrameDurationMillis, GetSnapshotOptions()->GetSnapshotsInterval()), L"Failed to initialize TimelineManager");
		m_OutputManager = make_unique<OutputManager>();
		RETURN_RESULT_ON_BAD_HR(hr = m_OutputManager->Initialize(m_DxResources.Context, m_DxResources.Device, m_TimelineManager.get(), GetEncoderOptions(), GetAudioOptions(), GetSnapshotOptions(), GetOutputOptions()), L"Failed to initialize OutputManager");
		m_CaptureManager = make_unique<ScreenCaptureManager>();
		RETURN_RESULT_ON_BAD_HR(m_CaptureManager->Initialize(m_DxResources.Context, m_DxResources.Device, GetOutputOptions(), GetEncoderOptions(), GetMouseOptions()), L"Failed to initialize ScreenCaptureManager");
		m_MouseManager = make_unique<MouseManager>();
		RETURN_RESULT_ON_BAD_HR(hr = m_MouseManager->Initialize(m_DxResources.Context, m_DxResources.Device, GetMouseOptions()), L"Failed to initialize MouseManager");
		m_DynamicWait = make_unique<DynamicWait>();
		RETURN_RESULT_ON_BAD_HR(hr = m_AudioManager->Initialize(GetAudioOptions(), m_TimelineManager.get()), L"Failed to initialize AudioManager");
		result = StartRecorderLoop(m_RecordingSources, m_Overlays, stream);
		if (RecordingStatusChangedCallback != nullptr && !m_IsDestructing) {
			RecordingStatusChangedCallback(STATUS_FINALIZING);
		}
		result.FinalizeResult = m_OutputManager->FinalizeRecording();

		LOG_INFO("Exiting recording task");
		return result;
		}).then([this](concurrency::task<REC_RESULT> t)
				{
					m_CaptureManager.reset(nullptr);
					m_MouseManager.reset(nullptr);
					m_AudioManager.reset(nullptr);
					m_TextureManager.reset(nullptr);
					m_IsRecording = false;
					m_IsPaused = false;
					REC_RESULT result{ };
					try {
						result = t.get();
						// if .get() didn't throw and the HRESULT succeeded, there are no errors.
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
					m_TaskWrapperImpl->m_RecordTaskActive = false;
				});
		return S_OK;
}

void RecordingManager::EndRecording() {
	if (m_IsRecording.exchange(false)) {
		m_TaskWrapperImpl->m_RecordTaskCts.cancel();
		LOG_DEBUG(L"Stopped recording task");
	}
}
void RecordingManager::PauseRecording() {
	if (m_IsRecording && !m_IsPaused.exchange(true)) {
		if (m_TimelineManager) {
			m_TimelineManager->PauseMediaClock();
		}
		if (m_AudioManager) {
			m_AudioManager->PauseCapture();
		}
		if (RecordingStatusChangedCallback != nullptr) {
			RecordingStatusChangedCallback(STATUS_PAUSED);
			LOG_DEBUG("Changed Recording Status to Paused");
		}
	}
}
void RecordingManager::ResumeRecording() {
	if (m_IsRecording && m_IsPaused.exchange(false)) {
		if (m_AudioManager) {
			m_AudioManager->ResumeCapture();
		}
		if (m_TimelineManager) {
			m_TimelineManager->ResumeMediaClock();
		}
		if (m_CaptureManager) {
			m_CaptureManager->InvalidateCaptureSources();
		}
		if (RecordingStatusChangedCallback != nullptr) {
			RecordingStatusChangedCallback(STATUS_RECORDING);
			LOG_DEBUG("Changed Recording Status to Recording");
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
		const std::lock_guard<std::mutex> lock(m_DxDebugMutex);
		m_DxResources.Debug->ReportLiveDeviceObjects(D3D11_RLDO_DETAIL | D3D11_RLDO_IGNORE_INTERNAL);
		SafeRelease(&m_DxResources.Debug);
	}
#endif
}

void RecordingManager::SetRecordingCompleteStatus(_In_ REC_RESULT result, _In_ std::optional<nlohmann::fifo_map<std::wstring, int>> frameDelays)
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
			RecordingCompleteCallback(m_OutputFullPath, frameDelays.value_or(nlohmann::fifo_map<std::wstring, int>()));
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
	std::optional<PTR_INFO> pPtrInfo = std::nullopt;
	HRESULT hr = S_OK;
	auto recorderMode = GetOutputOptions()->GetRecorderMode();

	// Event for when a thread encounters an error
	HANDLE ErrorEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
	if (nullptr == ErrorEvent) {
		LOG_ERROR(L"CreateEvent failed: last error is %u", GetLastError());
		return CAPTURE_RESULT(E_FAIL, L"Failed to create event");
	}
	CloseHandleOnExit closeExpectedErrorEvent(ErrorEvent);

	RETURN_RESULT_ON_BAD_HR(hr = m_CaptureManager->StartCapture(sources, overlays, ErrorEvent), L"Failed to start capture");

	RECT videoInputFrameRect{};
	SIZE videoOutputFrameSize{};
	RETURN_RESULT_ON_BAD_HR(hr = InitializeRects(
		m_CaptureManager->GetOutputSize(),
		&videoInputFrameRect,
		&videoOutputFrameSize), L"Failed to initialize frame rects");

	SetViewPort(m_DxResources.Context, static_cast<float>(videoOutputFrameSize.cx), static_cast<float>(videoOutputFrameSize.cy));

	if (recorderMode == RecorderModeInternal::Video) {
		m_AudioManager->StartCapture();
		m_AudioManager->PauseCapture();
	}
	if (pStream) {
		RETURN_RESULT_ON_BAD_HR(hr = m_OutputManager->BeginRecording(pStream, videoOutputFrameSize), L"Failed to initialize video sink writer");
	}
	else {
		RETURN_RESULT_ON_BAD_HR(hr = m_OutputManager->BeginRecording(m_OutputFullPath, videoOutputFrameSize), L"Failed to initialize video sink writer");
	}
	m_AudioManager->ResumeCapture();

	cancellation_token token = m_TaskWrapperImpl->m_RecordTaskCts.get_token();

	while (true)
	{
		if (token.is_canceled()) {
			LOG_DEBUG("Recording task was cancelled");
			hr = S_OK;
			break;
		}

		if (WaitForSingleObjectEx(ErrorEvent, 0, FALSE) == WAIT_OBJECT_0) {
			std::vector<CAPTURE_THREAD_DATA> captureData = m_CaptureManager->GetCaptureThreadData();
			if (captureData.size() > 0
				&& std::all_of(captureData.begin(), captureData.end(), [&](const CAPTURE_THREAD_DATA obj)
					{
						return FAILED(obj.ThreadResult->RecordingResult) && !obj.ThreadResult->IsRecoverableError;
					})) {
				return m_CaptureManager->GetCaptureResults().at(0)->RecordingResult;
			}

			std::vector<CAPTURE_RESULT *> results = m_CaptureManager->GetCaptureResults();
			auto firstRecoverableError = find_if(results.begin(), results.end(), [&](const CAPTURE_RESULT *obj)
				{
					return FAILED(obj->RecordingResult) && obj->IsRecoverableError;
				});

			if (firstRecoverableError != results.end()) {
				CAPTURE_RESULT *captureResult = *(firstRecoverableError);
				if (captureResult->NumberOfRetries >= 0 && m_RestartCaptureCount >= captureResult->NumberOfRetries) {
					RETURN_RESULT_ON_BAD_HR(captureResult->RecordingResult, L"Retry count was exceeded, exiting");
				}
				hr = RestartCapture(*captureResult, sources, overlays, ErrorEvent, &videoInputFrameRect);
			}
			else if (FAILED(hr)) {
				CAPTURE_RESULT captureResult{};
				ProcessCaptureHRESULT(hr, &captureResult, m_DxResources.Device);
				if (captureResult.IsRecoverableError) {
					if (captureResult.NumberOfRetries >= 0 && m_RestartCaptureCount >= captureResult.NumberOfRetries) {
						RETURN_RESULT_ON_BAD_HR(hr, L"Retry count was exceeded, exiting");
					}
					hr = RestartCapture(captureResult, sources, overlays, ErrorEvent, &videoInputFrameRect);
				}
				else {
					LOG_ERROR("Fatal error while reinitializing capture, exiting.");
					return captureResult;
				}
			}
			pPtrInfo.reset();
			if (SUCCEEDED(hr)) {
				SetViewPort(m_DxResources.Context, static_cast<float>(videoOutputFrameSize.cx), static_cast<float>(videoOutputFrameSize.cy));
				ResetEvent(ErrorEvent);
			}
			else {
				SetEvent(ErrorEvent);
				continue;
			}
		}
		if (m_IsPaused) {
			if (m_TimelineManager->isMediaClockRunning()) {
				m_TimelineManager->PauseMediaClock();
			}
			if (m_AudioManager) {
				m_AudioManager->PauseCapture();
			}
			ExecuteFuncOnExit clearDataOnExit([&]() {
				m_TimelineManager->UpdateLastSnapshotTime();
				if (m_AudioManager)
					m_AudioManager->ClearRecordedBytes();
			});
			if (!IsAnySourcePreviewsActive()) {
				wait(10);
				continue;
			}
		}
		CAPTURED_FRAME capturedFrame{};
		// Get new frame
		hr = m_CaptureManager->AcquireNextFrame(m_TimelineManager->GetTimeUntilNextFrameMillis(), m_MaxFrameLengthMillis, token, &capturedFrame);

		//If there are any source previews on paused status, the loop exits here. This allows the source previews to continue rendering.
		if (m_IsPaused) {
			wait(static_cast<UINT32>(round(m_TimelineManager->GetTargetVideoFrameDurationMillis())));
			continue;
		}
		if (SUCCEEDED(hr)) {
			if (capturedFrame.FrameUpdateCount > 0) {
				m_RestartCaptureCount = 0;
			}
			if (capturedFrame.PtrInfo) {
				pPtrInfo = capturedFrame.PtrInfo.value();
			}
		}
		else if (hr != DXGI_ERROR_WAIT_TIMEOUT) {
			RETURN_RESULT_ON_BAD_HR(hr, L"");
		}

		if (token.is_canceled()) {
			LOG_DEBUG("Recording task was cancelled");
			hr = S_OK;
			break;
		}
		if (m_TimelineManager->GetRenderedVideoFrameCount() == 0) {
			if (RecordingStatusChangedCallback != nullptr) {
				RecordingStatusChangedCallback(STATUS_RECORDING);
				LOG_DEBUG("Changed Recording Status to Recording");
			}
		}

		RETURN_RESULT_ON_BAD_HR(hr = PrepareAndRenderFrame(capturedFrame.Frame, pPtrInfo), L"Failed to render frame");
		if (recorderMode == RecorderModeInternal::Screenshot) {
			break;
		}
	}

	return CAPTURE_RESULT(hr);
}

HRESULT RecordingManager::PrepareAndRenderFrame(_In_ CComPtr<ID3D11Texture2D> pTextureToRender, _In_opt_ std::optional<PTR_INFO> pointerInfo)
{
	const INT64 nextVideoFrameStartPos100Nanos = m_TimelineManager->GetNextVideoFrameStartPosition();
	const INT64 nextVideoFrameDuration100Nanos = m_TimelineManager->OnVideoFrame();


	CComPtr<ID3D11Texture2D> processedTexture;
	HRESULT hr = ProcessTexture(pTextureToRender, &processedTexture, pointerInfo);
	if (hr == S_OK) {
		pTextureToRender.Release();
		pTextureToRender.Attach(processedTexture);
		(*pTextureToRender).AddRef();
	}
	if (GetOutputOptions()->GetRecorderMode() == RecorderModeInternal::Video) {
		if (GetSnapshotOptions()->IsSnapshotWithVideoEnabled() && m_TimelineManager->GetTimeUntilNextShapshot100Nanos() <= 0) {
			if (GetSnapshotOptions()->GetSnapshotsDirectory().empty())
				return S_FALSE;
			wstring snapshotPath = GetSnapshotOptions()->GetSnapshotsDirectory() + L"\\" + s2ws(CurrentTimeToFormattedString(true)) + GetSnapshotOptions()->GetImageExtension();
			TakeSnapshot(snapshotPath, nullptr, pTextureToRender);
		}
	}
	std::unique_ptr<FRAME_AUDIO_DATA> audioPacket(m_AudioManager->GrabAudioSamples());

	size_t unpaddedAudioSize = audioPacket->Data.size();
	bool paddedAudio = PadAudio(audioPacket->Data, nextVideoFrameStartPos100Nanos, nextVideoFrameDuration100Nanos);

	const INT64 nextAudioPacketStartPos100Nanos = m_TimelineManager->GetNextAudioFrameStartPosition();
	const int audioFrameCount = static_cast<int>(audioPacket->Data.size()) / ((GetAudioOptions()->GetAudioBitsPerSample() / 8) * GetAudioOptions()->GetAudioChannels());
	const INT64 nextAudioPacketDuration100Nanos = m_TimelineManager->OnAudioPacket(audioFrameCount, GetAudioOptions()->GetAudioSamplesPerSecond());

	FrameWriteModel model{};
	model.Frame = std::move(pTextureToRender);
	model.AudioQpcPosition = audioPacket->QpcTimestamp;
	model.Audio = std::move(audioPacket->Data);
	model.VideoStartPos = nextVideoFrameStartPos100Nanos;
	model.VideoDuration = nextVideoFrameDuration100Nanos;
	model.AudioStartPos = nextAudioPacketStartPos100Nanos;
	model.AudioDuration = nextAudioPacketDuration100Nanos;
	if (paddedAudio) {
		model.PaddedBytes = static_cast<int>(model.Audio.size() - unpaddedAudioSize);
	}


	RETURN_ON_BAD_HR(hr = m_EncoderResult = m_OutputManager->RenderFrame(model));
	if (RecordingFrameNumberChangedCallback != nullptr && !m_IsDestructing) {
		SendNewFrameCallback(m_TimelineManager->GetRenderedVideoFrameCount(), model.Frame, audioPacket->Info.get());
	}
	return hr;
}

HRESULT RecordingManager::RestartCapture(_In_ CAPTURE_RESULT &result, _In_ const std::vector<RECORDING_SOURCE *> &sources, _In_ const std::vector<RECORDING_OVERLAY *> &overlays, _In_  HANDLE hErrorEvent, _Out_opt_ RECT *videoInputFrameRect) {
	HRESULT hr = m_CaptureManager->StopCapture();

	// As we have encountered an error due to a system transition we wait before trying again, using this dynamic wait
	// the wait periods will get progressively long to avoid wasting too much system resource if this state lasts a long time
	m_DynamicWait->Wait();

	//Recreate D3D resources if needed
	if (SUCCEEDED(hr) && result.IsDeviceError) {
		CleanDx(&m_DxResources);
		hr = InitializeDx(nullptr, &m_DxResources);

		if (SUCCEEDED(hr)) {
			hr = m_MouseManager->Initialize(m_DxResources.Context, m_DxResources.Device, GetMouseOptions());
		}
		if (SUCCEEDED(hr)) {
			hr = m_TextureManager->Initialize(m_DxResources.Context, m_DxResources.Device);
		}
		if (SUCCEEDED(hr)) {
			hr = m_OutputManager->Initialize(
				m_DxResources.Context,
				m_DxResources.Device,
				m_TimelineManager.get(),
				GetEncoderOptions(),
				GetAudioOptions(),
				GetSnapshotOptions(),
				GetOutputOptions());
		}
	}
	//Recreate capture manager and restart capture
	if (SUCCEEDED(hr)) {
		m_CaptureManager.reset(new ScreenCaptureManager());
	}
	if (SUCCEEDED(hr)) {
		hr = m_CaptureManager->Initialize(
			m_DxResources.Context,
			m_DxResources.Device,
			GetOutputOptions(),
			GetEncoderOptions(),
			GetMouseOptions());
	}
	if (SUCCEEDED(hr)) {
		if (result.NumberOfRetries > 0) {
			m_RestartCaptureCount++;
		}
		ResetEvent(hErrorEvent);
		hr = m_CaptureManager->StartCapture(sources, overlays, hErrorEvent);
	}
	if (SUCCEEDED(hr)) {
		//The source dimensions may have changed
		hr = InitializeRects(m_CaptureManager->GetOutputSize(), videoInputFrameRect, nullptr);
		if (SUCCEEDED(hr)) {
			LOG_TRACE(L"Reinitialized input frame rect: [%d,%d,%d,%d]", videoInputFrameRect->left, videoInputFrameRect->top, videoInputFrameRect->right, videoInputFrameRect->bottom);
		}
	}

	return hr;
}

bool RecordingManager::IsAnySourcePreviewsActive()
{
	for each (RECORDING_SOURCE * source in GetRecordingSources())
	{
		if (source->IsVideoFramePreviewEnabled.value_or(false) && source->HasRegisteredCallbacks()) {
			return true;
		}
	}
	return false;
}


bool RecordingManager::PadAudio(_Inout_ std::vector<BYTE> &audioData, _In_ INT64 nextVideoFramePos, _In_ INT64 nextVideoFrameDuration)
{
	bool paddedAudio = false;
	/* If the audio pCaptureInstance returns no data, i.e. the source is silent, we need to pad the PCM stream with zeros to give the media sink silence as input.
	 * If we don't, the sink writer will begin throttling video frames because it expects audio samples to be delivered, and think they are delayed.
	 * We ignore every instance where the last frame had audio, due to sometimes very short frame durations due to mouse cursor changes have zero audio length,
	 * and inserting silence between two frames that has audio leads to glitching. */
	if (GetOutputOptions()->GetRecorderMode() == RecorderModeInternal::Video
		&& GetAudioOptions()->IsAudioEnabled()
		&& audioData.size() == 0
		&& nextVideoFrameDuration > 0) {
		if (!m_LastFrameHadAudio || HundredNanosToMillisDouble(nextVideoFrameDuration) > 5) {
			INT64 expectedAudioFrames = ((nextVideoFramePos + nextVideoFrameDuration) * GetAudioOptions()->GetAudioSamplesPerSecond()) / 10000000ULL;
			INT64 renderedAudioFrames = m_TimelineManager->GetRenderedAudioFrameCount();
			int frameCount = static_cast<int>(max(0, expectedAudioFrames - renderedAudioFrames));
			int byteCount = frameCount * (GetAudioOptions()->GetAudioBitsPerSample() / 8) * GetAudioOptions()->GetAudioChannels();
			audioData.insert(audioData.end(), byteCount, 0);
			paddedAudio = true;
		}
		m_LastFrameHadAudio = false;
	}
	else {
		m_LastFrameHadAudio = true;
	}
	return paddedAudio;
}

HRESULT RecordingManager::SendNewFrameCallback(_In_ const int frameNumber, _In_ ID3D11Texture2D *pTexture, _In_opt_ FRAME_AUDIO_INFO *audioData) {
	HRESULT hr = S_FALSE;
	if (RecordingFrameNumberChangedCallback != nullptr) {
		INT64 timestamp = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
		if (GetOutputOptions()->IsVideoFramePreviewEnabled()) {
			CComPtr< ID3D11Texture2D> pProcessedTexture = nullptr;
			unique_ptr<FRAME_BITMAP_DATA> pFramePreviewData = nullptr;
			D3D11_TEXTURE2D_DESC textureDesc;
			pTexture->GetDesc(&textureDesc);
			if (GetOutputOptions()->GetVideoFramePreviewSize().has_value()) {
				long cx = GetOutputOptions()->GetVideoFramePreviewSize().value().cx;
				long cy = GetOutputOptions()->GetVideoFramePreviewSize().value().cy;
				if (cx > 0 && cy == 0) {
					cy = static_cast<long>(round((static_cast<double>(textureDesc.Height) / static_cast<double>(textureDesc.Width)) * cx));
				}
				else if (cx == 0 && cy > 0) {
					cx = static_cast<long>(round((static_cast<double>(textureDesc.Width) / static_cast<double>(textureDesc.Height)) * cy));
				}
				ID3D11Texture2D *pResizedTexture;
				RETURN_ON_BAD_HR(hr = m_TextureManager->ResizeTexture(pTexture, SIZE{ cx,cy }, TextureStretchMode::Uniform, &pResizedTexture));
				pProcessedTexture.Attach(pResizedTexture);
				pResizedTexture->GetDesc(&textureDesc);
			}
			else {
				pProcessedTexture.Attach(pTexture);
				pTexture->AddRef();
			}
			int width = textureDesc.Width;
			int height = textureDesc.Height;

			if (m_FrameDataCallbackTextureDesc.Width != width || m_FrameDataCallbackTextureDesc.Height != height) {
				SafeRelease(&m_FrameDataCallbackTexture);
				textureDesc.Usage = D3D11_USAGE_STAGING;
				textureDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
				textureDesc.MiscFlags = 0;
				textureDesc.BindFlags = 0;
				RETURN_ON_BAD_HR(m_DxResources.Device->CreateTexture2D(&textureDesc, nullptr, &m_FrameDataCallbackTexture));

				m_FrameDataCallbackTextureDesc = textureDesc;
			}

			m_DxResources.Context->CopyResource(m_FrameDataCallbackTexture, pProcessedTexture);
			D3D11_MAPPED_SUBRESOURCE map;
			m_DxResources.Context->Map(m_FrameDataCallbackTexture, 0, D3D11_MAP_READ, 0, &map);

			int bytesPerPixel = map.RowPitch / width;
			int len = map.DepthPitch;
			int stride = map.RowPitch;
			BYTE *data = static_cast<BYTE *>(map.pData);
			pFramePreviewData = make_unique<FRAME_BITMAP_DATA>();
			pFramePreviewData->Data = data;
			pFramePreviewData->Stride = stride;
			pFramePreviewData->Width = width;
			pFramePreviewData->Height = height;
			pFramePreviewData->Length = len;
			RecordingFrameNumberChangedCallback(frameNumber, timestamp, pFramePreviewData.get(), audioData);
			m_DxResources.Context->Unmap(m_FrameDataCallbackTexture, 0);
		}
		else {
			RecordingFrameNumberChangedCallback(frameNumber, timestamp, nullptr, audioData);
		}
	}
	return hr;
}

HRESULT RecordingManager::InitializeRects(_In_ SIZE captureFrameSize, _Out_opt_ RECT *pAdjustedSourceRect, _Out_opt_ SIZE *pAdjustedOutputFrameSize) {

	RECT adjustedSourceRect = RECT{ 0,0, MakeEven(captureFrameSize.cx), MakeEven(captureFrameSize.cy) };
	SIZE adjustedOutputFrameSize = SIZE{ MakeEven(captureFrameSize.cx), MakeEven(captureFrameSize.cy) };
	if (GetOutputOptions()->GetSourceRectangle().has_value() && IsValidRect(GetOutputOptions()->GetSourceRectangle().value()))
	{
		adjustedSourceRect = GetOutputOptions()->GetSourceRectangle().value();
		adjustedOutputFrameSize = SIZE{ MakeEven(RectWidth(adjustedSourceRect)), MakeEven(RectHeight(adjustedSourceRect)) };
	}
	if (pAdjustedSourceRect) {
		*pAdjustedSourceRect = MakeRectEven(adjustedSourceRect);
	}
	if (pAdjustedOutputFrameSize) {
		auto outputRect = GetOutputOptions()->GetFrameSize().value_or(SIZE{});
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

		D3D11_BOX Box{};
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

bool RecordingManager::CheckDependencies(_Out_ std::wstring *error) const
{
	wstring errorText;
	bool result = true;

	if (FAILED(m_MfStartupResult)) {
		LOG_ERROR("Media Foundation failed to start: hr = 0x%08x", m_MfStartupResult);
		errorText = L"Failed to start Media Foundation.";
		result = false;
	}
	else {
		for each (auto *source in m_RecordingSources)
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
void RecordingManager::SaveTextureAsVideoSnapshotAsync(_In_ ID3D11Texture2D *pTexture, _In_ std::wstring snapshotPath, _In_ RECT destRect, _In_opt_ std::function<void(HRESULT)> onCompletion)
{
	pTexture->AddRef();
	auto token = m_TaskWrapperImpl->m_RecordTaskCts.get_token();
	Concurrency::create_task([this, pTexture, snapshotPath, destRect, onCompletion, token]() {
		return SaveTextureAsVideoSnapshot(pTexture, snapshotPath, destRect);
	   }, token).then([this, snapshotPath, pTexture, destRect, onCompletion, token](concurrency::task<HRESULT> t)
		   {
			   HRESULT hr;
			   try {
				   hr = t.get();
				   // if .get() didn't throw and the HRESULT succeeded, there are no errors.
			   }
			   catch (...) {
				   // handle error
				   LOG_ERROR(L"Exception saving snapshot", );
				   hr = E_FAIL;
			   }
			   if (token.is_canceled()) {
				   cancel_current_task();
			   };
			   if (onCompletion) {
				   std::invoke(onCompletion, hr);
			   }
			   return hr;
		   }, token);
}

HRESULT RecordingManager::SaveTextureAsVideoSnapshot(_In_ ID3D11Texture2D *pTexture, _In_ std::wstring snapshotPath, _In_ RECT destRect)
{
	CComPtr<ID3D11Texture2D> pProcessedTexture = nullptr;
	D3D11_TEXTURE2D_DESC frameDesc;
	pTexture->GetDesc(&frameDesc);
	int destWidth = RectWidth(destRect);
	int destHeight = RectHeight(destRect);
	if ((int)frameDesc.Width > RectWidth(destRect)
		|| (int)frameDesc.Height > RectHeight(destRect)) {
		//If the source frame is larger than the destionation rect, we crop it, to avoid black borders around the snapshots.
		RETURN_ON_BAD_HR(m_TextureManager->CropTexture(pTexture, destRect, &pProcessedTexture));
	}
	else {
		RETURN_ON_BAD_HR(m_DxResources.Device->CreateTexture2D(&frameDesc, nullptr, &pProcessedTexture));
		// Copy the current frame for a separate thread to write it to a file asynchronously.
		m_DxResources.Context->CopyResource(pProcessedTexture, pTexture);
	}
	return m_OutputManager->WriteFrameToImage(pProcessedTexture, snapshotPath.c_str());
}

HRESULT RecordingManager::SaveTextureAsVideoSnapshot(_In_ ID3D11Texture2D *pTexture, _In_ IStream *pStream, _In_ RECT destRect)
{
	CComPtr<ID3D11Texture2D> pProcessedTexture = nullptr;
	D3D11_TEXTURE2D_DESC frameDesc;
	pTexture->GetDesc(&frameDesc);
	int destWidth = RectWidth(destRect);
	int destHeight = RectHeight(destRect);
	if ((int)frameDesc.Width > RectWidth(destRect)
		|| (int)frameDesc.Height > RectHeight(destRect)) {
		//If the source frame is larger than the destionation rect, we crop it, to avoid black borders around the snapshots.
		RETURN_ON_BAD_HR(m_TextureManager->CropTexture(pTexture, destRect, &pProcessedTexture));
	}
	else {
		RETURN_ON_BAD_HR(m_DxResources.Device->CreateTexture2D(&frameDesc, nullptr, &pProcessedTexture));
		// Copy the current frame for a separate thread to write it to a file asynchronously.
		m_DxResources.Context->CopyResource(pProcessedTexture, pTexture);
	}
	return m_OutputManager->WriteFrameToImage(pProcessedTexture, pStream);
}

HRESULT RecordingManager::ProcessTexture(_In_ ID3D11Texture2D *pTexture, _Out_ ID3D11Texture2D **ppProcessedTexture, _In_opt_ std::optional<PTR_INFO> pPtrInfo = std::nullopt)
{
	*ppProcessedTexture = nullptr;
	HRESULT hr = E_FAIL;
	int updatedOverlaysCount = 0;
	m_CaptureManager->ProcessOverlays(pTexture, &updatedOverlaysCount);
	if (pPtrInfo) {
		hr = m_MouseManager->ProcessMousePointer(pTexture, &pPtrInfo.value());
		if (FAILED(hr)) {
			_com_error err(hr);
			LOG_ERROR(L"Error drawing mouse pointer: %s", err.ErrorMessage());
			//We just log the error and continue if the mouse pointer failed to draw. If there is an error with DXGI, it will be handled on the next call to AcquireNextFrame.
		}
	}
	SIZE videoOutputFrameSize{};
	RECT videoInputFrameRect{};
	RETURN_ON_BAD_HR(hr = InitializeRects(m_CaptureManager->GetOutputSize(), &videoInputFrameRect, &videoOutputFrameSize));
	CComPtr<ID3D11Texture2D> processedTexture;
	RETURN_ON_BAD_HR(hr = ProcessTextureTransforms(pTexture, &processedTexture, videoInputFrameRect, videoOutputFrameSize));

	*ppProcessedTexture = processedTexture;
	(*ppProcessedTexture)->AddRef();

	return hr;
}
