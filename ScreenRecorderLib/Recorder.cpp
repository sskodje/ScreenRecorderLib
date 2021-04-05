#include "Recorder.h"
#include <memory>
#include <msclr\marshal.h>
#include <msclr\marshal_cppstd.h>
#include "ManagedIStream.h"
using namespace ScreenRecorderLib;
using namespace nlohmann;

Recorder::Recorder(RecorderOptions^ options)
{
	lRec = new internal_recorder();
	SetOptions(options);
}

void Recorder::SetOptions(RecorderOptions^ options) {
	if (options && lRec) {
		if (options->VideoOptions) {
			lRec->SetVideoBitrate(options->VideoOptions->Bitrate);
			lRec->SetVideoQuality(options->VideoOptions->Quality);
			lRec->SetVideoFps(options->VideoOptions->Framerate);
			lRec->SetFixedFramerate(options->VideoOptions->IsFixedFramerate);
			lRec->SetH264EncoderProfile((UINT32)options->VideoOptions->EncoderProfile);
			lRec->SetVideoBitrateMode((UINT32)options->VideoOptions->BitrateMode);
			lRec->SetTakeSnapshotsWithVideo(options->VideoOptions->SnapshotsWithVideo);
			lRec->SetSnapshotsWithVideoInterval(options->VideoOptions->SnapshotsInterval);
			if (options->VideoOptions->SnapshotsDirectory != nullptr) {
				lRec->SetSnapshotDirectory(msclr::interop::marshal_as<std::wstring>(options->VideoOptions->SnapshotsDirectory));
			}
			switch (options->VideoOptions->SnapshotFormat)
			{
			case ImageFormat::BMP:
				lRec->SetSnapshotSaveFormat(GUID_ContainerFormatBmp);
				break;
			case ImageFormat::JPEG:
				lRec->SetSnapshotSaveFormat(GUID_ContainerFormatJpeg);
				break;
			case ImageFormat::TIFF:
				lRec->SetSnapshotSaveFormat(GUID_ContainerFormatTiff);
				break;
			default:
			case ImageFormat::PNG:
				lRec->SetSnapshotSaveFormat(GUID_ContainerFormatPng);
				break;
			}
		}
		if (options->DisplayOptions) {
			RECT rect;
			rect.left = options->DisplayOptions->Left;
			rect.top = options->DisplayOptions->Top;
			rect.right = options->DisplayOptions->Right;
			rect.bottom = options->DisplayOptions->Bottom;
			lRec->SetDestRectangle(rect);
			if (options->DisplayOptions->DisplayDevices) {
				std::vector<std::wstring> displays{};
				displays.reserve(options->DisplayOptions->DisplayDevices->Count);
				for each (String ^ str in options->DisplayOptions->DisplayDevices)
				{
					if (str) {
						displays.push_back(msclr::interop::marshal_as<std::wstring>(str));
					}
				}
				lRec->SetDisplayOutput(displays);
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
				lRec->SetWindowHandles(windows);
			}
		}
		if (options->AudioOptions) {
			lRec->SetAudioEnabled(options->AudioOptions->IsAudioEnabled);
			lRec->SetOutputDeviceEnabled(options->AudioOptions->IsOutputDeviceEnabled);
			lRec->SetInputDeviceEnabled(options->AudioOptions->IsInputDeviceEnabled);
			lRec->SetAudioBitrate((UINT32)options->AudioOptions->Bitrate);
			lRec->SetAudioChannels((UINT32)options->AudioOptions->Channels);
			if (options->AudioOptions->AudioOutputDevice != nullptr) {
				lRec->SetOutputDevice(msclr::interop::marshal_as<std::wstring>(options->AudioOptions->AudioOutputDevice));
			}
			if (options->AudioOptions->AudioInputDevice != nullptr) {
				lRec->SetInputDevice(msclr::interop::marshal_as<std::wstring>(options->AudioOptions->AudioInputDevice));
			}
			lRec->SetInputVolume(options->AudioOptions->InputVolume);
			lRec->SetOutputVolume(options->AudioOptions->OutputVolume);
		}
		if (options->MouseOptions) {
			lRec->SetMousePointerEnabled(options->MouseOptions->IsMousePointerEnabled);
			lRec->SetDetectMouseClicks(options->MouseOptions->IsMouseClicksDetected);
			lRec->SetMouseClickDetectionLMBColor(msclr::interop::marshal_as<std::string>(options->MouseOptions->MouseClickDetectionColor));
			lRec->SetMouseClickDetectionRMBColor(msclr::interop::marshal_as<std::string>(options->MouseOptions->MouseRightClickDetectionColor));
			lRec->SetMouseClickDetectionRadius(options->MouseOptions->MouseClickDetectionRadius);
			lRec->SetMouseClickDetectionDuration(options->MouseOptions->MouseClickDetectionDuration);
			lRec->SetMouseClickDetectionMode((UINT32)options->MouseOptions->MouseClickDetectionMode);
		}
		if (options->OverlayOptions) {
			lRec->SetOverlays(CreateNativeOverlayList(options->OverlayOptions->Overlays));
		}

		lRec->SetRecorderMode((UINT32)options->RecorderMode);
		lRec->SetRecorderApi((UINT32)options->RecorderApi);
		lRec->SetIsThrottlingDisabled(options->IsThrottlingDisabled);
		lRec->SetIsLowLatencyModeEnabled(options->IsLowLatencyEnabled);
		lRec->SetIsFastStartEnabled(options->IsMp4FastStartEnabled);
		lRec->SetIsHardwareEncodingEnabled(options->IsHardwareEncodingEnabled);
		lRec->SetIsFragmentedMp4Enabled(options->IsFragmentedMp4Enabled);
		lRec->SetIsLogEnabled(options->IsLogEnabled);
		if (options->LogFilePath != nullptr) {
			lRec->SetLogFilePath(msclr::interop::marshal_as<std::wstring>(options->LogFilePath));
		}
		lRec->SetLogSeverityLevel((UINT32)options->LogSeverityLevel);
	}
}

void Recorder::SetInputVolume(float volume)
{
	if (lRec)
	{
		lRec->SetInputVolume(volume);
	}
}

void Recorder::SetOutputVolume(float volume)
{
	if (lRec)
	{
		lRec->SetOutputVolume(volume);
	}
}

bool Recorder::SetExcludeFromCapture(System::IntPtr hwnd, bool isExcluded)
{
	return internal_recorder::SetExcludeFromCapture((HWND)hwnd.ToPointer(), isExcluded);
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

	HRESULT hr = CPrefs::list_devices(dFlow, &map);

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
	if (lRec) {
		delete lRec;
		lRec = nullptr;
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
	lRec->BeginRecording(pNativeStream);
}
void Recorder::Record(System::IO::Stream^ stream) {
	SetupCallbacks();
	m_ManagedStream = new ManagedIStream(stream);
	lRec->BeginRecording(m_ManagedStream);
}
void Recorder::Record(System::String^ path) {
	SetupCallbacks();
	std::wstring stdPathString = msclr::interop::marshal_as<std::wstring>(path);
	lRec->BeginRecording(stdPathString);
}
void Recorder::Pause() {
	lRec->PauseRecording();
}
void Recorder::Resume() {
	lRec->ResumeRecording();
}
void Recorder::Stop() {
	lRec->EndRecording();
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
	lRec->RecordingFailedCallback = cb;

}
void Recorder::CreateCompletionCallback() {
	InternalCompletionCallbackDelegate^ fp = gcnew InternalCompletionCallbackDelegate(this, &Recorder::EventComplete);
	_completedDelegateGcHandler = GCHandle::Alloc(fp);
	IntPtr ip = Marshal::GetFunctionPointerForDelegate(fp);
	CallbackCompleteFunction cb = static_cast<CallbackCompleteFunction>(ip.ToPointer());
	lRec->RecordingCompleteCallback = cb;
}
void Recorder::CreateStatusCallback() {
	InternalStatusCallbackDelegate^ fp = gcnew InternalStatusCallbackDelegate(this, &Recorder::EventStatusChanged);
	_statusChangedDelegateGcHandler = GCHandle::Alloc(fp);
	IntPtr ip = Marshal::GetFunctionPointerForDelegate(fp);
	CallbackStatusChangedFunction cb = static_cast<CallbackStatusChangedFunction>(ip.ToPointer());
	lRec->RecordingStatusChangedCallback = cb;
}
void Recorder::CreateSnapshotCallback() {
	InternalSnapshotCallbackDelegate^ fp = gcnew InternalSnapshotCallbackDelegate(this, &Recorder::EventSnapshotCreated);
	_snapshotDelegateGcHandler = GCHandle::Alloc(fp);
	IntPtr ip = Marshal::GetFunctionPointerForDelegate(fp);
	CallbackSnapshotFunction cb = static_cast<CallbackSnapshotFunction>(ip.ToPointer());
	lRec->RecordingSnapshotCreatedCallback = cb;
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
