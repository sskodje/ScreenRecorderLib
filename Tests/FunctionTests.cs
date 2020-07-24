using System;
using System.Diagnostics;
using System.IO;
using System.Net;
using System.Threading;
using MediaInfo;
using Microsoft.VisualStudio.TestTools.UnitTesting;

namespace ScreenRecorderLib
{
    [TestClass]
    public class FunctionTests
    {
        [TestMethod]
        public void DefaultRecordingToStreamTest()
        {
            using (var outStream = new MemoryStream())
            {
                using (var rec = Recorder.CreateRecorder())
                {
                    bool isError = false;
                    bool isComplete = false;
                    ManualResetEvent resetEvent = new ManualResetEvent(false);
                    rec.OnRecordingComplete += (s, args) =>
                    {
                        isComplete = true;
                        resetEvent.Set();
                    };
                    rec.OnRecordingFailed += (s, args) =>
                    {
                        isError = true;
                        resetEvent.Set();
                    };

                    rec.Record(outStream);
                    Thread.Sleep(3000);
                    rec.Stop();
                    resetEvent.WaitOne(5000);

                    Assert.IsFalse(isError);
                    Assert.IsTrue(isComplete);
                    Assert.AreNotEqual(outStream.Length, 0);
                }
            }
        }

        [TestMethod]
        public void SoftwareEncodingTest()
        {
            using (var outStream = new MemoryStream())
            {
                RecorderOptions options = new RecorderOptions();
                options.IsHardwareEncodingEnabled = false;
                using (var rec = Recorder.CreateRecorder(options))
                {
                    bool isError = false;
                    bool isComplete = false;
                    ManualResetEvent resetEvent = new ManualResetEvent(false);
                    rec.OnRecordingComplete += (s, args) =>
                    {
                        isComplete = true;
                        resetEvent.Set();
                    };
                    rec.OnRecordingFailed += (s, args) =>
                    {
                        isError = true;
                        resetEvent.Set();
                    };

                    rec.Record(outStream);
                    Thread.Sleep(3000);
                    rec.Stop();
                    resetEvent.WaitOne(5000);

                    Assert.IsFalse(isError);
                    Assert.IsTrue(isComplete);
                    Assert.AreNotEqual(outStream.Length, 0);
                }
            }
        }

        [TestMethod]
        public void RecordingWithAudioTest()
        {
            using (var outStream = new MemoryStream())
            {
                RecorderOptions options = new RecorderOptions();
                options.AudioOptions = new AudioOptions { IsAudioEnabled = true };
                using (var rec = Recorder.CreateRecorder(options))
                {
                    bool isError = false;
                    bool isComplete = false;
                    ManualResetEvent resetEvent = new ManualResetEvent(false);
                    rec.OnRecordingComplete += (s, args) =>
                    {
                        isComplete = true;
                        resetEvent.Set();
                    };
                    rec.OnRecordingFailed += (s, args) =>
                    {
                        isError = true;
                        resetEvent.Set();
                    };

                    rec.Record(outStream);
                    Thread.Sleep(3000);
                    rec.Stop();
                    resetEvent.WaitOne(5000);

                    Assert.IsFalse(isError);
                    Assert.IsTrue(isComplete);
                    Assert.AreNotEqual(outStream.Length, 0);
                }
            }
        }

        [TestMethod]
        public void RecordingWithCropTest()
        {
            using (var outStream = new MemoryStream())
            {
                RecorderOptions options = new RecorderOptions();
                options.DisplayOptions = new DisplayOptions { Left = 100, Top = 100, Right = 500, Bottom = 500 };
                using (var rec = Recorder.CreateRecorder(options))
                {
                    bool isError = false;
                    bool isComplete = false;
                    ManualResetEvent resetEvent = new ManualResetEvent(false);
                    rec.OnRecordingComplete += (s, args) =>
                    {
                        isComplete = true;
                        resetEvent.Set();
                    };
                    rec.OnRecordingFailed += (s, args) =>
                    {
                        isError = true;
                        resetEvent.Set();
                    };

                    rec.Record(outStream);
                    Thread.Sleep(3000);
                    rec.Stop();
                    resetEvent.WaitOne(5000);

                    Assert.IsFalse(isError);
                    Assert.IsTrue(isComplete);
                    Assert.AreNotEqual(outStream.Length, 0);
                }
            }
        }

        [TestMethod]
        public void FixedFramerateTest()
        {
            using (var outStream = new MemoryStream())
            {
                RecorderOptions options = new RecorderOptions();
                options.VideoOptions = new VideoOptions { IsFixedFramerate = true };
                using (var rec = Recorder.CreateRecorder(options))
                {
                    bool isError = false;
                    bool isComplete = false;
                    ManualResetEvent resetEvent = new ManualResetEvent(false);
                    rec.OnRecordingComplete += (s, args) =>
                    {
                        isComplete = true;
                        resetEvent.Set();
                    };
                    rec.OnRecordingFailed += (s, args) =>
                    {
                        isError = true;
                        resetEvent.Set();
                    };

                    rec.Record(outStream);
                    Thread.Sleep(3000);
                    rec.Stop();
                    resetEvent.WaitOne(5000);

                    Assert.IsFalse(isError);
                    Assert.IsTrue(isComplete);
                    Assert.AreNotEqual(outStream.Length, 0);
                }
            }
        }

        [TestMethod]
        public void CustomFixedBitrateTest()
        {
            using (var outStream = new MemoryStream())
            {
                RecorderOptions options = new RecorderOptions();
                options.VideoOptions = new VideoOptions
                {
                    BitrateMode = BitrateControlMode.CBR,
                    Bitrate = 4000
                };
                using (var rec = Recorder.CreateRecorder(options))
                {
                    bool isError = false;
                    bool isComplete = false;
                    ManualResetEvent resetEvent = new ManualResetEvent(false);
                    rec.OnRecordingComplete += (s, args) =>
                    {
                        isComplete = true;
                        resetEvent.Set();
                    };
                    rec.OnRecordingFailed += (s, args) =>
                    {
                        isError = true;
                        resetEvent.Set();
                    };

                    rec.Record(outStream);
                    Thread.Sleep(3000);
                    rec.Stop();
                    resetEvent.WaitOne(5000);

                    Assert.IsFalse(isError);
                    Assert.IsTrue(isComplete);
                    Assert.AreNotEqual(outStream.Length, 0);
                }
            }
        }

        [TestMethod]
        public void SnapshotTest()
        {
            RecorderOptions options = new RecorderOptions();
            options.RecorderMode = RecorderMode.Snapshot;
            string filePath = Path.Combine(Path.GetTempPath(), Path.ChangeExtension(Path.GetRandomFileName(), ".png"));
            try
            {
                using (var rec = Recorder.CreateRecorder(options))
                {
                    bool isError = false;
                    bool isComplete = false;
                    ManualResetEvent resetEvent = new ManualResetEvent(false);
                    rec.OnRecordingComplete += (s, args) =>
                    {
                        isComplete = true;
                        resetEvent.Set();
                    };
                    rec.OnRecordingFailed += (s, args) =>
                    {
                        isError = true;
                        resetEvent.Set();
                    };

                    rec.Record(filePath);
                    Thread.Sleep(3000);
                    rec.Stop();
                    resetEvent.WaitOne(5000);

                    Assert.IsFalse(isError);
                    Assert.IsTrue(isComplete);
                    Assert.IsTrue(new FileInfo(filePath).Length > 0);
                    var mediaInfo = new MediaInfoWrapper(filePath);
                    Assert.IsTrue(mediaInfo.Format == "PNG");
                }
            }
            finally
            {
                File.Delete(filePath);
            }
        }

        [TestMethod]
        public void SlideshowTest()
        {
            string directoryPath = Path.Combine(Path.GetTempPath(), Path.GetFileNameWithoutExtension(Path.GetRandomFileName()));
            try
            {
                RecorderOptions options = new RecorderOptions();
                options.RecorderMode = RecorderMode.Slideshow;
                options.VideoOptions = new VideoOptions { Framerate = 5 };
                Directory.CreateDirectory(directoryPath);
                Assert.IsTrue(Directory.Exists(directoryPath));
                using (var rec = Recorder.CreateRecorder(options))
                {
                    bool isError = false;
                    bool isComplete = false;
                    ManualResetEvent resetEvent = new ManualResetEvent(false);
                    rec.OnRecordingComplete += (s, args) =>
                    {
                        isComplete = true;
                        resetEvent.Set();
                    };
                    rec.OnRecordingFailed += (s, args) =>
                    {
                        isError = true;
                        resetEvent.Set();
                    };

                    rec.Record(directoryPath);
                    Thread.Sleep(1000);
                    rec.Stop();
                    resetEvent.WaitOne(5000);

                    Assert.IsFalse(isError);
                    Assert.IsTrue(isComplete);
                    foreach (string filePath in Directory.GetFiles(directoryPath))
                    {
                        FileInfo fi = new FileInfo(filePath);
                        var mediaInfo = new MediaInfoWrapper(filePath);
                        Assert.IsTrue(mediaInfo.Format == "PNG");
                    }
                }
            }
            finally
            {
                Directory.Delete(directoryPath, true);
            }
        }

        [TestMethod]
        public void DefaultRecordingToFileTest()
        {
            string filePath = Path.Combine(Path.GetTempPath(), Path.ChangeExtension(Path.GetRandomFileName(), ".mp4"));
            try
            {
                using (var rec = Recorder.CreateRecorder())
                {
                    bool isError = false;
                    bool isComplete = false;
                    ManualResetEvent resetEvent = new ManualResetEvent(false);
                    rec.OnRecordingComplete += (s, args) =>
                    {
                        isComplete = true;
                        resetEvent.Set();
                    };
                    rec.OnRecordingFailed += (s, args) =>
                    {
                        isError = true;
                        resetEvent.Set();
                    };

                    rec.Record(filePath);
                    Thread.Sleep(3000);
                    rec.Stop();
                    resetEvent.WaitOne(5000);

                    Assert.IsFalse(isError);
                    Assert.IsTrue(isComplete);
                    Assert.IsTrue(new FileInfo(filePath).Length > 0);
                    var mediaInfo = new MediaInfoWrapper(filePath);
                    Assert.IsTrue(mediaInfo.Format == "MPEG-4");
                    Assert.IsTrue(mediaInfo.VideoStreams.Count > 0);
                }
            }
            finally
            {
                File.Delete(filePath);
            }
        }

        [TestMethod]
        public void Run50RecordingsTestWithDifferentInstances()
        {
            for (int i = 0; i < 50; i++)
            {
                using (Stream outStream = new MemoryStream())
                {
                    RecorderOptions options = new RecorderOptions();
                    options.VideoOptions = new VideoOptions { Framerate = 60, IsFixedFramerate = false };
                    options.AudioOptions = new AudioOptions { IsAudioEnabled = true };
                    using (var rec = Recorder.CreateRecorder(options))
                    {
                        string error = "";
                        bool isError = false;
                        bool isComplete = false;
                        ManualResetEvent resetEvent = new ManualResetEvent(false);
                        rec.OnRecordingComplete += (s, args) =>
                        {
                            isComplete = true;
                            resetEvent.Set();
                        };
                        rec.OnRecordingFailed += (s, args) =>
                        {
                            isError = true;
                            error = args.Error;
                            resetEvent.Set();
                        };

                        rec.Record(outStream);
                        Thread.Sleep(2000);
                        rec.Stop();

                        Assert.IsTrue(resetEvent.WaitOne(5000), $"[{i}] Recording finalize timed out");
                        Assert.IsNotNull(outStream, $"[{i}] Outstream is null");
                        Assert.IsFalse(isError, $"[{i}] Recording error: " + error);
                        Assert.IsTrue(isComplete, $"[{i}] Recording not complete");
                        Assert.AreNotEqual(outStream.Length, 0, $"[{i}] Outstream length is 0");
                    }
                }
            }
        }
        [TestMethod]
        public void Run50RecordingsTestWithOneInstance()
        {
            string error = "";
            bool isError = false;
            bool isComplete = false;
            AutoResetEvent resetEvent = new AutoResetEvent(false);
            RecorderOptions options = new RecorderOptions();
            options.VideoOptions = new VideoOptions { Framerate = 60, IsFixedFramerate = false };
            options.AudioOptions = new AudioOptions { IsAudioEnabled = true };
            using (var rec = Recorder.CreateRecorder(options))
            {
                rec.OnRecordingComplete += (s, args) =>
                {
                    isComplete = true;
                    resetEvent.Set();
                };
                rec.OnRecordingFailed += (s, args) =>
                {
                    isError = true;
                    error = args.Error;
                    resetEvent.Set();
                };
                for (int i = 0; i < 50; i++)
                {
                    using (var outStream = new MemoryStream())
                    {
                        isError = false;
                        isComplete = false;
                        rec.Record(outStream);
                        Thread.Sleep(2000);
                        rec.Stop();

                        Assert.IsTrue(resetEvent.WaitOne(5000), $"[{i}] Recording finalize timed out");
                        Assert.IsNotNull(outStream, $"[{i}] Outstream is null");
                        Assert.IsFalse(isError, $"[{i}] Recording error: " + error);
                        Assert.IsTrue(isComplete, $"[{i}] Recording not complete");
                        Assert.AreNotEqual(outStream.Length, 0, $"[{i}] Outstream length is 0");
                    }
                }
            }
        }
    }
}
