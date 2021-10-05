#pragma once
#include "../ScreenRecorderLibNative/Native.h"
using namespace System;

namespace ScreenRecorderLib {
	public enum class Anchor {
		TopLeft,
		TopRight,
		BottomLeft,
		BottomRight
	};

	public ref class RecordingOverlayBase abstract : public INotifyPropertyChanged {
	private:
		ScreenSize^ _size;
		ScreenSize^ _offset;
		Anchor _anchorPosition;
		String^ _id;
	internal:
		RecordingOverlayBase()
		{
			ID = Guid::NewGuid().ToString();
			AnchorPosition = Anchor::TopLeft;
			Offset = nullptr;
			Size = nullptr;
		}
		RecordingOverlayBase(RecordingOverlayBase^ base) :RecordingOverlayBase() {
			ID = base->ID;
			Offset = base->Offset;
			Size = base->Size;
			AnchorPosition = base->AnchorPosition;
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

		virtual property Anchor AnchorPosition {
			Anchor get() {
				return _anchorPosition;
			}
			void set(Anchor anchor) {
				_anchorPosition = anchor;
				OnPropertyChanged("AnchorPosition");
			}
		}

		/// <summary>
		/// This option can be configured to set the size of this overlay in pixels.
		/// </summary>
		virtual property ScreenSize^ Size {
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
		virtual property ScreenSize^ Offset {
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
		property String^ SourcePath;
	};
	public ref class ImageOverlay :RecordingOverlayBase {
	public:
		ImageOverlay() {

		}
		ImageOverlay(String^ path) {
			SourcePath = path;
		}
		property String^ SourcePath;
	};
	public ref class DisplayOverlay :RecordingOverlayBase {
	public:
		DisplayOverlay() {

		}
		DisplayOverlay(String^ deviceName) {
			DeviceName = deviceName;
		}
		property String^ DeviceName;
	};
	public ref class WindowOverlay :RecordingOverlayBase {
	public:
		WindowOverlay() {

		}
		WindowOverlay(IntPtr handle) {
			Handle = handle;
		}
		property IntPtr Handle;
	};
}