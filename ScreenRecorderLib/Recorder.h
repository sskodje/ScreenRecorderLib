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
delegate void InternalErrorCallbackDelegate(std::wstring error, std::wstring path);
delegate void InternalSnapshotCallbackDelegate(std::wstring path);
delegate void InternalFrameNumberCallbackDelegate(int newFrameNumber);

namespace ScreenRecorderLib {

	ref class DynamicOptionsBuilder;

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
		void CreateErrorCallback();
		void CreateCompletionCallback();
		void CreateStatusCallback();
		void CreateSnapshotCallback();
		void CreateFrameNumberCallback();
		void EventComplete(std::wstring path, nlohmann::fifo_map<std::wstring, int> delays);
		void EventFailed(std::wstring error, std::wstring path);
		void EventStatusChanged(int status);
		void EventSnapshotCreated(std::wstring str);
		void FrameNumberChanged(int newFrameNumber);
		void SetupCallbacks();
		void ClearCallbacks();
		static HRESULT CreateNativeRecordingSource(_In_ RecordingSourceBase^ managedSource, _Out_ RECORDING_SOURCE* pNativeSource);
		static HRESULT CreateNativeRecordingOverlay(_In_ RecordingOverlayBase^ managedOverlay, _Out_ RECORDING_OVERLAY* pNativeOverlay);
		static std::vector<RECORDING_SOURCE> CreateRecordingSourceList(IEnumerable<RecordingSourceBase^>^ options);
		static std::vector<RECORDING_OVERLAY> CreateOverlayList(IEnumerable<RecordingOverlayBase^>^ managedOverlays);

		int _currentFrameNumber;
		RecorderStatus _status;
		RecordingManager* m_Rec;
		ManagedIStream* m_ManagedStream;
		GCHandle _statusChangedDelegateGcHandler;
		GCHandle _errorDelegateGcHandler;
		GCHandle _completedDelegateGcHandler;
		GCHandle _snapshotDelegateGcHandler;
		GCHandle _frameNumberDelegateGcHandler;

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
		static List<AudioDevice^>^ GetSystemAudioDevices(AudioDeviceSource source);
		static List<RecordableCamera^>^ GetSystemVideoCaptureDevices();
		static List<RecordableDisplay^>^ GetDisplays();
		static OutputDimensions^ GetOutputDimensionsForRecordingSources(IEnumerable<RecordingSourceBase^>^ recordingSources);
		event EventHandler<RecordingCompleteEventArgs^>^ OnRecordingComplete;
		event EventHandler<RecordingFailedEventArgs^>^ OnRecordingFailed;
		event EventHandler<RecordingStatusEventArgs^>^ OnStatusChanged;
		event EventHandler<SnapshotSavedEventArgs^>^ OnSnapshotSaved;
		event EventHandler<FrameRecordedEventArgs^>^ OnFrameRecorded;
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
		/// Set the source rect (crop) for a recording source with the given ID.
		/// </summary>
		/// <param name="recordingSourceID">ID for a recording source in progress</param>
		/// <param name="sourceRect"></param>
		/// <returns></returns>
		DynamicOptionsBuilder^ SetSourceRectForRecordingSource(String^ recordingSourceID, ScreenRect^ sourceRect) {
			if (!_options->SourceRects) {
				_options->SourceRects = gcnew Dictionary<String^, ScreenRect^>();
			}
			_options->SourceRects[recordingSourceID] = sourceRect;
			return this;
		}
		/// <summary>
		/// Enable or disable mouse cursor capture for the recording source with the given ID.
		/// </summary>
		/// <param name="recordingSourceID">ID for a recording source in progress</param>
		/// <param name="isCursorCaptureEnabled"></param>
		/// <returns></returns>
		DynamicOptionsBuilder^ SetCursorCaptureForRecordingSource(String^ recordingSourceID, bool isCursorCaptureEnabled) {
			if (!_options->SourceCursorCaptures) {
				_options->SourceCursorCaptures = gcnew Dictionary<String^, bool>();
			}
			_options->SourceCursorCaptures[recordingSourceID] = isCursorCaptureEnabled;
			return this;
		}
		/// <summary>
		/// Enable or disable mouse cursor capture for the recording overlay with the given ID.
		/// </summary>
		/// <param name="recordingSourceID">ID for a recording source in progress</param>
		/// <param name="isCursorCaptureEnabled"></param>
		/// <returns></returns>
		DynamicOptionsBuilder^ SetCursorCaptureForOverlay(String^ recordingSourceID, bool isCursorCaptureEnabled) {
			if (!_options->OverlayCursorCaptures) {
				_options->OverlayCursorCaptures = gcnew Dictionary<String^, bool>();
			}
			_options->OverlayCursorCaptures[recordingSourceID] = isCursorCaptureEnabled;
			return this;
		}
		/// <summary>
		/// Set the size of the overlay with the given ID.
		/// </summary>
		/// <param name="overlayID">ID for a recording source in progress</param>
		/// <param name="size">The size of the overlay in pixels</param>
		/// <returns></returns>
		DynamicOptionsBuilder^ SetSizeForOverlay(String^ overlayID, ScreenSize^ size) {
			if (!_options->OverlaySizes) {
				_options->OverlaySizes = gcnew Dictionary<String^, ScreenSize^>();
			}
			_options->OverlaySizes[overlayID] = size;
			return this;
		}
		/// <summary>
		/// Set the position offset of the overlay with the given ID.
		/// </summary>
		/// <param name="overlayID">ID for an overlay in progress</param>
		/// <param name="offset">The offset for the overlay, relative to the configured Anchor.</param>
		/// <returns></returns>
		DynamicOptionsBuilder^ SetOffsetForOverlay(String^ overlayID, ScreenSize^ offset) {
			if (!_options->OverlayOffsets) {
				_options->OverlayOffsets = gcnew Dictionary<String^, ScreenSize^>();
			}
			_options->OverlayOffsets[overlayID] = offset;
			return this;
		}
		/// <summary>
		/// Set the position anchor for the overlay with the given ID.
		/// </summary>
		/// <param name="overlayID">ID for an overlay in progress</param>
		/// <param name="anchor">Where to anchor the overlay</param>
		/// <returns></returns>
		DynamicOptionsBuilder^ SetAnchorForOverlay(String^ overlayID, Anchor anchor) {
			if (!_options->OverlayAnchors) {
				_options->OverlayAnchors = gcnew Dictionary<String^, Anchor>();
			}
			_options->OverlayAnchors[overlayID] = anchor;
			return this;
		}
		/// <summary>
		/// Configure if video capture is enabled for the recording source with the given ID.
		/// </summary>
		/// <param name="recordingSourceID">ID for a recording source in progress</param>
		/// <param name="isCaptureEnabled">If false, the source will be blacked out for the duration.</param>
		/// <returns></returns>
		DynamicOptionsBuilder^ SetVideoCaptureEnabledForRecordingSource(String^ recordingSourceID, bool isCaptureEnabled) {
			if (!_options->SourceVideoCaptures) {
				_options->SourceVideoCaptures = gcnew Dictionary<String^, bool>();
			}
			_options->SourceVideoCaptures[recordingSourceID] = isCaptureEnabled;
			return this;
		}
		/// <summary>
		/// Configure if video capture is enabled for the overlay with the given ID.
		/// </summary>
		/// <param name="overlayID">ID for a recording source in progress</param>
		/// <param name="isCaptureEnabled">If false, the overlay will be blacked out for the duration.</param>
		/// <returns></returns>
		DynamicOptionsBuilder^ SetVideoCaptureEnabledForOverlay(String^ overlayID, bool isCaptureEnabled) {
			if (!_options->OverlayVideoCaptures) {
				_options->OverlayVideoCaptures = gcnew Dictionary<String^, bool>();
			}
			_options->OverlayVideoCaptures[overlayID] = isCaptureEnabled;
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