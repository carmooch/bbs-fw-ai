using BBSFW.Model;
using BBSFW.ViewModel.Base;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Windows;
using System.Windows.Media;

namespace BBSFW.ViewModel
{
	public class AssistLevelViewModel : ObservableObject
	{
		private ConfigurationViewModel _configVm;
		private Configuration.AssistLevel _level;


		public enum AssistBaseType
		{
			Disabled,
			Pas,
			Throttle,
			Cruise
		}

		public enum AssistPasVariant
		{
			Cadence,
			Torque,
			Variable,
			Power
		}


		public List<ValueItemViewModel<AssistBaseType>> AssistBaseTypeOptions { get; } =
			new List<ValueItemViewModel<AssistBaseType>>()
			{
				new ValueItemViewModel<AssistBaseType>(AssistBaseType.Disabled, "Motor Disabled"),
				new ValueItemViewModel<AssistBaseType>(AssistBaseType.Pas, "PAS"),
				new ValueItemViewModel<AssistBaseType>(AssistBaseType.Throttle, "Throttle"),
				new ValueItemViewModel<AssistBaseType>(AssistBaseType.Cruise, "Cruise")
			};

		public List<ValueItemViewModel<AssistPasVariant>> AssistPasVariantOptions
		{
			get
			{
				var variants = new List<ValueItemViewModel<AssistPasVariant>>
				{
					new ValueItemViewModel<AssistPasVariant>(AssistPasVariant.Cadence, "Cadence")
				};

				if (_configVm.IsTorqueSensorSupported)
				{
					variants.Add(new ValueItemViewModel<AssistPasVariant>(AssistPasVariant.Torque, "Torque"));
				}

				variants.Add(new ValueItemViewModel<AssistPasVariant>(AssistPasVariant.Variable, "Variable"));
				variants.Add(new ValueItemViewModel<AssistPasVariant>(AssistPasVariant.Power, "Power (W from cadence)"));

				return variants;
			}
		}
			



		private int _id;
		public int Id
		{
			get { return _id; }
		}


		public ValueItemViewModel<AssistBaseType> SelectedType
		{
			get
			{
				var type = AssistBaseType.Disabled;

				if (_level.Type.HasFlag(Configuration.AssistFlagsType.Pas))
				{
					type = AssistBaseType.Pas;
				}
				else if (_level.Type.HasFlag(Configuration.AssistFlagsType.Throttle))
				{
					type = AssistBaseType.Throttle;
				}
				else if (_level.Type.HasFlag(Configuration.AssistFlagsType.Cruise))
				{
					type = AssistBaseType.Cruise;
				}

				return AssistBaseTypeOptions.FirstOrDefault((e) => e.Value == type);
			}
			set
			{
				if (value.Value != SelectedType.Value)
				{
					_level.Type = ApplyBaseTypeFlag(value.Value, _level.Type);
					OnPropertyChanged(nameof(SelectedType));

					switch (value.Value)
					{
						case AssistBaseType.Disabled:
							TargetCurrentPercent = 0;
							MaxThrottlePercent = 0;
							MaxSpeedPercent = 0;
							TorqueAmplificationFactor = 0;
							_level.Type = ClearThrottleFlag(_level.Type);
							_level.Type = ClearPasVariantFlag(_level.Type);
							IsThrottleCadenceOverrideEnabled = false;
							IsThrottleSpeedOverrideEnabled = false;
							break;
						case AssistBaseType.Throttle:
							TargetCurrentPercent = 0;
							TorqueAmplificationFactor = 0;
							TargetCurrentPercent = 0;
							_level.Type = ClearPasVariantFlag(_level.Type);
							IsThrottleCadenceOverrideEnabled = false;
							IsThrottleSpeedOverrideEnabled = false;
							break;
						case AssistBaseType.Pas:
							MaxThrottlePercent = 0;
							_level.Type = ClearThrottleFlag(_level.Type);
							break;
					}
				}
			}
		}

		public ValueItemViewModel<AssistPasVariant> SelectedPasVariant
		{
			get
			{
				var variant = AssistPasVariant.Cadence;
				if (_level.Type.HasFlag(Configuration.AssistFlagsType.PasTorque))
				{
					variant = AssistPasVariant.Torque;
				}
				else if (_level.Type.HasFlag(Configuration.AssistFlagsType.PasVariable))
				{
					variant = AssistPasVariant.Variable;
				}
				else if (_level.Type.HasFlag(Configuration.AssistFlagsType.PasPower))
				{
					variant = AssistPasVariant.Power;
				}

				return AssistPasVariantOptions.FirstOrDefault((e) => e.Value == variant);
			}
			set
			{
				if (value.Value != SelectedPasVariant.Value)
				{
					_level.Type = ApplyPasVariantFlag(value.Value, _level.Type);
					OnPropertyChanged(nameof(SelectedPasVariant));
					OnPropertyChanged(nameof(IsPasAssistVariableVariant));
					OnPropertyChanged(nameof(IsPasAssistTorqueVariant));
					OnPropertyChanged(nameof(IsPasAssistPowerVariant));

					switch(value.Value)
					{
						case AssistPasVariant.Variable:
							TorqueAmplificationFactor = 0;
							IsThrottleEnabled = false;
							IsThrottleCadenceOverrideEnabled = false;
							IsThrottleSpeedOverrideEnabled = false;
							MaxThrottlePercent = 0;
							break;
						case AssistPasVariant.Cadence:
						case AssistPasVariant.Power:
							TorqueAmplificationFactor = 0;
							break;
					}
				}
			}
		}

		public bool IsThrottleEnabled
		{
			get { return _level.Type.HasFlag(Configuration.AssistFlagsType.Throttle); }
			set
			{
				if (value != IsThrottleEnabled)
				{
					_level.Type = ApplyThrottleFlag(value, _level.Type);
					OnPropertyChanged(nameof(IsThrottleEnabled));
				}
			}
		}

		public bool IsThrottleCadenceOverrideEnabled
		{
			get { return _level.Type.HasFlag(Configuration.AssistFlagsType.CadenceOverride); }
			set
			{
				if (value != IsThrottleCadenceOverrideEnabled)
				{
					_level.Type = ApplyThrottleCadenceOverrideFlag(value, _level.Type);
					OnPropertyChanged(nameof(IsThrottleCadenceOverrideEnabled));
				}
			}
		}

		public bool IsThrottleSpeedOverrideEnabled
		{
			get { return _level.Type.HasFlag(Configuration.AssistFlagsType.SpeedOverride); }
			set
			{
				if (value != IsThrottleSpeedOverrideEnabled)
				{
					_level.Type = ApplyThrottleSpeedOverrideFlag(value, _level.Type);
					OnPropertyChanged(nameof(IsThrottleSpeedOverrideEnabled));
				}
			}
		}

		public bool IsPasAssistVariableVariant
		{
			get { return _level.Type.HasFlag(Configuration.AssistFlagsType.PasVariable); }
		}

		public bool IsPasAssistTorqueVariant
		{
			get { return _level.Type.HasFlag(Configuration.AssistFlagsType.PasTorque); }
		}

		public bool IsPasAssistPowerVariant
		{
			get { return _level.Type.HasFlag(Configuration.AssistFlagsType.PasPower); }
		}

		public uint PowerStartWatts
		{
			get { return _level.PowerStartWatts; }
			set
			{
				if (_level.PowerStartWatts != value)
				{
					_level.PowerStartWatts = value;
					OnPropertyChanged(nameof(PowerStartWatts));
					OnPowerCurveChanged();
				}
			}
		}

		public float PowerWattsPerRpm
		{
			get { return _level.PowerWattsPerRpm; }
			set
			{
				if (_level.PowerWattsPerRpm != value)
				{
					_level.PowerWattsPerRpm = value;
					OnPropertyChanged(nameof(PowerWattsPerRpm));
					OnPowerCurveChanged();
				}
			}
		}

		public uint CurrentRampAmpsSecond
		{
			get { return _level.CurrentRampAmpsSecond; }
			set
			{
				if (_level.CurrentRampAmpsSecond != value)
				{
					_level.CurrentRampAmpsSecond = value;
					OnPropertyChanged(nameof(CurrentRampAmpsSecond));
				}
			}
		}

		// Power curve preview: battery power (W) against cadence, 0-120 rpm, as
		// the firmware computes it, flattened at the level's current cap. The
		// cap in watts uses the middle of the configured battery voltage range.
		public const double PreviewWidth = 240;
		public const double PreviewHeight = 100;
		private const double PreviewMaxRpm = 120;

		private double PowerCapWatts
		{
			get
			{
				double volts = (_configVm.MaxBatteryVolts + _configVm.LowCutoffVolts) / 2.0;
				return _configVm.MaxCurrentAmps * _level.MaxCurrentPercent / 100.0 * volts;
			}
		}

		private double PowerAtCadence(double rpm)
		{
			return Math.Min(_level.PowerStartWatts + _level.PowerWattsPerRpm * rpm, PowerCapWatts);
		}

		private double PreviewMaxWatts
		{
			get { return Math.Max(100.0, Math.Max(PowerCapWatts, _level.PowerStartWatts + _level.PowerWattsPerRpm * PreviewMaxRpm)) * 1.1; }
		}

		public PointCollection PowerCurvePoints
		{
			get
			{
				var points = new PointCollection();
				double maxWatts = PreviewMaxWatts;
				for (int rpm = 0; rpm <= PreviewMaxRpm; rpm += 5)
				{
					points.Add(new Point(rpm / PreviewMaxRpm * PreviewWidth,
						PreviewHeight - PowerAtCadence(rpm) / maxWatts * PreviewHeight));
				}
				return points;
			}
		}

		public string PowerCurveSummary
		{
			get
			{
				return $"40 rpm: {PowerAtCadence(40):0} W   70 rpm: {PowerAtCadence(70):0} W   " +
					$"100 rpm: {PowerAtCadence(100):0} W   (cap {PowerCapWatts:0} W)";
			}
		}

		private void OnPowerCurveChanged()
		{
			OnPropertyChanged(nameof(PowerCurvePoints));
			OnPropertyChanged(nameof(PowerCurveSummary));
		}



		public uint TargetCurrentPercent
		{
			get { return _level.MaxCurrentPercent; }
			set
			{
				if (_level.MaxCurrentPercent != value)
				{
					_level.MaxCurrentPercent = value;
					OnPropertyChanged(nameof(TargetCurrentPercent));
					OnPowerCurveChanged();
				}
			}
		}

		public uint MaxThrottlePercent
		{
			get { return _level.MaxThrottlePercent; }
			set
			{
				if (_level.MaxThrottlePercent != value)
				{
					_level.MaxThrottlePercent = value;
					OnPropertyChanged(nameof(MaxThrottlePercent));
				}
			}
		}

		public uint MaxCadencePercent
		{
			get { return _level.MaxCadencePercent; }
			set
			{
				if (_level.MaxCadencePercent != value)
				{
					_level.MaxCadencePercent = value;
					OnPropertyChanged(nameof(MaxCadencePercent));
				}
			}
		}

		public uint MaxSpeedPercent
		{
			get { return _level.MaxSpeedPercent; }
			set
			{
				if (_level.MaxSpeedPercent != value)
				{
					_level.MaxSpeedPercent = value;
					OnPropertyChanged(nameof(MaxSpeedPercent));
				}
			}
		}

		public float TorqueAmplificationFactor
		{
			get { return _level.TorqueAmplificationFactor; }
			set
			{
				if (_level.TorqueAmplificationFactor != value)
				{
					_level.TorqueAmplificationFactor = value;
					OnPropertyChanged(nameof(TorqueAmplificationFactor));
				}
			}
		}


		public AssistLevelViewModel(ConfigurationViewModel configVm, int id, Configuration.AssistLevel level)
		{
			_configVm = configVm;
			_id = id;
			_level = level;
		}


		private static Configuration.AssistFlagsType ApplyBaseTypeFlag(AssistBaseType baseType, Configuration.AssistFlagsType flags)
		{
			byte f = (byte)flags;
			f &= (byte)~(Configuration.AssistFlagsType.Pas | Configuration.AssistFlagsType.Throttle | Configuration.AssistFlagsType.Cruise);

			var result = (Configuration.AssistFlagsType)f;
			switch (baseType)
			{
				case AssistBaseType.Pas:
					result |= Configuration.AssistFlagsType.Pas;
					break;
				case AssistBaseType.Throttle:
					result |= Configuration.AssistFlagsType.Throttle;
					break;
				case AssistBaseType.Cruise:
					result |= Configuration.AssistFlagsType.Cruise;
					break;
			}

			return result;
		}

		private static Configuration.AssistFlagsType ClearPasVariantFlag(Configuration.AssistFlagsType flags)
		{
			byte f = (byte)flags;
			f &= (byte)~(Configuration.AssistFlagsType.PasTorque | Configuration.AssistFlagsType.PasVariable | Configuration.AssistFlagsType.PasPower);

			return (Configuration.AssistFlagsType)f;
		}

		private static Configuration.AssistFlagsType ApplyPasVariantFlag(AssistPasVariant pasVariant, Configuration.AssistFlagsType flags)
		{
			var result = ClearPasVariantFlag(flags);
			switch (pasVariant)
			{
				case AssistPasVariant.Torque:
					result |= Configuration.AssistFlagsType.PasTorque;
					break;
				case AssistPasVariant.Variable:
					result |= Configuration.AssistFlagsType.PasVariable;
					break;
				case AssistPasVariant.Power:
					result |= Configuration.AssistFlagsType.PasPower;
					break;
			}

			return result;
		}

		private static Configuration.AssistFlagsType ClearThrottleFlag(Configuration.AssistFlagsType flags)
		{
			byte f = (byte)flags;
			f &= (byte)~(Configuration.AssistFlagsType.Throttle);

			return (Configuration.AssistFlagsType)f;
		}

		private static Configuration.AssistFlagsType ApplyThrottleFlag(bool enabled, Configuration.AssistFlagsType flags)
		{
			var result = ClearThrottleFlag(flags);
			if (enabled)
			{
				result |= Configuration.AssistFlagsType.Throttle;
			}

			return result;
		}

		private static Configuration.AssistFlagsType ClearThrottleCadenceOverrideFlag(Configuration.AssistFlagsType flags)
		{
			byte f = (byte)flags;
			f &= (byte)~(Configuration.AssistFlagsType.CadenceOverride);

			return (Configuration.AssistFlagsType)f;
		}

		private static Configuration.AssistFlagsType ApplyThrottleCadenceOverrideFlag(bool enabled, Configuration.AssistFlagsType flags)
		{
			var result = ClearThrottleCadenceOverrideFlag(flags);
			if (enabled)
			{
				result |= Configuration.AssistFlagsType.CadenceOverride;
			}

			return result;
		}

		private static Configuration.AssistFlagsType ClearThrottleSpeedOverrideFlag(Configuration.AssistFlagsType flags)
		{
			byte f = (byte)flags;
			f &= (byte)~(Configuration.AssistFlagsType.SpeedOverride);

			return (Configuration.AssistFlagsType)f;
		}

		private static Configuration.AssistFlagsType ApplyThrottleSpeedOverrideFlag(bool enabled, Configuration.AssistFlagsType flags)
		{
			var result = ClearThrottleSpeedOverrideFlag(flags);
			if (enabled)
			{
				result |= Configuration.AssistFlagsType.SpeedOverride;
			}

			return result;
		}

	}
}
