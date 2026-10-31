using System;
using System.Collections.Generic;
using System.IO.Ports;
using System.Linq;
using System.Management;
using System.Threading;
using System.Threading.Tasks;

namespace BBSFW.Model
{

	public class ComPort
	{
		public string Name { get; private set; }

		public string Description { get; private set; }

		public ComPort(string name, string description)
		{
			Name = name;
			Description = description;
		}
	}

	public class BbsfwConnection
	{
		public enum Controller
		{
			Unknown = 0,
			BBSHD = 1,
			BBS02 = 2,
			TSDZ2 = 3
		}

		private const int REQUEST_TYPE_READ =			0x01;
		private const int REQUEST_TYPE_WRITE =			0x02;

		private const int RESPONSE_TYPE_READ = 			0x01;
		private const int RESPONSE_TYPE_WRITE = 		0x02;

		private const int EVENT_LOG_ENTRY =				0xee;
		private const int EVENT_LOG_DATA_ENTRY =		0xed;

		private const int OPCODE_READ_FW_VERSION =		0x01;
		private const int OPCODE_READ_EVTLOG_ENABLE =	0x02;
		private const int OPCODE_READ_CONFIG =			0x03;
		private const int OPCODE_READ_STATUS =			0x04;
		private const int OPCODE_READ_PAS_CAPTURE =		0x05;

		private const int OPCODE_WRITE_EVTLOG_ENABLE =	0xf0;
		private const int OPCODE_WRITE_CONFIG =			0xf1;
		private const int OPCODE_WRITE_RESET_CONFIG =	0xf2;
		private const int OPCODE_WRITE_ADC_VOLTAGE_CALIBRATION = 0xf3;
		private const int OPCODE_WRITE_RESET_EFFORT =	0xf4;
		private const int OPCODE_WRITE_START_PAS_CAPTURE = 0xf5;

		// Bafang display protocol, as the display sends it (no reply)
		private const int BAFANG_REQUEST_WRITE =		0x16;
		private const int BAFANG_OPCODE_WRITE_PAS =		0x0b;
		private const int BAFANG_OPCODE_WRITE_LIGHTS =	0x1a;

		// display codes for assist levels 0-9, then walk
		private static readonly byte[] BafangLevelCodes = { 0x00, 0x01, 0x0b, 0x0c, 0x0d, 0x02, 0x15, 0x16, 0x17, 0x03, 0x06 };

		public const int PasCaptureEdgesPerRead = 10;

		private const int Keep = 0;
		private const int Discard = -1;

		private IPortTransport _port = null;

		// How a port is opened: the serial port, or (in tests) a stand-in.
		public static Func<ComPort, IPortTransport> OpenTransport = p => new SerialPortTransport(p.Name);
		private volatile bool _isConnecting = false;
		private volatile bool _isConnected = false;
		private Controller _controllerType = Controller.Unknown;

		private DateTime _lastRecv = DateTime.Now;
		private List<byte> _rxBuffer = new List<byte>();


		private CompletionQueue<Configuration> _readConfigCq = new CompletionQueue<Configuration>();
		private CompletionQueue<bool> _writeConfigCq = new CompletionQueue<bool>();
		private CompletionQueue<bool> _writeResetConfigCq = new CompletionQueue<bool>();
		private CompletionQueue<bool> _writeVoltageCalibrationCq = new CompletionQueue<bool>();
		private CompletionQueue<ControllerStatus> _readStatusCq = new CompletionQueue<ControllerStatus>();
		private CompletionQueue<PasCaptureChunk> _readPasCaptureCq = new CompletionQueue<PasCaptureChunk>();
		private CompletionQueue<bool> _writeResetEffortCq = new CompletionQueue<bool>();
		private CompletionQueue<bool> _writeStartPasCaptureCq = new CompletionQueue<bool>();


		private int ConfigVersion = 0;



		public bool IsConnected
		{
			get
			{
				return _isConnected;
			}
		}

		public Controller ControllerType
		{
			get
			{
				return _controllerType;
			}
		}

		public event Action<Controller, string, int>	Connected;
		public event Action								Disconnected;


		public event Action<EventLogEntry>		EventLog;


		public static List<ComPort> GetComPorts()
		{
			var result = new List<ComPort>();

			using (var searcher = new ManagementObjectSearcher("SELECT * FROM Win32_PnPEntity WHERE Name LIKE '%COM%'"))
			{
				var portNames = SerialPort.GetPortNames();
				var ports = searcher.Get().Cast<ManagementBaseObject>().ToList();

				foreach (var name in portNames)
				{
					var port = ports.FirstOrDefault(p => p["Name"].ToString().ToUpper().Contains(name.ToUpper()));
					if (port != null)
					{
						result.Add(new ComPort(name, port["Caption"].ToString()));
					}
					else
					{
						result.Add(new ComPort(name, name));
					}
				}
			}

			return result;
		}



		public async Task<bool> Connect(ComPort port, TimeSpan timeout)
		{
			_controllerType = Controller.Unknown;
			_isConnected = false;
			_isConnecting = true;
			_port = OpenTransport(port);
			_port.Received += OnDataReceived;

			var connected = await Task.Run(() => SetupConnection(timeout));
			if (!connected)
			{
				Close();
			}

			return connected;
		}

		public void Close()
		{
			if (_port != null)
			{
				_isConnected = false;
				_isConnecting = false;

				_port.Received -= OnDataReceived;
				_port.Close();
				_port = null;

				lock (_rxBuffer)
				{
					_rxBuffer.Clear();
				}

				Disconnected?.Invoke();
			}
		}


		public async Task<RequestResult<Configuration>> ReadConfiguration(TimeSpan timeout)
		{
			var reply = _readConfigCq.Expect(timeout);
			SendReadRequest(OPCODE_READ_CONFIG);
			return await reply;
		}

		public async Task<RequestResult<bool>> WriteConfiguration(Configuration configuration, TimeSpan timeout)
		{
			var reply = _writeConfigCq.Expect(timeout);
			SendWriteConfigRequest(configuration);
			return await reply;
		}

		public async Task<RequestResult<bool>> ResetConfiguration(TimeSpan timeout)
		{
			var reply = _writeResetConfigCq.Expect(timeout);
			SendWriteResetConfigRequest();
			return await reply;
		}

		public async Task<RequestResult<bool>> CalibrateBatteryVoltage(float actualVolts, TimeSpan timeout)
		{
			var reply = _writeVoltageCalibrationCq.Expect(timeout);
			SendWriteVoltageCalibration(actualVolts);
			return await reply;
		}

		// --- bench (firmware 2.2 and later) ---

		public bool SupportsBench => FirmwareVersion != null && FirmwareVersion >= new Version(2, 2, 0);

		public Version FirmwareVersion { get; private set; }

		// Live data. Polling this also stops effort learning in the firmware,
		// since on a stand the cranks don't move like on the road.
		public async Task<RequestResult<ControllerStatus>> ReadStatus(TimeSpan timeout)
		{
			var reply = _readStatusCq.Expect(timeout);
			SendReadRequest(OPCODE_READ_STATUS);
			return await reply;
		}

		public async Task<RequestResult<bool>> ResetEffortPattern(TimeSpan timeout)
		{
			var reply = _writeResetEffortCq.Expect(timeout);
			SendSimpleWriteRequest(OPCODE_WRITE_RESET_EFFORT);
			return await reply;
		}

		public async Task<RequestResult<bool>> StartPasCapture(TimeSpan timeout)
		{
			var reply = _writeStartPasCaptureCq.Expect(timeout);
			SendSimpleWriteRequest(OPCODE_WRITE_START_PAS_CAPTURE);
			return await reply;
		}

		public async Task<RequestResult<PasCaptureChunk>> ReadPasCapture(int first, TimeSpan timeout)
		{
			var reply = _readPasCaptureCq.Expect(timeout);
			var buf = new List<byte> { REQUEST_TYPE_READ, OPCODE_READ_PAS_CAPTURE, (byte)(first >> 8), (byte)first };
			buf.Add(ComputeChecksum(buf, buf.Count));
			_port.Write(buf.ToArray(), 0, buf.Count);
			return await reply;
		}

		// As the display does it: level 0-9, or 10 for walk. No reply.
		public void SetAssistLevel(int level)
		{
			var buf = new List<byte> { BAFANG_REQUEST_WRITE, BAFANG_OPCODE_WRITE_PAS, BafangLevelCodes[level] };
			buf.Add(ComputeChecksum(buf, buf.Count));
			_port.Write(buf.ToArray(), 0, buf.Count);
		}

		public void SetLights(bool on)
		{
			var buf = new byte[] { BAFANG_REQUEST_WRITE, BAFANG_OPCODE_WRITE_LIGHTS, (byte)(on ? 0xf1 : 0xf0) };
			_port.Write(buf, 0, buf.Length);
		}


		// data == null: the port closed
		private void OnDataReceived(byte[] data)
		{
			// check for communication error and reset
			if (_rxBuffer.Any() && DateTime.Now - _lastRecv > TimeSpan.FromMilliseconds(1000))
			{
				_rxBuffer.Clear();
			}

			bool close = data == null;
			if (!close)
			{
				lock(_rxBuffer)
				{
					_lastRecv = DateTime.Now;
					_rxBuffer.AddRange(data);
				}
			}
			
			if (close)
			{
				Close();
			}
			else
			{
				ProcessInputBuffer();
			}
		}


		private void ProcessInputBuffer()
		{
			lock(_rxBuffer)
			{
				while(true)
				{
					var result = ProcessMessage();
					if (result == Discard)
					{
						System.Diagnostics.Debug.WriteLine("Discarding: " + BitConverter.ToString(_rxBuffer.ToArray()).Replace("-", " "));
						_rxBuffer.Clear();
					}
					else if (result > 0)
					{
						if (_rxBuffer.Count > result)
						{
							_rxBuffer.RemoveRange(0, result);
						}
						else
						{
							_rxBuffer.Clear();
						}
					}
					else
					{
						// no data, done
						break;
					}
				}
				
			}
		}


		private int ProcessMessage()
		{
			if (_rxBuffer.Count < 1)
			{
				return 0;
			}

			switch(_rxBuffer[0])
			{
				case RESPONSE_TYPE_READ:
					return ProcessReadResponse();
				case RESPONSE_TYPE_WRITE:
					return ProcessWriteResponse();
				case EVENT_LOG_ENTRY:
				case EVENT_LOG_DATA_ENTRY:
					return ProcessEventLogEntry();
			}

			return Discard;
		}


		private int ProcessReadResponse()
		{
			if (_rxBuffer.Count < 2)
			{
				return 0;
			}

			switch(_rxBuffer[1])
			{
			case OPCODE_READ_FW_VERSION:
				return ProcessReadResponseFwVersion();
			case OPCODE_READ_EVTLOG_ENABLE:
				return ProcessReadResponseEvtlogEnable();
			case OPCODE_READ_CONFIG:
				return ProcessReadResponseConfig();
			case OPCODE_READ_STATUS:
				return ProcessReadResponseStatus();
			case OPCODE_READ_PAS_CAPTURE:
				return ProcessReadResponsePasCapture();
			}

			return -1;
		}

		private int ProcessReadResponseFwVersion()
		{
			const int MessageSizeV1 = 7;
			const int MessageSizeV2 = 8;

			if (_rxBuffer.Count < MessageSizeV1)
			{
				return Keep;
			}

			int size = MessageSizeV1;

			int major = _rxBuffer[2];
			int minor = _rxBuffer[3];
			int patch = _rxBuffer[4];

			if (major > 1 || minor > 0)
			{
				// Controller model field added in firmware version 1.1
				// Keep backwards compatibility
				if (_rxBuffer.Count < MessageSizeV2)
				{
					return Keep;
				}

				size = MessageSizeV2;
			}

			if (ComputeChecksum(_rxBuffer, size - 1) == _rxBuffer[size - 1])
			{
				ConfigVersion = _rxBuffer[5];
				FirmwareVersion = new Version(major, minor, patch);

				if (_isConnecting)
				{
					_isConnecting = false;
					_isConnected = true;
					_controllerType = (size == MessageSizeV1 ? Controller.BBSHD : (Controller)_rxBuffer[6]);

					Connected?.Invoke(ControllerType, String.Format("{0}.{1}.{2}", major, minor, patch), ConfigVersion); ;

					SendEventLogEnableRequest(true);
				}
			}

			return size;
		}

		private int ProcessReadResponseEvtlogEnable()
		{
			const int MessageSize = 4;

			if (_rxBuffer.Count < MessageSize)
			{
				return Keep;
			}

			// not used

			return 4;
		}

		private int ProcessReadResponseConfig()
		{
			int version;

			if (_rxBuffer.Count > 3)
			{
				version = _rxBuffer[2];
				var size = _rxBuffer[3];

				if (version < Configuration.MinVersion || version > Configuration.MaxVersion || size != Configuration.GetByteSize(version))
				{
					System.Diagnostics.Debug.WriteLine("Config read from flash is of an unsupported version or is corrupt, discarding.");
					return Discard;
				}
			}
			else
			{
				return Keep;
			}

			int MessageSize = (4 + Configuration.GetByteSize(version) + 1);

			if (_rxBuffer.Count < MessageSize)
			{
				return Keep;
			}

			if (ComputeChecksum(_rxBuffer, MessageSize - 1) == _rxBuffer[MessageSize - 1])
			{
				var cfg = new Configuration(ControllerType);

				switch(version)
				{
					case 1:
						cfg.ParseFromBufferV1(_rxBuffer.Skip(4).Take(Configuration.GetByteSize(version)).ToArray());
						break;
					case 2:
						cfg.ParseFromBufferV2(_rxBuffer.Skip(4).Take(Configuration.GetByteSize(version)).ToArray());
						break;
					case 3:
						cfg.ParseFromBufferV3(_rxBuffer.Skip(4).Take(Configuration.GetByteSize(version)).ToArray());
						break;
					case 4:
						cfg.ParseFromBufferV4(_rxBuffer.Skip(4).Take(Configuration.GetByteSize(version)).ToArray());
						break;
					case 5:
						cfg.ParseFromBufferV5(_rxBuffer.Skip(4).Take(Configuration.GetByteSize(version)).ToArray());
						break;
					case 6:
						cfg.ParseFromBufferV6(_rxBuffer.Skip(4).Take(Configuration.GetByteSize(version)).ToArray());
						break;
					case 7:
						cfg.ParseFromBufferV7(_rxBuffer.Skip(4).Take(Configuration.GetByteSize(version)).ToArray());
						break;
				}

				_readConfigCq.Complete(cfg);
			}
			else
			{
				System.Diagnostics.Debug.WriteLine("Config read from flash has mismatching checksum, discarding.");
			}

			return MessageSize;
		}


		private int ProcessWriteResponse()
		{
			if (_rxBuffer.Count < 2)
			{
				return Keep;
			}

			switch (_rxBuffer[1])
			{
				case OPCODE_WRITE_EVTLOG_ENABLE:
					return ProcessWriteResponseEvtlogEnable();
				case OPCODE_WRITE_CONFIG:
					return ProcessWriteResponseConfig();
				case OPCODE_WRITE_RESET_CONFIG:
					return ProcessWriteResponseResetConfig();
				case OPCODE_WRITE_ADC_VOLTAGE_CALIBRATION:
					return ProcessWriteResponseVoltageCalibration();
				case OPCODE_WRITE_RESET_EFFORT:
					return ProcessWriteResponseBool(_writeResetEffortCq);
				case OPCODE_WRITE_START_PAS_CAPTURE:
					return ProcessWriteResponseBool(_writeStartPasCaptureCq);
			}

			return Discard;
		}

		private int ProcessReadResponseStatus()
		{
			// type, opcode, payload length, payload, checksum
			if (_rxBuffer.Count < 3)
			{
				return Keep;
			}

			int size = 3 + _rxBuffer[2] + 1;
			if (_rxBuffer.Count < size)
			{
				return Keep;
			}

			if (ComputeChecksum(_rxBuffer, size - 1) != _rxBuffer[size - 1])
			{
				return Discard;
			}

			var status = ControllerStatus.Parse(_rxBuffer.Skip(3).Take(_rxBuffer[2]).ToArray(), DateTime.Now);
			if (status != null)
			{
				_readStatusCq.Complete(status);
			}

			return size;
		}

		private int ProcessReadResponsePasCapture()
		{
			// type, opcode, state, count (2), first (2), edges (2 each), checksum
			const int MessageSize = 7 + 2 * PasCaptureEdgesPerRead + 1;

			if (_rxBuffer.Count < MessageSize)
			{
				return Keep;
			}

			if (ComputeChecksum(_rxBuffer, MessageSize - 1) != _rxBuffer[MessageSize - 1])
			{
				return Discard;
			}

			var edges = new ushort[PasCaptureEdgesPerRead];
			for (int i = 0; i < edges.Length; ++i)
			{
				edges[i] = (ushort)(_rxBuffer[7 + 2 * i] << 8 | _rxBuffer[8 + 2 * i]);
			}

			_readPasCaptureCq.Complete(new PasCaptureChunk
			{
				State = (PasCaptureState)_rxBuffer[2],
				Count = _rxBuffer[3] << 8 | _rxBuffer[4],
				First = _rxBuffer[5] << 8 | _rxBuffer[6],
				Edges = edges
			});

			return MessageSize;
		}

		private int ProcessWriteResponseBool(CompletionQueue<bool> cq)
		{
			const int MessageSize = 4;

			if (_rxBuffer.Count < MessageSize)
			{
				return Keep;
			}

			cq.Complete(_rxBuffer[2] != 0);

			return MessageSize;
		}

		private int ProcessWriteResponseEvtlogEnable()
		{
			const int MessageSize = 4;

			if (_rxBuffer.Count < MessageSize)
			{
				return Keep;
			}

			// don't care

			return MessageSize;
		}

		private int ProcessWriteResponseConfig()
		{
			const int MessageSize = 4;

			if (_rxBuffer.Count < MessageSize)
			{
				return Keep;
			}

			_writeConfigCq.Complete(_rxBuffer[2] != 0);

			return MessageSize;
		}

		private int ProcessWriteResponseResetConfig()
		{
			const int MessageSize = 4;

			if (_rxBuffer.Count < MessageSize)
			{
				return Keep;
			}

			_writeResetConfigCq.Complete(_rxBuffer[2] != 0);

			return MessageSize;
		}

		private int ProcessWriteResponseVoltageCalibration()
		{
			const int MessageSize = 5;

			if (_rxBuffer.Count < MessageSize)
			{
				return Keep;
			}

			_writeVoltageCalibrationCq.Complete(true);

			return MessageSize;
		}

		private int ProcessEventLogEntry()
		{
			if (_rxBuffer[0] == EVENT_LOG_ENTRY)
			{
				const int MessageSize = 3;

				if (_rxBuffer.Count < MessageSize)
				{
					return Keep;
				}

				if (ComputeChecksum(_rxBuffer, MessageSize - 1) == _rxBuffer[MessageSize - 1])
				{
					EventLog?.Invoke(new EventLogEntry(_rxBuffer[1], null));
					return MessageSize;
				}
				else
				{
					Console.WriteLine("Event log cheksum missmatch. Discarding.");
					return Discard;
				}			
			}
			else if (_rxBuffer[0] == EVENT_LOG_DATA_ENTRY)
			{
				const int MessageSize = 5;

				if (_rxBuffer.Count < MessageSize)
				{
					return Keep;
				}

				if (ComputeChecksum(_rxBuffer, MessageSize - 1) == _rxBuffer[MessageSize - 1])
				{
					int data = _rxBuffer[2] << 8 | _rxBuffer[3];
					EventLog?.Invoke(new EventLogEntry(_rxBuffer[1], data));

					return MessageSize;
				}
				else
				{
					Console.WriteLine("Event log cheksum missmatch. Discarding.");
					return Discard;
				}				
			}

			return Discard;
		}


		private void SendReadRequest(byte opcode)
		{
			var buf = new List<byte>();
			buf.Add(REQUEST_TYPE_READ);
			buf.Add(opcode);
			buf.Add(ComputeChecksum(buf, buf.Count));

			_port.Write(buf.ToArray(), 0, buf.Count);
		}

		private void SendEventLogEnableRequest(bool enable)
		{
			var buf = new List<byte>();
			buf.Add(REQUEST_TYPE_WRITE);
			buf.Add(OPCODE_WRITE_EVTLOG_ENABLE);
			buf.Add((byte)(enable ? 1 : 0));
			buf.Add(ComputeChecksum(buf, buf.Count));

			_port.Write(buf.ToArray(), 0, buf.Count);
		}

		private void SendWriteConfigRequest(Configuration config)
		{
			if (Configuration.CurrentVersion != ConfigVersion)
			{
				throw new InvalidOperationException("Unsupported config version.");
			}

			var cfgarr = config.WriteToBuffer();

			var buf = new List<byte>();
			buf.Add(REQUEST_TYPE_WRITE);
			buf.Add(OPCODE_WRITE_CONFIG);
			buf.Add((byte)Configuration.CurrentVersion);
			buf.Add((byte)cfgarr.Length);
			buf.AddRange(cfgarr);
			buf.Add(ComputeChecksum(buf, buf.Count));

			_port.Write(buf.ToArray(), 0, buf.Count);
		}

		private void SendSimpleWriteRequest(byte opcode)
		{
			var buf = new List<byte> { REQUEST_TYPE_WRITE, opcode };
			buf.Add(ComputeChecksum(buf, buf.Count));
			_port.Write(buf.ToArray(), 0, buf.Count);
		}

		private void SendWriteResetConfigRequest()
		{
			var buf = new List<byte>();
			buf.Add(REQUEST_TYPE_WRITE);
			buf.Add(OPCODE_WRITE_RESET_CONFIG);
			buf.Add(ComputeChecksum(buf, buf.Count));

			_port.Write(buf.ToArray(), 0, buf.Count);
		}

		private void SendWriteVoltageCalibration(float volts)
		{
			uint volts_x100 = (uint)(volts * 100);

			var buf = new List<byte>();
			buf.Add(REQUEST_TYPE_WRITE);
			buf.Add(OPCODE_WRITE_ADC_VOLTAGE_CALIBRATION);
			buf.Add((byte)(volts_x100 >> 8));
			buf.Add((byte)volts_x100);
			buf.Add(ComputeChecksum(buf, buf.Count));

			_port.Write(buf.ToArray(), 0, buf.Count);
		}

		private bool SetupConnection(TimeSpan timeout)
		{
			var start = DateTime.Now;
			while (_isConnecting && !_isConnected)
			{
				if (DateTime.Now - start > timeout)
				{
					return false;
				}

				SendReadRequest(OPCODE_READ_FW_VERSION);
				Thread.Sleep(200);
			}

			return true;
		}


		private static byte ComputeChecksum(List<byte> buffer, int length)
		{
			unchecked
			{
				byte result = 0;
				for (int i = 0; i < length; i++)
				{
					result += buffer[i];
				}

				return result;
			}
		}

	}

	public interface IPortTransport
	{
		void Write(byte[] buffer, int offset, int count);
		void Close();

		// received bytes; null when the port closed
		event Action<byte[]> Received;
	}

	public class SerialPortTransport : IPortTransport
	{
		private readonly SerialPort _port;

		public event Action<byte[]> Received;

		public SerialPortTransport(string name)
		{
			_port = new SerialPort(name, 1200);
			_port.DataReceived += OnDataReceived;
			_port.Open();
		}

		public void Write(byte[] buffer, int offset, int count)
		{
			_port.Write(buffer, offset, count);
		}

		public void Close()
		{
			_port.DataReceived -= OnDataReceived;
			_port.Close();
		}

		private void OnDataReceived(object sender, SerialDataReceivedEventArgs e)
		{
			var data = new List<byte>();
			while (_port.IsOpen && _port.BytesToRead > 0)
			{
				var b = _port.ReadByte();
				if (b == -1)
				{
					Received?.Invoke(null);
					return;
				}
				data.Add((byte)b);
			}
			if (data.Count > 0)
			{
				Received?.Invoke(data.ToArray());
			}
		}
	}
}
