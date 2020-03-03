using System;
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
            Stream outStream = new MemoryStream();
            
            var rec = ScreenRecorderLib.Recorder.CreateRecorder();
            
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
            rec.Dispose();
            Assert.IsNotNull(outStream);
            Assert.IsFalse(isError);
            Assert.IsTrue(isComplete);
            Assert.AreNotEqual(outStream.Length, 0);
        }

        [TestMethod]
        public void Run50RecordingsTestWithDifferentInstances()
        {
            for (int i = 0; i < 50; i++)
            {
                Stream outStream = new MemoryStream();
                RecorderOptions options = new RecorderOptions();
                options.VideoOptions = new VideoOptions { Framerate = 60, IsFixedFramerate = false };
                var rec = ScreenRecorderLib.Recorder.CreateRecorder(options);
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
                Assert.IsTrue(resetEvent.WaitOne(5000), "Recording finalize timed out");
                Assert.IsNotNull(outStream, "Outstream is null");
                Assert.IsFalse(isError, "Recording error: "+error);
                Assert.IsTrue(isComplete, "Recording not complete");
                Assert.AreNotEqual(outStream.Length, 0, "Outstream length is 0");
                rec.Dispose();
                rec = null;
                outStream.Dispose();
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
            options.VideoOptions = new VideoOptions { Framerate = 60, IsFixedFramerate=false };
            var rec = ScreenRecorderLib.Recorder.CreateRecorder(options);
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
                Stream outStream = new MemoryStream();
                isError = false;
                isComplete = false;
                rec.Record(outStream);
                Thread.Sleep(2000);
                rec.Stop();
                Assert.IsTrue(resetEvent.WaitOne(5000), "Recording finalize timed out");
                Assert.IsNotNull(outStream, "Outstream is null");
                Assert.IsFalse(isError, "Recording error: "+error);
                Assert.IsTrue(isComplete, "Recording not complete");
                Assert.AreNotEqual(outStream.Length, 0, "Outstream length is 0");
                outStream.Dispose();
            }
            rec.Dispose();
            rec = null;
        }
    }
}
