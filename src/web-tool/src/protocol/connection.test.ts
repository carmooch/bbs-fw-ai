import { describe, it, expect } from 'vitest'
import { BbsfwConnection } from './connection'
import { createDefaultConfiguration, writeConfiguration } from './configuration'
import { computeChecksum } from './checksum'

// These exercise the byte-level message framing/parsing state machine
// directly -- the part with no Web Serial equivalent to test against, since
// there's no fake hardware to plug in. A real connect()/Web Serial round
// trip can only be verified against an actual controller, which is exactly
// the kind of thing this repo's README warns isn't done yet.
//
// `connection` is cast to `any` to reach the private receive pipeline and
// swap in a fake writer; this is testing implementation, not the public API,
// which is the right tradeoff here since the framing logic is the part most
// likely to have an off-by-one bug and the least likely to be caught by
// TypeScript's type checker.

function withFakeWriter(connection: BbsfwConnection) {
	const sent: number[][] = []
	;(connection as any).writer = {
		write: (bytes: Uint8Array) => {
			sent.push(Array.from(bytes))
			return Promise.resolve()
		},
	}
	return sent
}

function feed(connection: BbsfwConnection, bytes: number[]) {
	;(connection as any).onBytesReceived(Uint8Array.from(bytes))
}

describe('BbsfwConnection read config', () => {
	it('resolves with the parsed configuration on a valid response', async () => {
		const connection = new BbsfwConnection()
		withFakeWriter(connection)

		const cfg = createDefaultConfiguration()
		cfg.maxCurrentAmps = 30
		cfg.maxCadenceRpm = 168

		const pending = connection.readConfiguration(1000)

		const cfgBytes = writeConfiguration(cfg)
		const header = [0x01, 0x03, 6, cfgBytes.length] // RESPONSE_TYPE_READ, OPCODE_READ_CONFIG, version, length
		const withoutChecksum = [...header, ...cfgBytes]
		const message = [...withoutChecksum, computeChecksum(withoutChecksum, withoutChecksum.length)]

		feed(connection, message)

		const result = await pending
		expect(result.timedOut).toBe(false)
		expect(result.result).toEqual(cfg)
	})

	it('discards a response with a mismatched checksum and eventually times out', async () => {
		const connection = new BbsfwConnection()
		withFakeWriter(connection)

		const pending = connection.readConfiguration(150)

		const cfgBytes = writeConfiguration(createDefaultConfiguration())
		const header = [0x01, 0x03, 6, cfgBytes.length]
		const corruptChecksum = (computeChecksum([...header, ...cfgBytes], header.length + cfgBytes.length) + 1) & 0xff
		feed(connection, [...header, ...cfgBytes, corruptChecksum])

		const result = await pending
		expect(result.timedOut).toBe(true)
		expect(result.result).toBeNull()
	})

	it('discards a response declaring an unsupported config version', async () => {
		const connection = new BbsfwConnection()
		withFakeWriter(connection)

		const pending = connection.readConfiguration(150)

		const cfgBytes = writeConfiguration(createDefaultConfiguration())
		const header = [0x01, 0x03, 5, cfgBytes.length] // version 5 -- not the only version this fork supports (6)
		const message = [...header, ...cfgBytes, computeChecksum([...header, ...cfgBytes], header.length + cfgBytes.length)]
		feed(connection, message)

		const result = await pending
		expect(result.timedOut).toBe(true)
	})

	it('handles a response arriving split across two separate chunks', async () => {
		const connection = new BbsfwConnection()
		withFakeWriter(connection)

		const cfg = createDefaultConfiguration()
		const pending = connection.readConfiguration(1000)

		const cfgBytes = writeConfiguration(cfg)
		const header = [0x01, 0x03, 6, cfgBytes.length]
		const withoutChecksum = [...header, ...cfgBytes]
		const message = [...withoutChecksum, computeChecksum(withoutChecksum, withoutChecksum.length)]

		const splitAt = 10
		feed(connection, message.slice(0, splitAt))
		feed(connection, message.slice(splitAt))

		const result = await pending
		expect(result.timedOut).toBe(false)
		expect(result.result).toEqual(cfg)
	})
})

describe('BbsfwConnection write config', () => {
	it('sends the correctly framed request and resolves true on success', async () => {
		const connection = new BbsfwConnection()
		const sent = withFakeWriter(connection)
		;(connection as any).configVersion = 6

		const cfg = createDefaultConfiguration()
		cfg.maxCurrentAmps = 25

		const pending = connection.writeConfiguration(cfg, 1000)

		expect(sent.length).toBe(1)
		const expectedBody = [0x02, 0xf1, 6, writeConfiguration(cfg).length, ...writeConfiguration(cfg)]
		expect(sent[0]).toEqual([...expectedBody, computeChecksum(expectedBody, expectedBody.length)])

		// RESPONSE_TYPE_WRITE, OPCODE_WRITE_CONFIG, success=1
		const ackBody = [0x02, 0xf1, 1]
		feed(connection, [...ackBody, computeChecksum(ackBody, ackBody.length)])

		const result = await pending
		expect(result).toEqual({ timedOut: false, result: true })
	})

	it('rejects if the connected controller is on a different config version', async () => {
		const connection = new BbsfwConnection()
		withFakeWriter(connection)
		;(connection as any).configVersion = 5

		await expect(connection.writeConfiguration(createDefaultConfiguration(), 1000)).rejects.toThrow()
	})
})
