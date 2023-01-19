using ScreenRecorderLib;
using System;
using System.Diagnostics;
using System.IO;
using System.Threading;
using System.Threading.Tasks;
using System.Linq;

namespace TestConsoleAppDotNetCore
{
    class Program
    {
        private static bool _isRecording;
        private static Stopwatch _stopWatch;
        static void Main(string[] args)
        {
            //This is how you can select audio devices. If you want the system default device,
            //just leave the AudioInputDevice or AudioOutputDevice properties unset or pass null or empty string.
            var audioInputDevices = Recorder.GetSystemAudioDevices(AudioDeviceSource.InputDevices);
            var audioOutputDevices = Recorder.GetSystemAudioDevices(AudioDeviceSource.OutputDevices);
            string selectedAudioInputDevice = audioInputDevices.Count > 0 ? audioInputDevices.First().DeviceName : null;
            string selectedAudioOutputDevice = audioOutputDevices.Count > 0 ? audioOutputDevices.First().DeviceName : null;

            var opts = new RecorderOptions
            {
                AudioOptions = new AudioOptions
                {
                    AudioInputDevice = selectedAudioInputDevice,
                    AudioOutputDevice = selectedAudioOutputDevice,
                    IsAudioEnabled = true,
                    IsInputDeviceEnabled = true,
                    IsOutputDeviceEnabled = true,
                }
            };

            Recorder rec = Recorder.CreateRecorder(opts);
            rec.OnRecordingFailed += Rec_OnRecordingFailed;
            rec.OnRecordingComplete += Rec_OnRecordingComplete;
            rec.OnStatusChanged += Rec_OnStatusChanged;
            Console.WriteLine("Press ENTER to start recording or ESC to exit");
            while (true)
            {
                ConsoleKeyInfo info = Console.ReadKey(true);
                if (info.Key == ConsoleKey.Enter)
                {
                    break;
                }
                else if (info.Key == ConsoleKey.Escape)
                {
                    return;
                }
            }
            string timestamp = DateTime.Now.ToString("yyyy-MM-dd HH-mm-ss");
            string filePath = Path.Combine(Path.GetTempPath(), "ScreenRecorder", timestamp, timestamp + ".mp4");
            rec.Record(filePath);
            CancellationTokenSource cts = new CancellationTokenSource();
            var token = cts.Token;
            Task.Run(async () =>
            {
                while (true)
                {
                    if (token.IsCancellationRequested)
                        return;
                    if (_isRecording)
                    {
                        Console.Write(String.Format("\rElapsed: {0}s:{1}ms", _stopWatch.Elapsed.Seconds, _stopWatch.Elapsed.Milliseconds));
                    }
                    await Task.Delay(10);
                }
            }, token);
            while (true)
            {
                ConsoleKeyInfo info = Console.ReadKey(true);
                if (info.Key == ConsoleKey.Escape)
                {
                    break;
                }
            }
            cts.Cancel();
            rec.Stop();
            Console.WriteLine();

            Console.ReadKey();
        }

        private static void Rec_OnStatusChanged(object sender, RecordingStatusEventArgs e)
        {
            switch (e.Status)
            {
                case RecorderStatus.Idle:
                    //Console.WriteLine("Recorder is idle");
                    break;
                case RecorderStatus.Recording:
                    _stopWatch = new Stopwatch();
                    _stopWatch.Start();
                    _isRecording = true;
                    Console.WriteLine("Recording started");
                    Console.WriteLine("Press ESC to stop recording");
                    break;
                case RecorderStatus.Paused:
                    Console.WriteLine("Recording paused");
                    break;
                case RecorderStatus.Finishing:
                    Console.WriteLine("Finishing encoding");
                    break;
                default:
                    break;
            }
        }

        private static void Rec_OnRecordingComplete(object sender, RecordingCompleteEventArgs e)
        {
            Console.WriteLine("Recording completed");
            _isRecording = false;
            _stopWatch?.Stop();
            Console.WriteLine(String.Format("File: {0}", e.FilePath));
            Console.WriteLine();
            Console.WriteLine("Press any key to exit");
        }

        private static void Rec_OnRecordingFailed(object sender, RecordingFailedEventArgs e)
        {
            Console.WriteLine("Recording failed with: " + e.Error);
            _isRecording = false;
            _stopWatch?.Stop();
            Console.WriteLine();
            Console.WriteLine("Press any key to exit");
        }
    }
}