#pragma once
#include "../ScreenRecorderLibNative/Native.h"
#include "Coordinates.h"

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
	internal:
		RecordingSourceBase() {

		}
	public:
		virtual event PropertyChangedEventHandler^ PropertyChanged;

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


		void OnPropertyChanged(String^ info)
		{
			PropertyChanged(this, gcnew PropertyChangedEventArgs(info));
		}
	};

	public ref class WindowRecordingSource : public RecordingSourceBase {
	private:
		bool _isCursorCaptureEnabled;
	public:
		/// <summary>
		/// The handle to the window to record.
		/// </summary>
		property IntPtr Handle;

		WindowRecordingSource()
		{
			IsCursorCaptureEnabled = true;
		}
		WindowRecordingSource(IntPtr windowHandle) :WindowRecordingSource() {
			Handle = windowHandle;
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

	public ref class DisplayRecordingSource : public RecordingSourceBase {
	private:
		ScreenRect^ _sourceRect;
		RecorderApi _recorderApi;
		bool _isCursorCaptureEnabled;
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
			RecorderApi = ScreenRecorderLib::RecorderApi::DesktopDuplication;
			IsCursorCaptureEnabled = true;
		}
		DisplayRecordingSource(String^ deviceName) :DisplayRecordingSource() {
			DeviceName = deviceName;
		}

		virtual property ScreenRect^ SourceRect {
			ScreenRect^ get() {
				return _sourceRect;
			}
			void set(ScreenRect^ rect) {
				_sourceRect = rect;
				OnPropertyChanged("SourceRect");
			}
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

	public ref class VideoCaptureRecordingSource : public RecordingSourceBase {
	private:
		ScreenRect^ _sourceRect;
	public:
		/// <summary>
		/// The device name to record
		/// </summary>
		property String^ DeviceName;

		VideoCaptureRecordingSource()
		{

		}
		VideoCaptureRecordingSource(String^ deviceName) :VideoCaptureRecordingSource() {
			DeviceName = deviceName;
		}

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


	public ref class VideoRecordingSource : public RecordingSourceBase {
	private:
		ScreenRect^ _sourceRect;
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

	public ref class ImageRecordingSource : public RecordingSourceBase {
	private:
		ScreenRect^ _sourceRect;
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