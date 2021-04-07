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
	SetOptions(options);
}

void Recorder::SetOptions(RecorderOptions^ options) {
	if (options && m_Rec) {
		if (options->VideoOptions) {
			m_Rec->SetVideoBitrate(options->VideoOptions->Bitrate);
			m_Rec->SetVideoQuality(options->VideoOptions->Quality);
			m_Rec->SetVideoFps(options->VideoOptions->Framerate);
			m_Rec->SetFixedFramerate(options->VideoOptions->IsFixedFramerate);
			m_Rec->SetH264EncoderProfile((UINT32)options->VideoOptions->EncoderProfile);
			m_Rec->SetVideoBitrateMode((UINT32)options->VideoOptions->BitrateMode);
			m_Rec->SetTakeSnapshotsWithVideo(options->VideoOptions->SnapshotsWithVideo);
			m_Rec->SetSnapshotsWithVideoInterval(options->VideoOptions->SnapshotsInterval);
			if (options->VideoOptions->SnapshotsDirectory != nullptr) {
				m_Rec->SetSnapshotDirectory(msclr::interop::marshal_as<std::wstring>(options->VideoOptions->SnapshotsDirectory));
			}
			switch (options->VideoOptions->SnapshotFormat)
			{
			case ImageFormat::BMP:
				m_Rec->SetSnapshotSaveFormat(GUID_ContainerFormatBmp);
				break;
			case ImageFormat::JPEG:
				m_Rec->SetSnapshotSaveFormat(GUID_ContainerFormatJpeg);
				break;
			case ImageFormat::TIFF:
				m_Rec->SetSnapshotSaveFormat(GUID_ContainerFormatTiff);
				break;
			default:
			case ImageFormat::PNG:
				m_Rec->SetSnapshotSaveFormat(GUID_ContainerFormatPng);
				break;
			}
		}
		if (options->DisplayOptions) {
			RECT rect;
			rect.left = options->DisplayOptions->Left;
			rect.top = options->DisplayOptions->Top;
			rect.right = options->DisplayOptions->Right;
			rect.bottom = options->DisplayOptions->Bottom;
			m_Rec->SetDestRectangle(rect);
			if (options->DisplayOptions->DisplayDevices) {
				std::vector<std::wstring> displays{};
				displays.reserve(options->DisplayOptions->DisplayDevices->Count);
				for each (String ^ str in options->DisplayOptions->DisplayDevices)
				{
					if (str) {
						displays.push_back(msclr::interop::marshal_as<std::wstring>(str));
					}
				}
				m_Rec->SetDisplayOutput(displays);
			}

			if (options->DisplayOptions->WindowHandles)
			{
				std::vector<HWND> windows{};
				windows.reserve(options->DisplayOptions->WindowHandles->Count);
				for each (IntPtr ^ ptr in options->DisplayOptions->WindowHandles)
				{
					HWND window = (HWND)ptr->ToPointer();
					if (window) {
						windows.push_back(window);
					}
				}
				m_Rec->SetWindowHandles(windows);
			}
		}
		if (options->AudioOptions) {
			m_Rec->SetAudioEnabled(options->AudioOptions->IsAudioEnabled);
			m_Rec->SetOutputDeviceEnabled(options->AudioOptions->IsOutputDeviceEnabled);
			m_Rec->SetInputDeviceEnabled(options->AudioOptions->IsInputDeviceEnabled);
			m_Rec->SetAudioBitrate((UINT32)options->AudioOptions->Bitrate);
			m_Rec->SetAudioChannels((UINT32)options->AudioOptions->Channels);
			if (options->AudioOptions->AudioOutputDevice != nullptr) {
				m_Rec->SetOutputDevice(msclr::interop::marshal_as<std::wstring>(options->AudioOptions->AudioOutputDevice));
			}
			if (options->AudioOptions->AudioInputDevice != nullptr) {
				m_Rec->SetInputDevice(msclr::interop::marshal_as<std::wstring>(options->AudioOptions->AudioInputDevice));
			}
			m_Rec->SetInputVolume(options->AudioOptions->InputVolume);
			m_Rec->SetOutputVolume(options->AudioOptions->OutputVolume);
		}
		if (options->MouseOptions) {
			m_Rec->SetMousePointerEnabled(options->MouseOptions->IsMousePointerEnabled);
			m_Rec->SetDetectMouseClicks(options->MouseOptions->IsMouseClicksDetected);
			m_Rec->SetMouseClickDetectionLMBColor(msclr::interop::marshal_as<std::string>(options->MouseOptions->MouseClickDetectionColor));
			m_Rec->SetMouseClickDetectionRMBColor(msclr::interop::marshal_as<std::string>(options->MouseOptions->MouseRightClickDetectionColor));
			m_Rec->SetMouseClickDetectionRadius(options->MouseOptions->MouseClickDetectionRadius);
			m_Rec->SetMouseClickDetectionDuration(options->MouseOptions->MouseClickDetectionDuration);
			m_Rec->SetMouseClickDetectionMode((UINT32)options->MouseOptions->MouseClickDetectionMode);
		}
		if (options->OverlayOptions) {
			m_Rec->SetOverlays(CreateNativeOverlayList(options->OverlayOptions->Overlays));
		}

		m_Rec->SetRecorderMode((UINT32)options->RecorderMode);
		m_Rec->SetRecorderApi((UINT32)options->RecorderApi);
		m_Rec->SetIsThrottlingDisabled(options->IsThrottlingDisabled);
		m_Rec->SetIsLowLatencyModeEnabled(options->IsLowLatencyEnabled);
		m_Rec->SetIsFastStartEnabled(options->IsMp4FastStartEnabled);
		m_Rec->SetIsHardwareEncodingEnabled(options->IsHardwareEncodingEnabled);
		m_Rec->SetIsFragmentedMp4Enabled(options->IsFragmentedMp4Enabled);
		m_Rec->SetIsLogEnabled(options->IsLogEnabled);
		if (options->LogFilePath != nullptr) {
			m_Rec->SetLogFilePath(msclr::interop::marshal_as<std::wstring>(options->LogFilePath));
		}
		m_Rec->SetLogSeverityLevel((UINT32)options->LogSeverityLevel);
	}
}

void Recorder::SetInputVolume(float volume)
{
	if (m_Rec)
	{
		m_Rec->SetInputVolume(volume);
	}
}

void Recorder::SetOutputVolume(float volume)
{
	if (m_Rec)
	{
		m_Rec->SetOutputVolume(volume);
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

List<Display^>^ ScreenRecorderLib::Recorder::GetDisplays()
{
	List<Display^>^ displays = gcnew List<Display^>();
	std::vector<IDXGIOutput*> outputs{};
	EnumOutputs(&outputs);

	for each (IDXGIOutput * output in outputs)
	{
		DXGI_OUTPUT_DESC desc;
		if (SUCCEEDED(output->GetDesc(&desc))) {
			if (desc.AttachedToDesktop) {
				auto display = gcnew Display();
				display->DeviceName = gcnew String(desc.DeviceName);
				display->PosX = desc.DesktopCoordinates.left;
				display->PosY = desc.DesktopCoordinates.top;
				display->Width = RectWidth(desc.DesktopCoordinates);
				display->Height = RectHeight(desc.DesktopCoordinates);
				display->MonitorName = gcnew String(GetMonitorName(desc.Monitor).c_str());
				displays->Add(display);
			}
		}
		output->Release();
	}
	return displays;
}

Size^ Recorder::GetCombinedOutputSizeForDisplays(List<String^>^ displays)
{
	std::vector<RECT> monitorRects{};
	RECT combinedRect{};
	std::vector<IDXGIOutput*> outputs{};
	EnumOutputs(&outputs);
	for each (IDXGIOutput * output in outputs) {
		DXGI_OUTPUT_DESC desc;
		if (SUCCEEDED(output->GetDesc(&desc))) {
			if (displays->Contains(gcnew String(desc.DeviceName))) {
				monitorRects.push_back(desc.DesktopCoordinates);
			}
		}
		output->Release();
	}
	GetCombinedRects(monitorRects, &combinedRect, nullptr);
	return gcnew Size(RectWidth(combinedRect), RectHeight(combinedRect));
}

Size^ Recorder::GetCombinedOutputSizeForWindows(List<IntPtr>^ windowHandles)
{
	std::vector<RECORDING_SOURCE> sources{};
	sources.reserve(windowHandles->Count);
	for each (IntPtr ^ ptr in windowHandles)
	{
		HWND hwnd = (HWND)ptr->ToPointer();
		if (hwnd) {
			RECORDING_SOURCE source{};
			source.Type = SourceType::Window;
			source.WindowHandle = hwnd;
			sources.push_back(source);
		}
	}
	std::vector<RECT> outputRects{};
	std::vector<std::pair< RECORDING_SOURCE, RECT>> validOutputs{};
	HRESULT hr = GetOutputRectsForRecordingSources(sources, &validOutputs);
	for each (auto const &pair in validOutputs) {
		outputRects.push_back(pair.second);
	}
	RECT deskBounds;
	GetCombinedRects(outputRects, &deskBounds, nullptr);
	return gcnew Size(RectWidth(deskBounds), RectHeight(deskBounds));
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

Recorder^ Recorder::CreateRecorder(RecorderOptions ^ options)
{
	Recorder^ rec = gcnew Recorder(options);
	return rec;
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
void Recorder::Record(System::Runtime::InteropServices::ComTypes::IStream^ stream) {
	SetupCallbacks();
	IStream *pNativeStream = (IStream*)Marshal::GetComInterfaceForObject(stream, System::Runtime::InteropServices::ComTypes::IStream::typeid).ToPointer();
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

std::vector<RECORDING_OVERLAY> Recorder::CreateNativeOverlayList(List<RecordingOverlay^>^ managedOverlays){
	std::vector<RECORDING_OVERLAY> overlays{};
	if (managedOverlays != nullptr) {
		for each (RecordingOverlay^ managedOverlay in managedOverlays)
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

			if (managedOverlay->GetType() == CameraCaptureOverlay::typeid) {
				CameraCaptureOverlay^ videoCaptureOverlay = (CameraCaptureOverlay^)managedOverlay;
				overlay.Type = OverlayType::CameraCapture;
				if (videoCaptureOverlay->CaptureDeviceName != nullptr) {
					overlay.Source = msclr::interop::marshal_as<std::wstring>(videoCaptureOverlay->CaptureDeviceName);
				}
				overlays.push_back(overlay);
			}
			else if (managedOverlay->GetType() == PictureOverlay::typeid) {
				PictureOverlay^ pictureOverlay = (PictureOverlay^)managedOverlay;
				overlay.Type = OverlayType::Picture;
				if (pictureOverlay->FilePath != nullptr) {
					overlay.Source = msclr::interop::marshal_as<std::wstring>(pictureOverlay->FilePath);
				}
				overlays.push_back(overlay);
			}
			else if (managedOverlay->GetType() == VideoOverlay::typeid) {
				VideoOverlay^ videoOverlay = (VideoOverlay^)managedOverlay;
				overlay.Type = OverlayType::Video;
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
