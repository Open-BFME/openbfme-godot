extends SceneTree

## Shipping-tree containment proof. This exercises the real ModLoader autoload
## without changing it: the project must have no embedded example pack, while
## the moved example mounts exactly once when selected as an explicit root.

const WatchdogScript = preload("res://tests/runner_watchdog.gd")

var _watchdog := WatchdogScript.new()


func _initialize() -> void:
	_watchdog.start(self, "AMBIENT_MOD_CLEAN", 120000)
	call_deferred("_run")


func _run() -> void:
	var mod_loader = root.get_node_or_null("ModLoader")
	if mod_loader == null:
		_finish(false, "ModLoader autoload is unavailable")
		return
	var previous_content := OS.get_environment("OPENBFME_CONTENT")
	var example_root := OS.get_environment("OPENBFME_AMBIENT_MOD_EXAMPLE_ROOT")
	if example_root == "":
		_finish(false, "explicit example root was not provided")
		return
	OS.set_environment("OPENBFME_CONTENT", example_root)
	var roots: Array[String] = mod_loader.list_pack_roots()
	OS.set_environment("OPENBFME_CONTENT", previous_content)
	if roots.size() != 1 or not _same_path(roots[0], example_root):
		_finish(false, "explicit example did not mount exactly once")
		return
	if String(mod_loader.active_content_source) != "external":
		_finish(false, "explicit example lost external source identity")
		return
	if not _same_path(String(mod_loader.active_pack_root), example_root):
		_finish(false, "explicit example lost active-pack identity")
		return
	if not mod_loader.diagnostics.is_empty() or not mod_loader.suppressed_ambient_roots.is_empty():
		_finish(false, "clean explicit mount emitted diagnostics or found ambient packs")
		return
	var identity: Dictionary = mod_loader.pack_identity(roots[0])
	if String(identity.get("packId", "")) != "example_hard_orcs":
		_finish(false, "explicit example pack identity changed")
		return

	var report_path := OS.get_environment("OPENBFME_AMBIENT_MOD_REPORT")
	var report := {
		"schema": "openbfme.ambient-mod-clean",
		"schemaVersion": 1,
		"shippingAmbientExampleAbsent": true,
		"explicitSourceMounted": true,
		"explicitPackId": "example_hard_orcs",
		"mountedRootCount": 1,
		"trackedShippingPacks": 0,
		"example": "examples/mods/example_hard_orcs",
	}
	if report_path == "" or not _write_report(report_path, report):
		_finish(false, "declared report could not be written")
		return
	print("AMBIENT_MOD_CLEAN PASS tracked_shipping_packs=0 example=examples/mods/example_hard_orcs")
	_watchdog.stop()
	quit(0)


func _same_path(left: String, right: String) -> bool:
	return left.replace("\\", "/").trim_suffix("/").to_lower() == right.replace("\\", "/").trim_suffix("/").to_lower()


func _write_report(path: String, report: Dictionary) -> bool:
	var file := FileAccess.open(path, FileAccess.WRITE)
	if file == null:
		return false
	file.store_string(JSON.stringify(report, "  ") + "\n")
	file.close()
	return true


func _finish(ok: bool, reason: String) -> void:
	if not ok:
		printerr("AMBIENT_MOD_CLEAN FAIL %s" % reason)
	_watchdog.stop()
	quit(0 if ok else 1)
