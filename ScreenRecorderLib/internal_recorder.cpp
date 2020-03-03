#include <ppltasks.h> 
#include <ctime>   // localtime
#include <sstream> // stringstream
#include <iomanip> // put_time
#include <string>
#include <comdef.h>
#include <Mferror.h>
#include <wrl.h>
#include <ScreenGrab.h>
#include <concrt.h>
#include <mfidl.h>
#include <VersionHelpers.h>
#include <Wmcodecdsp.h>
#include <filesystem>
#include "mouse_pointer.h"
#include "loopback_capture.h"
#include "internal_recorder.h"
#include "audio_prefs.h"
#include "log.h"
#include "utilities.h"
#include "cleanup.h"


#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "D3D11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "Mfuuid.lib")
#pragma comment(lib, "Mfplat.lib")
#pragma comment(lib, "evr.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "Mf.lib")
#pragma comment(lib, "wmcodecdspuuid.lib")

using namespace std;
using namespace std::chrono;
using namespace concurrency;
using namespace DirectX;

UINT64 g_LastMouseClickDurationRemaining;
UINT g_MouseClickDetectionDurationMillis = 50;
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
	Concurrency::cancellation_token_source m_RecordTaskCts;
};


internal_recorder::internal_recorder() :m_TaskWrapperImpl(new TaskWrapper())
{
	m_IsDestructed = false;
}

internal_recorder::~internal_recorder()
{
	UnhookWindowsHookEx(m_Mousehook);
	if (m_IsRecording) {
		if (RecordingStatusChangedCallback != nullptr)
			RecordingStatusChangedCallback(STATUS_IDLE);
	}
	m_IsDestructed = true;
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

void internal_recorder::SetVideoFps(UINT32 fps)
{
	m_VideoFps = fps;
}
void internal_recorder::SetVideoBitrate(UINT32 bitrate)
{
	m_VideoBitrate = bitrate;
}
void internal_recorder::SetVideoQuality(UINT32 quality)
{
	m_VideoQuality = quality;
}
void internal_recorder::SetVideoBitrateMode(UINT32 mode) {
	m_VideoBitrateControlMode = mode;
}
void internal_recorder::SetAudioBitrate(UINT32 bitrate)
{
	m_AudioBitrate = bitrate;
}
void internal_recorder::SetAudioChannels(UINT32 channels)
{
	m_AudioChannels = channels;
}
void internal_recorder::SetOutputDevice(std::wstring& string)
{
	m_AudioOutputDevice = string;
}
void internal_recorder::SetInputDevice(std::wstring& string)
{
	m_AudioInputDevice = string;
}
void internal_recorder::SetAudioEnabled(bool enabled)
{
	m_IsAudioEnabled = enabled;
}
void internal_recorder::SetOutputDeviceEnabled(bool enabled)
{
	m_IsOutputDeviceEnabled = enabled;
}
void internal_recorder::SetInputDeviceEnabled(bool enabled)
{
	m_IsInputDeviceEnabled = enabled;
}
void internal_recorder::SetMousePointerEnabled(bool enabled)
{
	m_IsMousePointerEnabled = enabled;
}
void internal_recorder::SetIsFastStartEnabled(bool enabled) {
	m_IsMp4FastStartEnabled = enabled;
}
void internal_recorder::SetIsFragmentedMp4Enabled(bool enabled) {
	m_IsFragmentedMp4Enabled = enabled;
}
void internal_recorder::SetIsHardwareEncodingEnabled(bool enabled) {
	m_IsHardwareEncodingEnabled = enabled;
}
void internal_recorder::SetIsLowLatencyModeEnabled(bool enabled) {
	m_IsLowLatencyModeEnabled = enabled;
}
void internal_recorder::SetDestRectangle(RECT rect)
{
	if (rect.left % 2 != 0)
		rect.left += 1;
	if (rect.top % 2 != 0)
		rect.top += 1;
	if (rect.right % 2 != 0)
		rect.right += 1;
	if (rect.bottom % 2 != 0)
		rect.bottom += 1;
	m_DestRect = rect;
}
void internal_recorder::SetDisplayOutput(UINT32 output)
{
	m_DisplayOutput = output;
}
void internal_recorder::SetDisplayOutput(std::wstring output)
{
	m_DisplayOutputName = output;
}
void internal_recorder::SetRecorderMode(UINT32 mode)
{
	m_RecorderMode = mode;
}
void internal_recorder::SetFixedFramerate(bool value)
{
	m_IsFixedFramerate = value;
}
void internal_recorder::SetIsThrottlingDisabled(bool value) {
	m_IsThrottlingDisabled = value;
}
void internal_recorder::SetH264EncoderProfile(UINT32 value) {
	m_H264Profile = value;
}
void internal_recorder::SetDetectMouseClicks(bool value) {
	m_IsMouseClicksDetected = value;
}
void internal_recorder::SetMouseClickDetectionLMBColor(std::string value) {
	m_MouseClickDetectionLMBColor = value;
}
void internal_recorder::SetMouseClickDetectionRMBColor(std::string value) {
	m_MouseClickDetectionRMBColor = value;
}
void internal_recorder::SetMouseClickDetectionRadius(int value) {
	m_MouseClickDetectionRadius = value;
}
void internal_recorder::SetMouseClickDetectionDuration(int value) {
	g_MouseClickDetectionDurationMillis = value;
}
void internal_recorder::SetMouseClickDetectionMode(UINT32 value) {
	m_MouseClickDetectionMode = value;
}
void internal_recorder::SetSnapshotSaveFormat(GUID value) {
	m_ImageEncoderFormat = value;
}

std::vector<BYTE> internal_recorder::MixAudio(std::vector<BYTE> &first, std::vector<BYTE> &second)
{
	std::vector<BYTE> newvector;

	if (first.size() >= second.size())
	{
		newvector.insert(newvector.end(), first.begin(), first.end());

		for (UINT i = 0; i < second.size(); i++)
		{
			newvector[i] += second[i];
		}
	}
	else
	{
		//This will clip the second audio sample to the length of the first.
		//It fixes audio artifacts due to  variable length of the two samples, but potentially loses information..
		vector<BYTE>::iterator end = first.size() > 0 ? second.begin() += first.size() : second.end();

		newvector.insert(newvector.end(), second.begin(), end);

		for (UINT i = 0; i < first.size(); i++)
		{
			newvector[i] += first[i];
		}
	}

	for (UINT i = 0; i < newvector.size(); ++i)
		if (newvector[i] > 0x7fff)
			newvector[i] = newvector[i] / 2; // divide by the number of channels being mixed
		else if (newvector[i] < -0x7fff)
			newvector[i] = newvector[i] / 2;

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
		ERR("Image encoder format not recognized, defaulting to .jpg extension");
		return L".jpg";
	}
}

std::wstring internal_recorder::GetVideoExtension() {
	return L".mp4";
}

HRESULT internal_recorder::ConfigureOutputDir(std::wstring path) {
	m_OutputFullPath = path;
	wstring dir = path;
	LPWSTR directory = (LPWSTR)dir.c_str();
	PathRemoveFileSpecW(directory);
	std::error_code ec;
	if (std::filesystem::exists(directory) || std::filesystem::create_directories(directory, ec))
	{
		LOG(L"output folder is ready");
		m_OutputFolder = directory;
	}
	else
	{
		// Failed to create directory.
		ERR(L"failed to create output folder");
		if (RecordingFailedCallback != nullptr)
			RecordingFailedCallback(L"Failed to create output folder: " + utilities::s2ws(ec.message()));
		return E_FAIL;
	}
	if (m_RecorderMode == MODE_VIDEO || m_RecorderMode == MODE_SNAPSHOT) {
		wstring ext = m_RecorderMode == MODE_VIDEO ? GetVideoExtension() : GetImageExtension();
		LPWSTR pStrExtension = PathFindExtension(path.c_str());
		if (pStrExtension == nullptr || pStrExtension[0] == 0)
		{
			m_OutputFullPath = m_OutputFolder + L"\\" + utilities::s2ws(NowToString()) + ext;
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
		ERR(L"%ls", errorText);
		RecordingFailedCallback(errorText);
		return E_FAIL;
	}

	if (m_IsRecording) {
		if (m_IsPaused) {
			m_IsPaused = false;
			if (RecordingStatusChangedCallback != nullptr)
				RecordingStatusChangedCallback(STATUS_RECORDING);
		}
		return S_FALSE;
	}
	m_FrameDelays.clear();
	if (!path.empty()) {
		RETURN_ON_BAD_HR(ConfigureOutputDir(path));
	}

	cancellation_token token = m_TaskWrapperImpl->m_RecordTaskCts.get_token();

	if (m_IsMouseClicksDetected) {
		switch (m_MouseClickDetectionMode)
		{
		default:
		case MOUSE_DETECTION_MODE_POLLING: {
			concurrency::create_task([this, token]() {
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
				LOG("Exiting mouse click polling task");
			});
			break;
		}
		case MOUSE_DETECTION_MODE_HOOK: {
			m_Mousehook = SetWindowsHookEx(WH_MOUSE_LL, MouseHookProc, nullptr, 0);
			break;
		}
		}
	}
	concurrency::create_task([this, token, stream]() {
		HRESULT hr = CoInitializeEx(nullptr, COINITBASE_MULTITHREADED | COINIT_DISABLE_OLE1DDE);
		RETURN_ON_BAD_HR(hr = MFStartup(MF_VERSION, MFSTARTUP_LITE));
		{
			DXGI_OUTDUPL_DESC outputDuplDesc;
			RtlZeroMemory(&outputDuplDesc, sizeof(outputDuplDesc));
			CComPtr<ID3D11Device> pDevice = nullptr;
			CComPtr<IDXGIOutputDuplication> pDeskDupl = nullptr;
			std::unique_ptr<mouse_pointer> pMousePointer = make_unique<mouse_pointer>();
			std::unique_ptr<loopback_capture> pLoopbackCaptureOutputDevice = make_unique<loopback_capture>();
			std::unique_ptr<loopback_capture> pLoopbackCaptureInputDevice = make_unique<loopback_capture>();
			CComPtr<IDXGIOutput> pSelectedOutput = nullptr;
			hr = GetOutputForDeviceName(m_DisplayOutputName, &pSelectedOutput);
			RETURN_ON_BAD_HR(hr = InitializeDx(pSelectedOutput, &m_ImmediateContext, &pDevice, &pDeskDupl, &outputDuplDesc));

			DXGI_MODE_ROTATION screenRotation = outputDuplDesc.Rotation;
			D3D11_TEXTURE2D_DESC frameDesc;
			RECT sourceRect, destRect;

			RtlZeroMemory(&frameDesc, sizeof(frameDesc));
			RtlZeroMemory(&sourceRect, sizeof(sourceRect));
			RtlZeroMemory(&destRect, sizeof(destRect));

			RETURN_ON_BAD_HR(hr = initializeDesc(outputDuplDesc, &frameDesc, &sourceRect, &destRect));

			// create "loopback audio capture has started" events
			HANDLE hOutputCaptureStartedEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
			if (nullptr == hOutputCaptureStartedEvent) {
				ERR(L"CreateEvent failed: last error is %u", GetLastError());
				return E_FAIL;
			}
			HANDLE hInputCaptureStartedEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
			if (nullptr == hInputCaptureStartedEvent) {
				ERR(L"CreateEvent failed: last error is %u", GetLastError());
				return E_FAIL;
			}
			CloseHandleOnExit closeOutputCaptureStartedEvent(hOutputCaptureStartedEvent);
			CloseHandleOnExit closeInputCaptureStartedEvent(hInputCaptureStartedEvent);

			// create "stop capturing audio now" events
			HANDLE hOutputCaptureStopEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
			if (nullptr == hOutputCaptureStopEvent) {
				ERR(L"CreateEvent failed: last error is %u", GetLastError());
				return E_FAIL;
			}
			HANDLE hInputCaptureStopEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
			if (nullptr == hInputCaptureStopEvent) {
				ERR(L"CreateEvent failed: last error is %u", GetLastError());
				return E_FAIL;
			}

			CloseHandleOnExit closeOutputCaptureStopEvent(hOutputCaptureStopEvent);
			CloseHandleOnExit closeInputCaptureStopEvent(hInputCaptureStopEvent);

			bool recordAudio = m_RecorderMode == MODE_VIDEO && m_IsAudioEnabled;
			if (recordAudio && m_IsOutputDeviceEnabled)
			{
				bool isDeviceEmpty = m_AudioOutputDevice.empty();
				LPCWSTR argv[3] = { L"", L"--device", m_AudioOutputDevice.c_str() };
				int argc = isDeviceEmpty ? 1 : SIZEOF_ARRAY(argv);
				CPrefs prefs(argc, isDeviceEmpty ? nullptr : argv, hr, eRender);
				prefs.m_bInt16 = true;
				// create arguments for loopback capture thread
				LoopbackCaptureThreadFunctionArguments threadArgs;
				threadArgs.hr = E_UNEXPECTED; // thread will overwrite this
				threadArgs.pMMDevice = prefs.m_pMMDevice;
				threadArgs.pCaptureInstance = pLoopbackCaptureOutputDevice.get();
				threadArgs.bInt16 = prefs.m_bInt16;
				threadArgs.hFile = prefs.m_hFile;
				threadArgs.hStartedEvent = hOutputCaptureStartedEvent;
				threadArgs.hStopEvent = hOutputCaptureStopEvent;
				threadArgs.nFrames = 0;
				threadArgs.flow = eRender;
				threadArgs.samplerate = 0;

				HANDLE hThread = CreateThread(
					nullptr, 0,
					LoopbackCaptureThreadFunction, &threadArgs, 0, nullptr
				);
				if (nullptr == hThread) {
					ERR(L"CreateThread failed: last error is %u", GetLastError());
					return E_FAIL;
				}
				WaitForSingleObjectEx(hOutputCaptureStartedEvent, 1000, false);
				m_InputAudioSamplesPerSecond = pLoopbackCaptureOutputDevice->GetInputSampleRate();
				CloseHandle(hThread);
			}

			if (recordAudio && m_IsInputDeviceEnabled)
			{
				bool isDeviceEmpty = m_AudioInputDevice.empty();
				LPCWSTR argv[3] = { L"", L"--device", m_AudioInputDevice.c_str() };
				int argc = isDeviceEmpty ? 1 : SIZEOF_ARRAY(argv);
				CPrefs prefs(argc, isDeviceEmpty ? nullptr : argv, hr, eCapture);
				prefs.m_bInt16 = true;
				// create arguments for loopback capture thread
				LoopbackCaptureThreadFunctionArguments threadArgs;
				threadArgs.hr = E_UNEXPECTED; // thread will overwrite this
				threadArgs.pMMDevice = prefs.m_pMMDevice;
				threadArgs.pCaptureInstance = pLoopbackCaptureInputDevice.get();
				threadArgs.bInt16 = prefs.m_bInt16;
				threadArgs.hFile = prefs.m_hFile;
				threadArgs.hStartedEvent = hInputCaptureStartedEvent;
				threadArgs.hStopEvent = hInputCaptureStopEvent;
				threadArgs.nFrames = 0;
				threadArgs.flow = eCapture;
				threadArgs.samplerate = 0;

				if (m_IsOutputDeviceEnabled)
				{
					threadArgs.samplerate = m_InputAudioSamplesPerSecond;
				}

				HANDLE hThread = CreateThread(
					nullptr, 0,
					LoopbackCaptureThreadFunction, &threadArgs, 0, nullptr
				);
				if (nullptr == hThread) {
					ERR(L"CreateThread failed: last error is %u", GetLastError());
					return E_FAIL;
				}
				WaitForSingleObjectEx(hInputCaptureStartedEvent, 1000, false);
				m_InputAudioSamplesPerSecond = pLoopbackCaptureInputDevice->GetInputSampleRate();
				CloseHandle(hThread);
			}

			if (recordAudio && m_IsOutputDeviceEnabled && m_IsInputDeviceEnabled)
			{
				m_InputAudioSamplesPerSecond = pLoopbackCaptureOutputDevice->GetInputSampleRate();
			}

			// moved this section after sound initialization to get right m_InputAudioSamplesPerSecond from pLoopbackCapture before InitializeVideoSinkWriter
			if (m_RecorderMode == MODE_VIDEO) {
				CComPtr<IMFByteStream> outputStream = nullptr;
				if (stream != nullptr) {
					RETURN_ON_BAD_HR(hr = MFCreateMFByteStreamOnStream(stream, &outputStream));

				}
				RETURN_ON_BAD_HR(hr = InitializeVideoSinkWriter(m_OutputFullPath, outputStream, pDevice, sourceRect, destRect, outputDuplDesc.Rotation, &m_SinkWriter, &m_VideoStreamIndex, &m_AudioStreamIndex));
			}

			m_IsRecording = true;
			if (RecordingStatusChangedCallback != nullptr)
				RecordingStatusChangedCallback(STATUS_RECORDING);

			RETURN_ON_BAD_HR(hr = pMousePointer->Initialize(m_ImmediateContext, pDevice));
			SetViewPort(m_ImmediateContext, sourceRect.right - sourceRect.left, sourceRect.bottom - sourceRect.top);

			g_LastMouseClickDurationRemaining = 0;

			ULONGLONG lastFrameStartPos = 0;
			pLoopbackCaptureOutputDevice->ClearRecordedBytes();
			pLoopbackCaptureInputDevice->ClearRecordedBytes();

			UINT64 videoFrameDurationMillis = 1000 / m_VideoFps;
			UINT64 videoFrameDuration100Nanos = videoFrameDurationMillis * 10 * 1000;
			UINT frameTimeout = 0;
			int frameNr = 0;
			CComPtr<ID3D11Texture2D> pPreviousFrameCopy = nullptr;
			std::chrono::high_resolution_clock::time_point	lastFrame = std::chrono::high_resolution_clock::now();
			mouse_pointer::PTR_INFO PtrInfo;
			RtlZeroMemory(&PtrInfo, sizeof(PtrInfo));
			while (true)
			{
				bool gotMousePointer = false;
				CComPtr<IDXGIResource> pDesktopResource = nullptr;
				DXGI_OUTDUPL_FRAME_INFO FrameInfo;
				RtlZeroMemory(&FrameInfo, sizeof(FrameInfo));

				if (token.is_canceled()) {
					hr = S_OK;
					break;
				}

				if (m_IsPaused) {
					wait(10);
					lastFrame = high_resolution_clock::now();
					pLoopbackCaptureOutputDevice->ClearRecordedBytes();
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
					if (SUCCEEDED(pMousePointer->GetMouse(&PtrInfo, &(FrameInfo), sourceRect, pDeskDupl))) {
						gotMousePointer = true;
					}
				}

				if (pDeskDupl == nullptr
					|| hr == DXGI_ERROR_ACCESS_LOST
					|| hr == DXGI_ERROR_INVALID_CALL) {
					if (pDeskDupl) {
						pDeskDupl->ReleaseFrame();
						pDeskDupl.Release();
					}
					if (FAILED(hr))
					{
						_com_error err(hr);
						ERR(L"AcquireNextFrame error: %s\n", err.ErrorMessage());
					}

					hr = InitializeDesktopDupl(pDevice, pSelectedOutput, &pDeskDupl, &outputDuplDesc);
					if (FAILED(hr))
					{
						_com_error err(hr);
						ERR(L"Reinitialized desktop duplication error: %s\n", err.ErrorMessage());
					}
					if (hr == E_ACCESSDENIED) {
						//Access to video output is denied, probably due to DRM, screen saver, fullscreen application or similar.
						//We continue the recording, and instead of desktop texture just add a blank texture instead.
						hr = S_OK;
					}
					else {
						RETURN_ON_BAD_HR(hr);
					}
				}
				if (hr == DXGI_ERROR_DEVICE_REMOVED) {
					return pDevice->GetDeviceRemovedReason();
				}
				if (m_RecorderMode == MODE_SLIDESHOW
					|| m_RecorderMode == MODE_SNAPSHOT) {

					if (frameNr == 0 && FrameInfo.AccumulatedFrames == 0) {
						continue;
					}
				}

				UINT64 durationSinceLastFrame100Nanos = duration_cast<nanoseconds>(chrono::high_resolution_clock::now() - lastFrame).count() / 100;
				if (frameNr > 0 //always draw first frame 
					&& !m_IsFixedFramerate
					&& (!m_IsMousePointerEnabled || FrameInfo.PointerShapeBufferSize == 0)//always redraw when pointer changes if we draw pointer
					&& (hr == DXGI_ERROR_WAIT_TIMEOUT || (durationSinceLastFrame100Nanos) < videoFrameDuration100Nanos)) //skip if frame timeouted or duration is under our chosen framerate
				{
					if (hr == S_OK && pDesktopResource != nullptr) {
						//we got a frame, but it's too soon, so we cache it and see if there are more changes.
						if (pPreviousFrameCopy == nullptr) {
							RETURN_ON_BAD_HR(hr = pDevice->CreateTexture2D(&frameDesc, nullptr, &pPreviousFrameCopy));
						}
						CComPtr<ID3D11Texture2D> pAcquiredDesktopImage = nullptr;
						RETURN_ON_BAD_HR(hr = pDesktopResource->QueryInterface(IID_PPV_ARGS(&pAcquiredDesktopImage)));
						m_ImmediateContext->CopyResource(pPreviousFrameCopy, pAcquiredDesktopImage);
						pAcquiredDesktopImage.Release();
					}
					if (hr == S_OK || pPreviousFrameCopy == nullptr || durationSinceLastFrame100Nanos < videoFrameDuration100Nanos) {
						UINT32 delay = 1;
						if (durationSinceLastFrame100Nanos < videoFrameDuration100Nanos) {
							delay = static_cast<UINT32>((videoFrameDuration100Nanos - durationSinceLastFrame100Nanos) / 10 / 1000);
						}

						wait(delay);
						continue;
					}
				}

				if (pPreviousFrameCopy && hr != DXGI_ERROR_WAIT_TIMEOUT) {
					pPreviousFrameCopy.Release();
				}

				lastFrame = high_resolution_clock::now();
				{
					std::vector<BYTE> audioData;
					if (recordAudio) {
						if (m_IsOutputDeviceEnabled && !m_IsInputDeviceEnabled)
						{
							audioData = pLoopbackCaptureOutputDevice->GetRecordedBytes();
						}
						if (!m_IsOutputDeviceEnabled && m_IsInputDeviceEnabled)
						{
							audioData = pLoopbackCaptureInputDevice->GetRecordedBytes();
						}
						if (m_IsOutputDeviceEnabled && m_IsInputDeviceEnabled)
						{
							// mix our audio buffers from output device and input device to get one audio buffer since VideoSinkWriter works only with one Audio sink
							audioData = MixAudio(pLoopbackCaptureOutputDevice->GetRecordedBytes(), pLoopbackCaptureInputDevice->GetRecordedBytes());
						}
					}

					CComPtr<ID3D11Texture2D> pFrameCopy = nullptr;
					RETURN_ON_BAD_HR(hr = pDevice->CreateTexture2D(&frameDesc, nullptr, &pFrameCopy));

					if (pPreviousFrameCopy) {
						m_ImmediateContext->CopyResource(pFrameCopy, pPreviousFrameCopy);
					}
					else if (pDesktopResource != nullptr) {
						CComPtr<ID3D11Texture2D> pAcquiredDesktopImage = nullptr;
						RETURN_ON_BAD_HR(hr = pDesktopResource->QueryInterface(IID_PPV_ARGS(&pAcquiredDesktopImage)));
						m_ImmediateContext->CopyResource(pFrameCopy, pAcquiredDesktopImage);
					}

					SetDebugName(pFrameCopy, "FrameCopy");

					if (m_IsFixedFramerate && pPreviousFrameCopy == nullptr) {
						RETURN_ON_BAD_HR(hr = pDevice->CreateTexture2D(&frameDesc, nullptr, &pPreviousFrameCopy));
						m_ImmediateContext->CopyResource(pPreviousFrameCopy, pFrameCopy);
						SetDebugName(pPreviousFrameCopy, "PreviousFrameCopy");
					}
					else if (!m_IsFixedFramerate && pPreviousFrameCopy) {
						pPreviousFrameCopy.Release();
					}

					if (g_LastMouseClickDurationRemaining > 0
						&& m_IsMouseClicksDetected
						&& gotMousePointer)
					{
						if (g_LastMouseClickButton == VK_LBUTTON)
						{
							hr = pMousePointer->DrawMouseClick(&PtrInfo, pFrameCopy, m_MouseClickDetectionLMBColor, m_MouseClickDetectionRadius, screenRotation);
						}
						if (g_LastMouseClickButton == VK_RBUTTON)
						{
							hr = pMousePointer->DrawMouseClick(&PtrInfo, pFrameCopy, m_MouseClickDetectionRMBColor, m_MouseClickDetectionRadius, screenRotation);
						}
						auto millis = max(durationSinceLastFrame100Nanos / 10 / 1000, 0);
						g_LastMouseClickDurationRemaining = max(g_LastMouseClickDurationRemaining - millis, 0);
						LOG("Drawing mouse click, duration remaining on click is %u ms", g_LastMouseClickDurationRemaining);
					}

					if (m_IsMousePointerEnabled && gotMousePointer) {
						hr = pMousePointer->DrawMousePointer(&PtrInfo, m_ImmediateContext, pDevice, pFrameCopy, screenRotation);
						if (hr == DXGI_ERROR_ACCESS_LOST
							|| hr == DXGI_ERROR_INVALID_CALL) {
							if (pDeskDupl) {
								pDeskDupl->ReleaseFrame();
								pDeskDupl.Release();
							}
							if (FAILED(hr))
							{
								_com_error err(hr);
								ERR(L"DrawMousePointer error: %s\n", err.ErrorMessage());
							}
							hr = InitializeDesktopDupl(pDevice, pSelectedOutput, &pDeskDupl, &outputDuplDesc);
							if (FAILED(hr))
							{
								_com_error err(hr);
								ERR(L"Reinitialize desktop duplication error: %s\n", err.ErrorMessage());
							}
							if (hr != E_ACCESSDENIED) {
								RETURN_ON_BAD_HR(hr);
							}
							wait(1);
							continue;
						}
					}

					if (token.is_canceled()) {
						hr = S_OK;
						break;
					}
					if (m_IsRecording) {
						if (m_RecorderMode == MODE_VIDEO || m_RecorderMode == MODE_SLIDESHOW) {
							FrameWriteModel(model);
							model.Frame = pFrameCopy;
							model.Duration = durationSinceLastFrame100Nanos;
							model.StartPos = lastFrameStartPos;
							if (recordAudio) {
								model.Audio = audioData;
							}
							model.FrameNumber = frameNr;
							hr = EnqueueFrame(model);
							if (FAILED(hr)) {
								m_IsEncoderFailure = true;
								break;
							}
							frameNr++;
						}
						else if (m_RecorderMode == MODE_SNAPSHOT) {
							hr = WriteFrameToImage(pFrameCopy, m_OutputFullPath.c_str());
							break;
						}
					}

					lastFrameStartPos += durationSinceLastFrame100Nanos;
					if (m_IsFixedFramerate)
					{
						wait(static_cast<UINT32>(videoFrameDurationMillis));
					}
				}
			}

			SetEvent(hOutputCaptureStopEvent);
			SetEvent(hInputCaptureStopEvent);
			if (!m_IsDestructed) {
				if (RecordingStatusChangedCallback != nullptr)
					RecordingStatusChangedCallback(STATUS_FINALIZING);
			}

			pDeskDupl->ReleaseFrame();
			if (pPreviousFrameCopy)
				pPreviousFrameCopy.Release();
			if (pMousePointer) {
				pMousePointer->CleanupResources();
			}
			if (PtrInfo.PtrShapeBuffer)
				delete PtrInfo.PtrShapeBuffer;
			PtrInfo.PtrShapeBuffer = nullptr;
		}
		LOG("Exiting recording task");
		return hr;
	})
	.then([this, token](HRESULT hr) {
		m_IsRecording = false;

		if (!m_IsDestructed) {
			if (m_SinkWriter) {
				m_SinkWriter->Finalize();

				//Dispose of MPEG4MediaSink 
				IMFMediaSink *pSink;
				if (SUCCEEDED(m_SinkWriter->GetServiceForStream(MF_SINK_WRITER_MEDIASINK, GUID_NULL, IID_PPV_ARGS(&pSink)))) {
					pSink->Shutdown();
					pSink->Release();
				};
				try {
					m_SinkWriter->Release();
					m_SinkWriter = nullptr;
				}
				catch (...) {
					ERR(L"Error releasing sink writer");
				}
			}

			SafeRelease(&m_ImmediateContext);

			LOG(L"Finalized!");
			MFShutdown();
			CoUninitialize();
			LOG(L"MF shut down!");
#if _DEBUG
			if (m_Debug) {
				m_Debug->ReportLiveDeviceObjects(D3D11_RLDO_DETAIL);
				SafeRelease(&m_Debug);
			}
#endif
		}
		return hr;
	})
	.then([this](concurrency::task<HRESULT> t)
	{
		std::wstring errMsg = L"";
		bool success = false;
		try {
			HRESULT hr = t.get();
			// .get() didn't throw, so we succeeded.
			success = SUCCEEDED(hr);
			if (!success) {
				_com_error err(hr);
				errMsg = err.ErrorMessage();
			}
		}
		catch (const exception& e) {
			// handle error
			ERR(L"Exception in RecordTask: %s", e.what());
		}
		catch (...) {
			ERR(L"Exception in RecordTask");
		}

		if (RecordingStatusChangedCallback)
			RecordingStatusChangedCallback(STATUS_IDLE);


		if (success) {
			if (RecordingCompleteCallback)
				RecordingCompleteCallback(m_OutputFullPath, m_FrameDelays);
		}
		else {
			if (RecordingFailedCallback) {


				if (m_IsEncoderFailure) {
					errMsg = L"Write error in video encoder.";
					if (m_IsHardwareEncodingEnabled) {
						errMsg += L" If the problem persists, disabling hardware encoding may improve stability.";
					}
				}
				else {
					if (errMsg.empty()) {
						errMsg = utilities::GetLastErrorStdWstr();
					}
				}
				RecordingFailedCallback(errMsg);
			}
		}

		UnhookWindowsHookEx(m_Mousehook);
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
		if (RecordingStatusChangedCallback != nullptr)
			RecordingStatusChangedCallback(STATUS_PAUSED);
	}
}
void internal_recorder::ResumeRecording() {
	if (m_IsRecording) {
		if (m_IsPaused) {
			m_IsPaused = false;
			if (RecordingStatusChangedCallback != nullptr)
				RecordingStatusChangedCallback(STATUS_RECORDING);
		}
	}
}

HRESULT internal_recorder::initializeDesc(DXGI_OUTDUPL_DESC outputDuplDesc, _Out_ D3D11_TEXTURE2D_DESC *pFrameDesc, _Out_ RECT *pSourceRect, _Out_ RECT *pDestRect) {
	UINT monitorWidth = (outputDuplDesc.Rotation == DXGI_MODE_ROTATION_ROTATE90 || outputDuplDesc.Rotation == DXGI_MODE_ROTATION_ROTATE270)
		? outputDuplDesc.ModeDesc.Height : outputDuplDesc.ModeDesc.Width;

	UINT monitorHeight = (outputDuplDesc.Rotation == DXGI_MODE_ROTATION_ROTATE90 || outputDuplDesc.Rotation == DXGI_MODE_ROTATION_ROTATE270)
		? outputDuplDesc.ModeDesc.Width : outputDuplDesc.ModeDesc.Height;

	D3D11_TEXTURE2D_DESC frameDesc;
	frameDesc.Width = monitorWidth;
	frameDesc.Height = monitorHeight;
	frameDesc.Format = outputDuplDesc.ModeDesc.Format;
	frameDesc.ArraySize = 1;
	frameDesc.BindFlags = D3D11_BIND_FLAG::D3D11_BIND_RENDER_TARGET;
	frameDesc.MiscFlags = 0;
	frameDesc.SampleDesc.Count = 1;
	frameDesc.SampleDesc.Quality = 0;
	frameDesc.MipLevels = 1;
	frameDesc.CPUAccessFlags = 0;
	frameDesc.Usage = D3D11_USAGE_DEFAULT;

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

	*pSourceRect = sourceRect;
	*pDestRect = destRect;
	*pFrameDesc = frameDesc;
	return S_OK;
}

HRESULT internal_recorder::InitializeDx(_In_opt_ IDXGIOutput *pDxgiOutput, _Outptr_ ID3D11DeviceContext **ppContext, _Outptr_ ID3D11Device **ppDevice, _Outptr_ IDXGIOutputDuplication **ppDesktopDupl, _Out_ DXGI_OUTDUPL_DESC *pOutputDuplDesc) {
	*ppContext = nullptr;
	*ppDevice = nullptr;
	*ppDesktopDupl = nullptr;

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
		for each (D3D_DRIVER_TYPE type in gDriverTypes)
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
	hr = InitializeDesktopDupl(pDevice, pDxgiOutput, &pDeskDupl, &OutputDuplDesc);
	RETURN_ON_BAD_HR(hr);

	// Return the pointer to the caller.
	*ppContext = m_ImmediateContext;
	(*ppContext)->AddRef();
	*ppDevice = pDevice;
	(*ppDevice)->AddRef();
	*ppDesktopDupl = pDeskDupl;
	(*ppDesktopDupl)->AddRef();
	*pOutputDuplDesc = OutputDuplDesc;

	return hr;
}

HRESULT internal_recorder::InitializeDesktopDupl(_In_ ID3D11Device *pDevice, _In_opt_ IDXGIOutput *pDxgiOutput, _Outptr_ IDXGIOutputDuplication **ppDesktopDupl, _Out_ DXGI_OUTDUPL_DESC *pOutputDuplDesc) {
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
void internal_recorder::SetViewPort(ID3D11DeviceContext *deviceContext, UINT Width, UINT Height)
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

HRESULT internal_recorder::GetOutputForDeviceName(std::wstring deviceName, _Out_opt_ IDXGIOutput **ppOutput) {
	HRESULT hr = S_OK;
	if (deviceName != L"") {
		*ppOutput = nullptr;
		std::vector<CComPtr<IDXGIAdapter>> adapters = EnumDisplayAdapters();
		for each (CComPtr<IDXGIAdapter> adapter in adapters)
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

HRESULT internal_recorder::InitializeVideoSinkWriter(std::wstring path, _In_opt_ IMFByteStream *pOutStream, _In_ ID3D11Device* pDevice, RECT sourceRect, RECT destRect, DXGI_MODE_ROTATION rotation, _Outptr_ IMFSinkWriter **ppWriter, _Out_ DWORD *pVideoStreamIndex, _Out_ DWORD *pAudioStreamIndex)
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
	// Passing 6 as the argument to save re-allocations
	RETURN_ON_BAD_HR(MFCreateAttributes(&pAttributes, 6));
	RETURN_ON_BAD_HR(pAttributes->SetGUID(MF_TRANSCODE_CONTAINERTYPE, MFTranscodeContainerType_MPEG4));
	RETURN_ON_BAD_HR(pAttributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, m_IsHardwareEncodingEnabled));
	RETURN_ON_BAD_HR(pAttributes->SetUINT32(MF_MPEG4SINK_MOOV_BEFORE_MDAT, m_IsMp4FastStartEnabled));
	RETURN_ON_BAD_HR(pAttributes->SetUINT32(MF_LOW_LATENCY, m_IsLowLatencyModeEnabled));
	RETURN_ON_BAD_HR(pAttributes->SetUINT32(MF_SINK_WRITER_DISABLE_THROTTLING, m_IsThrottlingDisabled));
	// Add device manager to attributes. This enables hardware encoding.
	RETURN_ON_BAD_HR(pAttributes->SetUnknown(MF_SINK_WRITER_D3D_MANAGER, pDeviceManager));

	UINT sourceWidth = max(0, sourceRect.right - sourceRect.left);
	UINT sourceHeight = max(0, sourceRect.bottom - sourceRect.top);

	UINT destWidth = max(0, destRect.right - destRect.left);
	UINT destHeight = max(0, destRect.bottom - destRect.top);

	// Set the output video type.
	RETURN_ON_BAD_HR(MFCreateMediaType(&pVideoMediaTypeOut));
	RETURN_ON_BAD_HR(pVideoMediaTypeOut->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video));
	RETURN_ON_BAD_HR(pVideoMediaTypeOut->SetGUID(MF_MT_SUBTYPE, VIDEO_ENCODING_FORMAT));
	RETURN_ON_BAD_HR(pVideoMediaTypeOut->SetUINT32(MF_MT_AVG_BITRATE, m_VideoBitrate));
	RETURN_ON_BAD_HR(pVideoMediaTypeOut->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive));
	RETURN_ON_BAD_HR(pVideoMediaTypeOut->SetUINT32(MF_MT_MPEG2_PROFILE, m_H264Profile));
	RETURN_ON_BAD_HR(pVideoMediaTypeOut->SetUINT32(MF_MT_YUV_MATRIX, MFVideoTransferMatrix_BT601));
	RETURN_ON_BAD_HR(MFSetAttributeSize(pVideoMediaTypeOut, MF_MT_FRAME_SIZE, destWidth, destHeight));
	RETURN_ON_BAD_HR(MFSetAttributeRatio(pVideoMediaTypeOut, MF_MT_FRAME_RATE, m_VideoFps, 1));
	RETURN_ON_BAD_HR(MFSetAttributeRatio(pVideoMediaTypeOut, MF_MT_PIXEL_ASPECT_RATIO, 1, 1));

	// Set the input video type.
	CreateInputMediaTypeFromOutput(pVideoMediaTypeOut, VIDEO_INPUT_FORMAT, &pVideoMediaTypeIn);
	RETURN_ON_BAD_HR(MFSetAttributeSize(pVideoMediaTypeIn, MF_MT_FRAME_SIZE, sourceWidth, sourceHeight));
	pVideoMediaTypeIn->SetUINT32(MF_MT_VIDEO_ROTATION, rotationFormat);

	if (m_IsAudioEnabled) {
		// Set the output audio type.
		RETURN_ON_BAD_HR(MFCreateMediaType(&pAudioMediaTypeOut));
		RETURN_ON_BAD_HR(pAudioMediaTypeOut->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio));
		RETURN_ON_BAD_HR(pAudioMediaTypeOut->SetGUID(MF_MT_SUBTYPE, AUDIO_ENCODING_FORMAT));
		RETURN_ON_BAD_HR(pAudioMediaTypeOut->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, m_AudioChannels));
		RETURN_ON_BAD_HR(pAudioMediaTypeOut->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, AUDIO_BITS_PER_SAMPLE));
		RETURN_ON_BAD_HR(pAudioMediaTypeOut->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, m_InputAudioSamplesPerSecond));
		RETURN_ON_BAD_HR(pAudioMediaTypeOut->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND, m_AudioBitrate));

		// Set the input audio type.
		RETURN_ON_BAD_HR(MFCreateMediaType(&pAudioMediaTypeIn));
		RETURN_ON_BAD_HR(pAudioMediaTypeIn->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio));
		RETURN_ON_BAD_HR(pAudioMediaTypeIn->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM));
		RETURN_ON_BAD_HR(pAudioMediaTypeIn->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, AUDIO_BITS_PER_SAMPLE));
		RETURN_ON_BAD_HR(pAudioMediaTypeIn->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, m_InputAudioSamplesPerSecond));
		RETURN_ON_BAD_HR(pAudioMediaTypeIn->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, m_AudioChannels));
	}

	//Creates a streaming writer
	CComPtr<IMFMediaSink> pMp4StreamSink = nullptr;
	if (m_IsFragmentedMp4Enabled) {
		RETURN_ON_BAD_HR(MFCreateFMPEG4MediaSink(pOutStream, pVideoMediaTypeOut, pAudioMediaTypeOut, &pMp4StreamSink));
	}
	else {
		RETURN_ON_BAD_HR(MFCreateMPEG4MediaSink(pOutStream, pVideoMediaTypeOut, pAudioMediaTypeOut, &pMp4StreamSink));
	}
	pAudioMediaTypeOut.Release();
	pVideoMediaTypeOut.Release();
	RETURN_ON_BAD_HR(MFCreateSinkWriterFromMediaSink(pMp4StreamSink, pAttributes, &pSinkWriter));
	pMp4StreamSink.Release();
	videoStreamIndex = 0;
	audioStreamIndex = 1;
	RETURN_ON_BAD_HR(pSinkWriter->SetInputMediaType(videoStreamIndex, pVideoMediaTypeIn, nullptr));
	pVideoMediaTypeIn.Release();
	if (m_IsAudioEnabled) {
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
	*pVideoStreamIndex = videoStreamIndex;
	*pAudioStreamIndex = audioStreamIndex;
	return S_OK;
}

HRESULT internal_recorder::CreateInputMediaTypeFromOutput(
	_In_ IMFMediaType *pType,    // Pointer to an encoded video type.
	const GUID& subtype,    // Uncompressed subtype (eg, RGB-32, AYUV)
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


HRESULT internal_recorder::SetAttributeU32(_Inout_ CComPtr<ICodecAPI>& codec, const GUID& guid, UINT32 value)
{
	VARIANT val;
	val.vt = VT_UI4;
	val.uintVal = value;
	return codec->SetValue(&guid, &val);
}

HRESULT internal_recorder::EnqueueFrame(FrameWriteModel model) {
	HRESULT hr(S_OK);

	if (m_RecorderMode == MODE_VIDEO) {
		hr = WriteFrameToVideo(model.StartPos, model.Duration, m_VideoStreamIndex, model.Frame);
		if (FAILED(hr)) {
			_com_error err(hr);
			ERR(L"Writing of video frame with start pos %lld ms failed: %s\n", (model.StartPos / 10 / 1000), err.ErrorMessage());
			return hr;//Stop recording if we fail
		}
		BYTE *data;
		//If the audio pCaptureInstance returns no data, i.e. the source is silent, we need to pad the PCM stream with zeros to give the media sink silence as input.
		//If we don't, the sink writer will begin throttling video frames because it expects audio samples to be delivered, and think they are delayed.
		if (m_IsAudioEnabled && model.Audio.size() == 0) {
			auto frameCount = static_cast<UINT32>(ceil(m_InputAudioSamplesPerSecond * ((double)model.Duration / 10 / 1000 / 1000)));
			auto byteCount = frameCount * (AUDIO_BITS_PER_SAMPLE / 8)*m_AudioChannels;
			model.Audio.insert(model.Audio.end(), byteCount, 0);
			LOG(L"Inserted %zd bytes of silence", model.Audio.size());
		}
		if (model.Audio.size() > 0) {
			data = new BYTE[model.Audio.size()];
			std::copy(model.Audio.begin(), model.Audio.end(), data);
			hr = WriteAudioSamplesToVideo(model.StartPos, model.Duration, m_AudioStreamIndex, data, model.Audio.size());
			delete[] data;
			model.Audio.clear();
			vector<BYTE>().swap(model.Audio);
			if (FAILED(hr)) {
				_com_error err(hr);
				ERR(L"Writing of audio sample with start pos %lld ms failed: %s\n", (model.StartPos / 10 / 1000), err.ErrorMessage());
				return hr;//Stop recording if we fail
			}
		}
	}
	else if (m_RecorderMode == MODE_SLIDESHOW) {
		wstring	path = m_OutputFolder + L"\\" + to_wstring(model.FrameNumber) + GetImageExtension();
		hr = WriteFrameToImage(model.Frame, path.c_str());
		LONGLONG startposMs = (model.StartPos / 10 / 1000);
		LONGLONG durationMs = (model.Duration / 10 / 1000);
		if (FAILED(hr)) {
			_com_error err(hr);
			ERR(L"Writing of video frame with start pos %lld ms failed: %s\n", startposMs, err.ErrorMessage());
			return hr; //Stop recording if we fail
		}
		else {

			m_FrameDelays.insert(std::pair<wstring, int>(path, model.FrameNumber == 0 ? 0 : (int)durationMs));
			LOG(L"Wrote video slideshow frame with start pos %lld ms and with duration %lld ms\n", startposMs, durationMs);
		}
	}
	model.Frame.Release();

	return hr;
}

std::string internal_recorder::NowToString()
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
HRESULT internal_recorder::WriteFrameToImage(_In_ ID3D11Texture2D* pAcquiredDesktopImage, LPCWSTR filePath)
{
	HRESULT hr = SaveWICTextureToFile(m_ImmediateContext, pAcquiredDesktopImage,
		m_ImageEncoderFormat, filePath, nullptr);
	return hr;
}

HRESULT internal_recorder::WriteFrameToVideo(ULONGLONG frameStartPos, ULONGLONG frameDuration, DWORD streamIndex, _In_ ID3D11Texture2D* pAcquiredDesktopImage)
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
		hr = MFCreateVideoSampleFromSurface(nullptr, &pSample);
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
		if (SUCCEEDED(hr)) {
			LOG(L"Wrote frame with start pos %lld ms and with duration %lld ms", (frameStartPos / 10 / 1000), (frameDuration / 10 / 1000));
		}
	}
	SafeRelease(&pSample);
	SafeRelease(&p2DBuffer);
	SafeRelease(&pMediaBuffer);
	return hr;
}
HRESULT internal_recorder::WriteAudioSamplesToVideo(ULONGLONG frameStartPos, ULONGLONG frameDuration, DWORD streamIndex, _In_ BYTE *pSrc, DWORD cbData)
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
		hr = MFCreateVideoSampleFromSurface(nullptr, &pSample);
	}
	if (SUCCEEDED(hr))
	{
		hr = pSample->AddBuffer(pBuffer);
	}
	if (SUCCEEDED(hr))
	{
		LONGLONG start = frameStartPos;
		hr = pSample->SetSampleTime(start);
	}
	if (SUCCEEDED(hr))
	{
		LONGLONG duration = frameDuration;
		hr = pSample->SetSampleDuration(duration);
	}
	if (SUCCEEDED(hr))
	{
		// Send the sample to the Sink Writer.
		hr = m_SinkWriter->WriteSample(streamIndex, pSample);
		if (SUCCEEDED(hr)) {
			LOG(L"Wrote audio sample with start pos %lld ms and with duration %lld ms", frameStartPos / 10 / 1000, (frameDuration / 10 / 1000));
		}
	}
	SafeRelease(&pSample);
	SafeRelease(&pBuffer);
	return hr;
}

void internal_recorder::SetDebugName(ID3D11DeviceChild* child, const std::string& name)
{
#if _DEBUG
	if (child != nullptr)
		child->SetPrivateData(WKPDID_D3DDebugObjectName, name.size(), name.c_str());
#endif
}