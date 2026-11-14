// What differs between two configurations, setting by setting: the top-level
// fields of Configuration plus every assist level's fields, by name.

using System;
using System.Collections.Generic;
using System.Globalization;
using System.Reflection;
using System.Xml.Serialization;
using BBSFW.Model;

namespace BBSFW.App
{
	public class SettingChange
	{
		public string Name { get; set; }
		public string Before { get; set; }
		public string After { get; set; }
		public bool IsBattery { get; set; }
	}

	public static class TuneDiff
	{
		public static readonly HashSet<string> BatteryFields = new HashSet<string> { "MaxBatteryVolts", "LowCutoffVolts" };

		public static List<SettingChange> Compare(Configuration before, Configuration after)
		{
			var list = new List<SettingChange>();
			if (before == null || after == null)
			{
				return list;
			}

			foreach (var f in typeof(Configuration).GetFields(BindingFlags.Public | BindingFlags.Instance))
			{
				if (f.GetCustomAttribute<XmlIgnoreAttribute>() != null || f.FieldType.IsArray)
				{
					continue;
				}
				var a = Show(f.GetValue(before));
				var b = Show(f.GetValue(after));
				if (a != b)
				{
					list.Add(new SettingChange { Name = f.Name, Before = a, After = b, IsBattery = BatteryFields.Contains(f.Name) });
				}
			}

			foreach (var (name, la, lb) in new[] {
				("Standard", before.StandardAssistLevels, after.StandardAssistLevels),
				("Sport", before.SportAssistLevels, after.SportAssistLevels) })
			{
				for (int i = 0; i < la.Length; ++i)
				{
					foreach (var f in typeof(Configuration.AssistLevel).GetFields(BindingFlags.Public | BindingFlags.Instance))
					{
						var a = Show(f.GetValue(la[i]));
						var b = Show(f.GetValue(lb[i]));
						if (a != b)
						{
							list.Add(new SettingChange { Name = $"{name} L{i} {Label(f.Name)}", Before = a, After = b });
						}
					}
				}
			}
			return list;
		}

		public static string Label(string field)
		{
			switch (field)
			{
				case "MaxCurrentPercent": return "current %";
				case "MaxSpeedPercent": return "top speed %";
				case "MaxCadencePercent": return "cadence limit %";
				case "MaxThrottlePercent": return "throttle %";
				case "CurrentRampAmpsSecond": return "ramp A/s";
				case "EffortAssistFactor": return "effort assist ×";
				case "Type": return "assist type";
				default: return field;
			}
		}

		private static string Show(object v)
		{
			switch (v)
			{
				case null: return "";
				case float f: return f.ToString("0.##", CultureInfo.InvariantCulture);
				case double d: return d.ToString("0.##", CultureInfo.InvariantCulture);
				default: return Convert.ToString(v, CultureInfo.InvariantCulture);
			}
		}

		public static Configuration Clone(Configuration c)
		{
			var copy = new Configuration();
			copy.CopyFrom(c);
			return copy;
		}
	}
}
