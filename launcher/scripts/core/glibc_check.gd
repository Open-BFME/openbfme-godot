## Lane GLIBC-1: before Play on Linux, the system's GLIBC against the one the installed game needs.
##
## A game whose library needs a newer GLIBC than the system's does not load (v0.3.0-preview.6 on Pop!_OS 22.04: "version `GLIBC_2.38' not
## found", then a fatal error); the player gets a message naming both versions instead. The need is the package's: tools/release/package.sh
## writes "requires GLIBC <x.y>" into the Linux package's VERSION file (tools/release/elf_glibc.py, the highest GLIBC symbol version of its
## executable and extension). Packages made before GLIBC-1 name none and are not checked. The system's version comes from
## `getconf GNU_LIBC_VERSION` ("glibc 2.35", glibc's gnu_get_libc_version), else the first line of `ldd --version`.
extends RefCounted

const LINE_PREFIX := "requires GLIBC "


## "2.35" from <game dir>/VERSION; "" when the package names no requirement
static func required(game_dir: String) -> String:
	for line in FileAccess.get_file_as_string(game_dir.path_join("VERSION")).split("\n"):
		if line.begins_with(LINE_PREFIX):
			return line.trim_prefix(LINE_PREFIX).strip_edges()
	return ""


## the system's GLIBC version ("2.35"), "" when neither getconf nor ldd answers with one
static func system_version() -> String:
	var out: Array = []
	if OS.execute("getconf", ["GNU_LIBC_VERSION"], out, true) == 0 and not out.is_empty():
		var v := _version_in(str(out[0]).strip_edges().get_slice(" ", 1))
		if v != "":
			return v
	out.clear()
	if OS.execute("ldd", ["--version"], out, true) == 0 and not out.is_empty():
		var first := str(out[0]).strip_edges().get_slice("\n", 0)
		return _version_in(first.get_slice(" ", first.get_slice_count(" ") - 1))
	return ""


static func _version_in(s: String) -> String:
	var r := RegEx.create_from_string("^\\d+\\.\\d+(\\.\\d+)?$")
	return s if r.search(s) != null else ""


## <0, 0, >0 like a - b, for dotted numbers
static func compare(a: String, b: String) -> int:
	var x := a.split(".")
	var y := b.split(".")
	for i in maxi(x.size(), y.size()):
		var d := (int(x[i]) if i < x.size() else 0) - (int(y[i]) if i < y.size() else 0)
		if d != 0:
			return d
	return 0


## "" when the game may start; else why not, for the player. `system` = the system's version ("": read it here).
static func check(game_dir: String, system := "") -> String:
	var need := required(game_dir)
	if need == "":
		return ""
	if _version_in(need) == "":
		return "its VERSION file names an unreadable GLIBC requirement (%s)" % need
	var have := system if system != "" else system_version()
	if have == "":
		return "it needs GLIBC %s or newer, and this system's GLIBC version could not be read (getconf GNU_LIBC_VERSION, ldd --version)" % need
	if compare(have, need) < 0:
		return too_old(need, have)
	return ""


static func too_old(need: String, have: String) -> String:
	return ("this Linux system is too old for this build. Update to a Linux distribution with GLIBC %s or newer (this system has %s). "
		+ "If you need help, use the OpenBFME Discord #bug-reports forum or GitHub issues.") % [need, have]
