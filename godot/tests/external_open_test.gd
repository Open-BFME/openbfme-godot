extends SceneTree

const Opener := preload("res://scripts/external_open.gd")


func _initialize() -> void:
	Opener.requests.clear()
	var url := "https://openbfme.com"
	var path := OS.get_user_data_dir().path_join("logs")
	# The native website bridge calls the resource's static method, without a script instance.
	var bridge := ResourceLoader.load("res://scripts/external_open.gd")
	var release := root.get_node("Release")
	var ok := Opener.testing() and int(bridge.call("open_url", url)) == OK and int(release.call("open_user_folder", 0)) == OK
	ok = ok and Opener.requests == [{"kind": "url", "target": url}, {"kind": "file_manager", "target": path}]
	print("EXTERNAL OPEN ", "PASS (2 recorded requests)" if ok else "FAIL")
	quit(0 if ok else 1)
