#pragma once
using namespace System;
namespace ScreenRecorderLib {
	public ref class AudioDevice {
	public:
		AudioDevice() {};
		AudioDevice(String^ deviceName, String^ friendlyName) {
			DeviceName = deviceName;
			FriendlyName = friendlyName;
		}
		virtual property String^ DeviceName;
		virtual property String^ FriendlyName;
	};
}