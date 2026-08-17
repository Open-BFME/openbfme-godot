extends SceneTree
## Fortress artillery expansions must mount their engine ON THE TOWER DECK.
##
## Owner playtest bug: "the trebuchets are not physically spawned on top of the
## tower parts." Retail builds a fortress artillery expansion as two objects: the
## tower shell the player buys on the pad, and the engine the shell spawns onto
## its deck with an authored local offset.
##
## ORACLE (pure RotWK 2.01 tree,
## workspace/retail-work/cache/effective-assets/data/ini):
##   object/goodfaction/structures/men/trebuchetexpansion.ini:314-321
##       Behavior = ObjectCreationUpgrade MakeTheFreeTreb2
##           ThingToSpawn = MenTrebuchetFortress
##           Offset       = X:12.0 Y:0.0 Z:51.0
##   the side variant at :367-373 authors  X:-15.0 Y:0.0 Z:51.0
##   the shell authors GeometryHeight = 50.0, so Z:51 IS the deck
##   locomotor.ini:1836  FloatingCatapultLocomotor  ZAxisBehavior = FLOATING_Z
##       (why the engine stays up there instead of terrain-snapping down)
##
## This runner reads the authored strings back out of the MOUNTED pack rather
## than restating them, so it fails if a republish drops them, and pins the
## source-to-local mapping that turns Z:51 into world height.

const MountScript = preload("res://src/retail_slice/retail_expansion_mount.gd")

## data/ini/.../trebuchetexpansion.ini:321 and :373, verbatim.
const AUTHORED_CORNER_OFFSET := Vector3(12.0, 0.0, 51.0)
const AUTHORED_SIDE_OFFSET := Vector3(-15.0, 0.0, 51.0)
## The shell's own authored GeometryHeight. The engine must sit at or above the
## deck, never at the tower's foot.
const SHELL_GEOMETRY_HEIGHT := 50.0
## Fords of Isen map transform (RETAIL_RENDER_CAMERA local_transform_scale).
const FORDS_TRANSFORM_SCALE := 0.02649232738129

## The converted engine visuals the mounted packs still do not carry. Retail arms
## both Men trebuchet towers with MenTrebuchetFortress and neither the active nor
## any supplemental pack converts it, so the tower renders bare however correct
## the mount offset is. EMPTY THIS LIST when the importer ships the visual.
const EXPECTED_MISSING_TURRET_VISUALS: Array[String] = [
	"bfme2.object.men-trebuchet-fortress",
]

var passed := 0
var failed := 0
var missing_turret_visuals: Array[String] = []
var _pack_section_completed := false

const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")
var _runner_watchdog := RunnerWatchdogScript.new()


func _initialize() -> void:
	_runner_watchdog.start(self, "RETAIL_EXPANSION_TURRET_MOUNT_RUNNER")
	call_deferred("_run")


func _run() -> void:
	_check_parser()
	_check_mapping()
	await _check_mounted_pack()
	_check("pack_section_ran_to_completion", _pack_section_completed)
	print("RETAIL_EXPANSION_TURRET_MOUNT_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)


func _check_parser() -> void:
	## The importer keeps ObjectCreationUpgrade as an OPAQUE authored string
	## (module_contracts.py OPAQUE_DEFERRED_MODULE_KINDS), so the consumer has to
	## tokenise `X:.. Y:.. Z:..` itself. Everything below is retail syntax.
	_check(
		"parses_the_authored_men_corner_offset",
		MountScript.parse_authored_source_offset("X:12.0 Y:0.0 Z:51.0").is_equal_approx(AUTHORED_CORNER_OFFSET)
	)
	_check(
		"parses_the_authored_men_side_offset",
		MountScript.parse_authored_source_offset("X:-15.0 Y:0.0 Z:51.0").is_equal_approx(AUTHORED_SIDE_OFFSET)
	)
	# Other factions' artillery expansions, same idiom, as a spread check.
	_check(
		"parses_the_dwarven_catapult_offset",
		MountScript.parse_authored_source_offset("X:-18.0 Y:0.0 Z:51.0").is_equal_approx(Vector3(-18.0, 0.0, 51.0))
	)
	_check(
		"parses_the_mordor_catapult_offset",
		MountScript.parse_authored_source_offset("X:-16.0 Y:0.0 Z:48.0").is_equal_approx(Vector3(-16.0, 0.0, 48.0))
	)
	# An unreadable string must mount at the shell origin, not fling the engine.
	_check("unparsable_offset_is_the_origin", MountScript.parse_authored_source_offset("").is_equal_approx(Vector3.ZERO))
	_check("garbage_offset_is_the_origin", MountScript.parse_authored_source_offset("nonsense").is_equal_approx(Vector3.ZERO))
	# The Men trebuchet tower authors TWO ObjectCreationUpgrade rows and only the
	# second names a ThingToSpawn; the row that names the engine must win.
	var gameplay := {
		"moduleContracts": [
			{
				"module": "ObjectCreationUpgrade",
				"tag": "MakeTheFreeTreb",
				"fields": {"Offset": {"authored": "X:99.0 Y:0.0 Z:0.0"}},
			},
			{
				"module": "ObjectCreationUpgrade",
				"tag": "MakeTheFreeTreb2",
				"fields": {
					"Offset": {"authored": "X:12.0 Y:0.0 Z:51.0"},
					"ThingToSpawn": {"authored": "MenTrebuchetFortress"},
				},
			},
		]
	}
	_check(
		"the_row_that_names_the_engine_wins",
		MountScript.authored_spawn_offset(gameplay, "MenTrebuchetFortress").is_equal_approx(AUTHORED_CORNER_OFFSET),
		str(MountScript.authored_spawn_offset(gameplay, "MenTrebuchetFortress"))
	)


func _check_mapping() -> void:
	## SAGE (x, y, z-up) into the slice's local basis: the camera metadata records
	## the mapping as `godot=(sage.x, sage.z, -sage.y)`. The engine's LIFT is the
	## authored Z, and it must survive as world height.
	var local := MountScript.source_offset_to_local(AUTHORED_CORNER_OFFSET, FORDS_TRANSFORM_SCALE)
	print("RETAIL_EXPANSION_TURRET_MOUNT corner_local=%s" % str(local))
	_check(
		"authored_z_becomes_world_height",
		is_equal_approx(local.y, AUTHORED_CORNER_OFFSET.z * FORDS_TRANSFORM_SCALE),
		"y=%.4f expected %.4f" % [local.y, AUTHORED_CORNER_OFFSET.z * FORDS_TRANSFORM_SCALE]
	)
	_check(
		"authored_x_becomes_lateral_offset",
		is_equal_approx(local.x, AUTHORED_CORNER_OFFSET.x * FORDS_TRANSFORM_SCALE),
		"x=%.4f" % local.x
	)
	# THE DEFECT, STATED DIRECTLY: the engine must clear the tower deck. The old
	# code parented it at identity, so this was 0.0 - the trebuchet stood on the
	# ground at the tower's foot.
	_check(
		"the_engine_clears_the_tower_deck",
		local.y >= SHELL_GEOMETRY_HEIGHT * FORDS_TRANSFORM_SCALE,
		"lift=%.4f deck=%.4f" % [local.y, SHELL_GEOMETRY_HEIGHT * FORDS_TRANSFORM_SCALE]
	)
	var side_local := MountScript.source_offset_to_local(AUTHORED_SIDE_OFFSET, FORDS_TRANSFORM_SCALE)
	_check(
		"corner_and_side_towers_mount_at_different_places",
		absf(side_local.x - local.x) >= 0.5,
		"corner_x=%.4f side_x=%.4f" % [local.x, side_local.x]
	)


func _check_mounted_pack() -> void:
	## The authored offsets must actually be IN the pack this machine runs, read
	## back rather than restated. ContentDB resolves over its first frames.
	var content_db = root.get_node_or_null("ContentDB")
	if not _check("pack_content_db_available", content_db != null):
		return
	await process_frame
	await process_frame
	var checked := 0
	var expected := {
		"MenTrebuchetExpansion": AUTHORED_CORNER_OFFSET,
		"MenTrebuchetSideExpansion": AUTHORED_SIDE_OFFSET,
	}
	for object_id in expected.keys():
		var document: Dictionary = content_db.get_playable_structure_runtime(String(object_id))
		if document.is_empty():
			_check("pack_carries_%s" % object_id, false, "no compiled structure document")
			continue
		var gameplay: Dictionary = (
			(document.get("registration", {}) as Dictionary).get("gameplay", {}) as Dictionary
		)
		var combat: Dictionary = gameplay.get("combat", {}) as Dictionary
		var spawned := String(combat.get("spawnedObjectId", ""))
		var offset: Vector3 = MountScript.authored_spawn_offset(gameplay, spawned)
		print("RETAIL_EXPANSION_TURRET_MOUNT pack[%s] spawned='%s' offset=%s" % [object_id, spawned, str(offset)])
		_check(
			"pack_carries_the_authored_offset_for_%s" % object_id,
			offset.is_equal_approx(expected[object_id] as Vector3),
			"read %s, retail authors %s" % [str(offset), str(expected[object_id])]
		)
		# THE BLOCKING GAP, ASSERTED RATHER THAN ASSUMED. The offset above is
		# correct and is applied at spawn, but nothing is mounted unless the pack
		# also carries a converted VISUAL for the engine the shell names. On the
		# shipped selection it does not: the sim asks for
		# `bfme2.object.men-trebuchet-fortress` and ContentDB has no bundle object
		# under that id, so the slice records a named gap and the tower renders
		# bare. This row states which of the two worlds we are in, and flips to
		# asserting a mounted turret the moment an importer change plus
		# import-faction --convert / publish-faction-to-slice /
		# update-selection-entry ships the visual.
		if spawned != "":
			var runtime_id := "bfme2.object.%s" % _slugify(spawned)
			var bundle: Dictionary = content_db.get_bundle_object(runtime_id)
			print("RETAIL_EXPANSION_TURRET_MOUNT turret_bundle[%s] id=%s present=%s" % [
				object_id, runtime_id, str(not bundle.is_empty())
			])
			if bundle.is_empty():
				print("RETAIL_EXPANSION_TURRET_MOUNT PACK_GAP %s: no converted visual for '%s'" % [object_id, runtime_id])
				if not missing_turret_visuals.has(runtime_id):
					missing_turret_visuals.append(runtime_id)
		# NAMED PACK GAP, NOT A PASS. Retail arms this tower with an engine
		# (ThingToSpawn = MenTrebuchetFortress). The mounted pack carries no
		# compiled `combat.spawnedObjectId`, so the slice has nothing to mount and
		# the tower renders bare however correct the offset is. Reported here
		# every run so the gap cannot be mistaken for working artillery. Closing
		# it needs an importer change plus import-faction --convert +
		# publish-faction-to-slice + update-selection-entry.
		if spawned == "":
			print("RETAIL_EXPANSION_TURRET_MOUNT PACK_GAP %s: authored turret, no compiled combat.spawnedObjectId" % object_id)
		checked += 1
	_check("pack_offsets_were_read_for_both_expansions", checked == expected.size(), "%d of %d" % [checked, expected.size()])
	# THE GAP IS ASSERTED, NOT JUST PRINTED.
	#
	# A bare print cannot fail, so a future republish that DROPPED a converted
	# engine visual would be silent here - and so would the happy case where the
	# importer finally ships one. Pinning the exact set turns both directions
	# red: ship the engine and this row fails until EXPECTED_MISSING_TURRET_
	# VISUALS is emptied (and the offset assertions above become live); lose
	# another visual and it fails immediately.
	missing_turret_visuals.sort()
	var expected_missing := EXPECTED_MISSING_TURRET_VISUALS.duplicate()
	expected_missing.sort()
	_check(
		"the_missing_turret_visual_set_is_exactly_the_known_pack_gap",
		missing_turret_visuals == expected_missing,
		"observed %s, expected %s" % [str(missing_turret_visuals), str(expected_missing)]
	)
	_pack_section_completed = true


func _slugify(source_object_id: String) -> String:
	## `MenTrebuchetFortress` -> `men-trebuchet-fortress`, the runtime-id shape the
	## slice asks ContentDB for.
	var out := ""
	for index in source_object_id.length():
		var character := source_object_id[index]
		if index > 0 and character == character.to_upper() and character != character.to_lower():
			out += "-"
		out += character.to_lower()
	return out


func _check(name: String, condition: bool, detail: String = "") -> bool:
	if condition:
		passed += 1
		print("RETAIL_EXPANSION_TURRET_MOUNT PASS %s" % name)
	else:
		failed += 1
		printerr("RETAIL_EXPANSION_TURRET_MOUNT FAIL %s%s" % [name, " (%s)" % detail if detail != "" else ""])
	return condition
