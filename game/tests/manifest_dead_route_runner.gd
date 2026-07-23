extends SceneTree
## Regression gate for per-route producer resolution in
## RetailFactionManifest.from_registries: a unit authored at several producers
## keeps its resolved routes and is excluded ONLY when zero routes resolve.
## Dead routes are dropped with a recorded per-route reason; corrupt composite
## evidence still hard-errors; the roster pass and the production-rules pass
## must converge on every verdict. Pure synthetic registries (bare
## dictionaries), no content pack required.

const Manifest = preload("res://src/retail_slice/retail_faction_manifest.gd")

var passed := 0
var failed := 0


func _initialize() -> void:
	call_deferred("_run")


func _structure_doc(object_id: String, slug: String, evidence: String, builder_id: String) -> Dictionary:
	var routes: Array = []
	if builder_id != "":
		routes.append({"builderObjectId": builder_id, "surface": "construct"})
	return {
		"objectId": object_id,
		"slug": slug,
		"_pack_root": "res://packs/testfaction",
		"registration": {
			"production": {"evidence": evidence, "routes": routes},
			"presentation": {"buildingLifecycle": {"simulationFacts": {"maximumHealth": 1000}}},
			"gameplay": {"scalarFields": {
				"BuildCost": {"value": 300},
				"BuildTime": {"value": 20.0},
			}},
		},
	}


func _unit_doc(object_id: String, category: String, producer_ids: Array) -> Dictionary:
	var production: Array = []
	var slot := 1
	for producer_id in producer_ids:
		production.append({
			"producerObjectId": String(producer_id),
			"commandSetId": "%sCommandSet" % String(producer_id),
			"commandId": "Command_Construct%s" % object_id,
			"surface": "command-socket",
			"slot": slot,
			"rosterOrdinal": 0,
		})
		slot += 1
	return {
		"objectId": object_id,
		"category": category,
		"_pack_root": "res://packs/testfaction",
		"registration": {
			"production": production,
			"simulation": {
				"displayName": "Test %s" % object_id,
				"buildCost": 200,
				"buildTimeSeconds": 10.0,
				"commandPoints": 20,
				"memberCount": 2,
				"memberHealth": 120,
				"speed": 30.0,
				"visionRange": 200.0,
				"combat": {"damage": 30, "damageType": "slash"},
			},
		},
	}


func _builder_doc(object_id: String) -> Dictionary:
	return {
		"objectId": object_id,
		"category": "support",
		"_pack_root": "res://packs/testfaction",
		"registration": {"production": []},
	}


func _base_structures() -> Dictionary:
	return {
		"TestfactionFortress": _structure_doc("TestfactionFortress", "testfaction-fortress", "authored-construct-command", "TestfactionPorter"),
		"TestfactionBarracks": _structure_doc("TestfactionBarracks", "testfaction-barracks", "authored-construct-command", "TestfactionPorter"),
	}


func _run() -> void:
	var structures := _base_structures()
	var units := {
		"TestfactionPorter": _builder_doc("TestfactionPorter"),
		# The adversarial case: first authored route is a producer that never
		# loaded (SiegeWorks pattern), second is a valid loaded barracks. The
		# old code broke on the first binding and dropped the unit entirely.
		"TestfactionDualSoldier": _unit_doc("TestfactionDualSoldier", "infantry", ["TestfactionSiegeworks", "TestfactionBarracks"]),
		# Zero resolvable routes: still excluded, with the recorded reason.
		"TestfactionOrphan": _unit_doc("TestfactionOrphan", "cavalry", ["TestfactionSiegeworks"]),
		# Two dead routes: exclusion record carries per-route dropped_routes.
		"TestfactionDoubleOrphan": _unit_doc("TestfactionDoubleOrphan", "siege", ["TestfactionSiegeworks", "TestfactionLumberMill"]),
	}
	var manifest: Dictionary = Manifest.from_registries("testfaction", units, structures)
	_check(not manifest.has("_error"), "mixed-route manifest builds without error: %s" % String(manifest.get("_error", "")))
	if manifest.has("_error"):
		_finish()
		return

	var rules: Dictionary = manifest.get("unit_production_rules", {}) as Dictionary
	var dual_rule: Dictionary = rules.get("bfme2.object.testfaction-dual-soldier", {}) as Dictionary
	_check(not dual_rule.is_empty(), "dual-producer unit keeps a production rule from its valid route")
	_check(String(dual_rule.get("producer_kind", "")) == "barracks", "primary producer is the resolved barracks route: %s" % String(dual_rule.get("producer_kind", "")))
	var routes: Array = dual_rule.get("producer_routes", []) as Array
	_check(routes.size() == 1, "only the resolved route survives in producer_routes (%d)" % routes.size())
	var dropped: Array = dual_rule.get("dropped_routes", []) as Array
	_check(dropped.size() == 1, "exactly one dead route is recorded on the kept rule (%d)" % dropped.size())
	var dropped_row: Dictionary = (dropped[0] if dropped.size() > 0 else {}) as Dictionary
	_check(
		String(dropped_row.get("producer_source_object_id", "")) == "TestfactionSiegeworks"
		and String(dropped_row.get("reason", "")) == "producer-not-loaded:TestfactionSiegeworks",
		"dropped route records the producer and per-route reason: %s" % str(dropped_row)
	)

	var exclusions: Array = manifest.get("excluded_units", []) as Array
	var exclusions_by_id: Dictionary = {}
	var duplicate_exclusions := false
	for exclusion_value in exclusions:
		var exclusion: Dictionary = exclusion_value
		var object_id := String(exclusion.get("object_id", ""))
		if exclusions_by_id.has(object_id):
			duplicate_exclusions = true
		exclusions_by_id[object_id] = exclusion
	_check(not duplicate_exclusions, "roster and production passes never double-record an exclusion")
	_check(not exclusions_by_id.has("TestfactionDualSoldier"), "the kept dual-producer unit is never excluded (pass convergence)")

	var orphan: Dictionary = exclusions_by_id.get("TestfactionOrphan", {}) as Dictionary
	_check(
		String(orphan.get("reason", "")) == "producer-not-loaded:TestfactionSiegeworks",
		"zero-route unit is excluded with the producer-not-loaded reason: %s" % str(orphan)
	)
	_check(not orphan.has("dropped_routes"), "single-dead-route exclusion keeps its historical record shape")
	_check(not rules.has("bfme2.object.testfaction-orphan"), "zero-route unit never gains a production rule (pass convergence)")

	var double_orphan: Dictionary = exclusions_by_id.get("TestfactionDoubleOrphan", {}) as Dictionary
	_check(
		String(double_orphan.get("reason", "")) == "producer-not-loaded:TestfactionSiegeworks",
		"multi-dead-route exclusion keeps the first route's reason: %s" % str(double_orphan)
	)
	var double_dropped: Array = double_orphan.get("dropped_routes", []) as Array
	_check(double_dropped.size() == 2, "multi-dead-route exclusion records every dropped route (%d)" % double_dropped.size())
	if double_dropped.size() == 2:
		_check(
			String((double_dropped[1] as Dictionary).get("reason", "")) == "producer-not-loaded:TestfactionLumberMill",
			"second dead route keeps its own per-route reason"
		)
	_check(not rules.has("bfme2.object.testfaction-double-orphan"), "multi-dead-route unit never gains a production rule (pass convergence)")

	# Roster convergence: the excluded units never reach the spawn roster or
	# the AI plan; the kept unit drives both.
	var plan: Array = manifest.get("ai_production_plan", []) as Array
	_check(plan.has("bfme2.object.testfaction-dual-soldier"), "kept unit reaches the AI production plan")
	_check(
		not plan.has("bfme2.object.testfaction-orphan") and not plan.has("bfme2.object.testfaction-double-orphan"),
		"excluded units never reach the AI production plan"
	)

	# Fully-dead roster: when every trainable unit's routes are dead the
	# manifest still fails closed with the no-trainable-unit error.
	var dead_units := {
		"TestfactionPorter": _builder_doc("TestfactionPorter"),
		"TestfactionOrphan": _unit_doc("TestfactionOrphan", "cavalry", ["TestfactionSiegeworks"]),
	}
	var dead_manifest: Dictionary = Manifest.from_registries("testfaction", dead_units, _base_structures())
	_check(
		String(dead_manifest.get("_error", "")).contains("no trainable playable unit"),
		"all-dead roster still fails closed: %s" % String(dead_manifest.get("_error", ""))
	)

	# Corrupt composite evidence stays a hard manifest error even when the
	# unit also authors a valid route: that is data corruption, not a missing
	# structure, and must never be silently dropped as a dead route.
	var corrupt_structures := _base_structures()
	corrupt_structures["TestfactionCitadel"] = _structure_doc("TestfactionCitadel", "testfaction-citadel", "lifecycle-resource", "")
	var corrupt_units := {
		"TestfactionPorter": _builder_doc("TestfactionPorter"),
		"TestfactionDualSoldier": _unit_doc("TestfactionDualSoldier", "infantry", ["TestfactionBarracks", "TestfactionCitadel"]),
	}
	var corrupt_manifest: Dictionary = Manifest.from_registries("testfaction", corrupt_units, corrupt_structures)
	var corrupt_error := String(corrupt_manifest.get("_error", ""))
	_check(
		corrupt_error.contains("TestfactionCitadel") and corrupt_error.contains("lifecycle-resource"),
		"corrupt composite evidence still hard-errors despite a valid sibling route: %s" % corrupt_error
	)

	# Composite that claims engine-spawned evidence but does not author the
	# cited command set also stays a hard error under per-route semantics.
	var fake_structures := _base_structures()
	fake_structures["TestfactionCitadel"] = _structure_doc("TestfactionCitadel", "testfaction-citadel", "engine-spawned-composite", "")
	var fake_manifest: Dictionary = Manifest.from_registries("testfaction", corrupt_units, fake_structures)
	var fake_error := String(fake_manifest.get("_error", ""))
	_check(
		fake_error.contains("does not author command"),
		"composite that fails the command-set cross-check still hard-errors: %s" % fake_error
	)

	# All-resolved control: a unit whose routes all resolve carries no
	# dropped_routes field at all — untouched units keep their exact shape.
	var clean_units := {
		"TestfactionPorter": _builder_doc("TestfactionPorter"),
		"TestfactionDualSoldier": _unit_doc("TestfactionDualSoldier", "infantry", ["TestfactionFortress", "TestfactionBarracks"]),
	}
	var clean_manifest: Dictionary = Manifest.from_registries("testfaction", clean_units, _base_structures())
	_check(not clean_manifest.has("_error"), "all-resolved manifest builds without error: %s" % String(clean_manifest.get("_error", "")))
	var clean_rule: Dictionary = (clean_manifest.get("unit_production_rules", {}) as Dictionary).get("bfme2.object.testfaction-dual-soldier", {}) as Dictionary
	_check(not clean_rule.has("dropped_routes"), "fully-resolved rule never carries dropped_routes")
	_check((clean_rule.get("producer_routes", []) as Array).size() == 2, "fully-resolved unit keeps both routes")
	_check((clean_manifest.get("excluded_units", []) as Array).is_empty(), "all-resolved manifest records no exclusions")

	_finish()


func _check(condition: bool, label: String) -> void:
	if condition:
		passed += 1
		print("MANIFEST_DEAD_ROUTE PASS %s" % label)
	else:
		failed += 1
		printerr("MANIFEST_DEAD_ROUTE FAIL %s" % label)


func _finish() -> void:
	print("MANIFEST_DEAD_ROUTE_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)
