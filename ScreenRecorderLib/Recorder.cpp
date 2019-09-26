// This is the main DLL file.
#include "Recorder.h"
#include <memory>
#include <msclr\marshal.h>
#include <msclr\marshal_cppstd.h>
#include "cleanup.h"
#include "ManagedIStream.h"
#include "internal_recorder.h"
using namespace ScreenRecorderLib;
using namespace nlohmann;

Recorder::Recorder(RecorderOptions^ options)
{
	lRec = new internal_recorder();
	SetOptions(options);
	createErrorCallback();
	createCompletionCallback();
	createStatusCallback();
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
			lRec->SetDisplayOutput(msclr::interop::marshal_as<std::wstring>(options->DisplayOptions->MonitorDeviceName));
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
		}
		if (options->MouseOptions) {
			lRec->SetMousePointerEnabled(options->MouseOptions->IsMousePointerEnabled);
			lRec->SetDetectMouseClicks(options->MouseOptions->IsMouseClicksDetected);
			lRec->SetMouseClickDetectionLMBColor(msclr::interop::marshal_as<std::string>(options->MouseOptions->MouseClickDetectionColor));
			lRec->SetMouseClickDetectionRMBColor(msclr::interop::marshal_as<std::string>(options->MouseOptions->MouseRightClickDetectionColor));
			lRec->SetMouseClickDetectionRadius(options->MouseOptions->MouseClickDetectionRadius);
			lRec->SetMouseClickDetectionDuration(options->MouseOptions->MouseClickDetectionDuration);
		}
		switch (options->RecorderMode)
		{
		case RecorderMode::Video:
			lRec->SetRecorderMode(MODE_VIDEO);
			break;
		case RecorderMode::Slideshow:
			lRec->SetRecorderMode(MODE_SLIDESHOW);
			break;
		case RecorderMode::Snapshot:
			lRec->SetRecorderMode(MODE_SNAPSHOT);
			break;
		default:
			break;
		}
		lRec->SetIsThrottlingDisabled(options->IsThrottlingDisabled);
		lRec->SetIsLowLatencyModeEnabled(options->IsLowLatencyEnabled);
		lRec->SetIsFastStartEnabled(options->IsMp4FastStartEnabled);
		lRec->SetIsHardwareEncodingEnabled(options->IsHardwareEncodingEnabled);
		lRec->SetIsFragmentedMp4Enabled(options->IsFragmentedMp4Enabled);
	}
}

List<String^>^ Recorder::GetSystemAudioDevices(AudioDeviceSource source)
{
	std::vector<std::wstring> vector;
	EDataFlow dFlow;

	switch (source)
	{
	case  AudioDeviceSource::OutputDevices:
		dFlow = eRender;
		break;
	case AudioDeviceSource::InputDevices:
		dFlow = eCapture;
		break;
	case AudioDeviceSource::All:
		dFlow = eAll;
		break;
	default:
		break;
	}

	List<String^>^ devices = gcnew List<String^>();

	HRESULT hr = CPrefs::list_devices(dFlow, &vector);

	if (hr == S_OK)
	{
		if (vector.size() != 0)
		{
			for (int i = 0; i < vector.size(); ++i)
			{
				devices->Add(gcnew String(vector[i].c_str()));
			}
		}
	}
	return devices;
}

void Recorder::createErrorCallback() {
	InternalErrorCallbackDelegate^ fp = gcnew InternalErrorCallbackDelegate(this, &Recorder::EventFailed);
	_errorDelegateGcHandler = GCHandle::Alloc(fp);
	IntPtr ip = Marshal::GetFunctionPointerForDelegate(fp);
	CallbackErrorFunction cb = static_cast<CallbackErrorFunction>(ip.ToPointer());
	lRec->RecordingFailedCallback = cb;

}
void Recorder::createCompletionCallback() {
	InternalCompletionCallbackDelegate^ fp = gcnew InternalCompletionCallbackDelegate(this, &Recorder::EventComplete);
	_completedDelegateGcHandler = GCHandle::Alloc(fp);
	IntPtr ip = Marshal::GetFunctionPointerForDelegate(fp);
	CallbackCompleteFunction cb = static_cast<CallbackCompleteFunction>(ip.ToPointer());
	lRec->RecordingCompleteCallback = cb;
}
void Recorder::createStatusCallback() {
	InternalStatusCallbackDelegate^ fp = gcnew InternalStatusCallbackDelegate(this, &Recorder::EventStatusChanged);
	_statusChangedDelegateGcHandler = GCHandle::Alloc(fp);
	IntPtr ip = Marshal::GetFunctionPointerForDelegate(fp);
	CallbackStatusChangedFunction cb = static_cast<CallbackStatusChangedFunction>(ip.ToPointer());
	lRec->RecordingStatusChangedCallback = cb;
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
	_statusChangedDelegateGcHandler.Free();
	_errorDelegateGcHandler.Free();
	_completedDelegateGcHandler.Free();
}

void Recorder::EventComplete(std::wstring str, fifo_map<std::wstring, int> delays)
{
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
Recorder^ Recorder::CreateRecorder() {
	return gcnew Recorder(nullptr);
}

Recorder^ Recorder::CreateRecorder(RecorderOptions ^ options)
{
	Recorder^ rec = gcnew Recorder(options);
	return rec;
}
void Recorder::Record(System::Runtime::InteropServices::ComTypes::IStream^ stream) {
	IStream *pNativeStream = (IStream*)Marshal::GetComInterfaceForObject(stream, System::Runtime::InteropServices::ComTypes::IStream::typeid).ToPointer();
	lRec->BeginRecording(pNativeStream);
}
void Recorder::Record(System::IO::Stream^ stream) {
	m_ManagedStream = new ManagedIStream(stream);
	lRec->BeginRecording(m_ManagedStream);
}
void Recorder::Record(System::String^ path) {
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
