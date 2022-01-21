# ScreenRecorderLib
A .NET library for screen recording in Windows, using native Microsoft Media Foundation for realtime encoding to h264 video or PNG images. This library requires Windows 8 or higher to function, as well as [Visual C++ Redistributable x64](https://aka.ms/vs/16/release/vc_redist.x64.exe) or [Visual C++ Redistributable x86](https://aka.ms/vs/16/release/vc_redist.x86.exe) installed, depending on platform compiled for. This library also requires Media Foundation to work, which have to be installed from Server Manager if run on Windows Server, or from the respective "Media Feature Pack" if run on a Windows N or KN version.

Available on [NuGet](https://www.nuget.org/packages/ScreenRecorderLib/).

**Basic usage:**

This will start a video recording to a file, using the default settings:

```csharp
        using ScreenRecorderLib;
        
        Recorder _rec;
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
        }
        void EndRecording()
        {
            _rec.Stop(); 
        }
        private void Rec_OnRecordingComplete(object sender, RecordingCompleteEventArgs e)
        {
	    //Get the file path if recorded to a file
            string path = e.FilePath;	
        }
        private void Rec_OnRecordingFailed(object sender, RecordingFailedEventArgs e)
        {
            string error = e.Error;
        }
        private void Rec_OnStatusChanged(object sender, RecordingStatusEventArgs e)
        {
            RecorderStatus status = e.Status;
        }
```

For more info and examples, see the [quickstart guide](https://github.com/sskodje/ScreenRecorderLib/wiki/Quickstart-guide-v5.x.x), or check out the sample projects.

## Donation
If this project is useful to you, please consider supporting the development with a donation :) 

[![paypal](https://www.paypalobjects.com/en_US/NO/i/btn/btn_donateCC_LG.gif)](https://www.paypal.com/cgi-bin/webscr?cmd=_s-xclick&hosted_button_id=A8YE92K9QM7NA)
