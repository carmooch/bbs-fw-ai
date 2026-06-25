// Pure logic for interpreting and composing an assist level's `flags` byte,
// ported faithfully from AssistLevelViewModel.cs. The flags byte packs three
// concerns: a mutually-exclusive base type (PAS / Throttle / Cruise / none),
// the PAS variant (Cadence / Torque / Variable), and add-on bits (throttle
// enabled within a PAS level, plus cadence/speed overrides). Switching base
// type or PAS variant also zeroes fields that no longer apply — those
// coherence rules live here too, so they can be unit tested without a DOM.

import { AssistFlags } from './configuration'
import type { AssistLevel } from './configuration'

export enum AssistBaseType {
	Disabled,
	Pas,
	Throttle,
	Cruise,
}

export enum AssistPasVariant {
	Cadence,
	Torque,
	Variable,
}

function clearFlag(level: AssistLevel, flag: AssistFlags): void {
	level.flags = level.flags & ~flag & 0xff
}

function setFlag(level: AssistLevel, flag: AssistFlags, on: boolean): void {
	if (on) {
		level.flags = (level.flags | flag) & 0xff
	} else {
		clearFlag(level, flag)
	}
}

function clearPasVariant(level: AssistLevel): void {
	clearFlag(level, AssistFlags.PasTorque)
	clearFlag(level, AssistFlags.PasVariable)
}

// Order matters: a PAS level with throttle enabled has both the Pas and
// Throttle bits set, and must read back as Pas (throttle is then an add-on).
export function getBaseType(level: AssistLevel): AssistBaseType {
	if (level.flags & AssistFlags.Pas) return AssistBaseType.Pas
	if (level.flags & AssistFlags.Throttle) return AssistBaseType.Throttle
	if (level.flags & AssistFlags.Cruise) return AssistBaseType.Cruise
	return AssistBaseType.Disabled
}

export function setBaseType(level: AssistLevel, type: AssistBaseType): void {
	// Clear all three base-type bits, then set the chosen one.
	clearFlag(level, AssistFlags.Pas)
	clearFlag(level, AssistFlags.Throttle)
	clearFlag(level, AssistFlags.Cruise)

	switch (type) {
		case AssistBaseType.Pas:
			setFlag(level, AssistFlags.Pas, true)
			break
		case AssistBaseType.Throttle:
			setFlag(level, AssistFlags.Throttle, true)
			break
		case AssistBaseType.Cruise:
			setFlag(level, AssistFlags.Cruise, true)
			break
	}

	switch (type) {
		case AssistBaseType.Disabled:
			level.targetCurrentPercent = 0
			level.maxThrottleCurrentPercent = 0
			level.maxSpeedPercent = 0
			level.torqueAmplificationFactor = 0
			clearFlag(level, AssistFlags.Throttle)
			clearPasVariant(level)
			clearFlag(level, AssistFlags.OverrideCadence)
			clearFlag(level, AssistFlags.OverrideSpeed)
			break
		case AssistBaseType.Throttle:
			level.targetCurrentPercent = 0
			level.torqueAmplificationFactor = 0
			clearPasVariant(level)
			clearFlag(level, AssistFlags.OverrideCadence)
			clearFlag(level, AssistFlags.OverrideSpeed)
			break
		case AssistBaseType.Pas:
			level.maxThrottleCurrentPercent = 0
			clearFlag(level, AssistFlags.Throttle)
			break
	}
}

export function getPasVariant(level: AssistLevel): AssistPasVariant {
	if (level.flags & AssistFlags.PasTorque) return AssistPasVariant.Torque
	if (level.flags & AssistFlags.PasVariable) return AssistPasVariant.Variable
	return AssistPasVariant.Cadence
}

export function setPasVariant(level: AssistLevel, variant: AssistPasVariant): void {
	clearPasVariant(level)

	switch (variant) {
		case AssistPasVariant.Torque:
			setFlag(level, AssistFlags.PasTorque, true)
			break
		case AssistPasVariant.Variable:
			setFlag(level, AssistFlags.PasVariable, true)
			break
	}

	switch (variant) {
		case AssistPasVariant.Variable:
			level.torqueAmplificationFactor = 0
			clearFlag(level, AssistFlags.Throttle)
			clearFlag(level, AssistFlags.OverrideCadence)
			clearFlag(level, AssistFlags.OverrideSpeed)
			level.maxThrottleCurrentPercent = 0
			break
		case AssistPasVariant.Cadence:
			level.torqueAmplificationFactor = 0
			break
	}
}

export function getThrottleEnabled(level: AssistLevel): boolean {
	return (level.flags & AssistFlags.Throttle) !== 0
}

export function setThrottleEnabled(level: AssistLevel, enabled: boolean): void {
	setFlag(level, AssistFlags.Throttle, enabled)
}

export function getCadenceOverride(level: AssistLevel): boolean {
	return (level.flags & AssistFlags.OverrideCadence) !== 0
}

export function setCadenceOverride(level: AssistLevel, enabled: boolean): void {
	setFlag(level, AssistFlags.OverrideCadence, enabled)
}

export function getSpeedOverride(level: AssistLevel): boolean {
	return (level.flags & AssistFlags.OverrideSpeed) !== 0
}

export function setSpeedOverride(level: AssistLevel, enabled: boolean): void {
	setFlag(level, AssistFlags.OverrideSpeed, enabled)
}

export function baseTypeLabel(type: AssistBaseType): string {
	switch (type) {
		case AssistBaseType.Disabled:
			return 'Motor Disabled'
		case AssistBaseType.Pas:
			return 'PAS'
		case AssistBaseType.Throttle:
			return 'Throttle'
		case AssistBaseType.Cruise:
			return 'Cruise'
	}
}
