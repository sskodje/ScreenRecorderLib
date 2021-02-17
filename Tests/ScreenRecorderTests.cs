using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Net;
using System.Threading;
using MediaInfo;
using Microsoft.VisualStudio.TestTools.UnitTesting;

namespace ScreenRecorderLib
{
    [TestClass]
    public class ScreenRecorderTests
    {
        private string GetTempPath()
        {
            string path = Path.Combine(Path.GetTempPath(), "ScreenRecorder", "Tests");
            Directory.CreateDirectory(path);
            return path;
        }

        [TestMethod]
        public void DefaultRecordingToStreamTest()
        {
            string filePath = Path.Combine(GetTempPath(), Path.ChangeExtension(Path.GetRandomFileName(), ".mp4"));
            try
            {
                using (var outStream = File.Open(filePath, FileMode.Create, FileAccess.ReadWrite, FileShare.Read))
                {
                    using (var rec = Recorder.CreateRecorder())
                    {
                        bool isError = false;
                        bool isComplete = false;
                        ManualResetEvent finalizeResetEvent = new ManualResetEvent(false);
                        ManualResetEvent recordingResetEvent = new ManualResetEvent(false);
                        rec.OnRecordingComplete += (s, args) =>
                        {
                            isComplete = true;
                            finalizeResetEvent.Set();
                        };
                        rec.OnRecordingFailed += (s, args) =>
                        {
                            isError = true;
                            finalizeResetEvent.Set();
                            recordingResetEvent.Set();
                        };

                        rec.Record(outStream);
                        recordingResetEvent.WaitOne(3000);
                        rec.Stop();
                        finalizeResetEvent.WaitOne(5000);

                        Assert.IsFalse(isError);
                        Assert.IsTrue(isComplete);
                        Assert.AreNotEqual(outStream.Length, 0);
                        outStream.Seek(0, SeekOrigin.Begin);

                        Assert.IsTrue(new FileInfo(filePath).Length > 0);
                        var mediaInfo = new MediaInfoWrapper(filePath);
                        Assert.IsTrue(mediaInfo.Format == "MPEG-4");
                        Assert.IsTrue(mediaInfo.VideoStreams.Count > 0);
                    }
                }
            }
            finally
            {
                File.Delete(filePath);
            }
        }

        [TestMethod]
        public void QualitySettingTest()
        {
            long fullQualitySize;
            long lowestQualitySize;
            using (var outStream = new MemoryStream())
            {
                using (var rec = Recorder.CreateRecorder(new RecorderOptions { VideoOptions = new VideoOptions { BitrateMode = BitrateControlMode.Quality, Quality = 100 } }))
                {
                    bool isError = false;
                    bool isComplete = false;
                    ManualResetEvent finalizeResetEvent = new ManualResetEvent(false);
                    ManualResetEvent recordingResetEvent = new ManualResetEvent(false);
                    rec.OnRecordingComplete += (s, args) =>
                    {
                        isComplete = true;
                        finalizeResetEvent.Set();
                    };
                    rec.OnRecordingFailed += (s, args) =>
                    {
                        isError = true;
                        finalizeResetEvent.Set();
                        recordingResetEvent.Set();
                    };

                    rec.Record(outStream);
                    recordingResetEvent.WaitOne(3000);
                    rec.Stop();
                    finalizeResetEvent.WaitOne(5000);

                    Assert.IsFalse(isError);
                    Assert.IsTrue(isComplete);
                    Assert.AreNotEqual(outStream.Length, 0);
                    fullQualitySize = outStream.Length;
                }
            }
            using (var outStream = new MemoryStream())
            {
                using (var rec = Recorder.CreateRecorder(new RecorderOptions { VideoOptions = new VideoOptions { BitrateMode = BitrateControlMode.Quality, Quality = 0 } }))
                {
                    bool isError = false;
                    bool isComplete = false;
                    ManualResetEvent finalizeResetEvent = new ManualResetEvent(false);
                    ManualResetEvent recordingResetEvent = new ManualResetEvent(false);
                    rec.OnRecordingComplete += (s, args) =>
                    {
                        isComplete = true;
                        finalizeResetEvent.Set();
                    };
                    rec.OnRecordingFailed += (s, args) =>
                    {
                        isError = true;
                        finalizeResetEvent.Set();
                        recordingResetEvent.Set();
                    };

                    rec.Record(outStream);
                    recordingResetEvent.WaitOne(3000);
                    rec.Stop();
                    finalizeResetEvent.WaitOne(5000);

                    Assert.IsFalse(isError);
                    Assert.IsTrue(isComplete);
                    Assert.AreNotEqual(outStream.Length, 0);
                    lowestQualitySize = outStream.Length;
                }
            }
            Assert.IsTrue(fullQualitySize > lowestQualitySize * 2);
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
                    ManualResetEvent finalizeResetEvent = new ManualResetEvent(false);
                    ManualResetEvent recordingResetEvent = new ManualResetEvent(false);
                    rec.OnRecordingComplete += (s, args) =>
                    {
                        isComplete = true;
                        finalizeResetEvent.Set();
                    };
                    rec.OnRecordingFailed += (s, args) =>
                    {
                        isError = true;
                        finalizeResetEvent.Set();
                        recordingResetEvent.Set();
                    };

                    rec.Record(outStream);
                    recordingResetEvent.WaitOne(3000);
                    rec.Stop();
                    finalizeResetEvent.WaitOne(5000);

                    Assert.IsFalse(isError);
                    Assert.IsTrue(isComplete);
                    Assert.AreNotEqual(outStream.Length, 0);
                }
            }
        }

        [TestMethod]
        public void RecordingWithAudioOutputTest()
        {
            using (var outStream = new MemoryStream())
            {
                RecorderOptions options = new RecorderOptions();
                options.AudioOptions = new AudioOptions { IsAudioEnabled = true };
                using (var rec = Recorder.CreateRecorder(options))
                {
                    bool isError = false;
                    bool isComplete = false;
                    ManualResetEvent finalizeResetEvent = new ManualResetEvent(false);
                    ManualResetEvent recordingResetEvent = new ManualResetEvent(false);
                    rec.OnRecordingComplete += (s, args) =>
                    {
                        isComplete = true;
                        finalizeResetEvent.Set();
                    };
                    rec.OnRecordingFailed += (s, args) =>
                    {
                        isError = true;
                        finalizeResetEvent.Set();
                        recordingResetEvent.Set();
                    };

                    rec.Record(outStream);
                    recordingResetEvent.WaitOne(3000);
                    rec.Stop();
                    finalizeResetEvent.WaitOne(5000);

                    Assert.IsFalse(isError);
                    Assert.IsTrue(isComplete);
                    Assert.AreNotEqual(outStream.Length, 0);
                }
            }
        }

        [TestMethod]
        public void RecordingWithAudioInputTest()
        {
            using (var outStream = new MemoryStream())
            {
                RecorderOptions options = new RecorderOptions();
                options.AudioOptions = new AudioOptions { IsAudioEnabled = true, IsInputDeviceEnabled = true };
                using (var rec = Recorder.CreateRecorder(options))
                {
                    bool isError = false;
                    bool isComplete = false;
                    ManualResetEvent finalizeResetEvent = new ManualResetEvent(false);
                    ManualResetEvent recordingResetEvent = new ManualResetEvent(false);
                    rec.OnRecordingComplete += (s, args) =>
                    {
                        isComplete = true;
                        finalizeResetEvent.Set();
                    };
                    rec.OnRecordingFailed += (s, args) =>
                    {
                        isError = true;
                        finalizeResetEvent.Set();
                        recordingResetEvent.Set();
                    };

                    rec.Record(outStream);
                    recordingResetEvent.WaitOne(3000);
                    rec.Stop();
                    finalizeResetEvent.WaitOne(5000);

                    Assert.IsFalse(isError);
                    Assert.IsTrue(isComplete);
                    Assert.AreNotEqual(outStream.Length, 0);
                }
            }
        }

        [TestMethod]
        public void RecordingWithAudioMixingTest()
        {
            using (var outStream = new MemoryStream())
            {
                RecorderOptions options = new RecorderOptions();
                options.AudioOptions = new AudioOptions { IsAudioEnabled = true, IsInputDeviceEnabled = true, IsOutputDeviceEnabled = true };
                using (var rec = Recorder.CreateRecorder(options))
                {
                    bool isError = false;
                    bool isComplete = false;
                    ManualResetEvent finalizeResetEvent = new ManualResetEvent(false);
                    ManualResetEvent recordingResetEvent = new ManualResetEvent(false);
                    rec.OnRecordingComplete += (s, args) =>
                    {
                        isComplete = true;
                        finalizeResetEvent.Set();
                    };
                    rec.OnRecordingFailed += (s, args) =>
                    {
                        isError = true;
                        finalizeResetEvent.Set();
                        recordingResetEvent.Set();
                    };

                    rec.Record(outStream);
                    recordingResetEvent.WaitOne(3000);
                    rec.Stop();
                    finalizeResetEvent.WaitOne(5000);

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
                    ManualResetEvent finalizeResetEvent = new ManualResetEvent(false);
                    ManualResetEvent recordingResetEvent = new ManualResetEvent(false);
                    rec.OnRecordingComplete += (s, args) =>
                    {
                        isComplete = true;
                        finalizeResetEvent.Set();
                    };
                    rec.OnRecordingFailed += (s, args) =>
                    {
                        isError = true;
                        finalizeResetEvent.Set();
                        recordingResetEvent.Set();
                    };

                    rec.Record(outStream);
                    recordingResetEvent.WaitOne(3000);
                    rec.Stop();
                    finalizeResetEvent.WaitOne(5000);

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
                    ManualResetEvent finalizeResetEvent = new ManualResetEvent(false);
                    ManualResetEvent recordingResetEvent = new ManualResetEvent(false);
                    rec.OnRecordingComplete += (s, args) =>
                    {
                        isComplete = true;
                        finalizeResetEvent.Set();
                    };
                    rec.OnRecordingFailed += (s, args) =>
                    {
                        isError = true;
                        finalizeResetEvent.Set();
                        recordingResetEvent.Set();
                    };

                    rec.Record(outStream);
                    recordingResetEvent.WaitOne(3000);
                    rec.Stop();
                    finalizeResetEvent.WaitOne(5000);

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
                    ManualResetEvent finalizeResetEvent = new ManualResetEvent(false);
                    ManualResetEvent recordingResetEvent = new ManualResetEvent(false);
                    rec.OnRecordingComplete += (s, args) =>
                    {
                        isComplete = true;
                        finalizeResetEvent.Set();
                    };
                    rec.OnRecordingFailed += (s, args) =>
                    {
                        isError = true;
                        finalizeResetEvent.Set();
                        recordingResetEvent.Set();
                    };

                    rec.Record(outStream);
                    recordingResetEvent.WaitOne(3000);
                    rec.Stop();
                    finalizeResetEvent.WaitOne(5000);

                    Assert.IsFalse(isError);
                    Assert.IsTrue(isComplete);
                    Assert.AreNotEqual(outStream.Length, 0);
                }
            }
        }

        [TestMethod]
        public void ScreenshotTest()
        {
            RecorderOptions options = new RecorderOptions();
            options.RecorderMode = RecorderMode.Snapshot;
            string filePath = Path.Combine(GetTempPath(), Path.ChangeExtension(Path.GetRandomFileName(), ".png"));
            try
            {
                using (var rec = Recorder.CreateRecorder(options))
                {
                    bool isError = false;
                    bool isComplete = false;
                    ManualResetEvent finalizeResetEvent = new ManualResetEvent(false);
                    ManualResetEvent recordingResetEvent = new ManualResetEvent(false);
                    rec.OnRecordingComplete += (s, args) =>
                    {
                        isComplete = true;
                        finalizeResetEvent.Set();
                    };
                    rec.OnRecordingFailed += (s, args) =>
                    {
                        isError = true;
                        finalizeResetEvent.Set();
                        recordingResetEvent.Set();
                    };

                    rec.Record(filePath);
                    recordingResetEvent.WaitOne(3000);
                    rec.Stop();
                    finalizeResetEvent.WaitOne(5000);

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
        public void ScreenshotTestWithCropping()
        {
            RecorderOptions options = new RecorderOptions();
            options.RecorderMode = RecorderMode.Snapshot;
            options.DisplayOptions = new DisplayOptions { Left = 100, Top = 100, Right = 200, Bottom = 200 };
            string filePath = Path.Combine(GetTempPath(), Path.ChangeExtension(Path.GetRandomFileName(), ".png"));
            try
            {
                using (var rec = Recorder.CreateRecorder(options))
                {
                    bool isError = false;
                    bool isComplete = false;
                    ManualResetEvent finalizeResetEvent = new ManualResetEvent(false);
                    ManualResetEvent recordingResetEvent = new ManualResetEvent(false);
                    rec.OnRecordingComplete += (s, args) =>
                    {
                        isComplete = true;
                        finalizeResetEvent.Set();
                    };
                    rec.OnRecordingFailed += (s, args) =>
                    {
                        isError = true;
                        finalizeResetEvent.Set();
                        recordingResetEvent.Set();
                    };

                    rec.Record(filePath);
                    recordingResetEvent.WaitOne(3000);
                    rec.Stop();
                    finalizeResetEvent.WaitOne(5000);

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
            string directoryPath = Path.Combine(GetTempPath(), Path.GetFileNameWithoutExtension(Path.GetRandomFileName()));
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
                    ManualResetEvent finalizeResetEvent = new ManualResetEvent(false);
                    ManualResetEvent recordingResetEvent = new ManualResetEvent(false);
                    rec.OnRecordingComplete += (s, args) =>
                    {
                        isComplete = true;
                        finalizeResetEvent.Set();
                    };
                    rec.OnRecordingFailed += (s, args) =>
                    {
                        isError = true;
                        finalizeResetEvent.Set();
                        recordingResetEvent.Set();
                    };

                    rec.Record(directoryPath);
                    recordingResetEvent.WaitOne(1000);
                    rec.Stop();
                    finalizeResetEvent.WaitOne(5000);

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
            string filePath = Path.Combine(GetTempPath(), Path.ChangeExtension(Path.GetRandomFileName(), ".mp4"));
            try
            {
                using (var rec = Recorder.CreateRecorder())
                {
                    bool isError = false;
                    bool isComplete = false;
                    ManualResetEvent finalizeResetEvent = new ManualResetEvent(false);
                    ManualResetEvent recordingResetEvent = new ManualResetEvent(false);
                    rec.OnRecordingComplete += (s, args) =>
                    {
                        isComplete = true;
                        finalizeResetEvent.Set();
                    };
                    rec.OnRecordingFailed += (s, args) =>
                    {
                        isError = true;
                        finalizeResetEvent.Set();
                        recordingResetEvent.Set();
                    };

                    rec.Record(filePath);
                    recordingResetEvent.WaitOne(3000);
                    rec.Stop();
                    finalizeResetEvent.WaitOne(5000);

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
        public void RecordingToFileWithSnapshotsTest()
        {
            string filePath = Path.Combine(GetTempPath(), Path.ChangeExtension(Path.GetRandomFileName(), ".mp4"));
            string snapshotsDir = Path.ChangeExtension(filePath, null);
            try
            {
                RecorderOptions options = new RecorderOptions();
                options.VideoOptions = new VideoOptions { SnapshotsWithVideo = true, SnapshotsInterval = 2, SnapshotFormat = ImageFormat.JPEG };
                using (var rec = Recorder.CreateRecorder(options))
                {
                    List<string> snapshotCallbackList = new List<string>();
                    bool isError = false;
                    bool isComplete = false;
                    ManualResetEvent finalizeResetEvent = new ManualResetEvent(false);
                    ManualResetEvent recordingResetEvent = new ManualResetEvent(false);
                    rec.OnRecordingComplete += (s, args) =>
                    {
                        isComplete = true;
                        finalizeResetEvent.Set();
                    };
                    rec.OnRecordingFailed += (s, args) =>
                    {
                        isError = true;
                        finalizeResetEvent.Set();
                        recordingResetEvent.Set();
                    };
                    rec.OnSnapshotSaved += (s, args) =>
                    {
                        snapshotCallbackList.Add(args.SnapshotPath);
                    };
                    rec.Record(filePath);
                    recordingResetEvent.WaitOne(11900); // 10 < x < 12 sec
                    rec.Stop();
                    finalizeResetEvent.WaitOne(5000);

                    Assert.IsFalse(isError);
                    Assert.IsTrue(isComplete);
                    Assert.IsTrue(new FileInfo(filePath).Length > 0);
                    var mediaInfo = new MediaInfoWrapper(filePath);
                    Assert.IsTrue(mediaInfo.Format == "MPEG-4");
                    Assert.IsTrue(mediaInfo.VideoStreams.Count > 0);

                    var snapshotsOnDisk = Directory.GetFiles(snapshotsDir);
                    Assert.AreEqual(6, snapshotsOnDisk.Count());  // First snapshot taken at time 0.
                    Assert.IsTrue(Enumerable.SequenceEqual(snapshotCallbackList, snapshotsOnDisk));
                    foreach (var snapshot in snapshotsOnDisk)
                    {
                        Assert.IsTrue(new MediaInfoWrapper(snapshot).Format == "JPEG");
                    }
                }
            }
            finally
            {
                File.Delete(filePath);
                Directory.Delete(snapshotsDir, recursive: true);
            }
        }

        [TestMethod]
        public void RecordingToStreamWithSnapshotsTest()
        {
            string filePath = Path.Combine(GetTempPath(), Path.ChangeExtension(Path.GetRandomFileName(), ".mp4"));
            string snapshotsDir = Path.ChangeExtension(filePath, null);
            try
            {
                using (var outStream = File.Open(filePath, FileMode.Create, FileAccess.ReadWrite, FileShare.Read))
                {
                    RecorderOptions options = new RecorderOptions();
                    options.VideoOptions = new VideoOptions
                    {
                        SnapshotsWithVideo = true,
                        SnapshotsInterval = 2,
                        SnapshotFormat = ImageFormat.JPEG,
                        SnapshotsDirectory = snapshotsDir
                    };
                    using (var rec = Recorder.CreateRecorder(options))
                    {
                        List<string> snapshotCallbackList = new List<string>();
                        bool isError = false;
                        bool isComplete = false;
                        ManualResetEvent finalizeResetEvent = new ManualResetEvent(false);
                        ManualResetEvent recordingResetEvent = new ManualResetEvent(false);
                        rec.OnRecordingComplete += (s, args) =>
                        {
                            isComplete = true;
                            finalizeResetEvent.Set();
                        };
                        rec.OnRecordingFailed += (s, args) =>
                        {
                            isError = true;
                            finalizeResetEvent.Set();
                            recordingResetEvent.Set();
                        };
                        rec.OnSnapshotSaved += (s, args) =>
                        {
                            snapshotCallbackList.Add(args.SnapshotPath);
                        };
                        rec.Record(outStream);
                        recordingResetEvent.WaitOne(11900); // 10 < x < 12 sec
                        rec.Stop();
                        finalizeResetEvent.WaitOne(5000);

                        Assert.IsFalse(isError);
                        Assert.IsTrue(isComplete);
                        outStream.Seek(0, SeekOrigin.Begin);
                        Assert.IsTrue(new FileInfo(filePath).Length > 0);
                        var mediaInfo = new MediaInfoWrapper(filePath);
                        Assert.IsTrue(mediaInfo.Format == "MPEG-4");
                        Assert.IsTrue(mediaInfo.VideoStreams.Count > 0);

                        var snapshotsOnDisk = Directory.GetFiles(snapshotsDir);
                        Assert.AreEqual(6, snapshotsOnDisk.Count());  // First snapshot taken at time 0.
                        Assert.IsTrue(Enumerable.SequenceEqual(snapshotCallbackList, snapshotsOnDisk));
                        foreach (var snapshot in snapshotsOnDisk)
                        {
                            Assert.IsTrue(new MediaInfoWrapper(snapshot).Format == "JPEG");
                        }
                    }
                }
            }
            finally
            {
                Directory.Delete(snapshotsDir, recursive: true);
                File.Delete(filePath);
            }
        }

        [TestMethod]
        public void RecordingToFileWithSnapshotsAndCroppingTest()
        {
            string filePath = Path.Combine(GetTempPath(), Path.ChangeExtension(Path.GetRandomFileName(), ".mp4"));
            string snapshotsDir = Path.ChangeExtension(filePath, null);
            try
            {
                RecorderOptions options = new RecorderOptions();
                options.VideoOptions = new VideoOptions { SnapshotsWithVideo = true, SnapshotsInterval = 2, SnapshotFormat = ImageFormat.JPEG };
                options.DisplayOptions = new DisplayOptions { Left = 100, Top = 100, Right = 200, Bottom = 200 };
                using (var rec = Recorder.CreateRecorder(options))
                {
                    List<string> snapshotCallbackList = new List<string>();
                    bool isError = false;
                    bool isComplete = false;
                    ManualResetEvent finalizeResetEvent = new ManualResetEvent(false);
                    ManualResetEvent recordingResetEvent = new ManualResetEvent(false);
                    rec.OnRecordingComplete += (s, args) =>
                    {
                        isComplete = true;
                        finalizeResetEvent.Set();
                    };
                    rec.OnRecordingFailed += (s, args) =>
                    {
                        isError = true;
                        finalizeResetEvent.Set();
                        recordingResetEvent.Set();
                    };
                    rec.OnSnapshotSaved += (s, args) =>
                    {
                        snapshotCallbackList.Add(args.SnapshotPath);
                    };
                    rec.Record(filePath);
                    recordingResetEvent.WaitOne(11900); // 10 < x < 12 sec
                    rec.Stop();
                    finalizeResetEvent.WaitOne(5000);

                    Assert.IsFalse(isError);
                    Assert.IsTrue(isComplete);
                    Assert.IsTrue(new FileInfo(filePath).Length > 0);
                    var mediaInfo = new MediaInfoWrapper(filePath);
                    Assert.IsTrue(mediaInfo.Format == "MPEG-4");
                    Assert.IsTrue(mediaInfo.VideoStreams.Count > 0);

                    var snapshotsOnDisk = Directory.GetFiles(snapshotsDir);
                    Assert.AreEqual(6, snapshotsOnDisk.Count());  // First snapshot taken at time 0.
                    Assert.IsTrue(Enumerable.SequenceEqual(snapshotCallbackList, snapshotsOnDisk));
                    foreach (var snapshot in snapshotsOnDisk)
                    {
                        Assert.IsTrue(new MediaInfoWrapper(snapshot).Format == "JPEG");
                    }
                }
            }
            finally
            {
                File.Delete(filePath);
                Directory.Delete(snapshotsDir, recursive: true);
            }
        }

        [TestMethod]
        public void DefaultRecordingOneMinuteToFileTest()
        {
            string filePath = Path.Combine(GetTempPath(), Path.ChangeExtension(Path.GetRandomFileName(), ".mp4"));
            try
            {
                using (var rec = Recorder.CreateRecorder())
                {
                    bool isError = false;
                    bool isComplete = false;
                    ManualResetEvent finalizingResetEvent = new ManualResetEvent(false);
                    ManualResetEvent recordingResetEvent = new ManualResetEvent(false);
                    rec.OnRecordingComplete += (s, args) =>
                    {
                        isComplete = true;
                        finalizingResetEvent.Set();
                    };
                    rec.OnRecordingFailed += (s, args) =>
                    {
                        isError = true;
                        finalizingResetEvent.Set();
                        recordingResetEvent.Set();
                    };

                    rec.Record(filePath);
                    recordingResetEvent.WaitOne(60 * 1000);
                    rec.Stop();
                    finalizingResetEvent.WaitOne(5000);
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
                    options.AudioOptions = new AudioOptions { IsAudioEnabled = true, IsInputDeviceEnabled = true, IsOutputDeviceEnabled = true };
                    using (var rec = Recorder.CreateRecorder(options))
                    {
                        string error = "";
                        bool isError = false;
                        bool isComplete = false;
                        ManualResetEvent finalizeResetEvent = new ManualResetEvent(false);
                        ManualResetEvent recordingResetEvent = new ManualResetEvent(false);
                        rec.OnRecordingComplete += (s, args) =>
                        {
                            isComplete = true;
                            finalizeResetEvent.Set();
                        };
                        rec.OnRecordingFailed += (s, args) =>
                        {
                            isError = true;
                            error = args.Error;
                            finalizeResetEvent.Set();
                            recordingResetEvent.Set();
                        };

                        rec.Record(outStream);
                        recordingResetEvent.WaitOne(2000);
                        rec.Stop();

                        Assert.IsTrue(finalizeResetEvent.WaitOne(5000), $"[{i}] Recording finalize timed out");
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
            AutoResetEvent finalizeResetEvent = new AutoResetEvent(false);
            ManualResetEvent recordingResetEvent = new ManualResetEvent(false);
            RecorderOptions options = new RecorderOptions();
            options.VideoOptions = new VideoOptions { Framerate = 60, IsFixedFramerate = false };
            options.AudioOptions = new AudioOptions { IsAudioEnabled = true, IsInputDeviceEnabled = true, IsOutputDeviceEnabled = true };
            using (var rec = Recorder.CreateRecorder(options))
            {
                rec.OnRecordingComplete += (s, args) =>
                {
                    isComplete = true;
                    finalizeResetEvent.Set();
                };
                rec.OnRecordingFailed += (s, args) =>
                {
                    isError = true;
                    error = args.Error;
                    finalizeResetEvent.Set();
                    recordingResetEvent.Set();
                };
                for (int i = 0; i < 50; i++)
                {
                    using (var outStream = new MemoryStream())
                    {
                        isError = false;
                        isComplete = false;
                        rec.Record(outStream);
                        recordingResetEvent.WaitOne(2000);
                        rec.Stop();

                        Assert.IsTrue(finalizeResetEvent.WaitOne(5000), $"[{i}] Recording finalize timed out");
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
