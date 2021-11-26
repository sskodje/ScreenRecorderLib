# ScreenRecorderLib
A .NET library for screen recording in Windows, using native Microsoft Media Foundation for realtime encoding to h264 video or PNG images. This library requires Windows 8 or higher to function, as well as [Visual C++ Redistributable 2015](https://www.microsoft.com/en-us/download/details.aspx?id=48145) installed. This library also requires Media Foundation to work, which have to be installed from Server Manager if run on Windows Server, or from the respective "Media Feature Pack" if run on a Windows N or KN version.

Available on [NuGet](https://www.nuget.org/packages/ScreenRecorderLib/).

**Breaking changes in version 2.0.0:**

Audio device is now selected by ID instead of name. Recorder::GetSystemAudioDevices now returns a dictionary of ID and name.

**Breaking changes in version 1.2.0:**

IsMouseClicksDetected and IsMousePointerEnabled is moved to the new MouseOptions, along with several new mouse related properties.

**Breaking changes in version 1.1.3:**

The option to select monitor by index is removed. Monitor is now selected by Device Name, e.g. \\.\DISPLAY1

**Basic usage:**

This will start a video recording using the default settings:
* "Quality" encoding mode with quality 70.
* H.264 video encoder with baseline profile.
* 30fps
* no audio
* mouse pointer enabled

```csharp
        using ScreenRecorderLib;
        
        Recorder _rec;
        Stream _outStream;
        void CreateRecording()
        {
            string videoPath = Path.Combine(Path.GetTempPath(), "test.mp4");
            _rec = Recorder.CreateRecorder();
            _rec.OnRecordingComplete += Rec_OnRecordingComplete;
            _rec.OnRecordingFailed += Rec_OnRecordingFailed;
            _rec.OnStatusChanged += Rec_OnStatusChanged;
	    //Record to a file
	    string videoPath = Path.Combine(Path.GetTempPath(), "test.mp4");
            _rec.Record(videoPath);
	    //..Or to a stream
	    //_outStream = new MemoryStream();
            //_rec.Record(_outStream);
        }
        void EndRecording()
        {
            _rec.Stop(); 
        }
        private void Rec_OnRecordingComplete(object sender, RecordingCompleteEventArgs e)
        {
	    //Get the file path if recorded to a file
            string path = e.FilePath;	
            //or do something with your stream
	    //... something ...
	    _outStream?.Dispose();
        }
        private void Rec_OnRecordingFailed(object sender, RecordingFailedEventArgs e)
        {
            string error = e.Error;
            _outStream?.Dispose()
        }
        private void Rec_OnStatusChanged(object sender, RecordingStatusEventArgs e)
        {
            RecorderStatus status = e.Status;
        }
```

To change the options, pass a RecorderOptions when creating the Recorder:

```csharp
            RecorderOptions options = new RecorderOptions
            {
                RecorderMode = RecorderMode.Video,
                //If throttling is disabled, out of memory exceptions may eventually crash the program,
                //depending on encoder settings and system specifications.
                IsThrottlingDisabled = false,
		//Hardware encoding is enabled by default.
                IsHardwareEncodingEnabled = true,
		//Low latency mode provides faster encoding, but can reduce quality.
                IsLowLatencyEnabled = false,
		//Fast start writes the mp4 header at the beginning of the file, to facilitate streaming.
                IsMp4FastStartEnabled = false,		
                AudioOptions = new AudioOptions
                {
                    Bitrate = AudioBitrate.bitrate_128kbps,
                    Channels = AudioChannels.Stereo,
                    IsAudioEnabled = true
                },
                VideoOptions = new VideoOptions
                {
		    BitrateMode = BitrateControlMode.UnconstrainedVBR,
                    Bitrate = 8000 * 1000,
                    Framerate = 60,
                    IsFixedFramerate = true,
                    EncoderProfile = H264Profile.Main
                },
		MouseOptions = new MouseOptions
                {
		    //Displays a colored dot under the mouse cursor when the left mouse button is pressed.	
                    IsMouseClicksDetected = true,
                    MouseClickDetectionColor = "#FFFF00",
		    MouseRightClickDetectionColor = "#FFFF00",
                    MouseClickDetectionRadius = 30,
                    MouseClickDetectionDuration = 100
		    IsMousePointerEnabled = true,
                    /* Polling checks every millisecond if a mouse button is pressed.
                       Hook works better with programmatically generated mouse clicks, but may affect
                       mouse performance and interferes with debugging.*/
		    MouseClickDetectionMode = MouseDetectionMode.Hook
                }
            };
            _rec = Recorder.CreateRecorder(options);
```
To select audio device to record, query from Recorder and use as input in AudioOptions:
```csharp
            List<string> inputDevices = Recorder.GetSystemAudioDevices(AudioDeviceSource.InputDevices);
	    List<string> outputDevices = Recorder.GetSystemAudioDevices(AudioDeviceSource.OutputDevices);
	    string selectedOutputDevice = //select one of the devices.. Passing empty string or null uses system default playback device.
	    string selectedInputDevice = //select one of the devices.. Passing empty string or null uses system default recording device.
	    AudioOptions = new AudioOptions
            {
                 IsAudioEnabled = true,
		 IsOutputDeviceEnabled = true,
                 IsInputDeviceEnabled = true,
                 AudioOutputDevice = selectedOutputDevice,
                 AudioInputDevice = selectedInputDevice
            }	   	    
```

If recording both an input device like a microphone and and the system output audio, the gain should be set using the volume options. By default, both sources are mixed together at 100%, which can lead to the audio clipping depending on the loudness of the source:
```csharp
	    //Here both sources are set to 50% gain, which will result in a quieter recording but with guaranteed no clipping.
	    //You can configure this how you want for both sources, from 0.0 to 1.0 (or above).
	    AudioOptions = new AudioOptions
            {
		InputVolume = 0.5,
		OutputVolume = 0.5
            }	   	    
```

To record a single window, you can set the RecorderApi in RecorderOptions to WindowsGraphicsCapture and pass a HWND to the window you wish to record. For convenience, if you want to list all recordable windows, you can get them with the static method Recorder.GetWindows():
```csharp
            List<RecordableWindow> windows = Recorder.GetWindows();
            RecorderOptions options = new RecorderOptions
            {
               DisplayOptions = new DisplayOptions
	       {
		   WindowHandle = windows[0].Handle
	       },
	       RecorderApi = WindowsGraphicsCapture
            }
```


To select a monitor to record, you can pass the device name of the monitor. This works for both DesktopDuplication and WindowsGraphicsCapture API:
```csharp
	    //DeviceName in the form \\.\DISPLAY1. Typically you would enumerate system monitors and select one. 
	    //Default monitor is used if no valid input is given.
            string monitorDeviceName= System.Windows.Forms.Screen.PrimaryScreen.DeviceName; 
            RecorderOptions options = new RecorderOptions
            {
                DisplayOptions = new DisplayOptions
                {
                    MonitorDeviceName = monitorDeviceName
                },
            }
```

To only record a fixed portion of the screen, you can also set cropping in DisplayOptions. Note that cropping is only supported using DesktopDuplication API:
```csharp
            //crop to a 400x400px square at x=400,y=400. Passing 0 for these values will default to full screen recording.
            int left = 400;
            int top = 400;
            int right = 800;
            int bottom=800;
            RecorderOptions options = new RecorderOptions
            {
                DisplayOptions = new DisplayOptions
                {
                    Left = left,
                    Top = top,
                    Right = right,
                    Bottom = bottom
                },
	       RecorderApi =DesktopDuplication
            }
```

See the sample project for an implementation of all options.
