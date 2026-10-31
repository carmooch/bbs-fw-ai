using System;
using System.IO;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;

namespace BBSFW.Model
{
	// What the config tool ships with for flashing: stcgal.exe and the
	// firmware built from the same commit (see the CI workflow).
	public class FirmwarePackage
	{
		public string StcgalExe { get; private set; }
		public string[] StcgalPrefixArgs { get; private set; } = new string[0];
		public string HexPath { get; private set; }
		public Version Version { get; private set; }

		public bool IsComplete => File.Exists(HexPath) && (StcgalPrefixArgs.Length > 0 || File.Exists(StcgalExe)) && Version != null;

		// <app>/flash/: stcgal.exe, bbs-fw-BBSHD.hex, version.txt
		public static FirmwarePackage FromDirectory(string dir)
		{
			var pkg = new FirmwarePackage
			{
				StcgalExe = Path.Combine(dir, "stcgal.exe"),
				HexPath = Path.Combine(dir, "bbs-fw-BBSHD.hex"),
			};
			var versionFile = Path.Combine(dir, "version.txt");
			if (File.Exists(versionFile) && Version.TryParse(File.ReadAllText(versionFile).Trim(), out var v))
			{
				pkg.Version = v;
			}
			return pkg;
		}

		public static FirmwarePackage Bundled()
		{
			return FromDirectory(Path.Combine(AppContext.BaseDirectory, "flash"));
		}

		// for tests: run stcgal through an interpreter
		public static FirmwarePackage Custom(string stcgalExe, string[] prefixArgs, string hexPath, Version version)
		{
			return new FirmwarePackage { StcgalExe = stcgalExe, StcgalPrefixArgs = prefixArgs, HexPath = hexPath, Version = version };
		}

		public Stcgal CreateStcgal()
		{
			return new Stcgal(StcgalExe, StcgalPrefixArgs);
		}
	}

	// The steps of the flash wizard, without any UI: back up the settings,
	// check the chip, flash, then check the firmware and settings came through.
	// Switching the controller on at the right moment is the rider's part; the
	// UI prompts for it on Stcgal.WaitingForPower and on the timeouts below.
	public class FirmwareUpdate
	{
		// BBSHD controllers bbs-fw supports (upstream README): V1.4 and V1.5
		public static readonly string[] SupportedMcus = { "STC15W4K56S4", "IAP15W4K61S4" };

		public FirmwarePackage Package { get; private set; }
		public ComPort Port { get; private set; }

		public Configuration Backup { get; private set; }
		public string BackupPath { get; private set; }
		public Version PreviousFirmware { get; private set; }
		public StcgalResult Chip { get; private set; }

		public FirmwareUpdate(FirmwarePackage package, ComPort port)
		{
			Package = package;
			Port = port;
		}

		public class BackupOutcome
		{
			public bool Connected;
			public bool Saved;
			public string Message;
		}

		// Reads the current settings from bbs-fw (any version) and saves them.
		// Not connecting isn't fatal: the controller may run Bafang's firmware.
		public async Task<BackupOutcome> BackupSettings(string backupDir, TimeSpan connectTimeout)
		{
			var conn = new BbsfwConnection();
			try
			{
				if (!await conn.Connect(Port, connectTimeout))
				{
					return new BackupOutcome { Message = "No bbs-fw answered, so there are no settings to back up (the controller may be off, or run Bafang's own firmware)." };
				}
				PreviousFirmware = conn.FirmwareVersion;

				var cfg = await conn.ReadConfiguration(TimeSpan.FromSeconds(10));
				if (cfg.Timeout || cfg.Result == null)
				{
					return new BackupOutcome { Connected = true, Message = $"bbs-fw {PreviousFirmware} answered but its settings couldn't be read." };
				}

				Backup = cfg.Result;
				Directory.CreateDirectory(backupDir);
				BackupPath = Path.Combine(backupDir, $"settings-before-flash-{DateTime.Now:yyyyMMdd-HHmmss}.xml");
				Backup.WriteToFile(BackupPath);

				return new BackupOutcome { Connected = true, Saved = true, Message = $"Settings from bbs-fw {PreviousFirmware} saved to {BackupPath}." };
			}
			finally
			{
				conn.Close();
			}
		}

		public class ChipOutcome
		{
			public bool Supported;
			public string Message;
			public StcgalResult Result;
		}

		public async Task<ChipOutcome> IdentifyChip(Stcgal stcgal, CancellationToken ct)
		{
			var r = await stcgal.Identify(Port.Name, ct);
			Chip = r;
			if (!r.Success && r.McuName == null)
			{
				return new ChipOutcome { Result = r, Message = r.Error ?? "The chip didn't answer." };
			}
			if (!SupportedMcus.Contains(r.McuName))
			{
				return new ChipOutcome
				{
					Result = r,
					Message = $"This controller's chip is {r.McuName ?? "unknown"}. bbs-fw only supports BBSHD controllers with " +
						string.Join(" or ", SupportedMcus) + ", so flashing stops here."
				};
			}
			return new ChipOutcome
			{
				Supported = true,
				Result = r,
				Message = $"{r.McuName}: a supported BBSHD controller." +
					(r.EepromEraseEnabled == true ? " This chip is set to erase its stored data when flashed, so your settings will be restored from the backup afterwards." : "")
			};
		}

		public async Task<StcgalResult> Flash(Stcgal stcgal, CancellationToken ct)
		{
			return await stcgal.Flash(Port.Name, Package.HexPath, ct);
		}

		public class VerifyOutcome
		{
			public bool FirmwareOk;
			public Version Version;
			public bool SettingsKept;
			public string Message;
		}

		public async Task<VerifyOutcome> Verify(TimeSpan connectTimeout)
		{
			var conn = new BbsfwConnection();
			try
			{
				if (!await conn.Connect(Port, connectTimeout))
				{
					return new VerifyOutcome { Message = "The controller didn't answer after flashing. Switch it off and on again, then retry." };
				}

				var outcome = new VerifyOutcome { Version = conn.FirmwareVersion };
				outcome.FirmwareOk = Package.Version == null || conn.FirmwareVersion == Package.Version;
				if (!outcome.FirmwareOk)
				{
					outcome.Message = $"The controller runs bbs-fw {conn.FirmwareVersion}, not {Package.Version}: flashing didn't take.";
					return outcome;
				}

				var cfg = await conn.ReadConfiguration(TimeSpan.FromSeconds(10));
				if (cfg.Timeout || cfg.Result == null)
				{
					outcome.Message = $"bbs-fw {conn.FirmwareVersion} is running, but its settings couldn't be read.";
					return outcome;
				}

				outcome.SettingsKept = Backup == null || SameCoreSettings(Backup, cfg.Result);
				outcome.Message = $"bbs-fw {conn.FirmwareVersion} is running. " + (Backup == null
					? "There was no backup to compare the settings with."
					: outcome.SettingsKept ? "Your settings came through." : "The settings differ from your backup (the flash erased them): restore the backup.");
				return outcome;
			}
			finally
			{
				conn.Close();
			}
		}

		public async Task<bool> RestoreBackup(TimeSpan connectTimeout)
		{
			if (Backup == null)
			{
				return false;
			}

			var conn = new BbsfwConnection();
			try
			{
				if (!await conn.Connect(Port, connectTimeout))
				{
					return false;
				}
				var cfg = new Configuration(conn.ControllerType);
				cfg.CopyFrom(Backup);
				var res = await conn.WriteConfiguration(cfg, TimeSpan.FromSeconds(10));
				return !res.Timeout && res.Result;
			}
			finally
			{
				conn.Close();
			}
		}

		// The settings a rider sets once and would notice losing.
		public static bool SameCoreSettings(Configuration a, Configuration b)
		{
			if (a.MaxCurrentAmps != b.MaxCurrentAmps || a.LowCutoffVolts != b.LowCutoffVolts ||
				Math.Abs(a.MaxBatteryVolts - b.MaxBatteryVolts) > 0.05f || Math.Abs(a.WheelSizeInch - b.WheelSizeInch) > 0.05f ||
				a.MaxSpeedKph != b.MaxSpeedKph || a.UseSpeedSensor != b.UseSpeedSensor)
			{
				return false;
			}
			for (int i = 0; i < a.StandardAssistLevels.Length; ++i)
			{
				if (a.StandardAssistLevels[i].Type != b.StandardAssistLevels[i].Type ||
					a.StandardAssistLevels[i].MaxCurrentPercent != b.StandardAssistLevels[i].MaxCurrentPercent)
				{
					return false;
				}
			}
			return true;
		}
	}
}
