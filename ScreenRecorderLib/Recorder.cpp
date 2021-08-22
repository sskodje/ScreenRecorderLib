#include "Recorder.h"
#include <memory>
#include <msclr\marshal.h>
#include <msclr\marshal_cppstd.h>
#include "ManagedIStream.h"
using namespace ScreenRecorderLib;
using namespace nlohmann;

Recorder::Recorder(RecorderOptions^ options)
{
	m_Rec = new RecordingManager();
	if (!options) {
		options = RecorderOptions::DefaultMainMonitor;
	}
	else if (!options->SourceOptions) {
		options->SourceOptions = SourceOptions::MainMonitor;
	}
	SetOptions(options);
}

void Recorder::SetOptions(RecorderOptions^ options) {
	if (options && m_Rec && !m_Rec->IsRecording()) {
		if (options->VideoEncoderOptions) {
			if (!options->VideoEncoderOptions->Encoder) {
				options->VideoEncoderOptions->Encoder = gcnew H264VideoEncoder();
			}
			ENCODER_OPTIONS* encoderOptions = nullptr;

			switch (options->VideoEncoderOptions->Encoder->EncodingFormat)
			{
			default:
			case VideoEncoderFormat::H264: {
				encoderOptions = new H264_ENCODER_OPTIONS();
				break;
			}
			case VideoEncoderFormat::H265: {
				encoderOptions = new H265_ENCODER_OPTIONS();
				break;
			}
			}
			if (options->VideoEncoderOptions->FrameSize && options->VideoEncoderOptions->FrameSize != ScreenSize::Empty) {
				encoderOptions->SetFrameSize(SIZE{ (long)round(options->VideoEncoderOptions->FrameSize->Width),(long)round(options->VideoEncoderOptions->FrameSize->Height) });
			}
			encoderOptions->SetVideoBitrateMode((UINT32)options->VideoEncoderOptions->Encoder->GetBitrateMode());
			encoderOptions->SetEncoderProfile((UINT32)options->VideoEncoderOptions->Encoder->GetEncoderProfile());
			encoderOptions->SetVideoBitrate(options->VideoEncoderOptions->Bitrate);
			encoderOptions->SetVideoQuality(options->VideoEncoderOptions->Quality);
			encoderOptions->SetVideoFps(options->VideoEncoderOptions->Framerate);
			encoderOptions->SetIsFixedFramerate(options->VideoEncoderOptions->IsFixedFramerate);
			encoderOptions->SetIsThrottlingDisabled(options->VideoEncoderOptions->IsThrottlingDisabled);
			encoderOptions->SetIsLowLatencyModeEnabled(options->VideoEncoderOptions->IsLowLatencyEnabled);
			encoderOptions->SetIsFastStartEnabled(options->VideoEncoderOptions->IsMp4FastStartEnabled);
			encoderOptions->SetIsHardwareEncodingEnabled(options->VideoEncoderOptions->IsHardwareEncodingEnabled);
			encoderOptions->SetIsFragmentedMp4Enabled(options->VideoEncoderOptions->IsFragmentedMp4Enabled);
			m_Rec->SetEncoderOptions(encoderOptions);
		}
		if (options->SnapshotOptions) {
			SNAPSHOT_OPTIONS* snapshotOptions = new SNAPSHOT_OPTIONS();
			snapshotOptions->SetTakeSnapshotsWithVideo(options->SnapshotOptions->SnapshotsWithVideo);
			snapshotOptions->SetSnapshotsWithVideoInterval(options->SnapshotOptions->SnapshotsIntervalMillis);
			if (options->SnapshotOptions->SnapshotsDirectory != nullptr) {
				snapshotOptions->SetSnapshotDirectory(msclr::interop::marshal_as<std::wstring>(options->SnapshotOptions->SnapshotsDirectory));
			}
			switch (options->SnapshotOptions->SnapshotFormat)
			{
			case ImageFormat::BMP:
				snapshotOptions->SetSnapshotSaveFormat(GUID_ContainerFormatBmp);
				break;
			case ImageFormat::JPEG:
				snapshotOptions->SetSnapshotSaveFormat(GUID_ContainerFormatJpeg);
				break;
			case ImageFormat::TIFF:
				snapshotOptions->SetSnapshotSaveFormat(GUID_ContainerFormatTiff);
				break;
			default:
			case ImageFormat::PNG:
				snapshotOptions->SetSnapshotSaveFormat(GUID_ContainerFormatPng);
				break;
			}
			m_Rec->SetSnapshotOptions(snapshotOptions);
		}
		if (options->SourceOptions) {
			if (options->SourceOptions->SourceRect && options->SourceOptions->SourceRect != ScreenRect::Empty) {
				RECT rect;
				rect.left = (LONG)round(options->SourceOptions->SourceRect->Left);
				rect.top = (LONG)round(options->SourceOptions->SourceRect->Top);
				rect.right = (LONG)round(options->SourceOptions->SourceRect->Right);
				rect.bottom = (LONG)round(options->SourceOptions->SourceRect->Bottom);
				m_Rec->SetSourceRectangle(rect);
			}
			m_Rec->SetRecordingSources(CreateRecordingSourceList(options->SourceOptions->RecordingSources));
		}
		if (options->AudioOptions) {
			AUDIO_OPTIONS* audioOptions = new AUDIO_OPTIONS();

			audioOptions->SetAudioEnabled(options->AudioOptions->IsAudioEnabled);
			audioOptions->SetOutputDeviceEnabled(options->AudioOptions->IsOutputDeviceEnabled);
			audioOptions->SetInputDeviceEnabled(options->AudioOptions->IsInputDeviceEnabled);
			audioOptions->SetAudioBitrate((UINT32)options->AudioOptions->Bitrate);
			audioOptions->SetAudioChannels((UINT32)options->AudioOptions->Channels);
			if (options->AudioOptions->AudioOutputDevice != nullptr) {
				audioOptions->SetOutputDevice(msclr::interop::marshal_as<std::wstring>(options->AudioOptions->AudioOutputDevice));
			}
			if (options->AudioOptions->AudioInputDevice != nullptr) {
				audioOptions->SetInputDevice(msclr::interop::marshal_as<std::wstring>(options->AudioOptions->AudioInputDevice));
			}
			audioOptions->SetInputVolume(options->AudioOptions->InputVolume);
			audioOptions->SetOutputVolume(options->AudioOptions->OutputVolume);
			m_Rec->SetAudioOptions(audioOptions);
		}
		if (options->MouseOptions) {
			MOUSE_OPTIONS* mouseOptions = new MOUSE_OPTIONS();

			mouseOptions->SetMousePointerEnabled(options->MouseOptions->IsMousePointerEnabled);
			mouseOptions->SetDetectMouseClicks(options->MouseOptions->IsMouseClicksDetected);
			mouseOptions->SetMouseClickDetectionLMBColor(msclr::interop::marshal_as<std::string>(options->MouseOptions->MouseClickDetectionColor));
			mouseOptions->SetMouseClickDetectionRMBColor(msclr::interop::marshal_as<std::string>(options->MouseOptions->MouseRightClickDetectionColor));
			mouseOptions->SetMouseClickDetectionRadius(options->MouseOptions->MouseClickDetectionRadius);
			mouseOptions->SetMouseClickDetectionDuration(options->MouseOptions->MouseClickDetectionDuration);
			mouseOptions->SetMouseClickDetectionMode((UINT32)options->MouseOptions->MouseClickDetectionMode);
			m_Rec->SetMouseOptions(mouseOptions);
		}
		if (options->OverlayOptions) {
			m_Rec->SetOverlays(CreateOverlayList(options->OverlayOptions->Overlays));
		}
		if (options->LogOptions) {
			m_Rec->SetIsLogEnabled(options->LogOptions->IsLogEnabled);
			if (options->LogOptions->LogFilePath != nullptr) {
				m_Rec->SetLogFilePath(msclr::interop::marshal_as<std::wstring>(options->LogOptions->LogFilePath));
			}
			m_Rec->SetLogSeverityLevel((UINT32)options->LogOptions->LogSeverityLevel);
		}

		m_Rec->SetRecorderMode((UINT32)options->RecorderMode);
		m_Rec->SetRecorderApi((UINT32)options->RecorderApi);
	}
}

void ScreenRecorderLib::Recorder::SetSourceRect(ScreenRect sourceRect)
{
	if (m_Rec)
	{
		RECT rect;
		rect.left = (LONG)round(sourceRect.Left);
		rect.top = (LONG)round(sourceRect.Top);
		rect.right = (LONG)round(sourceRect.Right);
		rect.bottom = (LONG)round(sourceRect.Bottom);
		m_Rec->SetSourceRectangle(rect);
	}
}

void Recorder::SetInputVolume(float volume)
{
	if (m_Rec)
	{
		m_Rec->GetAudioOptions()->SetInputVolume(volume);
	}
}

void Recorder::SetOutputVolume(float volume)
{
	if (m_Rec)
	{
		m_Rec->GetAudioOptions()->SetOutputVolume(volume);
	}
}

bool Recorder::SetExcludeFromCapture(System::IntPtr hwnd, bool isExcluded)
{
	return RecordingManager::SetExcludeFromCapture((HWND)hwnd.ToPointer(), isExcluded);
}

Dictionary<String^, String^>^ Recorder::GetSystemAudioDevices(AudioDeviceSource source)
{
	std::map<std::wstring, std::wstring> map;
	EDataFlow dFlow;

	switch (source)
	{
	default:
	case  AudioDeviceSource::OutputDevices:
		dFlow = eRender;
		break;
	case AudioDeviceSource::InputDevices:
		dFlow = eCapture;
		break;
	case AudioDeviceSource::All:
		dFlow = eAll;
		break;
	}

	Dictionary<String^, String^>^ devices = gcnew Dictionary<String^, String^>();

	HRESULT hr = AudioPrefs::list_devices(dFlow, &map);

	if (SUCCEEDED(hr))
	{
		if (map.size() != 0)
		{
			for (auto const& element : map) {
				devices->Add(gcnew String(element.first.c_str()), gcnew String(element.second.c_str()));
			}
		}
	}
	return devices;
}

Dictionary<String^, String^>^ ScreenRecorderLib::Recorder::GetSystemVideoCaptureDevices()
{
	std::map<std::wstring, std::wstring> map;
	HRESULT hr = EnumVideoCaptureDevices(&map);
	Dictionary<String^, String^>^ devices = gcnew Dictionary<String^, String^>();
	if (SUCCEEDED(hr))
	{
		if (map.size() != 0)
		{
			for (auto const& element : map) {
				devices->Add(gcnew String(element.first.c_str()), gcnew String(element.second.c_str()));
			}
		}
	}
	return devices;
}

List<RecordableDisplay^>^ ScreenRecorderLib::Recorder::GetDisplays()
{
	List<RecordableDisplay^>^ displays = gcnew List<RecordableDisplay^>();
	std::vector<IDXGIOutput*> outputs{};
	EnumOutputs(&outputs);

	for each (IDXGIOutput * output in outputs)
	{
		DXGI_OUTPUT_DESC desc;
		if (SUCCEEDED(output->GetDesc(&desc))) {
			if (desc.AttachedToDesktop) {
				auto display = gcnew RecordableDisplay();
				display->DeviceName = gcnew String(desc.DeviceName);
				display->MonitorName = gcnew String(GetMonitorName(desc.Monitor).c_str());
				displays->Add(display);
			}
		}
		output->Release();
	}
	return displays;
}

List<RecordableWindow^>^ ScreenRecorderLib::Recorder::GetWindows()
{
	List<RecordableWindow^>^ windows = gcnew List<RecordableWindow^>();
	for each (const Window & win in EnumerateWindows())
	{
		RecordableWindow^ recordableWin = gcnew RecordableWindow(gcnew String(win.Title().c_str()), IntPtr(win.Hwnd()));
		windows->Add(recordableWin);
	}
	return windows;
}

OutputDimensions^ ScreenRecorderLib::Recorder::GetOutputDimensionsForRecordingSources(IEnumerable<RecordingSourceBase^>^ recordingSources)
{
	std::vector<RECORDING_SOURCE> sources = CreateRecordingSourceList(recordingSources);

	std::vector<RECT> outputRects{};
	std::vector<std::pair< RECORDING_SOURCE, RECT>> validOutputs{};
	HRESULT hr = GetOutputRectsForRecordingSources(sources, &validOutputs);

	OutputDimensions^ outputDimensions = gcnew OutputDimensions();
	outputDimensions->OutputCoordinates = gcnew List<SourceCoordinates^>();
	for each (auto const& pair in validOutputs) {
		RECORDING_SOURCE nativeSource = pair.first;
		RECT nativeSourceRect = pair.second;
		outputRects.push_back(nativeSourceRect);
		switch (nativeSource.Type)
		{
		case RecordingSourceType::Window: {
			for each (RecordingSourceBase ^ recordingSource in recordingSources)
			{
				if (isinst<WindowRecordingSource^>(recordingSource)) {
					WindowRecordingSource^ windowRecordingSource = (WindowRecordingSource^)recordingSource;
					HWND hwnd = (HWND)windowRecordingSource->Handle.ToPointer();
					HWND nativeSourceHwnd = static_cast<HWND>(nativeSource.Source);
					if (hwnd == nativeSourceHwnd) {
						outputDimensions->OutputCoordinates->Add(gcnew SourceCoordinates(recordingSource, gcnew ScreenRect(nativeSourceRect.left, nativeSourceRect.top, RectWidth(nativeSourceRect), RectHeight(nativeSourceRect))));
					}
				}
			}
		}
		case RecordingSourceType::Display: {
			for each (RecordingSourceBase ^ recordingSource in recordingSources)
			{
				if (isinst<DisplayRecordingSource^>(recordingSource)) {
					DisplayRecordingSource^ displayRecordingSource = (DisplayRecordingSource^)recordingSource;
					std::wstring nativeSourceDevice = *static_cast<std::wstring*>(nativeSource.Source);
					if ((gcnew String(nativeSourceDevice.c_str()))->Equals(displayRecordingSource->DeviceName)) {
						outputDimensions->OutputCoordinates->Add(gcnew SourceCoordinates(recordingSource, gcnew ScreenRect(nativeSourceRect.left, nativeSourceRect.top, RectWidth(nativeSourceRect), RectHeight(nativeSourceRect))));
					}
				}
			}
		}
		default:
			break;
		}
	}
	RECT deskBounds;
	GetCombinedRects(outputRects, &deskBounds, nullptr);
	outputDimensions->CombinedOutputSize = gcnew ScreenSize(RectWidth(deskBounds), RectHeight(deskBounds));
	return outputDimensions;
}

Recorder::~Recorder()
{
	this->!Recorder();
	GC::SuppressFinalize(this);
}

Recorder::!Recorder() {
	if (m_Rec) {
		delete m_Rec;
		m_Rec = nullptr;
	}
	if (m_ManagedStream) {
		delete m_ManagedStream;
		m_ManagedStream = nullptr;
	}
	ClearCallbacks();
}

Recorder^ Recorder::CreateRecorder() {
	return gcnew Recorder(nullptr);
}

Recorder^ Recorder::CreateRecorder(RecorderOptions^ options)
{
	Recorder^ rec = gcnew Recorder(options);
	return rec;
}
void Recorder::Record(System::Runtime::InteropServices::ComTypes::IStream^ stream) {
	SetupCallbacks();
	IStream* pNativeStream = (IStream*)Marshal::GetComInterfaceForObject(stream, System::Runtime::InteropServices::ComTypes::IStream::typeid).ToPointer();
	m_Rec->BeginRecording(pNativeStream);
}
void Recorder::Record(System::IO::Stream^ stream) {
	SetupCallbacks();
	m_ManagedStream = new ManagedIStream(stream);
	m_Rec->BeginRecording(m_ManagedStream);
}
void Recorder::Record(System::String^ path) {
	SetupCallbacks();
	std::wstring stdPathString = msclr::interop::marshal_as<std::wstring>(path);
	m_Rec->BeginRecording(stdPathString);
}
void Recorder::Pause() {
	m_Rec->PauseRecording();
}
void Recorder::Resume() {
	m_Rec->ResumeRecording();
}
void Recorder::Stop() {
	m_Rec->EndRecording();
}

void Recorder::SetupCallbacks() {
	CreateErrorCallback();
	CreateCompletionCallback();
	CreateStatusCallback();
	CreateSnapshotCallback();
}

void Recorder::ClearCallbacks() {
	if (_statusChangedDelegateGcHandler.IsAllocated)
		_statusChangedDelegateGcHandler.Free();
	if (_errorDelegateGcHandler.IsAllocated)
		_errorDelegateGcHandler.Free();
	if (_completedDelegateGcHandler.IsAllocated)
		_completedDelegateGcHandler.Free();
	if (_snapshotDelegateGcHandler.IsAllocated)
		_snapshotDelegateGcHandler.Free();
}

std::vector<RECORDING_SOURCE> Recorder::CreateRecordingSourceList(IEnumerable<RecordingSourceBase^>^ managedSources) {
	std::vector<RECORDING_SOURCE> sources{};
	if (managedSources) {
		for each (RecordingSourceBase ^ source in managedSources)
		{
			if (isinst<DisplayRecordingSource^>(source)) {
				DisplayRecordingSource^ displaySource = (DisplayRecordingSource^)source;
				if (!String::IsNullOrEmpty(displaySource->DeviceName)) {
					std::wstring deviceName = msclr::interop::marshal_as<std::wstring>(displaySource->DeviceName);
					CComPtr<IDXGIOutput> output;
					HRESULT hr = GetOutputForDeviceName(deviceName, &output);
					if (SUCCEEDED(hr)) {
						DXGI_OUTPUT_DESC desc;
						hr = output->GetDesc(&desc);
						if (SUCCEEDED(hr)) {
							RECORDING_SOURCE source{};
							source.Type = RecordingSourceType::Display;
							source.Source = new std::wstring(desc.DeviceName);
							if (displaySource->SourceRect
								&& displaySource->SourceRect != ScreenRect::Empty
								&& displaySource->SourceRect->Right > displaySource->SourceRect->Left
								&& displaySource->SourceRect->Bottom > displaySource->SourceRect->Top) {
								source.SourceRect = RECT{
									(long)displaySource->SourceRect->Left,
									(long)displaySource->SourceRect->Top,
									(long)displaySource->SourceRect->Right,
									(long)displaySource->SourceRect->Bottom };
							}
							if (displaySource->OutputSize
								&& displaySource->OutputSize != ScreenSize::Empty
								&& displaySource->OutputSize->Width > 0
								&& displaySource->OutputSize->Height > 0) {
								source.OutputSize = SIZE{
									(long)displaySource->OutputSize->Width,
									(long)displaySource->OutputSize->Height
								};
							}
							if (displaySource->Position && displaySource->Position != ScreenPoint::Empty) {
								source.Position = POINT
								{
									(long)displaySource->Position->Left,
									(long)displaySource->Position->Top
								};
							}
							source.IsCursorCaptureEnabled = displaySource->IsCursorCaptureEnabled;
							if (std::find(sources.begin(), sources.end(), source) == sources.end()) {
								sources.insert(sources.end(), source);
							}
						}
					}
				}
			}
			else if (isinst<WindowRecordingSource^>(source)) {
				WindowRecordingSource^ windowSource = (WindowRecordingSource^)source;
				if (windowSource->Handle != IntPtr::Zero) {
					HWND windowHandle = (HWND)(windowSource->Handle.ToPointer());
					if (!IsIconic(windowHandle) && IsWindow(windowHandle)) {
						RECORDING_SOURCE source{};
						source.Type = RecordingSourceType::Window;
						source.Source = windowHandle;
						if (windowSource->OutputSize
							&& windowSource->OutputSize != ScreenSize::Empty
							&& windowSource->OutputSize->Width > 0
							&& windowSource->OutputSize->Height > 0) {
							source.OutputSize = SIZE{
								(long)windowSource->OutputSize->Width,
								(long)windowSource->OutputSize->Height
							};
						}
						if (windowSource->Position && windowSource->Position != ScreenPoint::Empty) {
							source.Position = POINT
							{
								(long)windowSource->Position->Left,
								(long)windowSource->Position->Top
							};
						}
						source.IsCursorCaptureEnabled = windowSource->IsCursorCaptureEnabled;
						if (std::find(sources.begin(), sources.end(), source) == sources.end()) {
							sources.insert(sources.end(), source);
						}
					}
				}
			}
		}
		std::vector<RECORDING_SOURCE> sourceVector{};
		for each (auto obj in sources)
		{
			sourceVector.push_back(obj);
		}
		return sourceVector;
	}
	return std::vector<RECORDING_SOURCE>();
}

std::vector<RECORDING_OVERLAY> Recorder::CreateOverlayList(IEnumerable<RecordingOverlayBase^>^ managedOverlays) {
	std::vector<RECORDING_OVERLAY> overlays{};
	if (managedOverlays) {
		for each (RecordingOverlayBase ^ managedOverlay in managedOverlays)
		{
			RECORDING_OVERLAY overlay{};
			overlay.Offset = POINT{ managedOverlay->OffsetX, managedOverlay->OffsetY };
			overlay.Size = SIZE{ managedOverlay->Width, managedOverlay->Height };
			switch (managedOverlay->AnchorPosition)
			{
			case Anchor::BottomLeft:
				overlay.Anchor = OverlayAnchor::BottomLeft;
				break;
			case Anchor::BottomRight:
				overlay.Anchor = OverlayAnchor::BottomRight;
				break;
			case Anchor::TopLeft:
				overlay.Anchor = OverlayAnchor::TopLeft;
				break;
			case Anchor::TopRight:
				overlay.Anchor = OverlayAnchor::TopRight;
				break;
			default:
				break;
			}

			if (isinst<CameraCaptureOverlay^>(managedOverlay)) {
				CameraCaptureOverlay^ videoCaptureOverlay = (CameraCaptureOverlay^)managedOverlay;
				overlay.Type = RecordingOverlayType::CameraCapture;
				if (videoCaptureOverlay->CaptureDeviceName != nullptr) {
					overlay.Source = msclr::interop::marshal_as<std::wstring>(videoCaptureOverlay->CaptureDeviceName);
				}
				overlays.push_back(overlay);
			}
			else if (isinst<PictureOverlay^>(managedOverlay)) {
				PictureOverlay^ pictureOverlay = (PictureOverlay^)managedOverlay;
				overlay.Type = RecordingOverlayType::Picture;
				if (pictureOverlay->FilePath != nullptr) {
					overlay.Source = msclr::interop::marshal_as<std::wstring>(pictureOverlay->FilePath);
				}
				overlays.push_back(overlay);
			}
			else if (isinst<VideoOverlay^>(managedOverlay)) {
				VideoOverlay^ videoOverlay = (VideoOverlay^)managedOverlay;
				overlay.Type = RecordingOverlayType::Video;
				if (videoOverlay->FilePath != nullptr) {
					overlay.Source = msclr::interop::marshal_as<std::wstring>(videoOverlay->FilePath);
				}
				overlays.push_back(overlay);
			}
		}
	}
	return overlays;
}

void Recorder::CreateErrorCallback() {
	InternalErrorCallbackDelegate^ fp = gcnew InternalErrorCallbackDelegate(this, &Recorder::EventFailed);
	_errorDelegateGcHandler = GCHandle::Alloc(fp);
	IntPtr ip = Marshal::GetFunctionPointerForDelegate(fp);
	CallbackErrorFunction cb = static_cast<CallbackErrorFunction>(ip.ToPointer());
	m_Rec->RecordingFailedCallback = cb;

}
void Recorder::CreateCompletionCallback() {
	InternalCompletionCallbackDelegate^ fp = gcnew InternalCompletionCallbackDelegate(this, &Recorder::EventComplete);
	_completedDelegateGcHandler = GCHandle::Alloc(fp);
	IntPtr ip = Marshal::GetFunctionPointerForDelegate(fp);
	CallbackCompleteFunction cb = static_cast<CallbackCompleteFunction>(ip.ToPointer());
	m_Rec->RecordingCompleteCallback = cb;
}
void Recorder::CreateStatusCallback() {
	InternalStatusCallbackDelegate^ fp = gcnew InternalStatusCallbackDelegate(this, &Recorder::EventStatusChanged);
	_statusChangedDelegateGcHandler = GCHandle::Alloc(fp);
	IntPtr ip = Marshal::GetFunctionPointerForDelegate(fp);
	CallbackStatusChangedFunction cb = static_cast<CallbackStatusChangedFunction>(ip.ToPointer());
	m_Rec->RecordingStatusChangedCallback = cb;
}
void Recorder::CreateSnapshotCallback() {
	InternalSnapshotCallbackDelegate^ fp = gcnew InternalSnapshotCallbackDelegate(this, &Recorder::EventSnapshotCreated);
	_snapshotDelegateGcHandler = GCHandle::Alloc(fp);
	IntPtr ip = Marshal::GetFunctionPointerForDelegate(fp);
	CallbackSnapshotFunction cb = static_cast<CallbackSnapshotFunction>(ip.ToPointer());
	m_Rec->RecordingSnapshotCreatedCallback = cb;
}
void Recorder::EventComplete(std::wstring str, fifo_map<std::wstring, int> delays)
{
	ClearCallbacks();

	List<FrameData^>^ frameInfos = gcnew List<FrameData^>();

	for (auto x : delays) {
		frameInfos->Add(gcnew FrameData(gcnew String(x.first.c_str()), x.second));
	}
	if (m_ManagedStream) {
		delete m_ManagedStream;
		m_ManagedStream = nullptr;
	}
	RecordingCompleteEventArgs^ args = gcnew RecordingCompleteEventArgs(gcnew String(str.c_str()), frameInfos);
	OnRecordingComplete(this, args);
}
void Recorder::EventFailed(std::wstring str)
{
	ClearCallbacks();
	if (m_ManagedStream) {
		delete m_ManagedStream;
		m_ManagedStream = nullptr;
	}
	OnRecordingFailed(this, gcnew RecordingFailedEventArgs(gcnew String(str.c_str())));
}
void Recorder::EventStatusChanged(int status)
{
	RecorderStatus recorderStatus = (RecorderStatus)status;
	Status = recorderStatus;
	OnStatusChanged(this, gcnew RecordingStatusEventArgs(recorderStatus));
}

void ScreenRecorderLib::Recorder::EventSnapshotCreated(std::wstring str)
{
	OnSnapshotSaved(this, gcnew SnapshotSavedEventArgs(gcnew String(str.c_str())));
}