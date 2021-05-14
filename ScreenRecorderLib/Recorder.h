#pragma warning(disable:4561)
#pragma once
#include <atlbase.h>
#include <vcclr.h>
#include <vector>
#include <set>
#include "../ScreenRecorderLibNative/Native.h"
#include "ManagedIStream.h"
#include "Win32WindowEnumeration.h"
using namespace System;
using namespace System::Runtime::InteropServices;
using namespace System::Collections::Generic;


delegate void InternalStatusCallbackDelegate(int status);
delegate void InternalCompletionCallbackDelegate(std::wstring path, nlohmann::fifo_map<std::wstring, int>);
delegate void InternalErrorCallbackDelegate(std::wstring path);
delegate void InternalSnapshotCallbackDelegate(std::wstring path);

namespace ScreenRecorderLib {
	public ref class ScreenSize {
	public:
		property int Width;
		property int Height;
		ScreenSize(int width, int height) {
			Width = width;
			Height = height;
		}
	};

	public ref class ScreenRect {
	public:
		property int Left;
		property int Top;
		property int Right;
		property int Bottom;
		ScreenRect() {

		}
		ScreenRect(int left, int top, int width, int height) {
			Left = left;
			Top = top;
			Right = left + width;
			Bottom = top + height;
		}

		property int Width {
			int get() {
				return Right - Left;
			}
		}
		property int Height {
			int get() {
				return Bottom - Top;
			}
		}
	};

	public enum class LogLevel
	{
		Trace = 0,
		Debug = 1,
		Info = 2,
		Warn = 3,
		Error = 4
	};
	public enum class MouseDetectionMode {
		///<summary>
		///Use polling for detecting mouse clicks. Does not affect mouse performance, but may not work for all mouse clicks generated programmatically.
		///</summary>
		Polling = MOUSE_DETECTION_MODE_POLLING,
		///<summary>
		///Use a low level system hook for detecting mouse clicks. Works more reliably for programmatic events, but can negatively affect mouse performance while recording.
		///</summary>
		Hook = MOUSE_DETECTION_MODE_HOOK
	};
	public enum class ImageFormat {
		PNG,
		JPEG,
		TIFF,
		BMP
	};
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
		Video = MODE_VIDEO,
		///<summary>Record one PNG picture for each frame. </summary>
		Slideshow = MODE_SLIDESHOW,
		///<summary>Create a screenshot. This can not be used on a video recording in progress. Set a snapshot interval in VideoOptions to get snapshots from a runnning Recorder instance.</summary>
		Snapshot = MODE_SNAPSHOT
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
	public enum class RecorderApi {
		///<summary>Desktop Duplication is supported on all Windows 8 and 10 versions. This API supports recording of screens.</summary>
		DesktopDuplication = 0,
		///<summary>WindowsGraphicsCapture requires Windows 10 version 1803 or newer. This API supports recording windows in addition to screens.</summary>
		WindowsGraphicsCapture = 1,
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

	public interface class RecordingSource {
		property RecordingSourceType SourceType
		{
			RecordingSourceType get();
		}
	};

	public ref class WindowRecordingSource : public RecordingSource {
	public:
		property IntPtr Handle;

		WindowRecordingSource() {	}
		WindowRecordingSource(IntPtr windowHandle) {
			Handle = windowHandle;
		}

		virtual property RecordingSourceType SourceType {
			RecordingSourceType get() {
				return RecordingSourceType::Window;
			}
		}
	};

	public ref class DisplayRecordingSource : public RecordingSource {
	private:
		static DisplayRecordingSource^ _allMonitors = gcnew DisplayRecordingSource(gcnew String(ALL_MONITORS_ID));
	public:
		static property DisplayRecordingSource^ AllMonitors {
			DisplayRecordingSource^ get() {
				return _allMonitors;
			}
		}
		static property DisplayRecordingSource^ MainMonitor {
			DisplayRecordingSource^ get() {
				DisplayRecordingSource^ source = gcnew DisplayRecordingSource();
				IDXGIOutput* output;
				GetMainOutput(&output);
				DXGI_OUTPUT_DESC outputDesc;
				output->GetDesc(&outputDesc);
				source->DeviceName = gcnew String(outputDesc.DeviceName);
				return source;
			}
		}
		property String^ DeviceName;

		DisplayRecordingSource() {	}
		DisplayRecordingSource(String^ deviceName) {
			DeviceName = deviceName;
		}

		virtual property RecordingSourceType SourceType {
			RecordingSourceType get() {
				return RecordingSourceType::Display;
			}
		}
	};

	public enum class Anchor {
		TopLeft,
		TopRight,
		BottomLeft,
		BottomRight
	};

	public ref class RecordingOverlay abstract {
	public:
		RecordingOverlay() {
			AnchorPosition = Anchor::TopLeft;
		}
		property Anchor AnchorPosition;
		property int OffsetX;
		property int OffsetY;
		property int Width;
		property int Height;
	};
	public ref class CameraCaptureOverlay :RecordingOverlay {
	public:
		property String^ CaptureDeviceName;
	};
	public ref class VideoOverlay :RecordingOverlay {
	public:
		property String^ FilePath;
	};
	public ref class PictureOverlay :RecordingOverlay {
	public:
		property String^ FilePath;
	};

	public ref class SourceOptions {
	public:
		static property SourceOptions^ AllMonitors {
			SourceOptions^ get() {
				SourceOptions^ options = gcnew SourceOptions();
				options->RecordingSources->Add(DisplayRecordingSource::AllMonitors);
				return options;
			}
		}
		static property SourceOptions^ MainMonitor {
			SourceOptions^ get() {
				SourceOptions^ options = gcnew SourceOptions();
				options->RecordingSources->Add(DisplayRecordingSource::MainMonitor);
				return options;
			}
		}
		property List<RecordingSource^>^ RecordingSources;
		property int Left;
		property int Top;
		property int Right;
		property int Bottom;
		SourceOptions() {
			RecordingSources = gcnew List<RecordingSource^>();
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
			SnapshotFormat = ImageFormat::PNG;
			SnapshotsWithVideo = false;
			SnapshotsInterval = 10;
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
		/// <summary>
		///Image format for snapshots. This is only used with Snapshot and Slideshow modes.
		/// </summary>
		property ImageFormat SnapshotFormat;
		/// <summary>
		///Whether to take snapshots in a video recording. This is only used with Video mode.
		/// </summary>
		property bool SnapshotsWithVideo;
		/// <summary>
		///Interval in second for taking snapshots in a video recording. This is only used with Video mode AND SnapshotsWithVideo enabled.
		/// </summary>
		property int SnapshotsInterval;
		/// <summary>
		///Directory to store snapshots. If not set, the directory of the output file is used.
		/// </summary>
		property String^ SnapshotsDirectory;
	};
	public ref class AudioOptions {
	public:
		AudioOptions() {
			IsAudioEnabled = false;
			IsOutputDeviceEnabled = true;
			IsInputDeviceEnabled = false;
			Bitrate = AudioBitrate::bitrate_96kbps;
			Channels = AudioChannels::Stereo;
			InputVolume = 1.0f;
			OutputVolume = 1.0f;
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
		///Audio device to capture system audio from via loopback capture. Pass null or empty string to select system default.
		/// </summary>
		property String^ AudioOutputDevice;
		/// <summary>
		///Audio input device (e.g. microphone) to capture audio from. Pass null or empty string to select system default.
		/// </summary>
		property String^ AudioInputDevice;

		/// <summary>
		/// Volume if the input stream. Recommended values are between 0 and 1.
		/// Value of 0 mutes the stream and value of 1 makes it original volume.
		/// This value can be changed after the recording is started with SetInputVolume() method.
		/// </summary>
		property float InputVolume;

		/// <summary>
		/// Volume if the output stream. Recommended values are between 0 and 1.
		/// Value of 0 mutes the stream and value of 1 makes it original volume.
		/// This value can be changed after the recording is started with SetOutputVolume() method.
		/// </summary>
		property float OutputVolume;
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
			MouseClickDetectionMode = MouseDetectionMode::Polling;
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
		/// <summary>
		/// The mode for detecting mouse clicks. Default is Polling.
		/// </summary>
		property MouseDetectionMode MouseClickDetectionMode;

	};
	public ref class OverLayOptions {
	public:
		OverLayOptions() {

		}
		property List<RecordingOverlay^>^ Overlays;
	};
	public ref class RecorderOptions {
	public:
		static property RecorderOptions^ DefaultAllMonitors {
			RecorderOptions^ get() {
				RecorderOptions^ rec = gcnew RecorderOptions();
				rec->SourceOptions = ScreenRecorderLib::SourceOptions::AllMonitors;
				return rec;
			}
		}
		static property RecorderOptions^ DefaultMainMonitor {
			RecorderOptions^ get() {
				RecorderOptions^ rec = gcnew RecorderOptions();
				rec->SourceOptions = ScreenRecorderLib::SourceOptions::MainMonitor;
				return rec;
			}
		}
		RecorderOptions() {
			RecorderMode = ScreenRecorderLib::RecorderMode::Video;
			RecorderApi = ScreenRecorderLib::RecorderApi::DesktopDuplication;
			IsThrottlingDisabled = false;
			IsLowLatencyEnabled = false;
			IsHardwareEncodingEnabled = true;
			IsMp4FastStartEnabled = true;
#if _DEBUG
			IsLogEnabled = true;
			LogSeverityLevel = LogLevel::Debug;
#else
			IsLogEnabled = false;
			LogSeverityLevel = LogLevel::Info;
#endif
		}
		property RecorderMode RecorderMode;
		property RecorderApi RecorderApi;
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
		/// <summary>
		/// Toggles logging. Default is on when debugging, off in release mode, this setting overrides it.
		/// </summary>
		property bool IsLogEnabled;
		/// <summary>
		/// A path to a file to write logs to. If this is not empty, all logs will be redirected to it.
		/// </summary>
		property String^ LogFilePath;
		/// <summary>
		/// The maximum level of the logs to write.
		/// </summary>
		property LogLevel LogSeverityLevel;

		property VideoOptions^ VideoOptions;
		property SourceOptions^ SourceOptions;
		property AudioOptions^ AudioOptions;
		property MouseOptions^ MouseOptions;
		property OverLayOptions^ OverlayOptions;
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

	public ref class RecordableWindow : WindowRecordingSource {
	public:
		RecordableWindow() {}
		RecordableWindow(String^ title, IntPtr handle) :WindowRecordingSource(handle)
		{
			Title = title;
		}
		property String^ Title;

		bool IsMinmimized() {
			return IsIconic(((HWND)Handle.ToPointer()));
		}
		bool IsValidWindow() {
			return IsWindow(((HWND)Handle.ToPointer()));
		}
		ScreenRect^ GetScreenCoordinates() {
			if (Handle != IntPtr::Zero) {
				HWND hwnd = (HWND)Handle.ToPointer();
				if (IsWindow(hwnd)) {
					RECT rect;
					GetWindowRect(hwnd, &rect);
					if (rect.right > rect.left) {
						return gcnew ScreenRect(rect.left, rect.top, RectWidth(rect), RectHeight(rect));
					}
				}
			}
			return gcnew ScreenRect();
		}
	};

	public ref class RecordableDisplay : DisplayRecordingSource {
	public:
		RecordableDisplay() :DisplayRecordingSource() {
		}
		RecordableDisplay(String^ monitorName, String^ deviceName) :DisplayRecordingSource(deviceName) {
			MonitorName = monitorName;
		}
		property String^ MonitorName;
		ScreenRect^ GetScreenCoordinates() {
			if (!String::IsNullOrEmpty(DeviceName)) {
				IDXGIOutput* pOutput;
				if (SUCCEEDED(GetOutputForDeviceName(msclr::interop::marshal_as<std::wstring>(DeviceName), &pOutput))) {
					DXGI_OUTPUT_DESC desc;
					pOutput->GetDesc(&desc);
					pOutput->Release();
					return gcnew ScreenRect(desc.DesktopCoordinates.left, desc.DesktopCoordinates.top, RectWidth(desc.DesktopCoordinates), RectHeight(desc.DesktopCoordinates));
				}
			}
			return gcnew ScreenRect();
		}
	};

	public ref class SnapshotSavedEventArgs :System::EventArgs {
	public:
		property String^ SnapshotPath;
		SnapshotSavedEventArgs(String^ path) {
			SnapshotPath = path;
		}
	};

	public ref class Recorder {
	private:
		Recorder(RecorderOptions^ options);
		~Recorder();
		!Recorder();
		RecorderStatus _status;
		void CreateErrorCallback();
		void CreateCompletionCallback();
		void CreateStatusCallback();
		void CreateSnapshotCallback();
		void EventComplete(std::wstring str, nlohmann::fifo_map<std::wstring, int> delays);
		void EventFailed(std::wstring str);
		void EventStatusChanged(int status);
		void EventSnapshotCreated(std::wstring str);
		void SetupCallbacks();
		void ClearCallbacks();
		GCHandle _statusChangedDelegateGcHandler;
		GCHandle _errorDelegateGcHandler;
		GCHandle _completedDelegateGcHandler;
		GCHandle _snapshotDelegateGcHandler;
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
		RecordingManager* m_Rec;
		void Record(System::String^ path);
		void Record(System::Runtime::InteropServices::ComTypes::IStream^ stream);
		void Record(System::IO::Stream^ stream);
		void Pause();
		void Resume();
		void Stop();
		void SetOptions(RecorderOptions^ options);
		void SetInputVolume(float volume);
		void SetOutputVolume(float volume);

		std::vector<RECORDING_SOURCE> CreateRecordingSourceList(RecorderOptions^ options);
		std::vector<RECORDING_OVERLAY> CreateOverlayList(List<RecordingOverlay^>^ managedOverlays);

		static bool SetExcludeFromCapture(System::IntPtr hwnd, bool isExcluded);
		static Recorder^ CreateRecorder();
		static Recorder^ CreateRecorder(RecorderOptions^ options);
		static List<RecordableWindow^>^ GetWindows();
		static Dictionary<String^, String^>^ GetSystemAudioDevices(AudioDeviceSource source);
		static Dictionary<String^, String^>^ GetSystemVideoCaptureDevices();
		static List<RecordableDisplay^>^ GetDisplays();
		static ScreenSize^ GetCombinedOutputSizeForRecordingSources(List<RecordingSource^>^ recordingSources);
		event EventHandler<RecordingCompleteEventArgs^>^ OnRecordingComplete;
		event EventHandler<RecordingFailedEventArgs^>^ OnRecordingFailed;
		event EventHandler<RecordingStatusEventArgs^>^ OnStatusChanged;
		event EventHandler<SnapshotSavedEventArgs^>^ OnSnapshotSaved;
		ManagedIStream* m_ManagedStream;
	};
}
