extends SceneTree
## Pins the DEFERRED-BUT-NEVER-SKIPPED contract on the menu's skirmish options.
##
## `main_menu._ready()` no longer runs the faction/map availability sweep - it
## was a MEASURED 4.3 s (now ~1.7 s) of work filling a flyout that is hidden on
## the front page, and it sat directly on the pre-first-frame path. It is now
## deferred one frame, with `_ensure_skirmish_options()` guarding every reader.
##
## That is only safe while BOTH halves hold, so both are asserted here:
##   1. DEFERRED - the sweep has not run when the menu's `_ready` returns, so it
##      cannot climb back onto the boot path unnoticed.
##   2. NEVER SKIPPED - a reader that arrives before the deferred pass forces the
##      full sweep synchronously and gets real answers, not an empty map. This is
##      the half that keeps the fail-closed launch gate honest: if the guard is
##      dropped, `retail_launch_error()` would start judging a launch against an
##      unpopulated availability table and could wave through a faction the
##      slice cannot field.
##
## LIVENESS: 8 checks. Raise this when you add checks; never lower it.

const EXPECTED_CHECKS := 8

var _checks := 0
var _failures: Array[String] = []


func _init() -> void:
	# A SceneTree script's _init runs before Main::start() has registered the
	# autoload names as global identifiers, so compiling main_menu.gd here would
	# fail on "Identifier not found: ModLoader" and hand back a scriptless node.
	# One frame is enough for the autoloads to exist.
	await process_frame
	root.size = Vector2i(1920, 1080)
	var packed: PackedScene = load("res://scenes/boot.tscn")
	_check("boot_scene_parses", packed != null, "scenes/boot.tscn did not load")
	if packed == null:
		_report()
		return

	# ---- half 1: the sweep is NOT on the _ready path. ------------------------
	# Read immediately after add_child, before yielding a frame, so the deferred
	# one-shot has provably not fired yet.
	var menu := packed.instantiate()
	root.add_child(menu)
	_check("sweep_is_deferred", menu.get("_skirmish_options_ready") == false,
		"the availability sweep ran inside main_menu._ready() - it belongs off the boot path")
	_check("availability_empty_before_sweep",
		(menu.get("_skirmish_availability") as Dictionary).is_empty(),
		"faction availability was already populated during _ready()")

	# ---- half 2: a reader forces it, synchronously and completely. ----------
	# retail_launch_error() is the fail-closed launch gate. Calling it here, still
	# without having yielded a frame, must produce a fully populated table.
	menu.call("retail_launch_error")
	_check("reader_forces_sweep", menu.get("_skirmish_options_ready") == true,
		"reading the launch gate did not force the deferred sweep")
	var availability := menu.get("_skirmish_availability") as Dictionary
	_check("availability_populated_by_reader", availability.size() == 7,
		"expected 7 factions after the forced sweep, got %d" % availability.size())
	# Every RETAIL_FACTIONS id must have a verdict; a missing key is what would
	# let a launch gate read "" (available) for a faction nobody classified.
	var missing: Array[String] = []
	for faction_value in menu.get("RETAIL_FACTIONS"):
		var faction_id := String((faction_value as Dictionary)["id"])
		if not availability.has(faction_id):
			missing.append(faction_id)
	_check("every_faction_has_a_verdict", missing.is_empty(),
		"no availability verdict for: %s" % ", ".join(missing))

	# Idempotent: a second reader must not re-run the sweep.
	var before := availability.duplicate()
	menu.call("get_retail_faction_availability")
	_check("sweep_runs_once", before == (menu.get("_skirmish_availability") as Dictionary),
		"the sweep re-ran on a second read")

	# And the deferred one-shot arriving afterwards must be a no-op, not a
	# second 1.7 s pass.
	await process_frame
	await process_frame
	_check("deferred_pass_is_a_noop_after_forcing",
		before == (menu.get("_skirmish_availability") as Dictionary),
		"the deferred pass re-ran the sweep after a reader had already forced it")

	menu.queue_free()
	await process_frame
	_report()


func _check(name: String, condition: bool, why: String) -> void:
	_checks += 1
	if condition:
		print("BOOT_DEFERRED PASS %s" % name)
	else:
		print("BOOT_DEFERRED FAIL %s (%s)" % [name, why])
		_failures.append(name)


func _report() -> void:
	print("BOOT_DEFERRED_RESULT passed=%d failed=%d" % [_checks - _failures.size(), _failures.size()])
	if _checks != EXPECTED_CHECKS:
		print("LIVENESS FAILURE: %d checks ran, %d expected." % [_checks, EXPECTED_CHECKS])
		quit(1)
	quit(0 if _failures.is_empty() else 1)
