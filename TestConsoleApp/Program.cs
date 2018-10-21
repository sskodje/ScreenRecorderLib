using ScreenRecorderLib;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Threading;

namespace TestConsoleApp
{
    class Program
    {
        private static bool _isRecording;
        private static Stopwatch _stopWatch;
        static void Main(string[] args)
        {
            Recorder rec = Recorder.CreateRecorder();
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
            rec.Record(Path.ChangeExtension(Path.GetTempFileName(), ".mp4"));
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
                        Dispatcher.CurrentDispatcher.Invoke(() =>
                        {
                            Console.Write(String.Format("\rElapsed: {0}s:{1}ms", _stopWatch.Elapsed.Seconds, _stopWatch.Elapsed.Milliseconds));
                        });
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
            Console.WriteLine("Recording failed");
            _isRecording = false;
            _stopWatch?.Stop();
            Console.WriteLine();
            Console.WriteLine("Press any key to exit");
        }
    }
}
