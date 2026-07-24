using System;
using System.Collections.Generic;
using System.IO;

namespace TestApp
{
    public class StreamingWavWriter : IDisposable
    {
        private readonly FileStream _stream;
        private readonly BinaryWriter _writer;
        private int _dataLength = 0;

        public StreamingWavWriter(string path, int sampleRate, short channels, short bitsPerSample = 16)
        {
            _stream = new FileStream(path, FileMode.Create);
            _writer = new BinaryWriter(_stream);

            int byteRate = sampleRate * channels * (bitsPerSample / 8);
            short blockAlign = (short)(channels * (bitsPerSample / 8));

            _writer.Write(new[] { 'R', 'I', 'F', 'F' });
            _writer.Write(0); // placeholder, patched on Dispose
            _writer.Write(new[] { 'W', 'A', 'V', 'E' });
            _writer.Write(new[] { 'f', 'm', 't', ' ' });
            _writer.Write(16);
            _writer.Write((short)1);
            _writer.Write(channels);
            _writer.Write(sampleRate);
            _writer.Write(byteRate);
            _writer.Write(blockAlign);
            _writer.Write(bitsPerSample);
            _writer.Write(new[] { 'd', 'a', 't', 'a' });
            _writer.Write(0); // placeholder, patched on Dispose
        }

        public void WritePacket(byte[] packet)
        {
            _writer.Write(packet);
            _dataLength += packet.Length;
        }

        public void Dispose()
        {
            // Patch header sizes now that we know the total length
            _writer.Seek(4, SeekOrigin.Begin);
            _writer.Write(36 + _dataLength);
            _writer.Seek(40, SeekOrigin.Begin);
            _writer.Write(_dataLength);

            _writer.Flush();
            _writer.Dispose();
            _stream.Dispose();
        }
    }
}
