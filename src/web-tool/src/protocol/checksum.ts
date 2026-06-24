// Matches compute_checksum() in src/firmware/util.h and ComputeChecksum() in
// src/tool/Model/BbsfwConnection.cs: an 8-bit additive sum with wraparound.
export function computeChecksum(bytes: ArrayLike<number>, length: number = bytes.length): number {
	let result = 0
	for (let i = 0; i < length; ++i) {
		result = (result + bytes[i]) & 0xff
	}
	return result
}
