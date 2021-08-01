#pragma once
using namespace System;
using namespace System::Runtime::InteropServices;

namespace ScreenRecorderLib {
	public enum class H264BitrateControlMode {
		///<summary>Constant bitrate. Faster encoding than VBR, but produces larger files with consistent size. This setting might not work on software encoding. </summary>
		CBR = 0,
		///<summary>Default is unconstrained variable bitrate. Overall bitrate will average towards the Bitrate property, but can fluctuate greatly over and under it.</summary>
		UnconstrainedVBR = 2,
		///<summary>Quality-based VBR encoding. The encoder selects the bit rate to match a specified quality level. Set Quality level in VideoEncoderOptions from 1-100. Default is 70. </summary>
		Quality = 3
	};
	public enum class H265BitrateControlMode {
		///<summary>Constant bitrate. Faster encoding than VBR, but produces larger files with consistent size. This setting might not work on software encoding. </summary>
		CBR = 0,
		///<summary>Quality-based VBR encoding. The encoder selects the bit rate to match a specified quality level. Set Quality level in VideoEncoderOptions from 1-100. Default is 70. </summary>
		Quality = 3
	};

	public enum class H264Profile {
		Baseline = 66,
		Main = 77,
		High = 100
	};
	public enum class H265Profile {
		Main = 1,
	};

	public enum class VideoEncoderFormat {
		///<summary>H.264/AVC encoder. </summary>
		H264,
		///<summary>H.265/HEVC encoder. </summary>
		H265
	};

	public interface class IVideoEncoder {
	public:
		property VideoEncoderFormat EncodingFormat {
			VideoEncoderFormat get();
		}
		UInt32 GetEncoderProfile();
		UInt32 GetBitrateMode();
	};

	/// <summary>
	/// Encode video with H264 encoder.
	/// </summary>
	public ref class H264VideoEncoder : public IVideoEncoder {
	public:
		H264VideoEncoder() {
			EncoderProfile = H264Profile::High;
			BitrateMode = H264BitrateControlMode::Quality;
		}
		virtual property VideoEncoderFormat EncodingFormat {
			VideoEncoderFormat get() {
				return VideoEncoderFormat::H264;
			}
		}
		/// <summary>
		///The capabilities the h264 video encoder. 
		///Lesser profiles may increase playback compatibility and use less resources with older decoders and hardware at the cost of quality.
		/// </summary>
		property H264Profile EncoderProfile;
		/// <summary>
		///The bitrate control mode of the video encoder. Default is Quality.
		/// </summary>
		property H264BitrateControlMode BitrateMode;

		virtual UInt32 GetEncoderProfile() { return (UInt32)EncoderProfile; }
		virtual UInt32 GetBitrateMode() { return (UInt32)BitrateMode; }
	};

	/// <summary>
	/// Encode video with H265/HEVC encoder.
	/// </summary>
	public ref class H265VideoEncoder : public IVideoEncoder {
	public:
		H265VideoEncoder() {
			EncoderProfile = H265Profile::Main;
			BitrateMode = H265BitrateControlMode::Quality;
		}
		virtual property VideoEncoderFormat EncodingFormat {
			VideoEncoderFormat get() {
				return VideoEncoderFormat::H265;
			}
		}
		/// <summary>
		///The capabilities the h265 video encoder. At the moment only Main is supported
		/// </summary>
		property H265Profile EncoderProfile;
		/// <summary>
		///The bitrate control mode of the video encoder. Default is Quality.
		/// </summary>
		property H265BitrateControlMode BitrateMode;

		virtual UInt32 GetEncoderProfile() { return (UInt32)EncoderProfile; }
		virtual UInt32 GetBitrateMode() { return (UInt32)BitrateMode; }
	};
}