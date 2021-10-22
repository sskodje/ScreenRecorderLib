#pragma once
#include "../ScreenRecorderLibNative/Native.h"

using namespace System;
using namespace System::ComponentModel;

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
	internal:
		RecordingSourceBase() {
			ID = Guid::NewGuid().ToString();
			Stretch = StretchMode::Uniform;
			AnchorPoint = Anchor::Center;
		}
		RecordingSourceBase(RecordingSourceBase^ base) :RecordingSourceBase() {
			ID = base->ID;
			Position = base->Position;
			OutputSize = base->OutputSize;
			AnchorPoint = base->AnchorPoint;
			Stretch = base->Stretch;
			SourceRect = base->SourceRect;
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
		void OnPropertyChanged(String^ info)
		{
			PropertyChanged(this, gcnew PropertyChangedEventArgs(info));
		}
	};

	public ref class WindowRecordingSource : public RecordingSourceBase {
	private:
		bool _isCursorCaptureEnabled = true;
	public:
		/// <summary>
		/// The handle to the window to record.
		/// </summary>
		property IntPtr Handle;

		WindowRecordingSource() :RecordingSourceBase()
		{

		}
		WindowRecordingSource(IntPtr windowHandle) :WindowRecordingSource() {
			Handle = windowHandle;
		}
		WindowRecordingSource(WindowRecordingSource^ source) :RecordingSourceBase(source) {
			Handle = source->Handle;
			IsCursorCaptureEnabled = source->IsCursorCaptureEnabled;
		}
		property RecorderApi RecorderApi {
			ScreenRecorderLib::RecorderApi get() {
				return ScreenRecorderLib::RecorderApi::WindowsGraphicsCapture;
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
	};

	public ref class DisplayRecordingSource : public RecordingSourceBase {
	private:
		RecorderApi _recorderApi = ScreenRecorderLib::RecorderApi::DesktopDuplication;
		bool _isCursorCaptureEnabled = true;
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
		/// The device name to record, e.g. \\\\.\\DISPLAY1\
		/// </summary>
		property String^ DeviceName;

		DisplayRecordingSource()
		{

		}
		DisplayRecordingSource(String^ deviceName) :DisplayRecordingSource() {
			DeviceName = deviceName;
		}
		DisplayRecordingSource(DisplayRecordingSource^ source) :RecordingSourceBase(source) {
			DeviceName = source->DeviceName;
			IsCursorCaptureEnabled = source->IsCursorCaptureEnabled;
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
	};

	public ref class VideoCaptureRecordingSource : public RecordingSourceBase {
	public:
		/// <summary>
		/// The device name to record
		/// </summary>
		property String^ DeviceName;

		VideoCaptureRecordingSource() :RecordingSourceBase()
		{

		}
		VideoCaptureRecordingSource(String^ deviceName) :VideoCaptureRecordingSource() {
			DeviceName = deviceName;
		}
		VideoCaptureRecordingSource(VideoCaptureRecordingSource^ source) :RecordingSourceBase(source) {
			DeviceName = source->DeviceName;
		}
	};


	public ref class VideoRecordingSource : public RecordingSourceBase {
	public:
		/// <summary>
		/// The file path to the video
		/// </summary>
		property String^ SourcePath;

		VideoRecordingSource()
		{

		}
		VideoRecordingSource(String^ path) :VideoRecordingSource() {
			SourcePath = path;
		}
		VideoRecordingSource(VideoRecordingSource^ source) :RecordingSourceBase(source) {
			SourcePath = source->SourcePath;
		}
	};

	public ref class ImageRecordingSource : public RecordingSourceBase {
	public:
		/// <summary>
		/// The file path to the video
		/// </summary>
		property String^ SourcePath;

		ImageRecordingSource()
		{

		}
		ImageRecordingSource(String^ path) :ImageRecordingSource() {
			SourcePath = path;
		}
		ImageRecordingSource(ImageRecordingSource^ source) :RecordingSourceBase(source) {
			SourcePath = source->SourcePath;
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