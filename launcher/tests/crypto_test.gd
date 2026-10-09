## Lane LAUNCH-1: the launcher's SHA-512 and Ed25519 verification against reference values.
##
##   godot --headless --path launcher --script res://tests/crypto_test.gd [-- --vectors=FILE]
##
## Built in: the FIPS 180-4 SHA-512 examples and the RFC 8032 7.1 tests 1-3 (each also with a flipped message / signature / key bit, S + L
## and a non-canonical public key). --vectors: a JSON file {"sha512": [[hex in, hex digest]...], "ed25519": [[pk, msg, sig, valid]...]}
## written by tools/release/test_launcher.py from hashlib, tools/release/ed25519.py and OpenSSL.
## Prints CRYPTO lines and "CRYPTO OK <n> checks" or "CRYPTO FAIL ..."; exit 0 only when every check passed.
extends SceneTree

const Sha512 := preload("res://scripts/crypto/sha512.gd")
const Ed25519 := preload("res://scripts/crypto/ed25519.gd")

const RFC8032 := [
	["d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a", "",
	 "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b"],
	["3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c", "72",
	 "92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00"],
	["fc51cd8e6218a1a38da47ed00230f0580816ed13ba3303ac5deb911548908025", "af82",
	 "6291d657deec24024827e69c3abe01a30ce548a284743a445e3680d7db5ac3ac18ff9b538d16f290ae67f760984dc6594a7c15e9716ed28dc027beceea1ec40a"],
]

# the 8 points of small order (the torsion subgroup; tools/release/test_launcher_tools.py derives the same list from the curve)
const SMALL_ORDER := [
	"0000000000000000000000000000000000000000000000000000000000000000", "0000000000000000000000000000000000000000000000000000000000000080",
	"0100000000000000000000000000000000000000000000000000000000000000", "26e8958fc2b227b045c3f489f2ef98f0d5dfac05d3c63339b13802886d53fc05",
	"26e8958fc2b227b045c3f489f2ef98f0d5dfac05d3c63339b13802886d53fc85", "c7176a703d4dd84fba3c0b760d10670f2a2053fa2c39ccc64ec7fd7792ac037a",
	"c7176a703d4dd84fba3c0b760d10670f2a2053fa2c39ccc64ec7fd7792ac03fa", "ecffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff7f",
]

var _checks := 0
var _failed := []


func _check(ok: bool, what: String) -> void:
	_checks += 1
	if not ok:
		_failed.append(what)
		print("CRYPTO FAIL ", what)


func _init() -> void:
	_check(Sha512.digest("abc".to_utf8_buffer()).hex_encode() == "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f", "sha512 abc")
	_check(Sha512.digest(PackedByteArray()).hex_encode() == "cf83e1357eefb8bdf1542850d66d8007d620e4050b5715dc83f4a921d36ce9ce47d0d13c5d85f2b0ff8318d2877eec2f63b931bd47417a81a538327af927da3e", "sha512 empty")
	_check(Sha512.digest("abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu".to_utf8_buffer()).hex_encode() == "8e959b75dae313da8cf4f72814fc143f8f7779c6eb9f7fa17299aeadb6889018501d289e4900f7e4331b99dec4b5433ac7d329eeb6dd26545e96e55b874be909", "sha512 two blocks")
	var t0 := Time.get_ticks_msec()
	for v in RFC8032:
		var pk: PackedByteArray = v[0].hex_decode()
		var msg: PackedByteArray = v[1].hex_decode()
		var sig: PackedByteArray = v[2].hex_decode()
		_check(Ed25519.verify(pk, msg, sig), "rfc8032 valid %s" % v[0].left(8))
		var bad_sig := sig.duplicate()
		bad_sig[5] ^= 1
		_check(not Ed25519.verify(pk, msg, bad_sig), "rfc8032 flipped R bit %s" % v[0].left(8))
		bad_sig = sig.duplicate()
		bad_sig[40] ^= 1
		_check(not Ed25519.verify(pk, msg, bad_sig), "rfc8032 flipped S bit %s" % v[0].left(8))
		var bad_msg := msg.duplicate()
		bad_msg.append(0)
		_check(not Ed25519.verify(pk, bad_msg, sig), "rfc8032 longer message %s" % v[0].left(8))
		var bad_pk := pk.duplicate()
		bad_pk[0] ^= 2
		_check(not Ed25519.verify(bad_pk, msg, sig), "rfc8032 other key %s" % v[0].left(8))
		# S + L: the same point equation holds, RFC 8032 requires the rejection (malleability)
		var s_plus_l := sig.duplicate()
		var carry := 0
		for i in 32:
			var x: int = s_plus_l[32 + i] + Ed25519.L[i] + carry
			s_plus_l[32 + i] = x & 0xff
			carry = x >> 8
		_check(carry == 0 and not Ed25519.verify(pk, msg, s_plus_l), "rfc8032 S + L rejected %s" % v[0].left(8))
	var ms := Time.get_ticks_msec() - t0
	print("CRYPTO rfc8032: %d verifications in %d ms" % [RFC8032.size() * 6, ms])
	# a non-canonical public key: y = p (encodes 0 mod p) is not accepted even though y mod p = 0 is a point... (ed ff .. ff 7f)
	var noncanon := PackedByteArray()
	noncanon.resize(32)
	noncanon.fill(0xff)
	noncanon[0] = 0xed
	noncanon[31] = 0x7f
	_check(not Ed25519.verify(noncanon, PackedByteArray(), RFC8032[0][2].hex_decode()), "non-canonical public key")
	_check(not Ed25519.verify(RFC8032[0][0].hex_decode().slice(0, 31), PackedByteArray(), RFC8032[0][2].hex_decode()), "short public key")
	_check(not Ed25519.verify(RFC8032[0][0].hex_decode(), PackedByteArray(), RFC8032[0][2].hex_decode().slice(0, 63)), "short signature")
	# small-order public keys and commitments (Sol r1: the identity key with R = identity, S = 0 verified any message)
	var identity: PackedByteArray = SMALL_ORDER[2].hex_decode()
	var zero_s := PackedByteArray()
	zero_s.resize(32)
	for enc in SMALL_ORDER:
		var pt := Ed25519._unpackneg(enc.hex_decode())
		_check(not pt.is_empty() and Ed25519.is_small_order(pt), "small order: %s" % enc.left(8))
		for r in [identity, enc.hex_decode()]:
			var sig: PackedByteArray = r.duplicate()
			sig.append_array(zero_s)
			_check(not Ed25519.verify(enc.hex_decode(), "forged".to_utf8_buffer(), sig), "small-order key %s rejected" % enc.left(8))
	for v in RFC8032:
		_check(not Ed25519.is_small_order(Ed25519._unpackneg(v[0].hex_decode())), "a real key is not small order %s" % v[0].left(8))
		# a valid key with a small-order R: rejected
		var sig := identity.duplicate()
		sig.append_array(v[2].hex_decode().slice(32, 64))
		_check(not Ed25519.verify(v[0].hex_decode(), v[1].hex_decode(), sig), "small-order R rejected %s" % v[0].left(8))
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--vectors="):
			_run_file(arg.substr(10))
	if _failed.is_empty():
		print("CRYPTO OK %d checks" % _checks)
		quit(0)
	else:
		print("CRYPTO FAIL %d of %d checks" % [_failed.size(), _checks])
		quit(1)


func _run_file(path: String) -> void:
	var data = JSON.parse_string(FileAccess.get_file_as_string(path))
	if typeof(data) != TYPE_DICTIONARY:
		_check(false, "vectors file %s unreadable" % path)
		return
	for v in data.get("sha512", []):
		_check(Sha512.digest(v[0].hex_decode()).hex_encode() == v[1], "sha512 vector of %d bytes" % (v[0].length() / 2))
	var n := 0
	for v in data.get("ed25519", []):
		var ok := Ed25519.verify(v[0].hex_decode(), v[1].hex_decode(), v[2].hex_decode())
		_check(ok == v[3], "ed25519 vector %d (%s, expected %s)" % [n, v[4] if v.size() > 4 else "", "valid" if v[3] else "invalid"])
		n += 1
	print("CRYPTO vectors file: %d sha512, %d ed25519" % [data.get("sha512", []).size(), n])
