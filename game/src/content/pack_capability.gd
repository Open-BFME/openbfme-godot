extends RefCounted
## Capability probes for mounted content packs.
##
## The retail slice used to assert pack IDENTITY: "is this pack literally
## bfme2-men-vslice?". A pack id is a label chosen by the importer; every one of
## those gates was really protecting a CAPABILITY - the converted surfaces the
## branch below it goes on to read out of that pack. Gating on the label made
## the slice refuse a newer pack that provides strictly more (the six-faction
## v-slice), and would just as happily have accepted a same-named pack that
## provided nothing.
##
## These probes gate on the surfaces instead. They are deliberately NOT lenient:
## a pack that cannot field the surface still fails, and every refusal names the
## surfaces that were missing rather than naming a pack that was expected.

# pack.json "files" keys WITHOUT WHICH A MATCH CANNOT RUN, each mapped to the
# call site that reads it. This is the admission contract: the slice resolves
# its host pack against it, and the menu refuses to let the player press start
# when no mounted pack can field it. The two must be the same question - a menu
# that passes while the slice refuses is a launch that dies after the loading
# screen - so both go through resolve_host_slice_pack() below.
#
# MEMBERSHIP IS MEASURED, NOT ASSUMED. Each key was removed from a real, working
# pack.json (hard-linked mirror of bfme2-men-vslice, otherwise byte-identical)
# and retail_slice_runner was re-run with the admission requirement relaxed so
# the mirror was still chosen as host. Control: 352 passed / 0 failed.
#   audioEvents removed    -> 35/5. slice_ready refuses: "buildingLifecycle
#                             audio identifier is absent from selected-pack
#                             runtime contracts: BuildingSink". No match.
#   palantirScene removed  -> 5/8. slice_ready refuses: "Private retail HUD is
#                             incomplete: Required UI manifest image
#                             'SGCommandBar' is missing." No match.
#   entryMap/objects/animationCapabilities: the boot path reads the map document
#                             and the soldier/horde/capability documents out of
#                             this root before anything else runs.
# menOrderHint is DELIBERATELY ABSENT from this table; see below.
const HOST_SLICE_SURFACES := {
	"entryMap": "the slice entry map (retail_vertical_slice._resolve_pack_entry_map_definition)",
	"objects": "the shared bundle objects (soldier/horde documents)",
	"animationCapabilities": "the soldier animation capability table",
	"audioEvents": "the retail audio event registry (retail_slice_audio.configure)",
	"palantirScene": "the Palantir HUD dock contract (retail_hud_apt_runtime.configure_from_pack)",
}

# Surfaces that change how a match LOOKS but not whether it runs. They are
# probed individually at their own call sites, each with a documented fallback,
# and they are NOT admission requirements: refusing a launch over one of them
# would block a match that plays.
#
# menOrderHint measured the same way as the table above: removed from the
# mirror's pack.json, retail_slice_runner ran 351 passed / 1 failed - the match
# booted, ticked and tore down, and the single failure was
# private_route_uses_exact_retail_move_hint_without_synthetic_flag, which is the
# order indicator correctly reporting that it fell back to the synthetic hint.
# That is a presentation downgrade, not a blocked launch, so requiring it in the
# admission contract was one surface too strict.
const PRESENTATION_ONLY_SURFACES := {
	"menOrderHint": "the SCMoveHint order-indicator contract (retail_order_indicator.configure)",
}

# The converted retail presentation surfaces that decide whether a unit renders
# with its pack's own retail art or with the repository-authored legal-safe
# fallback (synthetic team/selection tori, health bars, invented team tints).
# Every converted faction pack ships the identical pair; a pack without them has
# nothing for the retail path to bind, so the fallback is the honest answer.
const RETAIL_PRESENTATION_SURFACES := {
	"menSelectionDecal": "the SHADOW_MERGE_DECAL selection contract (retail_selection_decal.configure)",
	"houseColor": "the converted HouseColor recolor masks (retail_house_color.apply)",
}

# The single presentation surface AssetFactory's house-color branch consumes.
const HOUSE_COLOR_SURFACE_KEY := "houseColor"

# The single presentation surface the order indicator's retail hint consumes.
const ORDER_HINT_SURFACE_KEY := "menOrderHint"

static var _declaration_cache: Dictionary = {}


static func clear_cache() -> void:
	_declaration_cache.clear()


static func _mod_loader() -> Node:
	## ModLoader is resolved through the scene tree rather than by its autoload
	## identifier: a `--script` runner compiles its preloaded scripts BEFORE the
	## autoload singletons are registered, and the bare identifier is a compile
	## error there ("Identifier not found: ModLoader") that permanently poisons
	## this script for the whole process. Same late-bound pattern
	## retail_faction_manifest.gd already uses for ContentDB.
	var tree := Engine.get_main_loop() as SceneTree
	if tree == null:
		return null
	return tree.root.get_node_or_null("/root/ModLoader")


static func declaration(pack_root: String) -> Dictionary:
	## The pack's own pack.json, or {} when the root declares none. Cached per
	## root: pack trees are immutable for a load generation and these probes run
	## per battalion/per visual.
	if pack_root == "":
		return {}
	if _declaration_cache.has(pack_root):
		return _declaration_cache[pack_root] as Dictionary
	var mod_loader := _mod_loader()
	if mod_loader == null:
		# No ModLoader yet: answer nothing rather than cache a wrong empty
		# result. Every probe below then refuses, which is the fail-closed side.
		return {}
	var value: Variant = mod_loader._read_json(mod_loader.resolve_pack_path(pack_root, "pack.json"))
	var result: Dictionary = (value as Dictionary) if typeof(value) == TYPE_DICTIONARY else {}
	_declaration_cache[pack_root] = result
	return result


static func declared_files(pack_root: String) -> Dictionary:
	var files: Variant = declaration(pack_root).get("files", {})
	return (files as Dictionary) if typeof(files) == TYPE_DICTIONARY else {}


static func declares_surface(pack_root: String, key: String) -> bool:
	## True when the pack declares the surface AND the declared relative path is
	## containable and present on disk. A declared-but-absent file is a broken
	## pack, not a capable one.
	return surface_path(pack_root, key) != ""


static func surface_path(pack_root: String, key: String) -> String:
	## Resolved absolute path of a declared surface, or "" when the pack does not
	## declare it, declares an unsafe relative path, or the file is missing.
	if pack_root == "":
		return ""
	# A files entry is either a relative path or an attested {path, sha256, ...}
	# record (pack.json "buildingRuntime"). Both name the same surface.
	var declared: Variant = declared_files(pack_root).get(key, "")
	var relative := ""
	if typeof(declared) == TYPE_DICTIONARY:
		relative = String((declared as Dictionary).get("path", ""))
	else:
		relative = String(declared)
	if relative == "":
		return ""
	var mod_loader := _mod_loader()
	if mod_loader == null or not mod_loader.is_safe_relative_path(relative):
		return ""
	var resolved := String(mod_loader.resolve_pack_path(pack_root, relative))
	if not mod_loader.path_is_within(pack_root, resolved) or not FileAccess.file_exists(resolved):
		return ""
	return resolved


static func missing_host_slice_surfaces(pack_root: String) -> Array:
	## The HOST_SLICE_SURFACES keys this pack cannot field, sorted. Empty means
	## the pack can host a match. Used to build refusals that name the gap.
	var missing: Array = []
	var keys: Array = HOST_SLICE_SURFACES.keys()
	keys.sort()
	for key_value in keys:
		var key := String(key_value)
		if not declares_surface(pack_root, key):
			missing.append(key)
	return missing


static func resolve_host_slice_pack(meta_rows: Array) -> Dictionary:
	## THE host-pack question, asked once for every caller. Returns
	## {"root": <pack root>} or {"error": <refusal naming the missing surfaces>}.
	##
	## Both the launch gate (main_menu._men_pack_gate_error) and the boot path
	## (retail_vertical_slice._resolve_host_slice_pack) call this. They used to
	## carry two copies of the walk, which is how they came to disagree about the
	## same pack set; one function means they cannot disagree again.
	##
	## Judged over ContentDB.pack_meta - the pack set the loaded documents
	## actually came from - never a fresh disk scan that could disagree with the
	## loaded state. The set is walked in REVERSE load order because ModLoader
	## sorts the active selection LAST so its documents win shared ids
	## (mod_loader.list_pack_roots, "The active selection must load last"): the
	## last capable pack is therefore the one whose surfaces are live. A
	## supplement that merely wins a shared object id contributes no host
	## surfaces and is passed over, which is the case this walk exists for.
	##
	## Fails closed and loudly. A pack set with nothing capable reports every
	## mounted pack and the surfaces each one lacks, so an unsuitable pack is a
	## named refusal instead of a half-loaded match.
	var report: Array = []
	for index in range(meta_rows.size() - 1, -1, -1):
		var meta := meta_rows[index] as Dictionary
		var root := String(meta.get("root", ""))
		if root == "":
			continue
		var missing: Array = missing_host_slice_surfaces(root)
		if missing.is_empty():
			return {"root": root}
		report.append("%s (missing: %s)" % [
			String(meta.get("id", root.get_file())),
			", ".join(PackedStringArray(missing)),
		])
	report.reverse()
	var surface_names: Array = HOST_SLICE_SURFACES.keys()
	surface_names.sort()
	return {
		"error": "No mounted content pack provides the host slice surfaces (%s). Mounted packs: %s." % [
			", ".join(PackedStringArray(surface_names)),
			"; ".join(PackedStringArray(report)) if not report.is_empty() else "none",
		],
	}


static func provides_retail_presentation(pack_root: String) -> bool:
	## True when the pack ships BOTH converted retail presentation surfaces, so
	## the retail presentation path has something to bind and the legal-safe
	## synthetic overlays are correctly suppressed.
	if pack_root == "" or pack_root.begins_with("res://"):
		return false
	for key_value in RETAIL_PRESENTATION_SURFACES.keys():
		if not declares_surface(pack_root, String(key_value)):
			return false
	return true


static func provides_house_color(pack_root: String) -> bool:
	## True when the pack ships the converted HouseColor masks RetailHouseColor
	## recolors from. Without them the invented team tint is the honest fallback.
	if pack_root == "" or pack_root.begins_with("res://"):
		return false
	return declares_surface(pack_root, HOUSE_COLOR_SURFACE_KEY)


static func provides_order_hint(pack_root: String) -> bool:
	## True when the pack ships the SCMoveHint order-hint contract. The indicator
	## still validates that contract fail-closed before it renders anything.
	if pack_root == "" or pack_root.begins_with("res://"):
		return false
	return declares_surface(pack_root, ORDER_HINT_SURFACE_KEY)
