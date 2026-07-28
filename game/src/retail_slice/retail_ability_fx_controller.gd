# No class_name: the slice loads this through a preloaded script constant, so
# headless runners do not depend on the editor-maintained global class cache.
extends Node3D
## Shared presentation cue for every hero ability and spellbook power cast.
##
## Before this existed there was no consumer for the sim's `ability.cast` event
## at all — a hero ability produced damage, knockback and a sound, and nothing
## on screen — while every spellbook power funnelled into a single hardcoded
## torus that was green for SpellBookHeal and orange for all eleven others. The
## authored FXList ids reached the runtime in both cases and were discarded.
##
## What this controller can honestly present today:
##   * one cue per cast, at the sim's target point,
##   * sized to the map-scaled radius the sim actually acted over,
##   * shaped by the authored FXList's retail geometry family (a ground-aligned
##     expanding shockwave for blast lists, a rising column for summons/buffs),
##   * and a diagnostics ledger naming every authored FXList id that had no
##     converted particle definition to bind to.
##
## The importer's ability-FX ingress lane
## (`importer/openbfme_importer/retail_ability_fx_ingress.py`) now resolves each
## authored id through FXList -> nested FXList -> ParticleSystem -> texture and
## seals the result into every unit and spellbook runtime document as an
## `fxBindings` block. `collect_fx_registry()` turns those blocks into the
## registry `configure()` takes, so a cast whose FXList converted is drawn with
## the emitter's authored ground alignment and authored `Color1`.
##
## What it still deliberately does not do: invent texture or particle motion,
## and never colour a cue from an FXList the pack did not convert. Ids that the
## lane could not close (retail W3D-geometry emitters such as EXLightRing.W3D,
## sound-only lists like FX_EomerSpearThrow) keep the neutral geometric cue and
## stay in `unresolved_fx_list_ids`.

## Retail FXLists whose particle systems are ground-aligned expanding
## shockwaves (FXParticleSystem ... IsGroundAligned = Yes with a positive
## SizeRate). Every one of these is reachable from a converted ability leaf in
## the active pack, so the family assignment is evidence-driven even though the
## particle definitions themselves are unconverted.
const SHOCKWAVE_EFFECT_KINDS: Array[String] = [
	"weapon-blast",
	"arrow-storm",
	"terror",
	"leadership-strip",
	"curse",
]
const COLUMN_EFFECT_KINDS: Array[String] = [
	"heal",
	"summon",
	"attribute-modifier",
	"experience-grant",
	"leadership-aura",
]
const SHOCKWAVE_POWER_KINDS: Array[String] = [
	"fire_weapon",
	"cloudbreak_stun",
]

## `WhichSpecialWeapon = 1|2|3` selects these retail model conditions in order
## (SPECIAL_WEAPON_ONE / _TWO / _THREE).
const SPECIAL_WEAPON_STATE_NAMES: Array[String] = [
	"specialWeaponOne",
	"specialWeaponTwo",
	"specialWeaponThree",
]
## Fire frame first, then the wind-up, then the channel: an ability that only
## authors part of the envelope still presents its own pose.
const ABILITY_PHASE_PREFERENCE: Array[String] = ["cast", "unpack", "prepare"]

const CUE_SECONDS := 0.65
const MIN_CUE_RADIUS := 1.5
const CUE_RING_THICKNESS := 0.12

## Cast counters and diagnostics, read by tests and by the presentation audit.
var cues_presented := 0
var casts_observed := 0
var unresolved_fx_list_ids: Array[String] = []
var cue_log: Array[Dictionary] = []

var _height_provider: Callable
var _resolved_fx_list_ids: Dictionary = {}
var _ability_animation_states: Dictionary = {}


static func collect_fx_registry(documents: Array) -> Dictionary:
	## Build the resolved-FXList registry from converted runtime documents.
	##
	## The importer's ability-FX ingress lane seals one `fxBindings` block per
	## unit (`registration.fxBindings`) and per spellbook
	## (`registration.presentation.fxBindings`). Each block names the FXLists
	## that reached converted particle definitions, and carries the authored
	## scalars (`Color1`, `IsGroundAligned`, `SizeRate`) of every definition
	## behind them. Nothing here is invented: an id absent from every block
	## stays unresolved and its cast keeps the neutral geometric fallback.
	var registry: Dictionary = {}
	for document_value in documents:
		if typeof(document_value) != TYPE_DICTIONARY:
			continue
		var registration_value: Variant = (document_value as Dictionary).get("registration", {})
		if typeof(registration_value) != TYPE_DICTIONARY:
			continue
		var registration := registration_value as Dictionary
		var blocks: Array = []
		if typeof(registration.get("fxBindings", null)) == TYPE_DICTIONARY:
			blocks.append(registration["fxBindings"])
		var presentation_value: Variant = registration.get("presentation", {})
		if typeof(presentation_value) == TYPE_DICTIONARY:
			var presentation := presentation_value as Dictionary
			if typeof(presentation.get("fxBindings", null)) == TYPE_DICTIONARY:
				blocks.append(presentation["fxBindings"])
		for block_value in blocks:
			_merge_fx_bindings(registry, block_value as Dictionary)
	return registry


static func _merge_fx_bindings(registry: Dictionary, block: Dictionary) -> void:
	if String(block.get("schema", "")) != "openbfme.ability-fx-bindings" or int(block.get("schemaVersion", -1)) != 0:
		return
	var scalars_by_system: Dictionary = {}
	var registry_value: Variant = block.get("definitionRegistry", [])
	if typeof(registry_value) == TYPE_ARRAY:
		for row_value in registry_value as Array:
			if typeof(row_value) != TYPE_DICTIONARY:
				continue
			var row := row_value as Dictionary
			var definition_id := String(row.get("definitionId", ""))
			if definition_id == "" or scalars_by_system.has(definition_id):
				continue
			if typeof(row.get("authoredScalars", null)) == TYPE_DICTIONARY:
				scalars_by_system[definition_id] = row["authoredScalars"]
	var lists_value: Variant = block.get("fxLists", [])
	if typeof(lists_value) != TYPE_ARRAY:
		return
	for row_value in lists_value as Array:
		if typeof(row_value) != TYPE_DICTIONARY:
			continue
		var row := row_value as Dictionary
		var fx_id := String(row.get("fxListId", ""))
		var systems_value: Variant = row.get("resolvedParticleSystemIds", [])
		if fx_id == "" or typeof(systems_value) != TYPE_ARRAY or (systems_value as Array).is_empty():
			# An FXList whose every particle system failed to convert is not
			# resolved: it stays in the unresolved ledger rather than being
			# presented with art that does not exist.
			continue
		if registry.has(fx_id):
			continue
		var ground_aligned := false
		var color := Color(0.86, 0.90, 1.0)
		var has_authored_color := false
		for system_value in systems_value as Array:
			var scalars_value: Variant = scalars_by_system.get(String(system_value), null)
			if typeof(scalars_value) != TYPE_DICTIONARY:
				continue
			var scalars := scalars_value as Dictionary
			if String(scalars.get("isgroundaligned", "")).to_lower() == "yes":
				ground_aligned = true
				if not has_authored_color:
					var parsed := _parse_authored_color(String(scalars.get("color1", "")))
					if parsed.a > 0.0:
						color = Color(parsed.r, parsed.g, parsed.b, 1.0)
						has_authored_color = true
			elif not has_authored_color:
				var fallback := _parse_authored_color(String(scalars.get("color1", "")))
				if fallback.a > 0.0:
					color = Color(fallback.r, fallback.g, fallback.b, 1.0)
		registry[fx_id] = {
			"ground_aligned": ground_aligned,
			"color": color,
			"has_authored_color": has_authored_color,
			"particle_system_ids": (systems_value as Array).duplicate(),
		}


static func _parse_authored_color(value: String) -> Color:
	## Retail authors particle colour keys as `R:82 G:139 B:235 <frame>`.
	## Returns alpha 0 when the value is absent or malformed so the caller can
	## tell "no authored colour" from "authored black".
	if value == "":
		return Color(0.0, 0.0, 0.0, 0.0)
	var channels := {"r": -1.0, "g": -1.0, "b": -1.0}
	for token in value.split(" ", false):
		var parts := token.split(":", false)
		if parts.size() != 2:
			continue
		var key := String(parts[0]).to_lower()
		if not channels.has(key) or not String(parts[1]).is_valid_float():
			continue
		channels[key] = clampf(float(parts[1]) / 255.0, 0.0, 1.0)
	if float(channels["r"]) < 0.0 or float(channels["g"]) < 0.0 or float(channels["b"]) < 0.0:
		return Color(0.0, 0.0, 0.0, 0.0)
	return Color(float(channels["r"]), float(channels["g"]), float(channels["b"]), 1.0)


static func collect_ability_animation_registry(documents: Array) -> Dictionary:
	## Join each authored SpecialPower to the caster pose retail animates it with.
	##
	## The FX half of a cast was already presented from converted particle
	## evidence, but the caster itself kept swinging: the pack compiler folds
	## every SPECIAL_WEAPON_* AnimationState into the generic "attack" semantic
	## state, so Gandalf's staff-lowering wizard blast clip reached the mesh
	## with no way to ask for it by name. Retail authors the join on the
	## ability's own update module:
	##
	##   * `WeaponFireSpecialAbilityUpdate.WhichSpecialWeapon = N` selects the
	##     `SPECIAL_WEAPON_<N>` AnimationState. gandalf.ini:1297-1312 gives
	##     SpecialAbilityWizardBlast `WhichSpecialWeapon = 2`; gandalf.ini:305
	##     binds `SPECIAL_WEAPON_TWO` to `GUGandalfG_SKL.GUGandalfG_SPCL`.
	##     The converter already seals that number as
	##     `abilities[].effect.specialWeaponSlot`.
	##   * `ArrowStormUpdate.UnpackingVariation = N` selects the
	##     `PACKING_TYPE_<N>` envelope (gandalf.ini:1330 for
	##     SpecialAbilityLightningSword), sealed as `effect.unpackingVariation`.
	##
	## Result maps a casefolded special-power id to the capability state base
	## name (`specialWeaponTwo`, `packingType1`, …). Abilities whose modules
	## author neither number are absent: nothing is guessed, and the caster
	## keeps whatever pose the sim state would have shown.
	var registry: Dictionary = {}
	for document_value in documents:
		if typeof(document_value) != TYPE_DICTIONARY:
			continue
		var registration_value: Variant = (document_value as Dictionary).get("registration", {})
		if typeof(registration_value) != TYPE_DICTIONARY:
			continue
		var abilities: Variant = (registration_value as Dictionary).get("abilities", [])
		if typeof(abilities) != TYPE_ARRAY:
			continue
		for ability_value in abilities as Array:
			if typeof(ability_value) != TYPE_DICTIONARY:
				continue
			var ability := ability_value as Dictionary
			var power_id := String(ability.get("specialPowerId", "")).to_lower()
			if power_id == "":
				continue
			var effect_value: Variant = ability.get("effect", {})
			if typeof(effect_value) != TYPE_DICTIONARY:
				continue
			var effect := effect_value as Dictionary
			var state := ""
			var slot := int(effect.get("specialWeaponSlot", 0))
			if slot >= 1 and slot <= SPECIAL_WEAPON_STATE_NAMES.size():
				state = String(SPECIAL_WEAPON_STATE_NAMES[slot - 1])
			elif int(effect.get("unpackingVariation", 0)) >= 1:
				state = "packingType%d" % int(effect["unpackingVariation"])
			if state == "":
				continue
			registry[power_id] = state
	return registry


func configure(
	height_provider: Callable,
	resolved_fx_list_ids: Dictionary = {},
	ability_animation_states: Dictionary = {}
) -> void:
	## `height_provider` maps a world XZ point to its presentation ground
	## height. `resolved_fx_list_ids` is the pack's converted FXList registry;
	## an empty registry means every authored id lands in the unresolved ledger.
	## `ability_animation_states` is `collect_ability_animation_registry`'s
	## special-power -> caster-pose join; an empty registry simply means no cast
	## requests an authored pose.
	_height_provider = height_provider
	_resolved_fx_list_ids = resolved_fx_list_ids.duplicate(true)
	_ability_animation_states = ability_animation_states.duplicate(true)


func resolve_cast_animation_states(event: Dictionary) -> Array[String]:
	## Capability state keys an `ability.cast` should try, best first.
	##
	## Retail channels a cast through a four-phase envelope; the fire frame
	## (`cast`) is the pose the ability is recognised by, so it is preferred and
	## the wind-up is the fallback for abilities that only author one.
	var result: Array[String] = []
	var power_id := String(event.get("special_power_id", "")).to_lower()
	var state := String(_ability_animation_states.get(power_id, ""))
	if state == "":
		return result
	for phase in ABILITY_PHASE_PREFERENCE:
		result.append("ability:%s:%s" % [state, phase])
	return result


func present_ability_cast(event: Dictionary) -> void:
	var point_value: Variant = event.get("point")
	if typeof(point_value) != TYPE_ARRAY or (point_value as Array).size() < 2:
		return
	casts_observed += 1
	var point := Vector2(float((point_value as Array)[0]), float((point_value as Array)[1]))
	var effect_kind := String(event.get("effect_kind", ""))
	var fx_lists := _string_array(event.get("fx_lists", []))
	_record_unresolved(fx_lists)
	var family := "none"
	if effect_kind in SHOCKWAVE_EFFECT_KINDS:
		family = "shockwave"
	elif effect_kind in COLUMN_EFFECT_KINDS:
		family = "column"
	var style := _authored_style(fx_lists)
	if family != "none" and style.has("family"):
		family = String(style["family"])
	_present(point, float(event.get("fx_radius", 0.0)), family, {
		"source": "ability.cast",
		"id": String(event.get("ability_id", "")),
		"effect_kind": effect_kind,
		"fx_lists": fx_lists,
		"fx_resolved": bool(style.get("resolved", false)),
	}, style.get("color", null))


func present_power_cast(event: Dictionary) -> void:
	var point_value: Variant = event.get("point")
	if typeof(point_value) != TYPE_ARRAY or (point_value as Array).size() < 2:
		return
	casts_observed += 1
	var point := Vector2(float((point_value as Array)[0]), float((point_value as Array)[1]))
	var effect_kind := String(event.get("effect_kind", ""))
	var fx_lists := _string_array(event.get("fx_lists", []))
	_record_unresolved(fx_lists)
	var family := "shockwave" if effect_kind in SHOCKWAVE_POWER_KINDS else "column"
	var style := _authored_style(fx_lists)
	if style.has("family"):
		family = String(style["family"])
	_present(point, float(event.get("fx_radius", 0.0)), family, {
		"source": "power.cast",
		"id": String(event.get("power_id", "")),
		"effect_kind": effect_kind,
		"fx_lists": fx_lists,
		"fx_resolved": bool(style.get("resolved", false)),
	}, style.get("color", null))


func _authored_style(fx_lists: Array[String]) -> Dictionary:
	## Prefer the authored particle evidence over the effect-kind heuristic.
	## A converted FXList tells us directly whether retail aligned its emitter
	## to the ground and what colour it started at; only when no id in the cast
	## resolved do we fall back to the kind-derived family and neutral colour.
	for fx_id in fx_lists:
		var entry_value: Variant = _resolved_fx_list_ids.get(fx_id, null)
		if typeof(entry_value) != TYPE_DICTIONARY:
			continue
		var entry := entry_value as Dictionary
		var style := {"resolved": true}
		if bool(entry.get("ground_aligned", false)):
			style["family"] = "shockwave"
		if typeof(entry.get("color", null)) == TYPE_COLOR and bool(entry.get("has_authored_color", false)):
			style["color"] = entry["color"]
		return style
	return {"resolved": false}


func _record_unresolved(fx_lists: Array[String]) -> void:
	for fx_id in fx_lists:
		if _resolved_fx_list_ids.has(fx_id):
			continue
		if not unresolved_fx_list_ids.has(fx_id):
			unresolved_fx_list_ids.append(fx_id)


func _string_array(value: Variant) -> Array[String]:
	var output: Array[String] = []
	if typeof(value) != TYPE_ARRAY:
		return output
	for entry in value as Array:
		var text := String(entry)
		if text != "":
			output.append(text)
	return output


func _present(
	point: Vector2,
	radius: float,
	family: String,
	record: Dictionary,
	authored_color: Variant = null
) -> void:
	var cue_radius := maxf(MIN_CUE_RADIUS, radius)
	record["radius"] = cue_radius
	record["family"] = family
	if typeof(authored_color) == TYPE_COLOR:
		record["authored_color"] = authored_color
	cue_log.append(record)
	if family == "none":
		# Self-targeted toggles (weapon/mount/stealth) author no radius in
		# retail either; their presentation is a model-condition swap, not a
		# world cue. Nothing to draw, and nothing invented.
		return
	if DisplayServer.get_name() == "headless" or not is_inside_tree():
		cues_presented += 1
		return
	var ground := _ground_height(point)
	if family == "shockwave":
		var ring_color: Color = authored_color if typeof(authored_color) == TYPE_COLOR else Color(0.86, 0.90, 1.0)
		_spawn_shockwave(Vector3(point.x, ground, point.y), cue_radius, ring_color)
	else:
		var column_color: Color = authored_color if typeof(authored_color) == TYPE_COLOR else Color(0.95, 0.93, 0.72)
		_spawn_column(Vector3(point.x, ground, point.y), cue_radius, column_color)
	cues_presented += 1


func _ground_height(point: Vector2) -> float:
	if _height_provider.is_valid():
		return float(_height_provider.call(point))
	return 0.0


func _spawn_shockwave(origin: Vector3, radius: float, color: Color) -> void:
	## Ground-aligned ring that expands to the sim's effect radius over the cue
	## window, matching the retail shockwave family's shape (IsGroundAligned,
	## positive SizeRate, alpha falling to zero). Colour is the authored
	## Color1 of the FXList's ground-aligned emitter when the importer's
	## ability-FX lane converted it, and neutral otherwise.
	var ring := MeshInstance3D.new()
	var mesh := TorusMesh.new()
	mesh.inner_radius = maxf(0.01, radius * (1.0 - CUE_RING_THICKNESS))
	mesh.outer_radius = radius
	ring.mesh = mesh
	var material := _cue_material(color)
	ring.material_override = material
	ring.position = origin + Vector3(0.0, 0.12, 0.0)
	add_child(ring)
	var tween := create_tween()
	tween.set_parallel(true)
	tween.tween_property(ring, "scale", Vector3.ONE, CUE_SECONDS).from(Vector3(0.05, 1.0, 0.05))
	tween.tween_property(material, "albedo_color:a", 0.0, CUE_SECONDS).from(0.85)
	tween.chain().tween_callback(ring.queue_free)


func _spawn_column(origin: Vector3, radius: float, color: Color) -> void:
	## Rising cylinder over the affected circle: the shared shape of the
	## retail summon/buff FXLists, which emit upward inside the effect radius.
	var column := MeshInstance3D.new()
	var mesh := CylinderMesh.new()
	mesh.top_radius = radius
	mesh.bottom_radius = radius
	mesh.height = 1.0
	column.mesh = mesh
	var material := _cue_material(color)
	column.material_override = material
	column.position = origin + Vector3(0.0, 0.5, 0.0)
	add_child(column)
	var tween := create_tween()
	tween.set_parallel(true)
	tween.tween_property(column, "scale", Vector3(1.0, maxf(2.0, radius), 1.0), CUE_SECONDS).from(Vector3(1.0, 0.05, 1.0))
	tween.tween_property(material, "albedo_color:a", 0.0, CUE_SECONDS).from(0.45)
	tween.chain().tween_callback(column.queue_free)


func _cue_material(color: Color) -> StandardMaterial3D:
	var material := StandardMaterial3D.new()
	material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	material.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	material.cull_mode = BaseMaterial3D.CULL_DISABLED
	material.albedo_color = color
	material.emission_enabled = true
	material.emission = color
	return material
