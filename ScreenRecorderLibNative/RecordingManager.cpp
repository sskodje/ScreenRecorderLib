#include <ppltasks.h> 
#include <concrt.h>
#include <mfidl.h>
#include <VersionHelpers.h>
#include <filesystem>
#include "LoopbackCapture.h"
#include "RecordingManager.h"
#include "Cleanup.h"
#include "Screengrab.h"
#include "TextureManager.h"

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
	m_TextureManager(nullptr),
	m_EncoderOptions(new H264_ENCODER_OPTIONS()),
	m_AudioOptions(new AUDIO_OPTIONS),
	m_MouseOptions(new MOUSE_OPTIONS),
	m_SnapshotOptions(new SNAPSHOT_OPTIONS)
{
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
}

RecordingManager::~RecordingManager()
{
	if (!m_TaskWrapperImpl->m_RecordTask.is_done()) {
		LOG_WARN("Recording is in progress while destructing, cancelling recording task and waiting for completion.");
		m_TaskWrapperImpl->m_RecordTaskCts.cancel();
		m_TaskWrapperImpl->m_RecordTask.wait();
		LOG_DEBUG("Wait for recording task completed.");
	}
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

		if (m_RecorderMode == MODE_VIDEO || m_RecorderMode == MODE_SCREENSHOT) {
			wstring ext = m_RecorderMode == MODE_VIDEO ? m_EncoderOptions->GetVideoExtension() : m_SnapshotOptions->GetImageExtension();
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
		DX_RESOURCES dxResources;
		RETURN_ON_BAD_HR(hr = InitializeDx(nullptr, &dxResources));
		m_Device = dxResources.Device;
		m_DeviceContext = dxResources.Context;
#if _DEBUG
		m_Debug = dxResources.Debug;
#endif
		m_TextureManager = make_unique<TextureManager>();
		m_TextureManager->Initialize(m_DeviceContext, m_Device);

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
		return new duplication_capture();
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
	SafeRelease(&m_DeviceContext);
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
				if (GetEncoderOptions()->GetIsHardwareEncodingEnabled()) {
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
	CComPtr<ID3D11Texture2D> pPreviousFrameCopy = nullptr;
	CComPtr<ID3D11Texture2D> pCurrentFrameCopy = nullptr;
	CComPtr<IMFSinkWriterCallback> pCallBack = nullptr;
	PTR_INFO *pPtrInfo = nullptr;
	unique_ptr<ScreenCaptureBase> pCapture(CreateCaptureSession());
	RETURN_ON_BAD_HR(pCapture->Initialize(m_DeviceContext, m_Device));
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

	SIZE videoOutputFrameSize{};
	RETURN_ON_BAD_HR(hr = InitializeRects(pCapture->GetOutputSize(), &videoInputFrameRect, &videoOutputFrameSize));

	HANDLE hMarkEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	CloseHandleOnExit closeMarkEvent(hMarkEvent);
	m_FinalizeEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	std::unique_ptr<AudioManager> pAudioManager = make_unique<AudioManager>();
	std::unique_ptr<MouseManager> pMouseManager = make_unique<MouseManager>();
	RETURN_ON_BAD_HR(hr = pMouseManager->Initialize(m_DeviceContext, m_Device, GetMouseOptions()));
	SetViewPort(m_DeviceContext, videoOutputFrameSize.cx, videoOutputFrameSize.cy);

	if (m_RecorderMode == MODE_VIDEO) {
		CComPtr<IMFByteStream> outputStream = nullptr;
		if (pStream != nullptr) {
			RETURN_ON_BAD_HR(hr = MFCreateMFByteStreamOnStream(pStream, &outputStream));
		}
		pCallBack = new (std::nothrow)CMFSinkWriterCallback(m_FinalizeEvent, hMarkEvent);
		RECT inputMediaFrameRect = RECT{ 0,0,videoOutputFrameSize.cx,videoOutputFrameSize.cy };
		RETURN_ON_BAD_HR(hr = InitializeVideoSinkWriter(m_OutputFullPath, outputStream, m_Device, inputMediaFrameRect, videoOutputFrameSize, DXGI_MODE_ROTATION_UNSPECIFIED, pCallBack, &m_SinkWriter, &m_VideoStreamIndex, &m_AudioStreamIndex));

		pAudioManager->Initialize(GetAudioOptions());
	}

	std::chrono::steady_clock::time_point lastFrame = std::chrono::steady_clock::now();
	std::chrono::steady_clock::time_point previousSnapshotTaken = (std::chrono::steady_clock::time_point::min)();
	INT64 videoFrameDurationMillis = 0;
	if (m_RecorderMode == MODE_VIDEO) {
		videoFrameDurationMillis = 1000 / GetEncoderOptions()->GetVideoFps();
	}
	else if (m_RecorderMode == MODE_SLIDESHOW) {
		videoFrameDurationMillis = GetSnapshotOptions()->GetSnapshotsInterval().count();
	}
	INT64 videoFrameDuration100Nanos = MillisToHundredNanos(videoFrameDurationMillis);

	INT frameNr = 0;
	INT64 lastFrameStartPos = 0;
	bool haveCachedPrematureFrame = false;
	cancellation_token token = m_TaskWrapperImpl->m_RecordTaskCts.get_token();

	auto IsTimeToTakeSnapshot([&]()
	{
		// The first condition is needed since (now - min) yields negative value because of overflow...
		return previousSnapshotTaken == (std::chrono::steady_clock::time_point::min)() ||
			(std::chrono::steady_clock::now() - previousSnapshotTaken) > GetSnapshotOptions()->GetSnapshotsInterval();
	});

	auto PrepareAndRenderFrame([&](CComPtr<ID3D11Texture2D> pTexture, INT64 duration100Nanos)->HRESULT {
		HRESULT renderHr = E_FAIL;
		if (pPtrInfo) {
			renderHr = pMouseManager->ProcessMousePointer(pTexture, pPtrInfo);
			if (FAILED(renderHr)) {
				_com_error err(renderHr);
				LOG_ERROR(L"Error drawing mouse pointer: %s", err.ErrorMessage());
				//We just log the error and continue if the mouse pointer failed to draw. If there is an error with DXGI, it will be handled on the next call to AcquireNextFrame.
			}
		}
		D3D11_TEXTURE2D_DESC desc;
		pTexture->GetDesc(&desc);

		if (RectWidth(videoInputFrameRect) != desc.Width
			|| RectHeight(videoInputFrameRect) != desc.Height) {
			ID3D11Texture2D *pCroppedFrameCopy;
			RETURN_ON_BAD_HR(renderHr = m_TextureManager->CropTexture(pTexture, videoInputFrameRect, &pCroppedFrameCopy));
			pTexture.Release();
			pTexture.Attach(pCroppedFrameCopy);
		}
		if (RectWidth(videoInputFrameRect) < videoOutputFrameSize.cx
			|| RectHeight(videoInputFrameRect) < videoOutputFrameSize.cy) {
			CComPtr<ID3D11Texture2D> pResizedFrameCopy;
			double widthRatio = (double)videoOutputFrameSize.cx / RectWidth(videoInputFrameRect);
			double heightRatio = (double)videoOutputFrameSize.cy / RectHeight(videoInputFrameRect);
			double resizeRatio = min(widthRatio, heightRatio);
			UINT resizedWidth = (UINT)MakeEven((LONG)round(RectWidth(videoInputFrameRect) * resizeRatio));
			UINT resizedHeight = (UINT)MakeEven((LONG)round(RectHeight(videoInputFrameRect) * resizeRatio));
			RETURN_ON_BAD_HR(renderHr = m_TextureManager->ResizeTexture(pTexture, &pResizedFrameCopy, SIZE{ static_cast<LONG>(resizedWidth), static_cast<LONG>(resizedHeight) }));
			D3D11_TEXTURE2D_DESC desc;
			pResizedFrameCopy->GetDesc(&desc);
			desc.Width = videoOutputFrameSize.cx;
			desc.Height = videoOutputFrameSize.cy;
			ID3D11Texture2D *pCanvas;
			RETURN_ON_BAD_HR(renderHr = m_Device->CreateTexture2D(&desc, nullptr, &pCanvas));
			int leftMargin = (int)max(0, round(((double)videoOutputFrameSize.cx - (double)resizedWidth)) / 2);
			int topMargin = (int)max(0, round(((double)videoOutputFrameSize.cy - (double)resizedHeight)) / 2);

			D3D11_BOX Box;
			Box.front = 0;
			Box.back = 1;
			Box.left = 0;
			Box.top = 0;
			Box.right = resizedWidth;
			Box.bottom = resizedHeight;
			m_DeviceContext->CopySubresourceRegion(pCanvas, 0, leftMargin, topMargin, 0, pResizedFrameCopy, 0, &Box);
			pTexture.Release();
			pTexture.Attach(pCanvas);
		}

		if (m_RecorderMode == MODE_VIDEO && GetSnapshotOptions()->IsSnapshotWithVideoEnabled() && IsTimeToTakeSnapshot()) {
			if (SUCCEEDED(renderHr = SaveTextureAsVideoSnapshot(pTexture, videoInputFrameRect))) {
				previousSnapshotTaken = steady_clock::now();
			}
			else {
				_com_error err(renderHr);
				LOG_ERROR("Error saving video snapshot: %ls", err.ErrorMessage());
			}
		}

		FrameWriteModel model{};
		model.Frame = pTexture;
		model.Duration = duration100Nanos;
		model.StartPos = lastFrameStartPos;
		model.Audio = pAudioManager->GrabAudioFrame();
		model.FrameNumber = frameNr;
		RETURN_ON_BAD_HR(renderHr = m_EncoderResult = RenderFrame(model));
		haveCachedPrematureFrame = false;
		frameNr++;
		lastFrameStartPos += duration100Nanos;
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
			previousSnapshotTaken = steady_clock::now();
			lastFrame = steady_clock::now();
			if (pAudioManager)
				pAudioManager->ClearRecordedBytes();
			if (pAudioManager)
				pAudioManager->ClearRecordedBytes();
			continue;
		}
		CAPTURED_FRAME capturedFrame{};
		// Get new frame
		hr = pCapture->AcquireNextFrame(
			haveCachedPrematureFrame || GetEncoderOptions()->GetIsFixedFramerate() ? 0 : 100,
			&capturedFrame);

		if (SUCCEEDED(hr)) {
			pCurrentFrameCopy.Attach(capturedFrame.Frame);
			if (capturedFrame.PtrInfo) {
				pPtrInfo = capturedFrame.PtrInfo;
			}
		}
		else {
			if (m_RecorderMode == MODE_SLIDESHOW
			   || m_RecorderMode == MODE_SCREENSHOT
			   && (frameNr == 0 && (pCurrentFrameCopy == nullptr || capturedFrame.FrameUpdateCount == 0))) {
				continue;
			}
			else if ((!pCurrentFrameCopy && !pPreviousFrameCopy)
				|| !pCapture->IsInitialFrameWriteComplete()) {
				//There is no first frame yet, so retry.
				wait(1);
				continue;
			}
		}
		INT64 durationSinceLastFrame100Nanos = max(duration_cast<nanoseconds>(chrono::steady_clock::now() - lastFrame).count() / 100, 0);
		INT64 durationSinceLastFrameMillis = HundredNanosToMillis(durationSinceLastFrame100Nanos);
		//Delay frames that comes quicker than selected framerate to see if we can skip them.
		if (hr == DXGI_ERROR_WAIT_TIMEOUT || durationSinceLastFrame100Nanos < videoFrameDuration100Nanos) //attempt to wait if frame timeouted or duration is under our chosen framerate
		{
			bool delayRender = false;
			INT64 delay100Nanos = 0;
			if (m_RecorderMode == MODE_VIDEO
				&& (frameNr == 0 //never delay the first frame 
					|| (GetMouseOptions()->IsMousePointerEnabled() && capturedFrame.PtrInfo && capturedFrame.PtrInfo->IsPointerShapeUpdated)//and never delay when pointer changes if we draw pointer
					|| (GetSnapshotOptions()->IsSnapshotWithVideoEnabled() && IsTimeToTakeSnapshot()))) // Or if we need to write a snapshot 
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
						m_DeviceContext->CopyResource(pPreviousFrameCopy, pCurrentFrameCopy);
					}
				}
				delayRender = true;
				haveCachedPrematureFrame = true;
				delay100Nanos = videoFrameDuration100Nanos - durationSinceLastFrame100Nanos;
			}
			else if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
				if (m_RecorderMode == MODE_SLIDESHOW
					|| GetEncoderOptions()->GetIsFixedFramerate()) {
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
					m_DeviceContext->CopyResource(pPreviousFrameCopy, pCurrentFrameCopy);
				}
			}
			else if (pPreviousFrameCopy) {
				D3D11_TEXTURE2D_DESC desc;
				pPreviousFrameCopy->GetDesc(&desc);
				RETURN_ON_BAD_HR(hr = m_Device->CreateTexture2D(&desc, nullptr, &pCurrentFrameCopy));
				m_DeviceContext->CopyResource(pCurrentFrameCopy, pPreviousFrameCopy);
			}

			if (token.is_canceled()) {
				LOG_DEBUG("Recording task was cancelled");
				hr = S_OK;
				break;
			}

			RETURN_ON_BAD_HR(hr = PrepareAndRenderFrame(pCurrentFrameCopy, durationSinceLastFrame100Nanos));
			if (m_RecorderMode == MODE_SCREENSHOT) {
				break;
			}
		}
	}

	//Push any last frame waiting to be recorded to the sink writer.
	if (pPreviousFrameCopy != nullptr) {
		INT64 duration = duration_cast<nanoseconds>(chrono::steady_clock::now() - lastFrame).count() / 100;
		RETURN_ON_BAD_HR(hr = PrepareAndRenderFrame(pPreviousFrameCopy, duration));
	}
	return hr;
}

HRESULT RecordingManager::InitializeRects(_In_ SIZE captureFrameSize, _Out_ RECT *pAdjustedSourceRect, _Out_ SIZE *pAdjustedOutputFrameSize) {

	RECT adjustedSourceRect = RECT{ 0,0, MakeEven(captureFrameSize.cx),  MakeEven(captureFrameSize.cy) };
	SIZE adjustedOutputFrameSize = SIZE{ MakeEven(captureFrameSize.cx),MakeEven(captureFrameSize.cy) };
	if (m_SourceRect.right > m_SourceRect.left
	&& m_SourceRect.bottom > m_SourceRect.top)
	{
		adjustedSourceRect = m_SourceRect;
		adjustedOutputFrameSize = SIZE{ MakeEven(RectWidth(m_SourceRect)),MakeEven(RectHeight(m_SourceRect)) };
	}
	auto outputRect = GetEncoderOptions()->GetFrameSize();
	if (outputRect.cx > 0
	&& outputRect.cy > 0)
	{
		adjustedOutputFrameSize = SIZE{ MakeEven(outputRect.cx),MakeEven(outputRect.cy) };
	}
	*pAdjustedSourceRect = MakeRectEven(adjustedSourceRect);
	*pAdjustedOutputFrameSize = adjustedOutputFrameSize;
	return S_OK;
}

HRESULT RecordingManager::InitializeVideoSinkWriter(
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

	// Passing 6 as the argument to save re-allocations
	RETURN_ON_BAD_HR(MFCreateAttributes(&pAttributes, 7));
	RETURN_ON_BAD_HR(pAttributes->SetGUID(MF_TRANSCODE_CONTAINERTYPE, MFTranscodeContainerType_MPEG4));
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
	RETURN_ON_BAD_HR(pVideoMediaType->SetGUID(MF_MT_SUBTYPE, GetEncoderOptions()->GetVideoEncoderFormat()));
	RETURN_ON_BAD_HR(pVideoMediaType->SetUINT32(MF_MT_AVG_BITRATE, GetEncoderOptions()->GetVideoBitrate()));
	RETURN_ON_BAD_HR(pVideoMediaType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive));
	RETURN_ON_BAD_HR(pVideoMediaType->SetUINT32(MF_MT_MPEG2_PROFILE, GetEncoderOptions()->GetEncoderProfile()));
	RETURN_ON_BAD_HR(pVideoMediaType->SetUINT32(MF_MT_YUV_MATRIX, MFVideoTransferMatrix_BT601));
	RETURN_ON_BAD_HR(MFSetAttributeSize(pVideoMediaType, MF_MT_FRAME_SIZE, destWidth, destHeight));
	RETURN_ON_BAD_HR(MFSetAttributeRatio(pVideoMediaType, MF_MT_FRAME_RATE, GetEncoderOptions()->GetVideoFps(), 1));
	RETURN_ON_BAD_HR(MFSetAttributeRatio(pVideoMediaType, MF_MT_PIXEL_ASPECT_RATIO, 1, 1));

	if (GetAudioOptions()->IsAnyAudioDeviceEnabled()) {
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
	// Copy the output media type
	CopyMediaType(pVideoMediaTypeOut, &pVideoMediaType);
	// Set the subtype.
	RETURN_ON_BAD_HR(pVideoMediaType->SetGUID(MF_MT_SUBTYPE, GetEncoderOptions()->GetVideoInputFormat()));
	// Uncompressed means all samples are independent.
	RETURN_ON_BAD_HR(pVideoMediaType->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE));
	//CreateInputMediaTypeFromOutput(pVideoMediaTypeOut, GetEncoderOptions()->GetVideoInputFormat(), &pVideoMediaType);
	RETURN_ON_BAD_HR(MFSetAttributeSize(pVideoMediaType, MF_MT_FRAME_SIZE, sourceWidth, sourceHeight));
	pVideoMediaType->SetUINT32(MF_MT_VIDEO_ROTATION, rotationFormat);

	if (GetAudioOptions()->IsAnyAudioDeviceEnabled()) {
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

		/* If the audio pCaptureInstance returns no data, i.e. the source is silent, we need to pad the PCM stream with zeros to give the media sink silence as input.
		 * If we don't, the sink writer will begin throttling video frames because it expects audio samples to be delivered, and think they are delayed.
		 * We ignore every instance where the last frame had audio, due to sometimes very short frame durations due to mouse cursor changes have zero audio length,
		 * and inserting silence between two frames that has audio leads to glitching. */
		if (GetAudioOptions()->IsAnyAudioDeviceEnabled() && model.Audio.size() == 0 && model.Duration > 0) {
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
	else if (m_RecorderMode == MODE_SLIDESHOW) {
		wstring	path = m_OutputFolder + L"\\" + to_wstring(model.FrameNumber) + GetSnapshotOptions()->GetImageExtension();
		hr = WriteFrameToImage(model.Frame, path);
		INT64 startposMs = HundredNanosToMillis(model.StartPos);
		INT64 durationMs = HundredNanosToMillis(model.Duration);
		if (FAILED(hr)) {
			_com_error err(hr);
			LOG_ERROR(L"Writing of slideshow frame with start pos %lld ms failed: %s", startposMs, err.ErrorMessage());
			return hr; //Stop recording if we fail
		}
		else {

			m_FrameDelays.insert(std::pair<wstring, int>(path, model.FrameNumber == 0 ? 0 : (int)durationMs));
			LOG_TRACE(L"Wrote video slideshow frame with start pos %lld ms and with duration %lld ms", startposMs, durationMs);
		}
	}
	else if (m_RecorderMode == MODE_SCREENSHOT) {
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


HRESULT RecordingManager::WriteFrameToImage(_In_ ID3D11Texture2D *pAcquiredDesktopImage, _In_ std::wstring filePath)
{
	return SaveWICTextureToFile(m_DeviceContext, pAcquiredDesktopImage, GetSnapshotOptions()->GetSnapshotEncoderFormat(), filePath.c_str());
}

void RecordingManager::WriteTextureToImageAsync(_In_ ID3D11Texture2D *pAcquiredDesktopImage, _In_ std::wstring filePath)
{
	pAcquiredDesktopImage->AddRef();
	concurrency::create_task([this, pAcquiredDesktopImage, filePath]() {
		return WriteFrameToImage(pAcquiredDesktopImage, filePath);
		}).then([this, filePath, pAcquiredDesktopImage](concurrency::task<HRESULT> t)
			{
				try {
					HRESULT hr = t.get();
					if (!m_TaskWrapperImpl->m_RecordTaskCts.get_token().is_canceled()) {
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
		RETURN_ON_BAD_HR(hr = m_Device->CreateTexture2D(&frameDesc, nullptr, &pProcessedTexture));
		// Copy the current frame for a separate thread to write it to a file asynchronously.
		m_DeviceContext->CopyResource(pProcessedTexture, pTexture);
	}


	wstring snapshotPath = GetSnapshotOptions()->GetSnapshotsDirectory() + L"\\" + s2ws(CurrentTimeToFormattedString()) + GetSnapshotOptions()->GetImageExtension();
	WriteTextureToImageAsync(pProcessedTexture, snapshotPath.c_str());
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