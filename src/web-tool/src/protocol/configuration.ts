// TypeScript port of src/tool/Model/Configuration.cs, V6 only (this fork's
// current format -- see ROADMAP.md Phase 1.5 for why legacy versions aren't
// supported here). Field order below must match config_t in
// src/firmware/cfgstore.h exactly: the wire protocol is a raw byte dump of
// that struct, not a tagged/named format.

export const CONFIG_VERSION = 6
export const CONFIG_BYTE_SIZE = 156

export enum Controller {
	Unknown = 0,
	BBSHD = 1,
	BBS02 = 2,
	TSDZ2 = 3,
}

// Hardware current limit per controller -- matches Configuration.cs's
// MaxCurrentLimitAmps. Used to bound Max Current (A) validation; setting this
// too high risks damaging the controller, so the bound should always reflect
// the actually-connected hardware, not a generic fallback.
export function maxCurrentLimitAmps(controller: Controller): number {
	switch (controller) {
		case Controller.BBSHD:
			return 33
		case Controller.BBS02:
			return 30
		case Controller.TSDZ2:
			return 20
		default:
			return 50
	}
}

export enum TemperatureSensor {
	Disabled = 0x00,
	Controller = 0x01,
	Motor = 0x02,
	All = 0x03,
}

export enum LightsMode {
	Default = 0,
	Disabled = 1,
	AlwaysOn = 2,
	BrakeLight = 3,
}

export enum ThrottleGlobalSpeedLimitOption {
	Disabled = 0,
	Enabled = 1,
	StandardLevels = 2,
}

export enum WalkModeData {
	Speed = 0,
	Temperature = 1,
	RequestedPower = 2,
	BatteryPercent = 3,
}

export enum DisplayRangeFieldData {
	Zero = 0,
	Temperature = 1,
	Power = 2,
}

export enum AssistModeSelect {
	Off = 0x00,
	Standard = 0x01,
	Lights = 0x02,
	Pas0Light = 0x03,
	Pas1Light = 0x04,
	Pas2Light = 0x05,
	Pas3Light = 0x06,
	Pas4Light = 0x07,
	Pas5Light = 0x08,
	Pas6Light = 0x09,
	Pas7Light = 0x0a,
	Pas8Light = 0x0b,
	Pas9Light = 0x0c,
	BrakesOnBoot = 0x0d,
}

export enum AssistFlags {
	None = 0x00,
	Pas = 0x01,
	Throttle = 0x02,
	Cruise = 0x04,
	PasVariable = 0x08,
	PasTorque = 0x10,
	OverrideCadence = 0x20,
	OverrideSpeed = 0x40,
}

export interface AssistLevel {
	flags: AssistFlags
	targetCurrentPercent: number
	maxThrottleCurrentPercent: number
	maxCadencePercent: number
	maxSpeedPercent: number
	/** Real multiplier, e.g. 1.0 means 100% -- wire value is this * 10. */
	torqueAmplificationFactor: number
}

export interface Configuration {
	useFreedomUnits: boolean

	maxCurrentAmps: number
	currentRampAmpsSecond: number
	maxBatteryVolts: number
	lowCutoffVolts: number
	maxSpeedKph: number

	useSpeedSensor: boolean
	useShiftSensor: boolean
	usePushWalk: boolean
	useTemperatureSensor: TemperatureSensor
	lightsMode: LightsMode
	usePretension: boolean
	pretensionSpeedCutoffKph: number

	wheelSizeInch: number
	numWheelSensorSignals: number

	pasStartDelayPulses: number
	pasStopDelayMilliseconds: number
	pasKeepCurrentPercent: number
	pasKeepCurrentCadenceRpm: number

	throttleStartMillivolts: number
	throttleEndMillivolts: number
	throttleStartPercent: number
	throttleGlobalSpeedLimit: ThrottleGlobalSpeedLimitOption
	throttleGlobalSpeedLimitPercent: number

	shiftInterruptDuration: number
	shiftInterruptCurrentThresholdPercent: number

	walkModeDataDisplay: WalkModeData

	assistModeSelection: AssistModeSelect
	assistStartupLevel: number
	standardAssistLevels: AssistLevel[]
	sportAssistLevels: AssistLevel[]

	displayRangeField: DisplayRangeFieldData
	maxCadenceRpm: number
}

function createDefaultAssistLevel(): AssistLevel {
	return {
		flags: AssistFlags.None,
		targetCurrentPercent: 0,
		maxThrottleCurrentPercent: 0,
		maxCadencePercent: 0,
		maxSpeedPercent: 0,
		torqueAmplificationFactor: 0,
	}
}

// A blank placeholder shown before any config has been read from a
// controller -- NOT the firmware's actual per-target factory defaults.
// Those only exist on the firmware side (cfgstore.c's load_default_config());
// the tool triggers them via the reset-config opcode and then re-reads.
export function createDefaultConfiguration(): Configuration {
	return {
		useFreedomUnits: false,

		maxCurrentAmps: 0,
		currentRampAmpsSecond: 0,
		maxBatteryVolts: 0,
		lowCutoffVolts: 0,
		maxSpeedKph: 0,

		useSpeedSensor: false,
		useShiftSensor: false,
		usePushWalk: false,
		useTemperatureSensor: TemperatureSensor.All,
		lightsMode: LightsMode.Default,
		usePretension: false,
		pretensionSpeedCutoffKph: 0,

		wheelSizeInch: 0,
		numWheelSensorSignals: 0,

		pasStartDelayPulses: 0,
		pasStopDelayMilliseconds: 0,
		pasKeepCurrentPercent: 0,
		pasKeepCurrentCadenceRpm: 0,

		throttleStartMillivolts: 0,
		throttleEndMillivolts: 0,
		throttleStartPercent: 0,
		throttleGlobalSpeedLimit: ThrottleGlobalSpeedLimitOption.Disabled,
		throttleGlobalSpeedLimitPercent: 0,

		shiftInterruptDuration: 0,
		shiftInterruptCurrentThresholdPercent: 0,

		walkModeDataDisplay: WalkModeData.Speed,

		assistModeSelection: AssistModeSelect.Off,
		assistStartupLevel: 0,
		standardAssistLevels: Array.from({ length: 10 }, createDefaultAssistLevel),
		sportAssistLevels: Array.from({ length: 10 }, createDefaultAssistLevel),

		displayRangeField: DisplayRangeFieldData.Zero,
		maxCadenceRpm: 0,
	}
}

class ByteReader {
	private pos = 0
	constructor(private readonly buf: Uint8Array) {}

	byte(): number {
		return this.buf[this.pos++]
	}

	bool(): boolean {
		return this.byte() !== 0
	}

	/** Little-endian: matches the firmware's *_u16l/*_u16h field order and .NET's BinaryReader.ReadUInt16(). */
	u16(): number {
		const lo = this.byte()
		const hi = this.byte()
		return hi * 256 + lo
	}
}

class ByteWriter {
	private readonly bytes: number[] = []

	byte(value: number): void {
		this.bytes.push(value & 0xff)
	}

	bool(value: boolean): void {
		this.byte(value ? 1 : 0)
	}

	u16(value: number): void {
		this.byte(value)
		this.byte(value >> 8)
	}

	toBytes(): Uint8Array {
		return new Uint8Array(this.bytes)
	}
}

function readAssistLevel(r: ByteReader): AssistLevel {
	return {
		flags: r.byte(),
		targetCurrentPercent: r.byte(),
		maxThrottleCurrentPercent: r.byte(),
		maxCadencePercent: r.byte(),
		maxSpeedPercent: r.byte(),
		torqueAmplificationFactor: r.byte() / 10,
	}
}

function writeAssistLevel(w: ByteWriter, level: AssistLevel): void {
	w.byte(level.flags)
	w.byte(level.targetCurrentPercent)
	w.byte(level.maxThrottleCurrentPercent)
	w.byte(level.maxCadencePercent)
	w.byte(level.maxSpeedPercent)
	w.byte(Math.round(level.torqueAmplificationFactor * 10))
}

export function parseConfiguration(buffer: Uint8Array): Configuration {
	if (buffer.length !== CONFIG_BYTE_SIZE) {
		throw new Error(`Expected ${CONFIG_BYTE_SIZE} bytes for config V${CONFIG_VERSION}, got ${buffer.length}`)
	}

	const r = new ByteReader(buffer)

	const cfg: Configuration = {
		useFreedomUnits: r.bool(),

		maxCurrentAmps: r.byte(),
		currentRampAmpsSecond: r.byte(),
		maxBatteryVolts: r.u16() / 100,
		lowCutoffVolts: r.byte(),
		maxSpeedKph: r.byte(),

		useSpeedSensor: r.bool(),
		useShiftSensor: r.bool(),
		usePushWalk: r.bool(),
		useTemperatureSensor: r.byte(),
		lightsMode: r.byte(),
		usePretension: r.bool(),
		pretensionSpeedCutoffKph: r.byte(),

		wheelSizeInch: r.u16() / 10,
		numWheelSensorSignals: r.byte(),

		pasStartDelayPulses: r.byte(),
		pasStopDelayMilliseconds: r.byte() * 10,
		pasKeepCurrentPercent: r.byte(),
		pasKeepCurrentCadenceRpm: r.byte(),

		throttleStartMillivolts: r.u16(),
		throttleEndMillivolts: r.u16(),
		throttleStartPercent: r.byte(),
		throttleGlobalSpeedLimit: r.byte(),
		throttleGlobalSpeedLimitPercent: r.byte(),

		shiftInterruptDuration: r.u16(),
		shiftInterruptCurrentThresholdPercent: r.byte(),

		walkModeDataDisplay: r.byte(),

		assistModeSelection: r.byte(),
		assistStartupLevel: r.byte(),
		standardAssistLevels: [],
		sportAssistLevels: [],

		displayRangeField: DisplayRangeFieldData.Zero,
		maxCadenceRpm: 0,
	}

	for (let i = 0; i < 10; ++i) {
		cfg.standardAssistLevels.push(readAssistLevel(r))
	}
	for (let i = 0; i < 10; ++i) {
		cfg.sportAssistLevels.push(readAssistLevel(r))
	}

	cfg.displayRangeField = r.byte()
	cfg.maxCadenceRpm = r.byte()

	return cfg
}

export function writeConfiguration(cfg: Configuration): Uint8Array {
	const w = new ByteWriter()

	w.bool(cfg.useFreedomUnits)

	w.byte(cfg.maxCurrentAmps)
	w.byte(cfg.currentRampAmpsSecond)
	w.u16(Math.round(cfg.maxBatteryVolts * 100))
	w.byte(cfg.lowCutoffVolts)
	w.byte(cfg.maxSpeedKph)

	w.bool(cfg.useSpeedSensor)
	w.bool(cfg.useShiftSensor)
	w.bool(cfg.usePushWalk)
	w.byte(cfg.useTemperatureSensor)
	w.byte(cfg.lightsMode)
	w.bool(cfg.usePretension)
	w.byte(cfg.pretensionSpeedCutoffKph)

	w.u16(Math.round(cfg.wheelSizeInch * 10))
	w.byte(cfg.numWheelSensorSignals)

	w.byte(cfg.pasStartDelayPulses)
	w.byte(Math.round(cfg.pasStopDelayMilliseconds / 10))
	w.byte(cfg.pasKeepCurrentPercent)
	w.byte(cfg.pasKeepCurrentCadenceRpm)

	w.u16(cfg.throttleStartMillivolts)
	w.u16(cfg.throttleEndMillivolts)
	w.byte(cfg.throttleStartPercent)
	w.byte(cfg.throttleGlobalSpeedLimit)
	w.byte(cfg.throttleGlobalSpeedLimitPercent)

	w.u16(cfg.shiftInterruptDuration)
	w.byte(cfg.shiftInterruptCurrentThresholdPercent)

	w.byte(cfg.walkModeDataDisplay)

	w.byte(cfg.assistModeSelection)
	w.byte(cfg.assistStartupLevel)

	for (const level of cfg.standardAssistLevels) {
		writeAssistLevel(w, level)
	}
	for (const level of cfg.sportAssistLevels) {
		writeAssistLevel(w, level)
	}

	w.byte(cfg.displayRangeField)
	w.byte(cfg.maxCadenceRpm)

	return w.toBytes()
}

function checkLimits(value: number, min: number, max: number, name: string, errors: string[]): void {
	if (value < min || value > max) {
		errors.push(`${name} must be in interval ${min}-${max}.`)
	}
}

// Mirrors Configuration.cs's Validate(). Returns all violations rather than
// throwing on the first one, since a web form can usefully show several at once.
// `controller` should be the actually-connected hardware so the current limit
// reflects what the controller can safely take, not a generic fallback.
export function validateConfiguration(cfg: Configuration, controller: Controller = Controller.Unknown): string[] {
	const errors: string[] = []

	checkLimits(cfg.maxCurrentAmps, 5, maxCurrentLimitAmps(controller), 'Max Current (A)', errors)
	checkLimits(cfg.currentRampAmpsSecond, 1, 255, 'Current Ramp (A/s)', errors)
	checkLimits(cfg.maxBatteryVolts, 1, 100, 'Max Battery Voltage (V)', errors)
	checkLimits(cfg.lowCutoffVolts, 1, 100, 'Low Voltage Cut Off (V)', errors)

	checkLimits(cfg.wheelSizeInch, 10, 40, 'Wheel Size (inch)', errors)
	checkLimits(cfg.numWheelSensorSignals, 1, 10, 'Wheel Sensor Signals', errors)
	checkLimits(cfg.maxSpeedKph, 0, 180, 'Max Speed (km/h)', errors)
	checkLimits(cfg.pretensionSpeedCutoffKph, 0, 100, 'Pretension Speed Cutoff (km/h)', errors)

	checkLimits(cfg.pasStartDelayPulses, 0, 24, 'Pas Delay (pulses)', errors)
	checkLimits(cfg.pasStopDelayMilliseconds, 50, 1000, 'Pas Stop Delay (ms)', errors)
	checkLimits(cfg.pasKeepCurrentPercent, 10, 100, 'Pas Keep Current (%)', errors)
	checkLimits(cfg.pasKeepCurrentCadenceRpm, 0, 255, 'Pas Keep Current Cadence (rpm)', errors)

	checkLimits(cfg.throttleStartMillivolts, 200, 2500, 'Throttle Start (mV)', errors)
	checkLimits(cfg.throttleEndMillivolts, 2500, 5000, 'Throttle End (mV)', errors)
	checkLimits(cfg.throttleStartPercent, 0, 100, 'Throttle Start (%)', errors)
	checkLimits(cfg.throttleGlobalSpeedLimitPercent, 0, 100, 'Throttle Global Speed Limit (%)', errors)

	checkLimits(cfg.shiftInterruptDuration, 50, 2000, 'Shift Interrupt Duration (ms)', errors)
	checkLimits(cfg.shiftInterruptCurrentThresholdPercent, 0, 100, 'Shift Interrupt Current Threshold (%)', errors)

	checkLimits(cfg.maxCadenceRpm, 60, 255, 'Max Cadence (rpm)', errors)

	checkLimits(cfg.assistStartupLevel, 0, 9, 'Assist Startup Level', errors)

	return errors
}
