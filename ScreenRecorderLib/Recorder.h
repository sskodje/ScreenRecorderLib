#pragma warning(disable:4561)
#pragma once
#include <atlbase.h>
#include <vcclr.h>
#include <vector>
#include <set>
#include "../ScreenRecorderLibNative/Native.h"
#include "ManagedIStream.h"
#include "Win32WindowEnumeration.h"
#include "Coordinates.h"
#include "Options.h"
#include "Callback.h"
#include "AudioDevice.h"

using namespace System;
using namespace System::Runtime::InteropServices;
using namespace System::Collections::Generic;
using namespace System::ComponentModel;

delegate void InternalStatusCallbackDelegate(int status);
delegate void InternalCompletionCallbackDelegate(std::wstring path, nlohmann::fifo_map<std::wstring, int>);
delegate void InternalErrorCallbackDelegate(std::wstring path);
delegate void InternalSnapshotCallbackDelegate(std::wstring path);
delegate void InternalFrameNumberCallbackDelegate(int newFrameNumber);

namespace ScreenRecorderLib {

	public ref class SourceCoordinates {
	public:
		SourceCoordinates() { };
		SourceCoordinates(RecordingSourceBase^ source, ScreenRect^ coordinates) {
			Coordinates = coordinates;
			Source = source;
		}
		property ScreenRect^ Coordinates;
		property RecordingSourceBase^ Source;
	};

	public ref class OutputDimensions {
	public:
		property ScreenSize^ CombinedOutputSize;
		property List<SourceCoordinates^>^ OutputCoordinates;
	};

	public enum class AudioDeviceSource
	{
		OutputDevices,
		InputDevices,
		All
	};

	public ref class Recorder {
	private:
		Recorder(RecorderOptions^ options);
		~Recorder();
		!Recorder();
		int _currentFrameNumber;
		void CreateErrorCallback();
		void CreateCompletionCallback();
		void CreateStatusCallback();
		void CreateSnapshotCallback();
		void CreateFrameNumberCallback();
		void EventComplete(std::wstring str, nlohmann::fifo_map<std::wstring, int> delays);
		void EventFailed(std::wstring str);
		void EventStatusChanged(int status);
		void EventSnapshotCreated(std::wstring str);
		void FrameNumberChanged(int newFrameNumber);
		void SetupCallbacks();
		void ClearCallbacks();
		static HRESULT CreateNativeRecordingSource(_In_ RecordingSourceBase^ managedSource, _Out_ RECORDING_SOURCE* pNativeSource);
		static std::vector<RECORDING_SOURCE> CreateRecordingSourceList(IEnumerable<RecordingSourceBase^>^ options);
		static std::vector<RECORDING_OVERLAY> CreateOverlayList(IEnumerable<RecordingOverlayBase^>^ managedOverlays);

		RecorderStatus _status;
		RecordingManager* m_Rec;
		ManagedIStream* m_ManagedStream;
		GCHandle _statusChangedDelegateGcHandler;
		GCHandle _errorDelegateGcHandler;
		GCHandle _completedDelegateGcHandler;
		GCHandle _snapshotDelegateGcHandler;
		GCHandle _frameNumberDelegateGcHandler;
	public:
		property RecorderStatus Status {
			RecorderStatus get() {
				return _status;
			}
		private:
			void set(RecorderStatus value) {
				_status = value;
			}
		}

		property int CurrentFrameNumber {
			int get() {
				return _currentFrameNumber;
			}
		private:
			void set(int value) {
				_currentFrameNumber = value;
			}
		}
		void Record(System::String^ path);
		void Record(System::Runtime::InteropServices::ComTypes::IStream^ stream);
		void Record(System::IO::Stream^ stream);
		void Pause();
		void Resume();
		void Stop();
		void SetOptions(RecorderOptions^ options);
		/// <summary>
		/// This method can be used to set the recording source rectangle while a recording is in progress.
		/// </summary>
		/// <param name="rect"></param>
		void SetSourceRect(ScreenRect rect);
		/// <summary>
		/// This method can be used to set the audio input device (e.g. microphone) volume while a recording is in progress.
		/// </summary>
		/// <param name="volume"></param>
		void SetInputVolume(float volume);
		/// <summary>
		/// This method can be used to set the audio output device (i.e. system audio) volume while a recording is in progress.
		/// </summary>
		/// <param name="volume"></param>
		void SetOutputVolume(float volume);

		static bool SetExcludeFromCapture(System::IntPtr hwnd, bool isExcluded);
		static Recorder^ CreateRecorder();
		static Recorder^ CreateRecorder(RecorderOptions^ options);
		static List<RecordableWindow^>^ GetWindows();
		static List<AudioDevice^>^ GetSystemAudioDevices(AudioDeviceSource source);
		static List<RecordableCamera^>^ GetSystemVideoCaptureDevices();
		static List<RecordableDisplay^>^ GetDisplays();
		static OutputDimensions^ GetOutputDimensionsForRecordingSources(IEnumerable<RecordingSourceBase^>^ recordingSources);
		event EventHandler<RecordingCompleteEventArgs^>^ OnRecordingComplete;
		event EventHandler<RecordingFailedEventArgs^>^ OnRecordingFailed;
		event EventHandler<RecordingStatusEventArgs^>^ OnStatusChanged;
		event EventHandler<SnapshotSavedEventArgs^>^ OnSnapshotSaved;
		event EventHandler<FrameRecordedEventArgs ^> ^OnFrameRecorded;
	};
}