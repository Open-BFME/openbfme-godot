## Lane LAUNCH-1: SHA-512 (FIPS 180-4) in GDScript, for Ed25519 verification (Godot's HashingContext has MD5, SHA-1 and SHA-256 only).
##
## Every 64-bit word is a pair of 32-bit halves (hi, lo) held in GDScript ints, so no operation ever overflows int64 or shifts a negative
## value (Godot refuses both at run time). Checked against Python's hashlib in launcher/tests/crypto_test.gd (vectors written by
## tools/release/test_launcher.py) and the FIPS 180-4 examples.
extends RefCounted

const M32 := 0xFFFFFFFF

# the round constants: the first 64 bits of the fractional parts of the cube roots of the first 80 primes, as (hi, lo)
const K: Array[int] = [
	0x428a2f98, 0xd728ae22, 0x71374491, 0x23ef65cd, 0xb5c0fbcf, 0xec4d3b2f, 0xe9b5dba5, 0x8189dbbc, 0x3956c25b, 0xf348b538,
	0x59f111f1, 0xb605d019, 0x923f82a4, 0xaf194f9b, 0xab1c5ed5, 0xda6d8118, 0xd807aa98, 0xa3030242, 0x12835b01, 0x45706fbe,
	0x243185be, 0x4ee4b28c, 0x550c7dc3, 0xd5ffb4e2, 0x72be5d74, 0xf27b896f, 0x80deb1fe, 0x3b1696b1, 0x9bdc06a7, 0x25c71235,
	0xc19bf174, 0xcf692694, 0xe49b69c1, 0x9ef14ad2, 0xefbe4786, 0x384f25e3, 0x0fc19dc6, 0x8b8cd5b5, 0x240ca1cc, 0x77ac9c65,
	0x2de92c6f, 0x592b0275, 0x4a7484aa, 0x6ea6e483, 0x5cb0a9dc, 0xbd41fbd4, 0x76f988da, 0x831153b5, 0x983e5152, 0xee66dfab,
	0xa831c66d, 0x2db43210, 0xb00327c8, 0x98fb213f, 0xbf597fc7, 0xbeef0ee4, 0xc6e00bf3, 0x3da88fc2, 0xd5a79147, 0x930aa725,
	0x06ca6351, 0xe003826f, 0x14292967, 0x0a0e6e70, 0x27b70a85, 0x46d22ffc, 0x2e1b2138, 0x5c26c926, 0x4d2c6dfc, 0x5ac42aed,
	0x53380d13, 0x9d95b3df, 0x650a7354, 0x8baf63de, 0x766a0abb, 0x3c77b2a8, 0x81c2c92e, 0x47edaee6, 0x92722c85, 0x1482353b,
	0xa2bfe8a1, 0x4cf10364, 0xa81a664b, 0xbc423001, 0xc24b8b70, 0xd0f89791, 0xc76c51a3, 0x0654be30, 0xd192e819, 0xd6ef5218,
	0xd6990624, 0x5565a910, 0xf40e3585, 0x5771202a, 0x106aa070, 0x32bbd1b8, 0x19a4c116, 0xb8d2d0c8, 0x1e376c08, 0x5141ab53,
	0x2748774c, 0xdf8eeb99, 0x34b0bcb5, 0xe19b48a8, 0x391c0cb3, 0xc5c95a63, 0x4ed8aa4a, 0xe3418acb, 0x5b9cca4f, 0x7763e373,
	0x682e6ff3, 0xd6b2b8a3, 0x748f82ee, 0x5defb2fc, 0x78a5636f, 0x43172f60, 0x84c87814, 0xa1f0ab72, 0x8cc70208, 0x1a6439ec,
	0x90befffa, 0x23631e28, 0xa4506ceb, 0xde82bde9, 0xbef9a3f7, 0xb2c67915, 0xc67178f2, 0xe372532b, 0xca273ece, 0xea26619c,
	0xd186b8c7, 0x21c0c207, 0xeada7dd6, 0xcde0eb1e, 0xf57d4f7f, 0xee6ed178, 0x06f067aa, 0x72176fba, 0x0a637dc5, 0xa2c898a6,
	0x113f9804, 0xbef90dae, 0x1b710b35, 0x131c471b, 0x28db77f5, 0x23047d84, 0x32caab7b, 0x40c72493, 0x3c9ebe0a, 0x15c9bebc,
	0x431d67c4, 0x9c100d4c, 0x4cc5d4be, 0xcb3e42b6, 0x597f299c, 0xfc657e2a, 0x5fcb6fab, 0x3ad6faec, 0x6c44198c, 0x4a475817]

# the initial hash value: the first 64 bits of the fractional parts of the square roots of the first 8 primes
const H0: Array[int] = [
	0x6a09e667, 0xf3bcc908, 0xbb67ae85, 0x84caa73b, 0x3c6ef372, 0xfe94f82b, 0xa54ff53a, 0x5f1d36f1, 0x510e527f, 0xade682d1,
	0x9b05688c, 0x2b3e6c1f, 0x1f83d9ab, 0xfb41bd6b, 0x5be0cd19, 0x137e2179]


## The SHA-512 digest (64 bytes) of `data`.
static func digest(data: PackedByteArray) -> PackedByteArray:
	var msg := data.duplicate()
	var bit_len := data.size() * 8
	msg.append(0x80)
	while msg.size() % 128 != 112:
		msg.append(0)
	# a 128-bit big-endian length; the upper 64 bits are zero for any message Godot can hold
	for i in 8:
		msg.append(0)
	for i in range(7, -1, -1):
		msg.append((bit_len >> (8 * i)) & 0xFF)
	var h := PackedInt64Array(H0)
	var wh := PackedInt64Array()
	var wl := PackedInt64Array()
	wh.resize(80)
	wl.resize(80)
	for block in range(0, msg.size(), 128):
		for t in 16:
			var o := block + 8 * t
			wh[t] = (msg[o] << 24) | (msg[o + 1] << 16) | (msg[o + 2] << 8) | msg[o + 3]
			wl[t] = (msg[o + 4] << 24) | (msg[o + 5] << 16) | (msg[o + 6] << 8) | msg[o + 7]
		for t in range(16, 80):
			# sigma1(w[t-2]) = rotr19 ^ rotr61 ^ shr6
			var xh: int = wh[t - 2]
			var xl: int = wl[t - 2]
			var s1h := (((xh >> 19) | (xl << 13)) ^ ((xl >> 29) | (xh << 3)) ^ (xh >> 6)) & M32
			var s1l := (((xl >> 19) | (xh << 13)) ^ ((xh >> 29) | (xl << 3)) ^ ((xl >> 6) | (xh << 26))) & M32
			# sigma0(w[t-15]) = rotr1 ^ rotr8 ^ shr7
			xh = wh[t - 15]
			xl = wl[t - 15]
			var s0h := (((xh >> 1) | (xl << 31)) ^ ((xh >> 8) | (xl << 24)) ^ (xh >> 7)) & M32
			var s0l := (((xl >> 1) | (xh << 31)) ^ ((xl >> 8) | (xh << 24)) ^ ((xl >> 7) | (xh << 25))) & M32
			var lo: int = s1l + wl[t - 7] + s0l + wl[t - 16]
			wl[t] = lo & M32
			wh[t] = (s1h + wh[t - 7] + s0h + wh[t - 16] + (lo >> 32)) & M32
		var ah: int = h[0]; var al: int = h[1]; var bh: int = h[2]; var bl: int = h[3]
		var ch: int = h[4]; var cl: int = h[5]; var dh: int = h[6]; var dl: int = h[7]
		var eh: int = h[8]; var el: int = h[9]; var fh: int = h[10]; var fl: int = h[11]
		var gh: int = h[12]; var gl: int = h[13]; var hh: int = h[14]; var hl: int = h[15]
		for t in 80:
			# Sigma1(e) = rotr14 ^ rotr18 ^ rotr41
			var S1h := (((eh >> 14) | (el << 18)) ^ ((eh >> 18) | (el << 14)) ^ ((el >> 9) | (eh << 23))) & M32
			var S1l := (((el >> 14) | (eh << 18)) ^ ((el >> 18) | (eh << 14)) ^ ((eh >> 9) | (el << 23))) & M32
			var chh := (eh & fh) ^ ((eh ^ M32) & gh)
			var chl := (el & fl) ^ ((el ^ M32) & gl)
			var t1l: int = hl + S1l + chl + K[2 * t + 1] + wl[t]
			var t1h: int = (hh + S1h + chh + K[2 * t] + wh[t] + (t1l >> 32)) & M32
			t1l &= M32
			# Sigma0(a) = rotr28 ^ rotr34 ^ rotr39
			var S0h := (((ah >> 28) | (al << 4)) ^ ((al >> 2) | (ah << 30)) ^ ((al >> 7) | (ah << 25))) & M32
			var S0l := (((al >> 28) | (ah << 4)) ^ ((ah >> 2) | (al << 30)) ^ ((ah >> 7) | (al << 25))) & M32
			var mjh := (ah & bh) ^ (ah & ch) ^ (bh & ch)
			var mjl := (al & bl) ^ (al & cl) ^ (bl & cl)
			var t2l := S0l + mjl
			var t2h := (S0h + mjh + (t2l >> 32)) & M32
			t2l &= M32
			hh = gh; hl = gl; gh = fh; gl = fl; fh = eh; fl = el
			el = dl + t1l
			eh = (dh + t1h + (el >> 32)) & M32
			el &= M32
			dh = ch; dl = cl; ch = bh; cl = bl; bh = ah; bl = al
			al = t1l + t2l
			ah = (t1h + t2h + (al >> 32)) & M32
			al &= M32
		var v := [ah, al, bh, bl, ch, cl, dh, dl, eh, el, fh, fl, gh, gl, hh, hl]
		for i in range(0, 16, 2):
			var lo: int = h[i + 1] + v[i + 1]
			h[i + 1] = lo & M32
			h[i] = (h[i] + v[i] + (lo >> 32)) & M32
	var out := PackedByteArray()
	for x in h:
		out.append((x >> 24) & 0xFF)
		out.append((x >> 16) & 0xFF)
		out.append((x >> 8) & 0xFF)
		out.append(x & 0xFF)
	return out
