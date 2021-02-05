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
#include "loopback_capture.h"
#include "internal_recorder.h"
#include "audio_prefs.h"
#include "log.h"
#include "utilities.h"
#include "cleanup.h"
#include "string_format.h"
#include "screengrab.h"
#include "graphics_capture.h"
#include "graphics_capture.util.h"
#include "monitor_list.h"
#include "duplication_capture.h"

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

struct internal_recorder::TaskWrapper {
	Concurrency::task<void> m_RecordTask = concurrency::task_from_result();
	Concurrency::cancellation_token_source m_RecordTaskCts;
};


internal_recorder::internal_recorder() :m_TaskWrapperImpl(make_unique<TaskWrapper>())
{

}

internal_recorder::~internal_recorder()
{
	UnhookWindowsHookEx(m_Mousehook);
	if (m_IsRecording) {
		LOG_WARN("Recording is in progress while destructing, cancelling recording task and waiting for completion.");
		m_TaskWrapperImpl->m_RecordTaskCts.cancel();
		m_TaskWrapperImpl->m_RecordTask.wait();
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
void internal_recorder::SetMouseClickDetectionDuration(int value) {
	g_MouseClickDetectionDurationMillis = value;
}
void internal_recorder::SetIsLogEnabled(bool value) {
	isLoggingEnabled = value;
}
void internal_recorder::SetLogFilePath(std::wstring value) {
	logFilePath = value;
}
void internal_recorder::SetLogSeverityLevel(int value) {
	logSeverityLevel = value;
}

std::vector<BYTE> internal_recorder::MixAudio(_In_ std::vector<BYTE> const &first, _In_ std::vector<BYTE> const &second, _In_ float firstVolume, _In_ float secondVolume)
{
	std::vector<BYTE> newvector(max(first.size(), second.size()));
	bool clipped = false;
	for (size_t i = 0; i < newvector.size(); i += 2) {
		short firstSample = first.size() > i + 1 ? static_cast<short>(first[i] | first[i + 1] << 8) : 0;
		short secondSample = second.size() > i + 1 ? static_cast<short>(second[i] | second[i + 1] << 8) : 0;
		auto out = reinterpret_cast<short*>(&newvector[i]);
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

std::wstring internal_recorder::GetImageExtension() {
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

std::wstring internal_recorder::GetVideoExtension() {
	return L".mp4";
}

HRESULT internal_recorder::ConfigureOutputDir(_In_ std::wstring path) {
	m_OutputFullPath = path;
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
		LOG_DEBUG(L"output folder is ready");
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
			// Snapshots will be saved in a folder named as video file name without extension. 
			m_OutputSnapshotsFolderPath = m_OutputFullPath.substr(0, m_OutputFullPath.find_last_of(L"."));
			std::filesystem::create_directory(m_OutputSnapshotsFolderPath);
		}
	}
	return S_OK;
}

HRESULT internal_recorder::BeginRecording(_In_opt_ IStream *stream) {
	return BeginRecording(L"", stream);
}

HRESULT internal_recorder::BeginRecording(_In_opt_ std::wstring path) {
	return BeginRecording(path, nullptr);
}

HRESULT internal_recorder::BeginRecording(_In_opt_ std::wstring path, _In_opt_ IStream *stream) {

	if (!IsWindows8OrGreater()) {
		wstring errorText = L"Windows 8 or higher is required";
		LOG_ERROR(L"%ls", errorText);
		RecordingFailedCallback(errorText);
		return E_FAIL;
	}

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

	g_LastMouseClickDurationRemaining = 0;
	m_EncoderResult = S_FALSE;
	m_LastFrameHadAudio = false;
	m_FrameDelays.clear();
	if (!path.empty()) {
		RETURN_ON_BAD_HR(ConfigureOutputDir(path));
	}

	m_TaskWrapperImpl->m_RecordTaskCts = cancellation_token_source();
	m_TaskWrapperImpl->m_RecordTask = concurrency::create_task([this, stream]() {
		LOG_INFO(L"Starting recording task");
		m_IsRecording = true;
		HRESULT hr = CoInitializeEx(nullptr, COINITBASE_MULTITHREADED | COINIT_DISABLE_OLE1DDE);
		RETURN_ON_BAD_HR(hr);
		RETURN_ON_BAD_HR(hr = MFStartup(MF_VERSION, MFSTARTUP_LITE));
		InitializeMouseClickDetection();
		RETURN_ON_BAD_HR(hr = InitializeDx(&m_ImmediateContext, &m_Device));

		if (RecordingStatusChangedCallback != nullptr) {
			RecordingStatusChangedCallback(STATUS_RECORDING);
			LOG_DEBUG("Changed Recording Status to Recording");
		}

		if (m_RecorderApi == API_GRAPHICS_CAPTURE) {
			RETURN_ON_BAD_HR(hr = StartGraphicsCaptureRecorderLoop(stream));
		}
		else if (m_RecorderApi == API_DESKTOP_DUPLICATION) {
			RETURN_ON_BAD_HR(hr = StartDesktopDuplicationRecorderLoop(stream));
		}
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

void internal_recorder::EndRecording() {
	if (m_IsRecording) {
		m_IsPaused = false;
		m_TaskWrapperImpl->m_RecordTaskCts.cancel();
	}
}
void internal_recorder::PauseRecording() {
	if (m_IsRecording) {
		m_IsPaused = true;
		if (RecordingStatusChangedCallback != nullptr) {
			RecordingStatusChangedCallback(STATUS_PAUSED);
			LOG_DEBUG("Changed Recording Status to Paused");
		}
	}
}
void internal_recorder::ResumeRecording() {
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

bool internal_recorder::SetExcludeFromCapture(HWND hwnd, bool isExcluded) {
	// The API call causes ugly black window on older builds of Windows, so skip if the contract is down-level. 
	if (winrt::Windows::Foundation::Metadata::ApiInformation::IsApiContractPresent(L"Windows.Foundation.UniversalApiContract", 9))
		return (bool)SetWindowDisplayAffinity(hwnd, isExcluded ? WDA_EXCLUDEFROMCAPTURE : WDA_NONE);
	else
		return false;
}

HRESULT internal_recorder::FinalizeRecording()
{
	LOG_INFO("Cleaning up resources");
	LOG_INFO("Finalizing recording");
	HRESULT finalizeResult = S_OK;
	if (m_SinkWriter) {
		if (RecordingStatusChangedCallback != nullptr) {
			RecordingStatusChangedCallback(STATUS_FINALIZING);
		}
		finalizeResult = m_SinkWriter->Finalize();
		if (m_FinalizeEvent) {
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

void internal_recorder::CleanupResourcesAndShutDownMF()
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

void internal_recorder::SetRecordingCompleteStatus(_In_ HRESULT hr)
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
				if (m_IsHardwareEncodingEnabled) {
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

HRESULT internal_recorder::StartGraphicsCaptureRecorderLoop(_In_opt_ IStream *pStream)
{
	auto isCaptureSupported = winrt::Windows::Graphics::Capture::GraphicsCaptureSession::IsSupported();
	if (!isCaptureSupported)
	{
		wstring error = L"Windows Graphics Capture API is not supported on this version of Windows";
		LOG_ERROR(L"%ls", error.c_str());
		if (RecordingFailedCallback != nullptr)
			RecordingFailedCallback(error);
		return E_FAIL;
	}

	LOG_DEBUG("Starting Windows Graphics Capture recorder loop");

	HRESULT hr;
	std::unique_ptr<loopback_capture> pLoopbackCaptureOutputDevice = nullptr;
	std::unique_ptr<loopback_capture> pLoopbackCaptureInputDevice = nullptr;
	CComPtr<ID3D11Texture2D> pPreviousFrameCopy = nullptr;
	CComPtr<IMFSinkWriterCallback> pCallBack = nullptr;
	CComPtr<IDXGIDevice> pDxgiDevice = nullptr;
	RETURN_ON_BAD_HR(hr = m_Device->QueryInterface(IID_PPV_ARGS(&pDxgiDevice)));
	auto pDevice = capture::util::CreateDirect3DDevice(pDxgiDevice);
	GraphicsCaptureItem captureItem = nullptr;
	RETURN_ON_BAD_HR(hr = CreateCaptureItem(&captureItem));
	RECT windowPositionOnDesktop{};
	GetWindowRect(m_WindowHandle, &windowPositionOnDesktop);
	auto pCapture = std::make_unique<graphics_capture>(pDevice, captureItem, DirectXPixelFormat::B8G8R8A8UIntNormalized, nullptr);
	if (winrt::Windows::Foundation::Metadata::ApiInformation::IsApiContractPresent(L"Windows.Foundation.UniversalApiContract", 9)) {
		pCapture->SetEnableCursorCapture(m_IsMousePointerEnabled);
	}
	pCapture->StartCapture();

	D3D11_TEXTURE2D_DESC sourceFrameDesc;
	RtlZeroMemory(&sourceFrameDesc, sizeof(sourceFrameDesc));
	int sourceWidth = 0;
	int sourceHeight = 0;
	//We try get the first frame now, because the dimensions for the captureItem is sometimes wrong, causing black borders around the recording, while the dimensions for a frame is always correct.
	Direct3D11CaptureFrame firstFrame = NULL;
	for (int i = 0; i < 10; i++) {
		firstFrame = pCapture->TryGetNextFrame();
		if (firstFrame) {
			break;
		}
		wait(10);
	}
	if (firstFrame) {
		sourceWidth = firstFrame.ContentSize().Width;
		sourceHeight = firstFrame.ContentSize().Height;
		firstFrame.Close();
	}
	else {
		sourceWidth = captureItem.Size().Width;
		sourceHeight = captureItem.Size().Height;
	}
	RECT videoOutputFrameRect, videoInputFrameRect, previousInputFrameRect;
	RtlZeroMemory(&videoOutputFrameRect, sizeof(videoOutputFrameRect));
	RtlZeroMemory(&videoInputFrameRect, sizeof(videoInputFrameRect));
	RtlZeroMemory(&previousInputFrameRect, sizeof(previousInputFrameRect));
	videoOutputFrameRect.left = 0;
	videoOutputFrameRect.top = 0;
	videoOutputFrameRect.right = sourceWidth;
	videoOutputFrameRect.bottom = sourceHeight;
	videoOutputFrameRect = MakeRectEven(videoOutputFrameRect);
	videoInputFrameRect = videoOutputFrameRect;
	//Differing input and output dimensions of the mediatype initializes the video processor with the sink writer so we can use it for resizing the input.
	//These values will be overwritten on a frame by frame basis.
	videoInputFrameRect.right += 2;
	videoInputFrameRect.bottom += 2;
	HANDLE hMarkEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	m_FinalizeEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	if (m_RecorderMode == MODE_VIDEO) {
		loopback_capture *outputCapture = nullptr;
		loopback_capture *inputCapture = nullptr;
		hr = InitializeAudioCapture(&outputCapture, &inputCapture);
		if (SUCCEEDED(hr)) {
			pLoopbackCaptureOutputDevice = std::unique_ptr<loopback_capture>(outputCapture);
			pLoopbackCaptureInputDevice = std::unique_ptr<loopback_capture>(inputCapture);
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

	std::unique_ptr<mouse_pointer> pMousePointer = make_unique<mouse_pointer>();
	RETURN_ON_BAD_HR(hr = pMousePointer->Initialize(m_ImmediateContext, m_Device));
	PTR_INFO PtrInfo{};

	pCapture->ClearFrameBuffer();
	if (pLoopbackCaptureOutputDevice)
		pLoopbackCaptureOutputDevice->ClearRecordedBytes();
	if (pLoopbackCaptureInputDevice)
		pLoopbackCaptureInputDevice->ClearRecordedBytes();

	std::chrono::steady_clock::time_point	lastFrame = std::chrono::steady_clock::now();
	m_previousSnapshotTaken = (std::chrono::steady_clock::time_point::min)();
	INT64 videoFrameDurationMillis = 1000 / m_VideoFps;
	INT64 videoFrameDuration100Nanos = MillisToHundredNanos(videoFrameDurationMillis);

	INT frameNr = 0;
	INT64 lastFrameStartPos = 0;
	bool haveCachedPrematureFrame = false;
	bool gotMousePointer = false;
	cancellation_token token = m_TaskWrapperImpl->m_RecordTaskCts.get_token();
	while (true) {
		if (token.is_canceled()) {
			LOG_DEBUG("Recording task was cancelled");
			hr = S_OK;
			break;
		}
		if (m_IsPaused) {
			wait(10);
			lastFrame = steady_clock::now();
			m_previousSnapshotTaken = steady_clock::now();
			pLoopbackCaptureOutputDevice->ClearRecordedBytes();
			pLoopbackCaptureInputDevice->ClearRecordedBytes();
			pCapture->ClearFrameBuffer();
			continue;
		}
		auto frame = pCapture->TryGetNextFrame();

		winrt::com_ptr<ID3D11Texture2D> surfaceTexture = nullptr;

		if (frame) {
			surfaceTexture = capture::util::GetDXGIInterfaceFromObject<ID3D11Texture2D>(frame.Surface());
			surfaceTexture->GetDesc(&sourceFrameDesc);
			// Clear flags that we don't need
			sourceFrameDesc.Usage = D3D11_USAGE_DEFAULT;
			sourceFrameDesc.BindFlags = D3D11_BIND_RENDER_TARGET;
			sourceFrameDesc.CPUAccessFlags = 0;
			sourceFrameDesc.MiscFlags = 0;

			videoInputFrameRect.right = frame.ContentSize().Width - videoInputFrameRect.left;
			videoInputFrameRect.bottom = frame.ContentSize().Height - videoInputFrameRect.top;
			videoInputFrameRect = MakeRectEven(videoInputFrameRect);
			if (!EqualRect(&videoInputFrameRect, &previousInputFrameRect)) {
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
					//The destination rectangle is the portion of the output surface where the source rectangle is blitted.
					videoProcessor->SetDestinationRectangle(&videoOutputFrameRect);
					LOG_TRACE("Changing video processor surface rect: source=%dx%d, dest = %dx%d", videoInputFrameRect.right - videoInputFrameRect.left, videoInputFrameRect.bottom - videoInputFrameRect.top, videoOutputFrameRect.right - videoOutputFrameRect.left, videoOutputFrameRect.bottom - videoOutputFrameRect.top);
				}
				SetViewPort(m_ImmediateContext, videoInputFrameRect.right - videoInputFrameRect.left, videoInputFrameRect.bottom - videoInputFrameRect.top);
				previousInputFrameRect = videoInputFrameRect;
			}
			// Get mouse info. Windows Graphics Capture includes the mouse cursor on the texture, so we only get the positioning info for mouse click draws.
			gotMousePointer = SUCCEEDED(pMousePointer->GetMouse(&PtrInfo, videoInputFrameRect, false, -windowPositionOnDesktop.left, -windowPositionOnDesktop.top));
			frame.Close();
		}
		else if (sourceFrameDesc.Width == 0) {
			//There is no first frame yet, so retry.
			continue;
		}
		else if (m_WindowHandle != nullptr && IsIconic(m_WindowHandle)) {
			//The targeted window is minimized and not rendered, so it cannot be recorded. The previous frame copy is deleted to make it record a black frame instead.
			pPreviousFrameCopy = nullptr;
			if (PtrInfo.PtrShapeBuffer)
			{
				delete[] PtrInfo.PtrShapeBuffer;
				PtrInfo.PtrShapeBuffer = nullptr;
			}
			RtlZeroMemory(&PtrInfo, sizeof(PtrInfo));
		}

		INT64 durationSinceLastFrame100Nanos = max(duration_cast<nanoseconds>(chrono::steady_clock::now() - lastFrame).count() / 100, 0);
		INT64 durationSinceLastFrameMillis = HundredNanosToMillis(durationSinceLastFrame100Nanos);
		//Delay frames that comes quicker than selected framerate to see if we can skip them.
		if (durationSinceLastFrame100Nanos < videoFrameDuration100Nanos) //attempt to wait if frame timeouted or duration is under our chosen framerate
		{
			bool delay = false;
			if (frameNr == 0 //never delay the first frame 
				|| m_IsFixedFramerate //or if the framerate is fixed
				|| (IsSnapshotsWithVideoEnabled() && IsTimeToTakeSnapshot())) // Or if we need to write a snapshot 
			{
				delay = false;
			}
			else if (SUCCEEDED(hr) && durationSinceLastFrame100Nanos < videoFrameDuration100Nanos) {
				if (surfaceTexture) {
					//we got a frame, but it's too soon, so we cache it and see if there are more changes.
					if (pPreviousFrameCopy == nullptr) {
						RETURN_ON_BAD_HR(hr = m_Device->CreateTexture2D(&sourceFrameDesc, nullptr, &pPreviousFrameCopy));
					}
					m_ImmediateContext->CopyResource(pPreviousFrameCopy, surfaceTexture.get());
				}
				delay = true;
				haveCachedPrematureFrame = true;
			}
			else if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
				if (haveCachedPrematureFrame && durationSinceLastFrameMillis < videoFrameDurationMillis) {
					delay = true;
				}
				else if (!haveCachedPrematureFrame && durationSinceLastFrame100Nanos < m_MaxFrameLength100Nanos) {
					delay = true;
				}
			}
			if (delay) {
				unsigned int delay = 1;
				if (durationSinceLastFrame100Nanos < videoFrameDuration100Nanos) {
					delay = (unsigned int)HundredNanosToMillis((videoFrameDuration100Nanos - durationSinceLastFrame100Nanos));
				}
				wait(delay);
				continue;
			}
		}

		lastFrame = steady_clock::now();
		CComPtr<ID3D11Texture2D> pFrameCopy = nullptr;
		RETURN_ON_BAD_HR(hr = m_Device->CreateTexture2D(&sourceFrameDesc, nullptr, &pFrameCopy));

		if (surfaceTexture != nullptr) {
			m_ImmediateContext->CopyResource(pFrameCopy, surfaceTexture.get());
			if (pPreviousFrameCopy) {
				pPreviousFrameCopy.Release();
			}
			//Copy new frame to pPreviousFrameCopy
			if (m_RecorderMode == MODE_VIDEO || m_RecorderMode == MODE_SLIDESHOW) {
				RETURN_ON_BAD_HR(hr = m_Device->CreateTexture2D(&sourceFrameDesc, nullptr, &pPreviousFrameCopy));
				m_ImmediateContext->CopyResource(pPreviousFrameCopy, pFrameCopy);
				SetDebugName(pPreviousFrameCopy, "PreviousFrameCopy");
			}
		}
		else if (pPreviousFrameCopy) {
			m_ImmediateContext->CopyResource(pFrameCopy, pPreviousFrameCopy);
		}

		if (gotMousePointer) {
			hr = DrawMousePointer(pFrameCopy, pMousePointer.get(), PtrInfo, DXGI_MODE_ROTATION_IDENTITY, durationSinceLastFrame100Nanos);
			if (FAILED(hr)) {
				_com_error err(hr);
				LOG_ERROR(L"Error drawing mouse pointer: %s", err.ErrorMessage());
				//We just log the error and continue if the mouse pointer failed to draw. If there is an error with DXGI, it will be handled on the next call to AcquireNextFrame.
			}
		}

		SetDebugName(pFrameCopy, "FrameCopy");

		if (IsSnapshotsWithVideoEnabled() && IsTimeToTakeSnapshot()) {
			TakeSnapshotsWithVideo(pFrameCopy, sourceFrameDesc, videoInputFrameRect);
		}

		if (token.is_canceled()) {
			LOG_DEBUG("Recording task was cancelled");
			hr = S_OK;
			break;
		}
		FrameWriteModel model;
		RtlZeroMemory(&model, sizeof(model));
		model.Frame = pFrameCopy;
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
		if (m_IsFixedFramerate)
		{
			wait(static_cast<UINT32>(max(videoFrameDurationMillis - duration_cast<milliseconds>(chrono::steady_clock::now() - lastFrame).count(), 0)));
		}
	}
	return hr;
}

HRESULT internal_recorder::StartDesktopDuplicationRecorderLoop(_In_opt_ IStream *pStream)
{
	LOG_DEBUG("Starting Desktop Duplication recorder loop");
	std::unique_ptr<loopback_capture> pLoopbackCaptureOutputDevice = nullptr;
	std::unique_ptr<loopback_capture> pLoopbackCaptureInputDevice = nullptr;
	CComPtr<ID3D11Texture2D> pFrameCopyForSnapshotsWithVideo = nullptr;
	CComPtr<ID3D11Texture2D> pPreviousFrameCopy = nullptr;
	CComPtr<ID3D11Texture2D> pCurrentFrameCopy = nullptr;

	HRESULT hr;
	bool gotMousePointer = false;

	HANDLE UnexpectedErrorEvent = nullptr;
	HANDLE ExpectedErrorEvent = nullptr;
	HANDLE TerminateThreadsEvent = nullptr;

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

	std::vector<std::wstring> outputs{ };
	for each (std::wstring display in m_DisplayOutputDevices)
	{
		if (GetOutputForDeviceName(display, nullptr) == S_OK) {
			outputs.push_back(display);
		}
	}
	auto pCapture = std::make_unique<duplication_capture>(m_Device, m_ImmediateContext);
	RETURN_ON_BAD_HR(hr = pCapture->StartCapture(outputs, UnexpectedErrorEvent, ExpectedErrorEvent));
	DesktopDuplicationCaptureStopOnExit stopCaptureOnExit(pCapture.get());

	DXGI_MODE_ROTATION screenRotation = DXGI_MODE_ROTATION_UNSPECIFIED;// outputDuplDesc.Rotation;
	D3D11_TEXTURE2D_DESC sourceFrameDesc{};
	D3D11_TEXTURE2D_DESC destFrameDesc{};
	RECT videoInputFrameRect{};
	RECT videoOutputFrameRect{};

	RETURN_ON_BAD_HR(hr = InitializeDesc(pCapture->GetOutputRect(), &sourceFrameDesc, &destFrameDesc, &videoInputFrameRect, &videoOutputFrameRect));
	bool isDestRectEqualToSourceRect = EqualRect(&videoInputFrameRect, &videoOutputFrameRect);

	std::unique_ptr<mouse_pointer> pMousePointer = make_unique<mouse_pointer>();
	RETURN_ON_BAD_HR(hr = pMousePointer->Initialize(m_ImmediateContext, m_Device));
	SetViewPort(m_ImmediateContext, videoInputFrameRect.right - videoInputFrameRect.left, videoInputFrameRect.bottom - videoInputFrameRect.top);

	LARGE_INTEGER lastMouseUpdateTime{};
	PTR_INFO PtrInfo{};

	if (m_RecorderMode == MODE_VIDEO) {
		loopback_capture *outputCapture = nullptr;
		loopback_capture *inputCapture = nullptr;
		hr = InitializeAudioCapture(&outputCapture, &inputCapture);
		if (SUCCEEDED(hr)) {
			pLoopbackCaptureOutputDevice = std::unique_ptr<loopback_capture>(outputCapture);
			pLoopbackCaptureInputDevice = std::unique_ptr<loopback_capture>(inputCapture);
		}
		else {
			LOG_ERROR(L"Audio capture failed to start: hr = 0x%08x", hr);
		}
		CComPtr<IMFByteStream> outputStream = nullptr;
		if (pStream != nullptr) {
			RETURN_ON_BAD_HR(hr = MFCreateMFByteStreamOnStream(pStream, &outputStream));
		}
		RETURN_ON_BAD_HR(hr = InitializeVideoSinkWriter(m_OutputFullPath, outputStream, m_Device, videoInputFrameRect, videoOutputFrameRect, screenRotation, nullptr, &m_SinkWriter, &m_VideoStreamIndex, &m_AudioStreamIndex));
	}
	if (pLoopbackCaptureInputDevice)
		pLoopbackCaptureInputDevice->ClearRecordedBytes();
	if (pLoopbackCaptureOutputDevice)
		pLoopbackCaptureOutputDevice->ClearRecordedBytes();

	std::chrono::steady_clock::time_point lastFrame = std::chrono::steady_clock::now();
	m_previousSnapshotTaken = (std::chrono::steady_clock::time_point::min)();
	INT64 videoFrameDurationMillis = 1000 / m_VideoFps;
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
			pCapture.reset(new duplication_capture(m_Device, m_ImmediateContext));
			stopCaptureOnExit.Reset(pCapture.get());
			ResetEvent(UnexpectedErrorEvent);
			ResetEvent(ExpectedErrorEvent);
			RETURN_ON_BAD_HR(hr = pCapture->StartCapture(outputs, UnexpectedErrorEvent, ExpectedErrorEvent));
			continue;
		}
		if (m_IsPaused) {
			wait(10);
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
			0,
			&capturedFrame);

		if (SUCCEEDED(hr)) {
			pCurrentFrameCopy.Attach(capturedFrame.Frame);
			// Get mouse info
			if (pCapture->GetPointerInfo()) {
				PtrInfo = *pCapture->GetPointerInfo();
				gotMousePointer = true;
			}
			else {
				gotMousePointer = false;
			}
		}

		if (m_RecorderMode == MODE_SLIDESHOW
			|| m_RecorderMode == MODE_SNAPSHOT) {

			if (frameNr == 0 && (pCurrentFrameCopy == nullptr || capturedFrame.UpdateCount == 0)) {
				continue;
			}
		}

		INT64 durationSinceLastFrame100Nanos = max(duration_cast<nanoseconds>(chrono::steady_clock::now() - lastFrame).count() / 100, 0);
		INT64 durationSinceLastFrameMillis = HundredNanosToMillis(durationSinceLastFrame100Nanos);
		//Delay frames that comes quicker than selected framerate to see if we can skip them.
		if (hr == DXGI_ERROR_WAIT_TIMEOUT || durationSinceLastFrame100Nanos < videoFrameDuration100Nanos) //attempt to wait if frame timeouted or duration is under our chosen framerate
		{
			bool delay = false;
			if (frameNr == 0 //never delay the first frame 
				|| m_IsFixedFramerate //or if the framerate is fixed
				|| (m_IsMousePointerEnabled && PtrInfo.LastTimeStamp.QuadPart > lastMouseUpdateTime.QuadPart)//and never delay when pointer changes if we draw pointer
				|| (IsSnapshotsWithVideoEnabled() && IsTimeToTakeSnapshot())) // Or if we need to write a snapshot 
			{
				delay = false;
			}
			else if (SUCCEEDED(hr) && durationSinceLastFrame100Nanos < videoFrameDuration100Nanos) {
				if (capturedFrame.UpdateCount > 0) {
					if (pCurrentFrameCopy != nullptr) {
						//we got a frame, but it's too soon, so we cache it and see if there are more changes.
						if (pPreviousFrameCopy == nullptr) {
							RETURN_ON_BAD_HR(hr = m_Device->CreateTexture2D(&sourceFrameDesc, nullptr, &pPreviousFrameCopy));
						}
						m_ImmediateContext->CopyResource(pPreviousFrameCopy, pCurrentFrameCopy);
					}
					delay = true;
					haveCachedPrematureFrame = true;
				}
			}
			else if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
				if (haveCachedPrematureFrame && durationSinceLastFrameMillis < videoFrameDurationMillis) {
					delay = true;
				}
				else if (!haveCachedPrematureFrame && durationSinceLastFrame100Nanos < m_MaxFrameLength100Nanos) {
					delay = true;
				}
			}
			if (delay) {
				unsigned int delay = 1;
				if (durationSinceLastFrameMillis < videoFrameDurationMillis) {
					delay = (unsigned int)HundredNanosToMillis((videoFrameDuration100Nanos - durationSinceLastFrame100Nanos));
				}
				wait(delay);
				continue;
			}
		}

		if (hr != DXGI_ERROR_WAIT_TIMEOUT) {
			RETURN_ON_BAD_HR(hr);
		}

		lastFrame = steady_clock::now();
		{
			if (pCurrentFrameCopy != nullptr) {
				if (pPreviousFrameCopy) {
					pPreviousFrameCopy.Release();
				}
				//Copy new frame to pPreviousFrameCopy
				if (m_RecorderMode == MODE_VIDEO || m_RecorderMode == MODE_SLIDESHOW) {
					RETURN_ON_BAD_HR(hr = m_Device->CreateTexture2D(&sourceFrameDesc, nullptr, &pPreviousFrameCopy));
					m_ImmediateContext->CopyResource(pPreviousFrameCopy, pCurrentFrameCopy);
					SetDebugName(pPreviousFrameCopy, "PreviousFrameCopy");
				}
			}
			else if (pPreviousFrameCopy) {
				RETURN_ON_BAD_HR(hr = m_Device->CreateTexture2D(&sourceFrameDesc, nullptr, &pCurrentFrameCopy));
				m_ImmediateContext->CopyResource(pCurrentFrameCopy, pPreviousFrameCopy);
			}

			SetDebugName(pCurrentFrameCopy, "FrameCopy");

			if (gotMousePointer) {
				hr = DrawMousePointer(pCurrentFrameCopy, pMousePointer.get(), PtrInfo, screenRotation, durationSinceLastFrame100Nanos);
				if (FAILED(hr)) {
					_com_error err(hr);
					LOG_ERROR(L"Error drawing mouse pointer: %s", err.ErrorMessage());
					//We just log the error and continue if the mouse pointer failed to draw. If there is an error with DXGI, it will be handled on the next call to AcquireNextFrame.
				}
				else {
					lastMouseUpdateTime = PtrInfo.LastTimeStamp;
				}
			}
			if (token.is_canceled()) {
				LOG_DEBUG("Recording task was cancelled");
				hr = S_OK;
				break;
			}

			if ((m_RecorderMode == MODE_SLIDESHOW || m_RecorderMode == MODE_SNAPSHOT) && !isDestRectEqualToSourceRect) {
				ID3D11Texture2D *pCroppedFrameCopy;
				RETURN_ON_BAD_HR(hr = CropFrame(pCurrentFrameCopy, destFrameDesc, videoOutputFrameRect, &pCroppedFrameCopy));
				pCurrentFrameCopy.Release();
				pCurrentFrameCopy.Attach(pCroppedFrameCopy);
			}

			if (IsSnapshotsWithVideoEnabled() && IsTimeToTakeSnapshot()) {
				TakeSnapshotsWithVideo(pCurrentFrameCopy, sourceFrameDesc, videoOutputFrameRect);
			}

			FrameWriteModel model;
			RtlZeroMemory(&model, sizeof(model));
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
			if (m_IsFixedFramerate)
			{
				wait(static_cast<UINT32>(max(videoFrameDurationMillis - duration_cast<milliseconds>(chrono::steady_clock::now() - lastFrame).count(), 0)));
			}
		}
	}

	//Push the last frame waiting to be recorded to the sink writer.
	if (pPreviousFrameCopy != nullptr) {
		INT64 duration = duration_cast<nanoseconds>(chrono::steady_clock::now() - lastFrame).count() / 100;
		if (gotMousePointer) {
			DrawMousePointer(pPreviousFrameCopy, pMousePointer.get(), PtrInfo, screenRotation, duration);
		}
		if ((m_RecorderMode == MODE_SLIDESHOW || m_RecorderMode == MODE_SNAPSHOT) && !isDestRectEqualToSourceRect) {
			ID3D11Texture2D *pCroppedFrameCopy;
			RETURN_ON_BAD_HR(hr = CropFrame(pPreviousFrameCopy, destFrameDesc, videoOutputFrameRect, &pCroppedFrameCopy));
			pPreviousFrameCopy.Release();
			pPreviousFrameCopy.Attach(pCroppedFrameCopy);
		}
		FrameWriteModel model;
		RtlZeroMemory(&model, sizeof(model));
		model.Frame = pPreviousFrameCopy;
		model.Duration = duration;
		model.StartPos = lastFrameStartPos;
		model.Audio = GrabAudioFrame(pLoopbackCaptureOutputDevice, pLoopbackCaptureInputDevice);
		model.FrameNumber = frameNr;
		hr = m_EncoderResult = RenderFrame(model);
	}
	return hr;
}

std::vector<BYTE> internal_recorder::GrabAudioFrame(_In_opt_ std::unique_ptr<loopback_capture> &pLoopbackCaptureOutputDevice, _In_opt_
	std::unique_ptr<loopback_capture> &pLoopbackCaptureInputDevice)
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

HRESULT internal_recorder::InitializeDesc(RECT outputRect, _Out_ D3D11_TEXTURE2D_DESC * pSourceFrameDesc, _Out_ D3D11_TEXTURE2D_DESC * pDestFrameDesc, _Out_ RECT * pSourceRect, _Out_ RECT * pDestRect) {
	UINT monitorWidth = outputRect.right - outputRect.left;
	UINT monitorHeight = outputRect.bottom - outputRect.top;

	RECT sourceRect;
	sourceRect.left = 0;
	sourceRect.right = monitorWidth;
	sourceRect.top = 0;
	sourceRect.bottom = monitorHeight;

	RECT destRect = sourceRect;
	if (m_DestRect.right != 0
		|| m_DestRect.top != 0
		|| m_DestRect.bottom != 0
		|| m_DestRect.left != 0)
	{
		destRect = m_DestRect;
	}

	D3D11_TEXTURE2D_DESC sourceFrameDesc;
	sourceFrameDesc.Width = monitorWidth;
	sourceFrameDesc.Height = monitorHeight;
	sourceFrameDesc.Format = DXGI_FORMAT::DXGI_FORMAT_B8G8R8A8_UNORM;
	sourceFrameDesc.ArraySize = 1;
	sourceFrameDesc.BindFlags = D3D11_BIND_FLAG::D3D11_BIND_RENDER_TARGET;
	sourceFrameDesc.MiscFlags = 0;
	sourceFrameDesc.SampleDesc.Count = 1;
	sourceFrameDesc.SampleDesc.Quality = 0;
	sourceFrameDesc.MipLevels = 1;
	sourceFrameDesc.CPUAccessFlags = 0;
	sourceFrameDesc.Usage = D3D11_USAGE_DEFAULT;

	D3D11_TEXTURE2D_DESC destFrameDesc;
	destFrameDesc.Width = destRect.right - destRect.left;
	destFrameDesc.Height = destRect.bottom - destRect.top;
	destFrameDesc.Format = DXGI_FORMAT::DXGI_FORMAT_B8G8R8A8_UNORM;
	destFrameDesc.ArraySize = 1;
	destFrameDesc.BindFlags = D3D11_BIND_FLAG::D3D11_BIND_RENDER_TARGET;
	destFrameDesc.MiscFlags = 0;
	destFrameDesc.SampleDesc.Count = 1;
	destFrameDesc.SampleDesc.Quality = 0;
	destFrameDesc.MipLevels = 1;
	destFrameDesc.CPUAccessFlags = 0;
	destFrameDesc.Usage = D3D11_USAGE_DEFAULT;

	*pSourceRect = sourceRect;
	*pDestRect = destRect;
	*pSourceFrameDesc = sourceFrameDesc;
	*pDestFrameDesc = destFrameDesc;
	return S_OK;
}

HRESULT internal_recorder::InitializeDx(_Outptr_ ID3D11DeviceContext * *ppContext, _Outptr_ ID3D11Device * *ppDevice) {
	*ppContext = nullptr;
	*ppDevice = nullptr;

	HRESULT hr(S_OK);
	DXGI_OUTDUPL_DESC OutputDuplDesc;
	RtlZeroMemory(&OutputDuplDesc, sizeof(OutputDuplDesc));
	CComPtr<ID3D11DeviceContext> m_ImmediateContext = nullptr;
	CComPtr<ID3D11Device> pDevice = nullptr;
	CComPtr<IDXGIOutputDuplication> pDeskDupl = nullptr;
	int lresult(-1);
	D3D_FEATURE_LEVEL featureLevel;

	CComPtr<IDXGIAdapter> pDxgiAdapter = nullptr;
	std::vector<D3D_DRIVER_TYPE> driverTypes;
	if (pDxgiAdapter) {
		driverTypes.push_back(D3D_DRIVER_TYPE_UNKNOWN);
	}
	else
	{
		for (D3D_DRIVER_TYPE type : gDriverTypes)
		{
			driverTypes.push_back(type);
		}
	}
	UINT numDriverTypes = driverTypes.size();
	UINT creationFlagsDebug = D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_DEBUG;
	UINT creationFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
	// Create devices
	for (UINT DriverTypeIndex = 0; DriverTypeIndex < numDriverTypes; ++DriverTypeIndex)
	{
		hr = D3D11CreateDevice(
			pDxgiAdapter,
			driverTypes[DriverTypeIndex],
			nullptr,
#if _DEBUG 
			creationFlagsDebug,
#else
			creationFlags,
#endif
			m_FeatureLevels,
			ARRAYSIZE(m_FeatureLevels),
			D3D11_SDK_VERSION,
			&pDevice,
			&featureLevel,
			&m_ImmediateContext);

		if (SUCCEEDED(hr))
		{
#if _DEBUG 
			HRESULT hr = pDevice->QueryInterface(IID_PPV_ARGS(&m_Debug));
#endif
			// Device creation success, no need to loop anymore
			break;
		}
	}

	RETURN_ON_BAD_HR(hr);
	CComPtr<ID3D10Multithread> pMulti = nullptr;
	hr = m_ImmediateContext->QueryInterface(IID_PPV_ARGS(&pMulti));
	RETURN_ON_BAD_HR(hr);
	pMulti->SetMultithreadProtected(TRUE);
	pMulti.Release();
	if (pDevice == nullptr)
		return E_FAIL;

	// Return the pointer to the caller.
	*ppContext = m_ImmediateContext;
	(*ppContext)->AddRef();
	*ppDevice = pDevice;
	(*ppDevice)->AddRef();

	return hr;
}

//
// Set new viewport
//
void internal_recorder::SetViewPort(_In_ ID3D11DeviceContext * deviceContext, _In_ UINT Width, _In_ UINT Height)
{
	D3D11_VIEWPORT VP;
	VP.Width = static_cast<FLOAT>(Width);
	VP.Height = static_cast<FLOAT>(Height);
	VP.MinDepth = 0.0f;
	VP.MaxDepth = 1.0f;
	VP.TopLeftX = 0;
	VP.TopLeftY = 0;
	deviceContext->RSSetViewports(1, &VP);
}
/// <summary>
/// MFSinkWriter crashes if source or destination dimensions are odd numbers. This method forces the dimensions of a RECT to be even by subtracting one pixel if odd.
/// </summary>
/// <param name="rect"></param>
RECT internal_recorder::MakeRectEven(_In_ RECT rect)
{
	if ((rect.right - rect.left) % 2 != 0)
		rect.right -= 1;
	if ((rect.bottom - rect.top) % 2 != 0)
		rect.bottom -= 1;
	return rect;
}

HRESULT internal_recorder::GetOutputForDeviceName(_In_ std::wstring deviceName, _Outptr_opt_result_maybenull_ IDXGIOutput **ppOutput) {
	HRESULT hr = S_FALSE;
	if (ppOutput) {
		*ppOutput = nullptr;
	}
	if (deviceName != L"") {
		std::vector<CComPtr<IDXGIAdapter>> adapters = EnumDisplayAdapters();
		for (CComPtr<IDXGIAdapter> adapter : adapters)
		{
			IDXGIOutput *pOutput;
			int i = 0;
			while (adapter->EnumOutputs(i, &pOutput) != DXGI_ERROR_NOT_FOUND)
			{
				DXGI_OUTPUT_DESC desc;
				RETURN_ON_BAD_HR(pOutput->GetDesc(&desc));

				if (desc.DeviceName == deviceName) {
					// Return the pointer to the caller.
					hr = S_OK;
					if (ppOutput) {
						*ppOutput = pOutput;
						(*ppOutput)->AddRef();
					}
					break;
				}
				SafeRelease(&pOutput);
				i++;
			}
			if (ppOutput && *ppOutput) {
				break;
			}
		}
	}
	return hr;
}

std::vector<CComPtr<IDXGIAdapter>> internal_recorder::EnumDisplayAdapters()
{
	std::vector<CComPtr<IDXGIAdapter>> vAdapters;
	IDXGIFactory1 * pFactory;
	HRESULT hr = CreateDXGIFactory1(__uuidof(IDXGIFactory), (void**)(&pFactory));
	if (SUCCEEDED(hr)) {
		UINT i = 0;
		IDXGIAdapter *pAdapter;
		while (pFactory->EnumAdapters(i, &pAdapter) != DXGI_ERROR_NOT_FOUND)
		{
			vAdapters.push_back(CComPtr<IDXGIAdapter>(pAdapter));
			SafeRelease(&pAdapter);
			++i;
		}
	}
	SafeRelease(&pFactory);
	return vAdapters;
}

HRESULT internal_recorder::InitializeVideoSinkWriter(
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

	UINT sourceWidth = max(0, sourceRect.right - sourceRect.left);
	UINT sourceHeight = max(0, sourceRect.bottom - sourceRect.top);

	UINT destWidth = max(0, destRect.right - destRect.left);
	UINT destHeight = max(0, destRect.bottom - destRect.top);

	RETURN_ON_BAD_HR(ConfigureOutputMediaTypes(destWidth, destHeight, &pVideoMediaTypeOut, &pAudioMediaTypeOut));
	RETURN_ON_BAD_HR(ConfigureInputMediaTypes(sourceWidth, sourceHeight, rotationFormat, pVideoMediaTypeOut, &pVideoMediaTypeIn, &pAudioMediaTypeIn));

	//Creates a streaming writer
	CComPtr<IMFMediaSink> pMp4StreamSink = nullptr;
	if (m_IsFragmentedMp4Enabled) {
		RETURN_ON_BAD_HR(MFCreateFMPEG4MediaSink(pOutStream, pVideoMediaTypeOut, pAudioMediaTypeOut, &pMp4StreamSink));
	}
	else {
		RETURN_ON_BAD_HR(MFCreateMPEG4MediaSink(pOutStream, pVideoMediaTypeOut, pAudioMediaTypeOut, &pMp4StreamSink));
	}
	pAudioMediaTypeOut.Release();

	// Passing 6 as the argument to save re-allocations
	RETURN_ON_BAD_HR(MFCreateAttributes(&pAttributes, 7));
	RETURN_ON_BAD_HR(pAttributes->SetGUID(MF_TRANSCODE_CONTAINERTYPE, MFTranscodeContainerType_MPEG4));
	RETURN_ON_BAD_HR(pAttributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, m_IsHardwareEncodingEnabled));
	RETURN_ON_BAD_HR(pAttributes->SetUINT32(MF_MPEG4SINK_MOOV_BEFORE_MDAT, m_IsMp4FastStartEnabled));
	RETURN_ON_BAD_HR(pAttributes->SetUINT32(MF_LOW_LATENCY, m_IsLowLatencyModeEnabled));
	RETURN_ON_BAD_HR(pAttributes->SetUINT32(MF_SINK_WRITER_DISABLE_THROTTLING, m_IsThrottlingDisabled));
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
		RETURN_ON_BAD_HR(SetAttributeU32(encoder, CODECAPI_AVEncCommonRateControlMode, m_VideoBitrateControlMode));
		switch (m_VideoBitrateControlMode) {
		case eAVEncCommonRateControlMode_Quality:
			RETURN_ON_BAD_HR(SetAttributeU32(encoder, CODECAPI_AVEncCommonQuality, m_VideoQuality));
			break;
		default:
			break;
		}
	}

	if (destWidth != sourceWidth || destHeight != sourceHeight) {

		CComPtr<IMFVideoProcessorControl> videoProcessor = nullptr;
		GetVideoProcessor(pSinkWriter, videoStreamIndex, &videoProcessor);
		if (videoProcessor) {
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

HRESULT internal_recorder::ConfigureOutputMediaTypes(
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
	RETURN_ON_BAD_HR(pVideoMediaType->SetGUID(MF_MT_SUBTYPE, VIDEO_ENCODING_FORMAT));
	RETURN_ON_BAD_HR(pVideoMediaType->SetUINT32(MF_MT_AVG_BITRATE, m_VideoBitrate));
	RETURN_ON_BAD_HR(pVideoMediaType->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive));
	RETURN_ON_BAD_HR(pVideoMediaType->SetUINT32(MF_MT_MPEG2_PROFILE, m_H264Profile));
	RETURN_ON_BAD_HR(pVideoMediaType->SetUINT32(MF_MT_YUV_MATRIX, MFVideoTransferMatrix_BT601));
	RETURN_ON_BAD_HR(MFSetAttributeSize(pVideoMediaType, MF_MT_FRAME_SIZE, destWidth, destHeight));
	RETURN_ON_BAD_HR(MFSetAttributeRatio(pVideoMediaType, MF_MT_FRAME_RATE, m_VideoFps, 1));
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

HRESULT internal_recorder::ConfigureInputMediaTypes(
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

HRESULT internal_recorder::InitializeAudioCapture(_Outptr_result_maybenull_ loopback_capture **outputAudioCapture, _Outptr_result_maybenull_ loopback_capture **inputAudioCapture)
{
	loopback_capture *pLoopbackCaptureOutputDevice = nullptr;
	loopback_capture *pLoopbackCaptureInputDevice = nullptr;
	HRESULT hr;
	bool recordAudio = m_RecorderMode == MODE_VIDEO && m_IsAudioEnabled;
	if (recordAudio && (m_IsOutputDeviceEnabled || m_IsInputDeviceEnabled)) {
		if (m_IsOutputDeviceEnabled)
		{
			pLoopbackCaptureOutputDevice = new loopback_capture(L"AudioOutputDevice");
			hr = pLoopbackCaptureOutputDevice->StartCapture(AUDIO_SAMPLES_PER_SECOND, m_AudioChannels, m_AudioOutputDevice, eRender);
		}

		if (m_IsInputDeviceEnabled)
		{
			pLoopbackCaptureInputDevice = new loopback_capture(L"AudioInputDevice");
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

void internal_recorder::InitializeMouseClickDetection()
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

HRESULT internal_recorder::CreateInputMediaTypeFromOutput(
	_In_ IMFMediaType * pType,    // Pointer to an encoded video type.
	_In_ const GUID & subtype,    // Uncompressed subtype (eg, RGB-32, AYUV)
	_Outptr_ IMFMediaType * *ppType   // Receives a matching uncompressed video type.
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
			(UINT32*)&par.Numerator,
			(UINT32*)&par.Denominator
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

HRESULT internal_recorder::DrawMousePointer(_In_ ID3D11Texture2D * frame, _In_ mouse_pointer * pMousePointer, _In_ PTR_INFO ptrInfo, _In_ DXGI_MODE_ROTATION screenRotation, _In_ INT64 durationSinceLastFrame100Nanos)
{
	HRESULT hr = S_FALSE;
	if (g_LastMouseClickDurationRemaining > 0
		&& m_IsMouseClicksDetected)
	{
		if (g_LastMouseClickButton == VK_LBUTTON)
		{
			hr = pMousePointer->DrawMouseClick(&ptrInfo, frame, m_MouseClickDetectionLMBColor, (float)m_MouseClickDetectionRadius, screenRotation);
		}
		if (g_LastMouseClickButton == VK_RBUTTON)
		{
			hr = pMousePointer->DrawMouseClick(&ptrInfo, frame, m_MouseClickDetectionRMBColor, (float)m_MouseClickDetectionRadius, screenRotation);
		}
		INT64 millis = max(HundredNanosToMillis(durationSinceLastFrame100Nanos), 0);
		g_LastMouseClickDurationRemaining = max(g_LastMouseClickDurationRemaining - millis, 0);
		LOG_DEBUG("Drawing mouse click, duration remaining on click is %u ms", g_LastMouseClickDurationRemaining);
	}

	if (m_IsMousePointerEnabled) {
		hr = pMousePointer->DrawMousePointer(&ptrInfo, m_ImmediateContext, m_Device, frame, screenRotation);
	}
	return hr;
}

HRESULT internal_recorder::CropFrame(_In_ ID3D11Texture2D *frame, _In_ D3D11_TEXTURE2D_DESC frameDesc, _In_ RECT destRect, _Outptr_ ID3D11Texture2D **pCroppedFrame)
{
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

HRESULT internal_recorder::CreateCaptureItem(_Out_ GraphicsCaptureItem *item)
{
	if (m_WindowHandle != nullptr) {
		*item = capture::util::CreateCaptureItemForWindow(m_WindowHandle);
	}
	else {

		std::wstring displayName = L"";
		if (m_DisplayOutputDevices.size() > 0) {
			displayName = m_DisplayOutputDevices[0];
		}
		auto pMonitorList = std::make_unique<monitor_list>(false);
		auto monitor = pMonitorList->GetMonitorForDisplayName(displayName);
		if (!monitor.has_value()) {
			if (pMonitorList->GetCurrentMonitors().size() == 0) {
				LOG_ERROR("Failed to find any monitors to record");
				return E_FAIL;
			}
			monitor = pMonitorList->GetCurrentMonitors().at(0);
		}
		LOG_INFO(L"Recording monitor %ls using Windows.Graphics.Capture", displayName.c_str());
		*item = capture::util::CreateCaptureItemForMonitor(monitor->MonitorHandle);
	}
	return S_OK;
}

HRESULT internal_recorder::GetVideoProcessor(_In_ IMFSinkWriter *pSinkWriter, _In_ DWORD streamIndex, _Outptr_ IMFVideoProcessorControl **pVideoProcessor)
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

HRESULT internal_recorder::SetAttributeU32(_Inout_ CComPtr<ICodecAPI> & codec, _In_ const GUID & guid, _In_ UINT32 value)
{
	VARIANT val;
	val.vt = VT_UI4;
	val.uintVal = value;
	return codec->SetValue(&guid, &val);
}

HRESULT internal_recorder::RenderFrame(_In_ FrameWriteModel & model) {
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
			hr = WriteAudioSamplesToVideo(model.StartPos, model.Duration, m_AudioStreamIndex, &(model.Audio)[0], model.Audio.size());
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
		LOG_TRACE(L"Wrote %s with start pos %lld ms and with duration %lld ms", frameInfoStr, HundredNanosToMillis(model.StartPos), HundredNanosToMillis(model.Duration));
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

std::string internal_recorder::CurrentTimeToFormattedString()
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
HRESULT internal_recorder::WriteFrameToImage(_In_ ID3D11Texture2D * pAcquiredDesktopImage, _In_ std::wstring filePath)
{
	return SaveWICTextureToFile(m_ImmediateContext, pAcquiredDesktopImage, m_ImageEncoderFormat, filePath.c_str());
}

void internal_recorder::WriteFrameToImageAsync(_In_ ID3D11Texture2D* pAcquiredDesktopImage, _In_ std::wstring filePath)
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
				catch (const exception & e) {
					// handle error
					LOG_ERROR(L"Exception saving snapshot: %s", e.what());
				}
				pAcquiredDesktopImage->Release();
			});
}
/// <summary>
/// Take screenshots in a video recording, if video recording is file mode.
/// </summary>
HRESULT internal_recorder::TakeSnapshotsWithVideo(_In_ ID3D11Texture2D* frame, _In_ D3D11_TEXTURE2D_DESC frameDesc, _In_ RECT destRect)
{
	if (m_OutputSnapshotsFolderPath.empty())
		return S_FALSE;

	HRESULT hr = S_OK;
	CComPtr<ID3D11Texture2D> m_pFrameCopyForSnapshotsWithVideo = nullptr;

	int destWidth = destRect.right - destRect.left;
	int destHeight = destRect.bottom - destRect.top;
	if (frameDesc.Width != destWidth
		|| frameDesc.Height != destHeight) {
		//If the source frame is larger than the destionation rect, we crop it, to avoid black borders around the snapshots.
		frameDesc.Width = min((UINT)destWidth, frameDesc.Width);
		frameDesc.Height = min((UINT)destHeight, frameDesc.Height);
		RETURN_ON_BAD_HR(hr = CropFrame(frame, frameDesc, destRect, &m_pFrameCopyForSnapshotsWithVideo));
	}
	else {
		m_Device->CreateTexture2D(&frameDesc, nullptr, &m_pFrameCopyForSnapshotsWithVideo);
		// Copy the current frame for a separate thread to write it to a file asynchronously.
		m_ImmediateContext->CopyResource(m_pFrameCopyForSnapshotsWithVideo, frame);
	}

	m_previousSnapshotTaken = steady_clock::now();
	wstring snapshotPath = m_OutputSnapshotsFolderPath + L"\\" + s2ws(CurrentTimeToFormattedString()) + GetImageExtension();
	WriteFrameToImageAsync(m_pFrameCopyForSnapshotsWithVideo, snapshotPath.c_str());
	return hr;
}

HRESULT internal_recorder::WriteFrameToVideo(_In_ INT64 frameStartPos, _In_ INT64 frameDuration, _In_ DWORD streamIndex, _In_ ID3D11Texture2D * pAcquiredDesktopImage)
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
HRESULT internal_recorder::WriteAudioSamplesToVideo(_In_ INT64 frameStartPos, _In_ INT64 frameDuration, _In_ DWORD streamIndex, _In_ BYTE * pSrc, _In_ DWORD cbData)
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

void internal_recorder::SetDebugName(_In_ ID3D11DeviceChild * child, _In_ const std::string & name)
{
#if _DEBUG
	if (child != nullptr)
		child->SetPrivateData(WKPDID_D3DDebugObjectName, name.size(), name.c_str());
#endif
}