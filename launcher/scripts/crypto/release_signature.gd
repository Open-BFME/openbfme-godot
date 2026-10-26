## Lane SIGNKMS-1: which release signatures are valid, under a LIST of trusted keys (docs/RELEASE.md, "Release signing").
##
## A release file (manifest.json, manifest-macos.json, payload.json) carries a detached 64-byte Ed25519 signature made in one of two ways:
##   scheme v1 "raw":      Ed25519 over the file's bytes (what the Deck key has always made: tools/release/sign_manifest.py --key);
##   scheme v2 "envelope": Ed25519 over the small fixed envelope that names the file (what the AWS KMS key makes: it signs at most 4096
##                         bytes, our manifests are larger):
##                             "OpenBFME signature v2\n" + kind + "\n" + <SHA-512 of the file, 128 lower-case hex> + "\n"
## `kind` is exactly one of KINDS (a launcher's proof file launcher-manifest.json is a release's manifest.json: kind "manifest.json").
## A signature is valid iff, for SOME key of the list, either scheme verifies; anything else (an unknown key, a wrong size, changed data,
## another kind) fails, and an empty list fails. An envelope starts with "OpenBFME", a signed file with "{", so neither scheme's signature
## can stand for the other's. The Python twin is tools/release/release_keys.py.
## The game's godot/scripts/crypto/release_signature.gd is this file byte for byte (tools/release/test_onezip.py checks that).
extends RefCounted

const Ed25519 := preload("res://scripts/crypto/ed25519.gd")
const Sha512 := preload("res://scripts/crypto/sha512.gd")

const ENVELOPE_HEADER := "OpenBFME signature v2\n"
const KINDS := ["manifest.json", "manifest-macos.json", "payload.json"]
const MAX_KEYS := 8
const MEMO_MAX := 256

# the results of earlier checks in this process (a check costs one to four Ed25519 verifications, ~0.3 s each in GDScript; the launcher
# checks the same cached manifest again and again): SHA-256 of (kind, keys, signature, SHA-256 of the data) -> the signer's index
static var _memo := {}


## the envelope a scheme-v2 signature of `data` (a file of `kind`) is made over
static func envelope(kind: String, data: PackedByteArray) -> PackedByteArray:
	return (ENVELOPE_HEADER + kind + "\n" + Sha512.digest(data).hex_encode() + "\n").to_ascii_buffer()


## the index in `keys` (an Array of 32-byte PackedByteArray) of the key `sig` is valid under, raw or envelope; -1 when none.
## Every (key, scheme) pair is tried until one verifies; the likely pairs first (the result is the same in any order): the first key,
## the legacy one, raw, and every other key over the envelope; then the other two.
static func signer(keys: Array, kind: String, data: PackedByteArray, sig: PackedByteArray) -> int:
	if not kind in KINDS or sig.size() != 64:
		return -1
	var hc := HashingContext.new()
	hc.start(HashingContext.HASH_SHA256)
	hc.update(data)
	var memo_text := "%s|%s|%s|" % [kind, sig.hex_encode(), hc.finish().hex_encode()]
	for key in keys:
		memo_text += (key.hex_encode() if typeof(key) == TYPE_PACKED_BYTE_ARRAY else "?") + ","
	var memo := memo_text.sha256_text()
	if _memo.has(memo):
		return _memo[memo]
	var env := envelope(kind, data)
	var found := -1
	for second_pass in [false, true]:
		for i in keys.size():
			var key = keys[i]
			if found >= 0 or typeof(key) != TYPE_PACKED_BYTE_ARRAY or key.size() != 32:
				continue
			var raw: bool = (i == 0) != second_pass
			if Ed25519.verify(key, data if raw else env, sig):
				found = i
	if _memo.size() >= MEMO_MAX:
		_memo.clear()
	_memo[memo] = found
	return found


static func valid(keys: Array, kind: String, data: PackedByteArray, sig: PackedByteArray) -> bool:
	return signer(keys, kind, data, sig) >= 0


## A list of keys as text: 64 lower-case hex digits each, separated by commas (build_info.json's entries, release_trust.gd RELEASE_KEYS,
## --trust-key). {ok, error, keys: Array of PackedByteArray}: 1 to MAX_KEYS keys, none twice; anything else is an error (never a guess).
static func parse_hex_list(text: String) -> Dictionary:
	var out := []
	var seen := {}
	for part in text.split(","):
		var hex := part.strip_edges()
		if not _is_key_hex(hex):
			return {"ok": false, "error": "'%s' is not an Ed25519 public key (64 lower-case hex digits)" % hex.left(70).c_escape(), "keys": []}
		if seen.has(hex):
			return {"ok": false, "error": "the key %s is listed twice" % hex.left(16), "keys": []}
		seen[hex] = true
		out.append(hex.hex_decode())
	if out.size() > MAX_KEYS:
		return {"ok": false, "error": "%d keys (at most %d)" % [out.size(), MAX_KEYS], "keys": []}
	return {"ok": true, "error": "", "keys": out}


static func _is_key_hex(hex: String) -> bool:
	if hex.length() != 64:
		return false
	for c in hex:
		if not c in "0123456789abcdef":
			return false
	return true
