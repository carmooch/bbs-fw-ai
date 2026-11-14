// The live dashboard: a read-only page at http://localhost:<port> showing
// what bbs-link reads from the controller, several times a second. It only
// answers on this computer (localhost) and changes nothing on the bike.
//
//   GET /                the page (dashboard.html, built in)
//   GET /data?since=N    state, plus readings and events numbered after N

using System;
using System.IO;
using System.Net;
using System.Reflection;
using System.Text;
using System.Threading;
using System.Threading.Tasks;

namespace BBSFW.Link
{
	public static class LiveDashboard
	{
		// the URL it serves, or null if the port couldn't be opened
		public static string Start(LinkServer server, int port, CancellationToken stop)
		{
			// localhost and 127.0.0.1; on Windows a user without admin rights may
			// only be allowed localhost
			var listener = Open(port, true) ?? Open(port, false);
			if (listener == null)
			{
				return null;
			}

			string page;
			using (var s = Assembly.GetExecutingAssembly().GetManifestResourceStream("dashboard.html"))
			using (var r = new StreamReader(s, Encoding.UTF8))
			{
				page = r.ReadToEnd();
			}

			stop.Register(() => { try { listener.Stop(); } catch { } });
			Task.Run(async () =>
			{
				while (listener.IsListening)
				{
					HttpListenerContext ctx;
					try
					{
						ctx = await listener.GetContextAsync();
					}
					catch (Exception)
					{
						break;
					}
					_ = Task.Run(() => Answer(ctx, server, page));
				}
			});
			return $"http://localhost:{port}/";
		}

		private static HttpListener Open(int port, bool withIp)
		{
			var listener = new HttpListener();
			listener.Prefixes.Add($"http://localhost:{port}/");
			if (withIp)
			{
				listener.Prefixes.Add($"http://127.0.0.1:{port}/");
			}
			try
			{
				listener.Start();
				return listener;
			}
			catch (Exception)
			{
				try { listener.Close(); } catch { }
				return null;
			}
		}

		private static void Answer(HttpListenerContext ctx, LinkServer server, string page)
		{
			try
			{
				var path = ctx.Request.Url.AbsolutePath;
				string body, type;
				int code = 200;
				if (ctx.Request.HttpMethod != "GET")
				{
					code = 405; body = "read-only"; type = "text/plain";
				}
				else if (path == "/")
				{
					body = page; type = "text/html; charset=utf-8";
				}
				else if (path == "/data")
				{
					long.TryParse(ctx.Request.QueryString["since"], out long since);
					body = server.LiveSince(since).ToJsonString(); type = "application/json";
				}
				else
				{
					code = 404; body = "not found"; type = "text/plain";
				}
				var bytes = Encoding.UTF8.GetBytes(body);
				ctx.Response.StatusCode = code;
				ctx.Response.ContentType = type;
				ctx.Response.Headers["Cache-Control"] = "no-store";
				ctx.Response.ContentLength64 = bytes.Length;
				ctx.Response.OutputStream.Write(bytes, 0, bytes.Length);
			}
			catch (Exception)
			{
				// the browser went away
			}
			finally
			{
				try { ctx.Response.Close(); } catch { }
			}
		}
	}
}
