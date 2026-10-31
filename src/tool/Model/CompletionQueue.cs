using System;
using System.Threading;
using System.Threading.Tasks;

namespace BBSFW.Model
{

	public class RequestResult<T>
	{
		public bool Timeout { get; private set; }

		public T Result { get; private set; }


		public RequestResult(bool timeout, T result)
		{
			Timeout = timeout;
			Result = result;
		}
	}


	public class CompletionQueue<T>
	{
		private TaskCompletionSource<T> _tcs = null;

		public void Complete(T response)
		{
			_tcs?.TrySetResult(response);
		}

		// Starts waiting before the request goes out, so a reply that arrives
		// quickly (or from a fast virtual controller) can't be missed:
		//   var reply = cq.Expect(timeout); Send(...); var result = await reply;
		public Task<RequestResult<T>> Expect(TimeSpan timeout)
		{
			Reset(timeout);
			return Await();
		}

		private async Task<RequestResult<T>> Await()
		{
			var tcs = _tcs;
			try
			{
				var res = await tcs.Task;
				return new RequestResult<T>(false, res);
			}
			catch (TaskCanceledException)
			{
				return new RequestResult<T>(true, default(T));
			}
			finally
			{
				Interlocked.CompareExchange(ref _tcs, null, tcs);
			}
		}

		public async Task<RequestResult<T>> WaitResponse(TimeSpan timeout)
		{
			Reset(timeout);

			try
			{
				var res =  await _tcs.Task;
				_tcs = null;
				return new RequestResult<T>(false, res);
			}
			catch(TaskCanceledException)
			{
				_tcs = null;
				return new RequestResult<T>(true, default(T));
			}
		}

		private void Reset(TimeSpan timeout)
		{
			var tcs = new TaskCompletionSource<T>();

			var cancelTokenSrc = new CancellationTokenSource((int)timeout.TotalMilliseconds);
			cancelTokenSrc.Token.Register(() => tcs.TrySetCanceled());

			_tcs = tcs;
		}

	}
}
