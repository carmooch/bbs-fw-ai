using System;
using System.Globalization;

namespace BBSFW.Model
{
	// Live data from the controller (firmware READ_STATUS, 2.2 and later):
	// what the display shows, plus what it doesn't.
	public class ControllerStatus
	{
		public const int PayloadSize = 21;

		public DateTime Time { get; private set; }

		public int AssistLevel { get; private set; }	// 0-9, 10 = walk
		public bool PedalingForwards { get; private set; }
		public bool PedalingBackwards { get; private set; }
		public bool Brake { get; private set; }
		public bool Lights { get; private set; }
		public bool EffortValid { get; private set; }
		public bool WheelMoving { get; private set; }
		public bool ThrottleOk { get; private set; }

		public float CadenceRpm { get; private set; }
		public float WheelRpm { get; private set; }
		public int TargetCurrentPercent { get; private set; }
		public int TargetSpeedPercent { get; private set; }
		public float BatteryVolts { get; private set; }
		public float BatteryAmps { get; private set; }
		public int BatteryPercent { get; private set; }
		public int ThrottlePercent { get; private set; }
		public int TemperatureC { get; private set; }
		public int StatusCode { get; private set; }
		public int RangeField { get; private set; }		// as the display gets it
		public int RiderWatts { get; private set; }
		public int EffortLearnedPercent { get; private set; }

		public static ControllerStatus Parse(byte[] p, DateTime time)
		{
			if (p.Length < PayloadSize)
			{
				return null;
			}

			return new ControllerStatus
			{
				Time = time,
				AssistLevel = p[0],
				PedalingForwards = (p[1] & 0x01) != 0,
				PedalingBackwards = (p[1] & 0x02) != 0,
				Brake = (p[1] & 0x04) != 0,
				Lights = (p[1] & 0x08) != 0,
				EffortValid = (p[1] & 0x10) != 0,
				WheelMoving = (p[1] & 0x20) != 0,
				ThrottleOk = (p[1] & 0x40) != 0,
				CadenceRpm = U16(p, 2) / 10f,
				WheelRpm = U16(p, 4) / 10f,
				TargetCurrentPercent = p[6],
				TargetSpeedPercent = p[7],
				BatteryVolts = U16(p, 8) / 10f,
				BatteryAmps = U16(p, 10) / 10f,
				BatteryPercent = p[12],
				ThrottlePercent = p[13],
				TemperatureC = p[14],
				StatusCode = p[15],
				RangeField = U16(p, 16),
				RiderWatts = U16(p, 18),
				EffortLearnedPercent = p[20],
			};
		}

		public float SpeedKph(float wheelSizeInch)
		{
			return WheelRpm * wheelSizeInch * 0.0254f * (float)Math.PI * 60f / 1000f;
		}

		public const string CsvHeader =
			"time,level,pedaling,backwards,brake,lights,cadence_rpm,wheel_rpm,target_current_pct,target_speed_pct," +
			"battery_v,battery_a,battery_pct,throttle_pct,temperature_c,status_code,range_field,rider_w,effort_learned_pct,effort_valid";

		public string ToCsv()
		{
			var c = CultureInfo.InvariantCulture;
			return string.Join(",",
				Time.ToString("HH:mm:ss.fff", c), AssistLevel, B(PedalingForwards), B(PedalingBackwards), B(Brake), B(Lights),
				CadenceRpm.ToString("0.0", c), WheelRpm.ToString("0.0", c), TargetCurrentPercent, TargetSpeedPercent,
				BatteryVolts.ToString("0.0", c), BatteryAmps.ToString("0.0", c), BatteryPercent, ThrottlePercent,
				TemperatureC, StatusCode, RangeField, RiderWatts, EffortLearnedPercent, B(EffortValid));
		}

		private static int U16(byte[] p, int i) => p[i] << 8 | p[i + 1];
		private static int B(bool b) => b ? 1 : 0;
	}
}
