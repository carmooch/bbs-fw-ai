// @vitest-environment jsdom
import { describe, it, expect, beforeEach } from 'vitest'
import { createAssistLevelsEditor } from './assistlevels'
import {
	AssistFlags,
	Controller,
	createDefaultConfiguration,
	type Configuration,
} from '../protocol/configuration'

// Verifies the DOM editor's wiring against jsdom -- the part that can't be
// browser-previewed in this environment. The flag math itself is covered by
// assistlevel.test.ts; here we check that controls render, show/hide by type,
// and write back into the config object.

let container: HTMLDivElement
let config: Configuration
let controller: Controller

function makeEditor() {
	const editor = createAssistLevelsEditor(
		container,
		() => config,
		() => controller,
	)
	editor.render()
	return editor
}

// Finds the control in the `.assist-row` whose label matches exactly.
function rowControl(labelText: string): HTMLInputElement | HTMLSelectElement | null {
	const labels = Array.from(container.querySelectorAll('.assist-row label'))
	const label = labels.find((l) => l.textContent === labelText)
	if (!label) return null
	return label.nextElementSibling as HTMLInputElement | HTMLSelectElement
}

function rowLabels(): string[] {
	return Array.from(container.querySelectorAll('.assist-row label')).map((l) => l.textContent ?? '')
}

function setSelect(control: HTMLSelectElement, value: number) {
	control.value = String(value)
	control.dispatchEvent(new Event('change'))
}

function setNumber(control: HTMLInputElement, value: number) {
	control.value = String(value)
	control.dispatchEvent(new Event('input'))
}

function setCheckbox(control: HTMLInputElement, checked: boolean) {
	control.checked = checked
	control.dispatchEvent(new Event('change'))
}

beforeEach(() => {
	container = document.createElement('div')
	document.body.replaceChildren(container)
	config = createDefaultConfiguration()
	controller = Controller.BBSHD
})

describe('level list', () => {
	it('renders all 10 levels of the selected mode with their type labels', () => {
		config.standardAssistLevels[2].flags = AssistFlags.Throttle
		makeEditor()

		const items = container.querySelectorAll('.assist-level-item')
		expect(items.length).toBe(10)
		expect(items[0].querySelector('.assist-level-item-title')!.textContent).toBe('Level 0')
		expect(items[2].querySelector('.assist-level-item-type')!.textContent).toBe('Throttle')
		expect(items[0].querySelector('.assist-level-item-type')!.textContent).toBe('Motor Disabled')
	})

	it('clicking a level selects it and shows its editor', () => {
		makeEditor()
		// click() triggers a re-render that rebuilds the list, so re-query after.
		container.querySelectorAll<HTMLButtonElement>('.assist-level-item')[5].click()

		const items = container.querySelectorAll<HTMLButtonElement>('.assist-level-item')
		expect(container.querySelector('.assist-level-editor-title')!.textContent).toBe('Level 5')
		expect(items[5].classList.contains('selected')).toBe(true)
	})
})

describe('type switching', () => {
	it('selecting PAS shows PAS fields and updates the flags', () => {
		makeEditor()
		setSelect(rowControl('Type') as HTMLSelectElement, 1 /* Pas */)

		expect(config.standardAssistLevels[0].flags & AssistFlags.Pas).toBe(AssistFlags.Pas)
		expect(rowLabels()).toContain('Variant')
		expect(rowLabels()).toContain('Max Cadence (%)')
	})

	it('PAS Variable hides the throttle add-on rows', () => {
		makeEditor()
		setSelect(rowControl('Type') as HTMLSelectElement, 1 /* Pas */)
		// variant select: Cadence=0, Variable=2 (no Torque option on BBSHD)
		setSelect(rowControl('Variant') as HTMLSelectElement, 2 /* Variable */)

		expect(rowLabels()).not.toContain('Enable Throttle')
		expect(rowLabels()).not.toContain('Max Throttle Current (%)')
	})

	it('enabling throttle in a PAS level un-disables the override + max-throttle rows', () => {
		makeEditor()
		setSelect(rowControl('Type') as HTMLSelectElement, 1 /* Pas */)

		const maxThrottle = rowControl('Max Throttle Current (%)') as HTMLInputElement
		expect(maxThrottle.disabled).toBe(true)

		setCheckbox(rowControl('Enable Throttle') as HTMLInputElement, true)

		expect(config.standardAssistLevels[0].flags & AssistFlags.Throttle).toBe(AssistFlags.Throttle)
		expect((rowControl('Max Throttle Current (%)') as HTMLInputElement).disabled).toBe(false)
		expect((rowControl('Throttle Cadence Override') as HTMLInputElement).disabled).toBe(false)
	})
})

describe('torque variant gating by controller', () => {
	it('offers Torque only on TSDZ2', () => {
		controller = Controller.TSDZ2
		makeEditor()
		setSelect(rowControl('Type') as HTMLSelectElement, 1 /* Pas */)
		const variant = rowControl('Variant') as HTMLSelectElement
		const labels = Array.from(variant.options).map((o) => o.textContent)
		expect(labels).toEqual(['Cadence', 'Torque', 'Variable'])
	})

	it('omits Torque on BBSHD', () => {
		controller = Controller.BBSHD
		makeEditor()
		setSelect(rowControl('Type') as HTMLSelectElement, 1 /* Pas */)
		const variant = rowControl('Variant') as HTMLSelectElement
		const labels = Array.from(variant.options).map((o) => o.textContent)
		expect(labels).toEqual(['Cadence', 'Variable'])
	})
})

describe('value write-back', () => {
	it('editing a numeric field updates the underlying level object', () => {
		makeEditor()
		setSelect(rowControl('Type') as HTMLSelectElement, 3 /* Cruise */)
		setNumber(rowControl('Max Current (%)') as HTMLInputElement, 42)
		expect(config.standardAssistLevels[0].targetCurrentPercent).toBe(42)
	})

	it('the mode toggle switches which array is edited', () => {
		config.sportAssistLevels[0].flags = AssistFlags.Cruise
		makeEditor()

		// Operation Mode Page select: Standard=0, Sport=1
		setSelect(rowControl('Operation Mode Page') as HTMLSelectElement, 1)

		expect(container.querySelector('.assist-level-item.selected .assist-level-item-type')!.textContent).toBe('Cruise')
	})

	it('startup level and operation-mode-toggle write to the global config', () => {
		makeEditor()
		setSelect(rowControl('Startup Assist Level') as HTMLSelectElement, 4)
		expect(config.assistStartupLevel).toBe(4)
	})
})
