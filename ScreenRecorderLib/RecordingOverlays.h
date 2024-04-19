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
	public:
		VideoCaptureOverlay() {

		}
		VideoCaptureOverlay(String^ deviceName) {
			DeviceName = deviceName;
		}
		property String^ DeviceName;
	};
	public ref class VideoOverlay :RecordingOverlayBase {
	public:
		VideoOverlay() {

		}
		VideoOverlay(String^ path) {
			SourcePath = path;
		}
		VideoOverlay(System::IO::Stream^ stream) {
			SourceStream = stream;
		}
		property String^ SourcePath;
		property System::IO::Stream^ SourceStream;
	};
	public ref class ImageOverlay :RecordingOverlayBase {
	public:
		ImageOverlay() {

		}
		ImageOverlay(String^ path) {
			SourcePath = path;
		}
		ImageOverlay(System::IO::Stream^ stream) {
			SourceStream = stream;
		}
		property String^ SourcePath;
		property System::IO::Stream^ SourceStream;
	};
	public ref class DisplayOverlay :RecordingOverlayBase {
	private:
		bool _isCursorCaptureEnabled = true;
	public:
		DisplayOverlay() {

		}
		DisplayOverlay(String^ deviceName) {
			DeviceName = deviceName;
		}
		property String^ DeviceName;
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
	public:
		WindowOverlay() {

		}
		WindowOverlay(IntPtr handle) {
			Handle = handle;
		}
		property IntPtr Handle;
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