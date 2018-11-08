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
            rec.OnRecordingComplete += (s, args) =>
            {
                isComplete = true;
            };
            rec.OnRecordingFailed += (s, args) =>
            {
                isError = true;
            };

            rec.Record(outStream);
            Thread.Sleep(3000);
            rec.Stop();
            Thread.Sleep(1000);
            Assert.IsNotNull(outStream);
            Assert.IsFalse(isError);
            Assert.IsTrue(isComplete);
            Assert.AreNotEqual(outStream.Length, 0);
        }
    }
}
