// This is the main DLL file.

#include "Recorder.h"
#include <memory>
#include <msclr\marshal.h>
#include <msclr\marshal_cppstd.h>

#include "internal_recorder.h"
using namespace ScreenRecorderLib;
using namespace nlohmann;

template <class T> void SafeRelease(T **ppT)
{
	if (*ppT)
	{
		(*ppT)->Release();
		*ppT = NULL;
	}
}

internal_recorder *lRec;
Recorder::Recorder(RecorderOptions^ options)
{
	lRec = new internal_recorder();
	if (options) {
		if (options->VideoOptions) {
			lRec->SetVideoBitrate(options->VideoOptions->Bitrate);
			lRec->SetVideoFps(options->VideoOptions->Framerate);
			lRec->SetFixedFramerate(options->VideoOptions->IsFixedFramerate);
			lRec->SetMousePointerEnabled(options->VideoOptions->IsMousePointerEnabled);
		}
		if (options->DisplayOptions) {
			RECT rect;
			rect.left = options->DisplayOptions->Left;
			rect.top = options->DisplayOptions->Top;
			rect.right = options->DisplayOptions->Right;
			rect.bottom = options->DisplayOptions->Bottom;
			lRec->SetDestRectangle(rect);
			lRec->SetDisplayOutput(options->DisplayOptions->Monitor);
		}

		if (options->AudioOptions) {
			lRec->SetAudioEnabled(options->AudioOptions->IsAudioEnabled);
			lRec->SetAudioBitrate((UINT32)options->AudioOptions->Bitrate);
			lRec->SetAudioChannels((UINT32)options->AudioOptions->Channels);
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
	}
	_Error_Delegate = gcnew Managed_Error_Delegate_Wrapper();
	_Error_Delegate->_Delegate = gcnew InternalErrorCallbackDelegate(this, &Recorder::EventFailed);
	CallbackErrorFunction errorCallback;
	Marshal::StructureToPtr(_Error_Delegate, (System::IntPtr)&errorCallback, false);
	lRec->RecordingFailedCallback = errorCallback;

	_Complete_Delegate = gcnew Managed_Completion_Delegate_Wrapper();
	_Complete_Delegate->_Delegate = gcnew InternalCompletionCallbackDelegate(this, &Recorder::EventComplete);
	CallbackCompleteFunction completeCallback;
	Marshal::StructureToPtr(_Complete_Delegate, (System::IntPtr)&completeCallback, false);
	lRec->RecordingCompleteCallback = completeCallback;

	_Status_Delegate = gcnew Managed_Status_Delegate_Wrapper();
	_Status_Delegate->_Delegate = gcnew InternalStatusCallbackDelegate(this, &Recorder::EventStatusChanged);
	CallbackStatusChangedFunction statusCallback;
	Marshal::StructureToPtr(_Status_Delegate, (System::IntPtr)&statusCallback, false);
	lRec->RecordingStatusChangedCallback = statusCallback;
}

Recorder::~Recorder()
{
	delete lRec;
}

void Recorder::EventComplete(std::wstring str, fifo_map<std::wstring, int> delays)
{
	List<FrameData^>^ frameInfos = gcnew List<FrameData^>();

	for (auto x : delays) {
		frameInfos->Add(gcnew FrameData(gcnew String(x.first.c_str()), x.second));
	}
	RecordingCompleteEventArgs^ args = gcnew RecordingCompleteEventArgs(gcnew String(str.c_str()), frameInfos);
	OnRecordingComplete(this, args);
}
void Recorder::EventFailed(std::wstring str)
{
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
