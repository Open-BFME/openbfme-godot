## OpenBFME. GPL-3.0.
extends RefCounted

const INSTALL_FILE := "user://install_id.txt"


# ONLINEMENU-1: no lock directory (SOCIAL-1's lock, left behind by a crash, refused every later sign-in): DEVINPR-1's write-then-rename already gives
# two games (or the game and the launcher) starting at the same moment the same id
static func install_id(path: String = INSTALL_FILE) -> Dictionary:
	if FileAccess.file_exists(path):
		return _read_install(path)
	var bytes := Crypto.new().generate_random_bytes(16)
	if bytes.size() != 16:
		return {"error": "cannot generate an install id"}
	# DEVINPR-1 (review of WP-C): written to a file of its own, then renamed into place, so a crash, a full disk or a second game starting at the same
	# moment never leaves a half-written id behind; the id used is the one the file holds after the rename (the same in both games of a race)
	var temp := "%s.%s.tmp" % [path, Crypto.new().generate_random_bytes(8).hex_encode()]
	var file := FileAccess.open(temp, FileAccess.WRITE)
	if file == null:
		return {"error": "cannot save the install id in %s" % ProjectSettings.globalize_path(path)}
	file.store_string(bytes.hex_encode() + "\n")
	file.flush()
	var failed := file.get_error() != OK
	file.close()
	if failed or DirAccess.rename_absolute(temp, path) != OK:
		DirAccess.remove_absolute(temp)
		# the other game of a race renamed its id in first (a rename does not replace a file on every system): that one is the install's
		if not failed and FileAccess.file_exists(path):
			return _read_install(path)
		return {"error": "cannot save the install id in %s" % ProjectSettings.globalize_path(path)}
	return _read_install(path)


static func _read_install(path: String) -> Dictionary:
	var where := ProjectSettings.globalize_path(path)
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null:
		return {"error": "cannot read the saved install id in %s" % where}
	var id := file.get_as_text().strip_edges()
	if file.get_error() != OK or RegEx.create_from_string("^[0-9a-f]{32}$").search(id) == null:
		return {"error": "the saved install id in %s is damaged: delete that file and press Online again (a new id is made)" % where}
	return {"install": id}


## ONLINEMENU-1 (owner 2026-10-20: a hardware id for bans only): the machine id that is hashed: Windows' MachineGuid
## (HKLM\SOFTWARE\Microsoft\Cryptography), macOS's IOPlatformUUID and Linux's /etc/machine-id (Godot's OS.get_unique_id reads those two).
## Only the salted one-way hash leaves this PC; it is never shown to players or peers. "" when it cannot be read (then no device is sent)
static func machine_id() -> String:
	if OS.get_name() == "Windows":
		var out: Array = []
		if OS.execute(OS.get_environment("SystemRoot").path_join("System32/reg.exe"), ["query", "HKLM\\SOFTWARE\\Microsoft\\Cryptography", "/v", "MachineGuid"], out, true) != 0 or out.is_empty():
			return ""
		var found := RegEx.create_from_string("MachineGuid\\s+REG_SZ\\s+([0-9A-Fa-f-]{36})").search(str(out[0]))
		return found.get_string(1).to_lower() if found != null else ""
	return OS.get_unique_id()


static func device_hash(machine_id: String) -> String:
	if machine_id.is_empty():
		return ""
	var bytes := "openbfme-device-v1".to_utf8_buffer()
	bytes.append(0)
	bytes.append_array(machine_id.to_utf8_buffer())
	var hash := HashingContext.new()
	if hash.start(HashingContext.HASH_SHA256) != OK or hash.update(bytes) != OK:
		return ""
	return hash.finish().hex_encode()


## lane JOINVER-1 (the owner, 2026-10-23): what a create / join tells the lobby about this build beside the sign-in's client (version, system): the
## build day and whether this is an official release: a packaged game (the release key, scripts/release/release_trust.gd) of a release tag (no
## development describe, nothing uncommitted). The other player reads it when a join is refused for a different build; the lobby trusts nothing of it
static func build_info() -> Dictionary:
	if not ClassDB.class_exists("ReleaseInfo"):
		return {}
	var info: RefCounted = ClassDB.instantiate("ReleaseInfo")
	var version := str(info.version())
	var packaged := not str(load("res://scripts/release/release_trust.gd").RELEASE_KEY).is_empty()
	var tag := RegEx.create_from_string("^v\\d+\\.\\d+\\.\\d+(-(alpha|beta|preview|nightly)\\.\\d+)?$").search(version) != null
	return {"date": str(info.build_date()), "official": packaged and tag}


static func identity() -> Dictionary:
	var result := install_id()
	if result.has("error"):
		return result
	if not ClassDB.class_exists("ReleaseInfo"):
		return {"error": "cannot read the client build version: extension unavailable"}
	var info: RefCounted = ClassDB.instantiate("ReleaseInfo")
	result["client"] = {"platform": OS.get_name().to_lower(), "version": info.version()}
	var device := device_hash(machine_id())
	if not device.is_empty():
		result["device"] = device
	return result
