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
	Concurrency::task<void> m_RenderTask;
	Concurrency::cancellation_token_source m_RecordTaskCts;
	Concurrency::cancellation_token_source m_RenderTaskCts;

	TaskWrapper() {
		m_RenderTask = concurrency::create_task([]() {});
	}
};



internal_recorder::internal_recorder() :m_TaskWrapperImpl(new TaskWrapper())
{
	m_IsDestructed = false;
}

internal_recorder::~internal_recorder()
{
	if (m_IsRecording) {
		if (RecordingStatusChangedCallback != NULL)
			RecordingStatusChangedCallback(STATUS_IDLE);
		m_TaskWrapperImpl->m_RecordTaskCts.cancel();
	}
	m_IsDestructed = true;
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
void internal_recorder::SetAudioEnabled(bool enabled)
{
	m_IsAudioEnabled = enabled;
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

HRESULT internal_recorder::ConfigureOutputDir(std::wstring path) {
	m_OutputFullPath = path;
	wstring dir = path;
	LPWSTR directory = (LPWSTR)dir.c_str();
	PathRemoveFileSpecW(directory);

	if (utilities::CreateAllDirs(directory))
	{
		LOG(L"output folder is ready");
		m_OutputFolder = directory;
	}
	else
	{
		// Failed to create directory.
		ERR(L"failed to create output folder");
		if (RecordingFailedCallback != NULL)
			RecordingFailedCallback(L"failed to create output folder");
		return S_FALSE;
	}
	if (m_RecorderMode == MODE_VIDEO || m_RecorderMode == MODE_SNAPSHOT) {
		wstring ext = m_RecorderMode == MODE_VIDEO ? L".mp4" : L".png";
		LPWSTR pStrExtension = PathFindExtension(path.c_str());
		if (pStrExtension == NULL || pStrExtension[0] == 0)
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
	return BeginRecording(path, NULL);
}

HRESULT internal_recorder::BeginRecording(std::wstring path, IStream *stream) {

	if (!IsWindows8OrGreater()) {
		wstring errorText = L"Windows 8 or higher is required";
		ERR(L"%ls", errorText);
		RecordingFailedCallback(errorText);
		return S_FALSE;
	}

	if (m_IsRecording) {
		if (m_IsPaused) {
			m_IsPaused = false;
			if (RecordingStatusChangedCallback != NULL)
				RecordingStatusChangedCallback(STATUS_RECORDING);
		}
		return S_FALSE;
	}
	m_FrameDelays.clear();
	if (!path.empty()) {
		HRESULT hr = ConfigureOutputDir(path);
		if (FAILED(hr)) {
			return hr;
		}
	}
	cancellation_token token = m_TaskWrapperImpl->m_RecordTaskCts.get_token();
	concurrency::create_task([this, token, stream]() {
		HRESULT hr = CoInitializeEx(NULL, COINITBASE_MULTITHREADED | COINIT_DISABLE_OLE1DDE);
		hr = MFStartup(MF_VERSION, MFSTARTUP_LITE);
		RETURN_ON_BAD_HR(hr);
		{
			DXGI_OUTDUPL_DESC outputDuplDesc;
			RtlZeroMemory(&outputDuplDesc, sizeof(outputDuplDesc));
			CComPtr<ID3D11Device> pDevice;
			CComPtr<IDXGIOutputDuplication> pDeskDupl;
			std::unique_ptr<mouse_pointer> pMousePointer = make_unique<mouse_pointer>();
			std::unique_ptr<loopback_capture> pLoopbackCapture = make_unique<loopback_capture>();
			CComPtr<IDXGIOutput> pSelectedOutput;
			hr = GetOutputForDeviceName(m_DisplayOutputName, &pSelectedOutput);
			hr = InitializeDx(pSelectedOutput, &m_ImmediateContext, &pDevice, &pDeskDupl, &outputDuplDesc);
			RETURN_ON_BAD_HR(hr);

			D3D11_TEXTURE2D_DESC frameDesc;
			frameDesc.Width = outputDuplDesc.ModeDesc.Width;
			frameDesc.Height = outputDuplDesc.ModeDesc.Height;
			frameDesc.Format = outputDuplDesc.ModeDesc.Format;
			frameDesc.ArraySize = 1;
			frameDesc.BindFlags = D3D11_BIND_FLAG::D3D11_BIND_RENDER_TARGET;
			frameDesc.MiscFlags = D3D11_RESOURCE_MISC_GDI_COMPATIBLE;
			frameDesc.SampleDesc.Count = 1;
			frameDesc.SampleDesc.Quality = 0;
			frameDesc.MipLevels = 1;
			frameDesc.CPUAccessFlags = 0;
			frameDesc.Usage = D3D11_USAGE_DEFAULT;

			RECT sourceRect;
			sourceRect.left = 0;
			sourceRect.right = outputDuplDesc.ModeDesc.Width;
			sourceRect.top = 0;
			sourceRect.bottom = outputDuplDesc.ModeDesc.Height;

			RECT destRect = sourceRect;
			if (m_DestRect.right != 0
				|| m_DestRect.top != 0
				|| m_DestRect.bottom != 0
				|| m_DestRect.left != 0)
			{
				destRect = m_DestRect;
			}

			// create a "loopback capture has started" event
			HANDLE hStartedEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
			if (NULL == hStartedEvent) {
				ERR(L"CreateEvent failed: last error is %u", GetLastError());
				return S_FALSE;
			}
			CloseHandleOnExit closeStartedEvent(hStartedEvent);

			// create a "stop capturing now" event
			HANDLE hStopEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
			if (NULL == hStopEvent) {
				ERR(L"CreateEvent failed: last error is %u", GetLastError());
				return S_FALSE;
			}
			CloseHandleOnExit closeStopEvent(hStopEvent);
			bool recordAudio = m_RecorderMode == MODE_VIDEO && m_IsAudioEnabled;
			if (recordAudio)
			{
				CPrefs prefs(1, NULL, hr);
				prefs.m_bInt16 = true;
				// create arguments for loopback capture thread
				LoopbackCaptureThreadFunctionArguments threadArgs;
				threadArgs.hr = E_UNEXPECTED; // thread will overwrite this
				threadArgs.pMMDevice = prefs.m_pMMDevice;
				threadArgs.pCaptureInstance = pLoopbackCapture.get();
				threadArgs.bInt16 = prefs.m_bInt16;
				threadArgs.hFile = prefs.m_hFile;
				threadArgs.hStartedEvent = hStartedEvent;
				threadArgs.hStopEvent = hStopEvent;
				threadArgs.nFrames = 0;

				HANDLE hThread = CreateThread(
					NULL, 0,
					LoopbackCaptureThreadFunction, &threadArgs, 0, NULL
				);
				if (NULL == hThread) {
					ERR(L"CreateThread failed: last error is %u", GetLastError());
					return S_FALSE;
				}
				CloseHandleOnExit closeThread(hThread);
				WaitForSingleObjectEx(hStartedEvent, 1000, false);
				m_InputAudioSamplesPerSecond = pLoopbackCapture->GetInputSampleRate();
			}

			if (m_RecorderMode == MODE_VIDEO) {
				CComPtr<IMFByteStream> outputStream = nullptr;
				if (stream != NULL) {
					RETURN_ON_BAD_HR(hr = MFCreateMFByteStreamOnStream(stream, &outputStream));

				}
				RETURN_ON_BAD_HR(hr = InitializeVideoSinkWriter(m_OutputFullPath, outputStream, pDevice, sourceRect, destRect, &m_SinkWriter, &m_VideoStreamIndex, &m_AudioStreamIndex));
			}
			m_IsRecording = true;
			if (RecordingStatusChangedCallback != NULL)
				RecordingStatusChangedCallback(STATUS_RECORDING);

			RETURN_ON_BAD_HR(hr = pMousePointer->Initialize(m_ImmediateContext, pDevice));
			SetViewPort(m_ImmediateContext, outputDuplDesc.ModeDesc.Width, outputDuplDesc.ModeDesc.Height);

			ULONGLONG lastFrameStartPos = 0;
			pLoopbackCapture->ClearRecordedBytes();

			UINT64 videoFrameDurationMillis = 1000 / m_VideoFps;
			UINT64 videoFrameDuration100Nanos = videoFrameDurationMillis * 10 * 1000;
			UINT frameTimeout = 0;
			int frameNr = 0;
			CComPtr<ID3D11Texture2D> pPreviousFrameCopy = nullptr;
			m_LastFrame = std::chrono::high_resolution_clock::now();
			while (true)
			{
				CComPtr<IDXGIResource> pDesktopResource = nullptr;
				DXGI_OUTDUPL_FRAME_INFO FrameInfo;
				RtlZeroMemory(&FrameInfo, sizeof(FrameInfo));

				if (!m_IsRecording)
				{
					hr = S_OK;
					break;
				}
				if (token.is_canceled()) {
					return E_UNEXPECTED;
				}
				if (m_IsPaused) {
					wait(10);
					m_LastFrame = high_resolution_clock::now();
					pLoopbackCapture->ClearRecordedBytes();
					continue;
				}
				if (pDeskDupl) {
					pDeskDupl->ReleaseFrame();
					// Get new frame
					hr = pDeskDupl->AcquireNextFrame(
						frameTimeout,
						&FrameInfo,
						&pDesktopResource);
				}
				if (pDeskDupl == NULL
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
					if (pDeskDupl)
						pDeskDupl.Release();
					hr = InitializeDesktopDupl(pDevice, pSelectedOutput, &pDeskDupl, &outputDuplDesc);
					if (FAILED(hr))
					{
						_com_error err(hr);
						ERR(L"Reinitialized desktop duplication error: %s\n", err.ErrorMessage());
					}
					if (hr != E_ACCESSDENIED) {
						RETURN_ON_BAD_HR(hr);
					}
					wait(1);
					continue;
				}

				if (m_RecorderMode == MODE_SLIDESHOW
					|| m_RecorderMode == MODE_SNAPSHOT) {

					if (frameNr == 0 && FrameInfo.AccumulatedFrames == 0) {
						continue;
					}
				}

				UINT64 durationSinceLastFrame100Nanos = duration_cast<nanoseconds>(chrono::high_resolution_clock::now() - m_LastFrame).count() / 100;
				if (frameNr > 0 //always draw first frame 
					&& !m_IsFixedFramerate
					&& (!m_IsMousePointerEnabled || FrameInfo.PointerShapeBufferSize == 0)//always redraw when pointer changes if we draw pointer
					&& (hr == DXGI_ERROR_WAIT_TIMEOUT || (durationSinceLastFrame100Nanos) < videoFrameDuration100Nanos)) //skip if frame timeouted or duration is under our chosen framerate
				{
					if (hr == S_OK) {
						//we got a frame, but it's too soon, so we cache it and see if there are more changes.
						if (pPreviousFrameCopy == nullptr) {
							RETURN_ON_BAD_HR(hr = pDevice->CreateTexture2D(&frameDesc, NULL, &pPreviousFrameCopy));
						}
						CComPtr<ID3D11Texture2D> pAcquiredDesktopImage = nullptr;
						RETURN_ON_BAD_HR(hr = pDesktopResource->QueryInterface(IID_PPV_ARGS(&pAcquiredDesktopImage)));
						m_ImmediateContext->CopyResource(pPreviousFrameCopy, pAcquiredDesktopImage);
						pAcquiredDesktopImage.Release();
					}
					if (hr == S_OK || pPreviousFrameCopy == nullptr || durationSinceLastFrame100Nanos < videoFrameDuration100Nanos) {
						UINT32 delay = 1;
						if (durationSinceLastFrame100Nanos < videoFrameDuration100Nanos) {
							double d = (videoFrameDuration100Nanos - (durationSinceLastFrame100Nanos)) / 10 / 1000;
							delay = round(d);
						}

						wait(delay);
						continue;
					}
				}
				if (pPreviousFrameCopy && hr == DXGI_ERROR_WAIT_TIMEOUT) {
					hr = S_OK;
				}
				else {
					if (pPreviousFrameCopy) {
						pPreviousFrameCopy.Release();
						pPreviousFrameCopy = nullptr;
					}
				}
				RETURN_ON_BAD_HR(hr);

				m_LastFrame = high_resolution_clock::now();
				{
					std::vector<BYTE> audioData;
					if (recordAudio) {
						audioData = pLoopbackCapture->GetRecordedBytes();
					}

					CComPtr<ID3D11Texture2D> pFrameCopy;
					RETURN_ON_BAD_HR(hr = pDevice->CreateTexture2D(&frameDesc, NULL, &pFrameCopy));

					if (pPreviousFrameCopy) {
						m_ImmediateContext->CopyResource(pFrameCopy, pPreviousFrameCopy);
					}
					else {
						CComPtr<ID3D11Texture2D> pAcquiredDesktopImage = nullptr;
						RETURN_ON_BAD_HR(hr = pDesktopResource->QueryInterface(IID_PPV_ARGS(&pAcquiredDesktopImage)));
						m_ImmediateContext->CopyResource(pFrameCopy, pAcquiredDesktopImage);
					}

					SetDebugName(pFrameCopy, "FrameCopy");

					if (m_IsFixedFramerate && pPreviousFrameCopy == nullptr) {
						RETURN_ON_BAD_HR(hr = pDevice->CreateTexture2D(&frameDesc, NULL, &pPreviousFrameCopy));
						m_ImmediateContext->CopyResource(pPreviousFrameCopy, pFrameCopy);
						SetDebugName(pPreviousFrameCopy, "PreviousFrameCopy");
					}
					else if (!m_IsFixedFramerate && pPreviousFrameCopy) {
						pPreviousFrameCopy.Release();
					}

					if (m_IsMousePointerEnabled) {
						hr = pMousePointer->DrawMousePointer(m_ImmediateContext, pDevice, FrameInfo, sourceRect, frameDesc, pDeskDupl, pFrameCopy);
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
						return E_UNEXPECTED;
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
							EnqueueFrame(model);
							frameNr++;
						}
						else if (m_RecorderMode == MODE_SNAPSHOT) {
							hr = WriteFrameToImage(pFrameCopy, m_OutputFullPath.c_str());
							m_IsRecording = false;
						}
					}

					lastFrameStartPos += durationSinceLastFrame100Nanos;
					if (m_IsFixedFramerate)
					{
						wait(videoFrameDurationMillis);
					}
				}
			}

			SetEvent(hStopEvent);
			m_IsRecording = false;
			if (!m_IsDestructed) {
				if (RecordingStatusChangedCallback != NULL)
					RecordingStatusChangedCallback(STATUS_FINALIZING);
			}

			pDeskDupl->ReleaseFrame();
			if (pPreviousFrameCopy)
				pPreviousFrameCopy.Release();
			if (pMousePointer) {
				pMousePointer->CleanupResources();
			}
		}
		return hr;
	}).then([this, token](HRESULT hr) {
		m_IsRecording = false;
		if (!m_IsDestructed) {
			if (token.is_canceled()) {
				m_TaskWrapperImpl->m_RenderTaskCts.cancel();
			}
			else {
				if (!m_TaskWrapperImpl->m_RenderTask.is_done())
					m_TaskWrapperImpl->m_RenderTask.wait();
				LOG(L"Recording completed!");
			}
			if (m_SinkWriter) {
				m_SinkWriter->Finalize();

				//Dispose of MPEG4MediaSink 
				IMFMediaSink *pSink;
				if (SUCCEEDED(m_SinkWriter->GetServiceForStream(MF_SINK_WRITER_MEDIASINK, GUID_NULL, IID_PPV_ARGS(&pSink)))) {
					pSink->Shutdown();
					SafeRelease(&pSink);
				};
				m_SinkWriter->Release();
				m_SinkWriter = NULL;
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

			if (RecordingStatusChangedCallback)
				RecordingStatusChangedCallback(STATUS_IDLE);
			if (SUCCEEDED(hr)) {
				if (RecordingCompleteCallback)
					RecordingCompleteCallback(m_OutputFullPath, m_FrameDelays);
			}
			else {
				if (RecordingFailedCallback) {

					std::wstring errMsg;
					if (m_IsEncoderFailure) {
						errMsg = L"Write error in video encoder.";
						if (m_IsHardwareEncodingEnabled) {
							errMsg += L" If the problem persists, disabling hardware encoding may improve stability.";
						}
					}
					else {
						_com_error err(hr);
						errMsg = err.ErrorMessage();
					}
					RecordingFailedCallback(errMsg);
				}
			}
			while (m_WriteQueue.size() > 0) {
				FrameWriteModel model = m_WriteQueue.front();
				m_WriteQueue.pop();
			}
		}
		return hr;
	}).then([this](concurrency::task<HRESULT> t)
	{
		bool success = false;
		try {
			t.get();
			// .get() didn't throw, so we succeeded.
			success = true;
		}
		catch (const exception& e) {
			// handle error
			ERR(L"Exception in RecordTask: %s", e.what());
		}
		catch (...) {
			ERR(L"Exception in RecordTask");
		}
		if (!success) {
			if (RecordingFailedCallback != NULL) {
				RecordingFailedCallback(utilities::GetLastErrorStdWstr());
			}
		}
	});

	return S_OK;
}

void internal_recorder::EndRecording() {
	if (m_IsRecording) {
		m_IsPaused = false;
		m_IsRecording = false;
	}
}
void internal_recorder::PauseRecording() {
	if (m_IsRecording) {
		m_IsPaused = true;
		if (RecordingStatusChangedCallback != NULL)
			RecordingStatusChangedCallback(STATUS_PAUSED);
	}
}
void internal_recorder::ResumeRecording() {
	if (m_IsRecording) {
		if (m_IsPaused) {
			m_IsPaused = false;
			if (RecordingStatusChangedCallback != NULL)
				RecordingStatusChangedCallback(STATUS_RECORDING);
		}
	}
}

HRESULT internal_recorder::InitializeDx(IDXGIOutput *pDxgiOutput, ID3D11DeviceContext **ppContext, ID3D11Device **ppDevice, IDXGIOutputDuplication **ppDesktopDupl, DXGI_OUTDUPL_DESC *pOutputDuplDesc) {
	*ppContext = NULL;
	*ppDevice = NULL;
	*ppDesktopDupl = NULL;
	*pOutputDuplDesc;

	HRESULT hr(S_OK);
	DXGI_OUTDUPL_DESC OutputDuplDesc;
	CComPtr<ID3D11DeviceContext> m_ImmediateContext = NULL;
	CComPtr<ID3D11Device> pDevice = NULL;
	CComPtr<IDXGIOutputDuplication> pDeskDupl = NULL;
	int lresult(-1);
	D3D_FEATURE_LEVEL featureLevel;

	CComPtr<IDXGIAdapter> pDxgiAdapter;
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
	int numDriverTypes = driverTypes.size();
	// Create devices
	for (UINT DriverTypeIndex = 0; DriverTypeIndex < numDriverTypes; ++DriverTypeIndex)
	{
		hr = D3D11CreateDevice(
			pDxgiAdapter,
			driverTypes[DriverTypeIndex],
			nullptr,
#if _DEBUG 
			D3D11_CREATE_DEVICE_DEBUG,
#else
			0,
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
	hr = pMulti->SetMultithreadProtected(TRUE);
	RETURN_ON_BAD_HR(hr);
	pMulti.Release();
	if (pDevice == nullptr)
		return S_FALSE;
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

HRESULT internal_recorder::InitializeDesktopDupl(ID3D11Device *pDevice, IDXGIOutput *pDxgiOutput, IDXGIOutputDuplication **ppDesktopDupl, DXGI_OUTDUPL_DESC *pOutputDuplDesc) {
	*ppDesktopDupl = NULL;
	*pOutputDuplDesc;

	// Get DXGI device
	CComPtr<IDXGIDevice> pDxgiDevice;
	CComPtr<IDXGIOutputDuplication> pDeskDupl = NULL;
	DXGI_OUTDUPL_DESC OutputDuplDesc;
	HRESULT hr = S_OK;
	if (!pDxgiOutput) {
		hr = pDevice->QueryInterface(IID_PPV_ARGS(&pDxgiDevice));
		RETURN_ON_BAD_HR(hr);
		// Get DXGI adapter
		CComPtr<IDXGIAdapter> pDxgiAdapter;
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
	CComPtr<IDXGIOutput1> pDxgiOutput1;

	hr = pDxgiOutput->QueryInterface(IID_PPV_ARGS(&pDxgiOutput1));
	RETURN_ON_BAD_HR(hr);
	//pDxgiOutput.Release();

	// Create desktop duplication
	hr = pDxgiOutput1->DuplicateOutput(
		pDevice,
		&pDeskDupl);
	RETURN_ON_BAD_HR(hr);
	pDxgiOutput1.Release();

	// Create GUI drawing texture
	pDeskDupl->GetDesc(&OutputDuplDesc);

	pDxgiOutput1.Release();

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

HRESULT internal_recorder::GetOutputForDeviceName(std::wstring deviceName, IDXGIOutput **ppOutput) {
	HRESULT hr = S_OK;
	*ppOutput = NULL;
	if (deviceName != L"") {
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

HRESULT internal_recorder::InitializeVideoSinkWriter(std::wstring path, IMFByteStream *pOutStream, ID3D11Device* pDevice, RECT sourceRect, RECT destRect, IMFSinkWriter **ppWriter, DWORD *pVideoStreamIndex, DWORD *pAudioStreamIndex)
{
	*ppWriter = NULL;
	*pVideoStreamIndex = NULL;
	*pAudioStreamIndex = NULL;

	UINT pResetToken;
	CComPtr<IMFDXGIDeviceManager> pDeviceManager = NULL;
	CComPtr<IMFSinkWriter>        pSinkWriter = NULL;
	CComPtr<IMFMediaType>         pVideoMediaTypeOut = NULL;
	CComPtr<IMFMediaType>         pAudioMediaTypeOut = NULL;
	CComPtr<IMFMediaType>         pVideoMediaTypeIn = NULL;
	CComPtr<IMFMediaType>         pAudioMediaTypeIn = NULL;
	CComPtr<IMFAttributes>        pAttributes = NULL;

	DWORD audioStreamIndex;
	DWORD videoStreamIndex;
	RETURN_ON_BAD_HR(MFCreateDXGIDeviceManager(&pResetToken, &pDeviceManager));
	RETURN_ON_BAD_HR(pDeviceManager->ResetDevice(pDevice, pResetToken));
	const wchar_t *pathString = nullptr;
	if (!path.empty()) {
		pathString = path.c_str();
	}

	if (pOutStream == NULL)
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

	if (m_IsAudioEnabled) {
		// Set the output audio type.
		RETURN_ON_BAD_HR(MFCreateMediaType(&pAudioMediaTypeOut));
		RETURN_ON_BAD_HR(pAudioMediaTypeOut->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio));
		RETURN_ON_BAD_HR(pAudioMediaTypeOut->SetGUID(MF_MT_SUBTYPE, AUDIO_ENCODING_FORMAT));
		RETURN_ON_BAD_HR(pAudioMediaTypeOut->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS, m_AudioChannels));
		RETURN_ON_BAD_HR(pAudioMediaTypeOut->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, AUDIO_BITS_PER_SAMPLE));
		RETURN_ON_BAD_HR(pAudioMediaTypeOut->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, AUDIO_SAMPLES_PER_SECOND));
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
	CComPtr<IMFMediaSink> pMp4StreamSink;
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
	RETURN_ON_BAD_HR(pSinkWriter->SetInputMediaType(videoStreamIndex, pVideoMediaTypeIn, NULL));
	pVideoMediaTypeIn.Release();
	if (m_IsAudioEnabled) {
		RETURN_ON_BAD_HR(pSinkWriter->SetInputMediaType(audioStreamIndex, pAudioMediaTypeIn, NULL));
		pAudioMediaTypeIn.Release();
	}

	CComPtr<ICodecAPI> encoder;
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
		CComPtr<IMFVideoProcessorControl> videoProcessor = NULL;
		CComPtr<IMFSinkWriterEx>      pSinkWriterEx = NULL;
		RETURN_ON_BAD_HR(pSinkWriter->QueryInterface(&pSinkWriterEx));
		while (true) {
			CComPtr<IMFTransform> transform;
			RETURN_ON_BAD_HR(pSinkWriterEx->GetTransformForStream(videoStreamIndex, transformIndex, &transformType, &transform));
			if (transformType == MFT_CATEGORY_VIDEO_PROCESSOR) {
				RETURN_ON_BAD_HR(transform->QueryInterface(&videoProcessor));
				break;
			}
			transformIndex++;
		}
		SIZE constrictionSize;
		constrictionSize.cx = sourceWidth;
		constrictionSize.cy = sourceHeight;
		videoProcessor->SetSourceRectangle(&destRect);
		videoProcessor->SetConstrictionSize(NULL);
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
	IMFMediaType *pType,    // Pointer to an encoded video type.
	const GUID& subtype,    // Uncompressed subtype (eg, RGB-32, AYUV)
	IMFMediaType **ppType   // Receives a matching uncompressed video type.
)
{
	CComPtr<IMFMediaType> pTypeUncomp = NULL;

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


HRESULT internal_recorder::SetAttributeU32(CComPtr<ICodecAPI>& codec, const GUID& guid, UINT32 value)
{
	VARIANT val;
	val.vt = VT_UI4;
	val.uintVal = value;
	return codec->SetValue(&guid, &val);
}

void internal_recorder::EnqueueFrame(FrameWriteModel model) {
	m_WriteQueue.push(model);
	if (m_TaskWrapperImpl->m_RenderTask.is_done() && m_IsRecording)
	{
		m_LastEncodedSampleCount = 0;
		cancellation_token token = m_TaskWrapperImpl->m_RenderTaskCts.get_token();
		m_TaskWrapperImpl->m_RenderTask = concurrency::create_task([this, token]() {
			while (true) {
				if (!m_IsRecording && m_WriteQueue.size() == 0) {
					break;
				}
				if (m_WriteQueue.size() == 0) {
					wait(1);
					continue;
				}
				if (token.is_canceled()) {
					return;
				}
				HRESULT hr(S_OK);
				FrameWriteModel model = m_WriteQueue.front();

				if (m_RecorderMode == MODE_VIDEO) {
					hr = WriteFrameToVideo(model.StartPos, model.Duration, m_VideoStreamIndex, model.Frame);
					if (FAILED(hr)) {
						_com_error err(hr);
						ERR(L"Writing of video frame with start pos %lld ms failed: %s\n", (model.StartPos / 10 / 1000), err.ErrorMessage());
						m_IsEncoderFailure = true;
						m_IsRecording = false; //Stop recording if we fail
					}
				}
				else if (m_RecorderMode == MODE_SLIDESHOW) {
					wstring	path = m_OutputFolder + L"\\" + to_wstring(model.FrameNumber) + L".png";
					hr = WriteFrameToImage(model.Frame, path.c_str());
					LONGLONG startposMs = (model.StartPos / 10 / 1000);
					LONGLONG durationMs = (model.Duration / 10 / 1000);
					if (FAILED(hr)) {
						_com_error err(hr);
						ERR(L"Writing of video frame with start pos %lld ms failed: %s\n", startposMs, err.ErrorMessage());
						m_IsEncoderFailure = true;
						m_IsRecording = false; //Stop recording if we fail
					}
					else {

						m_FrameDelays.insert(std::pair<wstring, int>(path, model.FrameNumber == 0 ? 0 : (int)durationMs));
						ERR(L"Wrote video slideshow frame with start pos %lld ms and with duration %lld ms\n", startposMs, durationMs);
					}
				}
				m_WriteQueue.pop();
				model.Frame.Release();
				BYTE *data;
				//If the audio pCaptureInstance returns no data, i.e. the source is silent, we need to pad the PCM stream with zeros to give the media sink silence as input.
				//If we don't, the sink writer will begin throttling video frames because it expects audio samples to be delivered, and think they are delayed.
				if (m_IsAudioEnabled && model.Audio.size() == 0) {
					int frameCount = ceil(m_InputAudioSamplesPerSecond * ((double)model.Duration / 10 / 1000 / 1000));
					LONGLONG byteCount = frameCount * (AUDIO_BITS_PER_SAMPLE / 8)*m_AudioChannels;
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
						m_IsEncoderFailure = true;
						m_IsRecording = false; //Stop recording if we fail
					}
				}
			}
		}).then([this](concurrency::task<void> t)
		{
			try {
				t.get();
				// .get() didn't throw, so we succeeded.
			}
			catch (const exception& e) {
				// handle error
				ERR(L"Exception in RenderTask: %s", e.what());
			}
			catch (...) {
				ERR(L"Exception in RenderTask");
			}

		});
	}
}

std::string internal_recorder::NowToString()
{
	chrono::system_clock::time_point p = chrono::system_clock::now();
	time_t t = chrono::system_clock::to_time_t(p);

	std::stringstream ss;
	ss << std::put_time(std::localtime(&t), "%Y-%m-%d %X");
	string time = ss.str();
	std::replace(time.begin(), time.end(), ':', '-');
	return time;
}
HRESULT internal_recorder::WriteFrameToImage(ID3D11Texture2D* pAcquiredDesktopImage, LPCWSTR filePath)
{

	HRESULT hr = SaveWICTextureToFile(m_ImmediateContext, pAcquiredDesktopImage,
		IMAGE_ENCODER_FORMAT, filePath);
	return hr;
}

HRESULT internal_recorder::WriteFrameToVideo(ULONGLONG frameStartPos, ULONGLONG frameDuration, DWORD streamIndex, ID3D11Texture2D* pAcquiredDesktopImage)
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
		hr = MFCreateVideoSampleFromSurface(NULL, &pSample);
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
HRESULT internal_recorder::WriteAudioSamplesToVideo(ULONGLONG frameStartPos, ULONGLONG frameDuration, DWORD streamIndex, BYTE *pSrc, DWORD cbData)
{
	IMFMediaBuffer *pBuffer = NULL;
	BYTE *pData = NULL;
	// Create the media buffer.
	HRESULT hr = MFCreateMemoryBuffer(
		cbData,   // Amount of memory to allocate, in bytes.
		&pBuffer
	);

	// Lock the buffer to get a pointer to the memory.
	if (SUCCEEDED(hr))
	{
		hr = pBuffer->Lock(&pData, NULL, NULL);
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
		hr = MFCreateVideoSampleFromSurface(NULL, &pSample);
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