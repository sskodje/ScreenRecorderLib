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

	public ref class RecordingOverlayBase abstract {
	internal:
		RecordingOverlayBase() {
			AnchorPosition = Anchor::TopLeft;
		}
	public:
		virtual property Anchor AnchorPosition;
		virtual property int OffsetX;
		virtual property int OffsetY;
		virtual property int Width;
		virtual property int Height;
	};
	public ref class CameraCaptureOverlay :RecordingOverlayBase {
	public:
		property String^ CaptureDeviceName;
	};
	public ref class VideoOverlay :RecordingOverlayBase {
	public:
		property String^ FilePath;
	};
	public ref class PictureOverlay :RecordingOverlayBase {
	public:
		property String^ FilePath;
	};
}