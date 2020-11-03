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
		WARN("Recording is in progress while destructing, cancelling recording task and waiting for completion.");
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

std::vector<BYTE> internal_recorder::MixAudio(std::vector<BYTE> const &first, std::vector<BYTE> const &second, float firstVolume, float secondVolume)
{
	std::vector<BYTE> newvector(max(first.size(), second.size()));

	for (size_t i = 0; i < newvector.size(); i += 2) {

		short buf1A = 0;
		short buf2A = 0;

		if (i + 1 < first.size())
		{
			buf1A = first[i + 1];
			buf2A = first[i];
			buf1A = (short)((buf1A & 0xff) << 8);
			buf2A = (short)(buf2A & 0xff);
		}

		short buf1B = 0;
		short buf2B = 0;

		if (i + 1 < second.size())
		{
			buf1B = second[i + 1];
			buf2B = second[i];
			buf1B = (short)((buf1B & 0xff) << 8);
			buf2B = (short)(buf2B & 0xff);
		}

		short buf1C = (short)round(buf1A * firstVolume + buf1B * secondVolume);
		short buf2C = (short)round(buf2A * firstVolume + buf2B * secondVolume);

		short res = (short)(buf1C + buf2C);

		newvector[i] = (BYTE)res;
		newvector[i + 1] = (BYTE)(res >> 8);
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
		WARN("Image encoder format not recognized, defaulting to .jpg extension");
		return L".jpg";
	}
}

std::wstring internal_recorder::GetVideoExtension() {
	return L".mp4";
}

HRESULT internal_recorder::ConfigureOutputDir(std::wstring path) {
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
		DEBUG(L"output folder is ready");
		m_OutputFolder = directory;
	}
	else
	{
		// Failed to create directory.
		ERROR(L"failed to create output folder");
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

HRESULT internal_recorder::BeginRecording(IStream *stream) {
	return BeginRecording(L"", stream);
}

HRESULT internal_recorder::BeginRecording(std::wstring path) {
	return BeginRecording(path, nullptr);
}

HRESULT internal_recorder::BeginRecording(std::wstring path, IStream *stream) {

	if (!IsWindows8OrGreater()) {
		wstring errorText = L"Windows 8 or higher is required";
		ERROR(L"%ls", errorText);
		RecordingFailedCallback(errorText);
		return E_FAIL;
	}

	if (m_IsRecording) {
		if (m_IsPaused) {
			m_IsPaused = false;
			if (RecordingStatusChangedCallback != nullptr) {
				RecordingStatusChangedCallback(STATUS_RECORDING);
				DEBUG("Changed Recording Status to Recording");
			}
		}
		std::wstring error = L"Recording is already in progress, aborting";
		WARN("%ls", error.c_str());
		if (RecordingFailedCallback != nullptr)
			RecordingFailedCallback(error);
		return S_FALSE;
	}

	m_EncoderResult = S_FALSE;
	m_LastFrameHadAudio = false;
	m_FrameDelays.clear();
	if (!path.empty()) {
		RETURN_ON_BAD_HR(ConfigureOutputDir(path));
	}

	m_TaskWrapperImpl->m_RecordTaskCts = cancellation_token_source();
	m_TaskWrapperImpl->m_RecordTask = concurrency::create_task([this, stream]() {
		INFO(L"Starting recording task");
		m_IsRecording = true;
		HRESULT hr = CoInitializeEx(nullptr, COINITBASE_MULTITHREADED | COINIT_DISABLE_OLE1DDE);
		RETURN_ON_BAD_HR(hr);
		RETURN_ON_BAD_HR(hr = MFStartup(MF_VERSION, MFSTARTUP_LITE));
		InitializeMouseClickDetection();
		CComPtr<IDXGIOutput> pSelectedOutput = nullptr;
		hr = GetOutputForDeviceName(m_DisplayOutputName, &pSelectedOutput);
		RETURN_ON_BAD_HR(hr = InitializeDx(pSelectedOutput, &m_ImmediateContext, &m_Device));

		if (RecordingStatusChangedCallback != nullptr) {
			RecordingStatusChangedCallback(STATUS_RECORDING);
			DEBUG("Changed Recording Status to Recording");
		}

		g_LastMouseClickDurationRemaining = 0;

		if (m_RecorderApi == API_GRAPHICS_CAPTURE) {
			RETURN_ON_BAD_HR(hr = StartGraphicsCaptureRecorderLoop(stream));
		}
		else if (m_RecorderApi == API_DESKTOP_DUPLICATION) {
			RETURN_ON_BAD_HR(hr = StartDesktopDuplicationRecorderLoop(stream, pSelectedOutput));
		}
		INFO("Exiting recording task");
		return hr;
	})
		.then([this](HRESULT recordingResult) {
		INFO("Cleaning up resources");
		HRESULT cleanupResult = S_OK;
		if (m_SinkWriter) {
			cleanupResult = m_SinkWriter->Finalize();
			if (FAILED(cleanupResult)) {
				ERROR("Failed to finalize sink writer");
			}
			//Dispose of MPEG4MediaSink 
			IMFMediaSink *pSink;
			if (SUCCEEDED(m_SinkWriter->GetServiceForStream(MF_SINK_WRITER_MEDIASINK, GUID_NULL, IID_PPV_ARGS(&pSink)))) {
				cleanupResult = pSink->Shutdown();
				if (FAILED(cleanupResult)) {
					ERROR("Failed to shut down IMFMediaSink");
				}
				else {
					DEBUG("Shut down IMFMediaSink");
				}
			};

			SafeRelease(&m_SinkWriter);
			DEBUG("Released IMFSinkWriter");
		}
		SafeRelease(&m_ImmediateContext);
		DEBUG("Released ID3D11DeviceContext");
		SafeRelease(&m_Device);
		DEBUG("Released ID3D11Device");
#if _DEBUG
		if (m_Debug) {
			m_Debug->ReportLiveDeviceObjects(D3D11_RLDO_DETAIL);
			SafeRelease(&m_Debug);
			DEBUG("Released ID3D11Debug");
		}
#endif
		if (SUCCEEDED(recordingResult) && FAILED(cleanupResult))
			return cleanupResult;
		else
			return recordingResult;
	})
		.then([this](concurrency::task<HRESULT> t)
	{
		MFShutdown();
		CoUninitialize();
		UnhookWindowsHookEx(m_Mousehook);
		INFO(L"Media Foundation shut down");
		std::wstring errMsg = L"";
		bool success = false;
		try {
			HRESULT hr = t.get();
			success = SUCCEEDED(hr);
			if (!success) {
				_com_error err(hr);
				errMsg = err.ErrorMessage();
			}
			// if .get() didn't throw and the HRESULT succeeded, there are no errors.
		}
		catch (const exception & e) {
			// handle error
			ERROR(L"Exception in RecordTask: %s", e.what());
		}
		catch (...) {
			ERROR(L"Exception in RecordTask");
		}

		if (RecordingStatusChangedCallback) {
			RecordingStatusChangedCallback(STATUS_IDLE);
			DEBUG("Changed Recording Status to Idle");
		}
		m_IsRecording = false;
		if (success) {
			if (RecordingCompleteCallback)
				RecordingCompleteCallback(m_OutputFullPath, m_FrameDelays);
			DEBUG("Sent Recording Complete callback");
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
				DEBUG("Sent Recording Failed callback");
			}
		}
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
			DEBUG("Changed Recording Status to Paused");
		}
	}
}
void internal_recorder::ResumeRecording() {
	if (m_IsRecording) {
		if (m_IsPaused) {
			m_IsPaused = false;
			if (RecordingStatusChangedCallback != nullptr) {
				RecordingStatusChangedCallback(STATUS_RECORDING);
				DEBUG("Changed Recording Status to Recording");
			}
		}
	}
}

HRESULT internal_recorder::StartGraphicsCaptureRecorderLoop(IStream *pStream)
{
	std::unique_ptr<loopback_capture> pLoopbackCaptureOutputDevice = nullptr;
	std::unique_ptr<loopback_capture> pLoopbackCaptureInputDevice = nullptr;
	CComPtr<ID3D11Texture2D> pFrameCopyForSnapshotsWithVideo = nullptr;
	CComPtr<ID3D11Texture2D> pPreviousFrameCopy = nullptr;
	auto isCaptureSupported = winrt::Windows::Graphics::Capture::GraphicsCaptureSession::IsSupported();
	if (!isCaptureSupported)
	{
		wstring error = L"Windows Graphics Capture API is not supported on this version of Windows";
		ERROR(L"%ls", error.c_str());
		if (RecordingFailedCallback != nullptr)
			RecordingFailedCallback(error);
		return E_FAIL;
	}
	std::chrono::high_resolution_clock::time_point	lastFrame = std::chrono::high_resolution_clock::now();
	std::chrono::system_clock::time_point previousTimeSnapshotTaken = std::chrono::system_clock::from_time_t(0);
	int totalCachedFrameDuration = 0;
	HRESULT hr;
	CComPtr<IDXGIDevice> pDxgiDevice;
	RETURN_ON_BAD_HR(hr = m_Device->QueryInterface(IID_PPV_ARGS(&pDxgiDevice)));
	auto pDevice = capture::util::CreateDirect3DDevice(pDxgiDevice);
	GraphicsCaptureItem captureItem = nullptr;
	RETURN_ON_BAD_HR(hr = CreateCaptureItem(&captureItem));
	auto pCapture = std::make_unique<graphics_capture>(pDevice, captureItem, DirectXPixelFormat::B8G8R8A8UIntNormalized, nullptr);
	pCapture->StartCapture();
	D3D11_TEXTURE2D_DESC sourceFrameDesc;
	RtlZeroMemory(&sourceFrameDesc, sizeof(sourceFrameDesc));

	RECT sourceRect, destRect;
	RtlZeroMemory(&sourceRect, sizeof(sourceRect));
	RtlZeroMemory(&destRect, sizeof(destRect));
	sourceRect.left = 0;
	sourceRect.top = 0;
	sourceRect.right = captureItem.Size().Width;
	sourceRect.bottom = captureItem.Size().Height;
	sourceRect = MakeRectEven(sourceRect);
	destRect = sourceRect;

	if (m_RecorderMode == MODE_VIDEO) {
		loopback_capture *outputCapture = nullptr;
		loopback_capture *inputCapture = nullptr;
		hr = InitializeAudioCapture(&outputCapture, &inputCapture);
		if (SUCCEEDED(hr)) {
			pLoopbackCaptureOutputDevice = std::unique_ptr<loopback_capture>(outputCapture);
			pLoopbackCaptureInputDevice = std::unique_ptr<loopback_capture>(inputCapture);
		}
		else {
			ERROR(L"Audio capture failed to start: hr = 0x%08x", hr);
		}
		CComPtr<IMFByteStream> outputStream = nullptr;
		if (pStream != nullptr) {
			RETURN_ON_BAD_HR(hr = MFCreateMFByteStreamOnStream(pStream, &outputStream));
		}
		RETURN_ON_BAD_HR(hr = InitializeVideoSinkWriter(m_OutputFullPath, outputStream, m_Device, sourceRect, destRect, DXGI_MODE_ROTATION_UNSPECIFIED, &m_SinkWriter, &m_VideoStreamIndex, &m_AudioStreamIndex));
	}

	INT64 videoFrameDurationMillis = 1000 / m_VideoFps;
	INT64 videoFrameDuration100Nanos = MillisToHundredNanos(videoFrameDurationMillis);
	INT frameTimeout = 0;
	INT frameNr = 0;
	INT64 lastFrameStartPos = 0;
	cancellation_token token = m_TaskWrapperImpl->m_RecordTaskCts.get_token();
	while (true) {
		if (token.is_canceled()) {
			DEBUG("Recording task was cancelled");
			hr = S_OK;
			break;
		}
		if (m_IsPaused) {
			wait(10);
			lastFrame = high_resolution_clock::now();
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
		}
		else if (pPreviousFrameCopy == nullptr) {
			continue;
		}

		INT64 durationSinceLastFrame100Nanos = max(duration_cast<nanoseconds>(chrono::high_resolution_clock::now() - lastFrame).count() / 100, 0);

		//Delay frames that comes quicker than selected framerate to see if we can skip them.
		if (frameNr > 0 //always draw first frame 
			&& !m_IsFixedFramerate)
		{
			bool delay = false;
			if (SUCCEEDED(hr) && durationSinceLastFrame100Nanos < videoFrameDuration100Nanos) {
				if (surfaceTexture) {
					//we got a frame, but it's too soon, so we cache it and see if there are more changes.
					if (pPreviousFrameCopy == nullptr) {
						RETURN_ON_BAD_HR(hr = m_Device->CreateTexture2D(&sourceFrameDesc, nullptr, &pPreviousFrameCopy));
					}
					m_ImmediateContext->CopyResource(pPreviousFrameCopy, surfaceTexture.get());
				}
				delay = true;
			}
			else if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
				if (durationSinceLastFrame100Nanos < m_MaxFrameLength100Nanos) {
					delay = true;
				}
			}
			if (delay) {
				UINT64 delay = 1;
				if (durationSinceLastFrame100Nanos < videoFrameDuration100Nanos) {
					delay = HundredNanosToMillis((videoFrameDuration100Nanos - durationSinceLastFrame100Nanos));
				}
				wait(delay);
				continue;
			}
		}

		lastFrame = high_resolution_clock::now();
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
			totalCachedFrameDuration = 0;
		}
		else if (pPreviousFrameCopy) {
			m_ImmediateContext->CopyResource(pFrameCopy, pPreviousFrameCopy);
			totalCachedFrameDuration += durationSinceLastFrame100Nanos;
		}

		SetDebugName(pFrameCopy, "FrameCopy");

		// Take screenshots in a video recording, if video recording is file mode.
		if (IsSnapshotsWithVideoEnabled() && !m_OutputSnapshotsFolderPath.empty()) {
			const auto now = std::chrono::system_clock::now();
			if (previousTimeSnapshotTaken == std::chrono::system_clock::from_time_t(0) ||
				now - previousTimeSnapshotTaken > m_SnapshotsWithVideoInterval) {
				previousTimeSnapshotTaken = now;
				if (pFrameCopyForSnapshotsWithVideo == nullptr)
					RETURN_ON_BAD_HR(hr = m_Device->CreateTexture2D(&sourceFrameDesc, nullptr, &pFrameCopyForSnapshotsWithVideo));
				wstring snapshotPath = m_OutputSnapshotsFolderPath + L"\\" + s2ws(CurrentTimeToFormattedString()) + GetImageExtension();

				// Copy the current frame for a separate thread to write it to a file asynchronously.
				// Assuming previous file writing is already done.
				m_ImmediateContext->CopyResource(pFrameCopyForSnapshotsWithVideo, pFrameCopy);
				WriteFrameToImageAsync(pFrameCopyForSnapshotsWithVideo, snapshotPath.c_str());
			}
		}

		if (token.is_canceled()) {
			DEBUG("Recording task was cancelled");
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

		if (m_RecorderMode == MODE_SNAPSHOT) {
			break;
		}
		frameNr++;
		lastFrameStartPos += durationSinceLastFrame100Nanos;
		if (m_IsFixedFramerate)
		{
			wait(static_cast<UINT32>(max(videoFrameDurationMillis - duration_cast<milliseconds>(chrono::high_resolution_clock::now() - lastFrame).count(), 0)));
		}
	}
	return hr;
}

HRESULT internal_recorder::StartDesktopDuplicationRecorderLoop(IStream *pStream, IDXGIOutput *pSelectedOutput)
{
	std::unique_ptr<loopback_capture> pLoopbackCaptureOutputDevice = nullptr;
	std::unique_ptr<loopback_capture> pLoopbackCaptureInputDevice = nullptr;
	CComPtr<ID3D11Texture2D> pFrameCopyForSnapshotsWithVideo = nullptr;
	CComPtr<ID3D11Texture2D> pPreviousFrameCopy = nullptr;
	CComPtr<IDXGIResource> pDesktopResource = nullptr;
	bool gotMousePointer = false;
	int totalCachedFrameDuration = 0;

	HRESULT hr;
	DXGI_OUTDUPL_FRAME_INFO FrameInfo;
	RtlZeroMemory(&FrameInfo, sizeof(FrameInfo));
	DXGI_OUTDUPL_DESC outputDuplDesc;
	RtlZeroMemory(&outputDuplDesc, sizeof(outputDuplDesc));
	CComPtr<IDXGIOutputDuplication> pDeskDupl = nullptr;
	RETURN_ON_BAD_HR(hr = InitializeDesktopDupl(m_Device, pSelectedOutput, &pDeskDupl, &outputDuplDesc));
	DXGI_MODE_ROTATION screenRotation = outputDuplDesc.Rotation;
	D3D11_TEXTURE2D_DESC sourceFrameDesc;
	D3D11_TEXTURE2D_DESC destFrameDesc;
	RECT sourceRect, destRect;

	RtlZeroMemory(&destFrameDesc, sizeof(destFrameDesc));
	RtlZeroMemory(&sourceFrameDesc, sizeof(sourceFrameDesc));
	RtlZeroMemory(&sourceRect, sizeof(sourceRect));
	RtlZeroMemory(&destRect, sizeof(destRect));

	RETURN_ON_BAD_HR(hr = InitializeDesc(outputDuplDesc, &sourceFrameDesc, &destFrameDesc, &sourceRect, &destRect));
	bool isDestRectEqualToSourceRect = EqualRect(&sourceRect, &destRect);

	std::unique_ptr<mouse_pointer> pMousePointer = make_unique<mouse_pointer>();
	RETURN_ON_BAD_HR(hr = pMousePointer->Initialize(m_ImmediateContext, m_Device));
	SetViewPort(m_ImmediateContext, sourceRect.right - sourceRect.left, sourceRect.bottom - sourceRect.top);
	std::chrono::high_resolution_clock::time_point	lastFrame = std::chrono::high_resolution_clock::now();
	std::chrono::system_clock::time_point previousTimeSnapshotTaken = std::chrono::system_clock::from_time_t(0);

	mouse_pointer::PTR_INFO PtrInfo;
	RtlZeroMemory(&PtrInfo, sizeof(PtrInfo));

	if (m_RecorderMode == MODE_VIDEO) {
		loopback_capture *outputCapture = nullptr;
		loopback_capture *inputCapture = nullptr;
		hr = InitializeAudioCapture(&outputCapture, &inputCapture);
		if (SUCCEEDED(hr)) {
			pLoopbackCaptureOutputDevice = std::unique_ptr<loopback_capture>(outputCapture);
			pLoopbackCaptureInputDevice = std::unique_ptr<loopback_capture>(inputCapture);
		}
		else {
			ERROR(L"Audio capture failed to start: hr = 0x%08x", hr);
		}
		CComPtr<IMFByteStream> outputStream = nullptr;
		if (pStream != nullptr) {
			RETURN_ON_BAD_HR(hr = MFCreateMFByteStreamOnStream(pStream, &outputStream));
		}
		RETURN_ON_BAD_HR(hr = InitializeVideoSinkWriter(m_OutputFullPath, outputStream, m_Device, sourceRect, destRect, outputDuplDesc.Rotation, &m_SinkWriter, &m_VideoStreamIndex, &m_AudioStreamIndex));
	}

	INT64 videoFrameDurationMillis = 1000 / m_VideoFps;
	INT64 videoFrameDuration100Nanos = MillisToHundredNanos(videoFrameDurationMillis);
	INT frameTimeout = 0;
	INT frameNr = 0;
	INT64 lastFrameStartPos = 0;
	cancellation_token token = m_TaskWrapperImpl->m_RecordTaskCts.get_token();
	while (true)
	{
		CComPtr<IDXGIResource> pDesktopResource = nullptr;
		DXGI_OUTDUPL_FRAME_INFO FrameInfo;
		RtlZeroMemory(&FrameInfo, sizeof(FrameInfo));

		if (token.is_canceled()) {
			DEBUG("Recording task was cancelled");
			hr = S_OK;
			break;
		}

		if (m_IsPaused) {
			wait(10);
			lastFrame = high_resolution_clock::now();
			if (pLoopbackCaptureOutputDevice)
				pLoopbackCaptureOutputDevice->ClearRecordedBytes();
			if (pLoopbackCaptureInputDevice)
				pLoopbackCaptureInputDevice->ClearRecordedBytes();
			continue;
		}
		if (pDeskDupl) {
			pDeskDupl->ReleaseFrame();
			// Get new frame
			hr = pDeskDupl->AcquireNextFrame(
				frameTimeout,
				&FrameInfo,
				&pDesktopResource);

			// Get mouse info
			gotMousePointer = SUCCEEDED(pMousePointer->GetMouse(&PtrInfo, &(FrameInfo), sourceRect, pDeskDupl));
		}

		if (pDeskDupl == nullptr
			|| hr == DXGI_ERROR_ACCESS_LOST) {
			if (pDeskDupl == nullptr) {
				DEBUG(L"Error getting next frame due to Desktop Duplication instance is NULL, reinitializing");
			}
			else if (hr == DXGI_ERROR_ACCESS_LOST) {
				_com_error err(hr);
				DEBUG(L"Error getting next frame due to DXGI_ERROR_ACCESS_LOST, reinitializing: %s", err.ErrorMessage());

			}
			if (pDeskDupl) {
				pDeskDupl->ReleaseFrame();
				pDeskDupl.Release();
			}
			hr = InitializeDesktopDupl(m_Device, pSelectedOutput, &pDeskDupl, &outputDuplDesc);
			if (FAILED(hr))
			{
				_com_error err(hr);
				switch (hr)
				{
				case DXGI_ERROR_DEVICE_REMOVED:
				case DXGI_ERROR_DEVICE_RESET:
					return m_Device->GetDeviceRemovedReason();
				case E_ACCESSDENIED:
				case DXGI_ERROR_MODE_CHANGE_IN_PROGRESS:
				case DXGI_ERROR_SESSION_DISCONNECTED:
					//Access to video output is denied, probably due to DRM, screen saver, desktop is switching, fullscreen application is launching, or similar.
					//We continue the recording, and instead of desktop texture just add a blank texture instead.
					hr = S_OK;
					if (pPreviousFrameCopy) {
						pPreviousFrameCopy.Release();
						RtlZeroMemory(&PtrInfo, sizeof(PtrInfo));
					}
					else {
						//We are just recording empty frames now. Slow down the framerate and rate of reconnect retry attempts to save resources.
						wait(200);
					}
					WARN(L"Desktop duplication temporarily unavailable: %s", err.ErrorMessage());
					break;
				case DXGI_ERROR_NOT_CURRENTLY_AVAILABLE:
					ERROR(L"Error reinitializing desktop duplication with DXGI_ERROR_NOT_CURRENTLY_AVAILABLE. This means DXGI reached the limit on the maximum number of concurrent duplication applications (default of four). Therefore, the calling application cannot create any desktop duplication interfaces until the other applications close");
					return hr;
				default:
					//Unexpected error, return.
					ERROR(L"Error reinitializing desktop duplication with unexpected error, aborting: %s", err.ErrorMessage());
					return hr;
				}
			}
			else {
				DEBUG("Desktop duplication reinitialized");
				continue;
			}
		}
		else if (FAILED(hr) && hr != DXGI_ERROR_WAIT_TIMEOUT) {
			return hr;
		}

		if (m_RecorderMode == MODE_SLIDESHOW
			|| m_RecorderMode == MODE_SNAPSHOT) {

			if (frameNr == 0 && FrameInfo.AccumulatedFrames == 0) {
				continue;
			}
		}

		INT64 durationSinceLastFrame100Nanos = max(duration_cast<nanoseconds>(chrono::high_resolution_clock::now() - lastFrame).count() / 100, 0);

		//Delay frames that comes quicker than selected framerate to see if we can skip them.
		if (frameNr > 0 //always draw first frame 
			&& !m_IsFixedFramerate
			&& (!m_IsMousePointerEnabled || FrameInfo.PointerShapeBufferSize == 0)//always redraw when pointer changes if we draw pointer
			&& ((hr == DXGI_ERROR_WAIT_TIMEOUT && durationSinceLastFrame100Nanos < m_MaxFrameLength100Nanos) //don't wait on timeout if the frame duration is > m_MaxFrameLength100Nanos
				|| durationSinceLastFrame100Nanos < videoFrameDuration100Nanos)) //wait if frame timeouted or duration is under our chosen framerate
		{
			bool delay = false;
			if (SUCCEEDED(hr) && durationSinceLastFrame100Nanos < videoFrameDuration100Nanos) {
				if (pDesktopResource != nullptr) {
					if (FrameInfo.AccumulatedFrames > 0) {
						//we got a frame, but it's too soon, so we cache it and see if there are more changes.
						if (pPreviousFrameCopy == nullptr) {
							RETURN_ON_BAD_HR(hr = m_Device->CreateTexture2D(&sourceFrameDesc, nullptr, &pPreviousFrameCopy));
						}
						CComPtr<ID3D11Texture2D> pAcquiredDesktopImage = nullptr;
						RETURN_ON_BAD_HR(hr = pDesktopResource->QueryInterface(IID_PPV_ARGS(&pAcquiredDesktopImage)));
						m_ImmediateContext->CopyResource(pPreviousFrameCopy, pAcquiredDesktopImage);
						pAcquiredDesktopImage.Release();
					}
				}
				delay = true;
			}
			else if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
				if (durationSinceLastFrame100Nanos < m_MaxFrameLength100Nanos) {
					delay = true;
				}
			}
			if (delay) {
				UINT64 delay = 1;
				if (durationSinceLastFrame100Nanos < videoFrameDuration100Nanos) {
					delay = HundredNanosToMillis((videoFrameDuration100Nanos - durationSinceLastFrame100Nanos));
				}
				wait(delay);
				continue;
			}
		}

		if (hr != DXGI_ERROR_WAIT_TIMEOUT) {
			RETURN_ON_BAD_HR(hr);
		}

		lastFrame = high_resolution_clock::now();
		{
			CComPtr<ID3D11Texture2D> pFrameCopy = nullptr;
			RETURN_ON_BAD_HR(hr = m_Device->CreateTexture2D(&sourceFrameDesc, nullptr, &pFrameCopy));
			CComPtr<ID3D11Texture2D> pAcquiredDesktopImage = nullptr;
			if (pDesktopResource != nullptr && FrameInfo.AccumulatedFrames > 0) {
				RETURN_ON_BAD_HR(hr = pDesktopResource->QueryInterface(IID_PPV_ARGS(&pAcquiredDesktopImage)));
			}
			if (pAcquiredDesktopImage != nullptr) {
				m_ImmediateContext->CopyResource(pFrameCopy, pAcquiredDesktopImage);
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

			SetDebugName(pFrameCopy, "FrameCopy");

			if (gotMousePointer) {
				hr = DrawMousePointer(pFrameCopy, pMousePointer.get(), PtrInfo, screenRotation, durationSinceLastFrame100Nanos);
				if (FAILED(hr)) {
					_com_error err(hr);
					ERROR(L"Error drawing mouse pointer: %s", err.ErrorMessage());
					//We just log the error and continue if the mouse pointer failed to draw. If there is an error with DXGI, it will be handled on the next call to AcquireNextFrame.
				}
			}
			if (token.is_canceled()) {
				DEBUG("Recording task was cancelled");
				hr = S_OK;
				break;
			}

			if ((m_RecorderMode == MODE_SLIDESHOW || m_RecorderMode == MODE_SNAPSHOT) && !isDestRectEqualToSourceRect) {
				pFrameCopy = CropFrame(pFrameCopy, destFrameDesc, destRect);
			}

			// Take screenshots in a video recording, if video recording is file mode.
			if (IsSnapshotsWithVideoEnabled() && !m_OutputSnapshotsFolderPath.empty()) {
				const auto now = std::chrono::system_clock::now();
				if (previousTimeSnapshotTaken == std::chrono::system_clock::from_time_t(0) ||
					now - previousTimeSnapshotTaken > m_SnapshotsWithVideoInterval) {
					previousTimeSnapshotTaken = now;
					if (pFrameCopyForSnapshotsWithVideo == nullptr)
						RETURN_ON_BAD_HR(hr = m_Device->CreateTexture2D(&sourceFrameDesc, nullptr, &pFrameCopyForSnapshotsWithVideo));
					wstring snapshotPath = m_OutputSnapshotsFolderPath + L"\\" + s2ws(CurrentTimeToFormattedString()) + GetImageExtension();

					// Copy the current frame for a separate thread to write it to a file asynchronously.
					// Assuming previous file writing is already done.
					m_ImmediateContext->CopyResource(pFrameCopyForSnapshotsWithVideo, pFrameCopy);
					WriteFrameToImageAsync(pFrameCopyForSnapshotsWithVideo, snapshotPath.c_str());
				}
			}

			FrameWriteModel model;
			RtlZeroMemory(&model, sizeof(model));
			model.Frame = pFrameCopy;
			model.Duration = durationSinceLastFrame100Nanos;
			model.StartPos = lastFrameStartPos;
			model.Audio = GrabAudioFrame(pLoopbackCaptureOutputDevice, pLoopbackCaptureInputDevice);
			model.FrameNumber = frameNr;
			RETURN_ON_BAD_HR(hr = m_EncoderResult = RenderFrame(model));

			if (m_RecorderMode == MODE_SNAPSHOT) {
				break;
			}
			frameNr++;
			lastFrameStartPos += durationSinceLastFrame100Nanos;
			if (m_IsFixedFramerate)
			{
				wait(static_cast<UINT32>(max(videoFrameDurationMillis - duration_cast<milliseconds>(chrono::high_resolution_clock::now() - lastFrame).count(), 0)));
			}
		}
	}

	//Push the last frame waiting to be recorded to the sink writer.
	if (pPreviousFrameCopy != nullptr) {
		INT64 duration = duration_cast<nanoseconds>(chrono::high_resolution_clock::now() - lastFrame).count() / 100;
		if (gotMousePointer) {
			DrawMousePointer(pPreviousFrameCopy, pMousePointer.get(), PtrInfo, screenRotation, duration);
		}
		if ((m_RecorderMode == MODE_SLIDESHOW || m_RecorderMode == MODE_SNAPSHOT) && !isDestRectEqualToSourceRect) {
			pPreviousFrameCopy = CropFrame(pPreviousFrameCopy, destFrameDesc, destRect);
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


std::vector<BYTE> internal_recorder::GrabAudioFrame(std::unique_ptr<loopback_capture> & pLoopbackCaptureOutputDevice,
	std::unique_ptr<loopback_capture> & pLoopbackCaptureInputDevice)
{
	if (m_IsOutputDeviceEnabled && m_IsInputDeviceEnabled && pLoopbackCaptureOutputDevice && pLoopbackCaptureInputDevice) {
		// mix our audio buffers from output device and input device to get one audio buffer since VideoSinkWriter works only with one Audio sink
		if (pLoopbackCaptureOutputDevice->PeakRecordedBytes().size() > 0) {
			std::vector<BYTE> outputDeviceData = pLoopbackCaptureOutputDevice->GetRecordedBytes(0);
			std::vector<BYTE> inputDeviceData = pLoopbackCaptureInputDevice->GetRecordedBytes(outputDeviceData.size());
			return std::move(MixAudio(outputDeviceData, inputDeviceData, m_OutputVolumeModifier, m_InputVolumeModifier));
		}
		else {
			std::vector<BYTE> inputDeviceData = pLoopbackCaptureInputDevice->GetRecordedBytes();
			std::vector<BYTE> outputDeviceData = pLoopbackCaptureOutputDevice->GetRecordedBytes(inputDeviceData.size());
			return std::move(MixAudio(outputDeviceData, inputDeviceData, m_OutputVolumeModifier, m_InputVolumeModifier));
		}
	}
	else if (m_IsOutputDeviceEnabled && pLoopbackCaptureOutputDevice)
		return std::move(MixAudio(pLoopbackCaptureOutputDevice->GetRecordedBytes(), std::vector<BYTE>(), m_OutputVolumeModifier, 1.0));
	else if (m_IsInputDeviceEnabled && pLoopbackCaptureInputDevice)
		return std::move(MixAudio(std::vector<BYTE>(), pLoopbackCaptureInputDevice->GetRecordedBytes(), 1.0, m_InputVolumeModifier));
	else
		return std::vector<BYTE>();
}

HRESULT internal_recorder::InitializeDesc(DXGI_OUTDUPL_DESC outputDuplDesc, _Out_ D3D11_TEXTURE2D_DESC * pSourceFrameDesc, _Out_ D3D11_TEXTURE2D_DESC * pDestFrameDesc, _Out_ RECT * pSourceRect, _Out_ RECT * pDestRect) {
	UINT monitorWidth = (outputDuplDesc.Rotation == DXGI_MODE_ROTATION_ROTATE90 || outputDuplDesc.Rotation == DXGI_MODE_ROTATION_ROTATE270)
		? outputDuplDesc.ModeDesc.Height : outputDuplDesc.ModeDesc.Width;

	UINT monitorHeight = (outputDuplDesc.Rotation == DXGI_MODE_ROTATION_ROTATE90 || outputDuplDesc.Rotation == DXGI_MODE_ROTATION_ROTATE270)
		? outputDuplDesc.ModeDesc.Width : outputDuplDesc.ModeDesc.Height;


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

HRESULT internal_recorder::InitializeDx(_In_opt_ IDXGIOutput * pDxgiOutput, _Outptr_ ID3D11DeviceContext * *ppContext, _Outptr_ ID3D11Device * *ppDevice) {
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
	if (pDxgiOutput) {
		// Get DXGI adapter
		hr = pDxgiOutput->GetParent(
			__uuidof(IDXGIAdapter),
			reinterpret_cast<void**>(&pDxgiAdapter));
	}
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

HRESULT internal_recorder::InitializeDesktopDupl(_In_ ID3D11Device * pDevice, _In_opt_ IDXGIOutput * pDxgiOutput, _Outptr_ IDXGIOutputDuplication * *ppDesktopDupl, _Out_ DXGI_OUTDUPL_DESC * pOutputDuplDesc) {
	*ppDesktopDupl = nullptr;

	// Get DXGI device
	CComPtr<IDXGIDevice> pDxgiDevice = nullptr;
	CComPtr<IDXGIOutputDuplication> pDeskDupl = nullptr;
	DXGI_OUTDUPL_DESC OutputDuplDesc;
	RtlZeroMemory(&OutputDuplDesc, sizeof(OutputDuplDesc));
	HRESULT hr = S_OK;
	if (!pDxgiOutput) {
		hr = pDevice->QueryInterface(IID_PPV_ARGS(&pDxgiDevice));
		RETURN_ON_BAD_HR(hr);
		// Get DXGI adapter
		CComPtr<IDXGIAdapter> pDxgiAdapter = nullptr;
		hr = pDxgiDevice->GetParent(
			__uuidof(IDXGIAdapter),
			reinterpret_cast<void**>(&pDxgiAdapter));
		pDxgiDevice.Release();
		RETURN_ON_BAD_HR(hr);


		// Get pDxgiOutput
		hr = pDxgiAdapter->EnumOutputs(
			m_DisplayOutput,
			&pDxgiOutput);

		RETURN_ON_BAD_HR(hr);
		pDxgiAdapter.Release();
	}

	RETURN_ON_BAD_HR(hr);
	CComPtr<IDXGIOutput1> pDxgiOutput1 = nullptr;

	hr = pDxgiOutput->QueryInterface(IID_PPV_ARGS(&pDxgiOutput1));
	RETURN_ON_BAD_HR(hr);

	// Create desktop duplication
	hr = pDxgiOutput1->DuplicateOutput(
		pDevice,
		&pDeskDupl);
	RETURN_ON_BAD_HR(hr);
	pDxgiOutput1.Release();

	// Create GUI drawing texture
	pDeskDupl->GetDesc(&OutputDuplDesc);

	*ppDesktopDupl = pDeskDupl;
	(*ppDesktopDupl)->AddRef();
	*pOutputDuplDesc = OutputDuplDesc;
	return hr;
}

//
// Set new viewport
//
void internal_recorder::SetViewPort(ID3D11DeviceContext * deviceContext, UINT Width, UINT Height)
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
RECT internal_recorder::MakeRectEven(RECT rect)
{
	if (rect.left % 2 != 0)
		rect.left -= 1;
	if (rect.top % 2 != 0)
		rect.top -= 1;
	if (rect.right % 2 != 0)
		rect.right -= 1;
	if (rect.bottom % 2 != 0)
		rect.bottom -= 1;
	return rect;
}

HRESULT internal_recorder::GetOutputForDeviceName(std::wstring deviceName, _Outptr_opt_result_maybenull_ IDXGIOutput * *ppOutput) {
	HRESULT hr = S_OK;
	*ppOutput = nullptr;
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
					*ppOutput = pOutput;
					(*ppOutput)->AddRef();
					break;
				}
				SafeRelease(&pOutput);
				i++;
			}
			if (*ppOutput) {
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

HRESULT internal_recorder::InitializeVideoSinkWriter(std::wstring path, _In_opt_ IMFByteStream *pOutStream, _In_ ID3D11Device *pDevice, RECT sourceRect, RECT destRect, DXGI_MODE_ROTATION rotation, _Outptr_ IMFSinkWriter **ppWriter, _Out_ DWORD *pVideoStreamIndex, _Out_ DWORD *pAudioStreamIndex, _Outptr_opt_ IMFMediaType **pVideoMediaTypeOutput, _Outptr_opt_ IMFMediaType **pVideoMediaTypeInput)
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
	RETURN_ON_BAD_HR(MFCreateAttributes(&pAttributes, 6));
	RETURN_ON_BAD_HR(pAttributes->SetGUID(MF_TRANSCODE_CONTAINERTYPE, MFTranscodeContainerType_MPEG4));
	RETURN_ON_BAD_HR(pAttributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, m_IsHardwareEncodingEnabled));
	RETURN_ON_BAD_HR(pAttributes->SetUINT32(MF_MPEG4SINK_MOOV_BEFORE_MDAT, m_IsMp4FastStartEnabled));
	RETURN_ON_BAD_HR(pAttributes->SetUINT32(MF_LOW_LATENCY, m_IsLowLatencyModeEnabled));
	RETURN_ON_BAD_HR(pAttributes->SetUINT32(MF_SINK_WRITER_DISABLE_THROTTLING, m_IsThrottlingDisabled));
	// Add device manager to attributes. This enables hardware encoding.
	RETURN_ON_BAD_HR(pAttributes->SetUnknown(MF_SINK_WRITER_D3D_MANAGER, pDeviceManager));
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
		GUID transformType;
		DWORD transformIndex = 0;
		CComPtr<IMFVideoProcessorControl> videoProcessor = nullptr;
		CComPtr<IMFSinkWriterEx>      pSinkWriterEx = nullptr;
		RETURN_ON_BAD_HR(pSinkWriter->QueryInterface(&pSinkWriterEx));
		while (true) {
			CComPtr<IMFTransform> transform = nullptr;
			RETURN_ON_BAD_HR(pSinkWriterEx->GetTransformForStream(videoStreamIndex, transformIndex, &transformType, &transform));
			if (transformType == MFT_CATEGORY_VIDEO_PROCESSOR) {
				RETURN_ON_BAD_HR(transform->QueryInterface(&videoProcessor));
				break;
			}
			transformIndex++;
		}
		if (videoProcessor) {
			videoProcessor->SetSourceRectangle(&destRect);
			videoProcessor->SetRotation(ROTATION_NORMAL);
		}
	}

	// Tell the sink writer to start accepting data.
	RETURN_ON_BAD_HR(pSinkWriter->BeginWriting());

	// Return the pointer to the caller.
	*ppWriter = pSinkWriter;
	(*ppWriter)->AddRef();
	if (pVideoMediaTypeOutput) {
		*pVideoMediaTypeOutput = pVideoMediaTypeOut;
		(*pVideoMediaTypeOutput)->AddRef();
	}
	if (pVideoMediaTypeInput) {
		*pVideoMediaTypeInput = pVideoMediaTypeIn;
		(*pVideoMediaTypeInput)->AddRef();
	}
	*pVideoStreamIndex = videoStreamIndex;
	*pAudioStreamIndex = audioStreamIndex;
	return S_OK;
}

HRESULT internal_recorder::ConfigureOutputMediaTypes(UINT destWidth, UINT destHeight, IMFMediaType **pVideoMediaTypeOut, IMFMediaType **pAudioMediaTypeOut)
{
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
		RETURN_ON_BAD_HR(pAudioMediaType->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, m_InputAudioSamplesPerSecond));
		RETURN_ON_BAD_HR(pAudioMediaType->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, m_AudioBitrate));

		*pAudioMediaTypeOut = pAudioMediaType;
		(*pAudioMediaTypeOut)->AddRef();
	}

	*pVideoMediaTypeOut = pVideoMediaType;
	(*pVideoMediaTypeOut)->AddRef();
	return S_OK;
}

HRESULT internal_recorder::ConfigureInputMediaTypes(UINT sourceWidth, UINT sourceHeight, MFVideoRotationFormat rotationFormat, IMFMediaType *pVideoMediaTypeOut, IMFMediaType **pVideoMediaTypeIn, IMFMediaType **pAudioMediaTypeIn)
{
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
		RETURN_ON_BAD_HR(pAudioMediaType->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, m_InputAudioSamplesPerSecond));
		RETURN_ON_BAD_HR(pAudioMediaType->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, m_AudioChannels));

		*pAudioMediaTypeIn = pAudioMediaType;
		(*pAudioMediaTypeIn)->AddRef();
	}

	*pVideoMediaTypeIn = pVideoMediaType;
	(*pVideoMediaTypeIn)->AddRef();
	return S_OK;
}

HRESULT internal_recorder::InitializeAudioCapture(loopback_capture **outputAudioCapture, loopback_capture **inputAudioCapture)
{
	loopback_capture *pLoopbackCaptureOutputDevice = nullptr;
	loopback_capture *pLoopbackCaptureInputDevice = nullptr;
	HRESULT hr;
	bool recordAudio = m_RecorderMode == MODE_VIDEO && m_IsAudioEnabled;
	UINT32 inputDeviceSampleRate = 0;
	if (recordAudio && (m_IsOutputDeviceEnabled || m_IsOutputDeviceEnabled)) {
		if (m_IsOutputDeviceEnabled)
		{
			pLoopbackCaptureOutputDevice = new loopback_capture(L"AudioOutputDevice");
			hr = pLoopbackCaptureOutputDevice->StartCapture(m_AudioChannels, m_AudioOutputDevice, eRender);
			if (SUCCEEDED(hr)) {
				inputDeviceSampleRate = m_InputAudioSamplesPerSecond = pLoopbackCaptureOutputDevice->GetInputSampleRate();
			}
		}

		if (m_IsInputDeviceEnabled)
		{
			pLoopbackCaptureInputDevice = new loopback_capture(L"AudioInputDevice");
			hr = pLoopbackCaptureInputDevice->StartCapture(inputDeviceSampleRate, m_AudioChannels, m_AudioInputDevice, eCapture);
			if (SUCCEEDED(hr)) {
				m_InputAudioSamplesPerSecond = pLoopbackCaptureInputDevice->GetInputSampleRate();
			}
		}

		if (m_IsOutputDeviceEnabled && m_IsInputDeviceEnabled)
		{
			m_InputAudioSamplesPerSecond = pLoopbackCaptureOutputDevice->GetInputSampleRate();
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
				INFO("Starting mouse click polling task");
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
				INFO("Exiting mouse click polling task");
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
	const GUID & subtype,    // Uncompressed subtype (eg, RGB-32, AYUV)
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

HRESULT internal_recorder::DrawMousePointer(ID3D11Texture2D * frame, mouse_pointer * pMousePointer, mouse_pointer::PTR_INFO ptrInfo, DXGI_MODE_ROTATION screenRotation, INT64 durationSinceLastFrame100Nanos)
{
	HRESULT hr = S_FALSE;
	if (g_LastMouseClickDurationRemaining > 0
		&& m_IsMouseClicksDetected)
	{
		if (g_LastMouseClickButton == VK_LBUTTON)
		{
			hr = pMousePointer->DrawMouseClick(&ptrInfo, frame, m_MouseClickDetectionLMBColor, m_MouseClickDetectionRadius, screenRotation);
		}
		if (g_LastMouseClickButton == VK_RBUTTON)
		{
			hr = pMousePointer->DrawMouseClick(&ptrInfo, frame, m_MouseClickDetectionRMBColor, m_MouseClickDetectionRadius, screenRotation);
		}
		INT64 millis = max(HundredNanosToMillis(durationSinceLastFrame100Nanos), 0);
		g_LastMouseClickDurationRemaining = max(g_LastMouseClickDurationRemaining - millis, 0);
		DEBUG("Drawing mouse click, duration remaining on click is %u ms", g_LastMouseClickDurationRemaining);
	}

	if (m_IsMousePointerEnabled) {
		hr = pMousePointer->DrawMousePointer(&ptrInfo, m_ImmediateContext, m_Device, frame, screenRotation);
	}
	return hr;
}

ID3D11Texture2D * internal_recorder::CropFrame(ID3D11Texture2D * frame, D3D11_TEXTURE2D_DESC frameDesc, RECT destRect)
{
	CComPtr<ID3D11Texture2D> pCroppedFrameCopy = nullptr;
	HRESULT hr = m_Device->CreateTexture2D(&frameDesc, nullptr, &pCroppedFrameCopy);
	D3D11_BOX sourceRegion;
	RtlZeroMemory(&sourceRegion, sizeof(sourceRegion));
	sourceRegion.left = destRect.left;
	sourceRegion.right = destRect.right;
	sourceRegion.top = destRect.top;
	sourceRegion.bottom = destRect.bottom;
	sourceRegion.front = 0;
	sourceRegion.back = 1;
	m_ImmediateContext->CopySubresourceRegion(pCroppedFrameCopy, 0, 0, 0, 0, frame, 0, &sourceRegion);
	return pCroppedFrameCopy;
}

HRESULT internal_recorder::CreateCaptureItem(GraphicsCaptureItem * item)
{
	if (m_WindowHandle != nullptr) {
		*item = capture::util::CreateCaptureItemForWindow(m_WindowHandle);
	}
	else {
		auto pMonitorList = std::make_unique<monitor_list>(false);
		auto monitor = pMonitorList->GetMonitorForDisplayName(m_DisplayOutputName);
		if (!monitor.has_value()) {
			if (pMonitorList->GetCurrentMonitors().size() == 0) {
				ERROR("Failed to find any monitors to record");
				return E_FAIL;
			}
			monitor = pMonitorList->GetCurrentMonitors().at(0);
		}
		INFO(L"Recording monitor %ls using Windows.Graphics.Capture", m_DisplayOutputName.c_str());
		*item = capture::util::CreateCaptureItemForMonitor(monitor->MonitorHandle);
	}
	return S_OK;
}

HRESULT internal_recorder::SetAttributeU32(_Inout_ CComPtr<ICodecAPI> & codec, const GUID & guid, UINT32 value)
{
	VARIANT val;
	val.vt = VT_UI4;
	val.uintVal = value;
	return codec->SetValue(&guid, &val);
}

HRESULT internal_recorder::RenderFrame(FrameWriteModel & model) {
	HRESULT hr(S_OK);

	if (m_RecorderMode == MODE_VIDEO) {
		hr = WriteFrameToVideo(model.StartPos, model.Duration, m_VideoStreamIndex, model.Frame);
		bool wroteAudioSample = false;
		if (FAILED(hr)) {
			_com_error err(hr);
			ERROR(L"Writing of video frame with start pos %lld ms failed: %s", (HundredNanosToMillis(model.StartPos)), err.ErrorMessage());
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
				int frameCount = int(ceil(m_InputAudioSamplesPerSecond * HundredNanosToMillis(model.Duration) / 1000));
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
				ERROR(L"Writing of audio sample with start pos %lld ms failed: %s", (HundredNanosToMillis(model.StartPos)), err.ErrorMessage());
				return hr;//Stop recording if we fail
			}
			else {
				wroteAudioSample = true;
			}
		}
		auto frameInfoStr = wroteAudioSample ? (paddedAudio ? L"video sample and audio padding" : L"video and audio sample") : L"video sample";
		TRACE(L"Wrote %s with start pos %lld ms and with duration %lld ms", frameInfoStr, HundredNanosToMillis(model.StartPos), HundredNanosToMillis(model.Duration));
	}
	else if (m_RecorderMode == MODE_SLIDESHOW) {
		wstring	path = m_OutputFolder + L"\\" + to_wstring(model.FrameNumber) + GetImageExtension();
		hr = WriteFrameToImage(model.Frame, path);
		INT64 startposMs = HundredNanosToMillis(model.StartPos);
		INT64 durationMs = HundredNanosToMillis(model.Duration);
		if (FAILED(hr)) {
			_com_error err(hr);
			ERROR(L"Writing of video slideshow frame with start pos %lld ms failed: %s", startposMs, err.ErrorMessage());
			return hr; //Stop recording if we fail
		}
		else {

			m_FrameDelays.insert(std::pair<wstring, int>(path, model.FrameNumber == 0 ? 0 : (int)durationMs));
			TRACE(L"Wrote video slideshow frame with start pos %lld ms and with duration %lld ms", startposMs, durationMs);
		}
	}
	else if (m_RecorderMode == MODE_SNAPSHOT) {
		hr = WriteFrameToImage(model.Frame, m_OutputFullPath);
		TRACE(L"Wrote snapshot to %s", m_OutputFullPath.c_str());
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
HRESULT internal_recorder::WriteFrameToImage(_In_ ID3D11Texture2D * pAcquiredDesktopImage, std::wstring filePath)
{
	return SaveWICTextureToFile(m_ImmediateContext, pAcquiredDesktopImage, m_ImageEncoderFormat, filePath.c_str());
}

void internal_recorder::WriteFrameToImageAsync(_In_ ID3D11Texture2D* pAcquiredDesktopImage, std::wstring filePath)
{
	concurrency::create_task([this, pAcquiredDesktopImage, filePath]() {
		return WriteFrameToImage(pAcquiredDesktopImage, filePath);
	}).then([this, filePath](concurrency::task<HRESULT> t)
	{
		try {
			HRESULT hr = t.get();
			bool success = SUCCEEDED(hr);
			if (success) {
				TRACE(L"Wrote snapshot to %s", filePath.c_str());
				if (RecordingSnapshotCreatedCallback != nullptr) {
					RecordingSnapshotCreatedCallback(filePath);
				}
			}
			else {
				_com_error err(hr);
				ERROR("Error saving snapshot: %s", err.ErrorMessage());
			}
			// if .get() didn't throw and the HRESULT succeeded, there are no errors.
		}
		catch (const exception & e) {
			// handle error
			ERROR(L"Exception saving snapshot: %s", e.what());
		}
	});
}

HRESULT internal_recorder::WriteFrameToVideo(INT64 frameStartPos, INT64 frameDuration, DWORD streamIndex, _In_ ID3D11Texture2D * pAcquiredDesktopImage)
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
HRESULT internal_recorder::WriteAudioSamplesToVideo(INT64 frameStartPos, INT64 frameDuration, DWORD streamIndex, _In_ BYTE * pSrc, DWORD cbData)
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

void internal_recorder::SetDebugName(ID3D11DeviceChild * child, const std::string & name)
{
#if _DEBUG
	if (child != nullptr)
		child->SetPrivateData(WKPDID_D3DDebugObjectName, name.size(), name.c_str());
#endif
}