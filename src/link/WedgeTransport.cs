// Simulator only (src/link/check.sh): the virtual controller's port, which
// `bbs-link test-wedge` can make behave like a wedged USB-serial adapter:
// nothing comes in, and writing or closing blocks for good while the port
// still looks open. Opening the port again (as after replugging the cable)
// gives a new transport that works.

using System;
using System.Threading;
using BBSFW.Model;

namespace BBSFW.Link
{
	class WedgeTransport : IPortTransport
	{
		public static volatile WedgeTransport Current;

		private readonly IPortTransport _inner;
		private readonly ManualResetEventSlim _never = new ManualResetEventSlim(false);
		private volatile bool _wedged;

		public event Action<byte[]> Received;

		public WedgeTransport(IPortTransport inner)
		{
			_inner = inner;
			_inner.Received += data =>
			{
				if (!_wedged)
				{
					Received?.Invoke(data);
				}
			};
		}

		public void Wedge()
		{
			_wedged = true;
			// stop the virtual port's reader, or it would go on taking bytes
			// meant for the port once it's opened again
			_inner.Close();
		}

		public void Write(byte[] buffer, int offset, int count)
		{
			if (_wedged)
			{
				_never.Wait();
			}
			_inner.Write(buffer, offset, count);
		}

		public void Close()
		{
			if (_wedged)
			{
				_never.Wait();
			}
			_inner.Close();
		}
	}
}
