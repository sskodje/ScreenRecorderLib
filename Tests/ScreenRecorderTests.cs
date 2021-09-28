using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Net;
using System.Threading;
using System.Threading.Tasks;
using MediaInfo;
using Microsoft.VisualStudio.TestTools.UnitTesting;

namespace ScreenRecorderLib
{
    [TestClass]
    public class ScreenRecorderTests
    {
        const int DefaultRecordingLengthMillis = 3000;

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
                    using (var rec = Recorder.CreateRecorder(RecorderOptions.DefaultMainMonitor))
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
                        recordingResetEvent.WaitOne(DefaultRecordingLengthMillis);
                        rec.Stop();
                        finalizeResetEvent.WaitOne(5000);
                        outStream.Flush();
                        Assert.IsFalse(isError, error);
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
        [DataRow(RecorderApi.DesktopDuplication)]
        [DataRow(RecorderApi.WindowsGraphicsCapture)]
        public void EnumAndRecordAllDisplaysSequentially(RecorderApi api)
        {
            foreach (DisplayRecordingSource displaySource in Recorder.GetDisplays())
            {
                string filePath = Path.Combine(GetTempPath(), Path.ChangeExtension(Path.GetRandomFileName(), ".mp4"));
                try
                {
                    using (var outStream = File.Open(filePath, FileMode.Create, FileAccess.ReadWrite, FileShare.Read))
                    {
                        displaySource.RecorderApi = api;
                        var options = new RecorderOptions
                        {
                            SourceOptions = new SourceOptions { RecordingSources = { displaySource } }
                        };
                        using (var rec = Recorder.CreateRecorder(options))
                        {
                            bool isError = false;
                            string error = "";
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
                            recordingResetEvent.WaitOne(DefaultRecordingLengthMillis);
                            rec.Stop();
                            finalizeResetEvent.WaitOne(5000);
                            outStream.Flush();

                            Assert.IsFalse(isError, error, "recording has error for {0}", api);
                            Assert.IsTrue(isComplete, "recording did not complete for {0}", api);
                            Assert.IsTrue(outStream.Length > 0, "stream length is 0 for {0}", api);
                            var mediaInfo = new MediaInfoWrapper(filePath);
                            Assert.IsTrue(mediaInfo.Format == "MPEG-4", "wrong video format for {0}", api);
                            Assert.IsTrue(mediaInfo.VideoStreams.Count > 0, "no video streams found for {0}", api);
                        }
                    }
                }
                finally
                {
                    File.Delete(filePath);
                }
            }
        }

        [TestMethod]
        public void QualitySettingTest()
        {
            long fullQualitySize;
            long lowestQualitySize;
            using (var outStream = new MemoryStream())
            {
                using (var rec = Recorder.CreateRecorder(new RecorderOptions
                {
                    VideoEncoderOptions = new VideoEncoderOptions
                    {
                        Encoder = new H264VideoEncoder { BitrateMode = H264BitrateControlMode.Quality },
                        Quality = 100
                    }
                }))
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
                    recordingResetEvent.WaitOne(DefaultRecordingLengthMillis);
                    rec.Stop();
                    finalizeResetEvent.WaitOne(5000);
                    outStream.Flush();
                    Assert.IsFalse(isError, error);
                    Assert.IsTrue(isComplete);
                    Assert.AreNotEqual(outStream.Length, 0);
                    fullQualitySize = outStream.Length;
                }
            }
            using (var outStream = new MemoryStream())
            {
                using (var rec = Recorder.CreateRecorder(new RecorderOptions
                {
                    VideoEncoderOptions = new VideoEncoderOptions
                    {
                        Encoder = new H264VideoEncoder { BitrateMode = H264BitrateControlMode.Quality },
                        Quality = 0
                    }
                }))
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
                    recordingResetEvent.WaitOne(DefaultRecordingLengthMillis);
                    rec.Stop();
                    finalizeResetEvent.WaitOne(5000);

                    Assert.IsFalse(isError, error);
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
                RecorderOptions options = new RecorderOptions() { VideoEncoderOptions = new VideoEncoderOptions { IsHardwareEncodingEnabled = false } };
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
                    recordingResetEvent.WaitOne(DefaultRecordingLengthMillis);
                    rec.Stop();
                    finalizeResetEvent.WaitOne(5000);
                    outStream.Flush();
                    Assert.IsFalse(isError, error);
                    Assert.IsTrue(isComplete);
                    Assert.AreNotEqual(outStream.Length, 0);
                }
            }
        }

        [TestMethod]
        public void RecordingWithNoAudioTest()
        {
            string filePath = Path.Combine(GetTempPath(), Path.ChangeExtension(Path.GetRandomFileName(), ".mp4"));
            try
            {
                using (var outStream = File.Open(filePath, FileMode.Create, FileAccess.ReadWrite, FileShare.Read))
                {
                    RecorderOptions options = new RecorderOptions();
                    options.AudioOptions = new AudioOptions { IsAudioEnabled = false };
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
                        recordingResetEvent.WaitOne(DefaultRecordingLengthMillis);
                        rec.Stop();
                        finalizeResetEvent.WaitOne(5000);
                        outStream.Flush();
                        var mediaInfo = new MediaInfoWrapper(filePath);
                        Assert.IsTrue(mediaInfo.AudioStreams.Count == 0);
                        Assert.IsFalse(isError, error);
                        Assert.IsTrue(isComplete);
                        Assert.AreNotEqual(outStream.Length, 0);
                    }
                }
            }
            finally
            {
                File.Delete(filePath);
            }
        }

        [TestMethod]
        public void RecordingWithAudioInputTest()
        {
            string filePath = Path.Combine(GetTempPath(), Path.ChangeExtension(Path.GetRandomFileName(), ".mp4"));
            try
            {
                using (var outStream = File.Open(filePath, FileMode.Create, FileAccess.ReadWrite, FileShare.Read))
                {
                    RecorderOptions options = new RecorderOptions();
                    options.AudioOptions = new AudioOptions { IsAudioEnabled = true, IsInputDeviceEnabled = true };
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
                        recordingResetEvent.WaitOne(DefaultRecordingLengthMillis);
                        rec.Stop();
                        finalizeResetEvent.WaitOne(5000);
                        outStream.Flush();
                        var mediaInfo = new MediaInfoWrapper(filePath);
                        Assert.IsTrue(mediaInfo.AudioStreams.Count > 0);
                        Assert.IsFalse(isError, error);
                        Assert.IsTrue(isComplete);
                        Assert.AreNotEqual(outStream.Length, 0);
                    }
                }
            }
            finally
            {
                File.Delete(filePath);
            }
        }

        [TestMethod]
        public void RecordingWithAudioMixingTest()
        {
            string filePath = Path.Combine(GetTempPath(), Path.ChangeExtension(Path.GetRandomFileName(), ".mp4"));
            try
            {
                using (var outStream = File.Open(filePath, FileMode.Create, FileAccess.ReadWrite, FileShare.Read))
                {
                    RecorderOptions options = new RecorderOptions();
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
                        recordingResetEvent.WaitOne(DefaultRecordingLengthMillis);
                        rec.Stop();
                        finalizeResetEvent.WaitOne(5000);
                        outStream.Flush();
                        var mediaInfo = new MediaInfoWrapper(filePath);
                        Assert.IsTrue(mediaInfo.AudioStreams.Count > 0);
                        Assert.IsFalse(isError, error);
                        Assert.IsTrue(isComplete);
                        Assert.AreNotEqual(outStream.Length, 0);
                    }
                }
            }
            finally
            {
                File.Delete(filePath);
            }
        }

        [TestMethod]
        public void RecordingWithCustomOutputFrameSizeTest()
        {
            string filePath = Path.Combine(GetTempPath(), Path.ChangeExtension(Path.GetRandomFileName(), ".mp4"));
            try
            {
                using (var outStream = File.Open(filePath, FileMode.Create, FileAccess.ReadWrite, FileShare.Read))
                {
                    RecorderOptions options = new RecorderOptions();
                    int frameWidth = 1000;
                    int frameHeight = 1000;
                    options.VideoEncoderOptions = new VideoEncoderOptions
                    {
                        FrameSize = new ScreenSize(frameWidth, frameHeight)
                    };
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
                        recordingResetEvent.WaitOne(DefaultRecordingLengthMillis);
                        rec.Stop();
                        finalizeResetEvent.WaitOne(5000);
                        outStream.Flush();
                        Assert.IsFalse(isError, error);
                        Assert.IsTrue(isComplete);
                        Assert.AreNotEqual(outStream.Length, 0);
                        Assert.IsTrue(new FileInfo(filePath).Length > 0);
                        var mediaInfo = new MediaInfoWrapper(filePath);
                        Assert.IsTrue(mediaInfo.Format == "MPEG-4");
                        Assert.IsTrue(mediaInfo.Width == frameWidth && mediaInfo.Height == frameHeight, "Expected and actual output dimensions differ");
                    }
                }
            }
            finally
            {
                File.Delete(filePath);
            }
        }

        [TestMethod]
        public void RecordingWithOutputCropAndCustomOutputFrameSizeTest()
        {
            string filePath = Path.Combine(GetTempPath(), Path.ChangeExtension(Path.GetRandomFileName(), ".mp4"));
            try
            {
                using (var outStream = File.Open(filePath, FileMode.Create, FileAccess.ReadWrite, FileShare.Read))
                {
                    RecorderOptions options = new RecorderOptions();
                    int frameWidth = 1000;
                    int frameHeight = 1000;
                    var sourceRect = new ScreenRect(100, 100, 500, 500);
                    options.SourceOptions = new SourceOptions
                    {
                        RecordingSources = { DisplayRecordingSource.MainMonitor },
                        SourceRect = sourceRect
                    };
                    options.VideoEncoderOptions = new VideoEncoderOptions
                    {
                        FrameSize = new ScreenSize(frameWidth, frameHeight),
                    };
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
                        recordingResetEvent.WaitOne(DefaultRecordingLengthMillis);
                        rec.Stop();
                        finalizeResetEvent.WaitOne(5000);
                        outStream.Flush();
                        Assert.IsFalse(isError, error);
                        Assert.IsTrue(isComplete);
                        Assert.AreNotEqual(outStream.Length, 0);
                        Assert.IsTrue(new FileInfo(filePath).Length > 0);
                        var mediaInfo = new MediaInfoWrapper(filePath);
                        Assert.IsTrue(mediaInfo.Format == "MPEG-4");
                        Assert.IsTrue(mediaInfo.Width == frameWidth && mediaInfo.Height == frameHeight, "Expected and actual output dimensions differ");
                    }
                }
            }
            finally
            {
                File.Delete(filePath);
            }
        }

        [TestMethod]
        public void RecordingWithSourceOutputSizeTest()
        {
            string filePath = Path.Combine(GetTempPath(), Path.ChangeExtension(Path.GetRandomFileName(), ".mp4"));
            try
            {
                using (var outStream = File.Open(filePath, FileMode.Create, FileAccess.ReadWrite, FileShare.Read))
                {
                    RecorderOptions options = new RecorderOptions();
                    int frameWidth = 500;
                    int frameHeight = 500;
                    options.SourceOptions = new SourceOptions
                    {
                        RecordingSources = new List<RecordingSourceBase>
                        {
                            new DisplayRecordingSource
                            {
                                DeviceName = DisplayRecordingSource.MainMonitor.DeviceName,
                                OutputSize = new ScreenSize(frameWidth, frameHeight)
                            }
                        }
                    };
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
                        recordingResetEvent.WaitOne(DefaultRecordingLengthMillis);
                        rec.Stop();
                        finalizeResetEvent.WaitOne(5000);
                        outStream.Flush();
                        Assert.IsFalse(isError, error);
                        Assert.IsTrue(isComplete);
                        Assert.AreNotEqual(outStream.Length, 0);
                        Assert.IsTrue(new FileInfo(filePath).Length > 0);
                        var mediaInfo = new MediaInfoWrapper(filePath);
                        Assert.IsTrue(mediaInfo.Format == "MPEG-4");
                        Assert.IsTrue(mediaInfo.Width == frameWidth && mediaInfo.Height == frameHeight, "Expected and actual output dimensions differ");
                    }
                }
            }
            finally
            {
                File.Delete(filePath);
            }
        }

        [TestMethod]
        public void RecordingWithSourceCropAndSourceOutputSizeTest()
        {
            string filePath = Path.Combine(GetTempPath(), Path.ChangeExtension(Path.GetRandomFileName(), ".mp4"));
            try
            {
                using (var outStream = File.Open(filePath, FileMode.Create, FileAccess.ReadWrite, FileShare.Read))
                {
                    RecorderOptions options = new RecorderOptions();
                    int frameWidth = 500;
                    int frameHeight = 500;
                    var sourceRect = new ScreenRect(0, 0, frameWidth, frameHeight);
                    options.SourceOptions = new SourceOptions
                    {
                        RecordingSources = new List<RecordingSourceBase>
                        {
                            new DisplayRecordingSource
                            {
                                DeviceName = DisplayRecordingSource.MainMonitor.DeviceName,
                                OutputSize = new ScreenSize(frameWidth, frameHeight),
                                SourceRect = sourceRect
                            }
                        }
                    };
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
                        recordingResetEvent.WaitOne(DefaultRecordingLengthMillis);
                        rec.Stop();
                        finalizeResetEvent.WaitOne(5000);
                        outStream.Flush();
                        Assert.IsFalse(isError, error);
                        Assert.IsTrue(isComplete);
                        Assert.AreNotEqual(outStream.Length, 0);
                        Assert.IsTrue(new FileInfo(filePath).Length > 0);
                        var mediaInfo = new MediaInfoWrapper(filePath);
                        Assert.IsTrue(mediaInfo.Format == "MPEG-4");
                        Assert.IsTrue(mediaInfo.Width == frameWidth && mediaInfo.Height == frameHeight, "Expected and actual output dimensions differ");
                    }
                }
            }
            finally
            {
                File.Delete(filePath);
            }
        }

        [TestMethod]
        public void FixedFramerateTest()
        {
            string filePath = Path.Combine(GetTempPath(), Path.ChangeExtension(Path.GetRandomFileName(), ".mp4"));
            try
            {
                using (var outStream = File.Open(filePath, FileMode.Create, FileAccess.ReadWrite, FileShare.Read))
                {
                    RecorderOptions options = new RecorderOptions();
                    options.VideoEncoderOptions = new VideoEncoderOptions { IsFixedFramerate = true };
                    options.VideoEncoderOptions.Framerate = 10;
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
                        recordingResetEvent.WaitOne(DefaultRecordingLengthMillis);
                        rec.Stop();
                        finalizeResetEvent.WaitOne(5000);
                        outStream.Flush();
                        Assert.IsFalse(isError, error);
                        Assert.IsTrue(isComplete);
                        Assert.AreNotEqual(outStream.Length, 0);
                        var mediaInfo = new MediaInfoWrapper(filePath);
                        Assert.IsTrue(Math.Abs(mediaInfo.Framerate - options.VideoEncoderOptions.Framerate) <= 3, "MediaInfo framerate {0} not equal to configured framerate {1}", mediaInfo.Framerate, options.VideoEncoderOptions.Framerate);
                    }
                }
            }
            finally
            {
                File.Delete(filePath);
            }
        }

        [TestMethod]
        public void CustomFixedBitrateTest()
        {
            string filePath = Path.Combine(GetTempPath(), Path.ChangeExtension(Path.GetRandomFileName(), ".mp4"));
            try
            {
                RecorderOptions options = new RecorderOptions();
                options.VideoEncoderOptions = new VideoEncoderOptions
                {
                    Encoder = new H264VideoEncoder { BitrateMode = H264BitrateControlMode.CBR },
                    IsHardwareEncodingEnabled = false,
                    Bitrate = 1000 * 1000
                };
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

                    rec.Record(filePath);
                    recordingResetEvent.WaitOne(DefaultRecordingLengthMillis);
                    rec.Stop();
                    finalizeResetEvent.WaitOne(5000);
                    Assert.IsFalse(isError, error);
                    Assert.IsTrue(isComplete);
                    Assert.IsTrue(new FileInfo(filePath).Length > 0);
                    var mediaInfo = new MediaInfoWrapper(filePath);
                    Assert.IsTrue(mediaInfo.VideoStreams.Count > 0);
                }
            }
            finally
            {
                File.Delete(filePath);
            }
        }

        [TestMethod]
        public void ScreenshotTest()
        {
            RecorderOptions options = new RecorderOptions();
            options.RecorderMode = RecorderMode.Screenshot;
            string filePath = Path.Combine(GetTempPath(), Path.ChangeExtension(Path.GetRandomFileName(), ".png"));
            try
            {
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
                        recordingResetEvent.Set();
                    };
                    rec.OnRecordingFailed += (s, args) =>
                    {
                        isError = true;
                        error = args.Error;
                        finalizeResetEvent.Set();
                        recordingResetEvent.Set();
                    };

                    rec.Record(filePath);
                    recordingResetEvent.WaitOne(DefaultRecordingLengthMillis);
                    rec.Stop();
                    finalizeResetEvent.WaitOne(5000);

                    Assert.IsFalse(isError, error);
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
            options.RecorderMode = RecorderMode.Screenshot;
            options.SourceOptions = new SourceOptions
            {
                RecordingSources = { DisplayRecordingSource.MainMonitor },
                SourceRect = new ScreenRect(100, 100, 100, 100)
            };
            string filePath = Path.Combine(GetTempPath(), Path.ChangeExtension(Path.GetRandomFileName(), ".png"));
            try
            {
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
                        recordingResetEvent.Set();
                    };
                    rec.OnRecordingFailed += (s, args) =>
                    {
                        isError = true;
                        error = args.Error;
                        finalizeResetEvent.Set();
                        recordingResetEvent.Set();
                    };

                    rec.Record(filePath);
                    recordingResetEvent.WaitOne(DefaultRecordingLengthMillis);
                    rec.Stop();
                    finalizeResetEvent.WaitOne(5000);

                    Assert.IsFalse(isError, error);
                    Assert.IsTrue(isComplete);
                    Assert.IsTrue(new FileInfo(filePath).Length > 0);
                    using (var bitmap = System.Drawing.Bitmap.FromFile(filePath))
                    {
                        Assert.IsTrue(bitmap.Width == options.SourceOptions.SourceRect.Width
                            && bitmap.Height == options.SourceOptions.SourceRect.Height);
                    }
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
                options.SnapshotOptions = new SnapshotOptions { SnapshotsIntervalMillis = 200 };
                Directory.CreateDirectory(directoryPath);
                Assert.IsTrue(Directory.Exists(directoryPath));
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

                    rec.Record(directoryPath);
                    recordingResetEvent.WaitOne(1000);
                    rec.Stop();
                    finalizeResetEvent.WaitOne(5000);

                    Assert.IsFalse(isError, error);
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
        public void RecordingWithH265EncoderTest()
        {
            string filePath = Path.Combine(GetTempPath(), Path.ChangeExtension(Path.GetRandomFileName(), ".mp4"));
            try
            {
                using (var rec = Recorder.CreateRecorder(new RecorderOptions { VideoEncoderOptions = new VideoEncoderOptions { Encoder = new H265VideoEncoder() } }))
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

                    rec.Record(filePath);
                    recordingResetEvent.WaitOne(DefaultRecordingLengthMillis);
                    rec.Stop();
                    finalizeResetEvent.WaitOne(5000);

                    Assert.IsFalse(isError, error);
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
        public void RecordingToStreamWithSnapshotsTest()
        {
            string filePath = Path.Combine(GetTempPath(), Path.ChangeExtension(Path.GetRandomFileName(), ".mp4"));
            string snapshotsDir = Path.ChangeExtension(filePath, null);
            try
            {
                using (var outStream = File.Open(filePath, FileMode.Create, FileAccess.ReadWrite, FileShare.Read))
                {
                    RecorderOptions options = new RecorderOptions();
                    options.SnapshotOptions = new SnapshotOptions
                    {
                        SnapshotsWithVideo = true,
                        SnapshotsIntervalMillis = 2000,
                        SnapshotFormat = ImageFormat.JPEG,
                        SnapshotsDirectory = snapshotsDir
                    };
                    using (var rec = Recorder.CreateRecorder(options))
                    {
                        List<string> snapshotCallbackList = new List<string>();
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
                        rec.OnSnapshotSaved += (s, args) =>
                        {
                            snapshotCallbackList.Add(args.SnapshotPath);
                        };
                        rec.Record(outStream);
                        recordingResetEvent.WaitOne(11900); // 10 < x < 12 sec
                        rec.Stop();
                        finalizeResetEvent.WaitOne(5000);
                        outStream.Flush();
                        Assert.IsFalse(isError, error);
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
                options.SnapshotOptions = new SnapshotOptions { SnapshotsWithVideo = true, SnapshotsIntervalMillis = 2000, SnapshotFormat = ImageFormat.JPEG };
                options.SourceOptions = new SourceOptions
                {
                    RecordingSources = { DisplayRecordingSource.MainMonitor },
                    SourceRect = new ScreenRect(100, 100, 100, 100)
                };
                using (var rec = Recorder.CreateRecorder(options))
                {
                    List<string> snapshotCallbackList = new List<string>();
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
                    rec.OnSnapshotSaved += (s, args) =>
                    {
                        snapshotCallbackList.Add(args.SnapshotPath);
                    };
                    rec.Record(filePath);
                    recordingResetEvent.WaitOne(11900); // 10 < x < 12 sec
                    rec.Stop();
                    finalizeResetEvent.WaitOne(5000);

                    Assert.IsFalse(isError, error);
                    Assert.IsTrue(isComplete);
                    Assert.IsTrue(new FileInfo(filePath).Length > 0);
                    var mediaInfo = new MediaInfoWrapper(filePath);
                    Assert.IsTrue(mediaInfo.Format == "MPEG-4");
                    Assert.IsTrue(mediaInfo.VideoStreams.Count > 0);
                    Assert.IsTrue(mediaInfo.Width == options.SourceOptions.SourceRect.Width && mediaInfo.Height == options.SourceOptions.SourceRect.Height, "Expected and actual output dimensions of video differs");
                    var snapshotsOnDisk = Directory.GetFiles(snapshotsDir);
                    Assert.AreEqual(6, snapshotsOnDisk.Count());  // First snapshot taken at time 0.
                    Assert.IsTrue(Enumerable.SequenceEqual(snapshotCallbackList, snapshotsOnDisk));
                    foreach (var snapshot in snapshotsOnDisk)
                    {
                        var snapshotMediaInfo = new MediaInfoWrapper(snapshot);
                        Assert.IsTrue(snapshotMediaInfo.Format == "JPEG");
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
        public void DefaultRecording30SecondsToFile()
        {
            string filePath = Path.Combine(GetTempPath(), Path.ChangeExtension(Path.GetRandomFileName(), ".mp4"));
            try
            {
                using (var rec = Recorder.CreateRecorder())
                {
                    string error = "";
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
                        error = args.Error;
                        finalizingResetEvent.Set();
                        recordingResetEvent.Set();
                    };
                    int recordingTimeMillis = 30 * 1000;
                    Stopwatch sw = Stopwatch.StartNew();
                    rec.Record(filePath);
                    recordingResetEvent.WaitOne(recordingTimeMillis);
                    rec.Stop();
                    finalizingResetEvent.WaitOne(5000);
                    Assert.IsTrue(sw.ElapsedMilliseconds >= recordingTimeMillis, "recording duration was too short");
                    Assert.IsFalse(isError, error, "recording has error");
                    Assert.IsTrue(isComplete, "recording did not complete");
                    Assert.IsTrue(new FileInfo(filePath).Length > 0, "file length is 0");
                    var mediaInfo = new MediaInfoWrapper(filePath);
                    Assert.IsTrue(mediaInfo.Format == "MPEG-4", "wrong video format");
                    Assert.IsTrue(mediaInfo.VideoStreams.Count > 0, "no video streams found");
                    Assert.IsTrue(Math.Round(mediaInfo.VideoStreams[0].Duration.TotalSeconds) == (int)Math.Round((double)recordingTimeMillis / 1000), "video length does not match recording time");
                }
            }
            finally
            {
                File.Delete(filePath);
            }
        }

        [TestMethod]
        public void TwoSimultaneousGCRecorderInstances()
        {
            string filePath1 = Path.Combine(GetTempPath(), Path.ChangeExtension(Path.GetRandomFileName(), ".mp4"));
            string filePath2 = Path.Combine(GetTempPath(), Path.ChangeExtension(Path.GetRandomFileName(), ".mp4"));
            try
            {
                var recordingSource = DisplayRecordingSource.MainMonitor;
                recordingSource.RecorderApi = RecorderApi.WindowsGraphicsCapture;
                var recording1 = Task.Run(() =>
                {
                    using (var rec = Recorder.CreateRecorder(new RecorderOptions { SourceOptions = new SourceOptions { RecordingSources = { recordingSource } } }))
                    {
                        string error = "";
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
                            error = args.Error;
                            finalizingResetEvent.Set();
                            recordingResetEvent.Set();
                        };

                        rec.Record(filePath1);


                        recordingResetEvent.WaitOne(DefaultRecordingLengthMillis);
                        rec.Stop();
                        finalizingResetEvent.WaitOne(5000);
                        Assert.IsFalse(isError, error);
                        Assert.IsTrue(isComplete);
                        Assert.IsTrue(new FileInfo(filePath1).Length > 0);
                        var mediaInfo = new MediaInfoWrapper(filePath1);
                        Assert.IsTrue(mediaInfo.Format == "MPEG-4");
                        Assert.IsTrue(mediaInfo.VideoStreams.Count > 0);
                    }
                });
                var recording2 = Task.Run(() =>
                {
                    using (var rec2 = Recorder.CreateRecorder(new RecorderOptions { SourceOptions = new SourceOptions { RecordingSources = { recordingSource } } }))
                    {
                        string error2 = "";
                        bool isError2 = false;
                        bool isComplete2 = false;
                        ManualResetEvent finalizingResetEvent2 = new ManualResetEvent(false);
                        ManualResetEvent recordingResetEvent2 = new ManualResetEvent(false);
                        rec2.OnRecordingComplete += (s, args) =>
                        {
                            isComplete2 = true;
                            finalizingResetEvent2.Set();
                        };
                        rec2.OnRecordingFailed += (s, args) =>
                        {
                            isError2 = true;
                            error2 = args.Error;
                            finalizingResetEvent2.Set();
                            recordingResetEvent2.Set();
                        };

                        rec2.Record(filePath2);
                        recordingResetEvent2.WaitOne(DefaultRecordingLengthMillis);
                        rec2.Stop();
                        finalizingResetEvent2.WaitOne(5000);
                        Assert.IsFalse(isError2, error2);
                        Assert.IsTrue(isComplete2);
                        Assert.IsTrue(new FileInfo(filePath2).Length > 0);
                        var mediaInfo2 = new MediaInfoWrapper(filePath2);
                        Assert.IsTrue(mediaInfo2.Format == "MPEG-4");
                        Assert.IsTrue(mediaInfo2.VideoStreams.Count > 0);
                    }
                });
                Task.WaitAll(recording1, recording2);
            }
            finally
            {
                File.Delete(filePath1);
                File.Delete(filePath2);
            }
        }
        [TestMethod]
        public void TwoSimultaneousGCAndDDRecorderInstances()
        {
            string filePath1 = Path.Combine(GetTempPath(), Path.ChangeExtension(Path.GetRandomFileName(), ".mp4"));
            string filePath2 = Path.Combine(GetTempPath(), Path.ChangeExtension(Path.GetRandomFileName(), ".mp4"));
            try
            {

                var recording1 = Task.Run(() =>
                {
                    var recordingSource = DisplayRecordingSource.MainMonitor;
                    recordingSource.RecorderApi = RecorderApi.WindowsGraphicsCapture;
                    using (var rec = Recorder.CreateRecorder(new RecorderOptions { SourceOptions = new SourceOptions { RecordingSources = { recordingSource } } }))
                    {
                        string error = "";
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
                            error = args.Error;
                            finalizingResetEvent.Set();
                            recordingResetEvent.Set();
                        };

                        rec.Record(filePath1);


                        recordingResetEvent.WaitOne(DefaultRecordingLengthMillis);
                        rec.Stop();
                        finalizingResetEvent.WaitOne(5000);
                        Assert.IsFalse(isError, error);
                        Assert.IsTrue(isComplete);
                        Assert.IsTrue(new FileInfo(filePath1).Length > 0);
                        var mediaInfo = new MediaInfoWrapper(filePath1);
                        Assert.IsTrue(mediaInfo.Format == "MPEG-4");
                        Assert.IsTrue(mediaInfo.VideoStreams.Count > 0);
                    }
                });
                var recording2 = Task.Run(() =>
                {
                    var recordingSource = DisplayRecordingSource.MainMonitor;
                    recordingSource.RecorderApi = RecorderApi.DesktopDuplication;
                    using (var rec2 = Recorder.CreateRecorder(new RecorderOptions { SourceOptions = new SourceOptions { RecordingSources = { recordingSource } } }))
                    {
                        string error2 = "";
                        bool isError2 = false;
                        bool isComplete2 = false;
                        ManualResetEvent finalizingResetEvent2 = new ManualResetEvent(false);
                        ManualResetEvent recordingResetEvent2 = new ManualResetEvent(false);
                        rec2.OnRecordingComplete += (s, args) =>
                        {
                            isComplete2 = true;
                            finalizingResetEvent2.Set();
                        };
                        rec2.OnRecordingFailed += (s, args) =>
                        {
                            isError2 = true;
                            error2 = args.Error;
                            finalizingResetEvent2.Set();
                            recordingResetEvent2.Set();
                        };

                        rec2.Record(filePath2);
                        recordingResetEvent2.WaitOne(DefaultRecordingLengthMillis);
                        rec2.Stop();
                        finalizingResetEvent2.WaitOne(5000);
                        Assert.IsFalse(isError2, error2);
                        Assert.IsTrue(isComplete2);
                        Assert.IsTrue(new FileInfo(filePath2).Length > 0);
                        var mediaInfo2 = new MediaInfoWrapper(filePath2);
                        Assert.IsTrue(mediaInfo2.Format == "MPEG-4");
                        Assert.IsTrue(mediaInfo2.VideoStreams.Count > 0);
                    }
                });
                Task.WaitAll(recording1, recording2);
            }
            finally
            {
                File.Delete(filePath1);
                File.Delete(filePath2);
            }
        }

        [TestMethod]
        public void Run25RecordingsTestWithDifferentInstances()
        {
            for (int i = 0; i < 25; i++)
            {
                using (Stream outStream = new MemoryStream())
                {
                    RecorderOptions options = new RecorderOptions();
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
                        recordingResetEvent.WaitOne(1000);
                        rec.Stop();

                        Assert.IsTrue(finalizeResetEvent.WaitOne(5000), $"[{i}] Recording finalize timed out");
                        outStream.Flush();
                        Assert.IsNotNull(outStream, $"[{i}] Outstream is null");
                        Assert.IsFalse(isError, $"[{i}]: " + error);
                        Assert.IsTrue(isComplete, $"[{i}] Recording not complete");
                        Assert.AreNotEqual(outStream.Length, 0, $"[{i}] Outstream length is 0");
                    }
                }
            }
        }
        [TestMethod]
        public void Run25RecordingsTestWithOneInstance()
        {
            string error = "";
            bool isError = false;
            bool isComplete = false;
            AutoResetEvent finalizeResetEvent = new AutoResetEvent(false);
            ManualResetEvent recordingResetEvent = new ManualResetEvent(false);
            RecorderOptions options = new RecorderOptions();
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
                for (int i = 0; i < 25; i++)
                {
                    using (var outStream = new MemoryStream())
                    {
                        isError = false;
                        isComplete = false;
                        rec.Record(outStream);
                        recordingResetEvent.WaitOne(1000);
                        rec.Stop();

                        Assert.IsTrue(finalizeResetEvent.WaitOne(5000), $"[{i}] Recording finalize timed out");
                        outStream.Flush();
                        Assert.IsNotNull(outStream, $"[{i}] Outstream is null");
                        Assert.IsFalse(isError, $"[{i}]: " + error);
                        Assert.IsTrue(isComplete, $"[{i}] Recording not complete");
                        Assert.AreNotEqual(outStream.Length, 0, $"[{i}] Outstream length is 0");
                    }
                }
            }
        }
        [TestMethod]
        public void RecordingWithOverlays()
        {
            string filePath = Path.Combine(GetTempPath(), Path.ChangeExtension(Path.GetRandomFileName(), ".mp4"));
            try
            {
                var overlays = new List<RecordingOverlayBase>();
                overlays.Add(new VideoOverlay
                {
                    AnchorPosition = Anchor.TopRight,
                    SourcePath = @"testmedia\cat.mp4",
                    Size = new ScreenSize(0, 200),
                    Offset = new ScreenSize(50, 50)
                });
                overlays.Add(new ImageOverlay
                {
                    AnchorPosition = Anchor.BottomLeft,
                    SourcePath = @"testmedia\alphatest.png",
                    Size = new ScreenSize(0, 300),
                    Offset = new ScreenSize(0, 0)
                });
                overlays.Add(new ImageOverlay
                {
                    AnchorPosition = Anchor.BottomRight,
                    SourcePath = @"testmedia\giftest.gif",
                    Size = new ScreenSize(0, 300),
                    Offset = new ScreenSize(75, 25)
                });
                if (Recorder.GetWindows().Where(x => x.IsValidWindow() && !x.IsMinmimized()).Count() > 0)
                {
                    overlays.Add(new WindowOverlay
                    {
                        AnchorPosition = Anchor.BottomRight,
                        Handle = Recorder.GetWindows().FirstOrDefault(x => x.IsValidWindow() && !x.IsMinmimized()).Handle,
                        Size = new ScreenSize(0, 300),
                        Offset = new ScreenSize(75, 25)
                    });
                }
                overlays.Add(new DisplayOverlay
                {
                    AnchorPosition = Anchor.BottomRight,
                    Size = new ScreenSize(0, 300),
                    Offset = new ScreenSize(75, 25)
                });
                RecorderOptions options = new RecorderOptions();
                options.OverlayOptions = new OverLayOptions { Overlays = overlays };
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

                    rec.Record(filePath);
                    recordingResetEvent.WaitOne(DefaultRecordingLengthMillis);
                    rec.Stop();
                    finalizeResetEvent.WaitOne(5000);

                    Assert.IsFalse(isError, error);
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
        public void RecordWindowTest()
        {
            string filePath = Path.Combine(GetTempPath(), Path.ChangeExtension(Path.GetRandomFileName(), ".mp4"));
            try
            {
                RecorderOptions options = new RecorderOptions
                {
                    SourceOptions = new SourceOptions
                    {
                        RecordingSources = { { Recorder.GetWindows().FirstOrDefault(x => x.IsValidWindow() && !x.IsMinmimized()) } }
                    }
                };
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

                    rec.Record(filePath);
                    recordingResetEvent.WaitOne(DefaultRecordingLengthMillis);
                    rec.Stop();
                    finalizeResetEvent.WaitOne(5000);

                    Assert.IsFalse(isError, error);
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
        public void RecordMultipleSourcesTest()
        {
            string filePath = Path.Combine(GetTempPath(), Path.ChangeExtension(Path.GetRandomFileName(), ".mp4"));
            try
            {
                var sources = new List<RecordingSourceBase>();
                sources.Add(DisplayRecordingSource.MainMonitor);
                sources.Add(Recorder.GetWindows().FirstOrDefault(x => x.IsValidWindow() && !x.IsMinmimized()));
                sources.Add(new VideoRecordingSource(@"testmedia\cat.mp4"));
                sources.Add(new ImageRecordingSource(@"testmedia\earth.gif"));
                foreach (var source in sources)
                {
                    source.OutputSize = new ScreenSize(500, 0);
                }
                var coordinates = Recorder.GetOutputDimensionsForRecordingSources(sources);
                RecorderOptions options = new RecorderOptions
                {
                    SourceOptions = new SourceOptions
                    {
                        RecordingSources = sources,
                    },
                    VideoEncoderOptions = new VideoEncoderOptions
                    {
                        IsHardwareEncodingEnabled = false
                    }
                };
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

                    rec.Record(filePath);
                    recordingResetEvent.WaitOne(DefaultRecordingLengthMillis);
                    rec.Stop();
                    finalizeResetEvent.WaitOne(5000);

                    Assert.IsFalse(isError, error, "recording has error");
                    Assert.IsTrue(isComplete, "recording failed to complete");
                    Assert.IsTrue(new FileInfo(filePath).Length > 0, "file length is 0");
                    var mediaInfo = new MediaInfoWrapper(filePath);
                    Assert.IsTrue(mediaInfo.Format == "MPEG-4");
                    Assert.IsTrue(mediaInfo.VideoStreams.Count > 0, "no video streams found");
                    Assert.IsTrue(mediaInfo.Width == coordinates.CombinedOutputSize.Width && mediaInfo.Height == coordinates.CombinedOutputSize.Height, "videos resolution {0}x{1} differs from source dimension {2}x{3}", mediaInfo.Width, mediaInfo.Height, coordinates.CombinedOutputSize.Width, coordinates.CombinedOutputSize.Height);
                }
            }
            finally
            {
                File.Delete(filePath);
            }
        }
    }
}
