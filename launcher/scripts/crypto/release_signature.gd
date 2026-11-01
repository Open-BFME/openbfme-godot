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
const NIGHTLY_MAX_KEYS := 4   # lane RELGH-2
const NIGHTLY_VERSION_RE := "^v(0|[1-9][0-9]{0,8})\\.(0|[1-9][0-9]{0,8})\\.(0|[1-9][0-9]{0,8})-nightly\\.[0-9]{8,9}\\z"

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


## lane RELGH-2: true when `data` (a release file of `kind`) is a nightly document: a JSON object whose "version" is a String that is a
## nightly tag; a manifest kind also needs "channel" == "preview" (payload.json has no channel field)
static func is_nightly_document(kind: String, data: PackedByteArray) -> bool:
	if not kind in KINDS:
		return false
	var d: Variant = JSON.parse_string(data.get_string_from_utf8())
	if typeof(d) != TYPE_DICTIONARY or typeof(d.get("version")) != TYPE_STRING:
		return false
	var re := RegEx.create_from_string(NIGHTLY_VERSION_RE)
	if re.search(d.version) == null:
		return false
	if kind != "payload.json":
		return typeof(d.get("channel")) == TYPE_STRING and d.channel == "preview"
	return true


## lane RELGH-2: `sig` is valid for the release file `data` of `kind` iff it verifies under the stable `keys` (valid), or under one of
## `nightly_keys` AND the file is a nightly document. An empty `nightly_keys` is the stable check alone.
static func valid_scoped(keys: Array, nightly_keys: Array, kind: String, data: PackedByteArray, sig: PackedByteArray) -> bool:
	if valid(keys, kind, data, sig):
		return true
	if nightly_keys.is_empty() or not is_nightly_document(kind, data):
		return false
	return valid(nightly_keys, kind, data, sig)


## lane RELGH-2: the nightly list as text (build_info.json's entries joined, release_trust.gd NIGHTLY_KEYS, --trust-nightly-key): empty text
## is no keys; else 1 to NIGHTLY_MAX_KEYS distinct keys. {ok, error, keys}; anything else is an error (never a guess).
static func parse_nightly_list(text: String) -> Dictionary:
	if text.strip_edges() == "":
		return {"ok": true, "error": "", "keys": []}
	var parsed := parse_hex_list(text)
	if parsed.ok and parsed.keys.size() > NIGHTLY_MAX_KEYS:
		return {"ok": false, "error": "%d nightly keys (at most %d)" % [parsed.keys.size(), NIGHTLY_MAX_KEYS], "keys": []}
	return parsed


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
