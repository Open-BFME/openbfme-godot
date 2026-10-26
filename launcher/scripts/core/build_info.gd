## Lane LAUNCH-1: what this launcher build is. tools/release/package.sh writes res://build_info.json into the staged project before the
## export: {"version", "commit", "date", "repo" (the GitHub <owner>/<name> it updates from, a build-time setting), "release_key" (the
## legacy Ed25519 public key, 64 hex digits), "release_keys" (lane SIGNKMS-1: the trusted keys, launcher/release_keys.pub in file order;
## its first entry must be release_key; a build_info.json without it, made before SIGNKMS-1, trusts [release_key]; a malformed list is
## an error, never a guess), "test_build" (true only for the test packages of tools/release/test_launcher.py), "local_trial" (a
## non-release key list; labels only, no test overrides)}. Signatures are checked under the whole list (scripts/crypto/release_signature.gd).
## A packaged launcher without it is an error (no update without a key and a repository).
## A development run (the Godot editor binary on launcher/) has no build_info.json: version v0.0.0-dev, the default repository, no key
## unless --trust-key=<hex>[,<hex>...] is given; development runs and test builds accept the test options (tests_allowed).
extends RefCounted

const DEFAULT_REPO := "Open-BFME/openbfme-godot"
const Semver := preload("res://scripts/core/semver.gd")
const ReleaseSignature := preload("res://scripts/crypto/release_signature.gd")

var version := "v0.0.0-dev"
var display_version := "v0.0.0-dev"
var commit := ""
var date := ""
var repo := DEFAULT_REPO
var release_keys: Array = []   # the trusted release keys, each a 32-byte PackedByteArray (empty: no key, no update)
var test_build := false
var local_trial := false
var macos_preview := false
var macos_updates := false
var packaged := false  # running from an exported executable
var error := ""


static func load_info() -> RefCounted:
	var info = load("res://scripts/core/build_info.gd").new()
	info.packaged = OS.has_feature("template")
	if not FileAccess.file_exists("res://build_info.json"):
		if info.packaged:
			info.error = "this launcher package has no build information (res://build_info.json): it was not made by tools/release/package.sh"
		return info
	return from_text(info, FileAccess.get_file_as_string("res://build_info.json"))


## `info` filled from the text of a build_info.json (info.error names what is wrong; the unit tests call this with their own texts)
static func from_text(info: RefCounted, text: String) -> RefCounted:
	var d = JSON.parse_string(text)
	if typeof(d) != TYPE_DICTIONARY or typeof(d.get("version")) != TYPE_STRING or typeof(d.get("repo")) != TYPE_STRING \
			or typeof(d.get("release_key")) != TYPE_STRING or typeof(d.get("test_build")) != TYPE_BOOL:
		info.error = "res://build_info.json is malformed"
		return info
	info.version = d.version
	info.display_version = str(d.get("display_version", d.version))
	info.commit = str(d.get("commit", ""))
	info.date = str(d.get("date", ""))
	info.repo = d.repo
	info.test_build = d.test_build
	if typeof(d.get("local_trial", false)) != TYPE_BOOL:
		info.error = "res://build_info.json has an invalid local_trial flag"
		return info
	info.local_trial = d.get("local_trial", false)
	if typeof(d.get("macos_preview", false)) != TYPE_BOOL:
		info.error = "res://build_info.json has an invalid macos_preview flag"
		return info
	info.macos_preview = d.get("macos_preview", false)
	if typeof(d.get("macos_updates", false)) != TYPE_BOOL:
		info.error = "res://build_info.json has an invalid macos_updates flag"
		return info
	info.macos_updates = d.get("macos_updates", false)
	var key: String = d.release_key
	if not ReleaseSignature.parse_hex_list(key).ok or key.contains(","):
		info.error = "res://build_info.json has no valid release key"
		return info
	var keys := parse_release_keys(d)
	if not keys.ok:
		info.error = "res://build_info.json: " + keys.error
		return info
	info.release_keys = keys.keys
	return info


## lane SIGNKMS-1: the trusted key list of a parsed build_info.json `d` whose release_key is valid: "release_keys" (an array of 1 to 8
## distinct 64-hex keys whose first is release_key), or [release_key] when the field is absent (a build made before the list).
## {ok, error, keys}
static func parse_release_keys(d: Dictionary) -> Dictionary:
	if not d.has("release_keys"):
		return {"ok": true, "error": "", "keys": [String(d.release_key).hex_decode()]}
	var list = d.release_keys
	if typeof(list) != TYPE_ARRAY or list.is_empty():
		return {"ok": false, "error": "release_keys is not a list of keys", "keys": []}
	for k in list:
		if typeof(k) != TYPE_STRING or k.contains(","):
			return {"ok": false, "error": "release_keys holds an entry that is not a key", "keys": []}
	var parsed := ReleaseSignature.parse_hex_list(",".join(PackedStringArray(list)))
	if not parsed.ok:
		return {"ok": false, "error": "release_keys: " + parsed.error, "keys": []}
	if list[0] != d.release_key:
		return {"ok": false, "error": "release_keys does not begin with release_key", "keys": []}
	return parsed


## the test options (--api-base, --trust-key, --launcher-dir) are honoured only here: a development run or a test package
func tests_allowed() -> bool:
	return OS.has_feature("editor") or test_build


func release_version() -> String:
	if macos_updates and Semver.is_valid(version.get_slice(" ", 0)):
		return version.get_slice(" ", 0)
	return version


## the operating system's name (Godot's OS.get_name()); OPENBFME_TEST_OS_NAME replaces it in test builds, so a test can play another OS
func os_name() -> String:
	var forced := OS.get_environment("OPENBFME_TEST_OS_NAME")
	return forced if forced != "" and tests_allowed() else OS.get_name()


func platform() -> String:
	if OS.get_name() == "Windows":
		return "windows-x64"
	if OS.get_name() == "macOS":
		if macos_updates:
			return "macos-arm64"
		if macos_preview:
			return "macos-arm64-unstable-preview"
		return "macos-universal"
	return "linux-x64"
