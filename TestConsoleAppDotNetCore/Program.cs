using ScreenRecorderLib;
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;


class Program
{
    static void Main(string[] args)
    {
        var source = DisplayRecordingSource.MainMonitor;
        source.RecorderApi = RecorderApi.WindowsGraphicsCapture;
        var opts = new RecorderOptions
        {
            SourceOptions = new SourceOptions
            {
                RecordingSources = { { source } }
            },
            AudioOptions = new AudioOptions { IsAudioEnabled = true, AudioSources = new List<AudioSourceBase> { LoopbackAudioSource.Default, CaptureAudioSource.Default } }
        };
        Recorder rec = Recorder.CreateRecorder(opts);
        Recorder rec2 = Recorder.CreateRecorder(opts);
        rec.OnRecordingComplete += (source, e) =>
        {
            Console.WriteLine($"Recording complete: {e.FilePath}");
        };
        rec2.OnRecordingComplete += (source, e) =>
        {
            Console.WriteLine($"Recording complete: {e.FilePath}");
        };

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
        ManualResetEvent completionEvent = new ManualResetEvent(false);
        ManualResetEvent escapeEvent = new ManualResetEvent(false);
        string timestamp = DateTime.Now.ToString("yyyy-MM-dd HH-mm-ss");
        Console.WriteLine("Starting recording");
        int count = 50;
        CancellationTokenSource cts = new CancellationTokenSource();
        var token = cts.Token;
        Task.Run(async () =>
            {
                for (int i = 0; i < count; i++)
                {
                    if (token.IsCancellationRequested)
                        break;

                    var currentRecorder = i % 2 == 0 ? rec : rec2;
                    var nextRecorder = i % 2 == 0 ? rec2 : rec;

                    if (currentRecorder.Status == RecorderStatus.Paused)
                    {
                        currentRecorder.Resume();
                    }
                    else
                    {
                        currentRecorder.Record(Path.Combine(Path.GetTempPath(), "ScreenRecorder", timestamp, i + ".mp4"));
                    }

                    await WaitForIdle(nextRecorder);
                    if (i < count)
                    {
                        nextRecorder.Record(Path.Combine(Path.GetTempPath(), "ScreenRecorder", timestamp, i + 1 + ".mp4"));
                        nextRecorder.Pause();
                    }
                    await Task.Delay(5000);
                    currentRecorder.Stop();
                }
                completionEvent.Set();
            });


        var escapeThread = new Thread(() =>
        {
            while (!escapeEvent.WaitOne(0))
            {
                if (Console.KeyAvailable)
                {
                    var key = Console.ReadKey(intercept: true);
                    if (key.Key == ConsoleKey.Escape)
                    {
                        escapeEvent.Set();
                        return;
                    }
                }
                else
                {
                    Thread.Sleep(50);
                }
            }
        });
        escapeThread.IsBackground = true;
        escapeThread.Start();

        int index = WaitHandle.WaitAny(new WaitHandle[] { completionEvent, escapeEvent });
        if (index == 1)
        {
            cts.Cancel();
            Console.WriteLine("Waiting for recording to exit..");
            completionEvent.WaitOne();
        }
        Console.WriteLine("Press any key to exit");
        Console.ReadKey();
    }

    private static async Task WaitForIdle(Recorder rec)
    {
        if (rec.Status == RecorderStatus.Idle)
        {
            return;
        }
        else
        {
            SemaphoreSlim semaphore = new SemaphoreSlim(0, 1);
            EventHandler<RecordingStatusEventArgs> handler = delegate (object s, RecordingStatusEventArgs args)
            {
                if (args.Status == RecorderStatus.Idle)
                {
                    semaphore.Release();
                }
            };
            rec.OnStatusChanged += handler;
            await semaphore.WaitAsync();
            rec.OnStatusChanged -= handler;
        }
    }
}