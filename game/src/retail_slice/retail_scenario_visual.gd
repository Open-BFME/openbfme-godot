class_name RetailScenarioVisual
extends Node3D
## Presentation-only binder for descriptor-backed scenario units and passive props.
## It consumes the selected neutral document directly; no bundle-object,
## faction, producer, HUD, or simulation registry is synthesized.

const AssetFactoryScript = preload("res://src/view/asset_factory.gd")

var domain := ""
var object_id := ""
var pack_root := ""
var source_index := -1
var source_yaw := 0.0
var source_scale := 1.0
var converted_paths: Array[String] = []
## SHROUD OWNERSHIP (owner round 12, proven by hud_diagnostic_runner: a creep
## wolf reported shroud_says_visible=false while its node was visible=true).
## `sync_state` decides visibility from the animation/presentation state and
## used to overwrite the caller's shroud gate, so scenario units - creeps,
## lairs, props - rendered through unexplored fog while battalions did not.
## The caller sets this flag before syncing; every visibility write below is
## ANDed with it, so state logic and fog cannot fight each other.
var shroud_visible := true
var presentation_binding := ""
var semantic_state := "idle"
var active_animation_identifier := ""
var active_animation_name := ""
var mesh_instance_count := 0
var contract_error := ""
## Exact selected-descriptor receipt for a presentation-only removal hook.
## Empty means the prop did not author FXListDie; it never means "use a
## default explosion".
var removal_presentation: Dictionary = {}

var _visual_recipe: Dictionary = {}
var _component_nodes: Array[Dictionary] = []
var _animation_players: Array[AnimationPlayer] = []
var _removal_presentation_consumed := false


func configure(document: Dictionary, row: Dictionary, source_scale: float, requested_domain: String) -> bool:
	domain = requested_domain
	object_id = String(document.get("objectId", ""))
	pack_root = String(document.get("_pack_root", ""))
	if domain not in ["unit", "prop"] or object_id == "" or pack_root == "":
		return _fail("scenario visual identity is incomplete")
	_visual_recipe = _document_visual(document)
	if _visual_recipe.is_empty():
		return _fail("scenario visual recipe is absent")
	if domain == "prop":
		removal_presentation = _prop_removal_presentation(document)
		if contract_error != "":
			return false
	name = "Scenario%s_%s" % [domain.capitalize(), object_id]
	var paths := _stage_visual_components(_visual_recipe)
	if paths.is_empty() or mesh_instance_count <= 0:
		return _fail("scenario visual has no converted mesh")
	converted_paths.assign(paths)
	self.source_scale = source_scale if is_finite(source_scale) and source_scale > 0.0 else 1.0
	scale = Vector3.ONE * self.source_scale
	set_meta("presentation", "selected-neutral-scenario-%s" % domain)
	set_meta("source_object_id", object_id)
	set_meta("pack_root", pack_root)
	set_meta("converted_paths", converted_paths.duplicate())
	set_meta("mesh_instance_count", mesh_instance_count)
	set_meta("removal_presentation", removal_presentation.duplicate(true))
	sync_state(row)
	return contract_error == ""


func consume_authoritative_removal_presentation() -> Dictionary:
	## Called only after the prop is absent from the authoritative scenario-prop
	## table. It does not decide death, damage, or timing and is exactly-once so
	## repeated presentation syncs cannot replay an authored FXList.
	if domain != "prop" or removal_presentation.is_empty() or _removal_presentation_consumed:
		return {}
	_removal_presentation_consumed = true
	var request := removal_presentation.duplicate(true)
	request["position"] = global_position
	request["sourceIndex"] = source_index
	request["sourceYaw"] = source_yaw
	request["sourceScale"] = source_scale
	request["packRoot"] = pack_root
	request["trigger"] = "authoritative-scenario-prop-removal"
	set_meta("removal_presentation_consumed", true)
	return request


func sync_state(row: Dictionary) -> void:
	var at := Vector2(row.get("position", Vector2.ZERO))
	position = Vector3(at.x, float(row.get("presentation_height", 0.0)), at.y)
	source_index = int(row.get("scenario_source_index", -1))
	source_yaw = float(row.get("yaw", source_yaw))
	if row.has("yaw"):
		rotation.y = source_yaw
	semantic_state = _semantic_state(row)
	_apply_component_visibility(semantic_state)
	_apply_semantic_presentation(semantic_state)
	set_meta("source_index", source_index)
	set_meta("source_position", row.get("scenario_source_position", Vector3.INF))
	set_meta("source_yaw", source_yaw)
	set_meta("semantic_state", semantic_state)
	set_meta("presentation_binding", presentation_binding)
	set_meta("active_animation_identifier", active_animation_identifier)
	set_meta("active_animation_name", active_animation_name)


func _document_visual(document: Dictionary) -> Dictionary:
	if domain == "prop":
		return document.get("presentation", {}) as Dictionary
	return (document.get("registration", {}) as Dictionary).get("visual", {}) as Dictionary


func _prop_removal_presentation(document: Dictionary) -> Dictionary:
	var modules_value: Variant = document.get("moduleContracts", [])
	if typeof(modules_value) != TYPE_ARRAY:
		_fail("scenario prop moduleContracts is not an array")
		return {}
	var found: Dictionary = {}
	for module_value in modules_value as Array:
		if typeof(module_value) != TYPE_DICTIONARY:
			_fail("scenario prop module contract is not a document")
			return {}
		var module := module_value as Dictionary
		if String(module.get("module", "")) != "FXListDie":
			continue
		if not found.is_empty():
			_fail("scenario prop declares duplicate FXListDie contracts")
			return {}
		var fields_value: Variant = module.get("fields", {})
		if typeof(fields_value) != TYPE_DICTIONARY:
			_fail("FXListDie fields are invalid")
			return {}
		var death_value: Variant = (fields_value as Dictionary).get("DeathFX")
		if typeof(death_value) != TYPE_DICTIONARY:
			_fail("FXListDie DeathFX receipt is absent")
			return {}
		var death := death_value as Dictionary
		var death_fx := String(death.get("authored", "")).strip_edges()
		var source_ini := String(death.get("sourceIni", "")).replace("\\", "/")
		var field_line := int(death.get("line", 0))
		if (
			String(module.get("carrier", "")) != "Behavior"
			or String(module.get("extraction", "")) not in ["typed", "opaque-authored"]
			or String(module.get("runtimeStatus", "")) not in ["implemented", "deferred"]
			or death_fx == ""
			or not death_fx.is_valid_identifier()
			or source_ini == ""
			or source_ini.begins_with("/")
			or source_ini.contains("..")
			or field_line <= 0
		):
			_fail("FXListDie authored receipt is unsafe")
			return {}
		found = {
			"schema": "openbfme.scenario-prop-removal-presentation-route",
			"schemaVersion": 0,
			"objectId": object_id,
			"module": "FXListDie",
			"tag": String(module.get("tag", "")),
			"carrier": "Behavior",
			"extraction": String(module.get("extraction", "")),
			"runtimeStatus": String(module.get("runtimeStatus", "")),
			"deathFx": death_fx,
			"sourceIni": source_ini,
			"line": field_line,
			"moduleLine": int(module.get("line", 0)),
		}
		var presentation_value: Variant = document.get("presentation", {})
		var sealed_value: Variant = (
			(presentation_value as Dictionary).get("deathFxBinding")
			if typeof(presentation_value) == TYPE_DICTIONARY else null
		)
		if sealed_value != null:
			if typeof(sealed_value) != TYPE_DICTIONARY or not _valid_death_fx_binding(
				sealed_value as Dictionary, death_fx
			):
				_fail("neutral prop death FX binding is invalid")
				return {}
			found["deathFxBinding"] = (sealed_value as Dictionary).duplicate(true)
	return found


func _valid_death_fx_binding(binding: Dictionary, death_fx: String) -> bool:
	if (
		String(binding.get("schema", "")) != "openbfme.neutral-prop-death-fx-binding"
		or int(binding.get("schemaVersion", -1)) != 0
		or String(binding.get("objectId", "")) != object_id
		or String(binding.get("fxListId", "")) != death_fx
		or String(binding.get("presentationStatus", "")) != "sealed-authored-route"
		or not _sha256(String(binding.get("particleClosureSha256", "")))
	):
		return false
	var source_value: Variant = binding.get("sourceSpan")
	var nuggets_value: Variant = binding.get("authoredNuggets")
	var particles_value: Variant = binding.get("particleBindings")
	if (
		typeof(source_value) != TYPE_DICTIONARY
		or not _sha256(String((source_value as Dictionary).get("sha256", "")))
		or typeof(nuggets_value) != TYPE_ARRAY
		or (nuggets_value as Array).is_empty()
		or typeof(particles_value) != TYPE_DICTIONARY
	):
		return false
	var particles := particles_value as Dictionary
	var audio_closure_value: Variant = binding.get("audioClosure")
	var audio_bindings_value: Variant = binding.get("audioBindings")
	if (
		String(particles.get("schema", "")) != "openbfme.ability-fx-bindings"
		or int(particles.get("schemaVersion", -1)) != 0
		or particles.get("presentableFxListIds", []) != [death_fx]
		or particles.get("unresolved", []) != []
		or typeof(particles.get("definitionRegistry")) != TYPE_ARRAY
		or (particles.get("definitionRegistry", []) as Array).is_empty()
		or typeof(particles.get("textures")) != TYPE_ARRAY
		or (particles.get("textures", []) as Array).is_empty()
		or typeof(audio_closure_value) != TYPE_DICTIONARY
		or typeof(audio_bindings_value) != TYPE_DICTIONARY
	):
		return false
	var audio_closure := audio_closure_value as Dictionary
	var audio_bindings := audio_bindings_value as Dictionary
	var audio_roots := _string_values(audio_closure.get("rootIds", []))
	var audio_samples := _string_values(audio_closure.get("sampleIds", []))
	if audio_roots.is_empty() or audio_samples.is_empty() or audio_bindings.size() != audio_roots.size():
		return false
	for audio_id in audio_roots:
		var outputs := _string_values(audio_bindings.get(audio_id, []))
		if outputs.size() != audio_samples.size():
			return false
		for output in outputs:
			if not output.begins_with("assets/audio/neutral-props/") or not output.ends_with(".wav"):
				return false
	var particle_names: Array[String] = []
	var has_shake := false
	var has_sound := false
	for nugget_value in nuggets_value as Array:
		if typeof(nugget_value) != TYPE_DICTIONARY:
			return false
		var nugget := nugget_value as Dictionary
		var kind := String(nugget.get("kind", ""))
		if kind not in ["ParticleSystem", "ViewShake", "Sound", "FXList", "FXListAtBonePos"]:
			return false
		var assignments_value: Variant = nugget.get("assignments")
		if typeof(assignments_value) != TYPE_ARRAY or typeof(nugget.get("sourceSpan")) != TYPE_DICTIONARY:
			return false
		for assignment_value in assignments_value as Array:
			if typeof(assignment_value) != TYPE_DICTIONARY:
				return false
			var assignment := assignment_value as Dictionary
			if String(assignment.get("field", "")) == "" or typeof(assignment.get("value")) != TYPE_STRING or typeof(assignment.get("sourceSpan")) != TYPE_DICTIONARY:
				return false
			if kind == "ParticleSystem" and String(assignment.get("field", "")).nocasecmp_to("Name") == 0:
				particle_names.append(String(assignment.get("value", "")))
			has_shake = has_shake or kind == "ViewShake"
			has_sound = has_sound or kind == "Sound"
	var lists_value: Variant = particles.get("fxLists")
	if typeof(lists_value) != TYPE_ARRAY or (lists_value as Array).size() != 1:
		return false
	var list_value: Variant = (lists_value as Array)[0]
	if typeof(list_value) != TYPE_DICTIONARY:
		return false
	var list := list_value as Dictionary
	var declared := _string_values(list.get("particleSystemIds", []))
	particle_names.sort_custom(func(a: String, b: String) -> bool: return a.naturalnocasecmp_to(b) < 0)
	declared.sort_custom(func(a: String, b: String) -> bool: return a.naturalnocasecmp_to(b) < 0)
	return (
		String(list.get("fxListId", "")) == death_fx
		and particle_names == declared
		and bool(list.get("hasViewShake", false)) == has_shake
		and (list.get("audioEventIds", []) as Array).is_empty() == not has_sound
		and _string_values(list.get("audioEventIds", [])) == audio_roots
	)


func _string_values(value: Variant) -> Array[String]:
	var result: Array[String] = []
	if typeof(value) != TYPE_ARRAY:
		return result
	for item in value as Array:
		if typeof(item) != TYPE_STRING or String(item) == "":
			return []
		result.append(String(item))
	return result


func _sha256(value: String) -> bool:
	if value.length() != 64:
		return false
	for character in value:
		if character not in "0123456789abcdef":
			return false
	return true


func _stage_visual_components(recipe: Dictionary) -> Array[String]:
	var rows: Array = []
	if domain == "prop":
		var converted: Variant = recipe.get("convertedVisual")
		if typeof(converted) == TYPE_DICTIONARY:
			rows = [{
				"output": String((converted as Dictionary).get("glb", "")),
				"conditions": (converted as Dictionary).get("sourceConditionSets", [[]])[0],
				"default": true,
				"role": "passive-prop",
			}]
	else:
		rows = recipe.get("components", []) as Array
	var paths: Array[String] = []
	var seen: Dictionary = {}
	for row_value in rows:
		if typeof(row_value) != TYPE_DICTIONARY:
			return []
		var component := row_value as Dictionary
		var relative := String(component.get("output", "")).replace("\\", "/")
		if relative == "" or relative.get_extension().to_lower() != "glb":
			return []
		var content_db := _content_db()
		if content_db == null:
			return []
		var resolved := String(content_db.call("resolve_asset", relative, pack_root))
		if resolved == "" or not _path_is_within(pack_root, resolved):
			return []
		var visual := AssetFactoryScript._try_load_model(resolved)
		if visual == null:
			return []
		var count := _mesh_count(visual)
		if count <= 0:
			visual.free()
			return []
		visual.name = "Component_%02d_%s" % [_component_nodes.size(), String(component.get("role", "visual"))]
		add_child(visual)
		mesh_instance_count += count
		_collect_animation_players(visual)
		_component_nodes.append({"node": visual, "recipe": component.duplicate(true)})
		if not seen.has(relative.to_lower()):
			seen[relative.to_lower()] = true
			paths.append(relative)
	return paths


func _content_db() -> Node:
	var tree := Engine.get_main_loop() as SceneTree
	return tree.root.get_node_or_null("ContentDB") if tree != null else null


func _path_is_within(root_path: String, candidate_path: String) -> bool:
	var normalized_root := root_path.replace("\\", "/").simplify_path().trim_suffix("/").to_lower()
	var normalized_candidate := candidate_path.replace("\\", "/").simplify_path().to_lower()
	return normalized_root != "" and normalized_candidate.begins_with(normalized_root + "/")


func _semantic_state(row: Dictionary) -> String:
	if int(row.get("health", 1)) <= 0 or String(row.get("state", "")).to_lower() in ["dead", "death", "dying"]:
		return "death"
	var state := String(row.get("state", "idle")).to_lower()
	if state in ["run", "move", "moving"]:
		return "move"
	if state in ["attack", "attacking", "firing"]:
		return "attack"
	return "idle"


func _apply_component_visibility(state: String) -> void:
	for entry_value in _component_nodes:
		var entry := entry_value as Dictionary
		var node := entry.get("node") as Node3D
		var recipe := entry.get("recipe", {}) as Dictionary
		if node == null:
			continue
		var conditions := recipe.get("conditions", []) as Array
		var shown := bool(recipe.get("default", false)) or conditions.is_empty()
		for condition_value in conditions:
			var condition := String(condition_value).to_upper()
			shown = shown or (state == "death" and condition in ["DYING", "RUBBLE", "POST_RUBBLE"])
			shown = shown or (state == "move" and condition in ["MOVING", "ACCELERATE", "DECELERATE"])
			shown = shown or (state == "attack" and (condition.contains("ATTACK") or condition.contains("FIRING") or condition.contains("WEAPON")))
		node.visible = shown


func _apply_semantic_presentation(state: String) -> void:
	active_animation_identifier = ""
	active_animation_name = ""
	var animations: Variant = _visual_recipe.get("coreAnimations")
	if typeof(animations) == TYPE_DICTIONARY:
		var candidates: Variant = (animations as Dictionary).get(state)
		if typeof(candidates) == TYPE_ARRAY:
			for candidate_value in candidates as Array:
				if typeof(candidate_value) != TYPE_DICTIONARY:
					continue
				var identifier := String((candidate_value as Dictionary).get("identifier", ""))
				if identifier == "":
					continue
				var played := _play_identifier(identifier, state in ["idle", "move"])
				if played != "":
					active_animation_identifier = identifier
					active_animation_name = played
					presentation_binding = "authored-animation"
					visible = shroud_visible
					return
	var presentations: Variant = _visual_recipe.get("corePresentations")
	if typeof(presentations) == TYPE_DICTIONARY and typeof((presentations as Dictionary).get(state)) == TYPE_DICTIONARY:
		var receipt := (presentations as Dictionary).get(state) as Dictionary
		presentation_binding = String(receipt.get("binding", ""))
		visible = shroud_visible and not (
			state == "death" and presentation_binding == "object-removal"
		)
		return
	# A converted default model with no semantic clip is deliberately static.
	# This is a receipt, not a fabricated animation fallback.
	presentation_binding = "static-converted-model"
	visible = shroud_visible


func _play_identifier(identifier: String, loop: bool) -> String:
	for player in _animation_players:
		var animation_name := _resolve_animation_name(player, identifier)
		if animation_name == "":
			continue
		var animation := player.get_animation(animation_name)
		if animation != null:
			animation.loop_mode = Animation.LOOP_LINEAR if loop else Animation.LOOP_NONE
		player.play(animation_name)
		return animation_name
	return ""


func _resolve_animation_name(player: AnimationPlayer, requested: String) -> String:
	var want := requested.strip_edges()
	var want_lower := want.to_lower()
	var want_base := want_lower.get_file().replace("\\", "/")
	if want_base.contains("."):
		want_base = want_base.get_slice(".", want_base.get_slice_count(".") - 1)
	for animation_name_value in player.get_animation_list():
		var candidate := String(animation_name_value)
		var folded := candidate.to_lower()
		var base := folded.get_file().replace("\\", "/")
		if base.contains("."):
			base = base.get_slice(".", base.get_slice_count(".") - 1)
		if folded == want_lower or folded.ends_with("/" + want_lower) or folded.ends_with("." + want_lower):
			return candidate
		if base == want_base or base.ends_with("_" + want_base) or want_base.ends_with("_" + base):
			return candidate
	return ""


func _collect_animation_players(root: Node) -> void:
	if root is AnimationPlayer:
		_animation_players.append(root as AnimationPlayer)
	for child in root.get_children():
		_collect_animation_players(child)


func _mesh_count(root: Node) -> int:
	var count := 1 if root is MeshInstance3D and (root as MeshInstance3D).mesh != null else 0
	for child in root.get_children():
		count += _mesh_count(child)
	return count


func _fail(reason: String) -> bool:
	contract_error = reason
	set_meta("contract_error", reason)
	return false
