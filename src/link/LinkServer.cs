// The live link: owns the serial port, polls live data as often as the
// 1200 baud line allows (about 4 times a second), logs every sample to a CSV
// file, and takes commands from `bbs-link <command>` over a local named pipe.
//
// It stands in for the display, so it holds the assist level (0 when it
// starts and when it stops). Safety rules it enforces, whoever asks:
// - settings are only written with the bike still: no pedalling, wheel
//   stopped, throttle closed, and live data under 2 s old;
// - every write is backed up first (undo with `config undo`) and read back;
// - Max Current is capped (--max-amps), battery voltages only change with
//   --battery, and the config tool's own limits apply.

using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.IO.Pipes;
using System.Linq;
using System.Text;
using System.Text.Json.Nodes;
using System.Threading;
using System.Threading.Tasks;
using BBSFW.Model;

namespace BBSFW.Link
{
	public class LinkServer
	{
		public const string PipeName = "bbs-link";

		private readonly string _portName;
		private readonly string _logDir;
		private readonly uint _maxAmps;

		private readonly SemaphoreSlim _io = new SemaphoreSlim(1, 1);
		private readonly CancellationTokenSource _stop = new CancellationTokenSource();
		private readonly object _lock = new object();

		private BbsfwConnection _conn;
		private volatile bool _connected;
		private string _firmware = "";
		private int _configVersion;
		private Configuration _config;
		private ControllerStatus _last;
		private DateTime _lastAt = DateTime.MinValue;
		private DateTime _since = DateTime.Now;
		private string _state = "starting";
		private int _desiredLevel = 0;
		private DateTime _levelSentAt = DateTime.MinValue;

		private StreamWriter _csv;
		private string _csvPath;
		private string _pendingMark;
		private StreamWriter _eventsLog;
		private readonly List<string> _events = new List<string>();
		private readonly Stack<string> _undo = new Stack<string>();
		private readonly List<List<ControllerStatus>> _watchers = new List<List<ControllerStatus>>();

		public LinkServer(string portName, string logDir, uint maxAmps)
		{
			_portName = portName;
			_logDir = Path.GetFullPath(logDir);
			_maxAmps = maxAmps;
		}

		private static string Now() => DateTime.Now.ToString("yyyyMMdd-HHmmss", CultureInfo.InvariantCulture);

		public async Task<int> Run()
		{
			if (await LinkClient.IsServerRunning())
			{
				Console.Error.WriteLine("A bbs-link server is already running (bbs-link stop ends it).");
				return 1;
			}

			Directory.CreateDirectory(Path.Combine(_logDir, "backups"));
			_csvPath = Path.Combine(_logDir, $"live-{Now()}.csv");
			_csv = new StreamWriter(_csvPath, false, new UTF8Encoding(false)) { AutoFlush = true };
			_csv.WriteLine(ControllerStatus.CsvHeader + ",speed_kph,battery_w,mark");
			_eventsLog = new StreamWriter(Path.Combine(_logDir, $"events-{Now()}.log"), false, new UTF8Encoding(false)) { AutoFlush = true };

			if (_portName.StartsWith("/dev/pts/", StringComparison.Ordinal))
			{
				BbsfwConnection.OpenTransport = p => new PtyTransport(p.Name);
			}

			Console.CancelKeyPress += (s, e) => { e.Cancel = true; _stop.Cancel(); };

			var pipes = Task.Run(ServePipe);
			Console.WriteLine($"bbs-link serving {_portName}; log {_csvPath}");

			try
			{
				await PollLoop();
			}
			catch (OperationCanceledException)
			{
			}

			await ShutDown();
			return 0;
		}

		// --- the controller ---

		private async Task PollLoop()
		{
			int misses = 0;
			while (!_stop.IsCancellationRequested)
			{
				if (!_connected)
				{
					if (!await TryConnect())
					{
						await Task.Delay(1000, _stop.Token);
					}
					misses = 0;
					continue;
				}

				ControllerStatus status = null;
				await _io.WaitAsync(_stop.Token);
				try
				{
					if (_connected && DateTime.Now - _levelSentAt > TimeSpan.FromSeconds(2))
					{
						// as a display does: keep telling the controller the level
						_conn.SetAssistLevel(_desiredLevel);
						_levelSentAt = DateTime.Now;
					}
					var r = await _conn.ReadStatus(TimeSpan.FromMilliseconds(1500));
					status = r.Timeout ? null : r.Result;
				}
				catch (Exception)
				{
					status = null;
				}
				finally
				{
					_io.Release();
				}

				if (status == null)
				{
					if (++misses >= 6)
					{
						Lost("no answer for several seconds (controller off or cable out?)");
					}
				}
				else
				{
					misses = 0;
					Record(status);
				}

				// let waiting commands in between polls
				await Task.Delay(30, _stop.Token);
			}
		}

		private async Task<bool> TryConnect()
		{
			_state = "connecting to " + _portName;
			var conn = new BbsfwConnection();
			int configVersion = 0;
			conn.EventLog += e => AddEvent(e.Message);
			conn.Connected += (controller, version, cfgVersion) => configVersion = cfgVersion;
			try
			{
				if (!await conn.Connect(new ComPort(_portName, ""), TimeSpan.FromSeconds(4)))
				{
					_state = "waiting for the controller on " + _portName + " (switched on? cable in?)";
					return false;
				}
			}
			catch (Exception ex)
			{
				_state = $"can't open {_portName}: {ex.Message}";
				return false;
			}

			_conn = conn;
			_firmware = conn.FirmwareVersion?.ToString() ?? "?";
			var cfg = await conn.ReadConfiguration(TimeSpan.FromSeconds(10));
			if (cfg.Timeout || cfg.Result == null)
			{
				conn.Close();
				_state = "connected, but the controller didn't send its settings";
				return false;
			}
			_config = cfg.Result;
			_configVersion = configVersion;
			_config.WriteToFile(Path.Combine(_logDir, "backups", $"config-at-connect-{Now()}.xml"));

			_desiredLevel = 0;
			conn.SetAssistLevel(0);
			_levelSentAt = DateTime.Now;
			_connected = true;
			_since = DateTime.Now;
			_state = "connected";
			AddEvent($"connected: bbs-fw {_firmware}, {conn.ControllerType}, config version {_configVersion}; level set to 0");
			return true;
		}

		private void Lost(string why)
		{
			_connected = false;
			try { _conn?.Close(); } catch { }
			_state = "lost the controller: " + why;
			AddEvent(_state);
		}

		private void Record(ControllerStatus s)
		{
			string mark;
			lock (_lock)
			{
				_last = s;
				_lastAt = DateTime.Now;
				mark = _pendingMark;
				_pendingMark = null;
				foreach (var w in _watchers)
				{
					w.Add(s);
				}
			}
			var c = CultureInfo.InvariantCulture;
			_csv.WriteLine(s.ToCsv() + "," + Speed(s).ToString("0.0", c) + "," +
				(s.BatteryVolts * s.BatteryAmps).ToString("0", c) + "," + CsvText(mark));
		}

		private static string CsvText(string s) => s == null ? "" : "\"" + s.Replace("\"", "'") + "\"";

		private float Speed(ControllerStatus s) => _config == null ? 0 : s.SpeedKph(_config.WheelSizeInch);

		private void AddEvent(string message)
		{
			var line = DateTime.Now.ToString("HH:mm:ss", CultureInfo.InvariantCulture) + " " + message;
			lock (_lock)
			{
				_events.Add(line);
				if (_events.Count > 200)
				{
					_events.RemoveAt(0);
				}
			}
			_eventsLog?.WriteLine(line);
		}

		private void Mark(string text)
		{
			lock (_lock)
			{
				_pendingMark = _pendingMark == null ? text : _pendingMark + "; " + text;
			}
		}

		private async Task ShutDown()
		{
			if (_connected)
			{
				await _io.WaitAsync();
				try
				{
					_desiredLevel = 0;
					_conn.SetAssistLevel(0);
					await Task.Delay(300);
					AddEvent("stopping: level set to 0");
					_conn.Close();
				}
				catch
				{
				}
				finally
				{
					_io.Release();
				}
			}
			_connected = false;
			_csv?.Dispose();
			_eventsLog?.Dispose();
			Console.WriteLine("bbs-link stopped.");
		}

		// --- commands ---

		private async Task ServePipe()
		{
			while (!_stop.IsCancellationRequested)
			{
				var pipe = new NamedPipeServerStream(PipeName, PipeDirection.InOut,
					NamedPipeServerStream.MaxAllowedServerInstances, PipeTransmissionMode.Byte, PipeOptions.Asynchronous);
				try
				{
					await pipe.WaitForConnectionAsync(_stop.Token);
				}
				catch (OperationCanceledException)
				{
					pipe.Dispose();
					return;
				}
				_ = Task.Run(() => HandleClient(pipe));
			}
		}

		private async Task HandleClient(NamedPipeServerStream pipe)
		{
			using (pipe)
			{
				try
				{
					var reader = new StreamReader(pipe, new UTF8Encoding(false));
					var writer = new StreamWriter(pipe, new UTF8Encoding(false)) { AutoFlush = true };
					var line = await reader.ReadLineAsync();
					JsonObject reply;
					try
					{
						var request = JsonNode.Parse(line ?? "{}").AsObject();
						reply = await Handle(request);
						reply["ok"] = true;
					}
					catch (Exception ex)
					{
						reply = new JsonObject { ["ok"] = false, ["text"] = ex.Message };
					}
					await writer.WriteLineAsync(reply.ToJsonString());
				}
				catch (Exception)
				{
					// the client went away
				}
			}
		}

		private async Task<JsonObject> Handle(JsonObject q)
		{
			var cmd = (string)q["cmd"];
			switch (cmd)
			{
				case "status": return Status();
				case "watch": return await Watch(Math.Clamp((double?)q["seconds"] ?? 10, 1, 120));
				case "level": return await SetLevel((int)q["level"]);
				case "lights": return await SetLights((bool)q["on"]);
				case "config-show": return await ConfigShow((string)q["filter"]);
				case "config-get": return await ConfigGet(q["names"].AsArray().Select(x => (string)x).ToList());
				case "config-set": return await ConfigSet(q["assignments"].AsArray().Select(x => (string)x).ToList(),
					(bool?)q["battery"] ?? false, (bool?)q["dryRun"] ?? false);
				case "config-undo": return await ConfigUndo();
				case "config-save": return await ConfigSave((string)q["path"]);
				case "config-load": return await ConfigLoad((string)q["path"], (bool?)q["battery"] ?? false, (bool?)q["dryRun"] ?? false);
				case "capture": return await Capture();
				case "reset-effort": return await ResetEffort();
				case "calibrate-voltage": return await CalibrateVoltage((double)q["volts"]);
				case "events": return Events((int?)q["count"] ?? 30);
				case "mark": Mark((string)q["text"]); return new JsonObject { ["text"] = "marked in the log" };
				case "stop": _stop.Cancel(); return new JsonObject { ["text"] = "stopping: level 0, port closed" };
			}
			throw new Exception($"unknown command '{cmd}'");
		}

		private void RequireConnected()
		{
			if (!_connected)
			{
				throw new Exception("Not connected: " + _state);
			}
		}

		// fresh live data (under 2 s old), or an exception saying why not
		private ControllerStatus Fresh()
		{
			RequireConnected();
			lock (_lock)
			{
				if (_last == null || DateTime.Now - _lastAt > TimeSpan.FromSeconds(2))
				{
					throw new Exception("No live data in the last 2 s.");
				}
				return _last;
			}
		}

		private void RequireStill()
		{
			var s = Fresh();
			var why = new List<string>();
			if (s.PedalingForwards || s.PedalingBackwards || s.CadenceRpm > 0) why.Add("the cranks are turning");
			if (s.WheelMoving || s.WheelRpm > 0) why.Add("the wheel is turning");
			if (s.ThrottlePercent > 0) why.Add("the throttle is open");
			if (why.Count > 0)
			{
				throw new Exception("Refused: settings only change with the bike still, and " + string.Join(", ", why) + ".");
			}
		}

		private JsonObject Status()
		{
			ControllerStatus s;
			DateTime at;
			lock (_lock)
			{
				s = _last;
				at = _lastAt;
			}
			var o = new JsonObject
			{
				["connected"] = _connected,
				["state"] = _state,
				["port"] = _portName,
				["firmware"] = _firmware,
				["config_version"] = _configVersion,
				["level_set"] = _desiredLevel,
				["log"] = _csvPath,
			};
			var sb = new StringBuilder();
			sb.AppendLine($"{_state}; bbs-fw {_firmware}; level set to {_desiredLevel}; log {_csvPath}");
			if (s != null)
			{
				var age = (DateTime.Now - at).TotalMilliseconds;
				o["age_ms"] = (int)age;
				o["live"] = Row(s, 0);
				sb.AppendLine(age > 2000 ? $"(last live data {age / 1000:0} s old)" : "");
				sb.Append(Line(s));
			}
			o["text"] = sb.ToString().TrimEnd();
			return o;
		}

		private JsonObject Row(ControllerStatus s, double t)
		{
			return new JsonObject
			{
				["t"] = Math.Round(t, 2),
				["level"] = s.AssistLevel,
				["cadence_rpm"] = s.CadenceRpm,
				["speed_kph"] = Math.Round(Speed(s), 1),
				["current_pct"] = s.TargetCurrentPercent,
				["speed_pct"] = s.TargetSpeedPercent,
				["volts"] = s.BatteryVolts,
				["amps"] = s.BatteryAmps,
				["battery_w"] = Math.Round(s.BatteryVolts * s.BatteryAmps),
				["battery_pct"] = s.BatteryPercent,
				["rider_w"] = s.RiderWatts,
				["effort_valid"] = s.EffortValid,
				["effort_learned_pct"] = s.EffortLearnedPercent,
				["throttle_pct"] = s.ThrottlePercent,
				["temp_c"] = s.TemperatureC,
				["status_code"] = s.StatusCode,
				["pedalling"] = s.PedalingForwards,
				["backwards"] = s.PedalingBackwards,
				["brake"] = s.Brake,
				["lights"] = s.Lights,
				["wheel_moving"] = s.WheelMoving,
			};
		}

		private string Line(ControllerStatus s)
		{
			var c = CultureInfo.InvariantCulture;
			var flags = new List<string>();
			if (s.PedalingForwards) flags.Add("pedalling");
			if (s.PedalingBackwards) flags.Add("backwards");
			if (s.Brake) flags.Add("BRAKE");
			if (s.Lights) flags.Add("lights");
			if (s.EffortValid) flags.Add("effort-valid");
			if (s.StatusCode != 0x01) flags.Add(StatusName(s.StatusCode));
			return string.Format(c,
				"level {0}  cadence {1,5:0.0} rpm  speed {2,4:0.0} km/h  motor {3,3}% ({4:0.0} A, {5:0} W)  battery {6:0.0} V {7}%  rider {8} W  learned {9}%  throttle {10}%  {11} C  {12}",
				s.AssistLevel, s.CadenceRpm, Speed(s), s.TargetCurrentPercent, s.BatteryAmps, s.BatteryVolts * s.BatteryAmps,
				s.BatteryVolts, s.BatteryPercent, s.RiderWatts, s.EffortLearnedPercent, s.ThrottlePercent, s.TemperatureC,
				string.Join(" ", flags));
		}

		// app.h STATUS_*
		private static string StatusName(int code)
		{
			switch (code)
			{
				case 0x01: return "normal";
				case 0x03: return "braking";
				case 0x04: return "ERROR throttle high at start";
				case 0x05: return "ERROR throttle";
				case 0x06: return "ERROR low voltage cutoff";
				case 0x07: return "ERROR high voltage";
				case 0x08: return "ERROR hall sensor";
				case 0x09: return "ERROR phase line";
				case 0x10: return "ERROR controller over temperature";
				case 0x11: return "ERROR motor over temperature";
				case 0x12: return "ERROR current sense";
				case 0x25: return "ERROR torque sensor";
				default: return $"status 0x{code:x2}";
			}
		}

		private async Task<JsonObject> Watch(double seconds)
		{
			RequireConnected();
			var samples = new List<ControllerStatus>();
			lock (_lock)
			{
				_watchers.Add(samples);
			}
			try
			{
				await Task.Delay(TimeSpan.FromSeconds(seconds), _stop.Token);
			}
			finally
			{
				lock (_lock)
				{
					_watchers.Remove(samples);
				}
			}

			List<ControllerStatus> got;
			lock (_lock)
			{
				got = samples.ToList();
			}
			if (got.Count == 0)
			{
				throw new Exception("No live data during the watch: " + _state);
			}

			var t0 = got[0].Time;
			var rows = new JsonArray();
			foreach (var s in got)
			{
				rows.Add(Row(s, (s.Time - t0).TotalSeconds));
			}

			// the table: averages over time buckets, at most 40 rows
			var c = CultureInfo.InvariantCulture;
			double bucket = Math.Max(0.25, seconds / 40);
			var sb = new StringBuilder();
			sb.AppendLine(string.Format(c, "{0} samples over {1:0.#} s, rows are {2:0.##} s averages (every sample is in {3})", got.Count, seconds, bucket, _csvPath));
			sb.AppendLine("    t  lvl  rpm   km/h  cur%    A     W  rider  flags");
			foreach (var g in got.GroupBy(s => (int)((s.Time - t0).TotalSeconds / bucket)))
			{
				var l = g.Last();
				sb.AppendLine(string.Format(c, "{0,5:0.0}  {1,3}  {2,4:0}  {3,5:0.0}  {4,4:0}  {5,5:0.0}  {6,4:0}  {7,5:0}  {8}{9}{10}{11}{12}",
					g.Key * bucket, l.AssistLevel, g.Average(s => s.CadenceRpm), g.Average(s => Speed(s)), g.Average(s => s.TargetCurrentPercent),
					g.Average(s => s.BatteryAmps), g.Average(s => s.BatteryVolts * s.BatteryAmps), g.Average(s => s.RiderWatts),
					g.Any(s => s.PedalingForwards) ? "P" : "-", g.Any(s => s.Brake) ? "B" : "-", g.Any(s => s.EffortValid) ? "E" : "-",
					g.Any(s => s.WheelMoving) ? "W" : "-", l.StatusCode != 0x01 ? " " + StatusName(l.StatusCode) : ""));
			}
			string Stat(string name, Func<ControllerStatus, double> f, string fmt) =>
				string.Format(c, "{0} {1} / {2} / {3}", name, got.Average(f).ToString(fmt, c), got.Min(f).ToString(fmt, c), got.Max(f).ToString(fmt, c));
			sb.AppendLine("mean / min / max: " + string.Join(";  ", new[]
			{
				Stat("cadence rpm", s => s.CadenceRpm, "0"), Stat("km/h", s => Speed(s), "0.0"), Stat("motor %", s => s.TargetCurrentPercent, "0"),
				Stat("battery W", s => s.BatteryVolts * s.BatteryAmps, "0"), Stat("rider W", s => s.RiderWatts, "0"), Stat("V", s => s.BatteryVolts, "0.0"),
			}));
			sb.Append("flags: P pedalling, B brake, E effort estimate valid, W wheel moving");
			return new JsonObject { ["samples"] = rows, ["text"] = sb.ToString() };
		}

		private async Task<JsonObject> SetLevel(int level)
		{
			if (level < 0 || level > 9)
			{
				throw new Exception("Level must be 0-9 (walk mode isn't offered here).");
			}
			RequireConnected();
			await _io.WaitAsync();
			try
			{
				_desiredLevel = level;
				_conn.SetAssistLevel(level);
				_levelSentAt = DateTime.Now;
			}
			finally
			{
				_io.Release();
			}
			Mark($"level {level}");
			AddEvent($"level set to {level}");
			return new JsonObject { ["text"] = $"Assist level {level}." };
		}

		private async Task<JsonObject> SetLights(bool on)
		{
			RequireConnected();
			await _io.WaitAsync();
			try
			{
				_conn.SetLights(on);
			}
			finally
			{
				_io.Release();
			}
			return new JsonObject { ["text"] = on ? "Lights on." : "Lights off." };
		}

		private async Task<T> Exclusive<T>(Func<Task<T>> f)
		{
			RequireConnected();
			await _io.WaitAsync();
			try
			{
				return await f();
			}
			finally
			{
				_io.Release();
			}
		}

		private async Task<Configuration> ReadConfig()
		{
			var r = await Exclusive(() => _conn.ReadConfiguration(TimeSpan.FromSeconds(10)));
			if (r.Timeout || r.Result == null)
			{
				throw new Exception("The controller didn't send its settings.");
			}
			_config = r.Result;
			return r.Result;
		}

		private async Task<JsonObject> ConfigShow(string filter)
		{
			var cfg = await ReadConfig();
			var fields = ConfigFields.All(cfg).Where(f => string.IsNullOrEmpty(filter) || f.Name.IndexOf(filter, StringComparison.OrdinalIgnoreCase) >= 0);
			var o = new JsonObject();
			var sb = new StringBuilder();
			foreach (var f in fields)
			{
				o[f.Name] = f.Text;
				sb.AppendLine($"{f.Name} = {f.Text}");
			}
			return new JsonObject { ["settings"] = o, ["text"] = sb.ToString().TrimEnd() };
		}

		private async Task<JsonObject> ConfigGet(List<string> names)
		{
			var cfg = await ReadConfig();
			var o = new JsonObject();
			var sb = new StringBuilder();
			foreach (var n in names)
			{
				var f = ConfigFields.Find(cfg, n);
				o[f.Name] = f.Text;
				sb.AppendLine($"{f.Name} = {f.Text}  ({f.Type})");
			}
			return new JsonObject { ["settings"] = o, ["text"] = sb.ToString().TrimEnd() };
		}

		private async Task<JsonObject> ConfigSet(List<string> assignments, bool battery, bool dryRun)
		{
			var before = await ReadConfig();
			var after = ConfigFields.Clone(before);
			foreach (var a in assignments)
			{
				int eq = a.IndexOf('=');
				if (eq <= 0)
				{
					throw new Exception($"'{a}': expected NAME=VALUE.");
				}
				ConfigFields.Set(ConfigFields.Find(after, a.Substring(0, eq).Trim()), a.Substring(eq + 1).Trim());
			}
			return await Write(before, after, battery, dryRun, true);
		}

		private async Task<JsonObject> ConfigUndo()
		{
			if (_undo.Count == 0)
			{
				throw new Exception("Nothing to undo in this session.");
			}
			var path = _undo.Peek();
			var before = await ReadConfig();
			var restored = new Configuration(before.Target);
			restored.ReadFromFile(path);
			var reply = await Write(before, restored, true, false, false);
			_undo.Pop();
			reply["text"] = $"Restored {path}\n" + (string)reply["text"];
			return reply;
		}

		private async Task<JsonObject> ConfigSave(string path)
		{
			var cfg = await ReadConfig();
			path = Path.GetFullPath(string.IsNullOrEmpty(path) ? Path.Combine(_logDir, "backups", $"config-{Now()}.xml") : path);
			cfg.WriteToFile(path);
			return new JsonObject { ["path"] = path, ["text"] = "Saved the controller's settings to " + path };
		}

		private async Task<JsonObject> ConfigLoad(string path, bool battery, bool dryRun)
		{
			var before = await ReadConfig();
			var loaded = new Configuration(before.Target);
			loaded.ReadFromFile(Path.GetFullPath(path));
			return await Write(before, loaded, battery, dryRun, true);
		}

		private async Task<JsonObject> Write(Configuration before, Configuration after, bool battery, bool dryRun, bool pushUndo)
		{
			var diff = ConfigFields.Diff(before, after);
			var changes = new JsonArray();
			var sb = new StringBuilder();
			foreach (var d in diff)
			{
				changes.Add(new JsonObject { ["name"] = d.name, ["from"] = d.from, ["to"] = d.to });
				sb.AppendLine($"  {d.name}: {d.from} -> {d.to}");
			}
			if (diff.Count == 0)
			{
				return new JsonObject { ["changes"] = changes, ["text"] = "Nothing to change: the controller already has those settings." };
			}

			if (!battery && diff.Any(d => ConfigFields.BatteryFields.Contains(d.name)))
			{
				throw new Exception("Refused: that changes battery voltages; add --battery if you mean it.\n" + sb.ToString().TrimEnd());
			}
			if (after.MaxCurrentAmps > _maxAmps)
			{
				throw new Exception($"Refused: Max Current {after.MaxCurrentAmps} A is over this link's cap of {_maxAmps} A (serve --max-amps).");
			}
			after.Validate();

			if (dryRun)
			{
				return new JsonObject { ["changes"] = changes, ["text"] = "Would change (dry run, nothing written):\n" + sb.ToString().TrimEnd() };
			}

			if (_configVersion != Configuration.CurrentVersion)
			{
				throw new Exception($"Refused: the controller's config version is {_configVersion}, this link writes version {Configuration.CurrentVersion}. Flash the matching firmware first.");
			}
			RequireStill();

			var backup = Path.Combine(_logDir, "backups", $"config-before-{Now()}.xml");
			before.WriteToFile(backup);

			var w = await Exclusive(() => _conn.WriteConfiguration(after, TimeSpan.FromSeconds(10)));
			if (w.Timeout || !w.Result)
			{
				throw new Exception("The controller didn't accept the settings (" + (w.Timeout ? "no answer" : "refused") + "). Backup: " + backup);
			}
			var check = await ReadConfig();
			bool same = check.WriteToBuffer().SequenceEqual(after.WriteToBuffer());
			if (pushUndo)
			{
				_undo.Push(backup);
			}
			var summary = string.Join(", ", diff.Select(d => $"{d.name}={d.to}"));
			Mark("config: " + summary);
			AddEvent("config written: " + summary);
			if (!same)
			{
				throw new Exception("Written, but reading back gave different settings. Check with `config show`; backup: " + backup);
			}
			return new JsonObject
			{
				["changes"] = changes,
				["backup"] = backup,
				["text"] = "Written and read back:\n" + sb.ToString().TrimEnd() + "\nBackup: " + backup + " (bbs-link config undo)",
			};
		}

		private async Task<JsonObject> Capture()
		{
			RequireConnected();
			var result = await Exclusive(() => PasCaptureSession.Run(_conn, null, _stop.Token));
			if (!result.ok)
			{
				throw new Exception(result.message);
			}
			var path = Path.Combine(_logDir, $"pas-capture-{Now()}.csv");
			File.WriteAllText(path, PasCaptureAnalysis.ToCsv(result.edges));
			var a = PasCaptureAnalysis.Analyse(result.edges);
			return new JsonObject
			{
				["usable"] = a.Usable,
				["jitter_pct"] = Math.Round(a.JitterPercent, 3),
				["jitter_ratio"] = Math.Round(a.JitterRatio, 2),
				["cadence_rpm"] = Math.Round(a.CadenceRpm, 1),
				["path"] = path,
				["text"] = a.Summary() + "\nEdges saved to " + path,
			};
		}

		private async Task<JsonObject> ResetEffort()
		{
			RequireStill();
			var r = await Exclusive(() => _conn.ResetEffortPattern(TimeSpan.FromSeconds(5)));
			if (r.Timeout || !r.Result)
			{
				throw new Exception("The controller didn't reset the learned pattern.");
			}
			AddEvent("learned PAS pattern reset");
			return new JsonObject { ["text"] = "Learned pedal sensor pattern reset: it's learned again on the next fast ride." };
		}

		private async Task<JsonObject> CalibrateVoltage(double volts)
		{
			if (volts < 20 || volts > 100)
			{
				throw new Exception("Give the battery voltage you measured, 20-100 V.");
			}
			RequireStill();
			var r = await Exclusive(() => _conn.CalibrateBatteryVoltage((float)volts, TimeSpan.FromSeconds(5)));
			if (r.Timeout || !r.Result)
			{
				throw new Exception("The controller didn't take the calibration.");
			}
			AddEvent($"battery voltage calibrated to {volts:0.0} V");
			return new JsonObject { ["text"] = $"Calibrated: the controller now reads {volts:0.0} V for the present battery voltage." };
		}

		private JsonObject Events(int count)
		{
			List<string> last;
			lock (_lock)
			{
				last = _events.Skip(Math.Max(0, _events.Count - count)).ToList();
			}
			var arr = new JsonArray();
			foreach (var e in last)
			{
				arr.Add(e);
			}
			return new JsonObject { ["events"] = arr, ["text"] = last.Count == 0 ? "No events yet." : string.Join("\n", last) };
		}
	}
}
