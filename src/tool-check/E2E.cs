// End-to-end tests of the config tool's model code against a virtual
// controller: the ride simulator running the real firmware protocol code
// (extcom.c) on a pseudo-terminal (ride_sim --serial), and a stand-in for
// stcgal (fake_stcgal.py). Linux only. See check.sh.

using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
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

static class E2E
{
	static int failures;
	static string rideSim;
	static string simDir;
	static string here;

	static void Check(string what, bool ok, string detail = "")
	{
		Console.WriteLine($"{(ok ? "PASS" : "FAIL")} {what}{(detail.Length > 0 ? ": " + detail : "")}");
		if (!ok)
		{
			failures++;
		}
	}

	class VirtualController : IDisposable
	{
		public Process Process;
		public string Port;

		public static VirtualController Start(string scenario)
		{
			var psi = new ProcessStartInfo(rideSim) { RedirectStandardOutput = true, UseShellExecute = false };
			psi.ArgumentList.Add(Path.Combine(simDir, scenario));
			psi.ArgumentList.Add("/dev/null");
			psi.ArgumentList.Add("--serial");
			var p = Process.Start(psi);
			var line = p.StandardOutput.ReadLine() ?? "";
			return new VirtualController { Process = p, Port = line.Replace("serial: ", "").Trim() };
		}

		public void Dispose()
		{
			try { Process.Kill(); } catch { }
			Process.Dispose();
		}
	}

	public static async Task<int> Run(string[] args)
	{
		BbsfwConnection.OpenTransport = p => new PtyTransport(p.Name);
		rideSim = Path.GetFullPath(args[0]);
		simDir = Path.GetFullPath(args[1]);
		here = AppContext.BaseDirectory;

		await BenchProtocol();
		await SteadinessCheck("bench.sim", expectClean: true);
		await SteadinessCheck("bench_noisy.sim", expectClean: false);
		await FlashWizard();
		await StcgalParser();
		await RealStcgal();

		Console.WriteLine(failures == 0 ? "End-to-end checks passed." : $"{failures} end-to-end check(s) failed.");
		return failures == 0 ? 0 : 1;
	}

	static async Task BenchProtocol()
	{
		using var vc = VirtualController.Start("bench.sim");
		var conn = new BbsfwConnection();
		var port = new ComPort(vc.Port, vc.Port);
		Check("connect", await conn.Connect(port, TimeSpan.FromSeconds(5)), vc.Port);
		Check("firmware supports bench", conn.SupportsBench, conn.FirmwareVersion?.ToString() ?? "none");

		var cfg = await conn.ReadConfiguration(TimeSpan.FromSeconds(5));
		Check("read config", !cfg.Timeout && cfg.Result != null);

		await Task.Delay(3500);	// pedalling starts at 2 s
		var st = await conn.ReadStatus(TimeSpan.FromSeconds(2));
		Check("status", !st.Timeout && st.Result != null);
		if (st.Result != null)
		{
			Check("status cadence", Math.Abs(st.Result.CadenceRpm - 63) < 5, st.Result.CadenceRpm.ToString());
			Check("status pedalling", st.Result.PedalingForwards);
			Check("status battery", Math.Abs(st.Result.BatteryVolts - 52) < 0.5f, st.Result.BatteryVolts.ToString());
			Check("status speed", Math.Abs(st.Result.SpeedKph(cfg.Result.WheelSizeInch) - 63 * 2.0 * cfg.Result.WheelSizeInch * 0.0254 * Math.PI * 60 / 1000) < 1.5,
				st.Result.SpeedKph(cfg.Result.WheelSizeInch).ToString("0.0"));
			Check("status level 0", st.Result.AssistLevel == 0);
			Check("status csv", st.Result.ToCsv().Split(',').Length == ControllerStatus.CsvHeader.Split(',').Length);
		}

		conn.SetAssistLevel(3);
		conn.SetLights(true);
		await Task.Delay(300);
		st = await conn.ReadStatus(TimeSpan.FromSeconds(2));
		Check("display write: level 3", st.Result?.AssistLevel == 3, st.Result?.AssistLevel.ToString());
		Check("display write: lights", st.Result?.Lights == true);
		await Task.Delay(1000);
		st = await conn.ReadStatus(TimeSpan.FromSeconds(2));
		Check("motor assists at level 3", st.Result?.TargetCurrentPercent > 0, st.Result?.TargetCurrentPercent.ToString());

		conn.SetAssistLevel(10);
		await Task.Delay(300);
		st = await conn.ReadStatus(TimeSpan.FromSeconds(2));
		Check("display write: walk", st.Result?.AssistLevel == 0x0a, st.Result?.AssistLevel.ToString());
		conn.SetAssistLevel(0);

		var reset = await conn.ResetEffortPattern(TimeSpan.FromSeconds(2));
		Check("reset effort pattern", !reset.Timeout && reset.Result);

		conn.Close();
	}

	static async Task SteadinessCheck(string scenario, bool expectClean)
	{
		using var vc = VirtualController.Start(scenario);
		var conn = new BbsfwConnection();
		Check($"{scenario}: connect", await conn.Connect(new ComPort(vc.Port, vc.Port), TimeSpan.FromSeconds(5)));
		await Task.Delay(3500);

		var progress = new List<string>();
		var (ok, message, edges) = await PasCaptureSession.Run(conn, new Progress<string>(progress.Add), CancellationToken.None);
		Check($"{scenario}: capture", ok, message);
		Check($"{scenario}: progress reported", progress.Count > 0);
		if (ok)
		{
			var a = PasCaptureAnalysis.Analyse(edges);
			Console.WriteLine("  " + a.Summary().Replace("\n", "\n  "));
			Check($"{scenario}: capture size", edges.Count == PasCaptureSession.EdgesPerCapture, edges.Count.ToString());
			Check($"{scenario}: forward order", a.OrderErrors == 0, a.OrderErrors.ToString());
			Check($"{scenario}: cadence", Math.Abs(a.CadenceRpm - 63) < 2, a.CadenceRpm.ToString("0.0"));
			Check($"{scenario}: channel shares", Math.Abs(a.StateSharePercent.Sum() - 100) < 0.1 && a.StateSharePercent.All(p => p > 15 && p < 35),
				string.Join("/", a.StateSharePercent.Select(p => p.ToString("0"))));
			if (expectClean)
			{
				Check($"{scenario}: judged clean (timing jitter as predicted)", a.Usable && a.JitterRatio > 0.7 && a.JitterRatio < 1.3, a.JitterRatio.ToString("0.00"));
			}
			else
			{
				Check($"{scenario}: judged noisy", a.Usable && a.JitterRatio >= 3, a.JitterRatio.ToString("0.00"));
			}
			Check($"{scenario}: csv", PasCaptureAnalysis.ToCsv(edges).Split('\n').Length >= edges.Count + 1);
		}
		conn.Close();
	}

	static FirmwarePackage FakePackage(Version version)
	{
		var hex = Path.Combine(Path.GetTempPath(), "bbs-fw-e2e.hex");
		File.WriteAllText(hex, ":00000001FF\n");
		return FirmwarePackage.Custom("python3", new[] { Path.Combine(here, "fake_stcgal.py") }, hex, version);
	}

	static async Task FlashWizard()
	{
		using var vc = VirtualController.Start("bench.sim");
		var port = new ComPort(vc.Port, vc.Port);
		var backupDir = Path.Combine(Path.GetTempPath(), "bbs-fw-e2e-backups");

		var update = new FirmwareUpdate(FakePackage(new Version(2, 2, 0)), port);
		Check("wizard: package complete", update.Package.IsComplete);

		var backup = await update.BackupSettings(backupDir, TimeSpan.FromSeconds(5));
		Check("wizard: backup saved", backup.Saved && File.Exists(update.BackupPath), backup.Message);
		Check("wizard: previous firmware", update.PreviousFirmware == new Version(2, 2, 0), update.PreviousFirmware?.ToString());

		// chip check, with the "cycle power" prompt
		var stcgal = update.Package.CreateStcgal();
		int prompts = 0;
		stcgal.WaitingForPower += () => prompts++;
		var chip = await update.IdentifyChip(stcgal, CancellationToken.None);
		Check("wizard: supported chip", chip.Supported, chip.Message);
		Check("wizard: power prompt seen", prompts == 1, prompts.ToString());
		Check("wizard: chip frequency", chip.Result.McuFrequencyMhz == 20.0);

		Environment.SetEnvironmentVariable("FAKE_STCGAL_CHIP", "STC12C5A60S2");
		chip = await update.IdentifyChip(update.Package.CreateStcgal(), CancellationToken.None);
		Check("wizard: unsupported chip refused", !chip.Supported && chip.Message.Contains("STC12C5A60S2"), chip.Message);
		Environment.SetEnvironmentVariable("FAKE_STCGAL_CHIP", "IAP15W4K61S4");
		chip = await update.IdentifyChip(update.Package.CreateStcgal(), CancellationToken.None);
		Check("wizard: BBSHD 1.5 chip accepted", chip.Supported, chip.Message);
		Environment.SetEnvironmentVariable("FAKE_STCGAL_CHIP", null);

		Environment.SetEnvironmentVariable("FAKE_STCGAL_FAIL", "connect");
		chip = await update.IdentifyChip(update.Package.CreateStcgal(), CancellationToken.None);
		Check("wizard: no answer reported", !chip.Supported && chip.Message.Contains("Protocol error"), chip.Message);
		Environment.SetEnvironmentVariable("FAKE_STCGAL_FAIL", null);

		// flash: arguments, prompt, progress
		var argsFile = Path.Combine(Path.GetTempPath(), "bbs-fw-e2e-args.txt");
		Environment.SetEnvironmentVariable("FAKE_STCGAL_ARGS", argsFile);
		stcgal = update.Package.CreateStcgal();
		var progress = new List<int>();
		prompts = 0;
		stcgal.Progress += progress.Add;
		stcgal.WaitingForPower += () => prompts++;
		var flash = await update.Flash(stcgal, CancellationToken.None);
		Environment.SetEnvironmentVariable("FAKE_STCGAL_ARGS", null);
		Check("wizard: flash ok", flash.Success, flash.Error ?? "");
		Check("wizard: flash prompt", prompts == 1);
		Check("wizard: flash progress", progress.Count >= 5 && progress.Last() == 100, string.Join(",", progress));
		var sent = File.ReadAllLines(argsFile);
		Check("wizard: stcgal arguments", sent.SequenceEqual(new[] { "-P", "stc15", "-t", "20000", "-b", "19200", "-p", vc.Port, update.Package.HexPath }),
			string.Join(" ", sent));

		Environment.SetEnvironmentVariable("FAKE_STCGAL_FAIL", "write");
		flash = await update.Flash(update.Package.CreateStcgal(), CancellationToken.None);
		Environment.SetEnvironmentVariable("FAKE_STCGAL_FAIL", null);
		Check("wizard: failed write reported", !flash.Success && flash.Error.Contains("checksum"), flash.Error ?? "");

		// cancelling while it waits for power
		using (var cts = new CancellationTokenSource(200))
		{
			var cancelled = await update.Flash(update.Package.CreateStcgal(), cts.Token);
			Check("wizard: cancel", !cancelled.Success && cancelled.Error == "Cancelled.", cancelled.Error ?? "");
		}

		// verify: the virtual controller runs this build
		var verify = await update.Verify(TimeSpan.FromSeconds(5));
		Check("wizard: verify version", verify.FirmwareOk, verify.Message);
		Check("wizard: settings kept", verify.SettingsKept, verify.Message);

		var other = new FirmwareUpdate(FakePackage(new Version(9, 9, 9)), port);
		verify = await other.Verify(TimeSpan.FromSeconds(5));
		Check("wizard: wrong version noticed", !verify.FirmwareOk && verify.Message.Contains("9.9.9"), verify.Message);

		// restore: change a setting, restore the backup, check it's back
		var conn = new BbsfwConnection();
		await conn.Connect(port, TimeSpan.FromSeconds(5));
		var cfg = (await conn.ReadConfiguration(TimeSpan.FromSeconds(5))).Result;
		cfg.MaxCurrentAmps = 20;
		await conn.WriteConfiguration(cfg, TimeSpan.FromSeconds(5));
		conn.Close();
		verify = await update.Verify(TimeSpan.FromSeconds(5));
		Check("wizard: changed settings noticed", verify.FirmwareOk && !verify.SettingsKept, verify.Message);
		Check("wizard: restore", await update.RestoreBackup(TimeSpan.FromSeconds(5)));
		verify = await update.Verify(TimeSpan.FromSeconds(5));
		Check("wizard: restored", verify.SettingsKept, verify.Message);
	}

	// The real stcgal, through the entry script the bundled stcgal.exe is built
	// from: its power prompt (no newline) must reach the tool through a pipe
	// while it waits.
	// No chip answers on the virtual controller, so it waits until cancelled.
	static async Task RealStcgal()
	{
		var python = Environment.GetEnvironmentVariable("STCGAL_PYTHON");
		if (string.IsNullOrEmpty(python))
		{
			Console.WriteLine("SKIP real stcgal (set STCGAL_PYTHON to a python with stcgal 1.10)");
			return;
		}

		using var vc = VirtualController.Start("bench.sim");
		var entry = Path.Combine(here, "stcgal_entry.py");
		var stcgal = new Stcgal(python, entry);
		var prompted = new TaskCompletionSource<bool>();
		stcgal.WaitingForPower += () => prompted.TrySetResult(true);

		using var cts = new CancellationTokenSource();
		var run = stcgal.Identify(vc.Port, cts.Token);
		var first = await Task.WhenAny(prompted.Task, Task.Delay(10000));
		Check("real stcgal: power prompt reaches the tool while it waits", first == prompted.Task);
		cts.Cancel();
		var r = await run;
		Check("real stcgal: cancel", !r.Success && r.Error == "Cancelled.", r.Error ?? "");
	}

	static Task StcgalParser()
	{
		// real stcgal output shapes: prompt without newline, tqdm on \r
		var p = new StcgalOutputParser();
		int prompts = 0;
		var progress = new List<int>();
		p.WaitingForPower += () => prompts++;
		p.Progress += progress.Add;
		p.Feed("Waiting for MCU, please cyc");
		p.Feed("le power: ");
		Check("parser: prompt split across reads", prompts == 1);
		p.Feed("done\nTarget model:\n  Name: STC15W4K56S4\n  Magic: F528\nTarget frequency: 20.000 MHz\n");
		p.Feed("Target options:\n  eeprom_erase_enabled=True\n");
		p.Feed("\rWriting flash:   0%|          | 0/100\rWriting flash:  50%|#####     | 50/100\rWriting flash: 100%|##########| 100/100\n");
		var r = p.Result();
		Check("parser: name", r.McuName == "STC15W4K56S4");

		// an error on stderr while stdout is mid-line (the prompt)
		var q = new StcgalOutputParser();
		q.Feed("Waiting for MCU, please cycle power: ", 0);
		q.Feed("Protocol error: incorrect magic in status packet\n", 1);
		q.Feed("\n", 0);
		Check("parser: stderr error during the prompt", q.Result().Error == "Protocol error: incorrect magic in status packet", q.Result().Error ?? "none");
		Check("parser: frequency", r.McuFrequencyMhz == 20.0);
		Check("parser: eeprom erase", r.EepromEraseEnabled == true);
		Check("parser: progress", progress.SequenceEqual(new[] { 0, 50, 100 }), string.Join(",", progress));
		Check("parser: no error", r.Error == null);
		return Task.CompletedTask;
	}
}
