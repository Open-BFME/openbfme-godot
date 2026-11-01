## Kept identical in the game and launcher: each exports its own res:// root.
extends RefCounted

static var requests: Array[Dictionary] = []


static func _static_init() -> void:
	if testing():
		OS.set_environment("OPENBFME_TEST", "1") # Child game/menu processes inherit the same guard.


static func testing() -> bool:
	if OS.get_environment("OPENBFME_TEST") == "1" or DisplayServer.get_name() == "headless":
		return true
	for arg in OS.get_cmdline_args():
		if arg.replace("\\", "/").contains("/tests/") and arg.ends_with(".gd"):
			return true
	return false


static func open_url(url: String) -> Error:
	if testing():
		requests.append({"kind": "url", "target": url})
		return OK
	return OS.shell_open(url)


static func show_in_file_manager(path: String) -> Error:
	if testing():
		requests.append({"kind": "file_manager", "target": path})
		return OK
	return OS.shell_show_in_file_manager(path, true)
