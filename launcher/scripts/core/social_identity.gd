extends RefCounted

const GameOptions := preload("res://scripts/core/game_options.gd")
const L10n := preload("res://scripts/l10n/l10n.gd")

const INSTALL_FILE := "install_id.txt"
const MAX_INSTALL_FILE_BYTES := 34
const INSTALL_ID_PATTERN := "^[0-9a-f]{32}$"


static func identity(version: String, game_dir: String = "") -> Dictionary:
	if version.is_empty() or version.length() > 64 or version.strip_edges() != version:
		return {"error": L10n.t("launcher.social_identity.version_invalid")}
	for c in version:
		var code := c.unicode_at(0)
		if code < 0x20 or code == 0x7f:
			return {"error": L10n.t("launcher.social_identity.version_invalid")}

	var directory := game_dir if game_dir != "" else GameOptions.game_user_dir()
	if directory.is_empty() or not directory.is_absolute_path():
		return {"error": L10n.t("launcher.social_identity.install_unavailable")}
	var make_dir_error := DirAccess.make_dir_recursive_absolute(directory)
	if make_dir_error != OK and not DirAccess.dir_exists_absolute(directory):
		return {"error": L10n.t("launcher.social_identity.install_unavailable")}

	var path := directory.path_join(INSTALL_FILE)
	var locked := _locked_install(path, directory)
	if not locked.ok:
		return {"error": locked.error, "code": locked.get("code", "")}
	var install: String = locked.install

	var platform := OS.get_name().to_lower()
	if platform.is_empty() or platform.length() > 32:
		return {"error": L10n.t("launcher.social_identity.platform_invalid")}
	var result := {"install": install, "client": {"platform": platform, "version": version}}
	var device := _device_hash(machine_id())
	if device != "":
		result["device"] = device
	return result


static func _locked_install(path: String, directory: String) -> Dictionary:
	var lock_path := directory.path_join(".install_id.lock")
	if DirAccess.make_dir_absolute(lock_path) != OK:
		return {"ok": false, "error": L10n.t("launcher.social_identity.shared_unavailable"), "code": "identity_unavailable"}

	var result := _read_install(path)
	if result.ok and not result.found:
		result = _create_install_locked(path)
	if not result.ok:
		DirAccess.remove_absolute(lock_path)
		return result
	var release_error := DirAccess.remove_absolute(lock_path)
	if release_error != OK:
		return {"ok": false, "error": L10n.t("launcher.social_identity.shared_unavailable"), "code": "identity_unavailable"}
	return result


static func _read_install(path: String) -> Dictionary:
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null:
		if FileAccess.get_open_error() == ERR_FILE_NOT_FOUND:
			return {"ok": true, "found": false}
		return {"ok": false, "error": L10n.t("launcher.social_identity.saved_unreadable")}

	var length := file.get_length()
	if length < 1 or length > MAX_INSTALL_FILE_BYTES:
		file.close()
		return {"ok": false, "error": L10n.t("launcher.social_identity.saved_invalid")}
	var bytes := file.get_buffer(length)
	var read_error := file.get_error()
	file.close()
	if read_error != OK or bytes.size() != length:
		return {"ok": false, "error": L10n.t("launcher.social_identity.saved_unreadable")}

	var value := bytes.get_string_from_ascii()
	if value.ends_with("\r\n"):
		value = value.substr(0, value.length() - 2)
	elif value.ends_with("\n"):
		value = value.substr(0, value.length() - 1)
	if RegEx.create_from_string(INSTALL_ID_PATTERN).search(value) == null:
		return {"ok": false, "error": L10n.t("launcher.social_identity.saved_invalid")}
	return {"ok": true, "found": true, "install": value}


static func _create_install_locked(path: String) -> Dictionary:
	var bytes := Crypto.new().generate_random_bytes(16)
	if bytes.size() != 16:
		return {"ok": false, "error": L10n.t("launcher.social_identity.generate_failed")}
	var candidate := bytes.hex_encode()
	var file := FileAccess.open(path, FileAccess.WRITE)
	if file == null:
		return {"ok": false, "error": L10n.t("launcher.social_identity.save_failed")}
	file.store_string(candidate + "\n")
	file.flush()
	var write_error := file.get_error()
	file.close()
	if write_error != OK:
		return {"ok": false, "error": L10n.t("launcher.social_identity.save_failed")}

	var saved := _read_install(path)
	if not saved.ok or not saved.found or saved.install != candidate:
		return {"ok": false, "error": L10n.t("launcher.social_identity.save_unsafe")}
	return saved


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


static func _device_hash(machine_id: String) -> String:
	if machine_id.is_empty():
		return ""
	var bytes := "openbfme-device-v1".to_utf8_buffer()
	bytes.append(0)
	bytes.append_array(machine_id.to_utf8_buffer())
	var hash := HashingContext.new()
	if hash.start(HashingContext.HASH_SHA256) != OK or hash.update(bytes) != OK:
		return ""
	return hash.finish().hex_encode()
