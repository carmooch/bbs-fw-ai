using BBSFW.Model;
using BBSFW.ViewModel.Base;
using System;
using System.Collections.Generic;
using System.IO;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Input;

namespace BBSFW.ViewModel
{
	// "Update Firmware" tab: walks through backup, chip check, flash and
	// verify. The rider's part is switching the controller off and on when
	// asked; everything else happens here.
	public class FlashWizardViewModel : ObservableObject
	{
		private readonly ConnectionViewModel _connectionVm;
		private readonly IUserPrompts _prompts;
		private readonly Func<FirmwarePackage> _packageSource;
		private CancellationTokenSource _cts;
		private TaskCompletionSource<bool> _next;

		public string BackupDirectory { get; set; } =
			Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "BBSFWTool", "Backups");

		// how long to wait for bbs-fw to answer, before and after flashing
		public TimeSpan ConnectTimeout { get; set; } = TimeSpan.FromSeconds(8);

		public FlashWizardViewModel(ConnectionViewModel connectionVm, IUserPrompts prompts, Func<FirmwarePackage> packageSource = null)
		{
			_connectionVm = connectionVm;
			_prompts = prompts;
			_packageSource = packageSource ?? FirmwarePackage.Bundled;
			Reset();
		}

		// --- what the page shows ---

		private List<ComPort> _comPorts = new List<ComPort>();
		public List<ComPort> ComPorts
		{
			get { return _comPorts; }
			set { _comPorts = value; OnPropertyChanged(nameof(ComPorts)); }
		}

		private ComPort _selectedComPort;
		public ComPort SelectedComPort
		{
			get { return _selectedComPort; }
			set { _selectedComPort = value; OnPropertyChanged(nameof(SelectedComPort)); }
		}

		private string _title;
		public string Title
		{
			get { return _title; }
			private set { _title = value; OnPropertyChanged(nameof(Title)); }
		}

		private string _instruction;
		public string Instruction
		{
			get { return _instruction; }
			private set { _instruction = value; OnPropertyChanged(nameof(Instruction)); }
		}

		private string _nextLabel;
		public string NextLabel
		{
			get { return _nextLabel; }
			private set { _nextLabel = value; OnPropertyChanged(nameof(NextLabel)); OnPropertyChanged(nameof(CanNext)); }
		}

		public bool CanNext => !string.IsNullOrEmpty(_nextLabel);

		private bool _isRunning;
		public bool IsRunning
		{
			get { return _isRunning; }
			private set { _isRunning = value; OnPropertyChanged(nameof(IsRunning)); OnPropertyChanged(nameof(IsIdle)); }
		}

		public bool IsIdle => !_isRunning;

		private bool _isWaitingForPower;
		public bool IsWaitingForPower
		{
			get { return _isWaitingForPower; }
			private set { _isWaitingForPower = value; OnPropertyChanged(nameof(IsWaitingForPower)); }
		}

		private int _progress;
		public int Progress
		{
			get { return _progress; }
			private set { _progress = value; OnPropertyChanged(nameof(Progress)); }
		}

		private readonly StringBuilder _log = new StringBuilder();
		public string Log => _log.ToString();

		public string PackageInfo { get; private set; }

		public bool Succeeded { get; private set; }

		// --- commands ---

		public ICommand StartCommand => new DelegateCommand(() => { var _ = Run(); });
		public ICommand NextCommand => new DelegateCommand(() => _next?.TrySetResult(true));
		public ICommand CancelCommand => new DelegateCommand(Cancel);
		public ICommand RefreshCommand => new DelegateCommand(RefreshPorts);

		public void RefreshPorts()
		{
			try
			{
				ComPorts = BbsfwConnection.GetComPorts();
			}
			catch (Exception)
			{
				ComPorts = new List<ComPort>();
			}
			SelectedComPort = _connectionVm?.SelectedComPort ?? (ComPorts.Count > 0 ? ComPorts[0] : null);
		}

		public void Cancel()
		{
			_cts?.Cancel();
			_next?.TrySetCanceled();
		}

		private void Reset()
		{
			var pkg = _packageSource();
			PackageInfo = pkg.IsComplete
				? $"Firmware in this tool: bbs-fw {pkg.Version} for BBSHD"
				: "This copy of the tool has no firmware bundled (use the BBSFWTool download from CI).";
			OnPropertyChanged(nameof(PackageInfo));
			Title = "Update the controller's firmware";
			Instruction =
				"1. Connect the programming cable to the controller and this computer.\n" +
				"2. Don't charge the battery while flashing.\n" +
				"3. Choose the cable's port, then Start. The wizard says when to switch the controller off and on.";
			NextLabel = null;
		}

		private void Append(string text)
		{
			_log.Append(text);
			OnPropertyChanged(nameof(Log));
		}

		private void Say(string title, string instruction, string next = null)
		{
			Title = title;
			Instruction = instruction;
			NextLabel = next;
			Append($"\n== {title}\n");
		}

		private async Task WaitForNext(string title, string instruction, string next)
		{
			Say(title, instruction, next);
			_next = new TaskCompletionSource<bool>();
			await _next.Task;
			NextLabel = null;
		}

		public async Task Run()
		{
			if (IsRunning)
			{
				return;
			}
			var pkg = _packageSource();
			if (!pkg.IsComplete)
			{
				_prompts.Inform(PackageInfo, "Update Firmware");
				return;
			}
			if (SelectedComPort == null)
			{
				_prompts.Inform("Choose the programming cable's port first.", "Update Firmware");
				return;
			}

			IsRunning = true;
			Succeeded = false;
			_log.Clear();
			OnPropertyChanged(nameof(Log));
			_cts = new CancellationTokenSource();

			// the port must be free for the wizard
			if (_connectionVm != null && _connectionVm.IsConnected)
			{
				_connectionVm.DisconnectCommand.Execute(null);
			}

			var update = new FirmwareUpdate(pkg, SelectedComPort);
			try
			{
				await Backup(update);
				if (!await CheckChip(update))
				{
					return;
				}
				while (!await Flash(update))
				{
					// Next = try again; Cancel ends the wizard
				}
				await VerifyAndRestore(update);
			}
			catch (OperationCanceledException)
			{
				Say("Cancelled", "Nothing more happens. If flashing had started, run the wizard again: the controller keeps its loader and can always be flashed again.");
			}
			finally
			{
				IsRunning = false;
				IsWaitingForPower = false;
				_next = null;
			}
		}

		private async Task Backup(FirmwareUpdate update)
		{
			await WaitForNext("Step 1 of 4: back up your settings",
				"Switch the controller on (battery on, cable connected), then click Next.", "Next");
			Say("Step 1 of 4: back up your settings", "Reading your settings...");

			var r = await update.BackupSettings(BackupDirectory, ConnectTimeout);
			Append(r.Message + "\n");
			if (!r.Saved)
			{
				await WaitForNext("Step 1 of 4: no backup",
					r.Message + "\n\nWithout a backup, the controller starts with default settings if flashing erases them. " +
					"Check the controller is on and click Next to continue anyway, or Cancel.", "Continue without backup");
			}
		}

		private async Task<T> RunStcgal<T>(FirmwareUpdate update, string title, Func<Stcgal, Task<T>> run)
		{
			var stcgal = update.Package.CreateStcgal();
			stcgal.Output += Append;
			stcgal.WaitingForPower += () =>
			{
				IsWaitingForPower = true;
				Say(title, "Switch the controller ON now.");
			};
			stcgal.Progress += p =>
			{
				IsWaitingForPower = false;
				Progress = p;
				Instruction = $"Writing the firmware: {p}%. Leave everything connected and switched on.";
			};
			Progress = 0;
			try
			{
				return await run(stcgal);
			}
			finally
			{
				IsWaitingForPower = false;
			}
		}

		private async Task<bool> CheckChip(FirmwareUpdate update)
		{
			const string title = "Step 2 of 4: check the controller";
			await WaitForNext(title, "Switch the controller OFF, then click Next.", "Next");
			Say(title, "Starting the flasher...");

			while (true)
			{
				var check = await RunStcgal(update, title, s => update.IdentifyChip(s, _cts.Token));
				_cts.Token.ThrowIfCancellationRequested();
				Append("\n" + check.Message + "\n");

				if (check.Supported)
				{
					Say(title, check.Message);
					return true;
				}
				if (update.Chip?.McuName != null)
				{
					Say("Stopped: unsupported controller", check.Message);
					return false;
				}

				await WaitForNext(title, check.Message + "\n\nSwitch the controller OFF, check the cable, and click Retry.", "Retry");
			}
		}

		private async Task<bool> Flash(FirmwareUpdate update)
		{
			const string title = "Step 3 of 4: flash the firmware";
			await WaitForNext(title, $"Ready to write bbs-fw {update.Package.Version}.\n\nSwitch the controller OFF, then click Flash.", "Flash");
			Say(title, "Starting the flasher...");

			var r = await RunStcgal(update, title, s => update.Flash(s, _cts.Token));
			_cts.Token.ThrowIfCancellationRequested();
			if (r.Success)
			{
				Append("\nFirmware written.\n");
				return true;
			}

			Append("\n" + r.Error + "\n");
			await WaitForNext(title, $"Flashing didn't finish: {r.Error}\n\nThis is safe to retry (the chip's loader can't be overwritten). " +
				"Switch the controller OFF and click Retry.", "Retry");
			return false;
		}

		private async Task VerifyAndRestore(FirmwareUpdate update)
		{
			const string title = "Step 4 of 4: check it worked";
			Say(title, "Waiting for the new firmware to answer...");

			var v = await update.Verify(ConnectTimeout);
			while (v.Version == null)
			{
				await WaitForNext(title, "The controller didn't answer yet. Switch it OFF and ON again, then click Next.", "Next");
				Say(title, "Waiting for the new firmware to answer...");
				v = await update.Verify(ConnectTimeout);
			}
			Append(v.Message + "\n");

			if (!v.FirmwareOk)
			{
				Say("Flashing didn't take", v.Message + "\n\nRun the wizard again.");
				return;
			}

			if (!v.SettingsKept && update.Backup != null)
			{
				await WaitForNext(title, v.Message, "Restore my settings");
				Say(title, "Writing your settings back...");
				bool ok = await update.RestoreBackup(ConnectTimeout);
				v = await update.Verify(ConnectTimeout);
				Append((ok ? "Settings restored. " : "Restoring failed. ") + v.Message + "\n");
				if (!v.SettingsKept)
				{
					Say("Settings not restored", $"bbs-fw {v.Version} is running, but your settings couldn't be written back. " +
						$"Open the backup ({update.BackupPath}) with File > Open and write it from the Connection tab.");
					return;
				}
			}

			Succeeded = true;
			Say("Done", $"bbs-fw {v.Version} is running" + (update.Backup != null ? " with your settings." : ".") +
				"\n\nNext: the Bench tab, to try it with the bike on a stand, or unplug the cable, plug the display back in and ride." +
				(update.BackupPath != null ? $"\n\nYour settings backup: {update.BackupPath}" : ""));
		}
	}
}
