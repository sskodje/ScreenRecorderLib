// ScreenRecorder.h

#pragma once
#include <atlbase.h>
#include <vcclr.h>
#include <vector>
#include "fifo_map.h"
#include "internal_recorder.h"
#include "ManagedIStream.h"
using namespace System;
using namespace System::Runtime::InteropServices;
using namespace System::Collections::Generic;


delegate void InternalStatusCallbackDelegate(int status);
delegate void InternalCompletionCallbackDelegate(std::wstring path, nlohmann::fifo_map<std::wstring, int>);
delegate void InternalErrorCallbackDelegate(std::wstring path);

namespace ScreenRecorderLib {

	public enum class AudioDeviceSource
	{
		OutputDevices,
		InputDevices,
		All
	};
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
		///<summary>Record as mp4 video in h264 format. </summary>
		Video,
		///<summary>Record one PNG picture for each frame. </summary>
		Slideshow,
		///<summary>Make a snapshot of the screen. </summary>
		Snapshot
	};
	public enum class H264Profile
	{
		Baseline = 66,
		Main = 77,
		High = 100
	};
	public enum class BitrateControlMode {
		///<summary>Constant bitrate. Faster encoding than VBR, but produces larger files with consistent size. This setting might not work on software encoding. </summary>
		CBR = 0,
		///<summary>Default is unconstrained variable bitrate. Overall bitrate will average towards the Bitrate property, but can fluctuate greatly over and under it.</summary>
		UnconstrainedVBR = 2,
		///<summary>Quality-based VBR encoding. The encoder selects the bit rate to match a specified quality level. Set Quality level in VideoOptions from 1-100. Default is 70. </summary>
		Quality
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
		property String^ MonitorDeviceName;
		property int Left;
		property int Top;
		property int Right;
		property int Bottom;
		DisplayOptions() {

		}
		DisplayOptions(int left, int top, int right, int bottom) {
			Left = left;
			Top = top;
			Right = right;
			Bottom = bottom;
		}

		/// <summary>
		///Select monitor to record via device name, e.g.\\.\DISPLAY1
		/// </summary>
		DisplayOptions(String^ monitorDeviceName) {
			MonitorDeviceName = monitorDeviceName;
		}

		/// <summary>
		///Select monitor to record via device name, e.g.\\.\DISPLAY1, and the rectangle to record on that monitor.
		/// </summary>
		DisplayOptions(String^ monitorDeviceName, int left, int top, int right, int bottom) {
			MonitorDeviceName = monitorDeviceName;
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
			Quality = 70;
			Bitrate = 4000 * 1000;
			IsFixedFramerate = false;
			EncoderProfile = H264Profile::Baseline;
			BitrateMode = BitrateControlMode::Quality;
		}
		property H264Profile EncoderProfile;
		/// <summary>
		///The bitrate control mode of the video encoder. Default is Quality.
		/// </summary>
		property BitrateControlMode BitrateMode;
		/// <summary>
		///Framerate in frames per second.
		/// </summary>
		property int Framerate;
		/// <summary>
		///Bitrate in bits per second
		/// </summary>
		property int Bitrate;
		/// <summary>
		///Video quality. This is only used when BitrateMode is set to Quality.
		/// </summary>
		property int Quality;
		/// <summary>
		///Send data to the video encoder every frame, even if it means duplicating the previous frame(s). Can fix stutter issues in fringe cases, but uses more resources.
		/// </summary>
		property bool IsFixedFramerate;
	};
	public ref class AudioOptions {
	public:
		AudioOptions() {
			IsAudioEnabled = false;
			IsOutputDeviceEnabled = true;
			IsInputDeviceEnabled = false;
			Bitrate = AudioBitrate::bitrate_96kbps;
			Channels = AudioChannels::Stereo;
		}
		property bool IsAudioEnabled;
		/// <summary>
		///Enable to record system audio output.
		/// </summary>
		property bool IsOutputDeviceEnabled;
		/// <summary>
		///Enable to record system audio input (e.g. microphone)
		/// </summary>
		property bool IsInputDeviceEnabled;
		property AudioBitrate Bitrate;
		property AudioChannels Channels;
		/// <summary>
		///Audio playback device to capture audio from. Pass null or empty string to select system default.
		/// </summary>
		property String^ AudioOutputDevice;
		/// <summary>
		///Audio input device to capture audio from. Pass null or empty string to select system default.
		/// </summary>
		property String^ AudioInputDevice;
	};
	public ref class MouseOptions {
	public:
		MouseOptions() {
			IsMousePointerEnabled = true;
			IsMouseClicksDetected = false;
			MouseClickDetectionColor = "#FFFF00";
			MouseRightClickDetectionColor = "#FFFF00";
			MouseClickDetectionRadius = 20;
			MouseClickDetectionDuration = 150;
		}
		/// <summary>
		///Display the mouse cursor on the recording
		/// </summary>
		property bool IsMousePointerEnabled;
		/// <summary>
		/// Display a colored dot where the left mouse button is pressed.
		/// </summary>
		property bool IsMouseClicksDetected;
		/// <summary>
		/// The color of the dot where the left mouse button is pressed, in hex format. Default is Yellow (#FFFF00).
		/// </summary>
		property String^ MouseClickDetectionColor;
		/// <summary>
		/// The color of the dot where the right mouse button is pressed, in hex format. Default is Yellow (#FFFF00).
		/// </summary>
		property String^ MouseRightClickDetectionColor;
		/// <summary>
		/// The radius of the dot where the left mouse button is pressed. Default is 20.
		/// </summary>
		property int MouseClickDetectionRadius;
		/// <summary>
		/// The duration of the dot shown where the left mouse button is pressed, in milliseconds. Default is 150.
		/// </summary>
		property int MouseClickDetectionDuration;

	};
	public ref class RecorderOptions {
	public:
		RecorderOptions() {
			IsThrottlingDisabled = false;
			IsLowLatencyEnabled = false;
			IsHardwareEncodingEnabled = true;
			IsMp4FastStartEnabled = true;
		}
		property RecorderMode RecorderMode;
		/// <summary>
		///Disable throttling of video renderer. If this is disabled, all frames are sent to renderer as fast as they come. Can cause out of memory crashes.
		/// </summary>
		property bool IsThrottlingDisabled;
		/// <summary>
		///Faster rendering, but can affect quality. Use when speed is more important than quality.
		/// </summary>
		property bool IsLowLatencyEnabled;
		/// <summary>
		///Enable hardware encoding if available. This is enabled by default.
		/// </summary>
		property bool IsHardwareEncodingEnabled;
		/// <summary>
		/// Place the mp4 header at the start of the file instead of the end. This allows streaming to start before entire file is downloaded.
		/// </summary>
		property bool IsMp4FastStartEnabled;
		/// <summary>
		/// Fragments the video into a list of individually playable blocks. This allows playback of video segments that has no end, i.e. live streaming.
		/// </summary>
		property bool IsFragmentedMp4Enabled;


		property VideoOptions^ VideoOptions;
		property DisplayOptions^ DisplayOptions;
		property AudioOptions^ AudioOptions;
		property MouseOptions^ MouseOptions;
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
		!Recorder();
		RecorderStatus _status;
		void createErrorCallback();
		void createCompletionCallback();
		void createStatusCallback();
		void EventComplete(std::wstring str, nlohmann::fifo_map<std::wstring, int> delays);
		void EventFailed(std::wstring str);
		void EventStatusChanged(int status);
		GCHandle _statusChangedDelegateGcHandler;
		GCHandle _errorDelegateGcHandler;
		GCHandle _completedDelegateGcHandler;
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
		internal_recorder *lRec;
		void Record(System::String^ path);
		void Record(System::Runtime::InteropServices::ComTypes::IStream^ stream);
		void Record(System::IO::Stream^ stream);
		void Pause();
		void Resume();
		void Stop();
		void SetOptions(RecorderOptions^ options);
		static Recorder^ CreateRecorder();
		static Recorder^ CreateRecorder(RecorderOptions^ options);
		static List<String^>^ GetSystemAudioDevices(AudioDeviceSource source);
		event EventHandler<RecordingCompleteEventArgs^>^ OnRecordingComplete;
		event EventHandler<RecordingFailedEventArgs^>^ OnRecordingFailed;
		event EventHandler<RecordingStatusEventArgs^>^ OnStatusChanged;
		ManagedIStream *m_ManagedStream;
	};
}
