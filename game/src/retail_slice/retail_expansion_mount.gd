class_name RetailExpansionMount
extends RefCounted
## Where a fortress artillery expansion's engine sits on its tower.
##
## RETAIL MOUNTS THE TURRET WITH AN AUTHORED OFFSET, NOT A BONE.
##
## A fortress artillery expansion is two objects: the tower shell the player buys
## on the pad, and the engine it carries. The shell spawns the engine with
##   Behavior = ObjectCreationUpgrade MakeTheFreeTreb2
##       ThingToSpawn = MenTrebuchetFortress
##       Offset       = X:12.0 Y:0.0 Z:51.0
## (data/ini/object/goodfaction/structures/men/trebuchetexpansion.ini:314-321;
## the side variant at :367-373 authors X:-15.0). That Z:51 is the tower deck -
## the shell authors GeometryHeight = 50.0 - and the engine stays up there
## because FloatingCatapultLocomotor sets ZAxisBehavior = FLOATING_Z
## (locomotor.ini:1836), so it never terrain-snaps back down. Every faction uses
## the same idiom: Dwarves Z:51, Isengard Z:50, Mordor Z:48, Wild Z:20.
##
## The offset survives the importer as an OPAQUE authored string on the module
## contract (module_contracts.py OPAQUE_DEFERRED_MODULE_KINDS), carrying no
## numeric members, so it must be tokenised here.
##
## Kept free of autoload references so a gate can preload it directly.


static func parse_authored_source_offset(authored: String) -> Vector3:
	## `X:12.0 Y:0.0 Z:51.0` in retail SOURCE units. Returns Vector3.ZERO for
	## anything unreadable, so a bad string mounts the engine at the shell origin
	## rather than flinging it somewhere arbitrary.
	var found := {}
	for token in authored.strip_edges().split(" ", false):
		var parts := String(token).split(":", false)
		if parts.size() != 2:
			continue
		var axis := String(parts[0]).strip_edges().to_upper()
		var value := String(parts[1]).strip_edges()
		if not value.is_valid_float() or not ["X", "Y", "Z"].has(axis):
			continue
		found[axis] = float(value)
	if found.is_empty():
		return Vector3.ZERO
	return Vector3(
		float(found.get("X", 0.0)), float(found.get("Y", 0.0)), float(found.get("Z", 0.0))
	)


static func source_offset_to_local(source_offset: Vector3, transform_scale: float) -> Vector3:
	## SAGE (x, y, z-up) into the slice's local basis - the mapping the camera
	## metadata records as `godot=(sage.x, sage.z, -sage.y)` - then scaled by the
	## map transform. At the Fords scale 0.02649232738129 the authored Z:51 lifts
	## the trebuchet 1.3511 world units onto the tower deck.
	return Vector3(source_offset.x, source_offset.z, -source_offset.y) * transform_scale


static func authored_spawn_offset(gameplay: Dictionary, spawned_source_id: String) -> Vector3:
	## Source-unit mount offset for the engine this shell spawns. Prefers the
	## ObjectCreationUpgrade row that actually names `spawned_source_id`: the Men
	## trebuchet tower authors MakeTheFreeTreb with no ThingToSpawn AND
	## MakeTheFreeTreb2 with one, and only the second describes the engine.
	var fallback := Vector3.ZERO
	var fallback_found := false
	for row_value in gameplay.get("moduleContracts", []) as Array:
		var row := row_value as Dictionary
		if String(row.get("module", "")).to_lower() != "objectcreationupgrade":
			continue
		var fields := row.get("fields", {}) as Dictionary
		var authored := String((fields.get("Offset", {}) as Dictionary).get("authored", ""))
		if authored == "":
			continue
		var offset := parse_authored_source_offset(authored)
		var thing := String((fields.get("ThingToSpawn", {}) as Dictionary).get("authored", ""))
		if thing != "" and spawned_source_id != "" and thing.to_lower() == spawned_source_id.to_lower():
			return offset
		if not fallback_found:
			fallback = offset
			fallback_found = true
	return fallback
