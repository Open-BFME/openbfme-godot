extends RefCounted

const Manifest := preload("res://scripts/core/manifest.gd")
const Log := preload("res://scripts/core/launcher_log.gd")
const Semver := preload("res://scripts/core/semver.gd")
const Archive := preload("res://scripts/core/archive.gd")
const L10n := preload("res://scripts/l10n/l10n.gd")

const ROOT_FILES := ["LICENSE", "NOTICE", "README-MAC.txt", "VERSION"]
const SWAP_REL := "OpenBFME Launcher.app/Contents/MacOS/openbfme-bundle-swap"
const LAUNCHER_REL := "OpenBFME Launcher.app/Contents/MacOS/OpenBFME Launcher"
const JOURNAL := "macos-update.json"
const RECEIPT_PREFIX := "macos-update-receipt-"
const REQUIRED_STATE_PREFIX := "macos-update-required-"
const REJECTED_PREFIX := "macos-update-rejected-"
const HELPER_ROOT := "macos-update/helper"
const LOCK_ROOT := "macos-update.lock"
const CONFIRM_TIMEOUT_MS := 60000
const MAX_ATTEMPTS := 3
const JOURNAL_SCHEMA := 3
const JOURNAL_STATES := ["staged", "swapping", "swapped", "confirmed", "rolled-back", "failed", "deferred", "rollback-failed",
	"cleaning-confirmed", "cleaning-rolled-back", "retiring-confirmed", "retiring-rolled-back", "retiring-damaged"]

var updater: RefCounted
static var kill_at := ""


func _init(owner: RefCounted) -> void:
	updater = owner


static func journal_path() -> String:
	return OS.get_user_data_dir().path_join(JOURNAL)


static func _helper_transaction_dir(txid: String) -> String:
	return OS.get_user_data_dir().path_join(HELPER_ROOT).path_join(txid)


static func _helper_app(txid: String) -> String:
	return _helper_transaction_dir(txid).path_join("OpenBFME Launcher.app")


static func _helper_swap(txid: String) -> String:
	return _helper_app(txid).path_join("Contents/MacOS/openbfme-bundle-swap")


static func _helper_launcher(txid: String) -> String:
	return _helper_app(txid).path_join("Contents/MacOS/OpenBFME Launcher")


static func _root_version_key(root: String, version: String) -> String:
	var hash := HashingContext.new()
	hash.start(HashingContext.HASH_SHA256)
	hash.update((root.simplify_path() + "\n" + version).to_utf8_buffer())
	return hash.finish().hex_encode()


static func _version_for_root(root: String) -> String:
	var bytes := Manifest.read_limited(root.path_join("VERSION"), 4096)
	if bytes.is_empty():
		return ""
	var first_line := bytes.get_string_from_utf8().get_slice("\n", 0).strip_edges()
	if not first_line.begins_with("OpenBFME "):
		return ""
	var version := first_line.trim_prefix("OpenBFME ").get_slice(" ", 0)
	return version if Semver.is_valid(version) else ""


static func receipt_path(root: String, version: String) -> String:
	return OS.get_user_data_dir().path_join(RECEIPT_PREFIX + _root_version_key(root, version) + ".json")


static func required_state_path(root: String, version: String) -> String:
	return OS.get_user_data_dir().path_join(REQUIRED_STATE_PREFIX + _root_version_key(root, version) + ".json")


static func _rejected_path(root: String, version: String) -> String:
	return OS.get_user_data_dir().path_join(REJECTED_PREFIX + _root_version_key(root, version) + ".json")


static func _rejected_attempts(root: String, version: String) -> int:
	var path := _rejected_path(root, version)
	if not FileAccess.file_exists(path):
		return 0
	var state := _read_json(path)
	if state.keys().size() != 3 or state.get("root", "") != root or state.get("version", "") != version or \
			not _json_int(state.get("attempts", -1)) or int(state.attempts) > MAX_ATTEMPTS:
		return MAX_ATTEMPTS
	return int(state.attempts)


static func _record_rejected(updater: RefCounted, journal: Dictionary) -> bool:
	var root := String(journal.root)
	var version := String(journal.version)
	var attempts := mini(MAX_ATTEMPTS, _rejected_attempts(root, version) + 1)
	var saved := _write_json(_rejected_path(root, version),
		{"root": root, "version": version, "attempts": attempts}, updater, journal)
	if not saved:
		Log.line("LAUNCHER could not persist the rejected Mac update version %s" % version)
	return saved


static func _finish_rollback(updater: RefCounted, journal: Dictionary) -> String:
	if not _journal_state(updater, journal, "rolled-back"):
		return L10n.t("launcher.macos_update.rolled_back_state_could_not")
	if not _record_rejected(updater, journal):
		return L10n.t("launcher.macos_update.rejected_update_attempt_could_not")
	return _cleanup_transaction(updater, journal, false)


static func _read_json(path: String) -> Dictionary:
	if not FileAccess.file_exists(path):
		return {}
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null:
		return {}
	if file.get_length() > Manifest.MAX_BYTES * 2:
		file.close()
		return {}
	var value = JSON.parse_string(file.get_as_text())
	file.close()
	return value if typeof(value) == TYPE_DICTIONARY else {}


static func _read_transaction() -> Dictionary:
	var path := journal_path()
	if not FileAccess.file_exists(path):
		return {"exists": false, "ok": true, "journal": {}, "error": ""}
	if _is_link(path):
		return {"exists": true, "ok": false, "journal": {}, "error": L10n.t("launcher.macos_update.existing_mac_update_journal_symbolic")}
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null or file.get_length() > Manifest.MAX_BYTES * 3:
		if file != null:
			file.close()
		return {"exists": true, "ok": false, "journal": {}, "error": L10n.t("launcher.macos_update.existing_mac_update_journal_cannot")}
	var value = JSON.parse_string(file.get_as_text())
	file.close()
	if typeof(value) != TYPE_DICTIONARY:
		return {"exists": true, "ok": false, "journal": {}, "error": L10n.t("launcher.macos_update.existing_mac_update_journal_corrupt")}
	var error := _validate_journal(value)
	return {"exists": true, "ok": error == "", "journal": value, "error": error}


static func _json_int(value) -> bool:
	return (typeof(value) == TYPE_INT or typeof(value) == TYPE_FLOAT) and \
		float(value) >= 0.0 and float(value) <= 9007199254740991.0 and float(value) == floor(float(value))


static func _valid_inventory(files: Variant) -> bool:
	if typeof(files) != TYPE_DICTIONARY or files.is_empty():
		return false
	for path in files:
		if typeof(path) != TYPE_STRING or String(path).is_absolute_path() or \
				String(path).begins_with("../") or String(path).contains("/../") or String(path).contains("\\"):
			return false
		var entry = files[path]
		if typeof(entry) != TYPE_DICTIONARY or entry.keys().size() != 2 or \
				not entry.has("size") or not entry.has("sha256") or not _json_int(entry.size) or \
				typeof(entry.sha256) != TYPE_STRING or RegEx.create_from_string("^[0-9a-f]{64}$").search(entry.sha256) == null:
			return false
	return true


static func _validate_journal(journal: Dictionary) -> String:
	var wanted := ["schema", "state", "root", "candidate", "version", "old_version", "old_files",
		"new_files", "manifest", "signature", "asset_sha256", "helper", "helper_launcher", "txid",
		"apply_pid", "attempts", "user_pick"]
	var keys := journal.keys()
	keys.sort()
	wanted.sort()
	if keys != wanted:
		return L10n.t("launcher.macos_update.existing_mac_update_journal_has")
	if not _json_int(journal.schema) or int(journal.schema) != JOURNAL_SCHEMA or \
			typeof(journal.state) != TYPE_STRING or not journal.state in JOURNAL_STATES or \
			typeof(journal.root) != TYPE_STRING or typeof(journal.candidate) != TYPE_STRING or \
			typeof(journal.version) != TYPE_STRING or typeof(journal.old_version) != TYPE_STRING or \
			not Semver.is_valid(journal.version) or not Semver.is_valid(journal.old_version) or \
			typeof(journal.manifest) != TYPE_STRING or typeof(journal.signature) != TYPE_STRING or \
			typeof(journal.asset_sha256) != TYPE_STRING or typeof(journal.helper) != TYPE_STRING or \
			typeof(journal.helper_launcher) != TYPE_STRING or typeof(journal.txid) != TYPE_STRING or \
			not _json_int(journal.apply_pid) or not _json_int(journal.attempts) or \
			int(journal.attempts) > MAX_ATTEMPTS or typeof(journal.user_pick) != TYPE_BOOL or \
			not _valid_inventory(journal.old_files) or not _valid_inventory(journal.new_files) or \
			RegEx.create_from_string("^[0-9a-f]{64}$").search(journal.asset_sha256) == null or \
			RegEx.create_from_string("^[0-9a-f]{128}$").search(journal.signature) == null or \
			RegEx.create_from_string("^[A-Za-z0-9._-]{1,80}$").search(journal.txid) == null:
		return L10n.t("launcher.macos_update.existing_mac_update_journal_has_invalid")
	var root := String(journal.root)
	var candidate := String(journal.candidate)
	var txid := String(journal.txid)
	if not root.is_absolute_path() or root != root.simplify_path() or \
			candidate != root.get_base_dir().path_join(".openbfme-update-" + txid) or \
			String(journal.helper) != _helper_swap(txid) or \
			String(journal.helper_launcher) != _helper_launcher(txid):
		return L10n.t("launcher.macos_update.existing_mac_update_journal_not")
	return ""


static func _write_json(path: String, value: Dictionary, updater: RefCounted, journal: Dictionary) -> bool:
	if _is_link(path) or not _canonical_directory(path.get_base_dir(), updater):
		return false
	var tmp := path + ".tmp-" + str(OS.get_process_id()) + "-" + str(Time.get_ticks_usec())
	if FileAccess.file_exists(tmp) or DirAccess.dir_exists_absolute(tmp) or _is_link(tmp):
		return false
	var file := FileAccess.open(tmp, FileAccess.WRITE)
	if file == null:
		return false
	file.store_string(JSON.stringify(value))
	file.flush()
	if file.get_error() != OK:
		file.close()
		DirAccess.remove_absolute(tmp)
		return false
	file.close()
	if DirAccess.rename_absolute(tmp, path) != OK:
		DirAccess.remove_absolute(tmp)
		return false
	var helper := String(journal.get("helper", ""))
	if helper == "":
		helper = _local_swap_helper()
	elif _verify_external_helper(updater, journal) != "":
		return false
	var output: Array = []
	return OS.execute(helper, ["--sync", path], output, true) == 0


static func _is_link(path: String) -> bool:
	var parent := DirAccess.open(path.get_base_dir())
	return parent != null and parent.is_link(path.get_file())


static func _remove_tree(path: String) -> void:
	if _is_link(path):
		DirAccess.remove_absolute(path)
		return
	if not DirAccess.dir_exists_absolute(path):
		return
	var dir := DirAccess.open(path)
	if dir == null:
		return
	dir.include_hidden = true
	for file in dir.get_files():
		DirAccess.remove_absolute(path.path_join(file))
	for child in dir.get_directories():
		var target := path.path_join(child)
		if dir.is_link(child):
			DirAccess.remove_absolute(target)
		else:
			_remove_tree(target)
	DirAccess.remove_absolute(path)


static func _local_swap_helper() -> String:
	var app := OS.get_executable_path()
	for _i in 3:
		app = app.get_base_dir()
	return app.path_join("Contents/MacOS/openbfme-bundle-swap")


static func _lock_status(updater: RefCounted) -> Dictionary:
	var helper := _local_swap_helper()
	var path := OS.get_user_data_dir().path_join(LOCK_ROOT)
	if not FileAccess.file_exists(helper) or _is_link(helper):
		return {"known": false, "locked": true, "owner_pid": 0, "token": ""}
	var output: Array = []
	var code := OS.execute(helper, ["--lock-status", path], output, true)
	if code == 0:
		return {"known": true, "locked": false, "owner_pid": 0, "token": ""}
	if code == 4:
		return {"known": true, "locked": false, "owner_pid": 0, "token": ""}
	if code != 3 or output.is_empty():
		return {"known": false, "locked": true, "owner_pid": 0, "token": ""}
	var fields := "\n".join(output).strip_edges().split(" ", false)
	if fields.size() != 4 or not fields[0].is_valid_int():
		return {"known": false, "locked": true, "owner_pid": 0, "token": ""}
	return {"known": true, "locked": true, "owner_pid": int(fields[0]), "token": fields[3]}


static func transaction_locked(updater: RefCounted) -> bool:
	return bool(_lock_status(updater).locked)


static func play_blocker(updater: RefCounted) -> String:
	var transaction := _read_transaction()
	if not transaction.exists:
		return ""
	if not transaction.ok:
		return transaction.error
	var journal: Dictionary = transaction.journal
	var root: String = String(updater.launcher_dir).simplify_path()
	if journal.root != root:
		return L10n.t("launcher.macos_update.existing_signed_mac_update_belongs")
	var trust_error := _transaction_trust_error(updater, journal)
	if trust_error != "":
		return trust_error
	var new_error := _verify_inventory(root, String(journal.version), journal.new_files, updater)
	if new_error == "":
		var receipt := signed_receipt(updater, root, String(journal.version))
		return "" if receipt.ok else String(receipt.error)
	var old_error := _verify_inventory(root, String(journal.old_version), journal.old_files, updater)
	if old_error == "" and journal.state in ["staged", "swapping", "deferred", "failed", "rolled-back",
			"cleaning-rolled-back", "retiring-rolled-back", "rollback-failed", "retiring-damaged"]:
		return ""
	return L10n.t("launcher.macos_update.installed_package_does_not_match")


static func _acquire_lock(updater: RefCounted, token: String) -> bool:
	var helper := _local_swap_helper()
	var path := OS.get_user_data_dir().path_join(LOCK_ROOT)
	if not FileAccess.file_exists(helper) or _is_link(helper):
		return false
	var output: Array = []
	var code := OS.execute(helper, ["--lock-acquire", path, str(OS.get_process_id()), token], output, true)
	if code == 0:
		return true
	var owner := _lock_status(updater)
	return owner.locked and owner.owner_pid == OS.get_process_id() and owner.token == token


static func process_under(updater: RefCounted, pid: int, root: String) -> bool:
	if pid <= 0:
		return false
	# MACPLAY-1: this launcher's own helper (a launcher without a package root plays versions/<v>/game too)
	var helper: String = _local_swap_helper()
	if not FileAccess.file_exists(helper) or _is_link(helper):
		return false
	var output: Array = []
	return OS.execute(helper, ["--process-under", str(pid), root], output, true) == 3


static func _pid_alive(helper: String, pid: int) -> bool:
	if pid <= 0 or not FileAccess.file_exists(helper) or _is_link(helper):
		return pid > 0
	var output: Array = []
	return OS.execute(helper, ["--pid-alive", str(pid)], output, true) != 1


static func _release_lock(token: String) -> void:
	var helper := _local_swap_helper()
	if not FileAccess.file_exists(helper) or _is_link(helper):
		Log.line("LAUNCHER the signed Mac update lock could not be released: helper missing")
		return
	var output: Array = []
	if OS.execute(helper, ["--lock-release", OS.get_user_data_dir().path_join(LOCK_ROOT),
			str(OS.get_process_id()), token], output, true) != 0:
		Log.line("LAUNCHER the signed Mac update lock could not be safely released")


static func _canonical_directory(path: String, updater: RefCounted) -> bool:
	if not path.is_absolute_path() or path != path.simplify_path():
		return false
	var current := "/"
	for component in path.trim_prefix("/").split("/", false):
		var parent := DirAccess.open(current)
		if parent == null or parent.is_link(component):
			return false
		current = current.path_join(component)
	if not DirAccess.dir_exists_absolute(path):
		return false
	var helper := _local_swap_helper()
	if not FileAccess.file_exists(helper):
		return true
	if _is_link(helper):
		return false
	var output: Array = []
	return OS.execute(helper, ["--canonical", path], output, true) == 0 and \
		"\n".join(output).strip_edges() == path


static func _sha256(path: String) -> String:
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null:
		return ""
	var hash := HashingContext.new()
	hash.start(HashingContext.HASH_SHA256)
	while file.get_position() < file.get_length():
		hash.update(file.get_buffer(1 << 20))
	return hash.finish().hex_encode()


static func _same_files(actual: Dictionary, expected: Dictionary) -> String:
	for path in expected:
		if not actual.has(path):
			return L10n.t("launcher.macos_update.missing_from_archive", {"path": path})
		if int(actual[path].size) != int(expected[path].size) or actual[path].sha256 != expected[path].sha256:
			return L10n.t("launcher.macos_update.does_not_match_signed_inventory", {"path": path})
	for path in actual:
		if not expected.has(path):
			return L10n.t("launcher.macos_update.not_in_signed_inventory", {"path": path})
	return ""


static func _capture_inventory(root: String) -> Dictionary:
	var files: Array = []
	var directories: Array = []
	var error := _scan(root, "", files, directories)
	if error != "":
		return {"ok": false, "error": error, "files": {}}
	var inventory := {}
	files.sort()
	for relative in files:
		var path := root.path_join(relative)
		var size := FileAccess.get_file_as_bytes(path).size()
		var digest := _sha256(path)
		if digest == "":
			return {"ok": false, "error": L10n.t("launcher.macos_update.could_not_hashed", {"path": relative}), "files": {}}
		inventory[relative] = {"size": size, "sha256": digest}
	return {"ok": true, "error": "", "files": inventory}


static func _verify_inventory(root: String, version: String, files: Dictionary, updater: RefCounted) -> String:
	if not Semver.is_valid(version) or _version_for_root(root) != version:
		return L10n.t("launcher.macos_update.package_version_does_not_match")
	var error := validate_tree(root, files, updater)
	if error == "":
		error = _verify_apps(root)
	return error


static func _validate_inventory_subset(root: String, files: Dictionary, updater: RefCounted) -> String:
	if _is_link(root) or FileAccess.file_exists(root):
		return L10n.t("launcher.macos_update.inactive_package_path_not_real")
	if not DirAccess.dir_exists_absolute(root):
		return ""
	if not _canonical_directory(root, updater):
		return L10n.t("launcher.macos_update.inactive_package_path_not_canonical")
	var actual_files: Array = []
	var actual_dirs: Array = []
	var error := _scan(root, "", actual_files, actual_dirs)
	if error != "":
		return error
	var expected_dirs := _expected_directories(files)
	for path in actual_dirs:
		if not path in expected_dirs:
			return L10n.t("launcher.macos_update.unexpected_directory_in_inactive_package", {"path": path})
	var subset := {}
	for path in actual_files:
		if not files.has(path):
			return L10n.t("launcher.macos_update.not_in_recorded_inactive_package", {"path": path})
		subset[path] = files[path]
	return Manifest.check_files(root, subset, [], false, true)  # MACFIX-1: Finder's own files, as _scan


static func _scan(root: String, relative: String, files: Array, directories: Array) -> String:
	var path := root.path_join(relative) if relative != "" else root
	var dir := DirAccess.open(path)
	if dir == null:
		return L10n.t("launcher.macos_update.cannot_read_package_directory", {"path": relative})
	dir.include_hidden = true
	for name in dir.get_files():
		if dir.is_link(name):
			return L10n.t("launcher.macos_update.symbolic_link", {"path": relative.path_join(name) if relative != "" else name})
		if Manifest.finder_metadata(name):
			continue   # MACFIX-1 (issue #47): Finder's own files are no part of a package (Manifest.finder_metadata)
		files.append(relative.path_join(name) if relative != "" else name)
	for name in dir.get_directories():
		if dir.is_link(name):
			return L10n.t("launcher.macos_update.symbolic_link", {"path": relative.path_join(name) if relative != "" else name})
		var child := relative.path_join(name) if relative != "" else name
		directories.append(child)
		var error := _scan(root, child, files, directories)
		if error != "":
			return error
	return ""


## MACPLAY-1 (Sol r1): "" when nothing inside the two apps below `root` is a symbolic link and no signed file of `files` (nor a folder on
## its way) is one: a link to identical bytes passes a hash check. Other entries of a player's own folder are not looked at.
static func link_error(root: String, files: Dictionary) -> String:
	for app in ["OpenBFME.app", "OpenBFME Launcher.app"]:
		if _is_link(root.path_join(app)):
			return L10n.t("launcher.macos_update.symbolic_link", {"path": app})
		if DirAccess.dir_exists_absolute(root.path_join(app)):
			var found: Array = []
			var directories: Array = []
			var error := _scan(root.path_join(app), "", found, directories)
			if error != "":
				return "%s: %s" % [app, error]
	for rel in files:
		var path := root
		for part in String(rel).split("/"):
			path = path.path_join(part)
			if _is_link(path):
				return L10n.t("launcher.macos_update.symbolic_link", {"path": rel})
	return ""


static func _expected_directories(files: Dictionary) -> Array:
	var out := {}
	for path in files:
		var parent: String = String(path).get_base_dir()
		while parent != "." and parent != "":
			out[parent] = true
			parent = parent.get_base_dir()
	var result: Array = out.keys()
	result.sort()
	return result


static func _validate_files_tree(root: String, files: Dictionary, updater: RefCounted = null) -> String:
	if not _canonical_directory(root, updater):
		return L10n.t("launcher.macos_update.package_root_not_canonical_real")
	var actual_files: Array = []
	var actual_dirs: Array = []
	var error := _scan(root, "", actual_files, actual_dirs)
	if error != "":
		return error
	actual_files.sort()
	actual_dirs.sort()
	var expected_files: Array = files.keys()
	expected_files.sort()
	if actual_files != expected_files:
		for name in expected_files:
			if not name in actual_files:
				return L10n.t("launcher.macos_update.missing", {"path": name})
		for name in actual_files:
			if not files.has(name):
				return L10n.t("launcher.macos_update.not_in_signed_package", {"path": name})
		return L10n.t("launcher.macos_update.package_files_do_not_match")
	var expected_dirs := _expected_directories(files)
	if actual_dirs != expected_dirs:
		for name in expected_dirs:
			if not name in actual_dirs:
				return L10n.t("launcher.macos_update.missing_app_directory", {"path": name})
		for name in actual_dirs:
			if not name in expected_dirs:
				return L10n.t("launcher.macos_update.unexpected_directory", {"path": name})
	return Manifest.check_files(root, files, [], false, true)  # MACFIX-1: Finder's own files, as _scan


static func validate_tree(root: String, files: Dictionary, updater: RefCounted = null) -> String:
	var error := _validate_files_tree(root, files, updater)
	if error != "":
		return error
	for name in ROOT_FILES:
		if not files.has(name):
			return L10n.t("launcher.macos_update.package_missing", {"path": name})
	if not files.has("OpenBFME.app/Contents/MacOS/OpenBFME") or not files.has(LAUNCHER_REL) or not files.has(SWAP_REL):
		return L10n.t("launcher.macos_update.package_missing_required_app_executable")
	return ""


static func validate_installed_root(root: String, updater: RefCounted = null) -> String:
	if not _canonical_directory(root, updater):
		return L10n.t("launcher.macos_update.install_root_not_canonical_real")
	if root.get_file().ends_with(".app") or root in ["/", "/Applications", "/System", "/Library", "/Users", "/Volumes"]:
		return L10n.t("launcher.macos_update.install_root_not_standalone_package")
	if not root.get_file().begins_with("openbfme-") or not root.get_file().ends_with("-macos-arm64"):
		return L10n.t("launcher.macos_update.install_root_not_generic_apple")
	for component in root.split("/", false):
		if component.ends_with(".app"):
			return L10n.t("launcher.macos_update.install_root_inside_app_bundle")
	var files: Array = []
	var directories: Array = []
	var error := _scan(root, "", files, directories)
	if error != "":
		return error
	var entries := {}
	for path in files + directories:
		entries[String(path).get_slice("/", 0)] = true
	var actual: Array = entries.keys()
	var expected := ["LICENSE", "NOTICE", "OpenBFME Launcher.app", "OpenBFME.app", "README-MAC.txt", "VERSION"]
	var sealed := expected + [Manifest.PAYLOAD, Manifest.PAYLOAD_SIG]  # MACPLAY-1: the release package carries its signed inventory
	actual.sort()
	expected.sort()
	sealed.sort()
	if actual != expected and actual != sealed:
		return L10n.t("launcher.macos_update.install_root_not_complete_standalone")
	for path in ROOT_FILES:
		if not FileAccess.file_exists(root.path_join(path)):
			return L10n.t("launcher.macos_update.install_root_missing", {"path": path})
	if not FileAccess.file_exists(root.path_join("OpenBFME.app/Contents/MacOS/OpenBFME")) or \
			not FileAccess.file_exists(root.path_join(LAUNCHER_REL)) or not FileAccess.file_exists(root.path_join(SWAP_REL)):
		return L10n.t("launcher.macos_update.install_root_missing_required_app")
	return _verify_apps(root)


static func _candidate_path_error(
	root: String, candidate: String, updater: RefCounted = null, txid := "", allow_missing := false
) -> String:
	if not _canonical_directory(root, updater):
		return L10n.t("launcher.macos_update.install_root_not_canonical_real")
	if not candidate.is_absolute_path() or candidate != candidate.simplify_path() or \
			candidate.get_base_dir() != root.get_base_dir() or \
			not candidate.get_file().begins_with(".openbfme-update-") or \
			(txid != "" and candidate.get_file() != ".openbfme-update-" + txid):
		return L10n.t("launcher.macos_update.staged_package_not_real_sibling")
	if _is_link(candidate) or FileAccess.file_exists(candidate):
		return L10n.t("launcher.macos_update.staged_package_not_real_sibling")
	if not DirAccess.dir_exists_absolute(candidate):
		return "" if allow_missing else L10n.t("launcher.macos_update.staged_package_not_real_sibling")
	if not _canonical_directory(candidate, updater):
		return L10n.t("launcher.macos_update.staged_package_not_real_sibling")
	return ""


static func _verified_journal_manifest(updater: RefCounted, journal: Dictionary) -> Dictionary:
	var bytes := Marshalls.base64_to_raw(String(journal.get("manifest", "")))
	var sig := String(journal.get("signature", "")).hex_decode()
	var verified := Manifest.verify(bytes, sig, updater.info.release_keys,
		{"repo": updater.info.repo, "tag": journal.get("version", "")}, "macos", updater.info.nightly_keys)
	if not verified.ok or verified.manifest.assets.game.is_empty():
		return {"ok": false, "error": L10n.t("launcher.macos_update.signed_mac_update_journal_manifest")}
	var asset: Dictionary = verified.manifest.assets.game["macos-arm64"]
	var recorded_files: Dictionary = journal.get("new_files", {})
	if asset.sha256 != journal.get("asset_sha256", "") or _same_files(asset.files, recorded_files) != "" or \
			verified.manifest.version != journal.get("version", ""):
		return {"ok": false, "error": L10n.t("launcher.macos_update.signed_mac_update_journal_has")}
	return {"ok": true, "error": "", "manifest": verified.manifest, "asset": asset}


static func _verify_external_helper(updater: RefCounted, journal: Dictionary) -> String:
	var verified := _verified_journal_manifest(updater, journal)
	if not verified.ok:
		return verified.error
	var txid := String(journal.get("txid", ""))
	var helper_dir := _helper_transaction_dir(txid)
	var helper_app := _helper_app(txid)
	var helper := _helper_swap(txid)
	var helper_launcher := _helper_launcher(txid)
	if journal.get("helper", "") != helper or journal.get("helper_launcher", "") != helper_launcher or \
			_is_link(helper_dir) or _is_link(helper_app) or _is_link(helper):
		return L10n.t("launcher.macos_update.signed_external_update_helper_path")
	if not _canonical_directory(helper_dir, updater) or not _canonical_directory(helper_app, updater):
		return L10n.t("launcher.macos_update.external_launcher_helper_not_canonical")
	var transaction_files := _signed_helper_files(verified.asset.files)
	var error := _validate_files_tree(helper_dir, transaction_files, updater)
	if error == "":
		error = _codesign(helper_app)
	return error


static func _signed_helper_files(files: Dictionary) -> Dictionary:
	var transaction_files := {}
	for path in files:
		if String(path).begins_with("OpenBFME Launcher.app/"):
			transaction_files[path] = files[path]
	return transaction_files


static func _retiring_state(state: String) -> bool:
	return state in ["retiring-confirmed", "retiring-rolled-back", "retiring-damaged"]


static func _transaction_trust_error(updater: RefCounted, journal: Dictionary) -> String:
	var error := _validate_journal(journal)
	if error != "":
		return error
	var verified := _verified_journal_manifest(updater, journal)
	if not verified.ok:
		return verified.error
	var helper_dir := _helper_transaction_dir(String(journal.txid))
	if _retiring_state(String(journal.state)):
		if not FileAccess.file_exists(helper_dir) and not DirAccess.dir_exists_absolute(helper_dir) and not _is_link(helper_dir):
			return ""
		var helper_error := _verify_external_helper(updater, journal)
		if helper_error == "":
			return ""
		return _validate_inventory_subset(helper_dir, _signed_helper_files(verified.asset.files), updater)
	return _verify_external_helper(updater, journal)


## The app's code seal: its code directory, every sealed resource (a file added, removed or changed anywhere in the app is refused), the
## executables' own signatures, nested code (--deep) and the strict symlink rule. MACFIX-1 (issue #47): --strict=symlinks, not a bare
## --strict, whose "sideband" check refuses an app that carries Finder information or a resource fork as extended attributes
## ("resource fork, Finder information, or similar detritus not allowed"). Finder, iCloud Drive's file provider and unzip tools add those
## to folders the player owns (com.apple.FinderInfo, com.apple.fileprovider.fpfs#P, com.apple.provenance): they are no part of the
## seal, never run, and the bytes of every sealed file are still checked here and against the signed inventory (payload.json / the
## signed manifest) by the callers. Measured on macOS 26.6: a bare --strict and --strict=symlinks refuse the same added, removed,
## changed and AppleDouble files; only the extended attributes differ.
static func _codesign(app: String) -> String:
	var output: Array = []
	var code := OS.execute("codesign", ["--verify", "--deep", "--strict=symlinks", app], output, true)
	return "" if code == 0 else L10n.t("launcher.macos_update.codesign_verification_failed_for", {"app": app.get_file(), "output": "\n".join(output).strip_edges()})


static func _verify_apps(root: String) -> String:
	for app in ["OpenBFME.app", "OpenBFME Launcher.app"]:
		var error := _codesign(root.path_join(app))
		if error != "":
			return error
	return ""


static func _clear_quarantine(path: String) -> bool:
	var output: Array = []
	var code := OS.execute("xattr", ["-p", "com.apple.quarantine", path], output, true)
	if code == 0:
		output.clear()
		if OS.execute("xattr", ["-d", "com.apple.quarantine", path], output, true) != 0:
			return false
	return true


static func _clear_tree_quarantine(root: String) -> bool:
	var files: Array = []
	var dirs: Array = []
	if _scan(root, "", files, dirs) != "":
		return false
	for path in files:
		if not _clear_quarantine(root.path_join(path)):
			return false
	for path in dirs:
		if not _clear_quarantine(root.path_join(path)):
			return false
	return _clear_quarantine(root)


static func _copy_launcher_helper(candidate: String, files: Dictionary, updater: RefCounted, txid: String) -> Dictionary:
	var source := candidate.path_join("OpenBFME Launcher.app")
	var helper_root := OS.get_user_data_dir().path_join(HELPER_ROOT)
	var target_dir := _helper_transaction_dir(txid)
	var target := _helper_app(txid)
	var support := OS.get_user_data_dir().path_join("macos-update")
	if _is_link(support) or _is_link(helper_root) or _is_link(target_dir):
		return {"ok": false, "error": L10n.t("launcher.macos_update.external_helper_directory_contains_symbolic")}
	if not DirAccess.dir_exists_absolute(support) and DirAccess.make_dir_absolute(support) != OK:
		return {"ok": false, "error": L10n.t("launcher.macos_update.cannot_create_external_helper_directory")}
	if not DirAccess.dir_exists_absolute(helper_root) and DirAccess.make_dir_absolute(helper_root) != OK:
		return {"ok": false, "error": L10n.t("launcher.macos_update.cannot_create_external_helper_directory")}
	if FileAccess.file_exists(target_dir) or DirAccess.dir_exists_absolute(target_dir):
		return {"ok": false, "error": L10n.t("launcher.macos_update.transaction_s_external_helper_path")}
	if DirAccess.make_dir_absolute(target_dir) != OK:
		return {"ok": false, "error": L10n.t("launcher.macos_update.cannot_create_external_helper_directory")}
	if _is_link(target):
		return {"ok": false, "error": L10n.t("launcher.macos_update.external_helper_app_path_symbolic")}
	var output: Array = []
	if OS.execute("ditto", ["--rsrc", source, target], output, true) != 0:
		return {"ok": false, "error": L10n.t("launcher.macos_update.cannot_copy_signed_launcher_helper", {"output": "\n".join(output).strip_edges()})}
	var subset := {}
	for path in files:
		if String(path).begins_with("OpenBFME Launcher.app/"):
			subset[String(path).trim_prefix("OpenBFME Launcher.app/")] = files[path]
	var why := _validate_files_tree(target, subset, updater)
	if why != "":
		return {"ok": false, "error": L10n.t("launcher.macos_update.external_launcher_helper_copy_failed", {"why": why})}
	why = _codesign(target)
	if why != "":
		return {"ok": false, "error": why}
	if not _clear_tree_quarantine(target) or _codesign(target) != "":
		return {"ok": false, "error": L10n.t("launcher.macos_update.verified_external_launcher_helper_could")}
	return {"ok": true, "path": target.path_join("Contents/MacOS/OpenBFME Launcher"),
		"swap": target.path_join("Contents/MacOS/openbfme-bundle-swap")}


func install(release: Dictionary, user_pick: bool) -> Dictionary:
	var txid := "%d-%d-%08x" % [OS.get_process_id(), Time.get_ticks_usec(), randi()]
	if not _acquire_lock(updater, txid):
		return {"ok": false, "error": L10n.t("launcher.macos_update.another_launcher_installing_or_applying"), "version": ""}
	updater.macos_transaction = true
	var result := _install(release, user_pick, txid)
	updater.macos_transaction = false
	_release_lock(txid)
	return result


func _install(release: Dictionary, user_pick: bool, txid: String) -> Dictionary:
	if updater.launcher_dir == "":
		return {"ok": false, "error": L10n.t("launcher.macos_update.launcher_has_no_package_root"), "version": ""}
	var existing := _read_transaction()
	if existing.exists:
		return {"ok": false, "error": L10n.t("launcher.macos_update.previous_signed_mac_update_transaction"), "version": ""}
	var mm: Dictionary = updater.release_manifest(release)
	if not mm.ok:
		return {"ok": false, "error": mm.error, "version": ""}
	if not mm.get("macos_sidecar", false):
		return {"ok": false, "error": L10n.t("launcher.macos_update.verified_release_has_no_signed"), "version": ""}
	if mm.get("no_entry", false):
		return {"ok": false, "error": L10n.t("launcher.macos_update.release_has_no_macos_package"), "version": ""}
	var manifest: Dictionary = mm.manifest
	var asset: Dictionary = manifest.assets.game["macos-arm64"]
	var current: String = updater.release_version()
	if not user_pick and Semver.is_valid(current) and Semver.compare(manifest.version, current) <= 0:
		return {"ok": false, "error": L10n.t("launcher.macos_update.not_newer_than_package", {"version": manifest.version, "current": current}), "version": ""}
	var root: String = updater.launcher_dir.simplify_path()
	if not _canonical_directory(root, updater):
		return {"ok": false, "error": L10n.t("launcher.macos_update.installed_mac_package_or_one"), "version": ""}
	if signed_required(updater, root):
		var receipt := signed_receipt(updater, root)
		if not receipt.ok:
			return {"ok": false, "error": L10n.t("launcher.macos_update.installed_signed_mac_package_cannot", {"why": receipt.error}), "version": ""}
	var root_error := validate_installed_root(root, updater)
	if root_error != "":
		return {"ok": false, "error": L10n.t("launcher.macos_update.installed_mac_package_cannot_updated", {"why": root_error}), "version": ""}
	var attempts := _rejected_attempts(root, manifest.version)
	if not user_pick and attempts >= MAX_ATTEMPTS:
		return {"ok": false, "error": L10n.t("launcher.macos_update.automatic_update_stopped_after_repeated", {"version": manifest.version}), "version": ""}
	var got: Dictionary = updater.fetch_asset(release, asset)
	if not got.ok:
		return {"ok": false, "error": got.error, "version": ""}
	var old_version := _version_for_root(root)
	if old_version != current:
		return {"ok": false, "error": L10n.t("launcher.macos_update.installed_package_version_does_not"), "version": ""}
	var old_inventory := _capture_inventory(root)
	if not old_inventory.ok:
		return {"ok": false, "error": L10n.t("launcher.macos_update.installed_package_inventory_could_not", {"why": old_inventory.error}), "version": ""}
	# MACPLAY-1 (Sol r1, P1): the inventory a rollback restores is the one signed data names, checked now and again before any rollback
	var old_trust := _old_trust_error(updater, {"root": root, "old_version": old_version, "old_files": old_inventory.files}, root)
	if old_trust != "":
		return {"ok": false, "error": L10n.t("launcher.macos_update.installed_mac_package_cannot_updated", {"why": old_trust}), "version": ""}
	var parent: String = root.get_base_dir()
	var candidate: String = parent.path_join(".openbfme-update-" + txid)
	if FileAccess.file_exists(candidate) or DirAccess.dir_exists_absolute(candidate) or _is_link(candidate):
		return {"ok": false, "error": L10n.t("launcher.macos_update.expected_mac_update_staging_path"), "version": ""}
	var extracted := Archive.extract(got.path, "zip", Manifest.top_folder(asset.name), candidate)
	if not extracted.ok:
		Log.line("LAUNCHER signed Mac staging failed; inspect %s: %s" % [candidate, extracted.error])
		return {"ok": false, "error": L10n.t("launcher.macos_update.mac_package_not_staged_old", {"why": extracted.error}), "version": ""}
	var differ := _same_files(extracted.files, asset.files)
	if differ == "" and _version_for_root(candidate) != manifest.version:
		differ = L10n.t("launcher.macos_update.candidate_version_does_not_match")
	if differ == "":
		differ = validate_tree(candidate, asset.files, updater)
	if differ == "":
		differ = _verify_apps(candidate)
	var helper_copy: Dictionary = {}
	if differ == "":
		helper_copy = _copy_launcher_helper(candidate, asset.files, updater, txid)
		if not helper_copy.ok:
			differ = helper_copy.error
	if differ == "" and not _clear_tree_quarantine(candidate):
		differ = L10n.t("launcher.macos_update.quarantine_could_not_cleared_on")
	if differ == "":
		differ = _verify_apps(candidate)
	if differ != "":
		Log.line("LAUNCHER signed Mac update candidate was refused and preserved for diagnosis: " + differ)
		DirAccess.remove_absolute(got.path)
		return {"ok": false, "error": L10n.t("launcher.macos_update.mac_package_not_installed_old", {"why": differ}), "version": ""}
	var journal := {
		"schema": JOURNAL_SCHEMA,
		"state": "staged",
		"root": root,
		"candidate": candidate,
		"version": manifest.version,
		"old_version": old_version,
		"old_files": old_inventory.files,
		"new_files": asset.files,
		"manifest": Marshalls.raw_to_base64(mm.bytes),
		"signature": mm.sig.hex_encode(),
		"asset_sha256": asset.sha256,
		"helper": helper_copy.swap,
		"helper_launcher": helper_copy.path,
		"txid": txid,
		"apply_pid": 0,
		"attempts": 0 if user_pick else attempts,
		"user_pick": user_pick,
	}
	var journal_error := _validate_journal(journal)
	if journal_error != "":
		Log.line("LAUNCHER signed Mac update journal could not be validated: " + journal_error)
		DirAccess.remove_absolute(got.path)
		return {"ok": false, "error": journal_error, "version": ""}
	if not _write_json(journal_path(), journal, updater, journal):
		Log.line("LAUNCHER signed Mac update journal persistence is uncertain; candidate preserved at " + candidate)
		DirAccess.remove_absolute(got.path)
		return {"ok": false, "error": L10n.t("launcher.macos_update.durable_mac_update_journal_could"), "version": ""}
	DirAccess.remove_absolute(got.path)
	Log.line("LAUNCHER staged signed macOS package %s (%d files); restart the launcher to exchange both apps" % [manifest.version, asset.files.size()])
	return {"ok": true, "error": "", "version": manifest.version}


func stage_self_update(release: Dictionary) -> Dictionary:
	var journal := _read_json(journal_path())
	if journal.is_empty() or journal.get("root", "") != updater.launcher_dir.simplify_path():
		return {"ok": true, "error": "", "staged": "", "why": L10n.t("launcher.macos_update.signed_mac_package_already_current")}
	if journal.get("version", "") != release.tag:
		return {"ok": false, "error": L10n.t("launcher.macos_update.staged_mac_package_does_not"), "staged": ""}
	return {"ok": true, "error": "", "staged": release.tag}


static func signed_receipt(updater: RefCounted, root: String, package_version := "") -> Dictionary:
	var version := package_version
	if version == "":
		version = updater.release_version() if root.simplify_path() == updater.launcher_dir.simplify_path() else _version_for_root(root)
	if not Semver.is_valid(version):
		return {"ok": false, "error": L10n.t("launcher.macos_update.installed_mac_package_version_invalid")}
	if not _canonical_directory(root.simplify_path(), updater) or _version_for_root(root) != version:
		return {"ok": false, "error": L10n.t("launcher.macos_update.installed_mac_package_path_or")}
	var receipt := _read_json(receipt_path(root, version))
	if receipt.is_empty() or receipt.keys().size() != 5:
		return {"ok": false, "error": L10n.t("launcher.macos_update.signed_macos_update_receipt_missing")}
	if receipt.get("root", "") != root.simplify_path():
		return {"ok": false, "error": L10n.t("launcher.macos_update.signed_macos_update_receipt_belongs_different")}
	if receipt.get("version", "") != version:
		return {"ok": false, "error": L10n.t("launcher.macos_update.signed_macos_update_receipt_belongs")}
	var bytes := Marshalls.base64_to_raw(String(receipt.get("manifest", "")))
	var sig := String(receipt.get("signature", "")).hex_decode()
	var v := Manifest.verify(bytes, sig, updater.info.release_keys,
		{"repo": updater.info.repo, "tag": receipt.get("version", "")}, "macos", updater.info.nightly_keys)
	if not v.ok or v.manifest.assets.game.is_empty():
		return {"ok": false, "error": (L10n.t("launcher.macos_update.signed_macos_update_receipt_invalid", {"why": v.error}) if not v.ok else L10n.t("launcher.macos_update.signed_macos_update_receipt_invalid_no"))}
	var asset: Dictionary = v.manifest.assets.game["macos-arm64"]
	if asset.sha256 != receipt.get("asset_sha256", ""):
		return {"ok": false, "error": L10n.t("launcher.macos_update.signed_macos_update_receipt_has")}
	var error := validate_tree(root, asset.files, updater)
	if error == "":
		error = _verify_apps(root)
	if error != "":
		return {"ok": false, "error": error}
	return {"ok": true, "error": "", "manifest": v.manifest, "files": asset.files}


static func signed_required(updater: RefCounted, package_root := "", package_version := "") -> bool:
	var root: String = (updater.launcher_dir if package_root == "" else package_root).simplify_path()
	var version := package_version
	if version == "":
		version = updater.release_version() if root == updater.launcher_dir.simplify_path() else _version_for_root(root)
	if not Semver.is_valid(version):
		return true
	var receipt_pathname := receipt_path(root, version)
	if FileAccess.file_exists(receipt_pathname):
		return true
	var state_pathname := required_state_path(root, version)
	return FileAccess.file_exists(state_pathname)


static func _write_receipt(updater: RefCounted, journal: Dictionary) -> String:
	var root: String = String(journal.root).simplify_path()
	if updater.info.error != "" or updater.release_version() != String(journal.version) or \
			_version_for_root(root) != String(journal.version):
		return L10n.t("launcher.macos_update.newly_launched_launcher_version_or")
	if root != updater.launcher_dir.simplify_path():
		return L10n.t("launcher.macos_update.confirmation_launcher_not_running_from")
	var bytes := Marshalls.base64_to_raw(String(journal.manifest))
	var sig := String(journal.signature).hex_decode()
	var v := Manifest.verify(bytes, sig, updater.info.release_keys, {"repo": updater.info.repo, "tag": journal.version}, "macos", updater.info.nightly_keys)
	if not v.ok or v.manifest.assets.game.is_empty():
		return L10n.t("launcher.macos_update.signed_mac_manifest_could_not")
	var files: Dictionary = v.manifest.assets.game["macos-arm64"].files
	var recorded_files: Dictionary = journal.get("new_files", {})
	if v.manifest.assets.game["macos-arm64"].sha256 != journal.get("asset_sha256", ""):
		return L10n.t("launcher.macos_update.signed_mac_package_digest_does")
	if _same_files(files, recorded_files) != "":
		return L10n.t("launcher.macos_update.signed_mac_confirmation_inventory_does")
	var error := validate_tree(root, files, updater)
	if error == "":
		error = _verify_apps(root)
	if error != "":
		return error
	var receipt := {
		"root": root, "version": journal.version, "manifest": journal.manifest,
		"signature": journal.signature, "asset_sha256": journal.asset_sha256,
	}
	if not _write_json(receipt_path(root, journal.version), receipt, updater, journal):
		return L10n.t("launcher.macos_update.root_bound_signed_macos_receipt")
	if not _write_json(required_state_path(root, journal.version),
			{"root": root, "version": journal.version, "required": true}, updater, journal):
		return L10n.t("launcher.macos_update.root_bound_signed_required_state")
	return ""


static func _journal_state(updater: RefCounted, journal: Dictionary, state: String) -> bool:
	var previous = journal.state
	journal.state = state
	if _write_json(journal_path(), journal, updater, journal):
		return true
	journal.state = previous
	return false


static func _arg(args: PackedStringArray, prefix: String) -> String:
	for arg in args:
		if arg.begins_with(prefix):
			return arg.trim_prefix(prefix)
	return ""


static func _launch(executable: String, args: PackedStringArray) -> int:
	return OS.create_process(executable, args)


static func _relaunch_args(updater: RefCounted) -> PackedStringArray:
	if updater.info.tests_allowed() and OS.get_environment("OPENBFME_TEST_MACOS_CONFIRM_EXIT") == "1":
		return PackedStringArray(["--headless", "--", "--version"])  # tests: no window on the Mac's screen (MACPLAY-1)
	return PackedStringArray()


## tests only (the callers' test points): this process, killed at once
static func kill_self() -> void:
	OS.execute("/bin/kill", ["-KILL", str(OS.get_process_id())])


static func test_point(updater: RefCounted, name: String) -> void:
	if updater.info.tests_allowed() and kill_at == name:
		Log.line("LAUNCHER test kill at Mac update " + name)
		OS.execute("/bin/kill", ["-KILL", str(OS.get_process_id())])


## tests only: at `name` (OPENBFME_TEST_MACOS_PAUSE_AT) this process writes <OPENBFME_TEST_MACOS_PAUSE_FILE>.waiting and waits up to 60 s
## for the test to create <file>.go (MACPLAY-1, Sol r2: a test changes the packages between two steps of one process)
static func test_pause(updater: RefCounted, name: String) -> void:
	var file := OS.get_environment("OPENBFME_TEST_MACOS_PAUSE_FILE")
	if not updater.info.tests_allowed() or OS.get_environment("OPENBFME_TEST_MACOS_PAUSE_AT") != name or file == "":
		return
	var marker := FileAccess.open(file + ".waiting", FileAccess.WRITE)
	if marker != null:
		marker.close()
	var started := Time.get_ticks_msec()
	while not FileAccess.file_exists(file + ".go") and Time.get_ticks_msec() - started < 60000:
		OS.delay_msec(100)


static func _confirm_timeout(updater: RefCounted) -> int:
	if updater.info.tests_allowed():
		var requested := OS.get_environment("OPENBFME_TEST_MACOS_CONFIRM_TIMEOUT_MS")
		if requested.is_valid_int():
			return clampi(int(requested), 250, CONFIRM_TIMEOUT_MS)
	return CONFIRM_TIMEOUT_MS


static func _start_apply(updater: RefCounted, journal: Dictionary, old_pid: int, force_rollback := false) -> bool:
	var error := _transaction_trust_error(updater, journal)
	if error != "":
		Log.line("LAUNCHER the signed external update helper was not started: " + error)
		return false
	var helper_launcher := String(journal.helper_launcher)
	var args := PackedStringArray(["--headless", "--", "--apply-macos-update=" + journal_path(),
		"--macos-old-pid=" + str(old_pid)])
	if force_rollback:
		args.append("--macos-force-rollback")
	var pid := _launch(helper_launcher, args)
	if pid > 0 and updater.info.tests_allowed():
		var marker := OS.get_environment("OPENBFME_TEST_MACOS_HELPER_LAUNCH_MARKER")
		if marker != "":
			var file := FileAccess.open(marker, FileAccess.WRITE)
			if file != null:
				file.store_string(str(pid))
				file.close()
	return pid > 0


static func _apply(updater: RefCounted, journal: Dictionary, args: PackedStringArray) -> Dictionary:
	if not _acquire_lock(updater, String(journal.txid)):
		return {"exit": true, "message": L10n.t("launcher.macos_update.signed_mac_update_already_in")}
	var result := _apply_locked(updater, journal, args)
	_release_lock(String(journal.txid))
	return result


static func _pair_error(updater: RefCounted, journal: Dictionary, root_is_new: bool) -> String:
	var root := String(journal.root)
	var candidate := String(journal.candidate)
	if _candidate_path_error(root, candidate, updater, String(journal.txid)) != "":
		return L10n.t("launcher.macos_update.transaction_paths_are_not_canonical")
	var error := _tree_error(updater, journal, root, root_is_new)
	if error == "":
		error = _tree_error(updater, journal, candidate, not root_is_new)
	if error != "":
		Log.line("LAUNCHER signed Mac root-pair validation failed: " + error)
	return error


## MACPLAY-1 (Sol r1 on DEVPORT-6, P1): one tree of a transaction, on its own: "" when `tree` holds the new package (`is_new`) exactly as
## the signed manifest of the journal names it, or the previous package exactly as SIGNED data names it (_old_trust_error: the journal's
## old_files are not signed by themselves), with valid code seals.
static func _tree_error(updater: RefCounted, journal: Dictionary, tree: String, is_new: bool) -> String:
	var version := String(journal.version) if is_new else String(journal.old_version)
	var files: Dictionary = journal.new_files if is_new else journal.old_files
	var error := _verify_inventory(tree, version, files, updater)
	if error == "" and not is_new:
		error = _old_trust_error(updater, journal, tree)
	return error


## MACPLAY-1 (Sol r1 on DEVPORT-6, P1): the previous package's inventory comes from the journal, a local file nothing signs: a locally
## changed (and re-signed ad hoc) old package with a journal edited to match passed every check, and a rollback started it. It is
## trusted only when signed data names exactly those files: the signed receipt of that root and version (a package an earlier update
## installed), or the release's signed payload inventory inside the tree itself (a package as it was unpacked). "" or why not.
static func _old_trust_error(updater: RefCounted, journal: Dictionary, tree: String) -> String:
	var version := String(journal.old_version)
	var files: Dictionary = journal.old_files
	var receipt_file := receipt_path(String(journal.root), version)
	if FileAccess.file_exists(receipt_file):
		var receipt := _read_json(receipt_file)
		var bytes := Marshalls.base64_to_raw(String(receipt.get("manifest", "")))
		var sig := String(receipt.get("signature", "")).hex_decode()
		var v := Manifest.verify(bytes, sig, updater.info.release_keys, {"repo": updater.info.repo, "tag": version}, "macos", updater.info.nightly_keys)
		if not v.ok or v.manifest.assets.game.is_empty() or receipt.get("root", "") != String(journal.root):
			return L10n.t("launcher.macos_update.previous_package_s_signed_receipt")
		if _same_files(files, v.manifest.assets.game["macos-arm64"].files) != "":
			return L10n.t("launcher.macos_update.previous_package_s_recorded_inventory_not")
		return ""
	var p := Manifest.read_bounded(tree.path_join(Manifest.PAYLOAD), Manifest.MAX_BYTES)
	var s := Manifest.read_bounded(tree.path_join(Manifest.PAYLOAD_SIG), 64)
	var v := Manifest.verify_payload(p.bytes, s.bytes, updater.info.release_keys, {"repo": updater.info.repo, "platform": "macos-arm64"}, updater.info.nightly_keys)
	if not v.ok:
		return L10n.t("launcher.macos_update.previous_package_has_no_signed", {"why": v.error})
	if v.payload.version != version:
		return L10n.t("launcher.macos_update.previous_package_s_signed_inventory", {"found": v.payload.version, "wanted": version})
	var want: Dictionary = v.payload.files.duplicate(true)
	for item in [[Manifest.PAYLOAD, p.bytes], [Manifest.PAYLOAD_SIG, s.bytes]]:
		var hc := HashingContext.new()
		hc.start(HashingContext.HASH_SHA256)
		hc.update(item[1])
		want[item[0]] = {"size": item[1].size(), "sha256": hc.finish().hex_encode()}
	if _same_files(files, want) != "":
		return L10n.t("launcher.macos_update.previous_package_s_recorded_inventory")
	return ""


static func _sync_parent(path: String, helper: String) -> bool:
	var output: Array = []
	return OS.execute(helper, ["--sync-parent", path], output, true) == 0


static func _active_root_error(updater: RefCounted, journal: Dictionary, root_is_new: bool) -> String:
	var root := String(journal.root)
	var version := String(journal.version) if root_is_new else String(journal.old_version)
	var error := _tree_error(updater, journal, root, root_is_new)
	if error != "":
		return error
	if root_is_new:
		var receipt := signed_receipt(updater, root, version)
		return "" if receipt.ok else String(receipt.error)
	if signed_required(updater, root, version):
		var receipt := signed_receipt(updater, root, version)
		return "" if receipt.ok else String(receipt.error)
	return ""


static func _helper_process_status(journal: Dictionary) -> Dictionary:
	if int(journal.apply_pid) == 0:
		# MACPLAY-1 (Sol r2): no helper of this transaction began the exchange (a damaged staged package retired before it); callers hold
		# the update lock, which a helper takes before any step, so none is at work
		return {"known": true, "active": false}
	var output: Array = []
	var helper := _local_swap_helper()
	if helper != String(journal.helper) and (not FileAccess.file_exists(helper) or _is_link(helper)):
		return {"known": false, "active": true}
	# MACPLAY-1 (Sol r3, P2): an interrupted retirement may have removed the helper's whole app (--process-under needs a real directory)
	# while its transaction directory remains: ask about that directory then, which holds wherever the helper's executable was (a wider
	# question, never a narrower one); a link in either place stays "unknown"
	var probe := _helper_app(String(journal.txid))
	if not DirAccess.dir_exists_absolute(probe) and not _is_link(probe) and not FileAccess.file_exists(probe):
		probe = _helper_transaction_dir(String(journal.txid))
	var code := OS.execute(helper, ["--process-under", str(int(journal.apply_pid)), probe], output, true)
	if code == 3:
		return {"known": true, "active": true}
	if code == 0:
		return {"known": true, "active": false}
	return {"known": false, "active": true}


static func _retire_transaction(updater: RefCounted, journal: Dictionary) -> String:
	test_pause(updater, "before-helper-retirement")
	var helper_dir := _helper_transaction_dir(String(journal.txid))
	if FileAccess.file_exists(helper_dir) or _is_link(helper_dir):
		return L10n.t("launcher.macos_update.recorded_external_helper_path_not")
	if DirAccess.dir_exists_absolute(helper_dir):
		var helper_error := _verify_external_helper(updater, journal)
		if helper_error != "" and not _retiring_state(String(journal.state)):
			return L10n.t("launcher.macos_update.recorded_external_helper_retained_because", {"why": helper_error})
		if helper_error != "":
			var verified := _verified_journal_manifest(updater, journal)
			if not verified.ok:
				return verified.error
			helper_error = _validate_inventory_subset(
				helper_dir, _signed_helper_files(verified.asset.files), updater,
			)
			if helper_error != "":
				return L10n.t("launcher.macos_update.recorded_external_helper_retained_because_remaining", {"why": helper_error})
		var process := _helper_process_status(journal)
		if not process.known:
			return L10n.t("launcher.macos_update.completed_update_helper_process_state")
		if process.active:
			return L10n.t("launcher.macos_update.completed_update_helper_still_exiting")
		_remove_tree(helper_dir)
		var local_helper := _local_swap_helper()
		if DirAccess.dir_exists_absolute(helper_dir) or FileAccess.file_exists(helper_dir) or _is_link(helper_dir) or \
				not FileAccess.file_exists(local_helper) or _is_link(local_helper) or \
				not _sync_parent(helper_dir, local_helper):
			return L10n.t("launcher.macos_update.verified_completed_update_helper_could")
	var path := journal_path()
	if _is_link(path):
		return L10n.t("launcher.macos_update.completed_mac_update_journal_became")
	if FileAccess.file_exists(path) and DirAccess.remove_absolute(path) != OK:
		return L10n.t("launcher.macos_update.completed_mac_update_journal_could_not")
	var local_helper := _local_swap_helper()
	if FileAccess.file_exists(path) or not FileAccess.file_exists(local_helper) or _is_link(local_helper) or \
			not _sync_parent(path, local_helper):
		return L10n.t("launcher.macos_update.completed_mac_update_journal_could")
	return ""


static func _cleanup_transaction(updater: RefCounted, journal: Dictionary, root_is_new: bool) -> String:
	var error := _transaction_trust_error(updater, journal)
	if error != "":
		return error
	error = _active_root_error(updater, journal, root_is_new)
	if error != "":
		return error
	var cleaning_state := "cleaning-confirmed" if root_is_new else "cleaning-rolled-back"
	var retiring_state := "retiring-confirmed" if root_is_new else "retiring-rolled-back"
	if journal.state not in [cleaning_state, retiring_state]:
		error = _pair_error(updater, journal, root_is_new)
		if error != "":
			return error
		if not _journal_state(updater, journal, cleaning_state):
			return L10n.t("launcher.macos_update.durable_mac_update_cleanup_phase")
	elif journal.state == retiring_state:
		var helper_dir := _helper_transaction_dir(String(journal.txid))
		if not DirAccess.dir_exists_absolute(helper_dir) and not FileAccess.file_exists(helper_dir) and not _is_link(helper_dir):
			var manifest := _verified_journal_manifest(updater, journal)
			if not manifest.ok:
				return manifest.error
	var inactive_files: Dictionary = journal.old_files if root_is_new else journal.new_files
	var candidate := String(journal.candidate)
	error = _validate_inventory_subset(candidate, inactive_files, updater)
	if error != "":
		return L10n.t("launcher.macos_update.inactive_package_retained_because_remaining", {"why": error})
	if DirAccess.dir_exists_absolute(candidate):
		_remove_tree(candidate)
	var sync_helper := String(journal.helper) if FileAccess.file_exists(String(journal.helper)) else _local_swap_helper()
	if DirAccess.dir_exists_absolute(candidate) or FileAccess.file_exists(candidate) or _is_link(candidate) or \
			not _sync_parent(candidate, sync_helper):
		return L10n.t("launcher.macos_update.verified_inactive_package_could_not")
	if journal.state == cleaning_state:
		test_point(updater, "after-candidate-removal")
		if not _journal_state(updater, journal, retiring_state):
			return L10n.t("launcher.macos_update.durable_mac_update_helper_retirement")
	return _retire_transaction(updater, journal)


## MACPLAY-1 (Sol r1 on DEVPORT-6, P1): the previous launcher is started again only when the root holds the previous package exactly as
## signed data names it (_tree_error, which authenticates the journal's old inventory); otherwise nothing is started and the reason is
## kept for the launcher's next window (state.cfg unseen_error).
static func _relaunch_old(updater: RefCounted, journal: Dictionary) -> void:
	var root := String(journal.root)
	var why := _tree_error(updater, journal, root, false)
	if why != "":
		var message := L10n.t("launcher.macos_update.previous_openbfme_launcher_not_started", {"why": why})
		Log.line("LAUNCHER " + message)
		updater.set_state("launcher", "unseen_error", message)
		return
	_launch(root.path_join(LAUNCHER_REL), _relaunch_args(updater))


## MACPLAY-1 (Sol r1 on DEVPORT-6, P1; Sol r2 P1/P2): puts the previous package back and retires the transaction, for every path that
## gives up on the new package (an interrupted transaction whose trees are no verified pair, a requested or failed-launch or unconfirmed
## rollback, a rollback that failed before, a rolled-back transaction whose staged copy changed). Each tree is checked on its own: when the
## previous package verifies (signed data, its code seals) it is put back at the root with --restore-if-idle (which needs only the
## package coming back to be complete: the damaged one may lack an app); then a new package that still verifies is a plain rollback
## (_finish_rollback), and a damaged one is kept aside (_retire_damaged). When the previous package does not verify either, nothing is
## changed. {restored, message}; called under the update lock. The message is kept for the next window (state.cfg unseen_error).
static func _restore_previous(updater: RefCounted, journal: Dictionary) -> Dictionary:
	var root := String(journal.root)
	var candidate := String(journal.candidate)
	var old_at_root := _tree_error(updater, journal, root, false) == ""
	var old_aside := not old_at_root and DirAccess.dir_exists_absolute(candidate) and not _is_link(candidate) and \
		_tree_error(updater, journal, candidate, false) == ""
	if not old_at_root and not old_aside:
		var refused := L10n.t("launcher.macos_update.mac_update_interrupted_neither_new", {"version": journal.version})
		updater.set_state("launcher", "unseen_error", refused)
		return {"restored": false, "message": refused}
	if old_aside:
		var helper := String(journal.helper) if FileAccess.file_exists(String(journal.helper)) else _local_swap_helper()
		var output: Array = []
		var code := OS.execute(helper, ["--restore-if-idle", root, candidate, str(OS.get_process_id())], output, true)
		if code != 0 or _tree_error(updater, journal, root, false) != "":
			Log.line("LAUNCHER the previous Mac package could not be put back: %s" % "\n".join(output).strip_edges())
			var blocked := L10n.t("launcher.macos_update.put_back_game_running", {"version": journal.version, "old_version": journal.old_version}) if code == 3 else L10n.t("launcher.macos_update.put_back_blocked", {"version": journal.version, "old_version": journal.old_version})
			return {"restored": false, "message": blocked}
		test_point(updater, "after-restore")
	if journal.state != "retiring-damaged" and DirAccess.dir_exists_absolute(candidate) and not _is_link(candidate) and \
			_tree_error(updater, journal, candidate, true) == "":
		var rollback_error := _finish_rollback(updater, journal)
		if rollback_error != "":
			Log.line("LAUNCHER Mac rollback needs recovery: " + rollback_error)
		return {"restored": true, "message": rollback_error}
	return _retire_damaged(updater, journal)


## MACPLAY-1 (Sol r2, P2): the previous package verifies at the root and the new one is damaged (or already moved aside): the attempt
## is counted as rejected and the state "retiring-damaged" recorded durably BEFORE anything else changes; from it every step restarts
## safely: the staged copy is renamed aside (<sibling>-damaged-<n>, never deleted) when it is still there (startup accepts the missing
## sibling in this state), and _retire_transaction removes the helper (a partly removed helper is checked as a subset of its signed
## files, as for the other retiring states) and then the journal, each removal checked and synced. {restored, message}.
static func _retire_damaged(updater: RefCounted, journal: Dictionary) -> Dictionary:
	var root := String(journal.root)
	var candidate := String(journal.candidate)
	var why := _tree_error(updater, journal, root, false)
	if why != "":
		var refused := L10n.t("launcher.macos_update.mac_update_interrupted_previous_package", {"version": journal.version, "why": why})
		updater.set_state("launcher", "unseen_error", refused)
		return {"restored": false, "message": refused}
	if journal.state != "retiring-damaged":
		_record_rejected(updater, journal)
		if not _journal_state(updater, journal, "retiring-damaged"):
			var pending := L10n.t("launcher.macos_update.damaged_record_pending", {"version": journal.version, "old_version": journal.old_version})
			Log.line("LAUNCHER " + pending)
			updater.set_state("launcher", "unseen_error", pending)
			return {"restored": true, "message": pending}
		test_point(updater, "damaged-recorded")
	var aside := ""
	if _is_link(candidate) or FileAccess.file_exists(candidate):
		var kept := L10n.t("launcher.macos_update.damaged_path_not_folder", {"version": journal.version, "old_version": journal.old_version})
		updater.set_state("launcher", "unseen_error", kept)
		return {"restored": true, "message": kept}
	if DirAccess.dir_exists_absolute(candidate):
		var n := 1
		aside = "%s-damaged-%d" % [candidate, n]
		while DirAccess.dir_exists_absolute(aside) or FileAccess.file_exists(aside) or _is_link(aside):
			n += 1
			aside = "%s-damaged-%d" % [candidate, n]
		if DirAccess.rename_absolute(candidate, aside) != OK or not _sync_parent(candidate, _local_swap_helper()):
			var pending := L10n.t("launcher.macos_update.damaged_aside_pending", {"version": journal.version, "old_version": journal.old_version})
			Log.line("LAUNCHER " + pending)
			updater.set_state("launcher", "unseen_error", pending)
			return {"restored": true, "message": pending}
		test_point(updater, "damaged-aside")
	var message := L10n.t("launcher.macos_update.damaged_kept_aside_at", {"version": journal.version, "old_version": journal.old_version, "path": aside}) if aside != "" else L10n.t("launcher.macos_update.damaged_kept_aside_next_to", {"version": journal.version, "old_version": journal.old_version, "name": candidate.get_file()})
	Log.line("LAUNCHER " + message)
	updater.set_state("launcher", "unseen_error", message)
	var retire_error := _retire_transaction(updater, journal)
	if retire_error != "":
		Log.line("LAUNCHER Mac update recovery: " + retire_error)
	return {"restored": true, "message": message}


## MACPLAY-1 (Sol r2, P2): a rollback the helper could not do: recorded (startup tries _restore_previous again) and explained in the next
## window.
static func _rollback_failed(updater: RefCounted, journal: Dictionary, why: String) -> void:
	if journal.state != "rollback-failed" and not _journal_state(updater, journal, "rollback-failed"):
		Log.line("LAUNCHER the failed rollback state could not be durably recorded")
	Log.line("LAUNCHER the Mac update rollback did not complete; both roots were retained: " + why)
	updater.set_state("launcher", "unseen_error", why)


static func _roll_back_and_relaunch(updater: RefCounted, journal: Dictionary) -> void:
	test_pause(updater, "before-rollback")
	var restored := _restore_previous(updater, journal)
	if restored.restored:
		_relaunch_old(updater, journal)
	else:
		_rollback_failed(updater, journal, String(restored.message))


static func _apply_locked(updater: RefCounted, journal: Dictionary, args: PackedStringArray) -> Dictionary:
	var root: String = journal.root
	var candidate: String = journal.candidate
	var helper: String = journal.helper
	var journal_error := _validate_journal(journal)
	if journal_error != "" or _candidate_path_error(root, candidate, updater, String(journal.txid)) != "" or helper.get_base_dir() != \
			_helper_app(String(journal.txid)).path_join("Contents/MacOS"):
		Log.line("LAUNCHER signed Mac update journal is invalid; no package paths were changed")
		return {"exit": true, "message": L10n.t("launcher.macos_update.signed_mac_update_journal_invalid")}
	var trust_error := _transaction_trust_error(updater, journal)
	if trust_error != "":
		Log.line("LAUNCHER the signed update transaction failed verification: " + trust_error)
		return {"exit": true, "message": L10n.t("launcher.macos_update.signed_mac_update_helper_failed")}
	var old_pair_error := _pair_error(updater, journal, false)
	var new_pair_error := _pair_error(updater, journal, true)
	var root_is_new := old_pair_error != "" and new_pair_error == ""
	if old_pair_error != "" and new_pair_error != "":
		Log.line("LAUNCHER signed Mac update roots do not match either recorded inventory: " + old_pair_error + "; " + new_pair_error)
		# MACPLAY-1 (Sol r1, P1): each tree on its own: a verified previous package is restored even when the new one is damaged
		var recovered := _restore_previous(updater, journal)
		if recovered.restored:
			_relaunch_old(updater, journal)
		return {"exit": true, "message": recovered.message}
	if not root_is_new:
		var old_pid := int(_arg(args, "--macos-old-pid="))
		var wait_started := Time.get_ticks_msec()
		while _pid_alive(helper, old_pid) and Time.get_ticks_msec() - wait_started < 30000:
			OS.delay_msec(100)
		if _pid_alive(helper, old_pid):
			Log.line("LAUNCHER timed out waiting for the previous launcher to exit")
			_relaunch_old(updater, journal)
			return {"exit": true, "message": ""}
		journal.apply_pid = OS.get_process_id()
		if not _journal_state(updater, journal, "swapping"):
			Log.line("LAUNCHER could not durably record the start of the Mac package exchange")
			_relaunch_old(updater, journal)
			return {"exit": true, "message": ""}
	var output: Array = []
	var swap_code := 0
	var force_rollback := "--macos-force-rollback" in args
	if root_is_new and force_rollback:
		_roll_back_and_relaunch(updater, journal)  # MACPLAY-1 (Sol r2): the restored previous package is verified on its own
		return {"exit": true, "message": ""}
	if not root_is_new:
		swap_code = OS.execute(helper, ["--swap-if-idle", root, candidate, str(OS.get_process_id())], output, true)
		var after_error := _pair_error(updater, journal, true)
		if after_error != "":
			if swap_code == 3 and _pair_error(updater, journal, false) == "":
				if not _journal_state(updater, journal, "deferred"):
					Log.line("LAUNCHER deferred Mac update state could not be durably recorded")
				Log.line("LAUNCHER signed Mac update deferred while a game or package process is running")
				_relaunch_old(updater, journal)
				return {"exit": true, "message": ""}
			Log.line("LAUNCHER atomic Mac exchange did not produce the verified new/old root pair: " + after_error)
			if _pair_error(updater, journal, false) == "":
				if not _journal_state(updater, journal, "failed"):
					Log.line("LAUNCHER failed Mac update state could not be durably recorded")
				_relaunch_old(updater, journal)
			elif swap_code == 0:
				_roll_back_and_relaunch(updater, journal)  # MACPLAY-1 (Sol r2): exchanged, and the new package is damaged
			return {"exit": true, "message": ""}
		root_is_new = true
		test_point(updater, "after-exchange")
	if not _journal_state(updater, journal, "swapped"):
		Log.line("LAUNCHER the package exchange completed but its journal write failed; inventories will reconcile on restart")
		_launch(root.path_join(LAUNCHER_REL), _relaunch_args(updater))
		return {"exit": true, "message": ""}
	var new_launcher := root.path_join(LAUNCHER_REL)
	var confirm_args := PackedStringArray(["--", "--macos-update-confirm=" + journal_path()])
	if updater.info.tests_allowed() and OS.get_environment("OPENBFME_TEST_MACOS_CONFIRM_EXIT") == "1":
		confirm_args.append("--version")
		confirm_args.insert(0, "--headless")  # tests: no window on the Mac's screen (MACPLAY-1)
	var new_pid := _launch(new_launcher, confirm_args)
	if new_pid <= 0:
		Log.line("LAUNCHER the new Mac launcher could not be started: rolling back")
		_roll_back_and_relaunch(updater, journal)
		return {"exit": true, "message": ""}
	var started := Time.get_ticks_msec()
	while Time.get_ticks_msec() - started < _confirm_timeout(updater):
		var current := _read_transaction()
		var current_state := String(current.journal.get("state", "")) if current.ok and current.exists else ""
		var confirmed := current_state in ["confirmed", "cleaning-confirmed", "retiring-confirmed"]
		if not confirmed and current_state == "swapped":
			confirmed = signed_receipt(updater, root, String(journal.version)).ok
		if confirmed:
			var receipt := signed_receipt(updater, root, String(journal.version))
			var cleanup: String = _cleanup_transaction(updater, journal, true) if receipt.ok else String(receipt.error)
			if cleanup == "":
				Log.line("LAUNCHER confirmed signed macOS update %s" % journal.version)
			else:
				Log.line("LAUNCHER confirmed update cleanup is pending recovery: " + cleanup)
			return {"exit": true, "message": ""}
		if not OS.is_process_running(new_pid):
			break
		OS.delay_msec(100)
	if OS.is_process_running(new_pid):
		OS.kill(new_pid)
	var stop_started := Time.get_ticks_msec()
	while OS.is_process_running(new_pid) and Time.get_ticks_msec() - stop_started < 10000:
		OS.delay_msec(100)
	if not OS.is_process_running(new_pid):
		Log.line("LAUNCHER the new Mac launcher did not confirm the update: rolling back")
		_roll_back_and_relaunch(updater, journal)
	else:
		Log.line("LAUNCHER the unconfirmed Mac launcher is still running; no package exchange or cleanup was attempted")
	return {"exit": true, "message": ""}


static func on_start(updater: RefCounted, args: PackedStringArray, begin_swap := true) -> Dictionary:
	if not updater.macos_updates_enabled():
		return {"exit": false, "message": ""}
	var apply_path := _arg(args, "--apply-macos-update=")
	if apply_path != "":
		if apply_path != journal_path():
			return {"exit": true, "message": ""}
		var apply_tx := _read_transaction()
		if not apply_tx.ok or not apply_tx.exists or apply_tx.journal.get("state", "") not in ["staged", "swapping", "swapped", "deferred"]:
			Log.line("LAUNCHER the signed Mac update helper refused an invalid journal: " + apply_tx.error)
			return {"exit": true, "message": ""}
		var apply_trust_error := _transaction_trust_error(updater, apply_tx.journal)
		if apply_trust_error != "":
			Log.line("LAUNCHER the signed Mac update helper refused an untrusted transaction: " + apply_trust_error)
			return {"exit": true, "message": ""}
		return _apply(updater, apply_tx.journal, args)
	var confirm_path := _arg(args, "--macos-update-confirm=")
	if confirm_path != "":
		if confirm_path != journal_path():
			return {"exit": true, "message": ""}
		var confirm_tx := _read_transaction()
		if not confirm_tx.ok or not confirm_tx.exists:
			return {"exit": true, "message": ""}
		var journal: Dictionary = confirm_tx.journal
		var trust_error := _transaction_trust_error(updater, journal)
		if trust_error != "":
			Log.line("LAUNCHER Mac confirmation refused because the transaction is untrusted: " + trust_error)
			return {"exit": true, "message": ""}
		var owner := _lock_status(updater)
		if journal.root != updater.launcher_dir.simplify_path() or journal.state not in ["swapping", "swapped"] or \
				not owner.locked or owner.owner_pid != int(journal.apply_pid) or owner.token != String(journal.txid):
			Log.line("LAUNCHER Mac confirmation was refused because this launcher does not own the live transaction")
			return {"exit": true, "message": ""}
		if updater.info.error != "" or updater.release_version() != String(journal.version):
			Log.line("LAUNCHER Mac confirmation refused: launched version %s does not match staged version %s" %
				[updater.release_version(), journal.version])
			return {"exit": true, "message": ""}
		test_point(updater, "before-confirm")
		var pair_error := _pair_error(updater, journal, true)
		if pair_error != "":
			Log.line("LAUNCHER Mac confirmation refused: " + pair_error)
			return {"exit": true, "message": ""}
		var error := _write_receipt(updater, journal)
		if error != "":
			Log.line("LAUNCHER signed Mac update confirmation failed: " + error)
			return {"exit": true, "message": ""}
		if not _journal_state(updater, journal, "confirmed"):
			Log.line("LAUNCHER Mac update receipt was written, but confirmation journal persistence failed")
			return {"exit": true, "message": ""}
		return {"exit": false, "message": ""}
	var lock := _lock_status(updater)
	if lock.locked:
		return {"exit": false, "message": L10n.t("launcher.macos_update.signed_mac_package_update_in")}
	if not lock.known:
		return {"exit": false, "message": L10n.t("launcher.macos_update.signed_mac_update_lock_cannot")}
	var transaction := _read_transaction()
	if not transaction.ok:
		Log.line("LAUNCHER " + transaction.error)
		return {"exit": false, "message": L10n.t("launcher.macos_update.signed_mac_update_journal_invalid_no")}
	if not transaction.exists:
		var token := "%d-%d-%08x" % [OS.get_process_id(), Time.get_ticks_usec(), randi()]
		if not _acquire_lock(updater, token):
			return {"exit": false, "message": L10n.t("launcher.macos_update.signed_mac_package_update_in")}
		var receipt_message := ""
		if signed_required(updater, updater.launcher_dir):
			var receipt := signed_receipt(updater, updater.launcher_dir)
			if not receipt.ok:
				Log.line("LAUNCHER signed Mac receipt verification failed: " + receipt.error)
				receipt_message = L10n.t("launcher.macos_update.signed_mac_package_receipt_could")
		_release_lock(token)
		return {"exit": false, "message": receipt_message}
	var journal: Dictionary = transaction.journal
	if journal.root != updater.launcher_dir.simplify_path():
		return {"exit": false, "message": L10n.t("launcher.macos_update.signed_mac_package_update_belongs")}
	var cleanup_state: bool = journal.state in ["cleaning-confirmed", "cleaning-rolled-back",
		"retiring-confirmed", "retiring-rolled-back", "retiring-damaged"]
	var path_error := _candidate_path_error(String(journal.root), String(journal.candidate), updater,
		String(journal.txid), cleanup_state)
	if path_error != "":
		Log.line("LAUNCHER signed Mac update journal has unsafe paths: " + path_error)
		return {"exit": false, "message": L10n.t("launcher.macos_update.signed_mac_update_paths_are")}
	var trust_error := _transaction_trust_error(updater, journal)
	if trust_error != "":
		Log.line("LAUNCHER signed Mac update transaction is untrusted: " + trust_error)
		return {"exit": false, "message": L10n.t("launcher.macos_update.signed_mac_update_transaction_could")}
	if journal.state == "failed":
		Log.line("LAUNCHER signed Mac update is retained for diagnosis in state %s" % journal.state)
		return {"exit": false, "message": L10n.t("launcher.macos_update.previous_signed_mac_update_failed")}
	var token := String(journal.txid)
	if not _acquire_lock(updater, token):
		return {"exit": false, "message": L10n.t("launcher.macos_update.signed_mac_package_update_in")}
	var message := ""
	var exit_app := false
	if journal.state in ["cleaning-confirmed", "retiring-confirmed"]:
		message = _cleanup_transaction(updater, journal, true)
		_release_lock(token)
		if message != "":
			Log.line("LAUNCHER Mac update recovery: " + message)
		return {"exit": false, "message": message}
	if journal.state in ["cleaning-rolled-back", "retiring-rolled-back"]:
		message = _cleanup_transaction(updater, journal, false)
		_release_lock(token)
		if message != "":
			Log.line("LAUNCHER Mac update recovery: " + message)
		return {"exit": false, "message": message}
	if journal.state in ["retiring-damaged", "rollback-failed"]:
		# MACPLAY-1 (Sol r2): an interrupted damaged-package recovery resumes where it stopped; a rollback that failed is tried again
		message = String(_restore_previous(updater, journal).message)
		_release_lock(token)
		if message != "":
			Log.line("LAUNCHER Mac update recovery: " + message)
		return {"exit": false, "message": message}
	var old_pair_error := _pair_error(updater, journal, false)
	var new_pair_error := _pair_error(updater, journal, true)
	if journal.state == "confirmed":
		if new_pair_error == "":
			var receipt := signed_receipt(updater, updater.launcher_dir, String(journal.version))
			message = _cleanup_transaction(updater, journal, true) if receipt.ok else receipt.error
		else:
			message = L10n.t("launcher.macos_update.confirmed_mac_package_no_longer")
	elif journal.state == "rolled-back":
		if old_pair_error == "":
			if _record_rejected(updater, journal):
				message = _cleanup_transaction(updater, journal, false)
			else:
				message = L10n.t("launcher.macos_update.rejected_mac_update_attempt_could")
		else:
			message = String(_restore_previous(updater, journal).message)  # MACPLAY-1 (Sol r2): e.g. the staged copy changed since
	elif old_pair_error != "" and new_pair_error != "":
		# MACPLAY-1 (Sol r1, P1): each tree on its own (this launcher then runs on as the restored previous package, or says why not)
		message = String(_restore_previous(updater, journal).message)
	elif new_pair_error == "":
		if updater.info.error == "" and updater.release_version() == String(journal.version) and \
				_version_for_root(updater.launcher_dir) == String(journal.version):
			var receipt_error := _write_receipt(updater, journal)
			if receipt_error == "" and _journal_state(updater, journal, "confirmed"):
				var receipt := signed_receipt(updater, updater.launcher_dir, String(journal.version))
				message = _cleanup_transaction(updater, journal, true) if receipt.ok else receipt.error
			else:
				message = receipt_error if receipt_error != "" else L10n.t("launcher.macos_update.mac_update_confirmation_could_not")
				Log.line("LAUNCHER signed Mac update recovery could not confirm: " + message)
		elif begin_swap:
			_release_lock(token)
			if not _start_apply(updater, journal, OS.get_process_id(), true):
				message = L10n.t("launcher.macos_update.staged_mac_update_failed_startup")
				return {"exit": false, "message": message}
			return {"exit": true, "message": ""}
	else:
		if journal.state == "deferred":
			var output: Array = []
			if OS.execute(String(journal.helper), ["--game-running", String(journal.root), str(OS.get_process_id())], output, true) == 3:
				message = L10n.t("launcher.macos_update.signed_mac_update_waiting_for")
			elif not begin_swap:
				message = L10n.t("launcher.macos_update.signed_mac_update_staged_will")
			else:
				_release_lock(token)
				if not _start_apply(updater, journal, OS.get_process_id()):
					message = L10n.t("launcher.macos_update.signed_mac_update_staged_but")
					return {"exit": false, "message": message}
				return {"exit": true, "message": ""}
		elif journal.state in ["staged", "swapping"] and begin_swap:
			_release_lock(token)
			if not _start_apply(updater, journal, OS.get_process_id()):
				message = L10n.t("launcher.macos_update.signed_mac_update_staged_but")
				return {"exit": false, "message": message}
			return {"exit": true, "message": ""}
		else:
			message = L10n.t("launcher.macos_update.signed_mac_update_staged_will")
	_release_lock(token)
	if message != "":
		Log.line("LAUNCHER Mac update recovery: " + message)
	return {"exit": exit_app, "message": message}
