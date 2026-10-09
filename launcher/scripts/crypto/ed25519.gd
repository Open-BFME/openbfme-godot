## Lane LAUNCH-1: Ed25519 signature VERIFICATION (RFC 8032, PureEdDSA) in GDScript, for the launcher's signed release manifests.
##
## Why GDScript (docs/RELEASE.md, "The updater"): Godot's Crypto has no Ed25519 (mbedTLS lacks it), and a native GDExtension for the launcher
## would need its own cross build per platform and a second toolchain in the update path. Verification is only a few thousand field
## multiplications on a manifest of a few kilobytes, so pure GDScript is fast enough (well under a second, timed by launcher/tests/crypto_test.gd),
## and the same file runs unchanged on every platform.
##
## Port of TweetNaCl's crypto_sign_open (Bernstein, van Gastel, Janssen, Lange, Schwabe, Smetsers; public domain): field elements are 16
## limbs of 16 bits in int64 (car25519, M, S, pack25519, inv25519, pow2523, the extended-coordinates `add`, unpackneg, modL). Changes:
##   * Godot refuses `>>` on a negative int at run time, so every shift of a possibly negative value is a floor division (_fsr);
##   * strict RFC 8032 5.1.7 decoding that TweetNaCl leaves out: S < L, the public key's y < p, and x = 0 with the sign bit set rejected;
##   * a public key A or a commitment R of small order (one of the 8 torsion points: [8]P is the neutral element) is rejected (Sol r1:
##     the identity key accepted a forged signature; the release key is of prime order, so this is defence in depth);
##   * a joint double-and-add for [h](-A) + [S]B (variable time: verification handles only public data).
## Checked against the RFC 8032 7.1 test vectors and signatures made by tools/release/ed25519.py and by OpenSSL (launcher/tests/crypto_test.gd).
extends RefCounted

const Sha512 := preload("res://scripts/crypto/sha512.gd")

const D: Array[int] = [0x78a3, 0x1359, 0x4dca, 0x75eb, 0xd8ab, 0x4141, 0x0a4d, 0x0070, 0xe898, 0x7779, 0x4079, 0x8cc7, 0xfe73, 0x2b6f, 0x6cee, 0x5203]
const D2: Array[int] = [0xf159, 0x26b2, 0x9b94, 0xebd6, 0xb156, 0x8283, 0x149a, 0x00e0, 0xd130, 0xeef3, 0x80f2, 0x198e, 0xfce7, 0x56df, 0xd9dc, 0x2406]
const GX: Array[int] = [0xd51a, 0x8f25, 0x2d60, 0xc956, 0xa7b2, 0x9525, 0xc760, 0x692c, 0xdc5c, 0xfdd6, 0xe231, 0xc0a4, 0x53fe, 0xcd6e, 0x36d3, 0x2169]
const GY: Array[int] = [0x6658, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666]
const SQRTM1: Array[int] = [0xa0b0, 0x4a0e, 0x1b27, 0xc4ee, 0xe478, 0xad2f, 0x1806, 0x2f43, 0xd7a7, 0x3dfb, 0x0099, 0x2b4d, 0xdf0b, 0x4fc1, 0x2480, 0x2b83]
# the group order L = 2^252 + 27742317777372353535851937790883648493, little-endian bytes
const L: Array[int] = [0xed, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58, 0xd6, 0x9c, 0xf7, 0xa2, 0xde, 0xf9, 0xde, 0x14,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x10]


## floor(x / 2^n) for any int (Godot's `>>` refuses negative operands)
static func _fsr(x: int, n: int) -> int:
	return x >> n if x >= 0 else -((-x - 1) >> n) - 1


static func _gf(v: Array = []) -> PackedInt64Array:
	var o := PackedInt64Array()
	o.resize(16)
	for i in v.size():
		o[i] = v[i]
	return o


static func _car(o: PackedInt64Array) -> void:
	for i in 16:
		o[i] += 65536
		var c := _fsr(o[i], 16)
		if i < 15:
			o[i + 1] += c - 1
		else:
			o[0] += 38 * (c - 1)
		o[i] -= c * 65536


static func _add_gf(a: PackedInt64Array, b: PackedInt64Array) -> PackedInt64Array:
	var o := _gf()
	for i in 16:
		o[i] = a[i] + b[i]
	return o


static func _sub_gf(a: PackedInt64Array, b: PackedInt64Array) -> PackedInt64Array:
	var o := _gf()
	for i in 16:
		o[i] = a[i] - b[i]
	return o


static func _mul(a: PackedInt64Array, b: PackedInt64Array) -> PackedInt64Array:
	var t := PackedInt64Array()
	t.resize(31)
	for i in 16:
		var ai: int = a[i]
		for j in 16:
			t[i + j] += ai * b[j]
	for i in 15:
		t[i] += 38 * t[i + 16]
	var o := t.slice(0, 16)
	_car(o)
	_car(o)
	return o


static func _sq(a: PackedInt64Array) -> PackedInt64Array:
	return _mul(a, a)


static func _inv(i: PackedInt64Array) -> PackedInt64Array:
	var c := i.duplicate()
	for a in range(253, -1, -1):
		c = _sq(c)
		if a != 2 and a != 4:
			c = _mul(c, i)
	return c


static func _pow2523(i: PackedInt64Array) -> PackedInt64Array:
	var c := i.duplicate()
	for a in range(250, -1, -1):
		c = _sq(c)
		if a != 1:
			c = _mul(c, i)
	return c


static func _pack25519(n: PackedInt64Array) -> PackedByteArray:
	var t := n.duplicate()
	_car(t)
	_car(t)
	_car(t)
	for j in 2:
		var m := _gf()
		m[0] = t[0] - 0xffed
		for i in range(1, 15):
			m[i] = t[i] - 0xffff - (_fsr(m[i - 1], 16) & 1)
			m[i - 1] &= 0xffff
		m[15] = t[15] - 0x7fff - (_fsr(m[14], 16) & 1)
		var b := _fsr(m[15], 16) & 1
		m[14] &= 0xffff
		if b == 0:
			t = m
	var o := PackedByteArray()
	o.resize(32)
	for i in 16:
		o[2 * i] = t[i] & 0xff
		o[2 * i + 1] = (t[i] >> 8) & 0xff
	return o


static func _unpack25519(n: PackedByteArray) -> PackedInt64Array:
	var o := _gf()
	for i in 16:
		o[i] = n[2 * i] + (n[2 * i + 1] << 8)
	o[15] &= 0x7fff
	return o


static func _parity(a: PackedInt64Array) -> int:
	return _pack25519(a)[0] & 1


static func _neq(a: PackedInt64Array, b: PackedInt64Array) -> bool:
	return _pack25519(a) != _pack25519(b)


## point addition in extended coordinates (X, Y, Z, T); the formula is complete on Ed25519, so it also doubles and adds the neutral element
static func _padd(p: Array, q: Array) -> Array:
	var a := _mul(_sub_gf(p[1], p[0]), _sub_gf(q[1], q[0]))
	var b := _mul(_add_gf(p[0], p[1]), _add_gf(q[0], q[1]))
	var c := _mul(_mul(p[3], q[3]), _gf(D2))
	var d := _mul(p[2], q[2])
	d = _add_gf(d, d)
	var e := _sub_gf(b, a)
	var f := _sub_gf(d, c)
	var g := _add_gf(d, c)
	var h := _add_gf(b, a)
	return [_mul(e, f), _mul(h, g), _mul(g, f), _mul(e, h)]


static func _pack_point(p: Array) -> PackedByteArray:
	var zi := _inv(p[2])
	var r := _pack25519(_mul(p[1], zi))
	r[31] ^= _parity(_mul(p[0], zi)) << 7
	return r


## -A for the encoded point A, or [] when it is not a canonical encoding of a curve point
static func _unpackneg(pk: PackedByteArray) -> Array:
	# y < p (RFC 8032 5.1.3 step 1): the only non-canonical y are p..2^255-1, i.e. bytes ed ff .. ff 7f (sign bit masked)
	var high := true
	for i in range(1, 31):
		high = high and pk[i] == 0xff
	if high and (pk[31] & 0x7f) == 0x7f and pk[0] >= 0xed:
		return []
	var one := _gf([1])
	var y := _unpack25519(pk)
	var num := _sq(y)
	var den := _mul(num, _gf(D))
	num = _sub_gf(num, one)
	den = _add_gf(one, den)
	var den2 := _sq(den)
	var den4 := _sq(den2)
	var den6 := _mul(den4, den2)
	var t := _mul(_mul(den6, num), den)
	t = _pow2523(t)
	t = _mul(_mul(_mul(t, num), den), den)
	var x := _mul(t, den)
	var chk := _mul(_sq(x), den)
	if _neq(chk, num):
		x = _mul(x, _gf(SQRTM1))
	chk = _mul(_sq(x), den)
	if _neq(chk, num):
		return []
	var sign := pk[31] >> 7
	var zero := PackedByteArray()
	zero.resize(32)
	if sign == 1 and _pack25519(x) == zero:
		return []  # x = 0 with the sign bit set (RFC 8032 5.1.3 step 4)
	if _parity(x) == sign:
		x = _sub_gf(_gf(), x)
	return [x, y, one, _mul(x, y)]


## x (64 little-endian byte values) reduced modulo L, as 32 bytes
static func _mod_l(x: PackedInt64Array) -> PackedByteArray:
	for i in range(63, 31, -1):
		var carry := 0
		var j := i - 32
		while j < i - 12:
			x[j] += carry - 16 * x[i] * L[j - (i - 32)]
			carry = _fsr(x[j] + 128, 8)
			x[j] -= carry * 256
			j += 1
		x[j] += carry
		x[i] = 0
	var carry := 0
	for j in 32:
		x[j] += carry - _fsr(x[31], 4) * L[j]
		carry = _fsr(x[j], 8)
		x[j] &= 255
	for j in 32:
		x[j] -= carry * L[j]
	var r := PackedByteArray()
	r.resize(32)
	for i in 32:
		x[i + 1] += _fsr(x[i], 8)
		r[i] = x[i] & 255
	return r


## true when [8]P is the neutral element (P is one of the 8 points of small order)
static func is_small_order(p: Array) -> bool:
	var q := p
	for i in 3:
		q = _padd(q, q)
	var zero := PackedByteArray()
	zero.resize(32)
	return _pack25519(q[0]) == zero and _pack25519(q[1]) == _pack25519(q[2])


static func _bit(s: PackedByteArray, i: int) -> int:
	return (s[i >> 3] >> (i & 7)) & 1


## true when `signature` (64 bytes) is a valid Ed25519 signature of `msg` under `public_key` (32 bytes)
static func verify(public_key: PackedByteArray, msg: PackedByteArray, signature: PackedByteArray) -> bool:
	if public_key.size() != 32 or signature.size() != 64:
		return false
	# S < L (RFC 8032 5.1.7 step 1: the signature's second half is a canonical scalar), compared from the most significant byte
	var s := signature.slice(32, 64)
	var below := false
	for i in range(31, -1, -1):
		if s[i] != L[i]:
			below = s[i] < L[i]
			break
	if not below:
		return false
	var neg_a := _unpackneg(public_key)
	if neg_a.is_empty() or is_small_order(neg_a):
		return false
	var neg_r := _unpackneg(signature.slice(0, 32))
	if neg_r.is_empty() or is_small_order(neg_r):
		return false
	var hash_in := signature.slice(0, 32)
	hash_in.append_array(public_key)
	hash_in.append_array(msg)
	var digest := Sha512.digest(hash_in)
	var wide := PackedInt64Array()
	wide.resize(64)
	for i in 64:
		wide[i] = digest[i]
	var h := _mod_l(wide)
	var base := [_gf(GX), _gf(GY), _gf([1]), _mul(_gf(GX), _gf(GY))]
	var both := _padd(neg_a, base)
	var p := [_gf(), _gf([1]), _gf([1]), _gf()]
	for i in range(255, -1, -1):
		p = _padd(p, p)
		var hb := _bit(h, i)
		var sb := _bit(s, i)
		if hb and sb:
			p = _padd(p, both)
		elif hb:
			p = _padd(p, neg_a)
		elif sb:
			p = _padd(p, base)
	return _pack_point(p) == signature.slice(0, 32)
