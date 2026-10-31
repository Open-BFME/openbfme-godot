## Lane ONEZIP-1 r3 (Sol r2): a signed file larger than its limit is refused by its size, never read whole first. The game beside a
## launcher is given a 4 MiB payload.json (limit 64 KiB) and then a 32 MiB payload.json.sig (64 bytes): bundled() names the size and
## the process's peak memory grows by far less than the file. Run by tools/release/test_onezip.py with -- --dir=<an empty folder>.
##   godot --headless --path launcher --script res://tests/bounded_read_test.gd -- --dir=<folder>
extends SceneTree

const Updater := preload("res://scripts/core/updater.gd")
const BuildInfo := preload("res://scripts/core/build_info.gd")

var failures := 0


func check(ok: bool, what: String) -> void:
	if not ok:
		failures += 1
		printerr("BOUNDED FAIL: ", what)


func _init() -> void:
	_run.call_deferred()


func big(path: String, size: int) -> void:
	var f := FileAccess.open(path, FileAccess.WRITE)
	f.seek(size - 1)   # a sparse file: the test itself allocates nothing
	f.store_8(0)
	f.close()


func _run() -> void:
	var dir := ""
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--dir="):
			dir = a.substr(6)
	var info: RefCounted = BuildInfo.load_info()
	var zero_key := PackedByteArray()
	zero_key.resize(32)
	info.release_keys = [zero_key]
	var u := Updater.new(info)
	u.launcher_dir = dir
	FileAccess.open(dir.path_join("OpenBFME.x86_64"), FileAccess.WRITE).close()
	FileAccess.open(dir.path_join("OpenBFME.exe"), FileAccess.WRITE).close()
	for c in [["payload.json", 4 << 20, "payload.json is 4194304 bytes (at most 65536)"],
			["payload.json.sig", 32 << 20, "payload.json.sig is 33554432 bytes (an Ed25519 signature is 64)"]]:
		for n in ["payload.json", "payload.json.sig"]:
			var f := FileAccess.open(dir.path_join(n), FileAccess.WRITE)
			f.store_string("{}" if n == "payload.json" else "x".repeat(64))
			f.close()
		big(dir.path_join(c[0]), c[1])
		var before := OS.get_static_memory_peak_usage()
		var b: Dictionary = u.bundled()
		var grew := OS.get_static_memory_peak_usage() - before
		print("BOUNDED %s: %s, peak memory grew %d bytes" % [c[0], u.bundled_problem(), grew])
		check(b.is_empty() and u.bundled_problem() == c[2], "%s: '%s'" % [c[0], u.bundled_problem()])
		# tolerance: the limit itself (64 KiB) and the bookkeeping around it, far below the file
		check(grew < (1 << 20), "%s: peak memory grew %d bytes" % [c[0], grew])
	print("BOUNDED OK" if failures == 0 else "BOUNDED FAILED: %d" % failures)
	quit(0 if failures == 0 else 1)
