#pragma once
#include "../ScreenRecorderLibNative/Native.h"
#include "Coordinates.h"

using namespace System;
using namespace System::ComponentModel;

namespace ScreenRecorderLib {

	public ref class RecordingSourceBase abstract : public INotifyPropertyChanged {
	private:
		ScreenSize^ _outputSize;
		ScreenPoint^ _position;
		bool _isCursorCaptureEnabled;
	internal:
		RecordingSourceBase() {
			IsCursorCaptureEnabled = true;
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

		void OnPropertyChanged(String^ info)
		{
			PropertyChanged(this, gcnew PropertyChangedEventArgs(info));
		}
	};

	public ref class WindowRecordingSource : public RecordingSourceBase {
	private:
		ScreenSize^ _outputSize;
		ScreenPoint^ _position;
	public:
		/// <summary>
		/// The handle to the window to record.
		/// </summary>
		property IntPtr Handle;

		WindowRecordingSource() {	}
		WindowRecordingSource(IntPtr windowHandle) {
			Handle = windowHandle;
		}
	};

	public ref class DisplayRecordingSource : public RecordingSourceBase {
	private:
		ScreenRect^ _sourceRect;
		ScreenSize^ _outputSize;
		ScreenPoint^ _position;
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

		DisplayRecordingSource() {	}
		DisplayRecordingSource(String^ deviceName) {
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
		/// <summary>
		/// Retrieves the dimensions of the bounding rectangle of the specified window. The dimensions are given in screen coordinates that are relative to the upper-left corner of the screen.
		/// </summary>
		/// <returns></returns>
		ScreenRect^ GetScreenCoordinates() {
			if (Handle != IntPtr::Zero) {
				HWND hwnd = (HWND)Handle.ToPointer();
				if (IsWindow(hwnd)) {
					RECT rect;
					DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &rect, sizeof(rect));
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
		/// <summary>
		/// The bounds of the output in desktop coordinates. Desktop coordinates depend on the dots per inch (DPI) of the desktop.
		/// </summary>
		/// <returns></returns>
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
}