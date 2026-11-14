// The virtual controller's pseudo-terminal (ride_sim --serial) as a port,
// for the end-to-end checks and bbs-link. Linux only.

using System;
using System.IO;
using System.Linq;
using System.Threading;
using BBSFW.Model;

// The virtual controller's pseudo-terminal as a port. (.NET's SerialPort
// can't open one: it sets DTR with an ioctl ptys don't support.)
class PtyTransport : IPortTransport
{
	private readonly FileStream _stream;
	private readonly Thread _reader;
	private volatile bool _open = true;

	public event Action<byte[]> Received;

	public PtyTransport(string path)
	{
		_stream = new FileStream(path, FileMode.Open, FileAccess.ReadWrite, FileShare.ReadWrite, 1);
		_reader = new Thread(ReadLoop) { IsBackground = true };
		_reader.Start();
	}

	private void ReadLoop()
	{
		var buf = new byte[256];
		while (_open)
		{
			int n;
			try
			{
				n = _stream.Read(buf, 0, buf.Length);
			}
			catch
			{
				break;
			}
			if (n <= 0)
			{
				break;
			}
			if (_open)
			{
				Received?.Invoke(buf.Take(n).ToArray());
			}
		}
	}

	public void Write(byte[] buffer, int offset, int count)
	{
		_stream.Write(buffer, offset, count);
		_stream.Flush();
	}

	public void Close()
	{
		_open = false;
		// the reader thread stays blocked in Read until the controller sends
		// something or exits; it's a background thread and discards it
	}
}
