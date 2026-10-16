## Lane LAUNCH-1: the launcher's log, user://launcher.log (appended by every process: the launcher, its update helper and the new
## launcher of a self-update write one story; above 1 MiB it is moved to launcher.old.log) and the console. Every line is flushed at
## once (a killed process loses nothing). The home folder is written as ~ (a tester may share the file). Thread-safe.
extends RefCounted

static var _mutex := Mutex.new()
static var _file: FileAccess
static var _home := ""


static func open() -> void:
	var dir := OS.get_user_data_dir()
	DirAccess.make_dir_recursive_absolute(dir)
	var path := dir.path_join("launcher.log")
	if FileAccess.file_exists(path) and FileAccess.open(path, FileAccess.READ).get_length() > (1 << 20):
		DirAccess.rename_absolute(path, dir.path_join("launcher.old.log"))
	_file = FileAccess.open(path, FileAccess.READ_WRITE if FileAccess.file_exists(path) else FileAccess.WRITE)
	if _file != null:
		_file.seek_end()
	_home = OS.get_environment("USERPROFILE" if OS.get_name() == "Windows" else "HOME")


static func scrub(text: String) -> String:
	if _home.length() > 1:
		text = text.replace(_home, "~")
		if OS.get_name() == "Windows":
			text = text.replace(_home.replace("\\", "/"), "~")
	return text


static func line(text: String) -> void:
	var s := scrub(text)
	_mutex.lock()
	print(s)
	if _file != null:
		_file.store_line("%s [%d] %s" % [Time.get_datetime_string_from_system(true), OS.get_process_id(), s])
		_file.flush()
	_mutex.unlock()
