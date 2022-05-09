using ScreenRecorderLib;
using System;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;

namespace TestConsoleAppStreaming
{
    class Program
    {
        private static bool _isRecording;
        private static Stopwatch _stopWatch;
        static void Main(string[] args)
        {
            var opts = new RecorderOptions
            {
                AudioOptions = new AudioOptions
                {
                    IsAudioEnabled = false,
                },
                VideoEncoderOptions = new VideoEncoderOptions
                {
                    IsFragmentedMp4Enabled = true,
                    IsLowLatencyEnabled = true,
                    Encoder = new H264VideoEncoder { BitrateMode = H264BitrateControlMode.Quality, EncoderProfile = H264Profile.Baseline },
                    IsFixedFramerate = true,
                    Framerate = 60
                }
            };
            using (Recorder rec = Recorder.CreateRecorder(opts))
            {
                rec.OnRecordingFailed += Rec_OnRecordingFailed;
                rec.OnRecordingComplete += Rec_OnRecordingComplete;
                rec.OnStatusChanged += Rec_OnStatusChanged;
                Console.WriteLine("Enter RTMP url and press ENTER to start recording");
                string rtmpUrl = Console.ReadLine();

                if (!rtmpUrl.StartsWith("rtmp://", StringComparison.InvariantCultureIgnoreCase))
                {
                    Console.WriteLine("Invalid RTMP url. Press any key to exit.");
                    Console.ReadKey();
                    return;
                }
                var inputArgs = "-re -i -";
                var outputArgs = $"-acodec copy -vcodec copy -f flv {rtmpUrl}";
                var ffMpegProcess = new Process
                {
                    StartInfo =
                        {
                            FileName = "ffmpeg.exe",
                            Arguments = $"{inputArgs} {outputArgs}",
                            UseShellExecute = false,
                            CreateNoWindow = true,
                            RedirectStandardInput = true
                        }
                };
                ffMpegProcess.Start();

                using (Stream ffmpegIn = ffMpegProcess.StandardInput.BaseStream)
                {
                    using (StreamWrapper wrappedStream = new StreamWrapper(ffmpegIn))
                    {
                        rec.Record(wrappedStream);
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

                                    Console.Write(String.Format("\rElapsed: {0}", _stopWatch.Elapsed.ToString(@"mm\:ss\:fff")));
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
                }
                ffMpegProcess.WaitForExit();
                Console.ReadKey();
            }
        }
    }

    private static void Rec_OnStatusChanged(object sender, RecordingStatusEventArgs e)
    {
        switch (e.Status)
        {
            case RecorderStatus.Idle:
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
