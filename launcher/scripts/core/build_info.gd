## Lane LAUNCH-1: what this launcher build is. tools/release/package.sh writes res://build_info.json into the staged project before the
## export: {"version", "commit", "date", "repo" (the GitHub <owner>/<name> it updates from, a build-time setting), "release_key" (the
## Ed25519 public key, 64 hex digits, the only key whose manifests it accepts), "test_build" (true only for the test packages of
## tools/release/test_launcher.py)}. A packaged launcher without it is an error (no update without a key and a repository).
## A development run (the Godot editor binary on launcher/) has no build_info.json: version v0.0.0-dev, the default repository, no key
## unless --trust-key=<hex> is given; development runs and test builds accept the test options (tests_allowed).
extends RefCounted

const DEFAULT_REPO := "Open-BFME/openbfme-godot"

var version := "v0.0.0-dev"
var commit := ""
var date := ""
var repo := DEFAULT_REPO
var release_key := PackedByteArray()
var test_build := false
var packaged := false  # running from an exported executable
var error := ""


static func load_info() -> RefCounted:
	var info = load("res://scripts/core/build_info.gd").new()
	info.packaged = OS.has_feature("template")
	if not FileAccess.file_exists("res://build_info.json"):
		if info.packaged:
			info.error = "this launcher package has no build information (res://build_info.json): it was not made by tools/release/package.sh"
		return info
	var d = JSON.parse_string(FileAccess.get_file_as_string("res://build_info.json"))
	if typeof(d) != TYPE_DICTIONARY or typeof(d.get("version")) != TYPE_STRING or typeof(d.get("repo")) != TYPE_STRING \
			or typeof(d.get("release_key")) != TYPE_STRING or typeof(d.get("test_build")) != TYPE_BOOL:
		info.error = "res://build_info.json is malformed"
		return info
	info.version = d.version
	info.commit = str(d.get("commit", ""))
	info.date = str(d.get("date", ""))
	info.repo = d.repo
	info.test_build = d.test_build
	var key: String = d.release_key
	if key.length() != 64 or key.hex_decode().size() != 32:
		info.error = "res://build_info.json has no valid release key"
		return info
	info.release_key = key.hex_decode()
	return info


## the test options (--api-base, --trust-key, --launcher-dir) are honoured only here: a development run or a test package
func tests_allowed() -> bool:
	return OS.has_feature("editor") or test_build


func platform() -> String:
	return "windows-x64" if OS.get_name() == "Windows" else "linux-x64"
