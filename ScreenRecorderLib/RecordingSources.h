#pragma once
#include "../ScreenRecorderLibNative/Native.h"
#include "VideoCaptureFormat.h"

using namespace System;
using namespace System::ComponentModel;
using namespace System::Collections::Generic;

namespace ScreenRecorderLib {

	public enum class RecorderApi {
		///<summary>Desktop Duplication is supported on all Windows 8 and 10 versions. This API supports recording of screens.</summary>
		DesktopDuplication = 0,
		///<summary>WindowsGraphicsCapture requires Windows 10 version 1803 or higher. This API supports recording windows in addition to screens.</summary>
		WindowsGraphicsCapture = 1,
	};

	public ref class RecordingSourceBase abstract : public INotifyPropertyChanged {
	private:
		String^ _id;
		ScreenSize^ _outputSize;
		ScreenPoint^ _position;
		Anchor _anchorPoint;
		StretchMode _stretch;
		ScreenRect^ _sourceRect;
		bool _isVideoCaptureEnabled;
	internal:
		RecordingSourceBase() {
			ID = Guid::NewGuid().ToString();
			Stretch = StretchMode::Uniform;
			AnchorPoint = Anchor::Center;
			IsVideoCaptureEnabled = true;
		}
		RecordingSourceBase(RecordingSourceBase^ base) :RecordingSourceBase() {
			ID = base->ID;
			Position = base->Position;
			OutputSize = base->OutputSize;
			AnchorPoint = base->AnchorPoint;
			Stretch = base->Stretch;
			SourceRect = base->SourceRect;
			IsVideoCaptureEnabled = base->IsVideoCaptureEnabled;
		}
	public:
		virtual event PropertyChangedEventHandler^ PropertyChanged;
		/// <summary>
		/// A unique generated ID for this recording source.
		/// </summary>
		property String^ ID {
			String^ get() {
				return _id;
			}
	private:
		void set(String^ id) {
			_id = id;
		}
		}

		/// <summary>
		/// This option can be configured to set the frame size of this source in pixels.
		/// </summary>
		property ScreenSize^ OutputSize {
			ScreenSize^ get() {
				return _outputSize;
			}
			void set(ScreenSize^ rect) {
				_outputSize = rect;
				OnPropertyChanged("OutputSize");
			}
		}
		/// <summary>
		/// This option can be configured to position the source frame within the output frame.
		/// </summary>
		property ScreenPoint^ Position {
			ScreenPoint^ get() {
				return _position;
			}
			void set(ScreenPoint^ pos) {
				_position = pos;
				OnPropertyChanged("Position");
			}
		}
		/// <summary>
		/// The point where the source anchors to.
		/// </summary>
		property Anchor AnchorPoint {
			Anchor get() {
				return _anchorPoint;
			}
			void set(Anchor anchor) {
				_anchorPoint = anchor;
				OnPropertyChanged("AnchorPoint");
			}
		}
		/// <summary>
		/// Gets or sets a value that describes how a recording source should be stretched to fill the destination rectangle.
		/// </summary>
		property StretchMode Stretch {
			StretchMode get() {
				return _stretch;
			}
			void set(StretchMode stretch) {
				_stretch = stretch;
				OnPropertyChanged("Stretch");
			}
		}
		property ScreenRect^ SourceRect {
			ScreenRect^ get() {
				return _sourceRect;
			}
			void set(ScreenRect^ rect) {
				_sourceRect = rect;
				OnPropertyChanged("SourceRect");
			}
		}
		property bool IsVideoCaptureEnabled {
			bool get() {
				return _isVideoCaptureEnabled;
			}
			void set(bool value) {
				_isVideoCaptureEnabled = value;
				OnPropertyChanged("IsVideoCaptureEnabled");
			}
		}
		void OnPropertyChanged(String^ info)
		{
			PropertyChanged(this, gcnew PropertyChangedEventArgs(info));
		}
	};

	public ref class WindowRecordingSource : public RecordingSourceBase {
	private:
		bool _isCursorCaptureEnabled = true;
		bool _isBorderRequired = true;
		IntPtr _handle;
	public:


		WindowRecordingSource() :RecordingSourceBase()
		{

		}
		WindowRecordingSource(IntPtr windowHandle) :WindowRecordingSource() {
			Handle = windowHandle;
		}
		WindowRecordingSource(WindowRecordingSource^ source) :RecordingSourceBase(source) {
			Handle = source->Handle;
			IsCursorCaptureEnabled = source->IsCursorCaptureEnabled;
			IsBorderRequired = source->IsBorderRequired;
		}
		property RecorderApi RecorderApi {
			ScreenRecorderLib::RecorderApi get() {
				return ScreenRecorderLib::RecorderApi::WindowsGraphicsCapture;
			}
		}

		/// <summary>
		///This HWND of the window to record
		/// </summary>
		property IntPtr Handle {
			IntPtr get() {
				return _handle;
			}
			void set(IntPtr value) {
				_handle = value;
				OnPropertyChanged("Handle");
			}
		}
		/// <summary>
		///This option determines if the mouse cursor is recorded for this source. Defaults to true.
		/// </summary>
		property bool IsCursorCaptureEnabled {
			bool get() {
				return _isCursorCaptureEnabled;
			}
			void set(bool value) {
				_isCursorCaptureEnabled = value;
				OnPropertyChanged("IsCursorCaptureEnabled");
			}
		}
		/// <summary>
		///Gets or sets a value specifying whether a Windows Graphics Capture operation requires a colored border around the window or display to indicate that a capture is in progress.
		///Requires Windows 11.
		/// </summary>
		property bool IsBorderRequired {
			bool get() {
				return _isBorderRequired;
			}
			void set(bool value) {
				_isBorderRequired = value;
				OnPropertyChanged("IsBorderRequired");
			}
		}
	};

	public ref class DisplayRecordingSource : public RecordingSourceBase {
	private:
		RecorderApi _recorderApi = ScreenRecorderLib::RecorderApi::DesktopDuplication;
		bool _isCursorCaptureEnabled = true;
		bool _isBorderRequired = true;
		String^ _deviceName;
	public:
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
		/// <summary>
		///The device name to record, e.g. \\\\.\\DISPLAY1\
		/// </summary>
		property String^ DeviceName {
			String^ get() {
				return _deviceName;
			}
			void set(String^ value) {
				_deviceName = value;
				OnPropertyChanged("DeviceName");
			}
		}

		DisplayRecordingSource()
		{

		}
		DisplayRecordingSource(String^ deviceName) :DisplayRecordingSource() {
			DeviceName = deviceName;
		}
		DisplayRecordingSource(DisplayRecordingSource^ source) :RecordingSourceBase(source) {
			DeviceName = source->DeviceName;
			IsCursorCaptureEnabled = source->IsCursorCaptureEnabled;
			IsBorderRequired = source->IsBorderRequired;
			RecorderApi = source->RecorderApi;
		}

		property RecorderApi RecorderApi {
			ScreenRecorderLib::RecorderApi get() {
				return _recorderApi;
			}
			void set(ScreenRecorderLib::RecorderApi api) {
				_recorderApi = api;
				OnPropertyChanged("RecorderApi");
			}
		}
		/// <summary>
		///This option determines if the mouse cursor is recorded for this source. Defaults to true.
		/// </summary>
		property bool IsCursorCaptureEnabled {
			bool get() {
				return _isCursorCaptureEnabled;
			}
			void set(bool value) {
				_isCursorCaptureEnabled = value;
				OnPropertyChanged("IsCursorCaptureEnabled");
			}
		}
		/// <summary>
		///Gets or sets a value specifying whether a Windows Graphics Capture operation requires a colored border around the window or display to indicate that a capture is in progress.
		///Requires Windows 11.
		/// </summary>
		property bool IsBorderRequired {
			bool get() {
				return _isBorderRequired;
			}
			void set(bool value) {
				_isBorderRequired = value;
				OnPropertyChanged("IsBorderRequired");
			}
		}
	};

	public ref class VideoCaptureRecordingSource : public RecordingSourceBase {
	public:
		/// <summary>
		/// The device name to record
		/// </summary>
		property String^ DeviceName;

		property VideoCaptureFormat^ CaptureFormat;

		VideoCaptureRecordingSource() :RecordingSourceBase()
		{

		}
		VideoCaptureRecordingSource(String^ deviceName) :VideoCaptureRecordingSource() {
			DeviceName = deviceName;
		}
		VideoCaptureRecordingSource(String^ deviceName, VideoCaptureFormat^ captureFormat) :VideoCaptureRecordingSource() {
			DeviceName = deviceName;
			CaptureFormat = captureFormat;
		}
		VideoCaptureRecordingSource(VideoCaptureRecordingSource^ source) :RecordingSourceBase(source) {
			DeviceName = source->DeviceName;
			CaptureFormat = source->CaptureFormat;
		}
	};


	public ref class VideoRecordingSource : public RecordingSourceBase {
	public:
		/// <summary>
		/// The file path to the video
		/// </summary>
		property String^ SourcePath;
		property System::IO::Stream^ SourceStream;

		VideoRecordingSource()
		{

		}
		VideoRecordingSource(String^ path) :VideoRecordingSource() {
			SourcePath = path;
		}
		VideoRecordingSource(VideoRecordingSource^ source) :RecordingSourceBase(source) {
			SourcePath = source->SourcePath;
			SourceStream = source->SourceStream;
		}
		VideoRecordingSource(System::IO::Stream^ stream) {
			SourceStream = stream;
		}
	};

	public ref class ImageRecordingSource : public RecordingSourceBase {
	public:
		/// <summary>
		/// The file path to the video
		/// </summary>
		property String^ SourcePath;
		property System::IO::Stream^ SourceStream;

		ImageRecordingSource()
		{

		}
		ImageRecordingSource(String^ path) :ImageRecordingSource() {
			SourcePath = path;
		}
		ImageRecordingSource(ImageRecordingSource^ source) :RecordingSourceBase(source) {
			SourcePath = source->SourcePath;
			SourceStream = source->SourceStream;
		}
		ImageRecordingSource(System::IO::Stream^ stream) {
			SourceStream = stream;
		}
	};

	public ref class RecordableCamera : VideoCaptureRecordingSource {
	public:
		RecordableCamera() {}
		RecordableCamera(String^ friendlyName, String^ deviceName) :VideoCaptureRecordingSource(deviceName)
		{
			FriendlyName = friendlyName;
		}
		property String^ FriendlyName;
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
	};

	public ref class RecordableDisplay : DisplayRecordingSource {
	public:
		RecordableDisplay() :DisplayRecordingSource() {
		}
		RecordableDisplay(String^ friendlyName, String^ deviceName) :DisplayRecordingSource(deviceName) {
			FriendlyName = friendlyName;
		}
		property String^ FriendlyName;

	};
}