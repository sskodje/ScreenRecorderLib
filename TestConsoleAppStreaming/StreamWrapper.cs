using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace TestConsoleAppStreaming
{
    //The video encoder requires a stream that can seek, but it doesn't actually require seeking to work when encoding fragmented MP4.
    //This stream wraps the non-seekable input stream from FFMPEG and lies about being able to seek, so the encoder is happy.
    public class StreamWrapper : Stream
    {
        Stream _outStream;
        public StreamWrapper(Stream outStream)
        {
            _outStream = outStream;
        }

        public override bool CanRead => false;

        public override bool CanSeek => true;

        public override bool CanWrite => true;

        public override long Length => 0;

        public override long Position { get => throw new NotImplementedException(); set => throw new NotImplementedException(); }

        public override void Flush()
        {
            _outStream.Flush();
        }

        public override int Read(byte[] buffer, int offset, int count)
        {
            throw new NotImplementedException();
        }

        public override long Seek(long offset, SeekOrigin origin)
        {
            return offset;
        }

        public override void SetLength(long value)
        {
        }

        public override void Write(byte[] buffer, int offset, int count)
        {
            try
            {
                _outStream.Write(buffer, offset, count);
            }
            catch (ObjectDisposedException) { }
        }
        public override void Close()
        {
            base.Close();
            _outStream.Close();
        }
        protected override void Dispose(bool disposing)
        {
            base.Dispose(disposing);
            _outStream.Dispose();
        }
    }
}
