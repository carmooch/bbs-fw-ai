import { describe, it, expect } from 'vitest'
import {
	CONFIG_BYTE_SIZE,
	Controller,
	DisplayRangeFieldData,
	LightsMode,
	TemperatureSensor,
	ThrottleGlobalSpeedLimitOption,
	WalkModeData,
	AssistFlags,
	createDefaultConfiguration,
	maxCurrentLimitAmps,
	parseConfiguration,
	validateConfiguration,
	writeConfiguration,
} from './configuration'

describe('writeConfiguration / parseConfiguration round trip', () => {
	it('produces a buffer of exactly CONFIG_BYTE_SIZE', () => {
		const cfg = createDefaultConfiguration()
		expect(writeConfiguration(cfg).length).toBe(CONFIG_BYTE_SIZE)
	})

	it('recovers every field unchanged for a realistic, non-default config', () => {
		const cfg = createDefaultConfiguration()

		cfg.useFreedomUnits = true
		cfg.maxCurrentAmps = 30
		cfg.currentRampAmpsSecond = 10
		cfg.maxBatteryVolts = 54.6
		cfg.lowCutoffVolts = 42
		cfg.maxSpeedKph = 45

		cfg.useSpeedSensor = true
		cfg.useShiftSensor = true
		cfg.usePushWalk = true
		cfg.useTemperatureSensor = TemperatureSensor.Motor
		cfg.lightsMode = LightsMode.BrakeLight
		cfg.usePretension = true
		cfg.pretensionSpeedCutoffKph = 16

		cfg.wheelSizeInch = 27.5
		cfg.numWheelSensorSignals = 3

		cfg.pasStartDelayPulses = 5
		cfg.pasStopDelayMilliseconds = 200
		cfg.pasKeepCurrentPercent = 60
		cfg.pasKeepCurrentCadenceRpm = 40

		cfg.throttleStartMillivolts = 1000
		cfg.throttleEndMillivolts = 3600
		cfg.throttleStartPercent = 1
		cfg.throttleGlobalSpeedLimit = ThrottleGlobalSpeedLimitOption.StandardLevels
		cfg.throttleGlobalSpeedLimitPercent = 80

		cfg.shiftInterruptDuration = 600
		cfg.shiftInterruptCurrentThresholdPercent = 10

		cfg.walkModeDataDisplay = WalkModeData.BatteryPercent

		cfg.standardAssistLevels[3].flags = AssistFlags.Pas | AssistFlags.Throttle
		cfg.standardAssistLevels[3].targetCurrentPercent = 70
		cfg.standardAssistLevels[3].maxThrottleCurrentPercent = 100
		cfg.standardAssistLevels[3].maxCadencePercent = 100
		cfg.standardAssistLevels[3].maxSpeedPercent = 100
		cfg.standardAssistLevels[3].torqueAmplificationFactor = 1.5

		cfg.sportAssistLevels[9].flags = AssistFlags.Pas | AssistFlags.PasTorque
		cfg.sportAssistLevels[9].targetCurrentPercent = 100
		cfg.sportAssistLevels[9].torqueAmplificationFactor = 2.5

		cfg.displayRangeField = DisplayRangeFieldData.Temperature
		cfg.maxCadenceRpm = 168

		const roundTripped = parseConfiguration(writeConfiguration(cfg))

		expect(roundTripped).toEqual(cfg)
	})

	it('rejects a buffer of the wrong length', () => {
		expect(() => parseConfiguration(new Uint8Array(10))).toThrow()
	})

	it('round trips fractional volt/inch fields without drift', () => {
		const cfg = createDefaultConfiguration()
		cfg.maxBatteryVolts = 58.8
		cfg.wheelSizeInch = 26.0

		const roundTripped = parseConfiguration(writeConfiguration(cfg))

		expect(roundTripped.maxBatteryVolts).toBeCloseTo(58.8, 5)
		expect(roundTripped.wheelSizeInch).toBeCloseTo(26.0, 5)
	})
})

describe('maxCurrentLimitAmps', () => {
	it('matches the hardware limit per controller', () => {
		expect(maxCurrentLimitAmps(Controller.BBSHD)).toBe(33)
		expect(maxCurrentLimitAmps(Controller.BBS02)).toBe(30)
		expect(maxCurrentLimitAmps(Controller.TSDZ2)).toBe(20)
		expect(maxCurrentLimitAmps(Controller.Unknown)).toBe(50)
	})
})

describe('validateConfiguration', () => {
	it('accepts a config within all bounds', () => {
		const cfg = createDefaultConfiguration()
		cfg.maxCurrentAmps = 30
		cfg.currentRampAmpsSecond = 10
		cfg.maxBatteryVolts = 54.6
		cfg.lowCutoffVolts = 42
		cfg.wheelSizeInch = 27.5
		cfg.numWheelSensorSignals = 3
		cfg.pasStartDelayPulses = 5
		cfg.pasStopDelayMilliseconds = 200
		cfg.pasKeepCurrentPercent = 60
		cfg.throttleStartMillivolts = 1000
		cfg.throttleEndMillivolts = 3600
		cfg.shiftInterruptDuration = 600
		cfg.maxCadenceRpm = 150
		cfg.assistStartupLevel = 3

		expect(validateConfiguration(cfg, Controller.BBSHD)).toEqual([])
	})

	it('flags max current above the connected controller hardware limit', () => {
		const cfg = createDefaultConfiguration()
		cfg.maxCurrentAmps = 40 // over BBS02's 30A limit
		cfg.currentRampAmpsSecond = 10
		cfg.maxBatteryVolts = 54.6
		cfg.lowCutoffVolts = 42
		cfg.wheelSizeInch = 27.5
		cfg.numWheelSensorSignals = 3
		cfg.pasStopDelayMilliseconds = 200
		cfg.throttleStartMillivolts = 1000
		cfg.throttleEndMillivolts = 3600
		cfg.shiftInterruptDuration = 600
		cfg.maxCadenceRpm = 150

		const bbs02Errors = validateConfiguration(cfg, Controller.BBS02)
		expect(bbs02Errors.some((e) => e.includes('Max Current'))).toBe(true)

		cfg.maxCurrentAmps = 25
		expect(validateConfiguration(cfg, Controller.BBS02).some((e) => e.includes('Max Current'))).toBe(false)
	})

	it('flags an out-of-range max cadence', () => {
		const cfg = createDefaultConfiguration()
		cfg.maxCurrentAmps = 30
		cfg.currentRampAmpsSecond = 10
		cfg.maxBatteryVolts = 54.6
		cfg.lowCutoffVolts = 42
		cfg.wheelSizeInch = 27.5
		cfg.numWheelSensorSignals = 3
		cfg.pasStopDelayMilliseconds = 200
		cfg.throttleStartMillivolts = 1000
		cfg.throttleEndMillivolts = 3600
		cfg.shiftInterruptDuration = 600
		cfg.maxCadenceRpm = 10

		expect(validateConfiguration(cfg, Controller.BBSHD).some((e) => e.includes('Max Cadence'))).toBe(true)
	})
})
