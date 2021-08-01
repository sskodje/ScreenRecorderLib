#pragma once
#include "Coordinates.h"
#include "RecordingSources.h"
#include "RecordingOverlays.h"
#include "VideoEncoders.h"

using namespace System;
using namespace System::Collections::Generic;

namespace ScreenRecorderLib {
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
		Polling = MOUSE_OPTIONS::MOUSE_DETECTION_MODE_POLLING,
		///<summary>
		///Use a low level system hook for detecting mouse clicks. Works more reliably for programmatic events, but can negatively affect mouse performance while recording.
		///</summary>
		Hook = MOUSE_OPTIONS::MOUSE_DETECTION_MODE_HOOK
	};

	public enum class ImageFormat {
		PNG,
		JPEG,
		TIFF,
		BMP
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
		///<summary>Record to mp4 container in H.264/AVC or H.265/HEVC format. </summary>
		Video = MODE_VIDEO,
		///<summary>Record a slideshow of pictures. </summary>
		Slideshow = MODE_SLIDESHOW,
		///<summary>Create a single screenshot.</summary>
		Screenshot = MODE_SCREENSHOT
	};


	public enum class RecorderApi {
		///<summary>Desktop Duplication is supported on all Windows 8 and 10 versions. This API supports recording of screens.</summary>
		DesktopDuplication = 0,
		///<summary>WindowsGraphicsCapture requires Windows 10 version 1803 or higher. This API supports recording windows in addition to screens.</summary>
		WindowsGraphicsCapture = 1,
	};


	public ref class SourceOptions {
	public:
		static property SourceOptions^ MainMonitor {
			SourceOptions^ get() {
				SourceOptions^ options = gcnew SourceOptions();
				options->RecordingSources->Add(DisplayRecordingSource::MainMonitor);
				return options;
			}
		}
		property List<RecordingSourceBase^>^ RecordingSources;
		/// <summary>
		/// The part of the combined source area to record. Null or empty records the entire source area.
		/// </summary>
		property ScreenRect^ SourceRect;

		SourceOptions() {
			RecordingSources = gcnew List<RecordingSourceBase^>();
			SourceRect = ScreenRect::Empty;
		}
	};

	public ref class VideoEncoderOptions {
	public:
		VideoEncoderOptions() {
			Framerate = 30;
			Quality = 70;
			Bitrate = 4000 * 1000;
			IsFixedFramerate = false;
			IsThrottlingDisabled = false;
			IsLowLatencyEnabled = false;
			IsHardwareEncodingEnabled = true;
			IsMp4FastStartEnabled = true;
			FrameSize = ScreenSize::Empty;
			Encoder = gcnew H264VideoEncoder();
		}
		/// <summary>
		/// The frame size of the video output in pixels.
		/// </summary>
		property ScreenSize^ FrameSize;
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
		/// Set the video encoder to use. Current supported encoders are H264VideoEncoder and H265VideoEncoder.
		/// </summary>
		property IVideoEncoder^ Encoder;
	};

	public ref class SnapshotOptions {
	public:
		SnapshotOptions() {
			SnapshotFormat = ImageFormat::PNG;
			SnapshotsWithVideo = false;
			SnapshotsIntervalMillis = 10000;
		}
		/// <summary>
		///Image format for snapshots. This is used with Screenshot, Slideshow, and video with SnapshotsWithVideo enabled.
		/// </summary>
		property ImageFormat SnapshotFormat;
		/// <summary>
		///Whether to take snapshots in a video recording. This is only used with Video mode.
		/// </summary>
		property bool SnapshotsWithVideo;
		/// <summary>
		///Interval in milliseconds between images in slideshows, or snapshots in a video recording.
		/// </summary>
		property int SnapshotsIntervalMillis;
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
		property List<RecordingOverlayBase^>^ Overlays;
	};
	public ref class LogOptions {
	public:
		LogOptions() {
#if _DEBUG
			IsLogEnabled = true;
			LogSeverityLevel = LogLevel::Debug;
#else
			IsLogEnabled = false;
			LogSeverityLevel = LogLevel::Info;
#endif
		}
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
	};

	public ref class RecorderOptions {
	public:
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
		}
		property RecorderMode RecorderMode;
		property RecorderApi RecorderApi;



		property VideoEncoderOptions^ VideoEncoderOptions;
		property SourceOptions^ SourceOptions;
		property AudioOptions^ AudioOptions;
		property MouseOptions^ MouseOptions;
		property OverLayOptions^ OverlayOptions;
		property SnapshotOptions^ SnapshotOptions;
		property LogOptions^ LogOptions;
	};
}
