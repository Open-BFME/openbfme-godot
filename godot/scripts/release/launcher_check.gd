## Lane ONEZIP-1 r2 (Sol r1 of ONEZIP-1): before a game started directly runs the OpenBFME Launcher beside it, it checks that the
## launcher is a signed one. The folder of the one download carries its signed payload inventory, payload.json and payload.json.sig
## (tools/release/seal_package.py; the launcher checks the same files, launcher/scripts/core/manifest.gd verify_payload). The launcher's
## executable must be the file the inventory signs, or a launcher a self-update put there whose release's signed manifest lies beside it
## (launcher-manifest.json + .sig, written by the launcher when the new one confirmed its start: Updater.write_launcher_proof).
## The signature check is the launcher's own code, scripts/crypto/ed25519.gd, sha512.gd and release_signature.gd, byte for byte the
## launcher's copies (tools/release/test_onezip.py checks that): one implementation, not a second one. Lane SIGNKMS-1: a signature is
## valid under ANY key of the trusted list (release_trust.gd RELEASE_KEYS), raw or over the signature envelope of its kind
## ("payload.json"; the proof launcher-manifest.json is a release's manifest.json: "manifest.json").
## Lane RELGH-2: the nightly-only keys (release_trust.gd NIGHTLY_KEYS) are also trusted, for a nightly payload / manifest only
## (ReleaseSignature.valid_scoped).
extends RefCounted

const ReleaseSignature := preload("res://scripts/crypto/release_signature.gd")
const PAYLOAD := "payload.json"
const PROOF := "launcher-manifest.json"
const MAX_JSON := 65536   # the launcher's limit for a manifest (Manifest.MAX_BYTES); the payload is a few KiB


static func launcher_name() -> String:
	return "OpenBFMELauncher.exe" if OS.get_name() == "Windows" else "OpenBFMELauncher.x86_64"


static func platform() -> String:
	return "windows-x64" if OS.get_name() == "Windows" else "linux-x64"


## lane SIGNKMS-1: the trusted keys of release_trust.gd's two texts: RELEASE_KEYS parsed, its first key RELEASE_KEY. {ok, error, keys};
## empty texts (a development run) or a list that is malformed or does not begin with RELEASE_KEY: no keys, by name.
static func trusted_keys(release_key: String, release_keys: String) -> Dictionary:
	if release_key == "" and release_keys == "":
		return {"ok": false, "error": "this game has no release key", "keys": []}
	var parsed := ReleaseSignature.parse_hex_list(release_keys)
	if not parsed.ok:
		return {"ok": false, "error": "this game's release key list is malformed: " + parsed.error, "keys": []}
	if release_keys.get_slice(",", 0).strip_edges() != release_key:
		return {"ok": false, "error": "this game's release key list does not begin with its release key", "keys": []}
	return parsed


## lane RELGH-2: the nightly-only keys of release_trust.gd's NIGHTLY_KEYS text: empty text is none; malformed (or one of `release_keys`)
## is an error, so the game has no keys at all (fail closed). {ok, error, keys}
static func nightly_keys(text: String, release_keys: Array = []) -> Dictionary:
	var parsed := ReleaseSignature.parse_nightly_list(text)
	if not parsed.ok:
		return {"ok": false, "error": "this game's nightly key list is malformed: " + parsed.error, "keys": []}
	for k in parsed.keys:
		if k in release_keys:
			return {"ok": false, "error": "this game's nightly key list holds a release key", "keys": []}
	return parsed


## "" when `dir`'s launcher may be started; else why not, for the player. `keys`: the trusted release keys (an Array of 32-byte
## PackedByteArray), `repo`: <owner>/<name>.
static func check(dir: String, keys: Array, repo: String, nightly: Array = []) -> String:
	if keys.is_empty() or repo == "":
		return "this game has no release key"
	for k in keys:
		if typeof(k) != TYPE_PACKED_BYTE_ARRAY or k.size() != 32:
			return "this game has no release key"
	var exe := launcher_name()
	var d := DirAccess.open(dir)
	if d == null or not FileAccess.file_exists(dir.path_join(exe)):
		return "there is no %s beside the game" % exe
	if d.is_link(exe):
		return "%s is a link, not the signed launcher" % exe
	var payload := _signed_json(dir.path_join(PAYLOAD), keys, "payload.json", nightly)
	if payload.has("error"):
		return "the package's %s: %s" % [PAYLOAD, payload.error]
	var p: Dictionary = payload.data
	if not _is(p.get("product"), "OpenBFME") or not _is(p.get("repo"), repo) or not _is(p.get("platform"), platform()) or typeof(p.get("files")) != TYPE_DICTIONARY:
		return "the package's %s is not this platform's package of %s" % [PAYLOAD, repo]
	var sha := _sha256_of(dir.path_join(exe))
	var signed = p.files.get(exe)
	if typeof(signed) == TYPE_DICTIONARY and _is(signed.get("sha256"), sha):
		return ""
	# a launcher update: the signed manifest of its release, beside it
	var proof := _signed_json(dir.path_join(PROOF), keys, "manifest.json", nightly)
	if proof.has("error"):
		return "%s is not the package's launcher, and there is no signed proof of an update (%s: %s)" % [exe, PROOF, proof.error]
	var m: Dictionary = proof.data
	if not _is(m.get("product"), "OpenBFME") or not _is(m.get("repo"), repo) or typeof(m.get("assets")) != TYPE_DICTIONARY:
		return "%s is not a manifest of %s" % [PROOF, repo]
	for kind in ["game", "launcher"]:
		var assets = m.assets.get(kind)
		if typeof(assets) != TYPE_DICTIONARY or typeof(assets.get(platform())) != TYPE_DICTIONARY:
			continue
		var files = assets[platform()].get("files")
		if typeof(files) == TYPE_DICTIONARY and typeof(files.get(exe)) == TYPE_DICTIONARY and _is(files[exe].get("sha256"), sha):
			return ""
	return "%s is neither the package's launcher nor the launcher of the signed release in %s" % [exe, PROOF]


## a text equal to `text` (GDScript refuses == between a text and a number: the type is checked first)
static func _is(v: Variant, text: String) -> bool:
	return typeof(v) == TYPE_STRING and v == text


## {data} of a JSON object whose detached signature (<path>.sig, 64 bytes) verifies under one of `keys` (raw, or the envelope of `kind`),
## else {error}
static func _signed_json(path: String, keys: Array, kind: String, nightly: Array = []) -> Dictionary:
	var f := FileAccess.open(path, FileAccess.READ)
	if f == null:
		return {"error": "missing"}
	if f.get_length() > MAX_JSON:
		return {"error": "larger than %d bytes" % MAX_JSON}
	var bytes := f.get_buffer(f.get_length())
	# the signature's size before it is read (Sol r2: a 32 MiB signature was read whole first)
	var s := FileAccess.open(path + ".sig", FileAccess.READ)
	if s == null or s.get_length() != 64:
		return {"error": "no signature (%s.sig is not 64 bytes)" % path.get_file()}
	var sig := s.get_buffer(64)
	s.close()
	if not ReleaseSignature.valid_scoped(keys, nightly, kind, bytes, sig):
		return {"error": "the signature is not the OpenBFME release's"}
	var data: Variant = JSON.parse_string(bytes.get_string_from_utf8())
	if not data is Dictionary:
		return {"error": "not a JSON object"}
	return {"data": data}


static func _sha256_of(path: String) -> String:
	var f := FileAccess.open(path, FileAccess.READ)
	if f == null:
		return ""
	var hc := HashingContext.new()
	hc.start(HashingContext.HASH_SHA256)
	while f.get_position() < f.get_length():
		hc.update(f.get_buffer(1 << 20))
	return hc.finish().hex_encode()
