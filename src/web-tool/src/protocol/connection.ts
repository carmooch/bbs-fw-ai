// TypeScript port of src/tool/Model/BbsfwConnection.cs over the Web Serial
// API instead of System.IO.Ports.SerialPort. Same wire protocol: a 1200 baud
// serial link, byte-additive checksums, opcode-tagged request/response
// messages framed by leading request/response-type + opcode bytes.

import { computeChecksum } from './checksum'
import {
	CONFIG_BYTE_SIZE,
	CONFIG_VERSION,
	Controller,
	parseConfiguration,
	writeConfiguration,
} from './configuration'
import type { Configuration } from './configuration'

const REQUEST_TYPE_READ = 0x01
const REQUEST_TYPE_WRITE = 0x02

const RESPONSE_TYPE_READ = 0x01
const RESPONSE_TYPE_WRITE = 0x02

const EVENT_LOG_ENTRY = 0xee
const EVENT_LOG_DATA_ENTRY = 0xed

const OPCODE_READ_FW_VERSION = 0x01
const OPCODE_READ_EVTLOG_ENABLE = 0x02
const OPCODE_READ_CONFIG = 0x03

const OPCODE_WRITE_EVTLOG_ENABLE = 0xf0
const OPCODE_WRITE_CONFIG = 0xf1
const OPCODE_WRITE_RESET_CONFIG = 0xf2
const OPCODE_WRITE_ADC_VOLTAGE_CALIBRATION = 0xf3

// Sentinel return values for the message handlers below, matching
// BbsfwConnection.cs: 0 means "not enough bytes yet, keep waiting", a
// negative value means "unrecognized/corrupt, discard the whole buffer",
// and a positive value is the number of bytes the message consumed.
const KEEP = 0
const DISCARD = -1

export interface ConnectionInfo {
	controller: Controller
	firmwareVersion: string
	configVersion: number
}

export interface RequestResult<T> {
	timedOut: boolean
	result: T | null
}

// Mirrors CompletionQueue<T> in CompletionQueue.cs: a single pending
// request/response slot per request kind, not an actual multi-item queue --
// the UI only ever has one outstanding read or write at a time.
class CompletionSlot<T> {
	private resolve: ((value: RequestResult<T>) => void) | null = null
	private timer: ReturnType<typeof setTimeout> | null = null

	waitResponse(timeoutMs: number): Promise<RequestResult<T>> {
		this.clearTimer()
		return new Promise((resolve) => {
			this.resolve = resolve
			this.timer = setTimeout(() => {
				this.resolve = null
				resolve({ timedOut: true, result: null })
			}, timeoutMs)
		})
	}

	complete(value: T): void {
		this.clearTimer()
		const resolve = this.resolve
		this.resolve = null
		resolve?.({ timedOut: false, result: value })
	}

	private clearTimer(): void {
		if (this.timer !== null) {
			clearTimeout(this.timer)
			this.timer = null
		}
	}
}

export class BbsfwConnection {
	private port: SerialPort | null = null
	private writer: WritableStreamDefaultWriter<Uint8Array> | null = null
	private reader: ReadableStreamDefaultReader<Uint8Array> | null = null
	private readLoopDone: Promise<void> | null = null

	private rxBuffer: number[] = []
	private lastRecvMs = 0

	private connecting = false
	private connected = false
	private controllerType: Controller = Controller.Unknown
	private firmwareVersion = ''
	private configVersion = 0
	private pendingConnect: ((info: ConnectionInfo | null) => void) | null = null

	private readonly readConfigSlot = new CompletionSlot<Configuration>()
	private readonly writeConfigSlot = new CompletionSlot<boolean>()
	private readonly resetConfigSlot = new CompletionSlot<boolean>()
	private readonly voltageCalibrationSlot = new CompletionSlot<boolean>()

	onConnected: ((info: ConnectionInfo) => void) | null = null
	onDisconnected: (() => void) | null = null

	get isConnected(): boolean {
		return this.connected
	}

	get controller(): Controller {
		return this.controllerType
	}

	/** Must be called from a user gesture (e.g. a click handler) -- the browser requires it to show the port picker. */
	async connect(timeoutMs: number): Promise<ConnectionInfo> {
		const port = await navigator.serial.requestPort()
		await port.open({ baudRate: 1200 })

		this.port = port
		this.writer = port.writable!.getWriter()
		this.reader = port.readable!.getReader()
		this.rxBuffer = []
		this.connecting = true
		this.connected = false
		this.controllerType = Controller.Unknown

		this.readLoopDone = this.runReadLoop()

		const info = await this.performHandshake(timeoutMs)
		if (info === null) {
			await this.close()
			throw new Error('Timed out waiting for a response from the controller.')
		}

		return info
	}

	async close(): Promise<void> {
		this.connecting = false
		this.connected = false
		this.pendingConnect = null

		if (this.reader) {
			await this.reader.cancel().catch(() => {})
		}
		if (this.readLoopDone) {
			await this.readLoopDone.catch(() => {})
		}
		if (this.writer) {
			await this.writer.close().catch(() => {})
			this.writer = null
		}
		if (this.port) {
			await this.port.close().catch(() => {})
			this.port = null
		}

		this.rxBuffer = []
		this.onDisconnected?.()
	}

	async readConfiguration(timeoutMs: number): Promise<RequestResult<Configuration>> {
		this.sendReadRequest(OPCODE_READ_CONFIG)
		return this.readConfigSlot.waitResponse(timeoutMs)
	}

	async writeConfiguration(config: Configuration, timeoutMs: number): Promise<RequestResult<boolean>> {
		if (this.configVersion !== CONFIG_VERSION) {
			throw new Error('Unsupported config version.')
		}
		this.sendWriteConfigRequest(config)
		return this.writeConfigSlot.waitResponse(timeoutMs)
	}

	async resetConfiguration(timeoutMs: number): Promise<RequestResult<boolean>> {
		this.send([REQUEST_TYPE_WRITE, OPCODE_WRITE_RESET_CONFIG])
		return this.resetConfigSlot.waitResponse(timeoutMs)
	}

	async calibrateBatteryVoltage(volts: number, timeoutMs: number): Promise<RequestResult<boolean>> {
		const voltsX100 = Math.round(volts * 100)
		this.send([REQUEST_TYPE_WRITE, OPCODE_WRITE_ADC_VOLTAGE_CALIBRATION, (voltsX100 >> 8) & 0xff, voltsX100 & 0xff])
		return this.voltageCalibrationSlot.waitResponse(timeoutMs)
	}

	private performHandshake(timeoutMs: number): Promise<ConnectionInfo | null> {
		return new Promise((resolve) => {
			const start = Date.now()
			this.pendingConnect = resolve

			const tryOnce = () => {
				if (this.pendingConnect !== resolve) {
					return // already resolved (connected, or a newer attempt took over)
				}
				if (Date.now() - start > timeoutMs) {
					this.pendingConnect = null
					resolve(null)
					return
				}
				this.sendReadRequest(OPCODE_READ_FW_VERSION)
				setTimeout(tryOnce, 200)
			}
			tryOnce()
		})
	}

	private async runReadLoop(): Promise<void> {
		const reader = this.reader!
		try {
			while (true) {
				const { value, done } = await reader.read()
				if (done) {
					break
				}
				if (value && value.length > 0) {
					this.onBytesReceived(value)
				}
			}
		} catch {
			// port unplugged or cancelled -- close() handles teardown
		} finally {
			reader.releaseLock()
		}
	}

	private onBytesReceived(bytes: Uint8Array): void {
		const now = Date.now()
		if (this.rxBuffer.length > 0 && now - this.lastRecvMs > 1000) {
			this.rxBuffer = []
		}
		this.lastRecvMs = now

		for (const b of bytes) {
			this.rxBuffer.push(b)
		}

		this.processInputBuffer()
	}

	private processInputBuffer(): void {
		while (true) {
			const result = this.processMessage()
			if (result === DISCARD) {
				this.rxBuffer = []
			} else if (result > 0) {
				this.rxBuffer = this.rxBuffer.slice(result)
			} else {
				break // KEEP -- not enough bytes yet
			}
		}
	}

	private processMessage(): number {
		if (this.rxBuffer.length < 1) {
			return KEEP
		}

		switch (this.rxBuffer[0]) {
			case RESPONSE_TYPE_READ:
				return this.processReadResponse()
			case RESPONSE_TYPE_WRITE:
				return this.processWriteResponse()
			case EVENT_LOG_ENTRY:
			case EVENT_LOG_DATA_ENTRY:
				return this.processEventLogEntry()
		}

		return DISCARD
	}

	private processReadResponse(): number {
		if (this.rxBuffer.length < 2) {
			return KEEP
		}

		switch (this.rxBuffer[1]) {
			case OPCODE_READ_FW_VERSION:
				return this.processReadResponseFwVersion()
			case OPCODE_READ_EVTLOG_ENABLE:
				return this.processReadResponseEvtlogEnable()
			case OPCODE_READ_CONFIG:
				return this.processReadResponseConfig()
		}

		return DISCARD
	}

	private processReadResponseFwVersion(): number {
		const MessageSizeV1 = 7
		const MessageSizeV2 = 8

		if (this.rxBuffer.length < MessageSizeV1) {
			return KEEP
		}

		let size = MessageSizeV1

		const major = this.rxBuffer[2]
		const minor = this.rxBuffer[3]
		const patch = this.rxBuffer[4]

		if (major > 1 || minor > 0) {
			// Controller model field added in firmware version 1.1.
			if (this.rxBuffer.length < MessageSizeV2) {
				return KEEP
			}
			size = MessageSizeV2
		}

		if (computeChecksum(this.rxBuffer, size - 1) === this.rxBuffer[size - 1]) {
			this.configVersion = this.rxBuffer[5]

			if (this.connecting) {
				this.connecting = false
				this.connected = true
				this.controllerType = size === MessageSizeV1 ? Controller.BBSHD : (this.rxBuffer[6] as Controller)
				this.firmwareVersion = `${major}.${minor}.${patch}`

				const info: ConnectionInfo = {
					controller: this.controllerType,
					firmwareVersion: this.firmwareVersion,
					configVersion: this.configVersion,
				}

				const resolvePending = this.pendingConnect
				this.pendingConnect = null
				resolvePending?.(info)
				this.onConnected?.(info)

				this.sendEventLogEnableRequest(true)
			}
		}

		return size
	}

	private processReadResponseEvtlogEnable(): number {
		const MessageSize = 4
		if (this.rxBuffer.length < MessageSize) {
			return KEEP
		}
		return MessageSize
	}

	private processReadResponseConfig(): number {
		if (this.rxBuffer.length <= 3) {
			return KEEP
		}

		const version = this.rxBuffer[2]
		const size = this.rxBuffer[3]

		if (version !== CONFIG_VERSION || size !== CONFIG_BYTE_SIZE) {
			console.warn('Config read from flash is of an unsupported version or is corrupt, discarding.')
			return DISCARD
		}

		const messageSize = 4 + size + 1
		if (this.rxBuffer.length < messageSize) {
			return KEEP
		}

		if (computeChecksum(this.rxBuffer, messageSize - 1) === this.rxBuffer[messageSize - 1]) {
			const configBytes = Uint8Array.from(this.rxBuffer.slice(4, 4 + size))
			this.readConfigSlot.complete(parseConfiguration(configBytes))
		} else {
			console.warn('Config read from flash has mismatching checksum, discarding.')
		}

		return messageSize
	}

	private processWriteResponse(): number {
		if (this.rxBuffer.length < 2) {
			return KEEP
		}

		switch (this.rxBuffer[1]) {
			case OPCODE_WRITE_EVTLOG_ENABLE:
				return this.fixedSizeAck(4, null)
			case OPCODE_WRITE_CONFIG:
				return this.fixedSizeAck(4, this.writeConfigSlot)
			case OPCODE_WRITE_RESET_CONFIG:
				return this.fixedSizeAck(4, this.resetConfigSlot)
			case OPCODE_WRITE_ADC_VOLTAGE_CALIBRATION:
				return this.fixedSizeAck(5, this.voltageCalibrationSlot, true)
		}

		return DISCARD
	}

	private fixedSizeAck(messageSize: number, slot: CompletionSlot<boolean> | null, alwaysTrue = false): number {
		if (this.rxBuffer.length < messageSize) {
			return KEEP
		}
		slot?.complete(alwaysTrue || this.rxBuffer[2] !== 0)
		return messageSize
	}

	private processEventLogEntry(): number {
		if (this.rxBuffer[0] === EVENT_LOG_ENTRY) {
			const MessageSize = 3
			if (this.rxBuffer.length < MessageSize) {
				return KEEP
			}
			if (computeChecksum(this.rxBuffer, MessageSize - 1) !== this.rxBuffer[MessageSize - 1]) {
				return DISCARD
			}
			// Event log entries aren't decoded yet -- see ROADMAP.md Phase 1.5 scope.
			return MessageSize
		} else {
			const MessageSize = 5
			if (this.rxBuffer.length < MessageSize) {
				return KEEP
			}
			if (computeChecksum(this.rxBuffer, MessageSize - 1) !== this.rxBuffer[MessageSize - 1]) {
				return DISCARD
			}
			return MessageSize
		}
	}

	private sendReadRequest(opcode: number): void {
		this.send([REQUEST_TYPE_READ, opcode])
	}

	private sendEventLogEnableRequest(enable: boolean): void {
		this.send([REQUEST_TYPE_WRITE, OPCODE_WRITE_EVTLOG_ENABLE, enable ? 1 : 0])
	}

	private sendWriteConfigRequest(config: Configuration): void {
		const configBytes = writeConfiguration(config)
		this.send([REQUEST_TYPE_WRITE, OPCODE_WRITE_CONFIG, CONFIG_VERSION, configBytes.length, ...configBytes])
	}

	/** Appends the checksum byte and writes the message; do not include it in `bytes`. */
	private send(bytes: number[]): void {
		const checksum = computeChecksum(bytes, bytes.length)
		const message = Uint8Array.from([...bytes, checksum])
		this.writer?.write(message).catch(() => {})
	}
}
