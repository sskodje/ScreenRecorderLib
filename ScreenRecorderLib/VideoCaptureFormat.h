#pragma once
namespace ScreenRecorderLib {
	public enum class VideoNominalRange {
		MFNominalRange_Unknown = 0,
		MFNominalRange_Normal = 1,
		MFNominalRange_Wide = 2,
		MFNominalRange_0_255 = 1,
		MFNominalRange_16_235 = 2,
		MFNominalRange_48_208 = 3,
		MFNominalRange_64_127 = 4
	};

	public enum class VideoInterlaceMode {
		MFVideoInterlace_Unknown = 0,
		MFVideoInterlace_Progressive = 2,
		MFVideoInterlace_FieldInterleavedUpperFirst = 3,
		MFVideoInterlace_FieldInterleavedLowerFirst = 4,
		MFVideoInterlace_FieldSingleUpper = 5,
		MFVideoInterlace_FieldSingleLower = 6,
		MFVideoInterlace_MixedInterlaceOrProgressive = 7
	};

	public enum VideoTransferMatrix {
		MFVideoTransferMatrix_Unknown = 0,
		MFVideoTransferMatrix_BT709 = 1,
		MFVideoTransferMatrix_BT601 = 2,
		MFVideoTransferMatrix_SMPTE240M = 3,
		MFVideoTransferMatrix_BT2020_10 = 4,
		MFVideoTransferMatrix_BT2020_12 = 5
	};

	public ref class VideoCaptureFormat {
	public:
		VideoCaptureFormat() {

		};
		property int Index;
		property Guid VideoFormat;
		property String^ VideoFormatName;
		property double Framerate;
		property ScreenSize^ FrameSize;
		property int AverageBitrate;
		property VideoTransferMatrix YUVMatrix;
		property VideoInterlaceMode InterlaceMode;
		property VideoNominalRange NominalRange;

		virtual String^ ToString() override {
			return String::Format("{0}:{1} {2}fps", FrameSize->Width, FrameSize->Height, Framerate);
		}
	private:

	};
}