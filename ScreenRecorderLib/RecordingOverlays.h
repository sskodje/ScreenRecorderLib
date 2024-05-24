#pragma once
#include "../ScreenRecorderLibNative/Native.h"
using namespace System;

namespace ScreenRecorderLib {

	public ref class RecordingOverlayBase abstract : public INotifyPropertyChanged {
	private:
		ScreenSize^ _size;
		ScreenSize^ _offset;
		Anchor _anchorPoint;
		String^ _id;
		StretchMode _stretch;
	internal:
		RecordingOverlayBase()
		{
			ID = Guid::NewGuid().ToString();
			AnchorPoint = Anchor::TopLeft;
			Offset = nullptr;
			Size = nullptr;
			Stretch = StretchMode::Uniform;
		}
		RecordingOverlayBase(RecordingOverlayBase^ base) :RecordingOverlayBase() {
			ID = base->ID;
			Offset = base->Offset;
			Size = base->Size;
			AnchorPoint = base->AnchorPoint;
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
		/// Gets or sets a value that describes how an overlay should be stretched to fill the destination rectangle.
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
		/// <summary>
		/// The point on the parent frame where the overlay anchors to.
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
		/// This option can be configured to set the size of this overlay in pixels.
		/// </summary>
		property ScreenSize^ Size {
			ScreenSize^ get() {
				return _size;
			}
			void set(ScreenSize^ rect) {
				_size = rect;
				OnPropertyChanged("Size");
			}
		}

		/// <summary>
		/// This option can be configured to position the overlay within the output frame.
		/// </summary>
		property ScreenSize^ Offset {
			ScreenSize^ get() {
				return _offset;
			}
			void set(ScreenSize^ offset) {
				_offset = offset;
				OnPropertyChanged("Offset");
			}
		}
		void OnPropertyChanged(String^ info)
		{
			PropertyChanged(this, gcnew PropertyChangedEventArgs(info));
		}
	};
	public ref class VideoCaptureOverlay :RecordingOverlayBase {
	private:
		String^ _deviceName;
	public:
		VideoCaptureOverlay() :RecordingOverlayBase() {

		}
		VideoCaptureOverlay(String^ deviceName) :RecordingOverlayBase() {
			DeviceName = deviceName;
		}
		VideoCaptureOverlay(VideoCaptureOverlay^ source) :RecordingOverlayBase(source) {
			DeviceName = source->DeviceName;
		}
		/// <summary>
		///The device name to record, e.g. \\\\.\\xxxxxxxxx\
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
	};
	public ref class VideoOverlay :RecordingOverlayBase {
	private:
		String^ _sourcePath;
		System::IO::Stream^ _sourceStream;
	public:
		VideoOverlay() :RecordingOverlayBase() {

		}
		VideoOverlay(String^ path) :RecordingOverlayBase() {
			SourcePath = path;
		}
		VideoOverlay(System::IO::Stream^ stream) :RecordingOverlayBase() {
			SourceStream = stream;
		}
		VideoOverlay(VideoOverlay^ source) :RecordingOverlayBase(source) {
			SourcePath = source->SourcePath;
			SourceStream = source->SourceStream;
		}
		/// <summary>
		///The file path of the video to record
		/// </summary>
		property String^ SourcePath {
			String^ get() {
				return _sourcePath;
			}
			void set(String^ value) {
				_sourcePath = value;
				OnPropertyChanged("SourcePath");
			}
		}
		/// <summary>
		///The source stream of the image to record
		/// </summary>
		property System::IO::Stream^ SourceStream {
			System::IO::Stream^ get() {
				return _sourceStream;
			}
			void set(System::IO::Stream^ value) {
				_sourceStream = value;
				OnPropertyChanged("SourceStream");
			}
		}
	};
	public ref class ImageOverlay :RecordingOverlayBase {
	private:
		String^ _sourcePath;
		System::IO::Stream^ _sourceStream;
	public:
		ImageOverlay() :RecordingOverlayBase() {

		}
		ImageOverlay(String^ path) :RecordingOverlayBase() {
			SourcePath = path;
		}
		ImageOverlay(System::IO::Stream^ stream) :RecordingOverlayBase() {
			SourceStream = stream;
		}
		ImageOverlay(ImageOverlay^ source) :RecordingOverlayBase(source) {
			SourcePath = source->SourcePath;
			SourceStream = source->SourceStream;
		}
		/// <summary>
		///The file path of the image to record
		/// </summary>
		property String^ SourcePath {
			String^ get() {
				return _sourcePath;
			}
			void set(String^ value) {
				_sourcePath = value;
				OnPropertyChanged("SourcePath");
			}
		}
		/// <summary>
		///The source stream of the image to record
		/// </summary>
		property System::IO::Stream^ SourceStream {
			System::IO::Stream^ get() {
				return _sourceStream;
			}
			void set(System::IO::Stream^ value) {
				_sourceStream = value;
				OnPropertyChanged("SourceStream");
			}
		}
	};
	public ref class DisplayOverlay :RecordingOverlayBase {
	private:
		bool _isCursorCaptureEnabled = true;
		String^ _deviceName;
	public:
		DisplayOverlay() :RecordingOverlayBase() {

		}
		DisplayOverlay(String^ deviceName) :RecordingOverlayBase() {
			DeviceName = deviceName;
		}
		DisplayOverlay(DisplayOverlay^ source) :RecordingOverlayBase(source) {
			DeviceName = source->DeviceName;
			IsCursorCaptureEnabled = source->IsCursorCaptureEnabled;
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
	public ref class WindowOverlay :RecordingOverlayBase {
	private:
		bool _isCursorCaptureEnabled = true;
		IntPtr _handle;
	public:
		WindowOverlay() :RecordingOverlayBase() {

		}
		WindowOverlay(IntPtr handle) :RecordingOverlayBase() {
			Handle = handle;
		}
		WindowOverlay(WindowOverlay^ source) :RecordingOverlayBase(source) {
			Handle = source->Handle;
			IsCursorCaptureEnabled = source->IsCursorCaptureEnabled;
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
	};
}