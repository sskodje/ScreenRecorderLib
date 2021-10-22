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
		Video = (int)RecorderModeInternal::Video,
		///<summary>Record a slideshow of pictures. </summary>
		Slideshow = (int)RecorderModeInternal::Slideshow,
		///<summary>Create a single screenshot.</summary>
		Screenshot = (int)RecorderModeInternal::Screenshot
	};

	public ref class SourceOptions : public INotifyPropertyChanged {
	private:
		List<RecordingSourceBase^>^ _recordingSources;
	public:
		static property SourceOptions^ MainMonitor {
			SourceOptions^ get() {
				SourceOptions^ options = gcnew SourceOptions();
				options->RecordingSources->Add(DisplayRecordingSource::MainMonitor);
				return options;
			}
		}
		property List<RecordingSourceBase^>^ RecordingSources {
			List<RecordingSourceBase^>^ get() {
				return _recordingSources;
			}
			void set(List<RecordingSourceBase^>^ value) {
				_recordingSources = value;
				OnPropertyChanged("RecordingSources");
			}
		}

		SourceOptions() {
			RecordingSources = gcnew List<RecordingSourceBase^>();
		}
		virtual event PropertyChangedEventHandler^ PropertyChanged;
		void OnPropertyChanged(String^ info)
		{
			PropertyChanged(this, gcnew PropertyChangedEventArgs(info));
		}
	};

	public ref class OutputOptions : public INotifyPropertyChanged {
	private: 
		StretchMode _stretch;
		ScreenSize^ _outputFrameSize;
		ScreenRect^ _sourceRect;
	public:
		OutputOptions() {
			Stretch = StretchMode::Uniform;
			OutputFrameSize = ScreenSize::Empty;
			SourceRect = ScreenRect::Empty;
		}
		virtual event PropertyChangedEventHandler^ PropertyChanged;
		void OnPropertyChanged(String^ info)
		{
			PropertyChanged(this, gcnew PropertyChangedEventArgs(info));
		}
		/// <summary>
		/// How the output should be stretched to fill the destination rectangle.
		/// </summary>
		property StretchMode Stretch {
			StretchMode get() {
				return _stretch;
			}
			void set(StretchMode value) {
				_stretch = value;
				OnPropertyChanged("Stretch");
			}
		}
		/// <summary>
		/// The frame size of the output in pixels.
		/// </summary>
		property ScreenSize^ OutputFrameSize {
			ScreenSize^ get() {
				return _outputFrameSize;
			}
			void set(ScreenSize^ value) {
				_outputFrameSize = value;
				OnPropertyChanged("OutputFrameSize");
			}
		}
		/// <summary>
		/// The part of the output to record. Null or empty records the entire source area, else the output is cropped to the rectangle.
		/// </summary>
		property ScreenRect^ SourceRect {
			ScreenRect^ get() {
				return _sourceRect;
			}
			void set(ScreenRect^ value) {
				_sourceRect = value;
				OnPropertyChanged("SourceRect");
			}
		}
	};

	public ref class VideoEncoderOptions : public INotifyPropertyChanged {
	private:
		int _framerate;
		int _quality;
		int _bitrate;
		bool _isFixedFramerate;
		bool _isThrottlingDisabled;
		bool _isLowLatencyEnabled;
		bool _isHardwareEncodingEnabled;
		bool _isMp4FastStartEnabled;
		bool _isFragmentedMp4Enabled;
		IVideoEncoder^ _encoder = gcnew H264VideoEncoder();
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
			IsFragmentedMp4Enabled = false;
			Encoder = gcnew H264VideoEncoder();
		}
		virtual event PropertyChangedEventHandler^ PropertyChanged;
		void OnPropertyChanged(String^ info)
		{
			PropertyChanged(this, gcnew PropertyChangedEventArgs(info));
		}
		/// <summary>
		///Framerate in frames per second.
		/// </summary>
		property int Framerate {
			int get() {
				return _framerate;
			}
			void set(int value) {
				_framerate = value;
				OnPropertyChanged("Framerate");
			}
		}
		/// <summary>
		///Bitrate in bits per second
		/// </summary>
		property int Bitrate {
			int get() {
				return _bitrate;
			}
			void set(int value) {
				_bitrate = value;
				OnPropertyChanged("Bitrate");
			}
		}
		/// <summary>
		///Video quality. This is only used when BitrateMode is set to Quality.
		/// </summary>
		property int Quality {
			int get() {
				return _quality;
			}
			void set(int value) {
				_quality = value;
				OnPropertyChanged("Quality");
			}
		}
		/// <summary>
		///Send data to the video encoder every frame, even if it means duplicating the previous frame(s). Can fix stutter issues in fringe cases, but uses more resources.
		/// </summary>
		property bool IsFixedFramerate {
			bool get() {
				return _isFixedFramerate;
			}
			void set(bool value) {
				_isFixedFramerate = value;
				OnPropertyChanged("IsFixedFramerate");
			}
		}
		/// <summary>
		///Disable throttling of video renderer. If this is disabled, all frames are sent to renderer as fast as they come. Can cause out of memory crashes.
		/// </summary>
		property bool IsThrottlingDisabled {
			bool get() {
				return _isThrottlingDisabled;
			}
			void set(bool value) {
				_isThrottlingDisabled = value;
				OnPropertyChanged("IsThrottlingDisabled");
			}
		}
		/// <summary>
		///Faster rendering, but can affect quality. Use when speed is more important than quality.
		/// </summary>
		property bool IsLowLatencyEnabled {
			bool get() {
				return _isLowLatencyEnabled;
			}
			void set(bool value) {
				_isLowLatencyEnabled = value;
				OnPropertyChanged("IsLowLatencyEnabled");
			}
		}
		/// <summary>
		///Enable hardware encoding if available. This is enabled by default.
		/// </summary>
		property bool IsHardwareEncodingEnabled {
			bool get() {
				return _isHardwareEncodingEnabled;
			}
			void set(bool value) {
				_isHardwareEncodingEnabled = value;
				OnPropertyChanged("IsHardwareEncodingEnabled");
			}
		}
		/// <summary>
		/// Place the mp4 header at the start of the file instead of the end. This allows streaming to start before entire file is downloaded.
		/// </summary>
		property bool IsMp4FastStartEnabled {
			bool get() {
				return _isMp4FastStartEnabled;
			}
			void set(bool value) {
				_isMp4FastStartEnabled = value;
				OnPropertyChanged("IsMp4FastStartEnabled");
			}
		}
		/// <summary>
		/// Fragments the video into a list of individually playable blocks. This allows playback of video segments that has no end, i.e. live streaming.
		/// </summary>
		property bool IsFragmentedMp4Enabled {
			bool get() {
				return _isFragmentedMp4Enabled;
			}
			void set(bool value) {
				_isFragmentedMp4Enabled = value;
				OnPropertyChanged("IsFragmentedMp4Enabled");
			}
		}
		/// <summary>
		/// Set the video encoder to use. Current supported encoders are H264VideoEncoder and H265VideoEncoder.
		/// </summary>
		property IVideoEncoder^ Encoder {
			IVideoEncoder^ get() {
				return _encoder;
			}
			void set(IVideoEncoder^ value) {
				_encoder = value;
				OnPropertyChanged("Encoder");
			}
		}
	};

	public ref class SnapshotOptions : public INotifyPropertyChanged {
	private:
		ImageFormat _snapshotFormat;
		bool _snapshotsWithVideo;
		int _snapshotsIntervalMillis;
		String^ _snapshotsDirectory;
	public:
		SnapshotOptions() {
			SnapshotFormat = ImageFormat::PNG;
			SnapshotsWithVideo = false;
			SnapshotsIntervalMillis = 10000;
		}
		virtual event PropertyChangedEventHandler^ PropertyChanged;
		void OnPropertyChanged(String^ info)
		{
			PropertyChanged(this, gcnew PropertyChangedEventArgs(info));
		}
		/// <summary>
		///Image format for snapshots. This is used with Screenshot, Slideshow, and video with SnapshotsWithVideo enabled.
		/// </summary>
		property ImageFormat SnapshotFormat {
			ImageFormat get() {
				return _snapshotFormat;
			}
			void set(ImageFormat value) {
				_snapshotFormat = value;
				OnPropertyChanged("SnapshotFormat");
			}
		}
		/// <summary>
		///Whether to take snapshots in a video recording. This is only used with Video mode.
		/// </summary>
		property bool SnapshotsWithVideo {
			bool get() {
				return _snapshotsWithVideo;
			}
			void set(bool value) {
				_snapshotsWithVideo = value;
				OnPropertyChanged("SnapshotsWithVideo");
			}
		}
		/// <summary>
		///Interval in milliseconds between images in slideshows, or snapshots in a video recording.
		/// </summary>
		property int SnapshotsIntervalMillis {
			int get() {
				return _snapshotsIntervalMillis;
			}
			void set(int value) {
				_snapshotsIntervalMillis = value;
				OnPropertyChanged("SnapshotsIntervalMillis");
			}
		}
		/// <summary>
		///Directory to store snapshots. If not set, the directory of the output file is used.
		/// </summary>
		property String^ SnapshotsDirectory {
			String^ get() {
				return _snapshotsDirectory;
			}
			void set(String^ value) {
				_snapshotsDirectory = value;
				OnPropertyChanged("SnapshotsDirectory");
			}
		}
	};

	public ref class DynamicAudioOptions : public INotifyPropertyChanged {
	private:
		Nullable<float> _inputVolume;
		Nullable<float> _outputVolume;
		Nullable<bool> _isInputDeviceEnabled;
		Nullable<bool> _isOutputDeviceEnabled;
	public:
		DynamicAudioOptions() {

		}
		virtual event PropertyChangedEventHandler^ PropertyChanged;
		void OnPropertyChanged(String^ info)
		{
			PropertyChanged(this, gcnew PropertyChangedEventArgs(info));
		}
		/// <summary>
		///Enable to record system audio output.
		/// </summary>
		property Nullable<bool> IsOutputDeviceEnabled {
			Nullable<bool> get() {
				return _isOutputDeviceEnabled;
			}
			void set(Nullable<bool> value) {
				_isOutputDeviceEnabled = value;
				OnPropertyChanged("IsOutputDeviceEnabled");
			}
		}
		/// <summary>
		///Enable to record system audio input (e.g. microphone)
		/// </summary>
		property Nullable<bool> IsInputDeviceEnabled {
			Nullable<bool> get() {
				return _isInputDeviceEnabled;
			}
			void set(Nullable<bool> value) {
				_isInputDeviceEnabled = value;
				OnPropertyChanged("IsInputDeviceEnabled");
			}
		}
		/// <summary>
		/// Volume if the input stream. Recommended values are between 0 and 1.
		/// Value of 0 mutes the stream and value of 1 makes it original volume.
		/// This value can be changed after the recording is started with SetInputVolume() method.
		/// </summary>
		property Nullable<float> InputVolume {
			Nullable<float> get() {
				return _inputVolume;
			}
			void set(Nullable<float> value) {
				_inputVolume = value;
				OnPropertyChanged("InputVolume");
			}
		}

		/// <summary>
		/// Volume if the output stream. Recommended values are between 0 and 1.
		/// Value of 0 mutes the stream and value of 1 makes it original volume.
		/// This value can be changed after the recording is started with SetOutputVolume() method.
		/// </summary>
		property Nullable<float> OutputVolume {
			Nullable<float> get() {
				return _outputVolume;
			}
			void set(Nullable<float> value) {
				_outputVolume = value;
				OnPropertyChanged("OutputVolume");
			}
		}
	};

	public ref class AudioOptions :DynamicAudioOptions {
	private:
		Nullable<bool> _isAudioEnabled;
		Nullable<AudioBitrate> _bitrate;
		Nullable<AudioChannels> _channels;
		String^ _audioInputDevice;
		String^ _audioOutputDevice;

	public:
		AudioOptions() :DynamicAudioOptions() {
			Bitrate = AudioBitrate::bitrate_96kbps;
			Channels = AudioChannels::Stereo;
			IsAudioEnabled = false;
			IsOutputDeviceEnabled = true;
			IsInputDeviceEnabled = false;
			InputVolume = 1.0f;
			OutputVolume = 1.0f;
		}
		/// <summary>
		/// Enable or disable the writing of an audio track for the recording.
		/// </summary>
		property Nullable<bool> IsAudioEnabled {
			Nullable<bool> get() {
				return _isAudioEnabled;
			}
			void set(Nullable<bool> value) {
				_isAudioEnabled = value;
				OnPropertyChanged("IsAudioEnabled");
			}
		}
		property  Nullable<AudioBitrate> Bitrate {
			Nullable<AudioBitrate> get() {
				return _bitrate;
			}
			void set(Nullable<AudioBitrate> value) {
				_bitrate = value;
				OnPropertyChanged("Bitrate");
			}
		}
		property  Nullable<AudioChannels> Channels {
			Nullable<AudioChannels> get() {
				return _channels;
			}
			void set(Nullable<AudioChannels> value) {
				_channels = value;
				OnPropertyChanged("Channels");
			}
		}
		/// <summary>
		///Audio device to capture system audio from via loopback capture. Pass null or empty string to select system default.
		/// </summary>
		property String^ AudioOutputDevice {
			String^ get() {
				return _audioOutputDevice;
			}
			void set(String^ value) {
				_audioOutputDevice = value;
				OnPropertyChanged("AudioOutputDevice");
			}
		}
		/// <summary>
		///Audio input device (e.g. microphone) to capture audio from. Pass null or empty string to select system default.
		/// </summary>
		property String^ AudioInputDevice {
			String^ get() {
				return _audioInputDevice;
			}
			void set(String^ value) {
				_audioInputDevice = value;
				OnPropertyChanged("AudioInputDevice");
			}
		}


	};

	public ref class DynamicMouseOptions : public INotifyPropertyChanged {
	private:
		Nullable<bool> _isMousePointerEnabled;
		Nullable<bool> _isMouseClicksDetected;
		Nullable<int> _mouseClickDetectionRadius;
		Nullable<int> _mouseClickDetectionDuration;
		String^ _mouseLeftClickDetectionColor;
		String^ _mouseRightClickDetectionColor;

	public:
		DynamicMouseOptions() {

		}
		virtual event PropertyChangedEventHandler^ PropertyChanged;
		void OnPropertyChanged(String^ info)
		{
			PropertyChanged(this, gcnew PropertyChangedEventArgs(info));
		}
		/// <summary>
		///Display the mouse cursor on the recording
		/// </summary>
		property Nullable<bool> IsMousePointerEnabled {
			Nullable<bool> get() {
				return _isMousePointerEnabled;
			}
			void set(Nullable<bool> value) {
				_isMousePointerEnabled = value;
				OnPropertyChanged("IsMousePointerEnabled");
			}
		}
		/// <summary>
		/// Display a colored dot where the left mouse button is pressed.
		/// </summary>
		property Nullable<bool> IsMouseClicksDetected {
			Nullable<bool> get() {
				return _isMouseClicksDetected;
			}
			void set(Nullable<bool> value) {
				_isMouseClicksDetected = value;
				OnPropertyChanged("IsMouseClicksDetected");
			}
		}
		/// <summary>
		/// The color of the dot where the left mouse button is pressed, in hex format. Default is Yellow (#FFFF00).
		/// </summary>
		property String^ MouseLeftClickDetectionColor {
			String^ get() {
				return _mouseLeftClickDetectionColor;
			}
			void set(String^ value) {
				_mouseLeftClickDetectionColor = value;
				OnPropertyChanged("MouseLeftClickDetectionColor");
			}
		}
		/// <summary>
		/// The color of the dot where the right mouse button is pressed, in hex format. Default is Yellow (#FFFF00).
		/// </summary>
		property String^ MouseRightClickDetectionColor {
			String^ get() {
				return _mouseRightClickDetectionColor;
			}
			void set(String^ value) {
				_mouseRightClickDetectionColor = value;
				OnPropertyChanged("MouseRightClickDetectionColor");
			}
		}
		/// <summary>
		/// The radius of the dot where the mouse button is pressed. Default is 20.
		/// </summary>
		property Nullable<int> MouseClickDetectionRadius {
			Nullable<int> get() {
				return _mouseClickDetectionRadius;
			}
			void set(Nullable<int> value) {
				_mouseClickDetectionRadius = value;
				OnPropertyChanged("MouseClickDetectionRadius");
			}
		}
		/// <summary>
		/// The duration of the dot shown where the mouse button is pressed, in milliseconds. Default is 150.
		/// </summary>
		property Nullable<int> MouseClickDetectionDuration {
			Nullable<int> get() {
				return _mouseClickDetectionDuration;
			}
			void set(Nullable<int> value) {
				_mouseClickDetectionDuration = value;
				OnPropertyChanged("MouseClickDetectionDuration");
			}
		}
	};

	public ref class MouseOptions :DynamicMouseOptions {
	private:
		MouseDetectionMode _mouseClickDetectionMode;
	public:
		MouseOptions() :DynamicMouseOptions() {
			MouseClickDetectionMode = MouseDetectionMode::Polling;
			IsMousePointerEnabled = true;
			IsMouseClicksDetected = false;
			MouseLeftClickDetectionColor = "#FFFF00";
			MouseRightClickDetectionColor = "#FFFF00";
			MouseClickDetectionRadius = 20;
			MouseClickDetectionDuration = 150;
		}
		/// <summary>
		/// The mode for detecting mouse clicks. Default is Polling.
		/// </summary>
		property MouseDetectionMode MouseClickDetectionMode {
			MouseDetectionMode get() {
				return _mouseClickDetectionMode;
			}
			void set(MouseDetectionMode value) {
				_mouseClickDetectionMode = value;
				OnPropertyChanged("MouseClickDetectionMode");
			}
		}
	};

	public ref class OverLayOptions : public INotifyPropertyChanged {
	private:
		List<RecordingOverlayBase^>^ _overlays;
	public:
		OverLayOptions() {

		}
		virtual event PropertyChangedEventHandler^ PropertyChanged;
		void OnPropertyChanged(String^ info)
		{
			PropertyChanged(this, gcnew PropertyChangedEventArgs(info));
		}
		property List<RecordingOverlayBase^>^ Overlays {
			List<RecordingOverlayBase^>^ get() {
				return _overlays;
			}
			void set(List<RecordingOverlayBase^>^ value) {
				_overlays = value;
				OnPropertyChanged("Overlays");
			}
		}
	};

	public ref class LogOptions : public INotifyPropertyChanged {
	private:
		bool _isLogEnabled;
		String^ _logFilePath;
		LogLevel _logSeverityLevel;

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
		virtual event PropertyChangedEventHandler^ PropertyChanged;
		void OnPropertyChanged(String^ info)
		{
			PropertyChanged(this, gcnew PropertyChangedEventArgs(info));
		}
		/// <summary>
		/// Toggles logging. Default is on when debugging, off in release mode, this setting overrides it.
		/// </summary>
		property bool IsLogEnabled {
			bool get() {
				return _isLogEnabled;
			}
			void set(bool value) {
				_isLogEnabled = value;
				OnPropertyChanged("IsLogEnabled");
			}
		}
		/// <summary>
		/// A path to a file to write logs to. If this is not empty, all logs will be redirected to it.
		/// </summary>
		property String^ LogFilePath {
			String^ get() {
				return _logFilePath;
			}
			void set(String^ value) {
				_logFilePath = value;
				OnPropertyChanged("LogFilePath");
			}
		}
		/// <summary>
		/// The maximum level of the logs to write.
		/// </summary>
		property LogLevel LogSeverityLevel {
			LogLevel get() {
				return _logSeverityLevel;
			}
			void set(LogLevel value) {
				_logSeverityLevel = value;
				OnPropertyChanged("LogSeverityLevel");
			}
		}
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
		}
		property RecorderMode RecorderMode;

		property VideoEncoderOptions^ VideoEncoderOptions;
		property SourceOptions^ SourceOptions;
		property OutputOptions^ OutputOptions;
		property AudioOptions^ AudioOptions;
		property MouseOptions^ MouseOptions;
		property OverLayOptions^ OverlayOptions;
		property SnapshotOptions^ SnapshotOptions;
		property LogOptions^ LogOptions;
	};


	private ref class DynamicOptions {
	public:
		property DynamicAudioOptions^ AudioOptions;
		property DynamicMouseOptions^ MouseOptions;
		property Dictionary<String^, ScreenRect^>^ SourceRects;
		property Dictionary<String^, bool>^ SourceCursorCaptures;
		property ScreenRect^ GlobalSourceRect;
		property Dictionary<String^, ScreenSize^>^ OverlaySizes;
		property Dictionary<String^, ScreenSize^>^ OverlayOffsets;
		property Dictionary<String^, Anchor>^ OverlayAnchors;
	};
}
