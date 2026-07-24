#pragma once

using namespace System;
using namespace System::Collections::Generic;

namespace ScreenRecorderLib {
	public enum class RecorderStatus {
		Idle,
		Recording,
		Paused,
		Finishing
	};
	public ref class FrameData {
	public:
		property String^ Path;
		property int Delay;
		FrameData() {}
		FrameData(String^ path, int delay) :FrameData() {
			Path = path;
			Delay = delay;
		}
	};

	public ref class FrameBitmapData {
	public:
		property int Stride;
		property int Width;
		property int Height;
		property IntPtr Data;
		property int Length;
		FrameBitmapData() {}
		FrameBitmapData(int stride, byte* data, int length, int width, int height) :FrameBitmapData() {
			Stride = stride;
			Data = IntPtr(data);
			Length = length;
			Width = width;
			Height = height;
		}
	};

	public ref class AudioPacketSource {
	public:
		property String^ Id;
		property double Gain;
		property cli::array<byte>^ Data;
		AudioPacketSource() {}
		AudioPacketSource(String^ id, double gain, std::optional<std::vector<byte>> audioData) :AudioPacketSource() {
			Id = id;
			Gain = gain;
			if (audioData.has_value()) {
				auto vector = audioData.value();
				cli::array<byte>^ managedArray = gcnew cli::array<Byte>(static_cast<int>(vector.size()));
				if (!vector.empty()) {
					// Pin the managed array so the GC can't move/collect it mid-copy
					pin_ptr<Byte> pinned = &managedArray[0];
					memcpy(pinned, vector.data(), vector.size());
				}
				Data = managedArray;
			}
		}
	};

	public ref class AudioPacketData {
	public:
		property double Gain;
		property cli::array<byte>^ Data;
		property List<AudioPacketSource^>^ Sources;
		AudioPacketData() {
			Gain = 0;
			Sources = gcnew List< AudioPacketSource^>();
		}
		AudioPacketData(double gain, std::optional<std::vector<byte>> audioData) :AudioPacketData() {
			Gain = gain;
			if (audioData.has_value()) {
				auto vector = audioData.value();
				cli::array<byte>^ managedArray = gcnew cli::array<Byte>(static_cast<int>(vector.size()));
				if (!vector.empty()) {
					// Pin the managed array so the GC can't move/collect it mid-copy
					pin_ptr<Byte> pinned = &managedArray[0];
					memcpy(pinned, vector.data(), vector.size());
				}			
				Data = managedArray;
			}
		}
	};

	public ref class RecordingStatusEventArgs :System::EventArgs {
	public:
		property RecorderStatus Status;
		RecordingStatusEventArgs(RecorderStatus status) {
			Status = status;
		}
	};
	public ref class RecordingCompleteEventArgs :System::EventArgs {
	public:
		property String^ FilePath;
		property  List<FrameData^>^ FrameInfos;
		RecordingCompleteEventArgs(String^ path, List<FrameData^>^ frameInfos) {
			FilePath = path;
			FrameInfos = frameInfos;
		}
	};
	public ref class RecordingFailedEventArgs :System::EventArgs {
	public:
		property String^ Error;
		property String^ FilePath;
		RecordingFailedEventArgs(String^ error, String^ path) {
			Error = error;
			FilePath = path;
		}
	};

	public ref class SnapshotSavedEventArgs :System::EventArgs {
	public:
		property String^ SnapshotPath;
		SnapshotSavedEventArgs(String^ path) {
			SnapshotPath = path;
		}
	};
	public ref class FrameRecordedEventArgs :System::EventArgs {
	public:
		property int FrameNumber;
		property INT64 Timestamp;
		FrameBitmapData^ BitmapData;
		FrameRecordedEventArgs() {}
		FrameRecordedEventArgs(int frameNumber, INT64 timestamp) {
			FrameNumber = frameNumber;
			Timestamp = timestamp;
		}
		FrameRecordedEventArgs(int frameNumber, INT64 timestamp, FrameBitmapData^ bitmapData) {
			FrameNumber = frameNumber;
			Timestamp = timestamp;
			BitmapData = bitmapData;
		}
	};

	public ref class FrameDataRecordedEventArgs :System::EventArgs {
	public:
		property FrameBitmapData^ BitmapData;
		FrameDataRecordedEventArgs() {}
		FrameDataRecordedEventArgs(FrameBitmapData^ bitmapData)
		{
			this->BitmapData = bitmapData;
		}
		FrameDataRecordedEventArgs(int stride, byte* data, int length, int width, int height) {
			this->BitmapData = gcnew FrameBitmapData(stride, data, length, width, height);
		}
	};
	public ref class AudioDataRecordedEventArgs :System::EventArgs {
	public:
		property AudioPacketData^ AudioData;
		AudioDataRecordedEventArgs() {}
		AudioDataRecordedEventArgs(AudioPacketData^ audioData)
		{
			this->AudioData = audioData;
		}
	};
}