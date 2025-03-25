using ScreenRecorderLib;
using System;
using System.IO;
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
            AudioOptions = new AudioOptions { IsInputDeviceEnabled = true, IsOutputDeviceEnabled = true, IsAudioEnabled=true }
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
        string timestamp = DateTime.Now.ToString("yyyy-MM-dd HH-mm-ss");
        Console.WriteLine("Starting recording");
        int count = 50;
        Task.Run(async () =>
            {
                for (int i = 0; i < count; i++)
                {
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

        completionEvent.WaitOne();
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