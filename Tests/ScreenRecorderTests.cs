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
        const int DefaultMaxRecordingLengthMillis = 3000;

        private string GetTempPath()
        {
            string path = Path.Combine(Path.GetTempPath(), "ScreenRecorder", "Tests");
            Directory.CreateDirectory(path);
            return path;
        }

        [TestMethod]
        public void DefaultRecordingToStream()
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
                        rec.OnStatusChanged += (s, args) =>
                        {
                            if (args.Status == RecorderStatus.Recording)
                            {
                                recordingResetEvent.Set();
                            }
                        };
                        rec.Record(outStream);
                        recordingResetEvent.WaitOne(DefaultMaxRecordingLengthMillis);
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
                            rec.OnStatusChanged += (s, args) =>
                            {
                                if (args.Status == RecorderStatus.Recording)
                                {
                                    recordingResetEvent.Set();
                                }
                            };
                            rec.Record(outStream);
                            recordingResetEvent.WaitOne(DefaultMaxRecordingLengthMillis);
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
        public void QualitySetting()
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
                    rec.OnStatusChanged += (s, args) =>
                    {
                        if (args.Status == RecorderStatus.Recording)
                        {
                            recordingResetEvent.Set();
                        }
                    };
                    rec.Record(outStream);
                    recordingResetEvent.WaitOne(DefaultMaxRecordingLengthMillis);
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
                    rec.OnStatusChanged += (s, args) =>
                    {
                        if (args.Status == RecorderStatus.Recording)
                        {
                            recordingResetEvent.Set();
                        }
                    };
                    rec.Record(outStream);
                    recordingResetEvent.WaitOne(DefaultMaxRecordingLengthMillis);
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
        public void SoftwareEncoding()
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
                    rec.OnStatusChanged += (s, args) =>
                    {
                        if (args.Status == RecorderStatus.Recording)
                        {
                            recordingResetEvent.Set();
                        }
                    };
                    rec.Record(outStream);
                    recordingResetEvent.WaitOne(DefaultMaxRecordingLengthMillis);
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
        public void RecordingWithAudioInput()
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
                        rec.OnStatusChanged += (s, args) =>
                        {
                            if (args.Status == RecorderStatus.Recording)
                            {
                                recordingResetEvent.Set();
                            }
                        };
                        rec.Record(outStream);
                        recordingResetEvent.WaitOne(DefaultMaxRecordingLengthMillis);
                        rec.Stop();
                        finalizeResetEvent.WaitOne(5000);
                        outStream.Flush();
                        Assert.IsFalse(isError, error);
                        Assert.IsTrue(isComplete);
                        Assert.AreNotEqual(outStream.Length, 0);
                        var mediaInfo = new MediaInfoWrapper(filePath);
                        Assert.IsTrue(mediaInfo.AudioStreams.Count > 0);
                    }
                }
            }
            finally
            {
                File.Delete(filePath);
            }
        }

        [TestMethod]
        public void RecordingWithAudioMixing()
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
                        rec.OnStatusChanged += (s, args) =>
                        {
                            if (args.Status == RecorderStatus.Recording)
                            {
                                recordingResetEvent.Set();
                            }
                        };
                        rec.Record(outStream);
                        recordingResetEvent.WaitOne(DefaultMaxRecordingLengthMillis);
                        rec.Stop();
                        finalizeResetEvent.WaitOne(5000);
                        outStream.Flush();
                        Assert.IsFalse(isError, error);
                        Assert.IsTrue(isComplete);
                        Assert.AreNotEqual(outStream.Length, 0);
                        var mediaInfo = new MediaInfoWrapper(filePath);
                        Assert.IsTrue(mediaInfo.AudioStreams.Count > 0);
                    }
                }
            }
            finally
            {
                File.Delete(filePath);
            }
        }

        [TestMethod]
        public void RecordingWithOutputCropAndCustomFrameSize()
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
                    options.OutputOptions = new OutputOptions
                    {
                        OutputFrameSize = new ScreenSize(frameWidth, frameHeight),
                        SourceRect = sourceRect
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
                        rec.OnStatusChanged += (s, args) =>
                        {
                            if (args.Status == RecorderStatus.Recording)
                            {
                                recordingResetEvent.Set();
                            }
                        };
                        rec.Record(outStream);
                        recordingResetEvent.WaitOne(DefaultMaxRecordingLengthMillis);
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

        [DataTestMethod]
        [DynamicData(nameof(GetRecordingSources), DynamicDataSourceType.Method)]
        public void RecordingWithCustomSourceDimensionsAndPositions(IEnumerable<RecordingSourceBase> recordingSources, ScreenSize expectedSize)
        {
            string filePath = Path.Combine(GetTempPath(), Path.ChangeExtension(Path.GetRandomFileName(), ".mp4"));
            try
            {
                using (var outStream = File.Open(filePath, FileMode.Create, FileAccess.ReadWrite, FileShare.Read))
                {
                    RecorderOptions options = new RecorderOptions();
                    options.SourceOptions = new SourceOptions
                    {
                        RecordingSources = recordingSources.ToList()
                    };
                    ScreenSize calculatedSize = Recorder.GetOutputDimensionsForRecordingSources(recordingSources).CombinedOutputSize;
                    Assert.AreEqual(calculatedSize, expectedSize);
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
                        rec.OnStatusChanged += (s, args) =>
                        {
                            if (args.Status == RecorderStatus.Recording)
                            {
                                recordingResetEvent.Set();
                            }
                        };
                        rec.Record(outStream);
                        recordingResetEvent.WaitOne(DefaultMaxRecordingLengthMillis);
                        rec.Stop();
                        finalizeResetEvent.WaitOne(5000);
                        outStream.Flush();
                        Assert.IsFalse(isError, error);
                        Assert.IsTrue(isComplete);
                        Assert.AreNotEqual(outStream.Length, 0);
                        Assert.IsTrue(new FileInfo(filePath).Length > 0);
                        var mediaInfo = new MediaInfoWrapper(filePath);
                        Assert.IsTrue(mediaInfo.Format == "MPEG-4");
                        Assert.IsTrue(mediaInfo.Width == expectedSize.Width && mediaInfo.Height == expectedSize.Height, "Expected and actual output dimensions differ");
                    }
                }
            }
            finally
            {
                File.Delete(filePath);
            }
        }
        public static IEnumerable<object[]> GetRecordingSources()
        {
            yield return new object[] {
               new List<RecordingSourceBase>()
               {
                   new DisplayRecordingSource
                            {
                                DeviceName = DisplayRecordingSource.MainMonitor.DeviceName,
                                OutputSize = new ScreenSize(500, 500),
                                SourceRect = new ScreenRect(100,100,500,500)
                            }
               },
               new ScreenSize(500,500)
            };
            yield return new object[] {
               new List<RecordingSourceBase>()
               {
                   new DisplayRecordingSource
                            {
                                DeviceName = DisplayRecordingSource.MainMonitor.DeviceName,
                                OutputSize = new ScreenSize(500, 500),
                                SourceRect = new ScreenRect(100,100,500,500)
                            },
                            new ImageRecordingSource
                            {
                               SourcePath=@"testmedia\earth.gif" //400x400px
                            }
               },
               new ScreenSize(900,500)
            };
            yield return new object[] {
               new List<RecordingSourceBase>()
               {
                   new DisplayRecordingSource
                            {
                                DeviceName = DisplayRecordingSource.MainMonitor.DeviceName,
                                OutputSize = new ScreenSize(500, 500),
                                SourceRect = new ScreenRect(100,100,500,500),
                                Position = new ScreenPoint(50,50)
                            },
                            new ImageRecordingSource
                            {
                               SourcePath=@"testmedia\earth.gif"//400x400px
                            }
               },
               new ScreenSize(950,550)
            };
            yield return new object[] {
               new List<RecordingSourceBase>()
               {
                   new DisplayRecordingSource
                            {
                                DeviceName = DisplayRecordingSource.MainMonitor.DeviceName,
                                OutputSize = new ScreenSize(500, 500),
                                SourceRect = new ScreenRect(100,100,500,500),
                                Position = new ScreenPoint(50,50)
                            },
                            new ImageRecordingSource
                            {
                               SourcePath=@"testmedia\earth.gif",//400x400px
                               OutputSize = new ScreenSize(500,500)
                            }
               },
               new ScreenSize(1050,550)
            };
            yield return new object[] {
               new List<RecordingSourceBase>()
               {
                   new DisplayRecordingSource
                            {
                                DeviceName = DisplayRecordingSource.MainMonitor.DeviceName,
                                OutputSize = new ScreenSize(500, 500),
                                SourceRect = new ScreenRect(100,100,500,500),
                                Position = new ScreenPoint(50,50)
                            },
                            new ImageRecordingSource
                            {
                               SourcePath=@"testmedia\earth.gif",//400x400px
                               OutputSize = new ScreenSize(500,500),
                               Position = new ScreenPoint(600,0)
                            }
               },
               new ScreenSize(1100,550)
            };
            yield return new object[] {
               new List<RecordingSourceBase>()
               {
                   new DisplayRecordingSource
                            {
                                DeviceName = DisplayRecordingSource.MainMonitor.DeviceName,
                                OutputSize = new ScreenSize(500, 500),
                                SourceRect = new ScreenRect(100,100,500,500),
                                Position = new ScreenPoint(50,50)
                            },
                            new ImageRecordingSource
                            {
                               SourcePath=@"testmedia\earth.gif",//400x400px
                               OutputSize = new ScreenSize(500,500),
                               Position = new ScreenPoint(0,550)
                            }
               },
               new ScreenSize(550,1050)
            };
            yield return new object[] {
               new List<RecordingSourceBase>()
               {
                   new DisplayRecordingSource
                            {
                                DeviceName = DisplayRecordingSource.MainMonitor.DeviceName,
                                OutputSize = new ScreenSize(500, 500),
                                SourceRect = new ScreenRect(100,100,500,500),
                                Position = new ScreenPoint(50,50)
                            },
                            new ImageRecordingSource
                            {
                               SourcePath=@"testmedia\earth.gif",//400x400px
                               OutputSize = new ScreenSize(500,500),
                               Position = new ScreenPoint(0,550)
                            },
                            new VideoRecordingSource
                            {
                               SourcePath=@"testmedia\cat.mp4",//480x480px
                            }
               },
               new ScreenSize(1030,1050)
            };
            yield return new object[] {
               new List<RecordingSourceBase>()
               {
                   new DisplayRecordingSource
                            {
                                DeviceName = DisplayRecordingSource.MainMonitor.DeviceName,
                                OutputSize = new ScreenSize(500, 500)
                            },
                            new ImageRecordingSource
                            {
                               SourcePath=@"testmedia\earth.gif",//400x400px
                               Position = new ScreenPoint(-400,-400)
                            }
               },
               new ScreenSize(900,900)
            };
        }

        [TestMethod]
        public void FixedFramerate()
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
                        int durationMillis = 5000;
                        rec.Record(outStream);
                        recordingResetEvent.WaitOne(durationMillis);
                        rec.Stop();
                        finalizeResetEvent.WaitOne(5000);
                        outStream.Flush();
                        Assert.IsFalse(isError, error);
                        Assert.IsTrue(isComplete);
                        Assert.AreNotEqual(outStream.Length, 0);
                        var mediaInfo = new MediaInfoWrapper(filePath);
                        Assert.AreEqual(rec.CurrentFrameNumber, options.VideoEncoderOptions.Framerate * (durationMillis / 1000));
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
        public void CustomFixedBitrate()
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
                    rec.OnStatusChanged += (s, args) =>
                    {
                        if (args.Status == RecorderStatus.Recording)
                        {
                            recordingResetEvent.Set();
                        }
                    };
                    rec.Record(filePath);
                    recordingResetEvent.WaitOne(DefaultMaxRecordingLengthMillis);
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
        [DataRow(RecorderApi.DesktopDuplication)]
        [DataRow(RecorderApi.WindowsGraphicsCapture)]
        public void Screenshot(RecorderApi api)
        {
            RecorderOptions options = new RecorderOptions();
            options.OutputOptions = new OutputOptions { RecorderMode = RecorderMode.Screenshot };
            options.SourceOptions = new SourceOptions { RecordingSources = { new DisplayRecordingSource { DeviceName = DisplayRecordingSource.MainMonitor.DeviceName, RecorderApi = api } } };
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
                    recordingResetEvent.WaitOne(DefaultMaxRecordingLengthMillis);
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
        public void ScreenshotWithCropping()
        {
            RecorderOptions options = new RecorderOptions();
            options.OutputOptions = new OutputOptions
            {
                RecorderMode = RecorderMode.Screenshot,
                SourceRect = new ScreenRect(100, 100, 200, 200)
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
                    recordingResetEvent.WaitOne(DefaultMaxRecordingLengthMillis);
                    rec.Stop();
                    finalizeResetEvent.WaitOne(5000);

                    Assert.IsFalse(isError, error);
                    Assert.IsTrue(isComplete);
                    Assert.IsTrue(new FileInfo(filePath).Length > 0);
                    using (var bitmap = System.Drawing.Bitmap.FromFile(filePath))
                    {
                        Assert.IsTrue(bitmap.Width == options.OutputOptions.SourceRect.Width
                            && bitmap.Height == options.OutputOptions.SourceRect.Height);
                    }
                }
            }
            finally
            {
                File.Delete(filePath);
            }
        }

        [TestMethod]
        public void Slideshow()
        {
            string directoryPath = Path.Combine(GetTempPath(), Path.GetFileNameWithoutExtension(Path.GetRandomFileName()));
            try
            {
                RecorderOptions options = new RecorderOptions();
                options.OutputOptions = new OutputOptions { RecorderMode = RecorderMode.Slideshow };
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
                    recordingResetEvent.WaitOne(DefaultMaxRecordingLengthMillis);
                    rec.Stop();
                    finalizeResetEvent.WaitOne(5000);

                    Assert.IsFalse(isError, error);
                    Assert.IsTrue(isComplete);
                    var files = Directory.GetFiles(directoryPath);
                    //First image is written immediately, then there should be one per interval.
                    Assert.IsTrue(files.Length == 1 + (DefaultMaxRecordingLengthMillis / options.SnapshotOptions.SnapshotsIntervalMillis));
                    foreach (string filePath in files)
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
        public void RecordingWithH265Encoder()
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
                    rec.OnStatusChanged += (s, args) =>
                    {
                        if (args.Status == RecorderStatus.Recording)
                        {
                            recordingResetEvent.Set();
                        }
                    };
                    rec.Record(filePath);
                    recordingResetEvent.WaitOne(DefaultMaxRecordingLengthMillis);
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
        public void RecordingToFileWithSnapshotsAndCropping()
        {
            string filePath = Path.Combine(GetTempPath(), Path.ChangeExtension(Path.GetRandomFileName(), ".mp4"));
            string snapshotsDir = Path.ChangeExtension(filePath, null);
            try
            {
                RecorderOptions options = new RecorderOptions();
                options.SnapshotOptions = new SnapshotOptions { SnapshotsWithVideo = true, SnapshotsIntervalMillis = 2000, SnapshotFormat = ImageFormat.JPEG };
                options.OutputOptions = new OutputOptions
                {
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
                    Assert.IsTrue(mediaInfo.Width == options.OutputOptions.SourceRect.Width && mediaInfo.Height == options.OutputOptions.SourceRect.Height, "Expected and actual output dimensions of video differs");
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
        [DataRow(RecorderApi.WindowsGraphicsCapture, RecorderApi.WindowsGraphicsCapture)]
        [DataRow(RecorderApi.DesktopDuplication, RecorderApi.WindowsGraphicsCapture)]
        public void TwoSimultaneousRecorderInstances(RecorderApi r1, RecorderApi r2)
        {
            string filePath1 = Path.Combine(GetTempPath(), Path.ChangeExtension(Path.GetRandomFileName(), ".mp4"));
            string filePath2 = Path.Combine(GetTempPath(), Path.ChangeExtension(Path.GetRandomFileName(), ".mp4"));
            try
            {
                var recording1 = Task.Run(() =>
                {
                    var recordingSource = DisplayRecordingSource.MainMonitor;
                    recordingSource.RecorderApi = r1;
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
                        rec.OnStatusChanged += (s, args) =>
                        {
                            if (args.Status == RecorderStatus.Recording)
                            {
                                recordingResetEvent.Set();
                            }
                        };
                        rec.Record(filePath1);


                        recordingResetEvent.WaitOne(DefaultMaxRecordingLengthMillis);
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
                    recordingSource.RecorderApi = r2;
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
                        rec2.OnStatusChanged += (s, args) =>
                        {
                            if (args.Status == RecorderStatus.Recording)
                            {
                                recordingResetEvent2.Set();
                            }
                        };
                        rec2.Record(filePath2);
                        recordingResetEvent2.WaitOne(DefaultMaxRecordingLengthMillis);
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
        public void Run25RecordingsWithUniqueInstances()
        {
            for (int i = 0; i < 25; i++)
            {
                string filePath = Path.Combine(GetTempPath(), Path.ChangeExtension(Path.GetRandomFileName(), ".mp4"));
                try
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
                            recordingResetEvent.Set();
                        };
                        rec.OnRecordingFailed += (s, args) =>
                        {
                            isError = true;
                            error = args.Error;
                            finalizeResetEvent.Set();
                            recordingResetEvent.Set();
                        };
                        rec.OnStatusChanged += (s, args) =>
                        {
                            if (args.Status == RecorderStatus.Recording)
                            {
                                rec.Stop();
                            }
                        };
                        rec.Record(filePath);
                        recordingResetEvent.WaitOne(1000);

                        Assert.IsTrue(finalizeResetEvent.WaitOne(1000), $"[{i}] Recording finalize timed out");
                        Assert.IsFalse(isError, $"[{i}]: " + error);
                        Assert.IsTrue(isComplete, $"[{i}] Recording not complete");
                        Assert.IsTrue(new FileInfo(filePath).Length > 0);
                        var mediaInfo = new MediaInfoWrapper(filePath);
                        Assert.IsTrue(mediaInfo.Format == "MPEG-4");
                        Assert.IsTrue(mediaInfo.VideoStreams.Count > 0);
                        Assert.IsTrue(mediaInfo.Duration > 0);
                    }
                }
                finally
                {
                    File.Delete(filePath);
                }
            }
        }
        [TestMethod]
        public void Run25RecordingsWithOneInstance()
        {
            string error = "";
            bool isError = false;
            bool isComplete = false;
            AutoResetEvent finalizeResetEvent = new AutoResetEvent(false);
            ManualResetEvent recordingResetEvent = new ManualResetEvent(false);
            RecorderOptions options = new RecorderOptions();
            options.AudioOptions = new AudioOptions { IsAudioEnabled = true };
            using (var rec = Recorder.CreateRecorder(options))
            {
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
                rec.OnStatusChanged += (s, args) =>
                {
                    if (args.Status == RecorderStatus.Recording)
                    {
                        rec.Stop();
                    }
                };
                for (int i = 0; i < 25; i++)
                {
                    string filePath = Path.Combine(GetTempPath(), Path.ChangeExtension(Path.GetRandomFileName(), ".mp4"));
                    try
                    {
                        isError = false;
                        isComplete = false;
                        rec.Record(filePath);
                        recordingResetEvent.WaitOne(1000);

                        Assert.IsTrue(finalizeResetEvent.WaitOne(1000), $"[{i}] Recording finalize timed out");
                        Assert.IsFalse(isError, $"[{i}]: " + error);
                        Assert.IsTrue(isComplete, $"[{i}] Recording not complete");
                        Assert.IsTrue(new FileInfo(filePath).Length > 0);
                        var mediaInfo = new MediaInfoWrapper(filePath);
                        Assert.IsTrue(mediaInfo.Format == "MPEG-4");
                        Assert.IsTrue(mediaInfo.VideoStreams.Count > 0);
                        Assert.IsTrue(mediaInfo.Duration > 0);
                    }
                    finally
                    {
                        File.Delete(filePath);
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
                    AnchorPoint = Anchor.TopRight,
                    SourcePath = @"testmedia\cat.mp4",
                    Size = new ScreenSize(0, 200),
                    Offset = new ScreenSize(50, 50)
                });
                overlays.Add(new ImageOverlay
                {
                    AnchorPoint = Anchor.BottomLeft,
                    SourcePath = @"testmedia\alphatest.png",
                    Size = new ScreenSize(0, 300),
                    Offset = new ScreenSize(0, 0)
                });
                overlays.Add(new ImageOverlay
                {
                    AnchorPoint = Anchor.BottomRight,
                    SourcePath = @"testmedia\giftest.gif",
                    Size = new ScreenSize(0, 300),
                    Offset = new ScreenSize(75, 25)
                });
                if (Recorder.GetWindows().Where(x => x.IsValidWindow() && !x.IsMinmimized()).Count() > 0)
                {
                    overlays.Add(new WindowOverlay
                    {
                        AnchorPoint = Anchor.BottomRight,
                        Handle = Recorder.GetWindows().FirstOrDefault(x => x.IsValidWindow() && !x.IsMinmimized()).Handle,
                        Size = new ScreenSize(0, 300),
                        Offset = new ScreenSize(75, 25)
                    });
                }
                overlays.Add(new DisplayOverlay
                {
                    AnchorPoint = Anchor.BottomRight,
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
                    rec.OnStatusChanged += (s, args) =>
                    {
                        if (args.Status == RecorderStatus.Recording)
                        {
                            recordingResetEvent.Set();
                        }
                    };
                    rec.Record(filePath);
                    recordingResetEvent.WaitOne(DefaultMaxRecordingLengthMillis);
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
        public void RecordWindow()
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
                    rec.OnStatusChanged += (s, args) =>
                    {
                        if (args.Status == RecorderStatus.Recording)
                        {
                            recordingResetEvent.Set();
                        }
                    };
                    rec.Record(filePath);
                    recordingResetEvent.WaitOne(DefaultMaxRecordingLengthMillis);
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
    }
}
