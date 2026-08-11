extends SceneTree
## Regression gate for the ring-hero producer-binding fix: Galadriel's roster
## entry is retail's One Ring summon slot, never a trained production route.
## Her document must be excluded with an explicit recorded reason — in the
## slice classification, in the adapter, and in from_registries for direct
## callers — and must never fail the elves manifest with the wrong faction's
## fortress (a cohabiting pack binds her to MenFortress).

const Manifest = preload("res://src/retail_slice/retail_faction_manifest.gd")
const Adapter = preload("res://src/retail_slice/playable_unit_runtime_adapter.gd")

var passed := 0
var failed := 0


const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")
# Turns a GDScript runtime error inside `_run` — which unwinds past every
# `quit()` and would otherwise leave this headless process idling forever —
# into a loud non-zero exit. See tests/runner_watchdog.gd.
var _runner_watchdog := RunnerWatchdogScript.new()


func _initialize() -> void:
	_runner_watchdog.start(self, "RETAIL_RING_HERO_EXCLUSION_RUNNER")
	call_deferred("_run")


func _run() -> void:
	var content_db = root.get_node_or_null("/root/ContentDB")
	_check(content_db != null, "ContentDB autoload is available")
	if content_db == null:
		_finish()
		return
	var galadriel: Dictionary = content_db.get_playable_unit_runtime("ElvenGaladriel_RingHero")
	_check(not galadriel.is_empty(), "ring-hero runtime document is indexed")
	if galadriel.is_empty():
		_finish()
		return
	var producer_ids: Array = []
	for binding in Adapter.producer_bindings(galadriel):
		producer_ids.append(String(binding.get("producer_source_object_id", "")))
	_check(not producer_ids.is_empty(), "ring-hero roster route is recorded")
	_check(
		Adapter.is_ring_hero_summon(galadriel),
		"adapter marks the zero-command-point hero roster entry as a ring-hero summon"
	)
	var verdict := Adapter.fieldability(galadriel)
	_check(
		not bool(verdict.get("ok", true)) and String(verdict.get("reason", "")) == "ring-hero-summon-not-trained",
		"adapter fieldability excludes the ring hero with the recorded summon reason: %s" % String(verdict.get("reason", ""))
	)
	var enabled_verdict := Adapter.fieldability(galadriel, true)
	_check(bool(enabled_verdict.get("ok", false)), "rule-on adapter fieldability admits the pack-shaped ring hero")

	# Direct callers (menu availability, probes) hand from_registries the full
	# registries. The ring hero must land in excluded_units with the summon
	# reason and must never surface her cohabiting-pack producer as an elves
	# manifest error.
	var elves: Dictionary = Manifest.from_registries(
		"elves",
		content_db.get_playable_unit_runtimes(),
		content_db.get_playable_structure_runtimes()
	)
	var elves_error := String(elves.get("_error", ""))
	_check(
		not elves_error.contains("MenFortress") and not elves_error.contains("ElvenGaladriel"),
		"elves manifest never blames the ring hero or her cohabiting-pack producer: %s" % elves_error
	)
	var exclusions: Array = elves.get("excluded_units", []) as Array
	var ring_exclusion := ""
	for exclusion_value in exclusions:
		var exclusion: Dictionary = exclusion_value
		if String(exclusion.get("object_id", "")) == "ElvenGaladriel_RingHero":
			ring_exclusion = String(exclusion.get("reason", ""))
	_check(
		ring_exclusion == "ring-hero-summon-not-trained",
		"elves manifest records the ring hero as a summon exclusion: %s" % str(exclusions)
	)
	if elves_error == "":
		var rules: Dictionary = elves.get("unit_production_rules", {}) as Dictionary
		_check(
			not rules.has("bfme2.object.elven-galadriel-ringhero"),
			"no trained production rule exists for the ring hero"
		)
	var enabled_elves: Dictionary = Manifest.from_registries(
		"elves",
		content_db.get_playable_unit_runtimes(),
		content_db.get_playable_structure_runtimes(),
		true
	)
	var enabled_error := String(enabled_elves.get("_error", ""))
	var enabled_rules: Dictionary = enabled_elves.get("unit_production_rules", {}) as Dictionary
	_check(enabled_error == "", "rule-on real manifest builds from mounted pack documents: %s" % enabled_error)
	var galadriel_rule: Dictionary = enabled_rules.get("bfme2.object.elven-galadriel-ring-hero", {}) as Dictionary
	_check(not galadriel_rule.is_empty() and String(galadriel_rule.get("producer_kind", "")) == "fortress", "rule-on real manifest surfaces Galadriel on the fortress roster")

	# Men is untouched: her object id stays outside the Men faction scope and
	# the Men manifest builds with her simply out of scope.
	var men: Dictionary = Manifest.from_registries(
		"men",
		content_db.get_playable_unit_runtimes(),
		content_db.get_playable_structure_runtimes()
	)
	_check(not men.has("_error"), "men manifest builds with the elves pack cohabiting: %s" % String(men.get("_error", "")))
	_finish()


func _check(condition: bool, label: String) -> void:
	if condition:
		passed += 1
		print("RING_HERO_EXCLUSION PASS %s" % label)
	else:
		failed += 1
		printerr("RING_HERO_EXCLUSION FAIL %s" % label)


func _finish() -> void:
	print("RING_HERO_EXCLUSION_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)
