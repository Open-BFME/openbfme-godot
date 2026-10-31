extends SceneTree

const Opener := preload("res://scripts/core/external_open.gd")
const Paths := preload("res://scripts/ui/paths.gd")


func _initialize() -> void:
	Opener.requests.clear()
	var path := OS.get_user_data_dir().replace("\\", "/").simplify_path()
	var ok := Opener.testing() and Opener.open_url("https://openbfme.com") == OK
	ok = ok and Paths.show_in_file_manager(path).is_empty()
	ok = ok and Opener.requests == [{"kind": "url", "target": "https://openbfme.com"}, {"kind": "file_manager", "target": path}]
	print("EXTERNAL OPEN ", "PASS (2 recorded requests)" if ok else "FAIL")
	quit(0 if ok else 1)
