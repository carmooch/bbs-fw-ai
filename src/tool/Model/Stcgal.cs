using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading;
using System.Threading.Tasks;

namespace BBSFW.Model
{
	public class StcgalResult
	{
		public bool Success;
		public int ExitCode;
		public string McuName;
		public double? McuFrequencyMhz;
		public string Protocol;
		public bool? EepromEraseEnabled;
		public string Error;
		public string Log;
	}

	// Runs stcgal (https://github.com/grigorig/stcgal), the open-source STC
	// flasher, and turns its output into events: when to switch the controller
	// on, which chip answered, and progress. stcgal prints its power prompt
	// without a newline and its progress bar with carriage returns, so output
	// is followed character by character.
	public class Stcgal
	{
		// BBSHD flashing settings, from the bbs-fw wiki: STC15 protocol and the
		// chip's internal oscillator trimmed to 20 MHz (-t is in kHz). The
		// transfer runs at 19200 baud, which programming cables handle reliably.
		public const string Protocol = "stc15";
		public const string TrimKhz = "20000";
		public const string TransferBaud = "19200";

		private readonly string _exe;
		private readonly string[] _prefixArgs;

		public event Action<string> Output;
		public event Action WaitingForPower;
		public event Action<int> Progress;

		// exe: stcgal.exe; prefixArgs come before stcgal's own arguments (for
		// running it through an interpreter, e.g. in tests)
		public Stcgal(string exe, params string[] prefixArgs)
		{
			_exe = exe;
			_prefixArgs = prefixArgs;
		}

		public static IList<string> IdentifyArgs(string port)
		{
			return new[] { "-P", "auto", "-p", port };
		}

		public static IList<string> FlashArgs(string port, string hexPath)
		{
			return new[] { "-P", Protocol, "-t", TrimKhz, "-b", TransferBaud, "-p", port, hexPath };
		}

		public string CommandLine(IList<string> args)
		{
			var all = new List<string>(_prefixArgs);
			all.AddRange(args);
			return Path.GetFileName(_exe) + " " + string.Join(" ", all.ConvertAll(Quote));
		}

		public Task<StcgalResult> Identify(string port, CancellationToken ct)
		{
			return Run(IdentifyArgs(port), ct);
		}

		public Task<StcgalResult> Flash(string port, string hexPath, CancellationToken ct)
		{
			return Run(FlashArgs(port, hexPath), ct);
		}

		public async Task<StcgalResult> Run(IList<string> args, CancellationToken ct)
		{
			var psi = new ProcessStartInfo(_exe)
			{
				UseShellExecute = false,
				CreateNoWindow = true,
				RedirectStandardOutput = true,
				RedirectStandardError = true,
				StandardOutputEncoding = Encoding.UTF8,
				StandardErrorEncoding = Encoding.UTF8,
			};
			foreach (var a in _prefixArgs)
			{
				psi.ArgumentList.Add(a);
			}
			foreach (var a in args)
			{
				psi.ArgumentList.Add(a);
			}
			psi.Environment["PYTHONUNBUFFERED"] = "1";

			var parser = new StcgalOutputParser();
			parser.WaitingForPower += () => WaitingForPower?.Invoke();
			parser.Progress += p => Progress?.Invoke(p);

			using (var process = new Process { StartInfo = psi })
			{
				try
				{
					process.Start();
				}
				catch (Exception ex)
				{
					return new StcgalResult { Success = false, ExitCode = -1, Error = "Couldn't start stcgal: " + ex.Message, Log = "" };
				}

				using (ct.Register(() => { try { process.Kill(); } catch { } }))
				{
					var stdout = Pump(process.StandardOutput, parser, 0);
					var stderr = Pump(process.StandardError, parser, 1);
					await Task.WhenAll(stdout, stderr);
					await process.WaitForExitAsync();
				}

				var result = parser.Result();
				result.ExitCode = process.ExitCode;
				result.Success = process.ExitCode == 0 && !ct.IsCancellationRequested && result.Error == null;
				if (ct.IsCancellationRequested && result.Error == null)
				{
					result.Error = "Cancelled.";
				}
				else if (!result.Success && result.Error == null)
				{
					result.Error = $"stcgal stopped with exit code {process.ExitCode}.";
				}
				return result;
			}
		}

		private async Task Pump(StreamReader reader, StcgalOutputParser parser, int stream)
		{
			var buf = new char[256];
			int n;
			while ((n = await reader.ReadAsync(buf, 0, buf.Length)) > 0)
			{
				var text = new string(buf, 0, n);
				lock (parser)
				{
					parser.Feed(text, stream);
				}
				Output?.Invoke(text);
			}
		}

		private static string Quote(string s)
		{
			return s.Contains(" ") ? "\"" + s + "\"" : s;
		}
	}

	// Recognises stcgal 1.x messages in its output stream.
	public class StcgalOutputParser
	{
		private readonly StringBuilder _log = new StringBuilder();
		// one line per stream: stcgal's stdout and stderr interleave
		// unpredictably, and its power prompt has no newline
		private readonly StringBuilder[] _lines = { new StringBuilder(), new StringBuilder() };
		private bool _announcedPower;
		private int _lastProgress = -1;

		private string _mcuName;
		private double? _frequency;
		private string _protocol;
		private bool? _eepromErase;
		private string _error;

		private static readonly Regex NameRe = new Regex(@"^\s*Name:\s*(\S+)");
		private static readonly Regex FrequencyRe = new Regex(@"^Target frequency:\s*([0-9.]+)\s*MHz");
		private static readonly Regex ProtocolRe = new Regex(@"^Protocol detected:\s*(\S+)");
		private static readonly Regex EepromEraseRe = new Regex(@"^\s*eeprom_erase_enabled=(\S+)");
		private static readonly Regex ProgressRe = new Regex(@"Writing flash:\s*(\d+)%");
		private static readonly Regex ErrorRe = new Regex(@"^(Protocol error|Serial port error|I/O error|Unexpected error|Option error):\s*(.*)$");

		public event Action WaitingForPower;
		public event Action<int> Progress;

		// stream: 0 = stdout, 1 = stderr
		public void Feed(string text, int stream = 0)
		{
			_log.Append(text);
			var current = _lines[stream];
			foreach (var ch in text)
			{
				if (ch == '\n' || ch == '\r')
				{
					EndLine(current);
				}
				else
				{
					current.Append(ch);
					// the power prompt has no newline after it
					if (!_announcedPower && current.ToString().Contains("please cycle power"))
					{
						_announcedPower = true;
						WaitingForPower?.Invoke();
					}
				}
			}
		}

		private void EndLine(StringBuilder current)
		{
			var line = current.ToString();
			current.Clear();

			Match m;
			if ((m = NameRe.Match(line)).Success)
			{
				_mcuName = m.Groups[1].Value;
			}
			else if ((m = FrequencyRe.Match(line)).Success)
			{
				_frequency = double.Parse(m.Groups[1].Value, CultureInfo.InvariantCulture);
			}
			else if ((m = ProtocolRe.Match(line)).Success)
			{
				_protocol = m.Groups[1].Value;
			}
			else if ((m = EepromEraseRe.Match(line)).Success)
			{
				_eepromErase = m.Groups[1].Value.Equals("True", StringComparison.OrdinalIgnoreCase);
			}
			else if ((m = ErrorRe.Match(line)).Success)
			{
				_error = line.Trim();
			}

			if ((m = ProgressRe.Match(line)).Success)
			{
				int p = int.Parse(m.Groups[1].Value, CultureInfo.InvariantCulture);
				if (p != _lastProgress)
				{
					_lastProgress = p;
					Progress?.Invoke(p);
				}
			}
		}

		public StcgalResult Result()
		{
			foreach (var line in _lines)
			{
				if (line.Length > 0)
				{
					EndLine(line);
				}
			}
			return new StcgalResult
			{
				McuName = _mcuName,
				McuFrequencyMhz = _frequency,
				Protocol = _protocol,
				EepromEraseEnabled = _eepromErase,
				Error = _error,
				Log = _log.ToString(),
			};
		}
	}
}
