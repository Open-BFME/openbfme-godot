## VERIFYFILES-1: compare one installed game folder with the English AIO pins,
## and, only after explicit confirmation, replace missing or changed pinned files.
extends RefCounted

const AioInstall := preload("res://scripts/core/aio_install.gd")
const Log := preload("res://scripts/core/launcher_log.gd")
const SelfUpdate := preload("res://scripts/core/self_update.gd")

const CHUNK := 1 << 20
const L10n := preload("res://scripts/l10n/l10n.gd")
const WINDOWS_SHELL_PATH_CHARS := ["%", "!", "^", "&", "|", "<", ">", "\"", "\r", "\n"]

var aio
var _reports := {}


func _init(aio_install) -> void:
	aio = aio_install


func _base(key: String, root: String, label := "") -> Dictionary:
	return {
		"ok": false, "error": "", "key": key, "root": root, "label": label,
		"good": [], "missing": [], "changed": [], "unchecked": [],
		"warning": "", "repairable": false
	}


func _failure(message: String, key := "", root := "", label := "", warning := "") -> Dictionary:
	Log.line("LAUNCHER VERIFYFILES " + message)
	var result := _base(key, root, label)
	result.error = message
	result.warning = warning
	return result


func _report(text: String, done := -1, total := -1) -> void:
	aio.updater._report(text, done, total)


func _cancelled() -> bool:
	return aio != null and aio.updater != null and aio.updater.http != null and aio.updater.http.cancelled


func _has_windows_shell_metacharacter(path: String) -> bool:
	for character in WINDOWS_SHELL_PATH_CHARS:
		if path.contains(character):
			return true
	return false


func _windows_repair_paths_safe(root: String, changes: Array) -> bool:
	if OS.get_name() != "Windows":
		return true
	if _has_windows_shell_metacharacter(root):
		return false
	for plan in changes:
		if _has_windows_shell_metacharacter(String(plan.target)):
			return false
	return true


func _path_is_link(path: String) -> bool:
	var parent := DirAccess.open(path.get_base_dir())
	return parent != null and parent.is_link(path.get_file())


func _has_link_ancestor(path: String) -> bool:
	var p := path.replace("\\", "/").simplify_path()
	var current := ""
	var rest := ""
	if p.begins_with("//"):
		var unc := p.trim_prefix("//").split("/", false)
		if unc.size() < 2:
			return false
		current = "//" + unc[0] + "/" + unc[1]
		rest = "/".join(unc.slice(2))
	elif p.length() >= 3 and p[1] == ":" and p[2] == "/":
		current = p.substr(0, 3)
		rest = p.substr(3)
	elif p.begins_with("/"):
		current = "/"
		rest = p.substr(1)
	else:
		return true
	for part in rest.split("/", false):
		if part.is_empty():
			continue
		current = current.path_join(part)
		if _path_is_link(current):
			return true
	return false


func _safe_root(root: String) -> String:
	if root.is_empty() or not root.is_absolute_path():
		return L10n.t("launcher.game_files.root_not_absolute")
	var native := root.replace("\\", "/")
	for part in native.split("/", false):
		if part in [".", ".."]:
			return L10n.t("launcher.game_files.root_dots")
	var clean := native.simplify_path()
	if _has_link_ancestor(clean):
		return L10n.t("launcher.game_files.root_link")
	if not DirAccess.dir_exists_absolute(clean):
		return L10n.t("launcher.game_files.root_missing", {"root": root})
	var dir := DirAccess.open(clean)
	if dir == null:
		return L10n.t("launcher.game_files.root_unopenable", {"error": error_string(DirAccess.get_open_error())})
	return ""


func _walk(path: String, rel: String, records: Array) -> String:
	if _path_is_link(path):
		records.append({"rel": rel, "kind": "link"})
		return ""
	var dir := DirAccess.open(path)
	if dir == null:
		return L10n.t("launcher.game_files.cannot_enumerate", {"path": path, "error": error_string(DirAccess.get_open_error())})
	dir.include_hidden = true
	dir.include_navigational = false
	var begin := dir.list_dir_begin()
	if begin != OK:
		dir.list_dir_end()
		return L10n.t("launcher.game_files.cannot_enumerate", {"path": path, "error": error_string(begin)})
	var children := []
	while true:
		var name := dir.get_next()
		if name.is_empty():
			break
		children.append({"name": name, "link": dir.is_link(name), "dir": dir.current_is_dir()})
	dir.list_dir_end()
	children.sort_custom(func(a, b): return String(a.name) < String(b.name))
	for child_info in children:
		var name: String = child_info.name
		var child_path := path.path_join(name)
		var child_rel := name if rel.is_empty() else rel.path_join(name)
		if child_info.link:
			records.append({"rel": child_rel, "kind": "link"})
		elif child_info.dir:
			records.append({"rel": child_rel, "kind": "dir"})
			var why := _walk(child_path, child_rel, records)
			if why != "":
				return why
		else:
			records.append({"rel": child_rel, "kind": "file"})
	return ""


func _entry_map(records: Array) -> Dictionary:
	var out := {}
	for entry in records:
		var folded := String(entry.rel).to_lower()
		if out.has(folded):
			return {}
		out[folded] = entry
	return out


func _package(key: String) -> Dictionary:
	for candidate in aio.packages:
		if str(candidate.get("key", "")) == key:
			return candidate
	return {}


func _pins(pkg: Dictionary) -> Dictionary:
	var out := {}
	for row in pkg.get("files", []):
		if typeof(row) != TYPE_ARRAY or row.size() != 3:
			return {}
		var rel := AioInstall.safe_relative(str(row[0]))
		if rel.is_empty() or out.has(rel.to_lower()):
			return {}
		var md5 := str(row[1]).to_lower()
		if md5.length() != 32 or RegEx.create_from_string("^[0-9a-f]{32}$").search(md5) == null:
			return {}
		if typeof(row[2]) != TYPE_INT and typeof(row[2]) != TYPE_FLOAT:
			return {}
		if int(row[2]) < 0:
			return {}
		out[rel.to_lower()] = {"rel": rel, "md5": md5, "size": int(row[2])}
	return out


func _hash_file(path: String, show: String, done: int, total: int) -> Dictionary:
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null:
		return {"ok": false, "error": L10n.t("launcher.game_files.cannot_read", {"path": path, "error": error_string(FileAccess.get_open_error())})}
	var length := file.get_length()
	var context := HashingContext.new()
	context.start(HashingContext.HASH_MD5)
	var read := 0
	while read < length:
		if _cancelled():
			file.close()
			return {"ok": false, "error": L10n.t("launcher.game_files.cancelled")}
		var want := mini(CHUNK, length - read)
		var bytes := file.get_buffer(want)
		if bytes.size() != want or file.get_error() != OK:
			var why := error_string(file.get_error())
			file.close()
			return {"ok": false, "error": L10n.t("launcher.game_files.cannot_read", {"path": path, "error": why})}
		context.update(bytes)
		read += bytes.size()
		_report(L10n.t("launcher.game_files.checking", {"name": show}), done + read, total)
	if file.get_length() != length or file.get_position() != length:
		file.close()
		return {"ok": false, "error": L10n.t("launcher.game_files.changed_while_reading", {"path": path})}
	file.close()
	return {"ok": true, "size": length, "md5": context.finish().hex_encode()}


func _resolve_target(root: String, rel: String, entries: Dictionary) -> String:
	var parts := rel.split("/", false)
	var current := root
	for i in parts.size():
		var wanted := parts[i]
		var prefix := "/".join(parts.slice(0, i + 1)).to_lower()
		if entries.has(prefix):
			current = current.path_join(String(entries[prefix].rel).get_file())
		else:
			current = current.path_join(wanted)
	return current


func _language_safe(pkg: Dictionary, rows: Array, unchecked: Array, states: Dictionary) -> bool:
	var proven := false
	for row in pkg.get("files", []):
		var rel := AioInstall.safe_relative(str(row[0]))
		if not rel.to_lower().begins_with("lang/") or not rel.to_lower().ends_with(".big"):
			continue
		var state: String = states.get(rel.to_lower(), {}).get("state", "")
		if state == "good":
			proven = true
		if state == "changed":
			return false
	for path in unchecked:
		var lower := String(path).to_lower()
		if lower.begins_with("lang/") and lower.ends_with(".big"):
			return false
	return proven


func _scan(key: String, root: String) -> Dictionary:
	var pkg := _package(key)
	var label := str(pkg.get("label", pkg.get("folder", key)))
	if pkg.is_empty():
		return {"ok": false, "error": L10n.t("launcher.game_files.no_pinned_package", {"key": key}), "key": key, "root": root, "label": label}
	var pins := _pins(pkg)
	if pins.is_empty():
		return {"ok": false, "error": L10n.t("launcher.game_files.malformed_package", {"key": key}), "key": key, "root": root, "label": label}
	var why := _safe_root(root)
	if why != "":
		return {"ok": false, "error": why, "key": key, "root": root, "label": label}
	root = root.replace("\\", "/").simplify_path()
	var records := []
	why = _walk(root, "", records)
	if why != "":
		return {"ok": false, "error": why, "key": key, "root": root, "label": label}
	var entries := _entry_map(records)
	if entries.size() != records.size():
		return {"ok": false, "error": L10n.t("launcher.game_files.duplicate_case"), "key": key, "root": root, "label": label}
	for entry in records:
		if entry.kind != "link":
			continue
		for pin_key in pins:
			if pin_key == String(entry.rel).to_lower() or pin_key.begins_with(String(entry.rel).to_lower() + "/"):
				return {"ok": false, "error": L10n.t("launcher.game_files.pinned_link", {"path": entry.rel}), "key": key, "root": root, "label": label}
	var good: Array[String] = []
	var missing: Array[String] = []
	var changed: Array[String] = []
	var unchecked: Array[String] = []
	var states := {}
	var total := 0
	for pin in pins.values():
		total += int(pin.size)
	var done := 0
	for pin_key in pins:
		if _cancelled():
			return {"ok": false, "error": L10n.t("launcher.game_files.cancelled"), "key": key, "root": root, "label": label}
		var pin: Dictionary = pins[pin_key]
		_report(L10n.t("launcher.game_files.checking", {"name": pin.rel}), done, total)
		var entry: Dictionary = entries.get(pin_key, {})
		if entry.is_empty():
			missing.append(pin.rel)
			states[pin_key] = {"state": "missing", "actual": "", "size": -1, "md5": ""}
			continue
		if entry.kind != "file":
			return {"ok": false, "error": L10n.t("launcher.game_files.pinned_not_file", {"path": entry.rel}), "key": key, "root": root, "label": label}
		var actual := root.path_join(String(entry.rel))
		actual = _resolve_target(root, pin.rel, entries)
		var hashed := _hash_file(actual, pin.rel, done, total)
		if not hashed.ok:
			return {"ok": false, "error": hashed.error, "key": key, "root": root, "label": label}
		done += int(hashed.size)
		if int(hashed.size) == pin.size and String(hashed.md5).to_lower() == pin.md5:
			good.append(String(entry.rel))
			states[pin_key] = {"state": "good", "actual": entry.rel, "size": int(hashed.size), "md5": hashed.md5}
		else:
			changed.append(String(entry.rel))
			states[pin_key] = {"state": "changed", "actual": entry.rel, "size": int(hashed.size), "md5": hashed.md5}
	for entry in records:
		if entry.kind == "dir":
			continue
		var folded := String(entry.rel).to_lower()
		if not pins.has(folded):
			unchecked.append(String(entry.rel))
	good.sort()
	missing.sort()
	changed.sort()
	unchecked.sort()
	var safe_language := _language_safe(pkg, records, unchecked, states)
	var warning := "" if safe_language else L10n.t("launcher.game_files.language_warning")
	var changes := []
	for rel in missing:
		var pin: Dictionary = pins[rel.to_lower()]
		changes.append({"rel": rel, "md5": pin.md5, "size": pin.size, "before_size": -1, "before_md5": "",
			"before_actual": "", "target": _resolve_target(root, rel, entries)})
	for rel in changed:
		var pin: Dictionary = pins[rel.to_lower()]
		var state: Dictionary = states[rel.to_lower()]
		changes.append({"rel": rel, "md5": pin.md5, "size": pin.size, "before_size": state.size,
			"before_md5": state.md5, "before_actual": state.actual, "target": root.path_join(String(state.actual))})
	var snapshot := JSON.stringify({"records": records, "states": states, "good": good, "missing": missing,
		"changed": changed, "unchecked": unchecked, "warning": warning})
	return {"ok": true, "key": key, "root": root, "label": label, "good": good, "missing": missing,
		"changed": changed, "unchecked": unchecked, "warning": warning, "repairable": safe_language,
		"snapshot": snapshot, "changes": changes, "pkg": pkg, "entries": entries}


func verify(key: String, root: String) -> Dictionary:
	var scan := _scan(key, root)
	if not scan.ok:
		return _failure(scan.error, key, root, scan.get("label", ""))
	var token := "%s-%s-%s" % [get_instance_id(), Time.get_ticks_usec(), randi()]
	var result := _base(key, root, scan.label)
	for field in ["good", "missing", "changed", "unchecked", "warning", "repairable"]:
		result[field] = scan[field]
	result.ok = true
	result["_token"] = token
	_reports[token] = {"key": key, "root": root, "snapshot": scan.snapshot, "changes": scan.changes, "pkg": scan.pkg,
		"repairable": scan.repairable, "warning": scan.warning}
	return result


func _fresh_report(token: String, report: Dictionary) -> Dictionary:
	var saved: Dictionary = _reports.get(token, {})
	if saved.is_empty() or report.get("key", "") != saved.key or report.get("root", "") != saved.root:
		return {"ok": false, "error": L10n.t("launcher.game_files.report_foreign")}
	var scan := _scan(saved.key, saved.root)
	if not scan.ok:
		return {"ok": false, "error": scan.error}
	if scan.snapshot != saved.snapshot:
		return {"ok": false, "error": L10n.t("launcher.game_files.install_changed")}
	return {"ok": true, "scan": scan, "saved": saved}


func _stage_name(root: String, token: String, index: int) -> String:
	var random := "%s-%s-%s" % [token, index, randi()]
	return root.path_join(".openbfme-verifyfiles-" + random + ".tmp")


func _cleanup(stages: Array) -> String:
	var errors := PackedStringArray()
	for stage in stages:
		if FileAccess.file_exists(stage) or _path_is_link(stage):
			var err := DirAccess.remove_absolute(stage)
			if err != OK:
				var message := L10n.t("launcher.game_files.cannot_remove_temp", {"path": stage, "error": error_string(err)})
				errors.append(message)
				Log.line("LAUNCHER VERIFYFILES " + message)
	return "; ".join(errors)


func _check_target(plan: Dictionary) -> Dictionary:
	var target: String = plan.target
	if _has_link_ancestor(target) or _path_is_link(target):
		return {"ok": false, "error": L10n.t("launcher.game_files.target_link", {"path": plan.rel})}
	if DirAccess.dir_exists_absolute(target):
		return {"ok": false, "error": L10n.t("launcher.game_files.target_directory", {"path": plan.rel})}
	var expected_size: int = plan.before_size
	if expected_size < 0:
		if FileAccess.file_exists(target):
			return {"ok": false, "error": L10n.t("launcher.game_files.target_appeared", {"path": plan.rel})}
		return {"ok": true}
	if not FileAccess.file_exists(target):
		return {"ok": false, "error": L10n.t("launcher.game_files.target_disappeared", {"path": plan.rel})}
	var checked := _hash_file(target, plan.rel, 0, expected_size)
	if not checked.ok:
		return checked
	if int(checked.size) != expected_size or String(checked.md5).to_lower() != String(plan.before_md5).to_lower():
		return {"ok": false, "error": L10n.t("launcher.game_files.target_changed", {"path": plan.rel})}
	return {"ok": true}


func _ensure_parent(target: String, root: String) -> Dictionary:
	var relative := target.replace("\\", "/").trim_prefix(root.replace("\\", "/") + "/")
	var parts := relative.get_base_dir().split("/", false)
	var current := root
	for part in parts:
		current = current.path_join(part)
		if _path_is_link(current):
			return {"ok": false, "error": L10n.t("launcher.game_files.repair_dir_link", {"path": current})}
		if DirAccess.dir_exists_absolute(current):
			continue
		if FileAccess.file_exists(current):
			return {"ok": false, "error": L10n.t("launcher.game_files.repair_dir_file", {"path": current})}
		var err := DirAccess.make_dir_absolute(current)
		if err != OK and not DirAccess.dir_exists_absolute(current):
			return {"ok": false, "error": L10n.t("launcher.game_files.cannot_create_dir", {"path": current, "error": error_string(err)})}
	return {"ok": true}


func _tail(cleanup: String) -> String:
	return ": " + cleanup if cleanup != "" else ""


func _repair_downloads(saved: Dictionary, scan: Dictionary, token: String) -> Dictionary:
	var pkg: Dictionary = saved.pkg
	var manifest_url := "%s/api/workshop/download?guid=%s" % [aio.service, String(pkg.guid).uri_encode()]
	_report(L10n.t("launcher.game_files.asking_service", {"service": aio.service.trim_prefix("https://"), "guid": pkg.guid}))
	var response: Dictionary = aio.updater.http.get_small(manifest_url, AioInstall.MAX_MANIFEST, "application/json", "",
		PackedStringArray(["AuthAccountUuid: unauthenticated", "AuthAccountPassword: "]))
	if not response.ok:
		return {"ok": false, "error": L10n.t("launcher.game_files.file_list_failed", {"guid": pkg.guid, "error": response.error})}
	if response.code != 200:
		return {"ok": false, "error": L10n.t("launcher.game_files.service_http", {"code": response.code, "guid": pkg.guid})}
	# the English pins the scan checked against (pkg.files), not the launcher's chosen download languages (Sol review)
	var checked: Dictionary = aio.check_manifest(pkg, response.body, pkg.files)
	if not checked.ok:
		return {"ok": false, "error": checked.error}
	var sources := {}
	for file in checked.files:
		sources[String(file.rel).to_lower()] = file
	var stages := []
	for index in saved.changes.size():
		if _cancelled():
			var cancel_cleanup := _cleanup(stages)
			return {"ok": false, "error": L10n.t("launcher.game_files.repair_cancelled", {"cleanup": _tail(cancel_cleanup)})}
		var plan: Dictionary = saved.changes[index]
		var source: Dictionary = sources.get(String(plan.rel).to_lower(), {})
		if source.is_empty():
			var missing_cleanup := _cleanup(stages)
			return {"ok": false, "error": L10n.t("launcher.game_files.manifest_missing", {"path": plan.rel, "cleanup": _tail(missing_cleanup)})}
		var stage := _stage_name(scan.root, token, index)
		if _has_link_ancestor(scan.root) or _path_is_link(stage) or FileAccess.file_exists(stage) or DirAccess.dir_exists_absolute(stage):
			var occupied_cleanup := _cleanup(stages)
			return {"ok": false, "error": L10n.t("launcher.game_files.temp_path_unsafe", {"path": stage, "cleanup": _tail(occupied_cleanup)})}
		stages.append(stage)
		var download: Dictionary = aio.updater.http.download(source.url, stage, int(plan.size),
			func(done: int, total: int) -> void: _report(L10n.t("launcher.game_files.downloading", {"path": plan.rel}), done, total))
		if not download.ok:
			var download_cleanup := _cleanup(stages)
			return {"ok": false, "error": L10n.t("launcher.game_files.download_failed", {"path": plan.rel, "error": download.error, "cleanup": _tail(download_cleanup)})}
		if int(plan.size) == 0 and not FileAccess.file_exists(stage):
			var empty := FileAccess.open(stage, FileAccess.WRITE)
			if empty == null:
				var empty_cleanup := _cleanup(stages)
				return {"ok": false, "error": L10n.t("launcher.game_files.zero_byte_failed", {"path": plan.rel, "cleanup": _tail(empty_cleanup)})}
			empty.close()
		var got := _hash_file(stage, plan.rel, 0, int(plan.size))
		if not got.ok or int(got.size) != int(plan.size) or String(got.md5).to_lower() != String(plan.md5).to_lower():
			var checksum := L10n.t("launcher.game_files.staged_unreadable", {"path": plan.rel}) if not got.ok else L10n.t("launcher.game_files.checksum_mismatch", {"path": plan.rel, "got": str(got), "size": int(plan.size), "md5": plan.md5})
			var checksum_cleanup := _cleanup(stages)
			return {"ok": false, "error": checksum + _tail(checksum_cleanup)}
	_report(L10n.t("launcher.game_files.downloads_ready"))
	return {"ok": true, "stages": stages}


func _replace(saved: Dictionary, stages: Array, root: String) -> Dictionary:
	var completed := 0
	for index in stages.size():
		if _cancelled():
			var cancel_cleanup := _cleanup(stages.slice(index))
			return {"ok": false, "error": L10n.t("launcher.game_files.repair_stopped", {"done": completed, "reason": L10n.t("launcher.game_files.reason_cancelled"), "cleanup": _tail(cancel_cleanup)}), "completed": completed}
		var plan: Dictionary = saved.changes[index]
		var checked := _check_target(plan)
		if not checked.ok:
			var check_cleanup := _cleanup(stages.slice(index))
			return {"ok": false, "error": L10n.t("launcher.game_files.repair_stopped", {"done": completed, "reason": checked.error, "cleanup": _tail(check_cleanup)}), "completed": completed}
		var parents := _ensure_parent(plan.target, root)
		if not parents.ok:
			var parent_cleanup := _cleanup(stages.slice(index))
			return {"ok": false, "error": L10n.t("launcher.game_files.repair_stopped", {"done": completed, "reason": parents.error, "cleanup": _tail(parent_cleanup)}), "completed": completed}
		checked = _check_target(plan)
		if not checked.ok:
			var changed_cleanup := _cleanup(stages.slice(index))
			return {"ok": false, "error": L10n.t("launcher.game_files.repair_stopped", {"done": completed, "reason": checked.error, "cleanup": _tail(changed_cleanup)}), "completed": completed}
		if _has_link_ancestor(plan.target) or _path_is_link(plan.target):
			var link_cleanup := _cleanup(stages.slice(index))
			return {"ok": false, "error": L10n.t("launcher.game_files.repair_stopped", {"done": completed, "reason": L10n.t("launcher.game_files.reason_link", {"path": plan.rel}), "cleanup": _tail(link_cleanup)}), "completed": completed}
		var replace_error := ""
		if OS.get_name() == "Windows":
			if not SelfUpdate._replace_atomically(stages[index], plan.target):
				replace_error = L10n.t("launcher.game_files.helper_failed")
		else:
			var err := DirAccess.rename_absolute(stages[index], plan.target)
			if err != OK:
				replace_error = error_string(err)
		if replace_error != "":
			var rename_cleanup := _cleanup(stages.slice(index))
			return {"ok": false, "error": L10n.t("launcher.game_files.repair_stopped", {"done": completed, "reason": L10n.t("launcher.game_files.reason_replace", {"path": plan.rel, "error": replace_error}), "cleanup": _tail(rename_cleanup)}), "completed": completed}
		completed += 1
	return {"ok": true, "completed": completed}


func repair(report: Dictionary, confirmed: bool) -> Dictionary:
	var token := str(report.get("_token", ""))
	var saved: Dictionary = _reports.get(token, {})
	if saved.is_empty() or report.get("key", "") != saved.key or report.get("root", "") != saved.root:
		return _failure(L10n.t("launcher.game_files.report_foreign"))
	if not confirmed:
		return _failure(L10n.t("launcher.game_files.needs_confirmation"), saved.key, saved.root)
	if not saved.repairable:
		return _failure(saved.warning, saved.key, saved.root, "", saved.warning)
	if saved.changes.is_empty():
		var unchanged := verify(saved.key, saved.root)
		return unchanged
	var fresh := _fresh_report(token, report)
	if not fresh.ok:
		return _failure(fresh.error, saved.key, saved.root)
	if not _windows_repair_paths_safe(fresh.scan.root, saved.changes):
		return _failure(L10n.t("launcher.game_files.windows_shell_chars"), saved.key, saved.root)
	var old_hosts: Array = aio.updater.policy.extra_hosts.duplicate()
	var hosts := old_hosts.duplicate()
	for host in AioInstall.HOSTS:
		if not host in hosts:
			hosts.append(host)
	aio.updater.policy.extra_hosts = hosts
	var stage_result := _repair_downloads(saved, fresh.scan, token)
	var result := stage_result
	if stage_result.ok:
		result = _replace(saved, stage_result.stages, fresh.scan.root)
	aio.updater.policy.extra_hosts = old_hosts
	if not result.ok:
		var failed := _failure(result.error, saved.key, saved.root)
		failed["repaired"] = int(result.get("completed", 0))
		return failed
	var checked := verify(saved.key, saved.root)
	if not checked.ok:
		return checked
	checked["repaired"] = int(result.completed)
	return checked
