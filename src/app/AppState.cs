// The app's state: the link to the controller, the tune on the bike and the
// working copy, live data, and the assist map built from them.

using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Threading;
using System.Threading.Tasks;
using System.Windows;
using BBSFW.Model;

namespace BBSFW.App
{
	public class Observable : INotifyPropertyChanged
	{
		public event PropertyChangedEventHandler PropertyChanged;
		protected void Raise([CallerMemberName] string name = null) => PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(name));
		protected bool Set<T>(ref T field, T value, [CallerMemberName] string name = null)
		{
			if (Equals(field, value)) return false;
			field = value;
			Raise(name);
			return true;
		}
	}

	// One row of the assist map: a field of every level, how to show it and how to change it.
	public class MapRowDef
	{
		public string Label;
		public string Note;
		public Func<Configuration.AssistLevel, double> Get;
		public Action<Configuration.AssistLevel, double> Put;
		public double Min, Max, Step;
		public bool Chartable;
		public bool IsCurrent;
		public string Format = "0";
		public Func<Configuration.AssistLevel, double, string> Show;
		public Func<Configuration.AssistLevel, bool> Muted = _ => false;
	}

	public class MapCell : Observable
	{
		private string _text;
		public MapRow Row { get; set; }
		public int Level { get; set; }
		public string Text { get => _text; set => Set(ref _text, value); }
		public bool Changed { get; set; }
		public bool Muted { get; set; }
		public bool Editable { get; set; }
		public string Tooltip { get; set; }
	}

	public class MapRow : Observable
	{
		private bool _selected;
		public MapRowDef Def { get; set; }
		public string Label => Def.Label;
		public string Note => Def.Note;
		public bool Chartable => Def.Chartable;
		public bool Selected { get => _selected; set => Set(ref _selected, value); }
		public ObservableCollection<MapCell> Cells { get; } = new ObservableCollection<MapCell>();
	}

	public class AppState : Observable
	{
		public const uint BbshdLimitAmps = 33;

		private readonly BbsfwConnection _conn = new BbsfwConnection();
		private readonly SemaphoreSlim _io = new SemaphoreSlim(1, 1);
		private CancellationTokenSource _poll;

		private Configuration _baseline;		// on the bike, or the file that was opened
		private Configuration _working;
		private ControllerStatus _live;
		private DateTime _liveAt;
		private string _status = "Not connected";
		private bool _connected;
		private bool _sport;
		private string _baselineLabel = "On bike";
		private ComPort _port;

		public AppState()
		{
			Rows = new ObservableCollection<MapRow>();
			RowDefs = BuildRowDefs();
			RefreshPorts();
			_conn.Disconnected += () => Application.Current?.Dispatcher.BeginInvoke(new Action(() => OnLost("The controller stopped answering. Replug the USB end of the cable, then connect again.")));
		}

		// --- connection ---

		public List<ComPort> Ports { get; private set; }
		public ComPort Port { get => _port; set => Set(ref _port, value); }
		public bool IsConnected { get => _connected; private set { if (Set(ref _connected, value)) { Raise(nameof(CanWrite)); Raise(nameof(ConnectLabel)); } } }
		public string ConnectLabel => IsConnected ? "Disconnect" : "Connect";
		public string Status { get => _status; set => Set(ref _status, value); }
		public string Firmware { get; private set; }

		public void RefreshPorts()
		{
			Ports = BbsfwConnection.GetComPorts();
			Raise(nameof(Ports));
			if (Port == null || !Ports.Any(p => p.Name == Port.Name))
			{
				Port = Ports.FirstOrDefault();
			}
		}

		public async Task ConnectAsync()
		{
			if (IsConnected)
			{
				Disconnect();
				return;
			}
			RefreshPorts();
			if (Port == null)
			{
				Status = "No cable found. Plug the USB end in first, then the motor end.";
				return;
			}
			Status = $"Connecting on {Port.Name}…";
			bool ok;
			try
			{
				ok = await _conn.Connect(Port, TimeSpan.FromSeconds(10));
			}
			catch (Exception e)
			{
				Status = $"Can't open {Port.Name}: {e.Message} Replug the USB end of the cable and try again.";
				return;
			}
			if (!ok)
			{
				Status = "The controller didn't answer. Switch the battery on, check the motor plug, then try again.";
				return;
			}
			Firmware = _conn.FirmwareVersion?.ToString() ?? "?";
			var cfg = await Exchange(c => c.ReadConfiguration(TimeSpan.FromSeconds(10)));
			if (cfg == null || cfg.Timeout || cfg.Result == null)
			{
				_conn.Close();
				Status = "Connected, but the controller didn't send its settings. Is it running bbs-fw 2.3?";
				return;
			}
			_conn.SetAssistLevel(0);
			SaveBackup(cfg.Result, "at-connect");
			_baseline = cfg.Result;
			_working = TuneDiff.Clone(cfg.Result);
			BaselineLabel = "On bike";
			IsConnected = true;
			Status = $"{Port.Name} · bbs-fw {Firmware}";
			Raise(nameof(Firmware));
			Rebuild();
			_poll = new CancellationTokenSource();
			_ = PollLoop(_poll.Token);
		}

		public void Disconnect()
		{
			_poll?.Cancel();
			try { _conn.SetAssistLevel(0); } catch { }
			_conn.Close();
			IsConnected = false;
			Status = "Disconnected · level left at 0";
		}

		private void OnLost(string why)
		{
			if (!IsConnected) return;
			_poll?.Cancel();
			IsConnected = false;
			Status = why;
		}

		private async Task<T> Exchange<T>(Func<BbsfwConnection, Task<T>> f) where T : class
		{
			await _io.WaitAsync();
			try { return await f(_conn); }
			catch { return null; }
			finally { _io.Release(); }
		}

		private async Task PollLoop(CancellationToken stop)
		{
			var levelAt = DateTime.MinValue;
			int misses = 0;
			while (!stop.IsCancellationRequested)
			{
				if (DateTime.Now - levelAt > TimeSpan.FromSeconds(2))
				{
					// as a display does: keep telling the controller the level (always 0 here)
					try { _conn.SetAssistLevel(0); } catch { }
					levelAt = DateTime.Now;
				}
				var r = await Exchange(c => c.ReadStatus(TimeSpan.FromMilliseconds(1500)));
				if (r != null && !r.Timeout && r.Result != null)
				{
					misses = 0;
					_live = r.Result;
					_liveAt = DateTime.Now;
					RaiseLive();
				}
				else if (++misses >= 8)
				{
					OnLost("No live data for a few seconds. If the bike is on, replug the USB end of the cable and connect again.");
					return;
				}
				try { await Task.Delay(250, stop); } catch (TaskCanceledException) { return; }
			}
		}

		// --- live data ---

		public string LiveLevel => _live == null ? "–" : _live.AssistLevel.ToString();
		public string LiveCadence => _live == null ? "–" : $"{_live.CadenceRpm:0} rpm";
		public string LiveSpeed => _live == null || _baseline == null ? "–" : $"{_live.WheelRpm * _baseline.WheelSizeInch * 0.0254 * Math.PI * 60 / 1000:0.0} km/h";
		public string LiveMotor => _live == null ? "–" : $"{_live.BatteryAmps:0.0} A";
		public string LiveBattery => _live == null ? "–" : $"{_live.BatteryVolts:0.0} V · {_live.BatteryPercent}%";
		public string LiveTemp => _live == null ? "–" : $"{_live.TemperatureC} °C";
		public string LiveLearned => _live == null ? "–" : $"{_live.EffortLearnedPercent} %";

		private void RaiseLive()
		{
			foreach (var n in new[] { nameof(LiveLevel), nameof(LiveCadence), nameof(LiveSpeed), nameof(LiveMotor), nameof(LiveBattery), nameof(LiveTemp), nameof(LiveLearned), nameof(StillText) })
			{
				Raise(n);
			}
		}

		// Why the bike isn't still, or null if it is (fresh live data, nothing turning, throttle closed).
		public string NotStillReason()
		{
			if (!IsConnected) return "not connected";
			if (_live == null || DateTime.Now - _liveAt > TimeSpan.FromSeconds(2)) return "no fresh live data";
			var why = new List<string>();
			if (_live.PedalingForwards || _live.PedalingBackwards || _live.CadenceRpm > 0) why.Add("the cranks are turning");
			if (_live.WheelMoving || _live.WheelRpm > 0) why.Add("the wheel is turning");
			if (_live.ThrottlePercent > 0) why.Add("the throttle is open");
			return why.Count == 0 ? null : string.Join(", ", why);
		}

		public string StillText => !IsConnected ? "" : NotStillReason() == null ? "bike still" : "moving";

		// --- tunes ---

		public Configuration Baseline => _baseline;
		public Configuration Working => _working;
		public string BaselineLabel { get => _baselineLabel; private set => Set(ref _baselineLabel, value); }
		public List<SettingChange> Changes => TuneDiff.Compare(_baseline, _working);
		public int ChangeCount => Changes.Count;
		public bool HasTune => _working != null;
		public bool CanWrite => IsConnected && ChangeCount > 0;
		public uint LimitAmps => _working == null || _working.Target == BbsfwConnection.Controller.Unknown ? BbshdLimitAmps : Math.Min(BbshdLimitAmps, _working.MaxCurrentLimitAmps);

		public void OpenFile(string path)
		{
			var cfg = new Configuration();
			cfg.ReadFromFile(path);
			if (!IsConnected)
			{
				_baseline = TuneDiff.Clone(cfg);
				BaselineLabel = Path.GetFileNameWithoutExtension(path);
			}
			_working = cfg;
			Rebuild();
		}

		public void SaveFile(string path) => _working?.WriteToFile(path);

		public void Discard()
		{
			if (_baseline == null) return;
			_working = TuneDiff.Clone(_baseline);
			Rebuild();
		}

		public static string AppDataDir(string sub)
		{
			var dir = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "BBS-FW-AI", sub);
			Directory.CreateDirectory(dir);
			return dir;
		}

		private static string SaveBackup(Configuration cfg, string tag)
		{
			var path = Path.Combine(AppDataDir("Backups"), $"{tag}-{DateTime.Now:yyyyMMdd-HHmmss}.xml");
			cfg.WriteToFile(path);
			return path;
		}

		// Write the working copy: still bike, within limits, backup first, read back after.
		public async Task<string> WriteAsync(bool allowBattery)
		{
			var still = NotStillReason();
			if (still != null) return $"Not written: settings only change with the bike still ({still}).";
			var changes = Changes;
			if (changes.Count == 0) return "Nothing to write.";
			if (!allowBattery && changes.Any(c => c.IsBattery)) return "Not written: battery voltages change. Tick the battery confirmation to allow it.";
			if (_working.MaxCurrentAmps > LimitAmps) return $"Not written: Max Current {_working.MaxCurrentAmps} A is over {LimitAmps} A.";
			try { _working.Validate(); }
			catch (Exception e) { return "Not written: " + e.Message; }

			var backup = SaveBackup(_baseline, "before-write");
			var w = await Exchange(c => c.WriteConfiguration(_working, TimeSpan.FromSeconds(10)));
			if (w == null || w.Timeout || !w.Result) return $"The controller didn't accept the settings. Nothing changed on the bike; backup at {backup}.";
			var back = await Exchange(c => c.ReadConfiguration(TimeSpan.FromSeconds(10)));
			if (back == null || back.Timeout || back.Result == null) return $"Written, but reading back failed: check before riding. Backup at {backup}.";
			var left = TuneDiff.Compare(back.Result, _working);
			_baseline = back.Result;
			_working = TuneDiff.Clone(back.Result);
			Rebuild();
			return left.Count == 0
				? $"Written and read back: {changes.Count} change{(changes.Count == 1 ? "" : "s")} on the bike. Backup saved first."
				: $"Written, but {left.Count} setting{(left.Count == 1 ? "" : "s")} read back differently: check before riding.";
		}

		// --- the assist map ---

		public bool Sport { get => _sport; set { if (Set(ref _sport, value)) Rebuild(); } }
		private bool _showBaseline;
		public bool ShowingBaseline { get => _showBaseline; set { if (Set(ref _showBaseline, value)) Rebuild(); } }
		public List<MapRowDef> RowDefs { get; }
		public ObservableCollection<MapRow> Rows { get; }
		public MapRow SelectedRow => Rows.FirstOrDefault(r => r.Selected);
		public event Action MapChanged;

		public Configuration.AssistLevel[] Levels(Configuration c) => c == null ? null : (Sport ? c.SportAssistLevels : c.StandardAssistLevels);

		private List<MapRowDef> BuildRowDefs()
		{
			string pct(Configuration.AssistLevel l, double v) => v.ToString("0", CultureInfo.InvariantCulture);
			bool noThrottle(Configuration.AssistLevel l) => (l.Type & Configuration.AssistFlagsType.Throttle) == 0;
			return new List<MapRowDef>
			{
				new MapRowDef { Label = "Assist type", Get = l => (double)l.Type, Show = (l, v) => TypeName(l.Type), Muted = _ => true },
				new MapRowDef { Label = "Current %", Get = l => l.MaxCurrentPercent, Put = (l, v) => l.MaxCurrentPercent = (uint)v, Min = 0, Max = 100, Step = 1, Chartable = true, IsCurrent = true, Show = pct },
				new MapRowDef { Label = "Top speed %", Get = l => l.MaxSpeedPercent, Put = (l, v) => l.MaxSpeedPercent = (uint)v, Min = 0, Max = 100, Step = 1, Chartable = true, Show = pct },
				new MapRowDef { Label = "Cadence limit %", Get = l => l.MaxCadencePercent, Put = (l, v) => l.MaxCadencePercent = (uint)v, Min = 0, Max = 100, Step = 1, Chartable = true, Show = pct },
				new MapRowDef { Label = "Throttle %", Note = "only on throttle levels", Get = l => l.MaxThrottlePercent, Put = (l, v) => l.MaxThrottlePercent = (uint)v, Min = 0, Max = 100, Step = 1, Chartable = true, Show = pct, Muted = noThrottle },
				new MapRowDef { Label = "Ramp A/s", Note = "0 = global", Get = l => l.CurrentRampAmpsSecond, Put = (l, v) => l.CurrentRampAmpsSecond = (uint)v, Min = 0, Max = 255, Step = 1, Chartable = true,
					Show = (l, v) => v == 0 ? $"{(_working?.CurrentRampAmpsSecond ?? 0)}*" : v.ToString("0", CultureInfo.InvariantCulture), Muted = l => l.CurrentRampAmpsSecond == 0 },
				new MapRowDef { Label = "Effort assist ×", Get = l => l.EffortAssistFactor, Put = (l, v) => l.EffortAssistFactor = (float)Math.Round(v, 1), Min = 0, Max = 25.5, Step = 0.1, Chartable = true,
					Show = (l, v) => v == 0 ? "off" : v.ToString("0.0", CultureInfo.InvariantCulture), Muted = l => l.EffortAssistFactor == 0 },
			};
		}

		private static string TypeName(Configuration.AssistFlagsType t)
		{
			if (t == Configuration.AssistFlagsType.None) return "off";
			var parts = new List<string>();
			if ((t & Configuration.AssistFlagsType.PasPower) != 0) parts.Add("Power");
			else if ((t & Configuration.AssistFlagsType.PasVariable) != 0) parts.Add("Var");
			else if ((t & Configuration.AssistFlagsType.Pas) != 0) parts.Add("PAS");
			if ((t & Configuration.AssistFlagsType.Throttle) != 0) parts.Add("+T");
			if ((t & Configuration.AssistFlagsType.Cruise) != 0) parts.Add("+C");
			return string.Join("", parts);
		}

		public void Rebuild()
		{
			var selected = SelectedRow?.Def ?? RowDefs[1];
			Rows.Clear();
			var w = Levels(ShowingBaseline ? _baseline : _working);
			var b = Levels(_baseline);
			if (w != null)
			{
				foreach (var def in RowDefs)
				{
					var row = new MapRow { Def = def, Selected = def == selected };
					for (int i = 1; i <= 9; ++i)
					{
						var v = def.Get(w[i]);
						var was = b == null ? v : def.Get(b[i]);
						row.Cells.Add(new MapCell
						{
							Row = row,
							Level = i,
							Text = def.Show(w[i], v),
							Changed = Math.Abs(v - was) > 1e-6,
							Muted = def.Muted(w[i]),
							Editable = def.Put != null && !ShowingBaseline,
							Tooltip = Math.Abs(v - was) > 1e-6 ? $"was {def.Show(b[i], was)}" : null,
						});
					}
					Rows.Add(row);
				}
			}
			Raise(nameof(HasTune));
			Raise(nameof(ChangeCount));
			Raise(nameof(CanWrite));
			Raise(nameof(Rows));
			MapChanged?.Invoke();
		}

		public void Select(MapRow row)
		{
			foreach (var r in Rows) r.Selected = r == row;
			MapChanged?.Invoke();
		}

		// Set a level's value in the selected row (from the chart or a cell); returns false if refused.
		public bool SetValue(MapRowDef def, int level, double value)
		{
			var w = Levels(_working);
			if (w == null || def.Put == null || ShowingBaseline) return false;
			value = Math.Max(def.Min, Math.Min(def.Max, Math.Round(value / def.Step) * def.Step));
			def.Put(w[level], value);
			return true;
		}

		public void Commit() => Rebuild();

		public double Amps(double percent) => percent / 100.0 * (_working?.MaxCurrentAmps ?? 0);
	}
}
