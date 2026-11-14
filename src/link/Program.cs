// bbs-link: a live link to a bbs-fw controller, for driving it from a
// terminal (or a Claude session) on the laptop plugged into the bike. See
// LIVE.md at the repository root.

using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.IO.Pipes;
using System.IO.Ports;
using System.Linq;
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;
using System.Threading.Tasks;

namespace BBSFW.Link
{
	public static class LinkClient
	{
		public static async Task<bool> IsServerRunning()
		{
			try
			{
				using (var pipe = new NamedPipeClientStream(".", LinkServer.PipeName, PipeDirection.InOut))
				{
					await pipe.ConnectAsync(300);
					var w = new StreamWriter(pipe, new UTF8Encoding(false)) { AutoFlush = true };
					await w.WriteLineAsync("{\"cmd\":\"events\",\"count\":0}");
					var r = new StreamReader(pipe, new UTF8Encoding(false));
					var read = r.ReadLineAsync();
					// a server that's stuck still holds the pipe: count it as running
					return await Task.WhenAny(read, Task.Delay(3000)) != read || await read != null;
				}
			}
			catch (Exception)
			{
				return false;
			}
		}

		// The server answers within its own deadlines (a stalled port ends in an
		// error); this is the last resort, so a command never waits for good.
		public static TimeSpan ReplyTimeout(JsonObject request)
		{
			switch ((string)request["cmd"])
			{
				case "watch": return TimeSpan.FromSeconds(((double?)request["seconds"] ?? 10) + 30);
				case "capture": return TimeSpan.FromSeconds(200);
				default: return TimeSpan.FromSeconds(90);
			}
		}

		public static async Task<JsonObject> Send(JsonObject request)
		{
			using (var pipe = new NamedPipeClientStream(".", LinkServer.PipeName, PipeDirection.InOut))
			{
				try
				{
					await pipe.ConnectAsync(2000);
				}
				catch (Exception)
				{
					throw new Exception("No bbs-link server running. Start one with: bbs-link serve --port <COM port>");
				}
				var w = new StreamWriter(pipe, new UTF8Encoding(false)) { AutoFlush = true };
				await w.WriteLineAsync(request.ToJsonString());
				var r = new StreamReader(pipe, new UTF8Encoding(false));
				var read = r.ReadLineAsync();
				var timeout = ReplyTimeout(request);
				if (await Task.WhenAny(read, Task.Delay(timeout)) != read)
				{
					throw new Exception($"No answer from the bbs-link server in {timeout.TotalSeconds:0} s. bbs-link status shows the link's state; if the server itself is stuck, end it (Ctrl+C) and start it again.");
				}
				var line = await read;
				if (line == null)
				{
					throw new Exception("The bbs-link server closed the connection without answering.");
				}
				return JsonNode.Parse(line).AsObject();
			}
		}
	}

	public static class Program
	{
		private const string Usage = @"bbs-link: live link to a bbs-fw controller (see LIVE.md)

  bbs-link ports                         list serial ports
  bbs-link serve --port COM5 [--log-dir DIR] [--max-amps 30] [--web 8710 | --no-web]
                                         hold the link: run this in its own terminal (or in the
                                         background); it logs live data to DIR (default ./live)
                                         and shows it live at http://localhost:8710 (this
                                         computer only)
  bbs-link status                        connection and the latest live data
  bbs-link watch [SECONDS]               live data for a while (default 10, max 120), as a table
  bbs-link level N                       assist level 0-9, as the display would set it
  bbs-link lights on|off
  bbs-link mark TEXT                     put a note in the live log (""push hard now"")
  bbs-link events [N]                    the last N events (controller event log and link)
  bbs-link config show [FILTER]          all settings (or those whose name contains FILTER)
  bbs-link config get NAME...
  bbs-link config set NAME=VALUE... [--dry-run] [--battery]
                                         change settings: bike still, backed up, read back
  bbs-link config undo                   put back the settings before the last change
  bbs-link config save [FILE]            save the settings to a file (config tool format)
  bbs-link config load FILE [--dry-run] [--battery]
  bbs-link capture                       pedal sensor steadiness check: turn the cranks
                                         forwards for about 6 turns
  bbs-link reset-effort --yes            forget the learned pedal sensor pattern
  bbs-link calibrate-voltage VOLTS       the battery voltage you measured
  bbs-link stop                          level 0, close the port, end the server

Add --json to any command for the raw reply.";

		public static async Task<int> Main(string[] argv)
		{
			var args = argv.ToList();
			bool json = args.Remove("--json");
			if (args.Count == 0 || args[0] == "help" || args[0] == "--help" || args[0] == "-h")
			{
				Console.WriteLine(Usage);
				return args.Count == 0 ? 1 : 0;
			}

			try
			{
				switch (args[0])
				{
					case "ports":
						var ports = SerialPort.GetPortNames();
						Console.WriteLine(ports.Length == 0 ? "No serial ports found (cable plugged in? driver installed?)" : string.Join("\n", ports));
						return 0;

					case "serve":
						var port = Option(args, "--port") ?? throw new Exception("serve needs --port (bbs-link ports lists them)");
						var logDir = Option(args, "--log-dir") ?? "live";
						var maxAmps = uint.Parse(Option(args, "--max-amps") ?? "30", CultureInfo.InvariantCulture);
						var web = args.Remove("--no-web") ? 0 : int.Parse(Option(args, "--web") ?? "8710", CultureInfo.InvariantCulture);
						return await new LinkServer(port, logDir, maxAmps, web).Run();
				}

				var request = Request(args);
				var reply = await LinkClient.Send(request);
				bool ok = (bool?)reply["ok"] ?? false;
				if (json)
				{
					Console.WriteLine(reply.ToJsonString(new JsonSerializerOptions { WriteIndented = true }));
				}
				else
				{
					var text = (string)reply["text"] ?? "";
					(ok ? Console.Out : Console.Error).WriteLine(text);
				}
				return ok ? 0 : 2;
			}
			catch (Exception ex)
			{
				Console.Error.WriteLine(ex.Message);
				return 1;
			}
		}

		private static string Option(List<string> args, string name)
		{
			int i = args.IndexOf(name);
			if (i < 0 || i + 1 >= args.Count)
			{
				return null;
			}
			var v = args[i + 1];
			args.RemoveRange(i, 2);
			return v;
		}

		private static JsonObject Request(List<string> args)
		{
			bool Flag(string f) => args.Remove(f);
			var c = CultureInfo.InvariantCulture;
			string Arg(int i, string what) => args.Count > i ? args[i] : throw new Exception($"{args[0]} needs {what}");
			JsonArray Rest(int from) => new JsonArray(args.Skip(from).Select(a => (JsonNode)JsonValue.Create(a)).ToArray());

			switch (args[0])
			{
				case "status":
					return new JsonObject { ["cmd"] = "status" };
				case "watch":
					return new JsonObject { ["cmd"] = "watch", ["seconds"] = args.Count > 1 ? double.Parse(args[1], c) : 10 };
				case "level":
					return new JsonObject { ["cmd"] = "level", ["level"] = int.Parse(Arg(1, "a level, 0-9"), c) };
				case "lights":
					var on = Arg(1, "on or off");
					return new JsonObject { ["cmd"] = "lights", ["on"] = on == "on" ? true : on == "off" ? false : throw new Exception("lights on|off") };
				case "mark":
					return new JsonObject { ["cmd"] = "mark", ["text"] = string.Join(" ", args.Skip(1)) };
				case "events":
					return new JsonObject { ["cmd"] = "events", ["count"] = args.Count > 1 ? int.Parse(args[1], c) : 30 };
				case "capture":
					return new JsonObject { ["cmd"] = "capture" };
				case "reset-effort":
					if (!Flag("--yes"))
					{
						throw new Exception("reset-effort forgets the learned pattern (it needs a fast ride to learn again): add --yes");
					}
					return new JsonObject { ["cmd"] = "reset-effort" };
				case "calibrate-voltage":
					return new JsonObject { ["cmd"] = "calibrate-voltage", ["volts"] = double.Parse(Arg(1, "the measured voltage"), c) };
				case "stop":
					return new JsonObject { ["cmd"] = "stop" };
				case "test-wedge":
					// src/link/check.sh, virtual controller only: not in the help
					return new JsonObject { ["cmd"] = "test-wedge" };
				case "config":
					bool dry = Flag("--dry-run");
					bool battery = Flag("--battery");
					switch (Arg(1, "show, get, set, undo, save or load"))
					{
						case "show":
							return new JsonObject { ["cmd"] = "config-show", ["filter"] = args.Count > 2 ? args[2] : null };
						case "get":
							Arg(2, "setting names");
							return new JsonObject { ["cmd"] = "config-get", ["names"] = Rest(2) };
						case "set":
							Arg(2, "NAME=VALUE");
							return new JsonObject { ["cmd"] = "config-set", ["assignments"] = Rest(2), ["dryRun"] = dry, ["battery"] = battery };
						case "undo":
							return new JsonObject { ["cmd"] = "config-undo" };
						case "save":
							return new JsonObject { ["cmd"] = "config-save", ["path"] = args.Count > 2 ? Path.GetFullPath(args[2]) : null };
						case "load":
							return new JsonObject { ["cmd"] = "config-load", ["path"] = Path.GetFullPath(Arg(2, "a file")), ["dryRun"] = dry, ["battery"] = battery };
					}
					throw new Exception("config show|get|set|undo|save|load");
			}
			throw new Exception($"Unknown command '{args[0]}'. bbs-link help lists them.");
		}
	}
}
