using BBSFW.Model;
using BBSFW.ViewModel.Base;
using System;
using System.IO;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Input;

namespace BBSFW.ViewModel
{
	// "Bench" tab: the tool stands in for the display while the bike is on a
	// stand. Shows what the display would, plus live data it can't, records it
	// to CSV, and checks the pedal sensor's steadiness for effort sensing.
	public class BenchViewModel : ObservableObject
	{
		private readonly ConnectionViewModel _connectionVm;
		private readonly IUserPrompts _prompts;
		private readonly Func<BbsfwConnection> _connection;
		private CancellationTokenSource _cts;
		private StreamWriter _recording;
		private float _wheelSizeInch;
		private Configuration.RangeFieldData _rangeField;

		public TimeSpan PollInterval { get; set; } = TimeSpan.FromMilliseconds(400);

		public BenchViewModel(ConnectionViewModel connectionVm, IUserPrompts prompts)
			: this(connectionVm, prompts, connectionVm.GetConnection)
		{ }

		// for tests: a connection without a ConnectionViewModel
		public BenchViewModel(ConnectionViewModel connectionVm, IUserPrompts prompts, Func<BbsfwConnection> connection)
		{
			_connectionVm = connectionVm;
			_prompts = prompts;
			_connection = connection;
		}

		// --- state ---

		private bool _isRunning;
		public bool IsRunning
		{
			get { return _isRunning; }
			private set { _isRunning = value; OnPropertyChanged(nameof(IsRunning)); OnPropertyChanged(nameof(IsStopped)); }
		}
		public bool IsStopped => !_isRunning;

		private string _message = "Connect on the Connection tab, put the bike on a stand with the rear wheel off the ground, then Start.";
		public string Message
		{
			get { return _message; }
			private set { _message = value; OnPropertyChanged(nameof(Message)); }
		}

		private ControllerStatus _status;
		public ControllerStatus Status
		{
			get { return _status; }
			private set
			{
				_status = value;
				OnPropertyChanged(nameof(Status));
				OnPropertyChanged(nameof(SpeedKph));
				OnPropertyChanged(nameof(LevelText));
				OnPropertyChanged(nameof(RangeText));
			}
		}

		public double SpeedKph => _status == null ? 0 : _status.SpeedKph(_wheelSizeInch);

		public string LevelText => _status == null ? "-" : _status.AssistLevel == 10 ? "Walk" : _status.AssistLevel.ToString();

		// what the display's Range field shows, and what the number means
		public string RangeText
		{
			get
			{
				if (_status == null)
				{
					return "-";
				}
				switch (_rangeField)
				{
					case Configuration.RangeFieldData.RiderPower:
						return _status.EffortLearnedPercent < 100
							? $"{_status.EffortLearnedPercent} (learning the pedal sensor, %)"
							: $"{_status.RangeField} (your power, W)";
					case Configuration.RangeFieldData.Temperature:
						return $"{_status.RangeField} (temperature)";
					case Configuration.RangeFieldData.Power:
						return $"{_status.RangeField / 10.0:0.0} (current, A)";
					default:
						return _status.RangeField.ToString();
				}
			}
		}

		private bool _isRecording;
		public bool IsRecording
		{
			get { return _isRecording; }
			private set { _isRecording = value; OnPropertyChanged(nameof(IsRecording)); OnPropertyChanged(nameof(RecordLabel)); }
		}
		public string RecordLabel => _isRecording ? "Stop recording" : "Record to CSV...";

		private string _sensorReport = "";
		public string SensorReport
		{
			get { return _sensorReport; }
			private set { _sensorReport = value; OnPropertyChanged(nameof(SensorReport)); }
		}

		private bool _isCapturing;
		public bool IsCapturing
		{
			get { return _isCapturing; }
			private set { _isCapturing = value; OnPropertyChanged(nameof(IsCapturing)); }
		}

		public System.Collections.Generic.List<ushort> LastCapture { get; private set; }
		public PasCaptureAnalysis LastAnalysis { get; private set; }

		// --- commands ---

		public ICommand StartCommand => new DelegateCommand(() => { var _ = Start(); });
		public ICommand StopCommand => new DelegateCommand(Stop);
		public ICommand SetLevelCommand => new DelegateCommand(p => SetLevel(Convert.ToInt32(p)));
		public ICommand ToggleLightsCommand => new DelegateCommand(() => SetLights(!(_status?.Lights ?? false)));
		public ICommand RecordCommand => new DelegateCommand(ToggleRecording);
		public ICommand CheckSensorCommand => new DelegateCommand(() => { var _ = CheckSensor(); });
		public ICommand SaveCaptureCommand => new DelegateCommand(SaveCapture);
		public ICommand ResetPatternCommand => new DelegateCommand(() => { var _ = ResetPattern(); });

		private BbsfwConnection Connection
		{
			get
			{
				var c = _connection();
				return c != null && c.IsConnected ? c : null;
			}
		}

		public async Task Start()
		{
			var conn = Connection;
			if (conn == null)
			{
				Message = "Not connected: connect on the Connection tab first.";
				return;
			}
			if (!conn.SupportsBench)
			{
				Message = $"The controller runs bbs-fw {conn.FirmwareVersion}; the Bench needs 2.2 or later. Update it on the Update Firmware tab.";
				return;
			}
			if (IsRunning)
			{
				return;
			}

			// the wheel size and Range field mode come from the controller; reading
			// the settings pauses it for about 2 s, fine while standing still
			var cfg = await conn.ReadConfiguration(TimeSpan.FromSeconds(5));
			if (cfg.Timeout || cfg.Result == null)
			{
				Message = "Couldn't read the controller's settings.";
				return;
			}
			_wheelSizeInch = cfg.Result.WheelSizeInch;
			_rangeField = cfg.Result.RangeFieldDisplay;

			IsRunning = true;
			Message = "Running. Effort learning is off until the controller is switched off, since the bike is on a stand.";
			_cts = new CancellationTokenSource();
			try
			{
				await Poll(_cts.Token);
			}
			catch (OperationCanceledException)
			{
			}
			finally
			{
				IsRunning = false;
				StopRecording();
			}
		}

		public void Stop()
		{
			_cts?.Cancel();
		}

		private async Task Poll(CancellationToken ct)
		{
			int misses = 0;
			while (!ct.IsCancellationRequested)
			{
				var conn = Connection;
				if (conn == null)
				{
					Message = "Disconnected.";
					return;
				}

				if (!IsCapturing)
				{
					var r = await conn.ReadStatus(TimeSpan.FromSeconds(1));
					if (r.Timeout || r.Result == null)
					{
						if (++misses >= 5)
						{
							Message = "The controller stopped answering.";
							return;
						}
					}
					else
					{
						misses = 0;
						Status = r.Result;
						_recording?.WriteLine(r.Result.ToCsv());
					}
				}

				await Task.Delay(PollInterval, ct);
			}
		}

		public void SetLevel(int level)
		{
			Connection?.SetAssistLevel(level);
		}

		public void SetLights(bool on)
		{
			Connection?.SetLights(on);
		}

		private void ToggleRecording()
		{
			if (IsRecording)
			{
				StopRecording();
				return;
			}
			var path = _prompts.AskSavePath("Record live data", "CSV file|*.csv", $"bench-{DateTime.Now:yyyyMMdd-HHmmss}.csv");
			if (path != null)
			{
				StartRecording(path);
			}
		}

		public void StartRecording(string path)
		{
			StopRecording();
			_recording = new StreamWriter(path) { AutoFlush = true };
			_recording.WriteLine(ControllerStatus.CsvHeader);
			IsRecording = true;
		}

		public void StopRecording()
		{
			_recording?.Dispose();
			_recording = null;
			IsRecording = false;
		}

		public async Task CheckSensor()
		{
			var conn = Connection;
			if (conn == null || !conn.SupportsBench || IsCapturing)
			{
				return;
			}

			// no motor while turning the cranks by hand
			conn.SetAssistLevel(0);
			IsCapturing = true;
			SensorReport = "Turn the cranks forwards steadily, by hand or pedalling gently, for about 10 seconds. Assist is set to 0.";
			try
			{
				var (ok, message, edges) = await PasCaptureSession.Run(conn, new Progress<string>(p => SensorReport = p), _cts?.Token ?? CancellationToken.None);
				if (!ok)
				{
					SensorReport = message;
					return;
				}
				LastCapture = edges;
				LastAnalysis = PasCaptureAnalysis.Analyse(edges);
				SensorReport = LastAnalysis.Summary();
			}
			catch (OperationCanceledException)
			{
				SensorReport = "Stopped.";
			}
			finally
			{
				IsCapturing = false;
			}
		}

		private void SaveCapture()
		{
			if (LastCapture == null)
			{
				return;
			}
			var path = _prompts.AskSavePath("Save pedal sensor capture", "CSV file|*.csv", $"pas-capture-{DateTime.Now:yyyyMMdd-HHmmss}.csv");
			if (path != null)
			{
				File.WriteAllText(path, PasCaptureAnalysis.ToCsv(LastCapture));
				File.WriteAllText(Path.ChangeExtension(path, ".txt"), LastAnalysis.Summary());
			}
		}

		public async Task<bool> ResetPattern()
		{
			var conn = Connection;
			if (conn == null || !conn.SupportsBench)
			{
				return false;
			}
			if (!_prompts.Confirm("Forget the pedal sensor pattern effort sensing has learned? It's learned again on your next fast ride.", "Reset learned pattern"))
			{
				return false;
			}
			var r = await conn.ResetEffortPattern(TimeSpan.FromSeconds(3));
			bool ok = !r.Timeout && r.Result;
			Message = ok ? "Learned pattern reset: it's learned again on the next fast ride (with the bike off the stand)." : "The reset didn't go through.";
			return ok;
		}
	}
}
