class_name RetailArcherProjectileController
extends Node3D
## Exact-asset, fail-closed Gondor Archer projectile/impact presenter.
##
## This controller intentionally does not subscribe to attack animation state.
## The converted contract does not prove the weapon-event frame, travel timing,
## W3D streak billboarding, target DamageFX selection, impact attachment policy,
## or audio-pool randomization.  Callers must provide those authoritative
## decisions explicitly; merely entering an "attack" state creates nothing.

const MAX_DOCUMENT_BYTES := 1024 * 1024
const EXPECTED_SCHEMA := "openbfme.archer-projectile-binding"
## Per-projectile compiled art (importer: projectile_art_compiler). Unlike the
## Gondor sidecar above this is keyed by the compiled projectileObjectId, so
## every faction pack answers for the arrows its own archers fire.
const PROJECTILE_ART_SCHEMA := "openbfme.projectile-art-runtime"
const ART_BINDING_COMPILED := "compiled-projectile-art"
const ART_BINDING_GONDOR_SIDECAR := "gondor-arrow-closure"
const EXPECTED_UNIT_OBJECT_ID := "GondorArcher"
const EXPECTED_DAMAGE_FX_SETS: Array[String] = [
	"NormalDamageFX",
	"GwaihirDamageFX",
	"FellBeastDamageFX",
	"MumakilDamageFX",
]
const EXPECTED_UNRESOLVED_SEMANTICS: Array[String] = [
	"Projectile spawn time must come from the authoritative Gondor Archer attack event; the INI closure does not prove the Godot animation event frame.",
	"GOOD_ARROW_PIERCE resolves through the struck object's DamageFX set; the four authored mappings are preserved and must not be treated as one global unconditional hit effect.",
	"The retail source does not specify deterministic random-pool selection seeds for ArcherWeapon or ImpactArrow.",
	"g_arrow is an impact AttachedModel with a W3D animation header; its attachment orientation, lifetime, and playback policy require an original-engine runtime oracle.",
	"FX_GondorArrowDeath is the ground-hit branch and is named here but lies outside the requested target-hit closure.",
]

var contract_declared := false
var contract_ready := false
## Which retail closure the presented art came from: the compiled
## per-projectile art document, or the Gondor sidecar's shared Good arrow.
var art_binding := ""
var art_projectile_object_id := ""
var presentation_assets_ready := false
var parity_ready := false
var error := ""
var validated_streak_texture_count := 0
var validated_impact_model_count := 0
var validated_fire_audio_leaf_count := 0
var validated_impact_audio_leaf_count := 0
var active_projectile_node_count := 0
var active_impact_node_count := 0
var diagnostics: Array[Dictionary] = []

var _pack_root := ""
var _normal_streak_texture: Texture2D
var _snow_streak_texture: Texture2D
var _impact_model_path := ""
var _fire_audio_paths: Array[String] = []
var _impact_audio_paths: Array[String] = []
var _active_projectiles: Dictionary = {}
var _active_impacts: Dictionary = {}
var _presentation_scale := 1.0
## Authored W3DStreakDraw Width/Length. The Gondor sidecar pins 2x15; a
## compiled per-projectile art row carries whatever retail authored for it.
var _streak_size := Vector2(2.0, 15.0)
var _streak_segments := 1
var _streak_additive := false
var _streak_color := Color(1.0, 1.0, 1.0)

static var _validated_asset_cache: Dictionary = {}


func configure_from_pack(pack_root: String, presentation_scale: float = 1.0) -> bool:
	_reset()
	_presentation_scale = presentation_scale if is_finite(presentation_scale) and presentation_scale > 0.0 else 1.0
	var pack_path := ModLoader.resolve_pack_path(pack_root, "pack.json")
	if pack_path == "" or not FileAccess.file_exists(pack_path):
		return _fail("archer projectile runtime requires a contained pack document")
	var pack_value: Variant = ModLoader._read_json(pack_path)
	if typeof(pack_value) != TYPE_DICTIONARY:
		return _fail("archer projectile runtime pack document is invalid")
	var files_value: Variant = (pack_value as Dictionary).get("files", {})
	if typeof(files_value) != TYPE_DICTIONARY:
		return _fail("archer projectile runtime pack files table is invalid")
	var files := files_value as Dictionary
	if not files.has("gondorArcherProjectile"):
		return true
	contract_declared = true
	var relative := String(files.get("gondorArcherProjectile", ""))
	var path := ModLoader.resolve_pack_path(pack_root, relative)
	if not _safe_file(pack_root, path, "json"):
		return _fail("declared Gondor Archer projectile document is missing or escaped")
	var document := _read_bounded_json(path)
	if document.is_empty():
		return _fail("Gondor Archer projectile document is invalid or empty")
	return configure_document(document, pack_root, _presentation_scale)


func configure_from_projectile_art(
	projectile_object_id: String,
	pack_root: String,
	entry: Dictionary,
	presentation_scale: float = 1.0
) -> bool:
	## Bind the streak art the importer compiled for THIS projectile object id.
	##
	## This is art only: the Gondor sidecar's impact model and 100 exact audio
	## leaves are a separate, Gondor-specific closure (GOOD_ARROW_PIERCE ->
	## FX_GoodArrowHit -> g_arrow + ImpactArrow). A caller that presents from
	## this binding gets the right arrow and no invented impact, and the
	## diagnostic below says so instead of implying parity.
	_reset()
	_presentation_scale = presentation_scale if is_finite(presentation_scale) and presentation_scale > 0.0 else 1.0
	contract_declared = true
	if String(entry.get("projectileObjectId", "")) != projectile_object_id or projectile_object_id == "":
		return _fail("compiled projectile art row does not name this projectile")
	var draws_value: Variant = entry.get("draws", [])
	if typeof(draws_value) != TYPE_ARRAY:
		return _fail("compiled projectile art row has no draw list")
	var streak: Dictionary = {}
	var extra_streak_tags: Array[String] = []
	for draw_value in draws_value as Array:
		if typeof(draw_value) != TYPE_DICTIONARY:
			return _fail("compiled projectile art draw row is invalid")
		var draw := draw_value as Dictionary
		if String(draw.get("kind", "")) != "W3DStreakDraw":
			continue
		if not streak.is_empty():
			# Retail layers streaks on some projectiles (the Mirkwood
			# silverthorn authors two). This presenter draws the first and
			# names the rest rather than compositing an unproven stack.
			extra_streak_tags.append(String(draw.get("instanceTag", "")))
			continue
		streak = draw
	if streak.is_empty():
		# A rock/axe projectile authors a model instead of a streak; this
		# controller presents streaks only. Not an error, and not a claim.
		return _fail("compiled projectile art authors no W3DStreakDraw")
	_pack_root = pack_root
	art_projectile_object_id = projectile_object_id
	var width := float(streak.get("width", 2))
	var length := float(streak.get("length", 15))
	if not (is_finite(width) and is_finite(length)) or width <= 0.0 or length <= 0.0:
		return _fail("compiled projectile art streak geometry is invalid")
	_streak_size = Vector2(width, length)
	_streak_segments = int(streak.get("numSegments", 1))
	_streak_additive = bool(streak.get("additive", false))
	_streak_color = _art_color(streak.get("color", {}))
	_normal_streak_texture = _load_texture(String(streak.get("texture", "")))
	if _normal_streak_texture == null:
		return _fail("compiled projectile art streak texture is missing, escaped, or invalid")
	validated_streak_texture_count = 1
	var weather_value: Variant = streak.get("weatherTextures", {})
	if typeof(weather_value) == TYPE_DICTIONARY:
		var snow_relative := String((weather_value as Dictionary).get("SNOWY", ""))
		if snow_relative != "":
			_snow_streak_texture = _load_texture(snow_relative)
			if _snow_streak_texture == null:
				return _fail("compiled projectile art snow texture is missing, escaped, or invalid")
			validated_streak_texture_count = 2
	art_binding = ART_BINDING_COMPILED
	contract_ready = true
	presentation_assets_ready = true
	parity_ready = false
	diagnostics.append({
		"code": "projectile-art-impact-closure-absent",
		"projectileObjectId": projectile_object_id,
		"packRoot": pack_root,
		"impact": "compiled projectile art carries streak art only; the target-impact model and exact audio leaves stay unbound",
	})
	if not extra_streak_tags.is_empty():
		diagnostics.append({
			"code": "projectile-art-layered-streaks-unpresented",
			"projectileObjectId": projectile_object_id,
			"instanceTags": extra_streak_tags.duplicate(),
			"impact": "only the first authored W3DStreakDraw is presented",
		})
	_set_runtime_metadata()
	return true


func configure_document(document: Dictionary, pack_root: String, presentation_scale: float = 1.0) -> bool:
	_reset()
	_presentation_scale = presentation_scale if is_finite(presentation_scale) and presentation_scale > 0.0 else 1.0
	contract_declared = true
	if not _validate_contract_shape(document):
		return false
	_pack_root = pack_root
	if not _load_assets(document):
		return false
	art_binding = ART_BINDING_GONDOR_SIDECAR
	art_projectile_object_id = "GondorArcherArrow"
	contract_ready = true
	presentation_assets_ready = true
	# All five unresolved source-engine decisions remain required from an
	# authoritative caller, so asset readiness is not a 1:1 parity claim.
	parity_ready = false
	diagnostics.append({
		"code": "archer-projectile-engine-semantics-unresolved",
		"count": EXPECTED_UNRESOLVED_SEMANTICS.size(),
		"semantics": EXPECTED_UNRESOLVED_SEMANTICS.duplicate(),
	})
	_set_runtime_metadata()
	return true


func validate_contract_shape(document: Dictionary) -> bool:
	## Public deterministic preflight used by tooling/tests without asset I/O.
	_reset()
	contract_declared = true
	var valid := _validate_contract_shape(document)
	_set_runtime_metadata()
	return valid


func present_authoritative_projectile(
	attack_event_token: int,
	authoritative_pose: Transform3D,
	use_snow_texture: bool,
	authoritative_fire_audio_leaf_index: int,
	play_embedded_audio: bool = true
) -> Node3D:
	## Create one source-textured W3DStreakDraw presentation snapshot.
	## The caller owns movement/timing and must call remove_projectile().
	if not contract_ready or attack_event_token < 0 or _active_projectiles.has(attack_event_token):
		return null
	if not _finite_transform(authoritative_pose):
		return null
	if play_embedded_audio and (authoritative_fire_audio_leaf_index < 0 or authoritative_fire_audio_leaf_index >= _fire_audio_paths.size()):
		return null
	var fire_stream: AudioStream = null
	if play_embedded_audio:
		fire_stream = _load_wav(_fire_audio_paths[authoritative_fire_audio_leaf_index])
	if play_embedded_audio and fire_stream == null:
		return null
	var texture := _snow_streak_texture if use_snow_texture else _normal_streak_texture
	if texture == null:
		return null
	var root := Node3D.new()
	root.name = "RetailGondorArcherProjectile_%d" % attack_event_token
	root.transform = authoritative_pose
	root.set_meta("source_draw_kind", "W3DStreakDraw")
	root.set_meta("source_draw_id", "EXArrowStreak01")
	root.set_meta("source_length", int(_streak_size.y))
	root.set_meta("source_width", int(_streak_size.x))
	root.set_meta("source_num_segments", _streak_segments)
	root.set_meta("art_binding", art_binding)
	root.set_meta("art_projectile_object_id", art_projectile_object_id)
	root.set_meta("parity_ready", false)
	root.set_meta("authoritative_attack_event_token", attack_event_token)

	var material := StandardMaterial3D.new()
	material.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	material.cull_mode = BaseMaterial3D.CULL_DISABLED
	material.albedo_texture = texture
	# Authored W3DStreakDraw Color/Additive. The Gondor sidecar authors white,
	# non-additive, so this is a no-op on that path.
	material.albedo_color = _streak_color
	if _streak_additive:
		material.blend_mode = BaseMaterial3D.BLEND_MODE_ADD
	var quad := QuadMesh.new()
	quad.orientation = PlaneMesh.FACE_Z
	quad.size = _streak_size * _presentation_scale
	quad.material = material
	var streak := MeshInstance3D.new()
	streak.name = "ExactEXArrowStreak01Texture"
	streak.mesh = quad
	# QuadMesh lies in local XY while the authoritative projectile root looks
	# down -Z. Rotate its authored length onto +Z (behind the projectile) and
	# offset the center so the streak begins at the event pose rather than
	# straddling it vertically.
	streak.rotation.x = PI * 0.5
	streak.position.z = quad.size.y * 0.5
	root.add_child(streak)
	if fire_stream != null:
		_add_audio_player(root, fire_stream, "ExactArcherWeaponLeaf")
	add_child(root)
	_active_projectiles[attack_event_token] = root
	active_projectile_node_count = _active_projectiles.size()
	_set_runtime_metadata()
	return root


func update_projectile_pose(attack_event_token: int, authoritative_pose: Transform3D) -> bool:
	if not _active_projectiles.has(attack_event_token) or not _finite_transform(authoritative_pose):
		return false
	var node := _active_projectiles.get(attack_event_token) as Node3D
	if node == null or not is_instance_valid(node):
		_active_projectiles.erase(attack_event_token)
		active_projectile_node_count = _active_projectiles.size()
		return false
	node.transform = authoritative_pose
	return true


func remove_projectile(attack_event_token: int) -> bool:
	if not _active_projectiles.has(attack_event_token):
		return false
	var node := _active_projectiles.get(attack_event_token) as Node
	_active_projectiles.erase(attack_event_token)
	if node != null and is_instance_valid(node):
		node.queue_free()
	active_projectile_node_count = _active_projectiles.size()
	_set_runtime_metadata()
	return true


func present_authoritative_target_impact(
	impact_event_token: int,
	target_parent: Node3D,
	authoritative_attachment_transform: Transform3D,
	authoritative_damage_fx_set_id: String,
	authoritative_impact_audio_leaf_index: int,
	play_embedded_audio: bool = true
) -> Node3D:
	## Attach exact g_arrow and one explicitly selected ImpactArrow leaf.
	## The caller owns orientation, lifetime, animation policy, and removal.
	if (
		not contract_ready
		or impact_event_token < 0
		or target_parent == null
		or _active_impacts.has(impact_event_token)
		or not EXPECTED_DAMAGE_FX_SETS.has(authoritative_damage_fx_set_id)
		or not _finite_transform(authoritative_attachment_transform)
		or (play_embedded_audio and authoritative_impact_audio_leaf_index < 0)
		or (play_embedded_audio and authoritative_impact_audio_leaf_index >= _impact_audio_paths.size())
	):
		return null
	var impact_stream: AudioStream = null
	if play_embedded_audio:
		impact_stream = _load_wav(_impact_audio_paths[authoritative_impact_audio_leaf_index])
	if play_embedded_audio and impact_stream == null:
		return null
	var model := _instantiate_glb(_impact_model_path)
	if model == null:
		return null
	var root := Node3D.new()
	root.name = "RetailGoodArrowHit_%d" % impact_event_token
	root.transform = authoritative_attachment_transform
	root.set_meta("source_fx_list_id", "FX_GoodArrowHit")
	root.set_meta("source_attached_model_id", "g_arrow")
	root.set_meta("source_sound_event_id", "ImpactArrow")
	root.set_meta("authoritative_damage_fx_set_id", authoritative_damage_fx_set_id)
	root.set_meta("authoritative_impact_event_token", impact_event_token)
	root.set_meta("parity_ready", false)
	model.name = "ExactGArrowAttachedModel"
	model.scale = Vector3.ONE * _presentation_scale
	root.add_child(model)
	if impact_stream != null:
		_add_audio_player(root, impact_stream, "ExactImpactArrowLeaf")
	target_parent.add_child(root)
	_active_impacts[impact_event_token] = root
	active_impact_node_count = _active_impacts.size()
	_set_runtime_metadata()
	return root


func remove_target_impact(impact_event_token: int) -> bool:
	if not _active_impacts.has(impact_event_token):
		return false
	var node: Variant = _active_impacts.get(impact_event_token)
	_active_impacts.erase(impact_event_token)
	if is_instance_valid(node):
		(node as Node).queue_free()
	active_impact_node_count = _active_impacts.size()
	_set_runtime_metadata()
	return true


func _validate_contract_shape(document: Dictionary) -> bool:
	if (
		String(document.get("schema", "")) != EXPECTED_SCHEMA
		or int(document.get("schemaVersion", -1)) != 0
		or String(document.get("unitObjectId", "")) != EXPECTED_UNIT_OBJECT_ID
	):
		return _fail("unexpected Gondor Archer projectile schema or unit identity")
	if _string_array(document.get("unresolvedEngineSemantics", [])) != EXPECTED_UNRESOLVED_SEMANTICS:
		return _fail("Gondor Archer projectile unresolved-engine boundary changed")
	if not _validate_projectile(document.get("projectilePresentation", {})):
		return false
	if not _validate_weapon(document.get("weapon", {})):
		return false
	if not _validate_damage(document.get("damage", {})):
		return false
	if not _validate_impact(document.get("impactPresentation", {})):
		return false
	if not _validate_audio_events(document.get("audioEvents", [])):
		return false
	return true


func _validate_projectile(value: Variant) -> bool:
	if typeof(value) != TYPE_DICTIONARY:
		return _fail("Gondor Archer projectile presentation is missing")
	var row := value as Dictionary
	var color_value: Variant = row.get("color", {})
	var trajectory_value: Variant = row.get("trajectory", {})
	if typeof(color_value) != TYPE_DICTIONARY or typeof(trajectory_value) != TYPE_DICTIONARY:
		return _fail("Gondor Archer projectile color or trajectory is invalid")
	var color := color_value as Dictionary
	var trajectory := trajectory_value as Dictionary
	if (
		String(row.get("kind", "")) != "W3DStreakDraw"
		or int(row.get("length", -1)) != 15
		or int(row.get("width", -1)) != 2
		or int(row.get("numSegments", -1)) != 1
		or bool(row.get("additive", true))
		or row.get("model", "sentinel") != null
		or int(color.get("r", -1)) != 255
		or int(color.get("g", -1)) != 255
		or int(color.get("b", -1)) != 255
		or String(row.get("texture", "")) != "assets/textures/combat/archer/exarrowstreak01.png"
		or String(row.get("snowTexture", "")) != "assets/textures/combat/archer/exarrowstreak_snow.png"
		or String(trajectory.get("kind", "")) != "BezierProjectileBehavior"
		or int(trajectory.get("firstHeight", -1)) != 9
		or int(trajectory.get("secondHeight", -1)) != 9
		or int(trajectory.get("firstPercentIndent", -1)) != 20
		or int(trajectory.get("secondPercentIndent", -1)) != 90
		or not is_equal_approx(float(trajectory.get("curveFlattenMinDist", -1.0)), 100.0)
		or int(trajectory.get("flightPathAdjustDistPerSecond", -1)) != 50
		or String(trajectory.get("groundHitFxListId", "")) != "FX_GondorArrowDeath"
	):
		return _fail("Gondor Archer W3DStreakDraw definition changed")
	var reskin_value: Variant = row.get("reskinSource", {})
	if typeof(reskin_value) != TYPE_DICTIONARY:
		return _fail("Gondor Archer projectile reskin evidence is missing")
	var reskin := reskin_value as Dictionary
	if String(reskin.get("parent", "")) != "GoodFactionArrow" or not _validate_source(
		reskin,
		"ObjectReskin",
		"GondorArcherArrow",
		"data/ini/object/goodfaction/goodfactionsubobjects.ini"
	):
		return _fail("Gondor Archer projectile reskin evidence changed")
	return _validate_source(row.get("source", {}), "Object", "GoodFactionArrow", "data/ini/object/goodfaction/goodfactionsubobjects.ini")


func _validate_weapon(value: Variant) -> bool:
	if typeof(value) != TYPE_DICTIONARY:
		return _fail("Gondor Archer weapon binding is missing")
	var row := value as Dictionary
	var speed_value: Variant = row.get("speed", {})
	if typeof(speed_value) != TYPE_DICTIONARY:
		return _fail("Gondor Archer projectile speed evidence is missing")
	var speed := speed_value as Dictionary
	if (
		String(row.get("weaponTemplateId", "")) != "GondorArcherBow"
		or String(row.get("warheadTemplateId", "")) != "GondorArcherBowWarhead"
		or String(row.get("projectileTemplateId", "")) != "GondorArcherArrow"
		or String(row.get("inheritedProjectileObjectId", "")) != "GoodFactionArrow"
		or String(row.get("fireFxListId", "")) != "FX_RohanArcherBowWeapon"
		or int(row.get("hitPercentage", -1)) != 100
		or not is_equal_approx(float(row.get("scatterRadius", -1.0)), 16.0)
		or int(speed.get("minimum", -1)) != 241
		or int(speed.get("nominal", -1)) != 321
		or int(speed.get("maximum", -1)) != 481
		or not bool(speed.get("scaleWithRange", false))
	):
		return _fail("Gondor Archer weapon/projectile binding changed")
	return _validate_source(row.get("source", {}), "Weapon", "GondorArcherBow", "data/ini/weapon.ini")


func _validate_damage(value: Variant) -> bool:
	if typeof(value) != TYPE_DICTIONARY:
		return _fail("Gondor Archer damage binding is missing")
	var row := value as Dictionary
	if String(row.get("damageType", "")) != "PIERCE" or String(row.get("damageFxType", "")) != "GOOD_ARROW_PIERCE":
		return _fail("Gondor Archer damage type changed")
	var mappings_value: Variant = row.get("majorFxMappings", [])
	if typeof(mappings_value) != TYPE_ARRAY or (mappings_value as Array).size() != EXPECTED_DAMAGE_FX_SETS.size():
		return _fail("Gondor Archer target DamageFX closure changed")
	var seen: Array[String] = []
	for mapping_value in mappings_value as Array:
		if typeof(mapping_value) != TYPE_DICTIONARY:
			return _fail("Gondor Archer DamageFX mapping is invalid")
		var mapping := mapping_value as Dictionary
		var set_id := String(mapping.get("damageFxSetId", ""))
		if (
			not EXPECTED_DAMAGE_FX_SETS.has(set_id)
			or seen.has(set_id)
			or String(mapping.get("damageFxType", "")) != "GOOD_ARROW_PIERCE"
			or String(mapping.get("majorFxListId", "")) != "FX_GoodArrowHit"
		):
			return _fail("Gondor Archer DamageFX mapping changed or duplicated")
		if not _validate_source(mapping.get("source", {}), "DamageFX", set_id, "data/ini/damagefx.ini"):
			return false
		seen.append(set_id)
	seen.sort()
	var expected := EXPECTED_DAMAGE_FX_SETS.duplicate()
	expected.sort()
	if seen != expected:
		return _fail("Gondor Archer DamageFX mapping set is incomplete")
	return _validate_source(row.get("source", {}), "Weapon", "GondorArcherBowWarhead", "data/ini/weapon.ini")


func _validate_impact(value: Variant) -> bool:
	if typeof(value) != TYPE_DICTIONARY:
		return _fail("Gondor Archer impact presentation is missing")
	var row := value as Dictionary
	if (
		String(row.get("fxListId", "")) != "FX_GoodArrowHit"
		or String(row.get("attachedModelId", "")) != "g_arrow"
		or String(row.get("glb", "")) != "assets/models/combat/g-arrow.glb"
		or String(row.get("soundEventId", "")) != "ImpactArrow"
	):
		return _fail("Gondor Archer target impact binding changed")
	return _validate_source(row.get("source", {}), "FXList", "FX_GoodArrowHit", "data/ini/fxlist.ini")


func _validate_audio_events(value: Variant) -> bool:
	if typeof(value) != TYPE_ARRAY or (value as Array).size() != 2:
		return _fail("Gondor Archer projectile audio-event closure changed")
	var seen: Dictionary = {}
	for event_value in value as Array:
		if typeof(event_value) != TYPE_DICTIONARY:
			return _fail("Gondor Archer projectile audio event is invalid")
		var event := event_value as Dictionary
		var event_id := String(event.get("eventId", ""))
		if seen.has(event_id) or not ["ArcherWeapon", "ImpactArrow"].has(event_id):
			return _fail("Gondor Archer projectile audio event changed or duplicated")
		var expected_tokens := _expected_fire_tokens() if event_id == "ArcherWeapon" else _expected_impact_tokens()
		var expected_paths := _audio_paths(expected_tokens)
		if _string_array(event.get("soundTokens", [])) != expected_tokens or _string_array(event.get("sourceVirtualPaths", [])) != expected_paths:
			return _fail("%s exact audio leaf closure changed" % event_id)
		var settings_value: Variant = event.get("settings", {})
		if typeof(settings_value) != TYPE_DICTIONARY:
			return _fail("%s settings are missing" % event_id)
		var settings := settings_value as Dictionary
		if not _validate_audio_settings(event_id, settings):
			return false
		if not _validate_source(event.get("source", {}), "AudioEvent", event_id, "data/ini/soundeffects.ini"):
			return false
		seen[event_id] = true
	return seen.size() == 2


func _validate_audio_settings(event_id: String, settings: Dictionary) -> bool:
	var expected := {
		"Control": "interrupt",
		"Limit": "2" if event_id == "ArcherWeapon" else "3",
		"PitchShift": "-2 2" if event_id == "ArcherWeapon" else "-5 5",
		"Priority": "normal" if event_id == "ArcherWeapon" else "low",
		"SubmixSlider": "SoundFX",
		"Type": "world shrouded everyone",
		"Volume": "60" if event_id == "ArcherWeapon" else "45",
		"VolumeShift": "-10" if event_id == "ArcherWeapon" else "-20",
	}
	if settings != expected:
		return _fail("%s exact audio settings changed" % event_id)
	return true


func _validate_source(value: Variant, kind: String, source_name: String, path: String) -> bool:
	if typeof(value) != TYPE_DICTIONARY:
		return _fail("source provenance is missing for %s" % source_name)
	var source := value as Dictionary
	if (
		String(source.get("kind", "")) != kind
		or String(source.get("name", "")) != source_name
		or String(source.get("sourceVirtualPath", "")) != path
		or not _is_sha256(String(source.get("sha256", "")))
		or int(source.get("byteLength", 0)) <= 0
		or int(source.get("startLine", 0)) <= 0
		or int(source.get("endLine", 0)) < int(source.get("startLine", 0))
	):
		return _fail("source provenance changed for %s" % source_name)
	return true


func _load_assets(document: Dictionary) -> bool:
	var cache_key := _pack_root.to_lower()
	var cached_value: Variant = _validated_asset_cache.get(cache_key)
	if typeof(cached_value) == TYPE_DICTIONARY:
		var cached := cached_value as Dictionary
		_normal_streak_texture = cached.get("normal_texture") as Texture2D
		_snow_streak_texture = cached.get("snow_texture") as Texture2D
		_impact_model_path = String(cached.get("impact_model_path", ""))
		_fire_audio_paths = _string_array(cached.get("fire_audio_paths", []))
		_impact_audio_paths = _string_array(cached.get("impact_audio_paths", []))
		validated_streak_texture_count = 2 if _normal_streak_texture != null and _snow_streak_texture != null else 0
		validated_impact_model_count = int(_impact_model_path != "")
		validated_fire_audio_leaf_count = _fire_audio_paths.size()
		validated_impact_audio_leaf_count = _impact_audio_paths.size()
		if validated_streak_texture_count == 2 and validated_impact_model_count == 1 and validated_fire_audio_leaf_count == 32 and validated_impact_audio_leaf_count == 68:
			return true
		_validated_asset_cache.erase(cache_key)
	var projectile := document.get("projectilePresentation", {}) as Dictionary
	var impact := document.get("impactPresentation", {}) as Dictionary
	_normal_streak_texture = _load_texture(String(projectile.get("texture", "")))
	_snow_streak_texture = _load_texture(String(projectile.get("snowTexture", "")))
	if _normal_streak_texture == null or _snow_streak_texture == null:
		return _fail("Gondor Archer exact streak textures are missing, escaped, or invalid")
	validated_streak_texture_count = 2
	_impact_model_path = ModLoader.resolve_pack_path(_pack_root, String(impact.get("glb", "")))
	if not _safe_glb(_pack_root, _impact_model_path):
		return _fail("Gondor Archer exact g_arrow impact GLB is missing, escaped, or invalid")
	var impact_probe := _instantiate_glb(_impact_model_path)
	if impact_probe == null:
		return _fail("Gondor Archer exact g_arrow impact GLB cannot be instantiated")
	impact_probe.free()
	validated_impact_model_count = 1
	for event_value in document.get("audioEvents", []) as Array:
		var event := event_value as Dictionary
		var paths: Array[String] = []
		for source_path in _string_array(event.get("sourceVirtualPaths", [])):
			var relative := "assets/audio/combat/archer/%s" % source_path.get_file().to_lower()
			if not _validate_wav(relative):
				return _fail("Gondor Archer exact audio leaf is missing, escaped, or invalid: %s" % relative)
			paths.append(relative)
		if String(event.get("eventId", "")) == "ArcherWeapon":
			_fire_audio_paths = paths
		else:
			_impact_audio_paths = paths
	validated_fire_audio_leaf_count = _fire_audio_paths.size()
	validated_impact_audio_leaf_count = _impact_audio_paths.size()
	var valid := validated_fire_audio_leaf_count == 32 and validated_impact_audio_leaf_count == 68
	if valid:
		_validated_asset_cache[cache_key] = {
			"normal_texture": _normal_streak_texture,
			"snow_texture": _snow_streak_texture,
			"impact_model_path": _impact_model_path,
			"fire_audio_paths": _fire_audio_paths.duplicate(),
			"impact_audio_paths": _impact_audio_paths.duplicate(),
		}
	return valid


func _art_color(value: Variant) -> Color:
	if typeof(value) != TYPE_DICTIONARY:
		return Color(1.0, 1.0, 1.0)
	var row := value as Dictionary
	return Color(
		clampf(float(row.get("r", 255)) / 255.0, 0.0, 1.0),
		clampf(float(row.get("g", 255)) / 255.0, 0.0, 1.0),
		clampf(float(row.get("b", 255)) / 255.0, 0.0, 1.0),
		clampf(float(row.get("a", 255)) / 255.0, 0.0, 1.0)
	)


func _load_texture(relative: String) -> Texture2D:
	var path := ModLoader.resolve_pack_path(_pack_root, relative)
	if not _safe_file(_pack_root, path, "png"):
		return null
	var image := Image.new()
	if image.load(path) != OK or image.is_empty() or image.get_width() > 4096 or image.get_height() > 4096:
		return null
	return ImageTexture.create_from_image(image)


func _load_wav(relative: String) -> AudioStream:
	var path := ModLoader.resolve_pack_path(_pack_root, relative)
	if not _validate_wav(relative):
		return null
	return AudioStreamWAV.load_from_file(path)


func _validate_wav(relative: String) -> bool:
	var path := ModLoader.resolve_pack_path(_pack_root, relative)
	if not _safe_file(_pack_root, path, "wav"):
		return false
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null:
		return false
	var header := file.get_buffer(12)
	file.close()
	return header.size() == 12 and header.slice(0, 4).get_string_from_ascii() == "RIFF" and header.slice(8, 12).get_string_from_ascii() == "WAVE"


func _safe_glb(root_path: String, path: String) -> bool:
	if not _safe_file(root_path, path, "glb"):
		return false
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null or file.get_length() < 12:
		return false
	var length := file.get_length()
	var header := file.get_buffer(12)
	file.close()
	return (
		header.size() == 12
		and header.slice(0, 4).get_string_from_ascii() == "glTF"
		and header.decode_u32(4) == 2
		and int(header.decode_u32(8)) == length
	)


func _instantiate_glb(path: String) -> Node3D:
	if path == "":
		return null
	var document := GLTFDocument.new()
	var state := GLTFState.new()
	if document.append_from_file(path, state) != OK:
		return null
	return document.generate_scene(state) as Node3D


func _add_audio_player(parent: Node3D, stream: AudioStream, node_name: String) -> void:
	var player := AudioStreamPlayer3D.new()
	player.name = node_name
	player.stream = stream
	parent.add_child(player)
	# The presentation root may be assembled immediately before it enters the
	# scene tree. Deferred playback starts only after the exact leaf is mounted.
	player.call_deferred("play")


func _expected_fire_tokens() -> Array[String]:
	var result: Array[String] = []
	for code in range("a".unicode_at(0), "z".unicode_at(0) + 1):
		result.append("GUArche_weapo1" + String.chr(code))
	for code in range("a".unicode_at(0), "f".unicode_at(0) + 1):
		result.append("GUArche_weapo2" + String.chr(code))
	return result


func _expected_impact_tokens() -> Array[String]:
	var result: Array[String] = []
	for group in [["flesh1", "v"], ["wood1", "x"], ["dirt1", "p"], ["gener1", "f"]]:
		for code in range("a".unicode_at(0), String(group[1]).unicode_at(0) + 1):
			result.append("WIArrow_%s%s" % [String(group[0]), String.chr(code)])
	return result


func _audio_paths(tokens: Array[String]) -> Array[String]:
	var result: Array[String] = []
	for token in tokens:
		result.append("data/audio/sounds/%s.wav" % token.to_lower())
	return result


func _read_bounded_json(path: String) -> Dictionary:
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null or file.get_length() <= 0 or file.get_length() > MAX_DOCUMENT_BYTES:
		return {}
	var payload := file.get_buffer(file.get_length())
	file.close()
	var parsed: Variant = JSON.parse_string(payload.get_string_from_utf8())
	return parsed as Dictionary if typeof(parsed) == TYPE_DICTIONARY else {}


func _safe_file(root_path: String, path: String, extension: String) -> bool:
	return (
		path != ""
		and path.get_extension().to_lower() == extension
		and FileAccess.file_exists(path)
		and ModLoader.path_is_within(root_path, path)
	)


func _string_array(value: Variant) -> Array[String]:
	var result: Array[String] = []
	if typeof(value) != TYPE_ARRAY:
		return result
	for item in value as Array:
		if typeof(item) != TYPE_STRING:
			return []
		result.append(String(item))
	return result


func _is_sha256(value: String) -> bool:
	if value.length() != 64:
		return false
	for index in range(value.length()):
		var code := value.unicode_at(index)
		if not (code >= 48 and code <= 57) and not (code >= 97 and code <= 102):
			return false
	return true


func _finite_transform(value: Transform3D) -> bool:
	for component in [
		value.origin.x, value.origin.y, value.origin.z,
		value.basis.x.x, value.basis.x.y, value.basis.x.z,
		value.basis.y.x, value.basis.y.y, value.basis.y.z,
		value.basis.z.x, value.basis.z.y, value.basis.z.z,
	]:
		if not is_finite(float(component)):
			return false
	return true


func _set_runtime_metadata() -> void:
	set_meta("contract_declared", contract_declared)
	set_meta("art_binding", art_binding)
	set_meta("art_projectile_object_id", art_projectile_object_id)
	set_meta("contract_ready", contract_ready)
	set_meta("presentation_assets_ready", presentation_assets_ready)
	set_meta("parity_ready", parity_ready)
	set_meta("validated_streak_texture_count", validated_streak_texture_count)
	set_meta("validated_impact_model_count", validated_impact_model_count)
	set_meta("validated_fire_audio_leaf_count", validated_fire_audio_leaf_count)
	set_meta("validated_impact_audio_leaf_count", validated_impact_audio_leaf_count)
	set_meta("active_projectile_node_count", active_projectile_node_count)
	set_meta("active_impact_node_count", active_impact_node_count)
	set_meta("diagnostics", diagnostics.duplicate(true))


func _reset() -> void:
	for node in _active_projectiles.values():
		if node is Node and is_instance_valid(node):
			(node as Node).queue_free()
	for node in _active_impacts.values():
		if node is Node and is_instance_valid(node):
			(node as Node).queue_free()
	contract_declared = false
	contract_ready = false
	presentation_assets_ready = false
	parity_ready = false
	art_binding = ""
	art_projectile_object_id = ""
	_streak_size = Vector2(2.0, 15.0)
	_streak_segments = 1
	_streak_additive = false
	_streak_color = Color(1.0, 1.0, 1.0)
	error = ""
	validated_streak_texture_count = 0
	validated_impact_model_count = 0
	validated_fire_audio_leaf_count = 0
	validated_impact_audio_leaf_count = 0
	active_projectile_node_count = 0
	active_impact_node_count = 0
	diagnostics.clear()
	_pack_root = ""
	_normal_streak_texture = null
	_snow_streak_texture = null
	_impact_model_path = ""
	_fire_audio_paths.clear()
	_impact_audio_paths.clear()
	_active_projectiles.clear()
	_active_impacts.clear()
	_presentation_scale = 1.0


func _fail(message: String) -> bool:
	error = message
	contract_ready = false
	presentation_assets_ready = false
	parity_ready = false
	_set_runtime_metadata()
	return false
