import { describe, it, expect } from 'vitest'
import { AssistFlags } from './configuration'
import type { AssistLevel } from './configuration'
import {
	AssistBaseType,
	AssistPasVariant,
	getBaseType,
	getCadenceOverride,
	getPasVariant,
	getSpeedOverride,
	getThrottleEnabled,
	setBaseType,
	setCadenceOverride,
	setPasVariant,
	setSpeedOverride,
	setThrottleEnabled,
} from './assistlevel'

function level(overrides: Partial<AssistLevel> = {}): AssistLevel {
	return {
		flags: AssistFlags.None,
		targetCurrentPercent: 50,
		maxThrottleCurrentPercent: 80,
		maxCadencePercent: 90,
		maxSpeedPercent: 100,
		torqueAmplificationFactor: 1.5,
		...overrides,
	}
}

describe('getBaseType', () => {
	it('reads PAS even when throttle is also enabled (throttle is an add-on)', () => {
		expect(getBaseType(level({ flags: AssistFlags.Pas | AssistFlags.Throttle }))).toBe(AssistBaseType.Pas)
	})

	it('reads each pure base type', () => {
		expect(getBaseType(level({ flags: AssistFlags.None }))).toBe(AssistBaseType.Disabled)
		expect(getBaseType(level({ flags: AssistFlags.Throttle }))).toBe(AssistBaseType.Throttle)
		expect(getBaseType(level({ flags: AssistFlags.Cruise }))).toBe(AssistBaseType.Cruise)
	})
})

describe('setBaseType', () => {
	it('switching to Disabled clears all flags and zeroes the dependent fields', () => {
		const l = level({
			flags: AssistFlags.Pas | AssistFlags.Throttle | AssistFlags.OverrideCadence | AssistFlags.PasTorque,
		})
		setBaseType(l, AssistBaseType.Disabled)

		expect(l.flags).toBe(AssistFlags.None)
		expect(l.targetCurrentPercent).toBe(0)
		expect(l.maxThrottleCurrentPercent).toBe(0)
		expect(l.maxSpeedPercent).toBe(0)
		expect(l.torqueAmplificationFactor).toBe(0)
		// maxCadencePercent is intentionally NOT zeroed by the Disabled rule
		expect(l.maxCadencePercent).toBe(90)
	})

	it('switching to Throttle clears PAS variant/overrides and zeroes target current + torque', () => {
		const l = level({ flags: AssistFlags.Pas | AssistFlags.PasTorque | AssistFlags.OverrideSpeed })
		setBaseType(l, AssistBaseType.Throttle)

		expect(getBaseType(l)).toBe(AssistBaseType.Throttle)
		expect(l.flags & AssistFlags.PasTorque).toBe(0)
		expect(l.flags & AssistFlags.OverrideSpeed).toBe(0)
		expect(l.flags & AssistFlags.Pas).toBe(0)
		expect(l.targetCurrentPercent).toBe(0)
		expect(l.torqueAmplificationFactor).toBe(0)
		// the Throttle level keeps using maxThrottleCurrentPercent, so it's preserved
		expect(l.maxThrottleCurrentPercent).toBe(80)
	})

	it('switching to PAS clears the throttle add-on bit and zeroes max throttle current', () => {
		const l = level({ flags: AssistFlags.Throttle })
		setBaseType(l, AssistBaseType.Pas)

		expect(getBaseType(l)).toBe(AssistBaseType.Pas)
		expect(getThrottleEnabled(l)).toBe(false)
		expect(l.maxThrottleCurrentPercent).toBe(0)
	})

	it('switching to Cruise sets the cruise bit but leaves a stray PAS-variant bit (matches WPF)', () => {
		// Faithful to AssistLevelViewModel.cs, whose SelectedType setter has no
		// Cruise case, so it never clears PAS-variant bits. Harmless: the
		// firmware only reads PasTorque/PasVariable when ASSIST_FLAG_PAS is set
		// (app.c:365,409), and Cruise levels don't set it. Preserving the quirk
		// keeps byte-parity with the WPF tool during the transition.
		const l = level({ flags: AssistFlags.Pas | AssistFlags.PasVariable })
		setBaseType(l, AssistBaseType.Cruise)

		expect(l.flags).toBe(AssistFlags.Cruise | AssistFlags.PasVariable)
		expect(getBaseType(l)).toBe(AssistBaseType.Cruise)
	})
})

describe('PAS variant', () => {
	it('round-trips each variant', () => {
		const l = level({ flags: AssistFlags.Pas })

		setPasVariant(l, AssistPasVariant.Torque)
		expect(getPasVariant(l)).toBe(AssistPasVariant.Torque)
		expect(l.flags & AssistFlags.Pas).toBe(AssistFlags.Pas) // base type preserved

		setPasVariant(l, AssistPasVariant.Variable)
		expect(getPasVariant(l)).toBe(AssistPasVariant.Variable)
		expect(l.flags & AssistFlags.PasTorque).toBe(0)

		setPasVariant(l, AssistPasVariant.Cadence)
		expect(getPasVariant(l)).toBe(AssistPasVariant.Cadence)
		expect(l.flags & (AssistFlags.PasTorque | AssistFlags.PasVariable)).toBe(0)
	})

	it('switching to Variable disables throttle, overrides, max throttle, and torque', () => {
		const l = level({
			flags: AssistFlags.Pas | AssistFlags.Throttle | AssistFlags.OverrideCadence | AssistFlags.OverrideSpeed,
			torqueAmplificationFactor: 2.0,
			maxThrottleCurrentPercent: 100,
		})
		setPasVariant(l, AssistPasVariant.Variable)

		expect(getThrottleEnabled(l)).toBe(false)
		expect(getCadenceOverride(l)).toBe(false)
		expect(getSpeedOverride(l)).toBe(false)
		expect(l.maxThrottleCurrentPercent).toBe(0)
		expect(l.torqueAmplificationFactor).toBe(0)
		expect(getPasVariant(l)).toBe(AssistPasVariant.Variable)
	})

	it('switching to Cadence zeroes torque amplification', () => {
		const l = level({ flags: AssistFlags.Pas | AssistFlags.PasTorque, torqueAmplificationFactor: 2.0 })
		setPasVariant(l, AssistPasVariant.Cadence)
		expect(l.torqueAmplificationFactor).toBe(0)
	})
})

describe('add-on flag setters', () => {
	it('toggle throttle / cadence override / speed override independently', () => {
		const l = level({ flags: AssistFlags.Pas })

		setThrottleEnabled(l, true)
		setCadenceOverride(l, true)
		setSpeedOverride(l, true)
		expect(l.flags).toBe(
			AssistFlags.Pas | AssistFlags.Throttle | AssistFlags.OverrideCadence | AssistFlags.OverrideSpeed,
		)

		setCadenceOverride(l, false)
		expect(getCadenceOverride(l)).toBe(false)
		expect(getThrottleEnabled(l)).toBe(true)
		expect(getSpeedOverride(l)).toBe(true)
		// base type bit untouched throughout
		expect(getBaseType(l)).toBe(AssistBaseType.Pas)
	})

	it('keeps flags within a byte', () => {
		const l = level({ flags: AssistFlags.None })
		setThrottleEnabled(l, true)
		setThrottleEnabled(l, false)
		expect(l.flags).toBeGreaterThanOrEqual(0)
		expect(l.flags).toBeLessThanOrEqual(0xff)
	})
})
