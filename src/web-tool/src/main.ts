import './style.css'
import { BbsfwConnection, type ConnectionInfo } from './protocol/connection'
import {
	createDefaultConfiguration,
	validateConfiguration,
	type Configuration,
} from './protocol/configuration'
import { bindCheckbox, bindNumber, bindSelect, type Binding } from './ui/binding'
import { createAssistLevelsEditor } from './ui/assistlevels'

const TIMEOUT_MS = 5000

const connectBtn = document.querySelector<HTMLButtonElement>('#connect-btn')!
const connectionStatus = document.querySelector<HTMLSpanElement>('#connection-status')!
const notSupported = document.querySelector<HTMLParagraphElement>('#not-supported')!
const configForm = document.querySelector<HTMLElement>('#config-form')!
const writeBtn = document.querySelector<HTMLButtonElement>('#write-btn')!
const resetBtn = document.querySelector<HTMLButtonElement>('#reset-btn')!
const writeStatus = document.querySelector<HTMLSpanElement>('#write-status')!
const validationErrorsEl = document.querySelector<HTMLUListElement>('#validation-errors')!

const connection = new BbsfwConnection()
let config: Configuration = createDefaultConfiguration()

const assistEditor = createAssistLevelsEditor(
	document.querySelector<HTMLDivElement>('#assist-levels')!,
	() => config,
	() => connection.controller,
)

// matches ConfigurationViewModel.cs's KphToMph/MphToKph exactly
function kphToMph(kph: number): number {
	return Math.round(kph * 0.621371192)
}
function mphToKph(mph: number): number {
	return Math.round(mph * 1.609344)
}

function bindSpeedKph(input: HTMLInputElement, get: () => number): Binding {
	return {
		refresh: () => {
			input.value = String(config.useFreedomUnits ? kphToMph(get()) : get())
		},
	}
}

function speedInputHandler(input: HTMLInputElement, set: (v: number) => void): void {
	input.addEventListener('input', () => {
		const v = Number(input.value)
		if (Number.isNaN(v)) {
			return
		}
		set(config.useFreedomUnits ? mphToKph(v) : v)
	})
}

// Coordinates the wheel-size preset dropdown with the free-entry number input:
// the number input is the source of truth (config.wheelSizeInch), the dropdown
// is a convenience that fills it. Selecting a preset sets the number; typing a
// value that matches no preset shows "custom".
function bindWheelSize(preset: HTMLSelectElement, input: HTMLInputElement): Binding {
	const presetValues = Array.from(preset.options)
		.map((o) => o.value)
		.filter((v) => v !== 'custom')

	const syncPresetToValue = () => {
		const match = presetValues.find((v) => Number(v) === config.wheelSizeInch)
		preset.value = match ?? 'custom'
	}

	preset.addEventListener('change', () => {
		if (preset.value === 'custom') {
			return
		}
		config.wheelSizeInch = Number(preset.value)
		input.value = String(config.wheelSizeInch)
	})

	input.addEventListener('input', () => {
		const v = Number(input.value)
		if (Number.isNaN(v)) {
			return
		}
		config.wheelSizeInch = v
		syncPresetToValue()
	})

	return {
		refresh: () => {
			input.value = String(config.wheelSizeInch)
			syncPresetToValue()
		},
	}
}

const bindings: Binding[] = []
function bind(b: Binding): void {
	bindings.push(b)
}

function el<T extends HTMLElement>(id: string): T {
	return document.querySelector<T>(`#${id}`)!
}

function setupBindings(): void {
	bind(bindCheckbox(el('useFreedomUnits'), () => config.useFreedomUnits, (v) => {
		config.useFreedomUnits = v
		updateUnitLabels()
		bindings.forEach((b) => b.refresh())
	}))

	bind(bindNumber(el('maxCurrentAmps'), () => config.maxCurrentAmps, (v) => (config.maxCurrentAmps = v)))
	bind(bindNumber(el('currentRampAmpsSecond'), () => config.currentRampAmpsSecond, (v) => (config.currentRampAmpsSecond = v)))
	bind(bindNumber(el('maxBatteryVolts'), () => config.maxBatteryVolts, (v) => (config.maxBatteryVolts = v)))
	bind(bindNumber(el('lowCutoffVolts'), () => config.lowCutoffVolts, (v) => (config.lowCutoffVolts = v)))

	const maxSpeedInput = el<HTMLInputElement>('maxSpeedKph')
	speedInputHandler(maxSpeedInput, (v) => (config.maxSpeedKph = v))
	bind(bindSpeedKph(maxSpeedInput, () => config.maxSpeedKph))

	bind(bindNumber(el('throttleStartMillivolts'), () => config.throttleStartMillivolts, (v) => (config.throttleStartMillivolts = v)))
	bind(bindNumber(el('throttleEndMillivolts'), () => config.throttleEndMillivolts, (v) => (config.throttleEndMillivolts = v)))
	bind(bindNumber(el('throttleStartPercent'), () => config.throttleStartPercent, (v) => (config.throttleStartPercent = v)))
	bind(bindSelect(el('throttleGlobalSpeedLimit'), () => config.throttleGlobalSpeedLimit, (v) => (config.throttleGlobalSpeedLimit = v)))
	bind(bindNumber(el('throttleGlobalSpeedLimitPercent'), () => config.throttleGlobalSpeedLimitPercent, (v) => (config.throttleGlobalSpeedLimitPercent = v)))

	bind(bindNumber(el('pasStartDelayPulses'), () => config.pasStartDelayPulses, (v) => (config.pasStartDelayPulses = v)))
	bind(bindNumber(el('pasStopDelayMilliseconds'), () => config.pasStopDelayMilliseconds, (v) => (config.pasStopDelayMilliseconds = v)))
	bind(bindNumber(el('pasKeepCurrentPercent'), () => config.pasKeepCurrentPercent, (v) => (config.pasKeepCurrentPercent = v)))
	bind(bindNumber(el('pasKeepCurrentCadenceRpm'), () => config.pasKeepCurrentCadenceRpm, (v) => (config.pasKeepCurrentCadenceRpm = v)))

	bind(bindCheckbox(el('useSpeedSensor'), () => config.useSpeedSensor, (v) => (config.useSpeedSensor = v)))
	bind(bindCheckbox(el('useShiftSensor'), () => config.useShiftSensor, (v) => (config.useShiftSensor = v)))
	bind(bindCheckbox(el('usePushWalk'), () => config.usePushWalk, (v) => (config.usePushWalk = v)))
	bind(bindSelect(el('useTemperatureSensor'), () => config.useTemperatureSensor, (v) => (config.useTemperatureSensor = v)))
	bind(bindSelect(el('lightsMode'), () => config.lightsMode, (v) => (config.lightsMode = v)))
	bind(bindCheckbox(el('usePretension'), () => config.usePretension, (v) => (config.usePretension = v)))

	const pretensionInput = el<HTMLInputElement>('pretensionSpeedCutoffKph')
	speedInputHandler(pretensionInput, (v) => (config.pretensionSpeedCutoffKph = v))
	bind(bindSpeedKph(pretensionInput, () => config.pretensionSpeedCutoffKph))

	bind(bindWheelSize(el('wheelSizePreset'), el('wheelSizeInch')))
	bind(bindNumber(el('numWheelSensorSignals'), () => config.numWheelSensorSignals, (v) => (config.numWheelSensorSignals = v)))

	bind(bindNumber(el('shiftInterruptDuration'), () => config.shiftInterruptDuration, (v) => (config.shiftInterruptDuration = v)))
	bind(bindNumber(el('shiftInterruptCurrentThresholdPercent'), () => config.shiftInterruptCurrentThresholdPercent, (v) => (config.shiftInterruptCurrentThresholdPercent = v)))

	bind(bindSelect(el('walkModeDataDisplay'), () => config.walkModeDataDisplay, (v) => (config.walkModeDataDisplay = v)))
	bind(bindSelect(el('displayRangeField'), () => config.displayRangeField, (v) => (config.displayRangeField = v)))
	bind(bindNumber(el('maxCadenceRpm'), () => config.maxCadenceRpm, (v) => (config.maxCadenceRpm = v)))
}

function updateUnitLabels(): void {
	const unit = config.useFreedomUnits ? 'mph' : 'km/h'
	document.querySelectorAll<HTMLLabelElement>('[data-unit-label="speed"]').forEach((label) => {
		label.textContent = label.textContent!.replace(/\(km\/h\)|\(mph\)/, `(${unit})`)
	})
}

function refreshForm(): void {
	bindings.forEach((b) => b.refresh())
	updateUnitLabels()
	assistEditor.render()
}

function setStatus(text: string): void {
	connectionStatus.textContent = text
}

async function handleConnect(): Promise<void> {
	connectBtn.disabled = true
	setStatus('Connecting...')
	try {
		const info: ConnectionInfo = await connection.connect(TIMEOUT_MS)
		setStatus(`Connected: ${controllerName(info.controller)} firmware ${info.firmwareVersion}`)
		connectBtn.textContent = 'Disconnect'
		connectBtn.disabled = false

		const result = await connection.readConfiguration(TIMEOUT_MS)
		if (result.timedOut || result.result === null) {
			setStatus(`${connectionStatus.textContent} -- failed to read configuration`)
			return
		}

		config = result.result
		configForm.hidden = false
		refreshForm()
	} catch (err) {
		setStatus(`Connection failed: ${(err as Error).message}`)
		connectBtn.disabled = false
	}
}

async function handleDisconnect(): Promise<void> {
	await connection.close()
	connectBtn.textContent = 'Connect'
	setStatus('Not connected')
	configForm.hidden = true
}

function controllerName(value: number): string {
	return ['Unknown', 'BBSHD', 'BBS02', 'TSDZ2'][value] ?? 'Unknown'
}

async function handleWrite(): Promise<void> {
	const errors = validateConfiguration(config, connection.controller)
	validationErrorsEl.innerHTML = ''
	if (errors.length > 0) {
		errors.forEach((e) => {
			const li = document.createElement('li')
			li.textContent = e
			validationErrorsEl.appendChild(li)
		})
		writeStatus.textContent = ''
		return
	}

	writeBtn.disabled = true
	writeStatus.textContent = 'Writing...'
	try {
		const result = await connection.writeConfiguration(config, TIMEOUT_MS)
		if (result.timedOut) {
			writeStatus.textContent = 'Timed out writing configuration.'
		} else if (result.result) {
			writeStatus.textContent = 'Configuration written.'
		} else {
			writeStatus.textContent = 'Controller rejected the configuration.'
		}
	} finally {
		writeBtn.disabled = false
	}
}

async function handleReset(): Promise<void> {
	resetBtn.disabled = true
	writeStatus.textContent = 'Resetting...'
	try {
		const result = await connection.resetConfiguration(TIMEOUT_MS)
		if (result.timedOut || !result.result) {
			writeStatus.textContent = 'Failed to reset configuration.'
			return
		}

		const read = await connection.readConfiguration(TIMEOUT_MS)
		if (read.timedOut || read.result === null) {
			writeStatus.textContent = 'Reset, but failed to re-read configuration.'
			return
		}

		config = read.result
		refreshForm()
		writeStatus.textContent = 'Configuration reset to factory defaults.'
	} finally {
		resetBtn.disabled = false
	}
}

function main(): void {
	if (!('serial' in navigator)) {
		notSupported.hidden = false
		connectBtn.disabled = true
		return
	}

	setupBindings()

	connectBtn.addEventListener('click', () => {
		if (connection.isConnected) {
			handleDisconnect()
		} else {
			handleConnect()
		}
	})
	writeBtn.addEventListener('click', handleWrite)
	resetBtn.addEventListener('click', handleReset)

	connection.onDisconnected = () => {
		connectBtn.textContent = 'Connect'
		setStatus('Disconnected')
		configForm.hidden = true
	}
}

main()
