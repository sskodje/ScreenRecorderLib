// ScreenRecorder.h

#pragma once
#include <atlbase.h>
#include <vcclr.h>
#include <vector>
#include "fifo_map.h"
#include "internal_recorder.h"
using namespace System;
using namespace System::Runtime::InteropServices;
using namespace System::Collections::Generic;


delegate void InternalStatusCallbackDelegate(int status);
delegate void InternalCompletionCallbackDelegate(std::wstring path, nlohmann::fifo_map<std::wstring, int>);
delegate void InternalErrorCallbackDelegate(std::wstring path);
[StructLayoutAttribute(LayoutKind::Sequential, CharSet = CharSet::Unicode)]
public ref struct Managed_Completion_Delegate_Wrapper
{
	[MarshalAsAttribute(UnmanagedType::FunctionPtr)]
	InternalCompletionCallbackDelegate ^_Delegate;
};

[StructLayoutAttribute(LayoutKind::Sequential, CharSet = CharSet::Unicode)]
public ref struct Managed_Error_Delegate_Wrapper
{
	[MarshalAsAttribute(UnmanagedType::FunctionPtr)]
	InternalErrorCallbackDelegate ^_Delegate;
};

[StructLayoutAttribute(LayoutKind::Sequential, CharSet = CharSet::Unicode)]
public ref struct Managed_Status_Delegate_Wrapper
{
	[MarshalAsAttribute(UnmanagedType::FunctionPtr)]
	InternalStatusCallbackDelegate ^_Delegate;
};
namespace ScreenRecorderLib {

	public enum class RecorderStatus {
		Idle,
		Recording,
		Paused,
		Finishing
	};
	public enum class AudioChannels {
		Mono = 1,
		Stereo = 2,
		FivePointOne = 6
	};
	public enum class AudioBitrate {
		bitrate_96kbps = 12000,
		bitrate_128kbps = 16000,
		bitrate_160kbps = 20000,
		bitrate_192kbps = 24000,

	};

	public enum class RecorderMode {
		Video,
		Slideshow,
		Snapshot
	};
	public ref class FrameData {
	public:
		property String^ Path;
		property int Delay;
		FrameData() {}
		FrameData(String^ path, int delay) {
			Path = path;
			Delay = delay;
		}
	};
	public ref class DisplayOptions {
	public:
		property int Monitor;
		property int Left;
		property int Top;
		property int Right;
		property int Bottom;
		DisplayOptions() {

		}
		DisplayOptions(int monitor) {
			Monitor = monitor;
		}
		DisplayOptions(int monitor, int left, int top, int right, int bottom) {
			Monitor = monitor;
			Left = left;
			Top = top;
			Right = right;
			Bottom = bottom;
		}
	};

	public ref class VideoOptions {
	public:
		VideoOptions() {
			Framerate = 30;
			Bitrate = 4000 * 1000;
			IsFixedFramerate = false;
			IsMousePointerEnabled = true;
		}
		property int Framerate;
		//Bitrate in bits per second
		property int Bitrate;
		property bool IsMousePointerEnabled;
		property bool IsFixedFramerate;
	};
	public ref class AudioOptions {
	public:
		AudioOptions() {
			IsAudioEnabled = false;
			Bitrate = AudioBitrate::bitrate_96kbps;
			Channels = AudioChannels::Stereo;
		}
		property bool IsAudioEnabled;
		property AudioBitrate Bitrate;
		property AudioChannels Channels;
	};
	public ref class RecorderOptions {
	public:
		property RecorderMode RecorderMode;
		property VideoOptions^ VideoOptions;
		property DisplayOptions^ DisplayOptions;
		property AudioOptions^ AudioOptions;
	};

	public ref class RecordingStatusEventArgs :System::EventArgs {
	public:
		property RecorderStatus Status;
		RecordingStatusEventArgs(RecorderStatus status) {
			Status = status;
		}
	};
	public ref class RecordingCompleteEventArgs :System::EventArgs {
	public:
		property String^ FilePath;
		property  List<FrameData^>^ FrameInfos;
		RecordingCompleteEventArgs(String^ path, List<FrameData^>^ frameInfos) {
			FilePath = path;
			FrameInfos = frameInfos;
		}
	};
	public ref class RecordingFailedEventArgs :System::EventArgs {
	public:
		property String^ Error;
		RecordingFailedEventArgs(String^ error) {
			Error = error;
		}
	};
	public ref class Recorder {
	private:
		Recorder(RecorderOptions^ options);
		~Recorder();
		RecorderStatus _status;
		Managed_Completion_Delegate_Wrapper^ _Complete_Delegate;
		Managed_Error_Delegate_Wrapper^ _Error_Delegate;
		Managed_Status_Delegate_Wrapper^ _Status_Delegate;
		void EventComplete(std::wstring str, nlohmann::fifo_map<std::wstring, int> delays);
		void EventFailed(std::wstring str);
		void EventStatusChanged(int status);

	public:
		property RecorderStatus Status {
			RecorderStatus get() {
				return _status;
			}
		private:
			void set(RecorderStatus value) {
				_status = value;
			}
		}
		void Record(System::String^ path);
		void Pause();
		void Resume();
		void Stop();
		static Recorder^ CreateRecorder();
		static Recorder^ CreateRecorder(RecorderOptions^ options);
		event EventHandler<RecordingCompleteEventArgs^>^ OnRecordingComplete;
		event EventHandler<RecordingFailedEventArgs^>^ OnRecordingFailed;
		event EventHandler<RecordingStatusEventArgs^>^ OnStatusChanged;
	};
}
