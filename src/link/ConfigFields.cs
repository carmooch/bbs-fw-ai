// Config settings by name, for reading and changing them from the command
// line: the config tool's Configuration fields, with assist levels as
// StandardAssistLevels[3].MaxCurrentPercent and the like.

using System;
using System.Collections;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using System.Reflection;
using BBSFW.Model;

namespace BBSFW.Link
{
	public static class ConfigFields
	{
		// battery settings: changing them needs --battery (a wrong value can
		// let the pack run flat or never let the motor run)
		public static readonly string[] BatteryFields = { "MaxBatteryVolts", "LowCutoffVolts" };

		public class Field
		{
			public string Name;
			public object Owner;
			public FieldInfo Info;

			public object Get() => Info.GetValue(Owner);
			public string Text => Format(Get());
			public string Type => Info.FieldType.IsEnum
				? string.Join("|", Enum.GetNames(Info.FieldType))
				: Info.FieldType.Name.ToLowerInvariant();
		}

		public static List<Field> All(Configuration cfg)
		{
			var list = new List<Field>();
			foreach (var f in typeof(Configuration).GetFields(BindingFlags.Public | BindingFlags.Instance))
			{
				var value = f.GetValue(cfg);
				if (value is Array array)
				{
					for (int i = 0; i < array.Length; ++i)
					{
						var item = array.GetValue(i);
						foreach (var lf in item.GetType().GetFields(BindingFlags.Public | BindingFlags.Instance))
						{
							list.Add(new Field { Name = $"{f.Name}[{i}].{lf.Name}", Owner = item, Info = lf });
						}
					}
				}
				else
				{
					list.Add(new Field { Name = f.Name, Owner = cfg, Info = f });
				}
			}
			return list;
		}

		public static Field Find(Configuration cfg, string name)
		{
			var all = All(cfg);
			var f = all.FirstOrDefault(x => string.Equals(x.Name, name, StringComparison.OrdinalIgnoreCase));
			if (f == null)
			{
				var near = all.Where(x => x.Name.IndexOf(name, StringComparison.OrdinalIgnoreCase) >= 0).Take(8).Select(x => x.Name);
				throw new Exception($"No setting called {name}." + (near.Any() ? " Did you mean: " + string.Join(", ", near) : ""));
			}
			return f;
		}

		public static void Set(Field f, string text)
		{
			var t = f.Info.FieldType;
			object value;
			var c = CultureInfo.InvariantCulture;
			try
			{
				if (t.IsEnum)
				{
					value = Enum.Parse(t, text, true);
				}
				else if (t == typeof(bool))
				{
					value = text == "1" || text.Equals("true", StringComparison.OrdinalIgnoreCase) || text.Equals("on", StringComparison.OrdinalIgnoreCase)
						? true
						: (text == "0" || text.Equals("false", StringComparison.OrdinalIgnoreCase) || text.Equals("off", StringComparison.OrdinalIgnoreCase)
							? false : throw new FormatException());
				}
				else if (t == typeof(float))
				{
					value = float.Parse(text, NumberStyles.Float, c);
				}
				else if (t == typeof(double))
				{
					value = double.Parse(text, NumberStyles.Float, c);
				}
				else
				{
					value = Convert.ChangeType(text, t, c);
				}
			}
			catch (Exception)
			{
				throw new Exception($"{f.Name}: '{text}' isn't a valid {f.Type}.");
			}
			f.Info.SetValue(f.Owner, value);
		}

		public static string Format(object v)
		{
			switch (v)
			{
				case float x: return x.ToString("0.###", CultureInfo.InvariantCulture);
				case double x: return x.ToString("0.###", CultureInfo.InvariantCulture);
				case bool b: return b ? "true" : "false";
				case null: return "";
				default: return Convert.ToString(v, CultureInfo.InvariantCulture);
			}
		}

		public static Configuration Clone(Configuration cfg)
		{
			var copy = new Configuration(cfg.Target);
			copy.CopyFrom(cfg);
			return copy;
		}

		// settings whose values differ: name, old, new
		public static List<(string name, string from, string to)> Diff(Configuration a, Configuration b)
		{
			var fa = All(a);
			var fb = All(b).ToDictionary(x => x.Name);
			return fa.Where(x => x.Text != fb[x.Name].Text).Select(x => (x.Name, x.Text, fb[x.Name].Text)).ToList();
		}
	}
}
