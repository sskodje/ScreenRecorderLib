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
			encoderOptions->SetVideoBitrateMode((UINT32)options->VideoEncoderOptions->Encoder->GetBitrateMode());
			encoderOptions->SetEncoderProfile((UINT32)options->VideoEncoderOptions->Encoder->GetEncoderProfile());
			encoderOptions->SetVideoBitrate(options->VideoEncoderOptions->Bitrate);
			encoderOptions->SetVideoQuality(options->VideoEncoderOptions->Quality);
			encoderOptions->SetVideoFps(options->VideoEncoderOptions->Framerate);
			encoderOptions->SetFixedFramerate(options->VideoEncoderOptions->IsFixedFramerate);
			encoderOptions->SetThrottlingDisabled(options->VideoEncoderOptions->IsThrottlingDisabled);
			encoderOptions->SetLowLatencyModeEnabled(options->VideoEncoderOptions->IsLowLatencyEnabled);
			encoderOptions->SetFastStartEnabled(options->VideoEncoderOptions->IsMp4FastStartEnabled);
			encoderOptions->SetHardwareEncodingEnabled(options->VideoEncoderOptions->IsHardwareEncodingEnabled);
			encoderOptions->SetFragmentedMp4Enabled(options->VideoEncoderOptions->IsFragmentedMp4Enabled);
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
			m_Rec->SetRecordingSources(CreateRecordingSourceList(options->SourceOptions->RecordingSources));
		}
		if (options->OutputOptions) {
			OUTPUT_OPTIONS* outputOptions = new OUTPUT_OPTIONS();
			if (options->OutputOptions->SourceRect && !options->OutputOptions->SourceRect->Equals(ScreenRect::Empty)) {
				outputOptions->SetSourceRectangle(options->OutputOptions->SourceRect->ToRECT());
			}
			if (options->OutputOptions->OutputFrameSize && !options->OutputOptions->OutputFrameSize->Equals(ScreenSize::Empty)) {
				outputOptions->SetFrameSize(SIZE{ (long)round(options->OutputOptions->OutputFrameSize->Width),(long)round(options->OutputOptions->OutputFrameSize->Height) });
			}
			outputOptions->SetRecorderMode(static_cast<RecorderModeInternal>(options->OutputOptions->RecorderMode));
			outputOptions->SetStretch(static_cast<TextureStretchMode>(options->OutputOptions->Stretch));
			m_Rec->SetOutputOptions(outputOptions);
		}
		if (options->AudioOptions) {
			AUDIO_OPTIONS* audioOptions = new AUDIO_OPTIONS();

			if (options->AudioOptions->IsAudioEnabled.HasValue) {
				audioOptions->SetAudioEnabled(options->AudioOptions->IsAudioEnabled.Value);
			}
			if (options->AudioOptions->IsOutputDeviceEnabled.HasValue) {
				audioOptions->SetOutputDeviceEnabled(options->AudioOptions->IsOutputDeviceEnabled.Value);
			}
			if (options->AudioOptions->IsInputDeviceEnabled.HasValue) {
				audioOptions->SetInputDeviceEnabled(options->AudioOptions->IsInputDeviceEnabled.Value);
			}
			if (options->AudioOptions->Bitrate.HasValue) {
				audioOptions->SetAudioBitrate((UINT32)options->AudioOptions->Bitrate.Value);
			}
			if (options->AudioOptions->Channels.HasValue) {
				audioOptions->SetAudioChannels((UINT32)options->AudioOptions->Channels.Value);
			}
			if (options->AudioOptions->AudioOutputDevice != nullptr) {
				audioOptions->SetOutputDevice(msclr::interop::marshal_as<std::wstring>(options->AudioOptions->AudioOutputDevice));
			}
			if (options->AudioOptions->AudioInputDevice != nullptr) {
				audioOptions->SetInputDevice(msclr::interop::marshal_as<std::wstring>(options->AudioOptions->AudioInputDevice));
			}
			if (options->AudioOptions->InputVolume.HasValue) {
				audioOptions->SetInputVolume(options->AudioOptions->InputVolume.Value);
			}
			if (options->AudioOptions->OutputVolume.HasValue) {
				audioOptions->SetOutputVolume(options->AudioOptions->OutputVolume.Value);
			}
			m_Rec->SetAudioOptions(audioOptions);
		}
		if (options->MouseOptions) {
			MOUSE_OPTIONS* mouseOptions = new MOUSE_OPTIONS();

			if (options->MouseOptions->IsMousePointerEnabled.HasValue) {
				mouseOptions->SetMousePointerEnabled(options->MouseOptions->IsMousePointerEnabled.Value);
			}
			if (options->MouseOptions->IsMouseClicksDetected.HasValue) {
				mouseOptions->SetDetectMouseClicks(options->MouseOptions->IsMouseClicksDetected.Value);
			}
			if (!String::IsNullOrEmpty(options->MouseOptions->MouseLeftClickDetectionColor)) {
				mouseOptions->SetMouseClickDetectionLMBColor(msclr::interop::marshal_as<std::string>(options->MouseOptions->MouseLeftClickDetectionColor));
			}
			if (!String::IsNullOrEmpty(options->MouseOptions->MouseRightClickDetectionColor)) {
				mouseOptions->SetMouseClickDetectionRMBColor(msclr::interop::marshal_as<std::string>(options->MouseOptions->MouseRightClickDetectionColor));
			}
			if (options->MouseOptions->MouseClickDetectionRadius.HasValue) {
				mouseOptions->SetMouseClickDetectionRadius(options->MouseOptions->MouseClickDetectionRadius.Value);
			}
			if (options->MouseOptions->MouseClickDetectionDuration.HasValue) {
				mouseOptions->SetMouseClickDetectionDuration(options->MouseOptions->MouseClickDetectionDuration.Value);
			}
			mouseOptions->SetMouseClickDetectionMode((UINT32)options->MouseOptions->MouseClickDetectionMode);
			m_Rec->SetMouseOptions(mouseOptions);
		}
		if (options->OverlayOptions) {
			m_Rec->SetOverlays(CreateOverlayList(options->OverlayOptions->Overlays));
		}
		if (options->LogOptions) {
			m_Rec->SetLogEnabled(options->LogOptions->IsLogEnabled);
			if (options->LogOptions->LogFilePath != nullptr) {
				m_Rec->SetLogFilePath(msclr::interop::marshal_as<std::wstring>(options->LogOptions->LogFilePath));
			}
			m_Rec->SetLogSeverityLevel((UINT32)options->LogOptions->LogSeverityLevel);
		}
	}
}

DynamicOptionsBuilder^ ScreenRecorderLib::Recorder::GetDynamicOptionsBuilder()
{
	return gcnew DynamicOptionsBuilder(this);
}

void Recorder::SetDynamicOptions(DynamicOptions^ options)
{
	if (options->AudioOptions) {
		if (options->AudioOptions->IsOutputDeviceEnabled.HasValue) {
			m_Rec->GetAudioOptions()->SetOutputDeviceEnabled(options->AudioOptions->IsOutputDeviceEnabled.Value);
		}
		if (options->AudioOptions->IsInputDeviceEnabled.HasValue) {
			m_Rec->GetAudioOptions()->SetInputDeviceEnabled(options->AudioOptions->IsInputDeviceEnabled.Value);
		}
		if (options->AudioOptions->InputVolume.HasValue) {
			m_Rec->GetAudioOptions()->SetInputVolume(options->AudioOptions->InputVolume.Value);
		}
		if (options->AudioOptions->OutputVolume.HasValue) {
			m_Rec->GetAudioOptions()->SetOutputVolume(options->AudioOptions->OutputVolume.Value);
		}
	}
	if (options->MouseOptions) {
		if (options->MouseOptions->IsMouseClicksDetected.HasValue) {
			m_Rec->GetMouseOptions()->SetDetectMouseClicks(options->MouseOptions->IsMouseClicksDetected.Value);
		}
		if (options->MouseOptions->IsMousePointerEnabled.HasValue) {
			m_Rec->GetMouseOptions()->SetMousePointerEnabled(options->MouseOptions->IsMousePointerEnabled.Value);
		}
		if (!String::IsNullOrEmpty(options->MouseOptions->MouseLeftClickDetectionColor)) {
			m_Rec->GetMouseOptions()->SetMouseClickDetectionLMBColor(msclr::interop::marshal_as<std::string>(options->MouseOptions->MouseLeftClickDetectionColor));
		}
		if (!String::IsNullOrEmpty(options->MouseOptions->MouseRightClickDetectionColor)) {
			m_Rec->GetMouseOptions()->SetMouseClickDetectionRMBColor(msclr::interop::marshal_as<std::string>(options->MouseOptions->MouseRightClickDetectionColor));
		}
		if (options->MouseOptions->MouseClickDetectionRadius.HasValue) {
			m_Rec->GetMouseOptions()->SetMouseClickDetectionRadius(options->MouseOptions->MouseClickDetectionRadius.Value);
		}
		if (options->MouseOptions->MouseClickDetectionDuration.HasValue) {
			m_Rec->GetMouseOptions()->SetMouseClickDetectionDuration(options->MouseOptions->MouseClickDetectionDuration.Value);
		}
	}
	if (options->OutputOptions) {
		if (options->OutputOptions->SourceRect) {
			m_Rec->GetOutputOptions()->SetSourceRectangle(options->OutputOptions->SourceRect->ToRECT());
		}
		if (options->OutputOptions->IsVideoCaptureEnabled.HasValue) {
			m_Rec->GetOutputOptions()->SetVideoCaptureEnabled(options->OutputOptions->IsVideoCaptureEnabled.Value);
		}
	}
	if (options->SourceRects) {
		for each (KeyValuePair<String^, ScreenRect^> ^ kvp in options->SourceRects)
		{
			std::wstring id = msclr::interop::marshal_as<std::wstring>(kvp->Key);
			for each (RECORDING_SOURCE * nativeSource in m_Rec->GetRecordingSources())
			{
				if (nativeSource->ID == id) {
					if (kvp->Value) {
						nativeSource->SourceRect = kvp->Value->ToRECT();
					}
					else {
						nativeSource->SourceRect = std::nullopt;
					}
					break;
				}
			}
		}
	}
	if (options->SourceCursorCaptures) {
		for each (KeyValuePair<String^, bool> ^ kvp in options->SourceCursorCaptures)
		{
			std::wstring id = msclr::interop::marshal_as<std::wstring>(kvp->Key);
			for each (RECORDING_SOURCE * nativeSource in m_Rec->GetRecordingSources())
			{
				if (nativeSource->ID == id) {
					nativeSource->IsCursorCaptureEnabled = kvp->Value;
					break;
				}
			}
		}
	}
	if (options->OverlayCursorCaptures) {
		for each (KeyValuePair<String^, bool> ^ kvp in options->OverlayCursorCaptures)
		{
			std::wstring id = msclr::interop::marshal_as<std::wstring>(kvp->Key);
			for each (RECORDING_OVERLAY * nativeOverlay in m_Rec->GetRecordingOverlays())
			{
				if (nativeOverlay->ID == id) {
					nativeOverlay->IsCursorCaptureEnabled = kvp->Value;
					break;
				}
			}
		}
	}
	if (options->OverlayAnchors) {
		for each (KeyValuePair<String^, Anchor> ^ kvp in options->OverlayAnchors)
		{
			std::wstring id = msclr::interop::marshal_as<std::wstring>(kvp->Key);
			for each (RECORDING_OVERLAY * nativeOverlay in m_Rec->GetRecordingOverlays())
			{
				if (nativeOverlay->ID == id) {
					nativeOverlay->Anchor = static_cast<ContentAnchor>(kvp->Value);
					break;
				}
			}
		}
	}
	if (options->OverlayOffsets) {
		for each (KeyValuePair<String^, ScreenSize^> ^ kvp in options->OverlayOffsets)
		{
			std::wstring id = msclr::interop::marshal_as<std::wstring>(kvp->Key);
			for each (RECORDING_OVERLAY * nativeOverlay in m_Rec->GetRecordingOverlays())
			{
				if (nativeOverlay->ID == id) {
					nativeOverlay->Offset = kvp->Value->ToSIZE();
					break;
				}
			}
		}
	}
	if (options->OverlaySizes) {
		for each (KeyValuePair<String^, ScreenSize^> ^ kvp in options->OverlaySizes)
		{
			std::wstring id = msclr::interop::marshal_as<std::wstring>(kvp->Key);
			for each (RECORDING_OVERLAY * nativeOverlay in m_Rec->GetRecordingOverlays())
			{
				if (nativeOverlay->ID == id) {
					nativeOverlay->OutputSize = kvp->Value->ToSIZE();
					break;
				}
			}
		}
	}
	if (options->SourceVideoCaptures) {
		for each (KeyValuePair<String^, bool> ^ kvp in options->SourceVideoCaptures)
		{
			std::wstring id = msclr::interop::marshal_as<std::wstring>(kvp->Key);
			for each (RECORDING_SOURCE * nativeSource in m_Rec->GetRecordingSources())
			{
				if (nativeSource->ID == id) {
					nativeSource->IsVideoCaptureEnabled = kvp->Value;
				}
			}
		}
	}
	if (options->OverlayVideoCaptures) {
		for each (KeyValuePair<String^, bool> ^ kvp in options->OverlayVideoCaptures)
		{
			std::wstring id = msclr::interop::marshal_as<std::wstring>(kvp->Key);
			for each (RECORDING_OVERLAY * nativeOverlay in m_Rec->GetRecordingOverlays())
			{
				if (nativeOverlay->ID == id) {
					nativeOverlay->IsVideoCaptureEnabled = kvp->Value;
				}
			}
		}
	}

	if (options->RecordingOverlays) {
		for each (RecordingOverlayBase ^ managedOverlay in options->RecordingOverlays)
		{
			std::wstring id = msclr::interop::marshal_as<std::wstring>(managedOverlay->ID);

			RECORDING_OVERLAY* nativeOverlay;
			HRESULT hr = m_Rec->GetRecordingOverlayForId(id, &nativeOverlay);

			if (SUCCEEDED(hr)) {
				hr = CreateOrUpdateNativeRecordingOverlay(managedOverlay, nativeOverlay);
			}
			else {
				nativeOverlay = new RECORDING_OVERLAY{};
				hr = CreateOrUpdateNativeRecordingOverlay(managedOverlay, nativeOverlay);
				m_Rec->GetRecordingOverlays().push_back(nativeOverlay);
				hr = m_Rec->UpdateOverlays();
			}

			if (FAILED(hr)) {
				LOG_ERROR("Failed to update recording overlay properties with ID %ls", id.c_str());
			}
		}
	}

	if (options->RecordingSources) {
		for each (RecordingSourceBase ^ managedSource in options->RecordingSources)
		{
			std::wstring id = msclr::interop::marshal_as<std::wstring>(managedSource->ID);
			for each (RECORDING_SOURCE * nativeSource in m_Rec->GetRecordingSources())
			{
				if (nativeSource->ID == id) {
					HRESULT hr = CreateOrUpdateNativeRecordingSource(managedSource, nativeSource);
					if (FAILED(hr)) {
						LOG_ERROR("Failed to update recording source properties with ID %ls", id.c_str());
					}
				}
			}
		}
	}
}

bool Recorder::SetExcludeFromCapture(System::IntPtr hwnd, bool isExcluded)
{
	return RecordingManager::SetExcludeFromCapture((HWND)hwnd.ToPointer(), isExcluded);
}

List<AudioDevice^>^ Recorder::GetSystemAudioDevices(AudioDeviceSource source)
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

	auto devices = gcnew List<AudioDevice^>();

	HRESULT hr = ListAudioDevices(dFlow, &map);

	if (SUCCEEDED(hr))
	{
		if (map.size() != 0)
		{
			for (auto const& element : map) {
				devices->Add(gcnew AudioDevice(gcnew String(element.first.c_str()), gcnew String(element.second.c_str())));
			}
		}
	}
	return devices;
}

List<RecordableCamera^>^ Recorder::GetSystemVideoCaptureDevices()
{
	std::vector<IMFActivate*> captureDevices;
	HRESULT hr = EnumVideoCaptureDevices(&captureDevices);
	List<RecordableCamera^>^ devices = gcnew List<RecordableCamera^>();
	if (SUCCEEDED(hr))
	{
		for (auto const& pDevice : captureDevices) {
			ReleaseOnExit releaseDevice(pDevice);
			// Get the device path
			WCHAR* symbolicLink = NULL;
			UINT32 cchSymbolicLink;
			hr = pDevice->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, &symbolicLink, &cchSymbolicLink);
			CoTaskMemFreeOnExit freeSymbolicLink(symbolicLink);
			if (FAILED(hr) || symbolicLink == NULL) {
				continue;
			}
			WCHAR* nameString = NULL;
			// Get the human-friendly name of the device
			UINT32 cchName;
			hr = pDevice->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &nameString, &cchName);
			CoTaskMemFreeOnExit freeName(nameString);
			if (SUCCEEDED(hr) && nameString != NULL)
			{
				String^ path = gcnew String(symbolicLink);
				String^ title = gcnew String(nameString);
				devices->Add(gcnew RecordableCamera(title, path));
			}

		}
	}
	return devices;
}

List<RecordableDisplay^>^ Recorder::GetDisplays()
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
				display->FriendlyName = gcnew String(GetMonitorName(desc.Monitor).c_str());
				displays->Add(display);
			}
		}
		output->Release();
	}
	return displays;
}

List<RecordableWindow^>^ Recorder::GetWindows()
{
	List<RecordableWindow^>^ windows = gcnew List<RecordableWindow^>();
	for each (const Window & win in EnumerateWindows())
	{
		RecordableWindow^ recordableWin = gcnew RecordableWindow(gcnew String(win.Title().c_str()), IntPtr(win.Hwnd()));
		windows->Add(recordableWin);
	}
	return windows;
}

OutputDimensions^ Recorder::GetOutputDimensionsForRecordingSources(IEnumerable<RecordingSourceBase^>^ recordingSources)
{
	std::vector<RECORDING_SOURCE> sources = CreateRecordingSourceList(recordingSources);

	std::vector<RECT> outputRects{};
	std::vector<std::pair<RECORDING_SOURCE*, RECT>> validOutputs{};
	std::vector<RECORDING_SOURCE*> sourcePtrs;
	for each (RECORDING_SOURCE source in sources)
	{
		sourcePtrs.push_back(new RECORDING_SOURCE(source));
	}
	HRESULT hr = GetOutputRectsForRecordingSources(sourcePtrs, &validOutputs);



	OutputDimensions^ outputDimensions = gcnew OutputDimensions();
	outputDimensions->OutputCoordinates = gcnew List<SourceCoordinates^>();
	for each (auto const& pair in validOutputs) {
		RECORDING_SOURCE* nativeSource = pair.first;
		RECT nativeSourceRect = pair.second;
		outputRects.push_back(nativeSourceRect);
		switch (nativeSource->Type)
		{
			case RecordingSourceType::Window: {
				for each (RecordingSourceBase ^ recordingSource in recordingSources)
				{
					if (isinst<WindowRecordingSource^>(recordingSource)) {
						if ((gcnew String(nativeSource->ID.c_str()))->Equals(recordingSource->ID)) {
							outputDimensions->OutputCoordinates->Add(gcnew SourceCoordinates(recordingSource, gcnew ScreenRect(nativeSourceRect.left, nativeSourceRect.top, RectWidth(nativeSourceRect), RectHeight(nativeSourceRect))));
							break;
						}
					}
				}
				break;
			}
			case RecordingSourceType::Display: {
				for each (RecordingSourceBase ^ recordingSource in recordingSources)
				{
					if (isinst<DisplayRecordingSource^>(recordingSource)) {
						if ((gcnew String(nativeSource->ID.c_str()))->Equals(recordingSource->ID)) {
							outputDimensions->OutputCoordinates->Add(gcnew SourceCoordinates(recordingSource, gcnew ScreenRect(nativeSourceRect.left, nativeSourceRect.top, RectWidth(nativeSourceRect), RectHeight(nativeSourceRect))));
							break;
						}
					}
				}
				break;
			}
			case RecordingSourceType::Video: {
				for each (RecordingSourceBase ^ recordingSource in recordingSources)
				{
					if (isinst<VideoRecordingSource^>(recordingSource)) {
						if ((gcnew String(nativeSource->ID.c_str()))->Equals(recordingSource->ID)) {
							outputDimensions->OutputCoordinates->Add(gcnew SourceCoordinates(recordingSource, gcnew ScreenRect(nativeSourceRect.left, nativeSourceRect.top, RectWidth(nativeSourceRect), RectHeight(nativeSourceRect))));
							break;
						}
					}
				}
				break;
			}
			case RecordingSourceType::CameraCapture: {
				for each (RecordingSourceBase ^ recordingSource in recordingSources)
				{
					if (isinst<VideoCaptureRecordingSource^>(recordingSource)) {
						if ((gcnew String(nativeSource->ID.c_str()))->Equals(recordingSource->ID)) {
							outputDimensions->OutputCoordinates->Add(gcnew SourceCoordinates(recordingSource, gcnew ScreenRect(nativeSourceRect.left, nativeSourceRect.top, RectWidth(nativeSourceRect), RectHeight(nativeSourceRect))));
							break;
						}
					}
				}
				break;
			}
			case RecordingSourceType::Picture: {
				for each (RecordingSourceBase ^ recordingSource in recordingSources)
				{
					if (isinst<ImageRecordingSource^>(recordingSource)) {
						if ((gcnew String(nativeSource->ID.c_str()))->Equals(recordingSource->ID)) {
							outputDimensions->OutputCoordinates->Add(gcnew SourceCoordinates(recordingSource, gcnew ScreenRect(nativeSourceRect.left, nativeSourceRect.top, RectWidth(nativeSourceRect), RectHeight(nativeSourceRect))));
							break;
						}
					}
				}
				break;
			}
			default:
				break;
		}
	}
	RECT deskBounds;
	GetCombinedRects(outputRects, &deskBounds, nullptr);

	outputDimensions->CombinedOutputSize = gcnew ScreenSize(RectWidth(deskBounds), RectHeight(deskBounds));
	for each (RECORDING_SOURCE * ptr in sourcePtrs)
	{
		delete ptr;
	}
	return outputDimensions;
}

List<VideoCaptureFormat^>^ ScreenRecorderLib::Recorder::GetSupportedVideoCaptureFormatsForDevice(String^ DevicePath)
{
	MeasureExecutionTime measure(L"GetSupportedVideoCaptureFormatsForDevice");
	std::vector<IMFActivate*> captureDevices;
	HRESULT hr = EnumVideoCaptureDevices(&captureDevices);
	std::wstring devicePathWstring = msclr::interop::marshal_as<std::wstring>(DevicePath);


	if (SUCCEEDED(hr))
	{
		ReleaseVectorOnExit release(captureDevices);
		for (auto const& pDevice : captureDevices) {
			// Get the device path
			WCHAR* symbolicLink = NULL;
			UINT32 cchSymbolicLink;
			hr = pDevice->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, &symbolicLink, &cchSymbolicLink);
			CoTaskMemFreeOnExit freeSymbolicLink(symbolicLink);
			if (FAILED(hr) || symbolicLink == NULL || (!devicePathWstring.empty() && devicePathWstring != symbolicLink)) {
				continue;
			}

			CComPtr<IMFMediaSource> pSource = nullptr;
			CONTINUE_ON_BAD_HR(hr = pDevice->ActivateObject(__uuidof(IMFMediaSource), (void**)&pSource));

			std::vector<IMFMediaType*> captureFormats;
			EnumerateCaptureFormats(pSource, &captureFormats);
			auto list = CreateVideoCaptureFormatList(captureFormats);
			pDevice->ShutdownObject();
			return list;
		}
	}
	return gcnew List<VideoCaptureFormat^>();
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
	ReleaseResources();
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
bool Recorder::TakeSnapshot()
{
	HRESULT hr = m_Rec->TakeSnapshot(L"");
	return SUCCEEDED(hr);
}
bool Recorder::TakeSnapshot(System::String^ path)
{
	std::wstring stdPathString = msclr::interop::marshal_as<std::wstring>(path);
	HRESULT hr = m_Rec->TakeSnapshot(stdPathString);
	return SUCCEEDED(hr);
}
bool Recorder::TakeSnapshot(System::IO::Stream^ stream) {
	ManagedIStream* interopStream = new ManagedIStream(stream);
	HRESULT hr = m_Rec->TakeSnapshot(interopStream);
	interopStream->Release();
	OutputDebugStringW(L"Snapshot returning");
	return SUCCEEDED(hr);
}
void Recorder::SetupCallbacks() {
	CreateErrorCallback();
	CreateCompletionCallback();
	CreateStatusCallback();
	CreateSnapshotCallback();
	CreateFrameNumberCallback();
}

void Recorder::ReleaseCallbacks() {
	if (_statusChangedDelegateGcHandler.IsAllocated)
		_statusChangedDelegateGcHandler.Free();
	if (_errorDelegateGcHandler.IsAllocated)
		_errorDelegateGcHandler.Free();
	if (_completedDelegateGcHandler.IsAllocated)
		_completedDelegateGcHandler.Free();
	if (_snapshotDelegateGcHandler.IsAllocated)
		_snapshotDelegateGcHandler.Free();
	if (_frameNumberDelegateGcHandler.IsAllocated)
		_frameNumberDelegateGcHandler.Free();
}

void Recorder::ReleaseResources() {
	ReleaseCallbacks();
	if (m_ManagedStream) {
		delete m_ManagedStream;
		m_ManagedStream = nullptr;
	}
	if (m_Rec) {
		for each (auto var in m_Rec->GetRecordingOverlays())
		{
			SafeRelease(&var->SourceStream);
		}
		for each (auto var in m_Rec->GetRecordingSources())
		{
			SafeRelease(&var->SourceStream);
		}
	}
}

HRESULT Recorder::CreateOrUpdateNativeRecordingSource(_In_ RecordingSourceBase^ managedSource, _Inout_ RECORDING_SOURCE* pNativeSource)
{
	HRESULT hr = E_FAIL;
	pNativeSource->ID = msclr::interop::marshal_as<std::wstring>(managedSource->ID);
	if (managedSource->OutputSize
		&& !managedSource->OutputSize->Equals(ScreenSize::Empty)
		&& (managedSource->OutputSize->Width > 0 || managedSource->OutputSize->Height > 0)) {
		pNativeSource->OutputSize = managedSource->OutputSize->ToSIZE();
	}

	if (managedSource->Position && !managedSource->Position->Equals(ScreenPoint::Empty)) {
		pNativeSource->Position = managedSource->Position->ToPOINT();
	}
	if (managedSource->SourceRect
		&& !managedSource->SourceRect->Equals(ScreenRect::Empty)
		&& managedSource->SourceRect->Right > managedSource->SourceRect->Left
		&& managedSource->SourceRect->Bottom > managedSource->SourceRect->Top) {
		pNativeSource->SourceRect = managedSource->SourceRect->ToRECT();
	}

	pNativeSource->IsVideoFramePreviewEnabled = managedSource->IsVideoFramePreviewEnabled;
	if (pNativeSource->RecordingNewFrameDataCallback == nullptr) {
		pNativeSource->RecordingNewFrameDataCallback = managedSource->RegisterNewFrameDataCallback();
	}

	pNativeSource->IsVideoCaptureEnabled = managedSource->IsVideoCaptureEnabled;
	pNativeSource->Stretch = static_cast<TextureStretchMode>(managedSource->Stretch);
	pNativeSource->Anchor = static_cast<ContentAnchor>(managedSource->AnchorPoint);
	if (isinst<DisplayRecordingSource^>(managedSource)) {
		DisplayRecordingSource^ displaySource = (DisplayRecordingSource^)managedSource;
		if (!String::IsNullOrEmpty(displaySource->DeviceName)) {
			std::wstring deviceName = msclr::interop::marshal_as<std::wstring>(displaySource->DeviceName);
			CComPtr<IDXGIOutput> output;
			hr = GetOutputForDeviceName(deviceName, &output);
			if (SUCCEEDED(hr)) {
				DXGI_OUTPUT_DESC desc;
				hr = output->GetDesc(&desc);
				pNativeSource->Type = RecordingSourceType::Display;
				pNativeSource->SourcePath = desc.DeviceName;
				pNativeSource->IsCursorCaptureEnabled = displaySource->IsCursorCaptureEnabled;
				pNativeSource->IsBorderRequired = displaySource->IsBorderRequired;

				switch (displaySource->RecorderApi)
				{
					case RecorderApi::DesktopDuplication: {
						pNativeSource->SourceApi = RecordingSourceApi::DesktopDuplication;
						break;
					}
					case RecorderApi::WindowsGraphicsCapture: {
						pNativeSource->SourceApi = RecordingSourceApi::WindowsGraphicsCapture;
						break;
					}
					default:
						break;
				}
				hr = S_OK;
			}
		}
	}
	else if (isinst<WindowRecordingSource^>(managedSource)) {
		WindowRecordingSource^ windowSource = (WindowRecordingSource^)managedSource;
		if (windowSource->Handle != IntPtr::Zero) {
			HWND windowHandle = (HWND)(windowSource->Handle.ToPointer());
			if (IsWindow(windowHandle)) {
				pNativeSource->Type = RecordingSourceType::Window;
				pNativeSource->SourceWindow = windowHandle;
				pNativeSource->IsCursorCaptureEnabled = windowSource->IsCursorCaptureEnabled;
				pNativeSource->IsBorderRequired = windowSource->IsBorderRequired;
				pNativeSource->SourceApi = RecordingSourceApi::WindowsGraphicsCapture;
				hr = S_OK;
			}
		}
	}
	else if (isinst<VideoCaptureRecordingSource^>(managedSource)) {
		VideoCaptureRecordingSource^ videoCaptureSource = (VideoCaptureRecordingSource^)managedSource;
		if (!String::IsNullOrEmpty(videoCaptureSource->DeviceName)) {
			std::wstring deviceName = msclr::interop::marshal_as<std::wstring>(videoCaptureSource->DeviceName);
			pNativeSource->Type = RecordingSourceType::CameraCapture;
			pNativeSource->SourcePath = deviceName;
			if (videoCaptureSource->CaptureFormat) {
				pNativeSource->CaptureFormatIndex = videoCaptureSource->CaptureFormat->Index;
			}
			hr = S_OK;
		}
	}
	else if (isinst<VideoRecordingSource^>(managedSource)) {
		VideoRecordingSource^ videoSource = (VideoRecordingSource^)managedSource;
		pNativeSource->Type = RecordingSourceType::Video;
		if (videoSource->SourceStream) {
			SafeRelease(&pNativeSource->SourceStream);
			pNativeSource->SourceStream = new ManagedIStream(videoSource->SourceStream);
			hr = S_OK;
		}
		else if (!String::IsNullOrEmpty(videoSource->SourcePath)) {
			pNativeSource->SourcePath = msclr::interop::marshal_as<std::wstring>(videoSource->SourcePath);
			hr = S_OK;
		}
	}
	else if (isinst<ImageRecordingSource^>(managedSource)) {
		ImageRecordingSource^ imageSource = (ImageRecordingSource^)managedSource;
		pNativeSource->Type = RecordingSourceType::Picture;
		if (imageSource->SourceStream) {
			SafeRelease(&pNativeSource->SourceStream);
			pNativeSource->SourceStream = new ManagedIStream(imageSource->SourceStream);
			hr = S_OK;
		}
		else if (!String::IsNullOrEmpty(imageSource->SourcePath)) {
			pNativeSource->SourcePath = msclr::interop::marshal_as<std::wstring>(imageSource->SourcePath);
			hr = S_OK;
		}
	}
	else {
		return E_NOTIMPL;
	}
	if (managedSource->IsVideoFramePreviewEnabled) {
		pNativeSource->RecordingNewFrameDataCallback = managedSource->RegisterNewFrameDataCallback();
	}
	return hr;
}

HRESULT Recorder::CreateOrUpdateNativeRecordingOverlay(_In_ RecordingOverlayBase^ managedOverlay, _Inout_ RECORDING_OVERLAY* pNativeOverlay)
{
	HRESULT hr = E_FAIL;
	pNativeOverlay->ID = msclr::interop::marshal_as<std::wstring>(managedOverlay->ID);
	if (managedOverlay->Offset) {
		pNativeOverlay->Offset = SIZE{ static_cast<long>(managedOverlay->Offset->Width), static_cast<long>(managedOverlay->Offset->Height) };
	}
	if (managedOverlay->Size) {
		pNativeOverlay->OutputSize = SIZE{ static_cast<long>(managedOverlay->Size->Width), static_cast<long>(managedOverlay->Size->Height) };
	}
	pNativeOverlay->Anchor = static_cast<ContentAnchor>(managedOverlay->AnchorPoint);
	pNativeOverlay->Stretch = static_cast<TextureStretchMode>(managedOverlay->Stretch);
	if (isinst<VideoCaptureOverlay^>(managedOverlay)) {
		VideoCaptureOverlay^ videoCaptureOverlay = (VideoCaptureOverlay^)managedOverlay;
		pNativeOverlay->Type = RecordingSourceType::CameraCapture;
		if (!String::IsNullOrEmpty(videoCaptureOverlay->DeviceName)) {
			pNativeOverlay->SourcePath = msclr::interop::marshal_as<std::wstring>(videoCaptureOverlay->DeviceName);
			hr = S_OK;
		}
	}
	else if (isinst<ImageOverlay^>(managedOverlay)) {
		ImageOverlay^ pictureOverlay = (ImageOverlay^)managedOverlay;
		pNativeOverlay->Type = RecordingSourceType::Picture;
		if (pictureOverlay->SourceStream) {
			SafeRelease(&pNativeOverlay->SourceStream);
			pNativeOverlay->SourceStream = new ManagedIStream(pictureOverlay->SourceStream);
			hr = S_OK;
		}
		else if (!String::IsNullOrEmpty(pictureOverlay->SourcePath)) {
			pNativeOverlay->SourcePath = msclr::interop::marshal_as<std::wstring>(pictureOverlay->SourcePath);
			hr = S_OK;
		}
	}
	else if (isinst<VideoOverlay^>(managedOverlay)) {
		VideoOverlay^ videoOverlay = (VideoOverlay^)managedOverlay;
		pNativeOverlay->Type = RecordingSourceType::Video;
		if (videoOverlay->SourceStream) {
			SafeRelease(&pNativeOverlay->SourceStream);
			pNativeOverlay->SourceStream = new ManagedIStream(videoOverlay->SourceStream);
			hr = S_OK;
		}
		else if (!String::IsNullOrEmpty(videoOverlay->SourcePath)) {
			pNativeOverlay->SourcePath = msclr::interop::marshal_as<std::wstring>(videoOverlay->SourcePath);
			hr = S_OK;
		}
	}
	else if (isinst<DisplayOverlay^>(managedOverlay)) {
		DisplayOverlay^ displayOverlay = (DisplayOverlay^)managedOverlay;
		pNativeOverlay->Type = RecordingSourceType::Display;
		if (!String::IsNullOrEmpty(displayOverlay->DeviceName)) {
			pNativeOverlay->SourcePath = msclr::interop::marshal_as<std::wstring>(displayOverlay->DeviceName);
			pNativeOverlay->IsCursorCaptureEnabled = displayOverlay->IsCursorCaptureEnabled;
			hr = S_OK;
		}
	}
	else if (isinst<WindowOverlay^>(managedOverlay)) {
		WindowOverlay^ windowOverlay = (WindowOverlay^)managedOverlay;
		pNativeOverlay->Type = RecordingSourceType::Window;
		if (windowOverlay->Handle != IntPtr::Zero) {
			HWND windowHandle = (HWND)(windowOverlay->Handle.ToPointer());
			if (!IsIconic(windowHandle) && IsWindow(windowHandle)) {
				pNativeOverlay->SourceWindow = windowHandle;
				pNativeOverlay->IsCursorCaptureEnabled = windowOverlay->IsCursorCaptureEnabled;
				hr = S_OK;
			}
		}
	}
	else {
		return E_NOTIMPL;
	}
	return hr;
}

List<VideoCaptureFormat^>^ ScreenRecorderLib::Recorder::CreateVideoCaptureFormatList(_In_ std::vector<IMFMediaType*> mediaTypes)
{
	MeasureExecutionTime measure(L"CreateVideoCaptureFormatList");
	List<VideoCaptureFormat^>^ managedFormats = gcnew List<VideoCaptureFormat^>();

	bool supportsVideoInfo2 = false;
	//Check if device supports FORMAT_VideoInfo2, see https://learn.microsoft.com/en-us/windows/win32/medfound/how-to-set-the-video-capture-format
	for each (IMFMediaType * pMediaType in mediaTypes)
	{
		GUID majorType;
		pMediaType->GetMajorType(&majorType);
		if (majorType != MFMediaType_Video) {
			continue;
		}
		if (IsVideoInfo2(pMediaType)) {
			supportsVideoInfo2 = true;
			break;
		}
	}

	for (int i = 0; i < mediaTypes.size(); i++)
	{
		IMFMediaType* pMediaType = mediaTypes.at(i);
		GUID majorType;
		pMediaType->GetMajorType(&majorType);

		if (majorType != MFMediaType_Video
			|| (supportsVideoInfo2 && !IsVideoInfo2(pMediaType))) {
			continue;
		}

		GUID videoFormat;
		pMediaType->GetGUID(MF_MT_SUBTYPE, &videoFormat);
		WCHAR* pVideoFormatString;
		GetGUIDName(videoFormat, &pVideoFormatString);
		double framerate = 0.0;
		GetFrameRate(pMediaType, &framerate);
		SIZE frameSize;
		GetFrameSize(pMediaType, &frameSize);
		UINT32 avgBitrate = 0;
		pMediaType->GetUINT32(MF_MT_AVG_BITRATE, &avgBitrate);
		UINT32 YUVMatrix = 0;
		pMediaType->GetUINT32(MF_MT_YUV_MATRIX, &YUVMatrix);
		UINT32 videoNominalRange = 0;
		pMediaType->GetUINT32(MF_MT_VIDEO_NOMINAL_RANGE, &videoNominalRange);
		UINT32 videoInterlaceMode = 0;
		pMediaType->GetUINT32(MF_MT_INTERLACE_MODE, &videoInterlaceMode);

		VideoCaptureFormat^ managedFormat = gcnew VideoCaptureFormat();
		managedFormat->Index = i;
		managedFormat->VideoFormat = FromNativeGuid(videoFormat);
		managedFormat->VideoFormatName = gcnew String(pVideoFormatString);
		managedFormat->Framerate = framerate;
		managedFormat->FrameSize = gcnew ScreenSize(frameSize.cx, frameSize.cy);
		managedFormat->AverageBitrate = avgBitrate;
		managedFormat->InterlaceMode = (VideoInterlaceMode)videoInterlaceMode;
		managedFormat->YUVMatrix = (VideoTransferMatrix)YUVMatrix;
		managedFormat->NominalRange = (VideoNominalRange)videoNominalRange;
		managedFormats->Add(managedFormat);

		CoTaskMemFree(pVideoFormatString);
	}
	return managedFormats;
}


std::vector<RECORDING_SOURCE> Recorder::CreateRecordingSourceList(_In_ IEnumerable<RecordingSourceBase^>^ managedSources) {
	std::vector<RECORDING_SOURCE> sources{};
	if (managedSources) {
		for each (RecordingSourceBase ^ source in managedSources)
		{
			RECORDING_SOURCE nativeSource{};
			HRESULT hr = CreateOrUpdateNativeRecordingSource(source, &nativeSource);
			if (SUCCEEDED(hr)) {
				if (std::find(sources.begin(), sources.end(), nativeSource) == sources.end()) {
					sources.insert(sources.end(), nativeSource);
				}
			}
		}

		return sources;
	}
	return std::vector<RECORDING_SOURCE>();
}

std::vector<RECORDING_OVERLAY> Recorder::CreateOverlayList(_In_ IEnumerable<RecordingOverlayBase^>^ managedOverlays) {
	std::vector<RECORDING_OVERLAY> overlays{};
	if (managedOverlays) {
		for each (RecordingOverlayBase ^ overlay in managedOverlays)
		{
			RECORDING_OVERLAY nativeOverlay{};
			HRESULT hr = CreateOrUpdateNativeRecordingOverlay(overlay, &nativeOverlay);
			if (SUCCEEDED(hr)) {
				if (std::find(overlays.begin(), overlays.end(), nativeOverlay) == overlays.end()) {
					overlays.insert(overlays.end(), nativeOverlay);
				}
			}
		}
	}
	return overlays;
}

Guid ScreenRecorderLib::Recorder::FromNativeGuid(_In_ const GUID& guid)
{
	return *reinterpret_cast<Guid*>(const_cast<GUID*>(&guid));
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
void Recorder::CreateFrameNumberCallback() {
	InternalFrameNumberCallbackDelegate^ fp = gcnew InternalFrameNumberCallbackDelegate(this, &Recorder::FrameNumberChanged);
	_frameNumberDelegateGcHandler = GCHandle::Alloc(fp);
	IntPtr ip = Marshal::GetFunctionPointerForDelegate(fp);
	CallbackFrameNumberChangedFunction cb = static_cast<CallbackFrameNumberChangedFunction>(ip.ToPointer());
	m_Rec->RecordingFrameNumberChangedCallback = cb;
}
void Recorder::EventComplete(std::wstring path, fifo_map<std::wstring, int> delays)
{
	ReleaseResources();

	List<FrameData^>^ frameInfos = gcnew List<FrameData^>();

	for (auto x : delays) {
		frameInfos->Add(gcnew FrameData(gcnew String(x.first.c_str()), x.second));
	}
	RecordingCompleteEventArgs^ args = gcnew RecordingCompleteEventArgs(gcnew String(path.c_str()), frameInfos);
	OnRecordingComplete(this, args);
}
void Recorder::EventFailed(std::wstring error, std::wstring path)
{
	ReleaseResources();
	OnRecordingFailed(this, gcnew RecordingFailedEventArgs(gcnew String(error.c_str()), gcnew String(path.c_str())));
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

void Recorder::FrameNumberChanged(int newFrameNumber, INT64 timestamp)
{
	OnFrameRecorded(this, gcnew FrameRecordedEventArgs(newFrameNumber, timestamp));
	CurrentFrameNumber = newFrameNumber;
}
