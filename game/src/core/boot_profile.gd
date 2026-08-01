extends RefCounted
## Startup stopwatch for the boot path, gated on OPENBFME_PROFILE_BOOT=1.
##
## WHY THIS EXISTS. The project owner reported "several seconds of black window
## before anything appears". Measurement showed ~12.5 s elapsed before the first
## frame was ever drawn, and that headless and windowed runs cost the SAME - so
## it was never the graphics driver or window creation, it was our own boot work.
## Guessing at which of six autoloads, one 1943-line menu script, and a preload
## chain of ~30k lines of GDScript owned that time is exactly how people
## "optimise" the wrong thing. Every number in the report came from here.
##
## Marks are absolute engine ticks (Time.get_ticks_msec() counts from engine
## start), so a single mark tells you both when a stage began relative to process
## start and, by subtraction from the previous mark, what it cost. That matters
## for the very first mark: it is the cost of engine init + autoload script
## compilation, which we do not control and must not pretend we can.
##
## Zero cost when disabled: `enabled` is read once into a static and every mark()
## is a single boolean test before returning.
##
## Reached by `preload`, deliberately NOT by `class_name`. A non-editor run reads
## global class names from `.godot/global_script_class_cache.cfg`, which only the
## editor regenerates - a fresh checkout that has never opened the editor would
## fail every autoload with "Identifier not declared". preload has no such
## dependency.

static var _enabled_cached: int = -1
static var _marks: Array[Dictionary] = []
static var _last_ms: int = 0


static func enabled() -> bool:
	## Either OPENBFME_PROFILE_BOOT=1 or a `-- --profile-boot` user argument. The
	## argument form exists because tests/boot_startup_runner.gd measures the real
	## boot by launching the game as a child process, and passing a flag through
	## argv is deterministic where inheriting an environment variable is not.
	if _enabled_cached < 0:
		var on := OS.get_environment("OPENBFME_PROFILE_BOOT") == "1"
		if not on:
			on = OS.get_cmdline_user_args().has("--profile-boot")
		_enabled_cached = 1 if on else 0
	return _enabled_cached == 1


static func mark(label: String) -> void:
	if not enabled():
		return
	var now := Time.get_ticks_msec()
	var delta := now - _last_ms
	_last_ms = now
	_marks.append({"label": label, "at_ms": now, "delta_ms": delta})
	print("BOOT_PROFILE stage=%-38s delta_ms=%-6d at_ms=%d" % [label, delta, now])


static func measure(label: String, elapsed_ms: int) -> void:
	## A stage whose cost is NOT "time since the previous mark".
	##
	## WHY THIS EXISTS. `mark()` derives a stage's cost by subtracting the previous
	## mark, which is exactly right for a sequence of blocking stages and exactly
	## WRONG for work that has been spread across idle frames on purpose. The menu's
	## faction/map availability sweep is now stepped one unit per frame, so the gap
	## between the mark before it and the mark after it is mostly the player sitting
	## in an interactive menu - reporting that as "the sweep cost 5.3 s" would
	## describe a stall that does not happen.
	##
	## So a stepped stage reports two real numbers through here instead: the SUM of
	## its steps (the work actually done) and its WORST single step (the longest
	## frame the player could feel). Both are printed in the same line shape as
	## mark(), so tests/boot_startup_runner.gd parses them with no special case.
	if not enabled():
		return
	var now := Time.get_ticks_msec()
	_last_ms = now
	_marks.append({"label": label, "at_ms": now, "delta_ms": maxi(0, elapsed_ms)})
	print("BOOT_PROFILE stage=%-38s delta_ms=%-6d at_ms=%d" % [label, maxi(0, elapsed_ms), now])


static func marks() -> Array[Dictionary]:
	## Copy, so a test runner reading the timeline cannot mutate it.
	return _marks.duplicate(true)


static func elapsed_for(label: String) -> int:
	## Cost of the named stage, or -1 when it was never reached. Runners assert
	## budgets against this rather than re-timing the work themselves.
	for entry in _marks:
		if String(entry.get("label", "")) == label:
			return int(entry.get("delta_ms", -1))
	return -1
