using System;
using System.Diagnostics;
using System.IO;
using System.Threading;
using Microsoft.VisualStudio.TestTools.UnitTesting;

namespace ScreenRecorderLib
{
    [TestClass]
    public class FunctionTests
    {
        [TestMethod]
        public void RunInTestMethodTest()
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
        public void Run50RecordingsTestWithDifferentInstances()
        {
            for (int i = 0; i < 50; i++)
            {
                using (Stream outStream = new MemoryStream())
                {
                    RecorderOptions options = new RecorderOptions();
                    options.VideoOptions = new VideoOptions { Framerate = 60, IsFixedFramerate = false };
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
