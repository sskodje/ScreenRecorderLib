#pragma once
using namespace System;
using namespace System::ComponentModel;
using namespace System::Collections::Generic;

namespace ScreenRecorderLib {
	public ref class AudioSourceBase abstract : public INotifyPropertyChanged {
	private:
		String^ _id;
		float _volume;
		bool _isAudioCaptureEnabled;

	internal:
		AudioSourceBase() {
			ID = Guid::NewGuid().ToString();
			Volume = 1.0f;
			IsAudioCaptureEnabled = true;
		}
		AudioSourceBase(AudioSourceBase^ base) :AudioSourceBase() {
			ID = base->ID;
			Volume = base->Volume;
			IsAudioCaptureEnabled = base->IsAudioCaptureEnabled;
		}
		~AudioSourceBase() {

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
		property bool IsAudioCaptureEnabled {
			bool get() {
				return _isAudioCaptureEnabled;
			}
			void set(bool value) {
				if (_isAudioCaptureEnabled != value) {
					_isAudioCaptureEnabled = value;
					OnPropertyChanged("IsAudioCaptureEnabled");
				}
			}
		}
		property float Volume {
			float get() {
				return _volume;
			}
			void set(float value) {
				if (_volume != value) {
					_volume = value;
					OnPropertyChanged("Volume");
				}
			}
		}
		void OnPropertyChanged(String^ info)
		{
			PropertyChanged(this, gcnew PropertyChangedEventArgs(info));
		}
	};

	public ref class CaptureAudioSource : public AudioSourceBase {
	private:
		int _inputMasterChannel;
		bool _forceInputDeviceMono;
	public:

		property String^ DeviceName;

		/// <summary>
		/// Returns an audio source for the main audio capture device. If no audio capture device is available, it returns NULL.
		/// </summary>
		static property CaptureAudioSource^ Default {
			CaptureAudioSource^ get() {
				CaptureAudioSource^ source = gcnew CaptureAudioSource();
				CComPtr<IMMDevice> pDevice;
				HRESULT hr = GetDefaultAudioDevice(EDataFlow::eCapture, &pDevice);
				if (SUCCEEDED(hr)) {
					LPWSTR pwszID = NULL;
					pDevice->GetId(&pwszID);
					CoTaskMemFreeOnExit releasePwszID(pwszID);
					source->DeviceName = gcnew String(pwszID);
					return source;
				}
				else {
					return nullptr;
				}
			}
		}

		/// <summary>
		/// The channel to use as source when downmixing audio input to mono.
		/// 0 (default) is the left channel, 2 is the right, etc.
		/// This is used in conjunction with the ForceInputDeviceMono property.
		/// </summary>
		property int InputDeviceMasterChannel {
			int get() {
				return _inputMasterChannel;
			}
			void set(int value) {
				_inputMasterChannel = value;
				OnPropertyChanged("InputDeviceMasterChannel");
			}
		}
		/// <summary>
		/// Uses only the source audio channel selected with the InputDeviceMasterChannel property,
		/// and copies that to all channels when encoding. 
		/// Used to fix issues with some microphones outputing a stereo signal, but only having sound on one of the channels.
		/// </summary>
		property bool ForceMono {
			bool get() {
				return _forceInputDeviceMono;
			}
			void set(bool value) {
				_forceInputDeviceMono = value;
				OnPropertyChanged("ForceMono");
			}
		}

		CaptureAudioSource()
		{
			_inputMasterChannel = 0;
			_forceInputDeviceMono = false;
		}
		CaptureAudioSource(String^ deviceName) :CaptureAudioSource() {
			DeviceName = deviceName;
		}
		CaptureAudioSource(CaptureAudioSource^ source) :AudioSourceBase(source) {
			DeviceName = source->DeviceName;
		}
	};

	public ref class LoopbackAudioSource : public AudioSourceBase {
	public:

		property String^ DeviceName;

		/// <summary>
		/// Returns an audio source for the main audio output. If no audio output is available, it returns NULL.
		/// </summary>
		static property LoopbackAudioSource^ Default {
			LoopbackAudioSource^ get() {
				LoopbackAudioSource^ source = gcnew LoopbackAudioSource();
				CComPtr<IMMDevice> pDevice;
				HRESULT hr = GetDefaultAudioDevice(EDataFlow::eRender, &pDevice);
				if (SUCCEEDED(hr)) {
					LPWSTR pwszID = NULL;
					pDevice->GetId(&pwszID);
					CoTaskMemFreeOnExit releasePwszID(pwszID);
					source->DeviceName = gcnew String(pwszID);
					return source;
				}
				else {
					return nullptr;
				}
			}
		}

		LoopbackAudioSource()
		{

		}
		LoopbackAudioSource(String^ deviceName) :LoopbackAudioSource() {
			DeviceName = deviceName;
		}
		LoopbackAudioSource(LoopbackAudioSource^ source) :AudioSourceBase(source) {
			DeviceName = source->DeviceName;
		}
	};
	public ref class ProcessAudioSource : public AudioSourceBase {
	public:

		property int ProcessId;

		ProcessAudioSource()
		{

		}
		ProcessAudioSource(int pid) :ProcessAudioSource() {
			ProcessId = pid;
		}
		ProcessAudioSource(ProcessAudioSource^ source) :AudioSourceBase(source) {
			ProcessId = source->ProcessId;
		}
	};

	public ref class RecordableAudioProcess : ProcessAudioSource {
	public:
		RecordableAudioProcess() :ProcessAudioSource() {
		}
		RecordableAudioProcess(String^ friendlyName, int pid) :RecordableAudioProcess() {
			FriendlyName = friendlyName;
			this->ProcessId = pid;
		}
		property String^ FriendlyName;

	};
	public ref class RecordableAudioCaptureDevice : CaptureAudioSource {
	public:
		RecordableAudioCaptureDevice() :CaptureAudioSource() {
		}
		RecordableAudioCaptureDevice(String^ friendlyName, String^ deviceName) :RecordableAudioCaptureDevice() {
			FriendlyName = friendlyName;
			DeviceName = deviceName;
		}
		RecordableAudioCaptureDevice(String^ friendlyName, String^ deviceName, bool isDefaultDevice) :RecordableAudioCaptureDevice(friendlyName, deviceName) {
			IsDefaultDevice = isDefaultDevice;
		}
		property String^ FriendlyName;
		property bool IsDefaultDevice;

	};
	public ref class RecordableAudioLoopbackDevice : LoopbackAudioSource {
	public:
		RecordableAudioLoopbackDevice() :LoopbackAudioSource() {

		}
		RecordableAudioLoopbackDevice(String^ friendlyName, String^ deviceName) :RecordableAudioLoopbackDevice() {
			FriendlyName = friendlyName;
			DeviceName = deviceName;
		}
		RecordableAudioLoopbackDevice(String^ friendlyName, String^ deviceName, bool isDefaultDevice) :RecordableAudioLoopbackDevice(friendlyName, deviceName) {
			IsDefaultDevice = isDefaultDevice;
		}
		property String^ FriendlyName;
		property bool IsDefaultDevice;
	};
}