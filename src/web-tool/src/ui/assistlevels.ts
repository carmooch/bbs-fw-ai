// Master-detail assist-level editor, ported from the WPF AssistLevels views.
// Left: mode toggle (Standard/Sport) + a list of the 10 levels. Right: an
// editor for the selected level whose fields depend on its base type, mirroring
// AssistLevelPasView / AssistLevelThrottleView / AssistLevelCruiseView. All the
// flag/coherence logic lives in protocol/assistlevel.ts; this module is just
// rendering + wiring.

import { AssistModeSelect, Controller } from '../protocol/configuration'
import type { AssistLevel, Configuration } from '../protocol/configuration'
import {
	AssistBaseType,
	AssistPasVariant,
	baseTypeLabel,
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
} from '../protocol/assistlevel'

type Mode = 'standard' | 'sport'

export interface AssistLevelsEditor {
	render(): void
}

interface Option<T> {
	value: T
	label: string
}

const ASSIST_MODE_OPTIONS: Option<AssistModeSelect>[] = [
	{ value: AssistModeSelect.Off, label: 'Off' },
	{ value: AssistModeSelect.Standard, label: 'Sport Button' },
	{ value: AssistModeSelect.Lights, label: 'Lights Button' },
	{ value: AssistModeSelect.BrakesOnBoot, label: 'Brakes @ Power On' },
	{ value: AssistModeSelect.Pas0Light, label: 'PAS 0 + Lights Button' },
	{ value: AssistModeSelect.Pas1Light, label: 'PAS 1 + Lights Button' },
	{ value: AssistModeSelect.Pas2Light, label: 'PAS 2 + Lights Button' },
	{ value: AssistModeSelect.Pas3Light, label: 'PAS 3 + Lights Button' },
	{ value: AssistModeSelect.Pas4Light, label: 'PAS 4 + Lights Button' },
	{ value: AssistModeSelect.Pas5Light, label: 'PAS 5 + Lights Button' },
	{ value: AssistModeSelect.Pas6Light, label: 'PAS 6 + Lights Button' },
	{ value: AssistModeSelect.Pas7Light, label: 'PAS 7 + Lights Button' },
	{ value: AssistModeSelect.Pas8Light, label: 'PAS 8 + Lights Button' },
	{ value: AssistModeSelect.Pas9Light, label: 'PAS 9 + Lights Button' },
]

const BASE_TYPE_OPTIONS: Option<AssistBaseType>[] = [
	{ value: AssistBaseType.Disabled, label: 'Motor Disabled' },
	{ value: AssistBaseType.Pas, label: 'PAS' },
	{ value: AssistBaseType.Throttle, label: 'Throttle' },
	{ value: AssistBaseType.Cruise, label: 'Cruise' },
]

function div(className: string, children: (Node | string)[] = []): HTMLDivElement {
	const el = document.createElement('div')
	el.className = className
	el.append(...children)
	return el
}

function labeledRow(labelText: string, control: HTMLElement): HTMLDivElement {
	const label = document.createElement('label')
	label.textContent = labelText
	return div('assist-row', [label, control])
}

function numberRow(
	labelText: string,
	value: number,
	onInput: (v: number) => void,
	opts: { step?: string; disabled?: boolean } = {},
): HTMLDivElement {
	const input = document.createElement('input')
	input.type = 'number'
	input.value = String(value)
	if (opts.step) input.step = opts.step
	if (opts.disabled) input.disabled = true
	input.addEventListener('input', () => {
		const v = Number(input.value)
		if (!Number.isNaN(v)) {
			onInput(v)
		}
	})
	return labeledRow(labelText, input)
}

function selectRow<T extends number>(
	labelText: string,
	options: Option<T>[],
	selected: T,
	onChange: (v: T) => void,
): HTMLDivElement {
	const select = document.createElement('select')
	for (const o of options) {
		const opt = document.createElement('option')
		opt.value = String(o.value)
		opt.textContent = o.label
		if (o.value === selected) {
			opt.selected = true
		}
		select.append(opt)
	}
	select.addEventListener('change', () => onChange(Number(select.value) as T))
	return labeledRow(labelText, select)
}

function checkboxRow(
	labelText: string,
	checked: boolean,
	onChange: (v: boolean) => void,
	disabled = false,
): HTMLDivElement {
	const input = document.createElement('input')
	input.type = 'checkbox'
	input.checked = checked
	input.disabled = disabled
	input.addEventListener('change', () => onChange(input.checked))
	return labeledRow(labelText, input)
}

export function createAssistLevelsEditor(
	container: HTMLElement,
	getConfig: () => Configuration,
	getController: () => Controller,
): AssistLevelsEditor {
	let mode: Mode = 'standard'
	let selectedIndex = 0

	function levelsFor(cfg: Configuration): AssistLevel[] {
		return mode === 'standard' ? cfg.standardAssistLevels : cfg.sportAssistLevels
	}

	function renderLevelList(cfg: Configuration): HTMLElement {
		const list = div('assist-level-list')
		levelsFor(cfg).forEach((level, i) => {
			const row = document.createElement('button')
			row.type = 'button'
			row.className = 'assist-level-item' + (i === selectedIndex ? ' selected' : '')
			row.append(
				div('assist-level-item-title', [`Level ${i}`]),
				div('assist-level-item-type', [baseTypeLabel(getBaseType(level))]),
			)
			row.addEventListener('click', () => {
				selectedIndex = i
				render()
			})
			list.append(row)
		})
		return list
	}

	function renderPasEditor(level: AssistLevel): HTMLElement[] {
		const rows: HTMLElement[] = []
		const variant = getPasVariant(level)

		const variantOptions: Option<AssistPasVariant>[] = [{ value: AssistPasVariant.Cadence, label: 'Cadence' }]
		if (getController() === Controller.TSDZ2) {
			variantOptions.push({ value: AssistPasVariant.Torque, label: 'Torque' })
		}
		variantOptions.push({ value: AssistPasVariant.Variable, label: 'Variable' })

		rows.push(
			selectRow('Variant', variantOptions, variant, (v) => {
				setPasVariant(level, v)
				render()
			}),
		)

		if (variant === AssistPasVariant.Torque) {
			rows.push(
				numberRow('Torque Amplification', level.torqueAmplificationFactor, (v) => (level.torqueAmplificationFactor = v), {
					step: '0.1',
				}),
			)
		}

		rows.push(numberRow('Max Current (%)', level.targetCurrentPercent, (v) => (level.targetCurrentPercent = v)))
		rows.push(numberRow('Max Cadence (%)', level.maxCadencePercent, (v) => (level.maxCadencePercent = v)))
		rows.push(numberRow('Max Speed (%)', level.maxSpeedPercent, (v) => (level.maxSpeedPercent = v)))

		// Throttle add-on options only apply to the Cadence/Torque variants.
		if (variant !== AssistPasVariant.Variable) {
			const throttleEnabled = getThrottleEnabled(level)
			rows.push(
				checkboxRow('Enable Throttle', throttleEnabled, (v) => {
					setThrottleEnabled(level, v)
					render()
				}),
			)
			rows.push(
				checkboxRow(
					'Throttle Cadence Override',
					getCadenceOverride(level),
					(v) => setCadenceOverride(level, v),
					!throttleEnabled,
				),
			)
			rows.push(
				checkboxRow(
					'Throttle Speed Override',
					getSpeedOverride(level),
					(v) => setSpeedOverride(level, v),
					!throttleEnabled,
				),
			)
			rows.push(
				numberRow(
					'Max Throttle Current (%)',
					level.maxThrottleCurrentPercent,
					(v) => (level.maxThrottleCurrentPercent = v),
					{ disabled: !throttleEnabled },
				),
			)
		}

		return rows
	}

	function renderLevelEditor(level: AssistLevel): HTMLElement {
		const editor = div('assist-level-editor')
		editor.append(div('assist-level-editor-title', [`Level ${selectedIndex}`]))

		editor.append(
			selectRow('Type', BASE_TYPE_OPTIONS, getBaseType(level), (v) => {
				setBaseType(level, v)
				render()
			}),
		)

		switch (getBaseType(level)) {
			case AssistBaseType.Pas:
				editor.append(...renderPasEditor(level))
				break
			case AssistBaseType.Throttle:
				editor.append(
					numberRow('Max Current (%)', level.maxThrottleCurrentPercent, (v) => (level.maxThrottleCurrentPercent = v)),
					numberRow('Max Cadence (%)', level.maxCadencePercent, (v) => (level.maxCadencePercent = v)),
					numberRow('Max Speed (%)', level.maxSpeedPercent, (v) => (level.maxSpeedPercent = v)),
				)
				break
			case AssistBaseType.Cruise:
				editor.append(
					numberRow('Max Current (%)', level.targetCurrentPercent, (v) => (level.targetCurrentPercent = v)),
					numberRow('Max Cadence (%)', level.maxCadencePercent, (v) => (level.maxCadencePercent = v)),
					numberRow('Max Speed (%)', level.maxSpeedPercent, (v) => (level.maxSpeedPercent = v)),
				)
				break
		}

		return editor
	}

	function render(): void {
		const cfg = getConfig()
		selectedIndex = Math.min(Math.max(selectedIndex, 0), levelsFor(cfg).length - 1)
		const level = levelsFor(cfg)[selectedIndex]

		const modeToggle = selectRow<number>(
			'Operation Mode Page',
			[
				{ value: 0, label: 'Standard' },
				{ value: 1, label: 'Sport' },
			],
			mode === 'standard' ? 0 : 1,
			(v) => {
				mode = v === 0 ? 'standard' : 'sport'
				render()
			},
		)

		const left = div('assist-left', [modeToggle, renderLevelList(cfg)])
		const right = renderLevelEditor(level)

		const startupOptions: Option<number>[] = Array.from({ length: 10 }, (_, i) => ({ value: i, label: String(i) }))
		const footer = div('assist-footer', [
			selectRow('Operation Mode Toggle', ASSIST_MODE_OPTIONS, cfg.assistModeSelection, (v) => {
				cfg.assistModeSelection = v
			}),
			selectRow('Startup Assist Level', startupOptions, cfg.assistStartupLevel, (v) => {
				cfg.assistStartupLevel = v
			}),
		])

		container.replaceChildren(div('assist-grid', [left, right]), footer)
	}

	return { render }
}
