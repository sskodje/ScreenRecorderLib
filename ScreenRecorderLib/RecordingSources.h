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
		ScreenSize^ _outputSize;
		ScreenPoint^ _position;
		StretchMode _stretch;
		String^ _id;
	internal:
		RecordingSourceBase() {
			ID = Guid::NewGuid().ToString();
			Stretch = StretchMode::Uniform;
		}
		RecordingSourceBase(RecordingSourceBase^ base) :RecordingSourceBase() {
			ID = base->ID;
			Position = base->Position;
			OutputSize = base->OutputSize;
			Stretch = base->Stretch;
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
		virtual property ScreenSize^ OutputSize {
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
		virtual property ScreenPoint^ Position {
			ScreenPoint^ get() {
				return _position;
			}
			void set(ScreenPoint^ pos) {
				_position = pos;
				OnPropertyChanged("Position");
			}
		}
		/// <summary>
		/// Gets or sets a value that describes how a recording source should be stretched to fill the destination rectangle.
		/// </summary>
		virtual property StretchMode Stretch {
			StretchMode get() {
				return _stretch;
			}
			void set(StretchMode stretch) {
				_stretch = stretch;
				OnPropertyChanged("Stretch");
			}
		}

		void OnPropertyChanged(String^ info)
		{
			PropertyChanged(this, gcnew PropertyChangedEventArgs(info));
		}
	};

	public ref class CroppableRecordingSource abstract :RecordingSourceBase {
	private:
		ScreenRect^ _sourceRect;
	internal:
		CroppableRecordingSource() :RecordingSourceBase() {

		}
		CroppableRecordingSource(CroppableRecordingSource^ base) :RecordingSourceBase(base) {
			SourceRect = base->SourceRect;
		}
	public:
		virtual property ScreenRect^ SourceRect {
			ScreenRect^ get() {
				return _sourceRect;
			}
			void set(ScreenRect^ rect) {
				_sourceRect = rect;
				OnPropertyChanged("SourceRect");
			}
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
		}
		virtual property RecorderApi RecorderApi {
			ScreenRecorderLib::RecorderApi get() {
				return ScreenRecorderLib::RecorderApi::WindowsGraphicsCapture;
			}
		}
		/// <summary>
		///This option determines if the mouse cursor is recorded for this source. Defaults to true.
		/// </summary>
		virtual property bool IsCursorCaptureEnabled {
			bool get() {
				return _isCursorCaptureEnabled;
			}
			void set(bool value) {
				_isCursorCaptureEnabled = value;
				OnPropertyChanged("IsCursorCaptureEnabled");
			}
		}
	};

	public ref class DisplayRecordingSource : public CroppableRecordingSource {
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
		DisplayRecordingSource(DisplayRecordingSource^ source) :CroppableRecordingSource(source) {
			DeviceName = source->DeviceName;
		}

		virtual property RecorderApi RecorderApi {
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
		virtual property bool IsCursorCaptureEnabled {
			bool get() {
				return _isCursorCaptureEnabled;
			}
			void set(bool value) {
				_isCursorCaptureEnabled = value;
				OnPropertyChanged("IsCursorCaptureEnabled");
			}
		}
	};

	public ref class VideoCaptureRecordingSource : public CroppableRecordingSource {
	public:
		/// <summary>
		/// The device name to record
		/// </summary>
		property String^ DeviceName;

		VideoCaptureRecordingSource() :CroppableRecordingSource()
		{

		}
		VideoCaptureRecordingSource(String^ deviceName) :VideoCaptureRecordingSource() {
			DeviceName = deviceName;
		}
		VideoCaptureRecordingSource(VideoCaptureRecordingSource^ source) :CroppableRecordingSource(source) {
			DeviceName = source->DeviceName;
		}
	};


	public ref class VideoRecordingSource : public CroppableRecordingSource {
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
		VideoRecordingSource(VideoRecordingSource^ source) :CroppableRecordingSource(source) {
			SourcePath = source->SourcePath;
		}
	};

	public ref class ImageRecordingSource : public CroppableRecordingSource {
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
		ImageRecordingSource(ImageRecordingSource^ source) :CroppableRecordingSource(source) {
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