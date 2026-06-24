// Minimal two-way binding: each bind*() call wires the input's change event
// to push into the model, and returns a refresh() that pulls from the model
// back into the input -- used after reading a config from the controller.
// Deliberately not a framework; the form is static and flat enough that
// this is less code than adopting one.

export interface Binding {
	refresh(): void
}

export function bindNumber(input: HTMLInputElement, get: () => number, set: (v: number) => void): Binding {
	input.addEventListener('input', () => {
		const v = Number(input.value)
		if (!Number.isNaN(v)) {
			set(v)
		}
	})
	return { refresh: () => (input.value = String(get())) }
}

export function bindCheckbox(input: HTMLInputElement, get: () => boolean, set: (v: boolean) => void): Binding {
	input.addEventListener('change', () => set(input.checked))
	return { refresh: () => (input.checked = get()) }
}

export function bindSelect(select: HTMLSelectElement, get: () => number, set: (v: number) => void): Binding {
	select.addEventListener('change', () => set(Number(select.value)))
	return { refresh: () => (select.value = String(get())) }
}
