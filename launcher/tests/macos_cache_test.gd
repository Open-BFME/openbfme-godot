## Lane MACPLAY-1 (Sol r1 on DEVPORT-6, P2): the macOS manifest cache. tools/release/test_macplay.py drives it:
##   godot --headless --path launcher --script res://tests/macos_cache_test.gd -- --origin=<server> --key=<hex> --tag=<tag> --mode=<m>
## modes: "fetch" (Updater.macos_release_manifest once; prints "CACHE RESULT ok=<bool> error=<text>" and the cache folder's files),
## "kill" (the same, killed by the updater's own test point between writing the pair and publishing it), "incomplete" / "badsig"
## (leave a cache folder with only the manifest / with a wrong signature, then fetch), "linkentry" / "linkparent" (Sol r2: the tag's
## cache folder / cache/manifests-macos is a symbolic link to a folder outside the cache holding an invalid manifest and keep.txt; then
## fetch; prints "CACHE SENTINEL keep=<bool> manifest=<its manifest-macos.json>" and "CACHE ENTRY link=<bool>").
extends SceneTree

const Updater := preload("res://scripts/core/updater.gd")


class CacheInfo:
	extends RefCounted
	var version := "v1.3.0-preview.1"
	var display_version := "v1.3.0-preview.1"
	var repo := "Test/repo"
	var packaged := false
	var commit := ""
	var release_keys: Array = []   # lane SIGNKMS-1: the trusted key list
	var error := ""
	var macos_updates := true
	var test_build := true

	func platform() -> String:
		return "macos-arm64"

	func os_name() -> String:
		return OS.get_name()

	func tests_allowed() -> bool:
		return true

	func release_version() -> String:
		return version


func _init() -> void:
	var a := {}
	for arg in OS.get_cmdline_user_args():
		for k in ["origin", "key", "tag", "mode"]:
			if arg.begins_with("--%s=" % k):
				a[k] = arg.substr(k.length() + 3)
	if a.size() != 4:
		print("CACHE FAIL arguments")
		quit(2)
		return
	var info := CacheInfo.new()
	info.release_keys = [String(a.key).hex_decode()]
	var u := Updater.new(info)
	var why := u.set_test_api_base(a.origin)
	if why != "":
		print("CACHE FAIL " + why)
		quit(2)
		return
	var tag: String = a.tag
	var dir := u.data_dir.path_join("cache/manifests-macos").path_join(tag)
	var sentinel := u.data_dir.path_join("outside-sentinel")
	if a.mode != "fetch":
		# a fresh case (the Mac CI's godot wrapper gives every run the same data folder); links of an earlier case first, alone
		for link in [u.data_dir.path_join("cache/manifests-macos"), dir]:
			if DirAccess.open(link.get_base_dir()) != null and DirAccess.open(link.get_base_dir()).is_link(link.get_file()):
				DirAccess.remove_absolute(link)
		Updater.remove_tree(dir)
		Updater.remove_tree(sentinel)
	if a.mode in ["linkentry", "linkparent"]:
		var target := sentinel.path_join(tag) if a.mode == "linkparent" else sentinel
		DirAccess.make_dir_recursive_absolute(target)
		for item in [["keep.txt", "not the launcher's"], ["manifest-macos.json", "{}"]]:
			var w := FileAccess.open(target.path_join(item[0]), FileAccess.WRITE)
			w.store_string(item[1])
			w.close()
		if a.mode == "linkparent":
			DirAccess.make_dir_recursive_absolute(u.data_dir.path_join("cache"))
			Updater.remove_tree(u.data_dir.path_join("cache/manifests-macos"))
			DirAccess.open(u.data_dir).create_link(sentinel, u.data_dir.path_join("cache/manifests-macos"))
		else:
			DirAccess.make_dir_recursive_absolute(dir.get_base_dir())
			DirAccess.open(u.data_dir).create_link(sentinel, dir)
	if a.mode in ["incomplete", "badsig"]:
		DirAccess.make_dir_recursive_absolute(dir)
		var f := FileAccess.open(dir.path_join("manifest-macos.json"), FileAccess.WRITE)
		f.store_string("{}")
		f.close()
		if a.mode == "badsig":
			f = FileAccess.open(dir.path_join("manifest-macos.json.sig"), FileAccess.WRITE)
			f.store_buffer(_zeros(64))
			f.close()
	if a.mode == "kill":
		Updater.kill_at = "manifest-cache-written"
	var base := "%s/dl/%s/" % [a.origin, tag]
	var release := {"tag": tag, "prerelease": tag.contains("-"), "assets": {
		"manifest-macos.json": {"url": base + "manifest-macos.json"}, "manifest-macos.json.sig": {"url": base + "manifest-macos.json.sig"}}}
	var r := u.macos_release_manifest(release)
	print("CACHE RESULT ok=%s error=%s" % [str(r.ok), str(r.get("error", ""))])
	var files := PackedStringArray()
	if DirAccess.dir_exists_absolute(dir):
		files = DirAccess.get_files_at(dir)
	print("CACHE FILES " + ",".join(files))
	if a.mode in ["linkentry", "linkparent"]:
		var target := sentinel.path_join(tag) if a.mode == "linkparent" else sentinel
		print("CACHE SENTINEL keep=%s manifest=%s" % [str(FileAccess.file_exists(target.path_join("keep.txt"))),
			FileAccess.get_file_as_string(target.path_join("manifest-macos.json"))])
		print("CACHE ENTRY link=%s" % str(DirAccess.open(dir.get_base_dir()) != null and DirAccess.open(dir.get_base_dir()).is_link(tag)))
	quit(0 if r.ok else 1)


static func _zeros(n: int) -> PackedByteArray:
	var b := PackedByteArray()
	b.resize(n)
	return b
