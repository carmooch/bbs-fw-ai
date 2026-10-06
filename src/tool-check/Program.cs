// Parses config bytes written by config_dump.c, checks the values arrived in
// the right fields, and writes them back: the result must be byte-identical.

using System;
using System.IO;
using System.Linq;
using BBSFW.Model;

var bytes = File.ReadAllBytes(args[0]);
var cfg = new Configuration(BbsfwConnection.Controller.BBSHD);
var failures = 0;

void Check(string what, object expected, object actual)
{
	if (!expected.Equals(actual))
	{
		Console.WriteLine($"FAIL {what}: expected {expected}, got {actual}");
		failures++;
	}
}

Check("config size", Configuration.ByteSizeV6, bytes.Length);
Check("parse", true, cfg.ParseFromBufferV6(bytes));

Check("max current", 33u, cfg.MaxCurrentAmps);
Check("current ramp", 12u, cfg.CurrentRampAmpsSecond);
Check("max battery", 58.8f, cfg.MaxBatteryVolts);
Check("low cutoff", 42u, cfg.LowCutoffVolts);
Check("wheel size", 27.5f, cfg.WheelSizeInch);

var l3 = cfg.StandardAssistLevels[3];
Check("level 3 type", Configuration.AssistFlagsType.Pas | Configuration.AssistFlagsType.PasPower, l3.Type);
Check("level 3 current", 60u, l3.MaxCurrentPercent);
Check("level 3 power start", 100u, l3.PowerStartWatts);
Check("level 3 power per rpm", 2.0f, l3.PowerWattsPerRpm);
Check("level 3 ramp", 5u, l3.CurrentRampAmpsSecond);
Check("sport level 9 speed", 77u, cfg.SportAssistLevels[9].MaxSpeedPercent);
Check("sport level 9 power per rpm", 9.9f, cfg.SportAssistLevels[9].PowerWattsPerRpm);

Check("predictive stop", true, cfg.PasStopPredictive);
Check("rolling start", 1u, cfg.PasStartDelayPulsesRolling);
Check("launch ramp", 30u, cfg.LaunchRampAmpsSecond);
Check("gear boost", 50u, cfg.GearBoostMaxPercent);
Check("gear ratio low", 1.2f, cfg.GearRatioLow);
Check("gear ratio high", 3.0f, cfg.GearRatioHigh);
Check("cadence lock", 15u, cfg.CadenceLockMarginRpm);

Check("written bytes identical", true, cfg.WriteToBuffer().SequenceEqual(bytes));

Console.WriteLine(failures == 0 ? "Config tool matches the firmware layout." : $"{failures} check(s) failed.");
return failures == 0 ? 0 : 1;
