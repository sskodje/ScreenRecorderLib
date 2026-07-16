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

using namespace System;
using namespace System::Runtime::InteropServices;
using namespace System::Collections::Generic;
using namespace System::ComponentModel;

delegate void InternalStatusCallbackDelegate(int status);
delegate void InternalCompletionCallbackDelegate(std::wstring path, nlohmann::fifo_map<std::wstring, int>);
delegate void InternalErrorCallbackDelegate(std::wstring error, std::wstring path);
delegate void InternalSnapshotCallbackDelegate(std::wstring path);
delegate void InternalFrameNumberCallbackDelegate(int newFrameNumber, INT64 timestamp, FRAME_BITMAP_DATA* bitmapData);
delegate void InternalAudioDataCallbackDelegate(FRAME_AUDIO_INFO* audioData);
namespace ScreenRecorderLib {

	ref class DynamicOptionsBuilder;

	public ref class SourceCoordinates {
	public:
		SourceCoordinates() {};
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


	public ref class Recorder {
	private:
		Recorder(RecorderOptions^ options);
		~Recorder();
		!Recorder();
		void CreateErrorCallback();
		void CreateCompletionCallback();
		void CreateStatusCallback();
		void CreateSnapshotCallback();
		void CreateFrameNumberCallback();
		void CreateAudioDataCallback();
		void EventComplete(std::wstring path, nlohmann::fifo_map<std::wstring, int> delays);
		void EventFailed(std::wstring error, std::wstring path);
		void EventStatusChanged(int status);
		void EventSnapshotCreated(std::wstring str);
		void FrameNumberChanged(int newFrameNumber, INT64 timestamp, FRAME_BITMAP_DATA* bitmapData);
		void AudioDataChanged(FRAME_AUDIO_INFO* audioData);
		void SetupCallbacks();
		void ReleaseCallbacks();
		void ReleaseResources();
		static HRESULT CreateOrUpdateNativeAudioSource(_In_ AudioSourceBase^ managedSource, _Inout_ AUDIO_SOURCE* pNativeSource);
		static HRESULT CreateOrUpdateNativeRecordingSource(_In_ RecordingSourceBase^ managedSource, _Inout_ RECORDING_SOURCE* pNativeSource);
		static HRESULT CreateOrUpdateNativeRecordingOverlay(_In_ RecordingOverlayBase^ managedOverlay, _Inout_ RECORDING_OVERLAY* pNativeOverlay);
		static List<VideoCaptureFormat^>^ CreateVideoCaptureFormatList(_In_ std::vector< IMFMediaType*> mediaTypes);
		static std::vector<RECORDING_SOURCE> CreateRecordingSourceList(_In_ IEnumerable<RecordingSourceBase^>^ managedSources);
		static std::vector<RECORDING_OVERLAY> CreateOverlayList(_In_ IEnumerable<RecordingOverlayBase^>^ managedOverlays);
		static std::vector<AUDIO_SOURCE> CreateAudioSourceList(_In_ IEnumerable<AudioSourceBase^>^ managedSources);
		static Guid FromNativeGuid(_In_ const GUID& guid);

		int _currentFrameNumber;
		RecorderStatus _status;
		RecordingManager* m_Rec;
		ManagedIStream* m_ManagedStream;
		GCHandle _statusChangedDelegateGcHandler;
		GCHandle _errorDelegateGcHandler;
		GCHandle _completedDelegateGcHandler;
		GCHandle _snapshotDelegateGcHandler;
		GCHandle _frameNumberDelegateGcHandler;
		GCHandle _audioDataDelegateGcHandler;

	internal:
		void SetDynamicOptions(DynamicOptions^ options);

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
		bool TakeSnapshot();
		bool TakeSnapshot(System::String^ path);
		bool TakeSnapshot(System::IO::Stream^ stream);
		void Pause();
		void Resume();
		void Stop();
		void SetOptions(RecorderOptions^ options);
		/// <summary>
		/// DynamicOptionsBuilder can be used to update a subset of options while a recording is in progress.
		/// </summary>
		/// <returns></returns>
		DynamicOptionsBuilder^ GetDynamicOptionsBuilder();

		static bool SetExcludeFromCapture(System::IntPtr hwnd, bool isExcluded);
		static Recorder^ CreateRecorder();
		static Recorder^ CreateRecorder(RecorderOptions^ options);
		static List<RecordableWindow^>^ GetWindows();
		static List<RecordableAudioCaptureDevice^>^ Recorder::GetSystemAudioCaptureDevices();
		static List<RecordableAudioLoopbackDevice^>^ Recorder::GetSystemAudioLoopbackDevices();
		static List<RecordableCamera^>^ GetSystemVideoCaptureDevices();
		static List<RecordableDisplay^>^ GetDisplays();
		static OutputDimensions^ GetOutputDimensionsForRecordingSources(IEnumerable<RecordingSourceBase^>^ recordingSources);
		static List<VideoCaptureFormat^>^ GetSupportedVideoCaptureFormatsForDevice(String^ DevicePath);
		event EventHandler<RecordingCompleteEventArgs^>^ OnRecordingComplete;
		event EventHandler<RecordingFailedEventArgs^>^ OnRecordingFailed;
		event EventHandler<RecordingStatusEventArgs^>^ OnStatusChanged;
		event EventHandler<SnapshotSavedEventArgs^>^ OnSnapshotSaved;
		event EventHandler<FrameRecordedEventArgs^>^ OnFrameRecorded;
		event EventHandler<AudioDataRecordedEventArgs^>^ OnAudioPacketRecorded;
	};

	public ref class DynamicOptionsBuilder {
	public:
		DynamicOptionsBuilder^ SetDynamicAudioOptions(DynamicAudioOptions^ options) {
			_options->AudioOptions = options;
			return this;
		}

		DynamicOptionsBuilder^ SetDynamicMouseOptions(DynamicMouseOptions^ options) {
			_options->MouseOptions = options;
			return this;
		}
		DynamicOptionsBuilder^ SetDynamicOutputOptions(DynamicOutputOptions^ options) {
			_options->OutputOptions = options;
			return this;
		}

		/// <summary>
		/// Update properties for the given overlay
		/// </summary>
		/// <param name="overlay">The overlay to update. It must have the same ID as an existing overlay</param>
		/// <returns></returns>
		DynamicOptionsBuilder^ SetUpdatedOverlay(RecordingOverlayBase^ overlay) {
			if (!_options->RecordingOverlays) {
				_options->RecordingOverlays = gcnew List<RecordingOverlayBase^>();
			}
			else if (_options->RecordingOverlays->Contains(overlay)) {
				_options->RecordingOverlays->Remove(overlay);
			}
			_options->RecordingOverlays->Add(overlay);

			return this;
		}


		/// <summary>
		/// Update properties for the given recording source
		/// </summary>
		/// <param name="source">The recording source to update. It must have the same ID as an existing source</param>
		/// <returns></returns>
		DynamicOptionsBuilder^ SetUpdatedRecordingSource(RecordingSourceBase^ source) {
			if (!_options->RecordingSources) {
				_options->RecordingSources = gcnew List<RecordingSourceBase^>();
			}
			else if (_options->RecordingSources->Contains(source)) {
				_options->RecordingSources->Remove(source);
			}
			_options->RecordingSources->Add(source);

			return this;
		}

		/// <summary>
		/// Update properties for the given audio source
		/// </summary>
		/// <param name="source">The audio source to update. It must have the same ID as an existing source</param>
		/// <returns></returns>
		DynamicOptionsBuilder^ SetUpdatedAudioSource(AudioSourceBase^ source) {
			if (!_options->AudioOptions) {
				_options->AudioOptions = gcnew DynamicAudioOptions();
			}
			if (!_options->AudioOptions->AudioSources) {
				_options->AudioOptions->AudioSources = gcnew List<AudioSourceBase^>();
			}
			else if (_options->AudioOptions->AudioSources->Contains(source)) {
				_options->AudioOptions->AudioSources->Remove(source);
			}
			_options->AudioOptions->AudioSources->Add(source);

			return this;
		}

		/// <summary>
		/// Apply the changes to the current active recording. Fails if no recording is in progress.
		/// </summary>
		/// <returns>True if successfully applied changes to a recording in progress, else false</returns>
		bool Apply() {
			if (_rec && _rec->Status == RecorderStatus::Recording) {
				_rec->SetDynamicOptions(_options);
				return true;
			}
			else {
				return false;
			}
		}

	internal:
		DynamicOptionsBuilder(Recorder^ recorder) {
			_options = gcnew DynamicOptions();
			_rec = recorder;
		}
	private:
		DynamicOptions^ _options;
		Recorder^ _rec;
	};
}