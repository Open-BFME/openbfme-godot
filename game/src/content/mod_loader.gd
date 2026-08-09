extends Node
## Discovers repository packs, mods, and one durable user-owned content pack.
## Pack load order is deterministic: ascending priority, then normalized path.

const BootProfile = preload("res://src/core/boot_profile.gd")

const BASE_PATH := "res://data"
const RES_MODS := "res://mods"
const USER_MODS := "user://mods"
const USER_PACK_CACHE := "user://content-packs"
const USER_PACK_SELECTION := "user://content-packs/selection.json"
const PACK_CACHE_SETTING := "openbfme/content/user_pack_cache"
const PACK_SELECTION_SETTING := "openbfme/content/user_pack_selection"
const WORKSPACE_CONTENT_SETTING := "openbfme/content/workspace_content_root"
const WORKSPACE_CONTENT_RELATIVE := ".private/content-packs"
const SELECTION_SCHEMA := "openbfme.pack-selection"
const SELECTION_VERSION := 0
## Strict parity profile. When requested, the mounted set is EXACTLY the
## selection's own closure: no res://data base pack, no res:// or user:// example
## mod, no sibling pack, nothing ambient. RULE T24 makes demo data, example mods
## and BFME2 supplements product failures inside the parity profile, and the
## runtime is the only place that can prove which packs actually mounted (RULE
## P8), so the mode is explicit and its report is machine-readable.
##
## Requested by either OPENBFME_STRICT_PARITY_PROFILE=1 or a selection document
## carrying "strictParityProfile": true. It is never inferred, so an ordinary
## playtest, editor run or existing runner keeps its current mounting exactly.
const STRICT_PARITY_PROFILE_ENV := "OPENBFME_STRICT_PARITY_PROFILE"
## Pack-id namespace the strict parity profile will mount. BFME2 packs are
## SOURCE-LAYER input for the RotWK conversion, never runtime supplements
## (RULE T24), so a strict run refuses them loudly instead of serving BFME2
## objects and eight extra BFME2 maps inside a RotWK parity match. Overridable
## per profile so a future non-RotWK parity profile is not blocked by this.
const STRICT_PARITY_PACK_PREFIX_ENV := "OPENBFME_STRICT_PARITY_PACK_PREFIX"
const STRICT_PARITY_PACK_PREFIX := "rotwk-"

var diagnostics: Array[String] = []
## True when the last list_pack_roots scan ran in strict parity profile mode.
var strict_parity_profile := false
## Fail-closed findings from that scan: a missing supplement, an unsafe entry, a
## selected pack that is not a strict completion build. NEVER a silent skip
## (RULE P7) - each one is pushed to the error log AND surfaced here so the
## parity-profile audit report can name it.
var strict_parity_errors: Array[String] = []
## Per-call record of every declared supplement that did not resolve, filled by
## selected_pack_supplements. Outside strict mode these stay diagnostics-only,
## exactly as before.
var supplement_failures: Array[String] = []
## Absolute root of the pack the winning selection made active, or "".
var active_pack_root := ""
## Ambient (repository base, example mod, user mod) roots the last strict scan
## refused to mount. Empty outside strict mode.
var suppressed_ambient_roots: Array[String] = []
## Selection entries the last strict scan refused because they are outside the
## profile's pack-id namespace (BFME2 supplements in a RotWK parity run).
var refused_foreign_packs: Array[String] = []
## Which selection source won the last list_pack_roots scan:
## "external" (OPENBFME_CONTENT), "workspace" (repo .private/content-packs),
## "durable" (user:// cache), or "" when no selection is active.
var active_content_source := ""
## Absolute path of the selection document (or explicit bundle root) that the
## winning source above was resolved FROM. Two sources can select the same pack
## id and bundle sha from different trees, so consumers that key a cache on
## content identity need the path, not just the source name.
var active_selection_path := ""

# Boot-path memoization. _link_status performs a DirAccess.open per call, and
# boot-time asset resolution calls it for every path segment of every resolved
# asset (tens of thousands of opens on large retail pack sets). Link status of
# the immutable pack trees cannot change while a load generation is active, so
# results are memoized per (parent, child) - now inside the parent's own
# _dir_index_cache entry - and flushed whenever the pack set is re-scanned
# (list_pack_roots) or the selection is rewritten (select_user_pack). Every
# cache below shares that one generation lifetime through clear_path_caches.
var _link_cache_mutex := Mutex.new()

# MEASURED BOOT HOT PATH. ContentDB's playable-runtime validation calls
# resolve_asset ~16,000 times per boot, and 2,889 ms of a 3,427 ms
# resolve_asset total was spent right here in resolve_pack_path - NOT in the
# link syscalls but in recomputing the same string transforms over and over.
# One resolve_pack_path used to run _absolute_path six times and
# _comparison_path four times over ~200-character absolute paths (every
# converted asset lives under a sha-256 directory name), then re-walked all
# five path segments through a per-(parent,child) cache that costs two mutex
# operations and a string concatenation each.
#
# The two caches below remove that repetition. NEITHER SKIPS A CHECK:
#  * _absolute_path_cache memoizes a pure function of its input string.
#  * _clean_prefix_cache records directories already PROVEN link-free from the
#    pack root down. A later candidate under a proven-clean parent still probes
#    its own final segment; it only stops re-proving the ancestors that a
#    previous candidate already walked segment by segment.
# Both are flushed by clear_path_caches, so they share the generation lifetime
# exactly: the pack trees are immutable while a load generation is active, and
# any re-scan or selection rewrite invalidates every cache together.
var _absolute_path_cache: Dictionary = {}
var _clean_prefix_cache: Dictionary = {}

# MEASURED, SECOND PASS. With the caches above in place a warm resolve_pack_path
# still cost 46 us per call over 15,177 boot calls (~700 ms single-threaded, and
# far worse across eight validation threads because every call took ~14 turns on
# the one mutex below). Almost none of that was syscalls: it was re-deriving,
# per call, facts that depend only on the PACK ROOT - its absolute form, its
# comparison form, and whether the root itself is a link.
#
# _root_context_cache records those once per pack root per generation. There are
# four roots and 15,177 calls, so this is the same proof done 4 times instead of
# 15,177 times. See _root_context.
var _root_context_cache: Dictionary = {}

# One directory enumeration replacing N per-file syscalls.
#
# MEASURED: the 12,027 distinct assets validated at boot live in 361
# directories. Asking the filesystem about them one file at a time cost 596 ms
# of FileAccess.file_exists; ONE DirAccess enumeration per directory returns the
# same 12,027 names in 85 ms. The entry also holds the per-child is_link answers
# (see _link_status), keyed inside the directory's own dictionary instead of a
# global cache keyed by a concatenated ~200-character string.
#
# Entry shape: {"readable": bool, "names": {folded_name: true}, "links":
# {child_name: int}, "dir": DirAccess}. `readable` false means the directory
# could not be opened, which callers must treat as "unknown", never as "absent".
var _dir_index_cache: Dictionary = {}


func clear_path_caches() -> void:
	_link_cache_mutex.lock()
	_absolute_path_cache.clear()
	_clean_prefix_cache.clear()
	_root_context_cache.clear()
	_dir_index_cache.clear()
	_link_cache_mutex.unlock()


func list_pack_roots() -> Array[String]:
	diagnostics.clear()
	strict_parity_errors.clear()
	suppressed_ambient_roots.clear()
	refused_foreign_packs.clear()
	supplement_failures.clear()
	strict_parity_profile = false
	active_pack_root = ""
	clear_path_caches()
	var roots: Array[String] = []
	# AMBIENT ROOTS, HELD SEPARATELY. These are the repository base pack, the
	# shipped example mods and the user mod folder: content nobody selected. The
	# strict parity profile refuses all of them (see the merge below); every
	# other run keeps them, in their original position ahead of the selection.
	var ambient: Array[String] = []
	_collect_packs(BASE_PATH, ambient)
	_collect_packs(RES_MODS, ambient)
	_ensure_dir(USER_MODS)
	_collect_packs(USER_MODS, ambient)

	# Developer/CI override. This remains ephemeral; normal installs use the
	var external := OS.get_environment("OPENBFME_CONTENT")
	var external_selected := ""
	var external_unusable := false
	active_selection_path = ""
	if external != "":
		if DirAccess.dir_exists_absolute(external):
			if is_valid_pack_root(external):
				# An explicit immutable bundle root is a complete selection, not
				# a supplement directory. It must suppress the durable user pack.
				external_selected = external
				active_selection_path = _absolute_path(external)
				roots.append(external_selected)
			else:
				# A private workspace mirrors the normal immutable cache layout:
				# selection.json points through <pack-id>/<bundle-hash>, while
				# immediate child packs may provide audited supplements. A strict
				# completion build is already a composed closure, so loading sibling
				# packs would let stale leaves override the selected bundle.
				var external_selection := external.path_join("selection.json")
				if FileAccess.file_exists(external_selection):
					external_selected = selected_user_pack_root(external, external_selection)
					if external_selected != "":
						var external_supplements := selected_pack_supplements(external, external_selection)
						if supplement_failures.is_empty():
							active_selection_path = _absolute_path(external_selection)
							roots.append(external_selected)
							roots.append_array(external_supplements)
						else:
							external_selected = ""
							external_unusable = true
							_diagnose("OPENBFME_CONTENT selection is unusable because one or more named supplements failed validation: %s" % external_selection)
						# Explicit selection is a complete load set (active + named
						# supplements). Never auto-mount sibling packs — stale
						# leaves (e.g. private-leaves UI) would override the
						# selected bundle's registries and fail pack-root gates.
					else:
						external_unusable = true
						_diagnose("OPENBFME_CONTENT selection is unusable; refusing to scan sibling packs: %s" % external_selection)
				else:
					external_unusable = true
					_diagnose("OPENBFME_CONTENT has no selection.json; refusing to scan sibling packs: %s" % external)
		else:
			external_unusable = true
			_diagnose("OPENBFME_CONTENT does not exist: %s" % external)

	# An explicit path is a launch contract. If it names neither a valid pack
	# root nor a complete selection, return no roots: never substitute ambient,
	# workspace, durable, or sibling content for bytes the caller did not choose.
	if external != "" and external_unusable:
		_diagnose("Explicit OPENBFME_CONTENT failed closed; no content packs will be mounted.")
		active_content_source = "external-invalid"
		active_pack_root = ""
		return []

	# A generated retail bundle is opt-in. The explicit developer/CI selection
	# replaces the durable user selection so two versions of the same ruleset
	# cannot merge merely because both caches exist.
	_ensure_dir(user_pack_cache_root())
	var active_selected := external_selected
	active_content_source = "external" if external_selected != "" else ""

	# Workspace-first: when no explicit OPENBFME_CONTENT override is set, a repo
	# checkout's .private/content-packs selection is the freshest published
	# truth, so editor playtests must never silently fall back to a stale
	# durable cache copy of the same ruleset. Any workspace that exists but
	# cannot be loaded is diagnosed loudly before the durable fallback runs.
	if external == "" and active_selected == "":
		var workspace := workspace_content_root()
		if workspace != "":
			var workspace_selection := workspace.path_join("selection.json")
			if not FileAccess.file_exists(workspace_selection):
				_diagnose("Workspace content at %s has no selection.json; falling back to the durable user pack cache, which may be STALE." % workspace)
			else:
				var workspace_selected := selected_user_pack_root(workspace, workspace_selection)
				if workspace_selected != "":
					active_selected = workspace_selected
					active_content_source = "workspace"
					active_selection_path = _absolute_path(workspace_selection)
					roots.append(workspace_selected)
					roots.append_array(selected_pack_supplements(workspace, workspace_selection))
				else:
					_diagnose("Workspace content selection %s is unusable; falling back to the durable user pack cache, which may be STALE." % workspace_selection)

	if external_selected == "" and active_selected == "":
		var selected := selected_user_pack_root()
		if selected != "":
			active_selected = selected
			active_content_source = "durable"
			active_selection_path = _absolute_path(user_pack_selection_path())
			# LOUD ON PURPOSE. This is the durable pack in the user data
			# directory, reached only because OPENBFME_CONTENT named nothing
			# usable. It can be months old while the repository's real pack has
			# moved on, and a stale pack producing confidently wrong results
			# that pass review is a recorded failure in this project.
			#
			# Until now this path said nothing. The only message was the
			# upstream "OPENBFME_CONTENT does not exist", which does not name
			# what replaced it, and push_warning alone is invisible when the
			# windowed Godot binary is launched. So this also prints to stdout,
			# which reaches the console binary and the user:// log either way.
			var age := _pack_age_description(selected)
			var notice := (
				"Falling back to the DURABLE pack in user data because "
				+ "OPENBFME_CONTENT selected nothing: %s%s. " % [selected, age]
				+ "If you are testing repository content, set OPENBFME_CONTENT "
				+ "to the pack root before launching run_game.bat."
			)
			_diagnose(notice)
			print("[ModLoader] %s" % notice)
			roots.append(selected)
			roots.append_array(selected_pack_supplements())

	active_pack_root = active_selected
	# STRICT PARITY PROFILE. Requested explicitly; never inferred. When it is on
	# the mounted set is the selection's closure and nothing else, and every
	# fail-closed finding is recorded rather than diagnosed and forgotten.
	if _strict_parity_requested():
		strict_parity_profile = true
		# Cross-edition supplements are refused, not merely reported. A RotWK
		# parity match that quietly serves BFME2 objects and eight extra BFME2
		# maps is a product failure (RULE T24), and a loader that mounts them
		# while the judge complains has already shipped the wrong bytes.
		var prefix := OS.get_environment(STRICT_PARITY_PACK_PREFIX_ENV).strip_edges()
		if prefix == "":
			prefix = STRICT_PARITY_PACK_PREFIX
		var kept: Array[String] = []
		for candidate in roots:
			var candidate_id := String(pack_identity(candidate).get("packId", ""))
			if candidate_id.begins_with(prefix):
				kept.append(candidate)
				continue
			refused_foreign_packs.append(candidate)
			_strict_error(
				"refusing to mount pack '%s' at %s: the strict parity profile mounts only '%s*' packs" % [candidate_id, candidate, prefix]
			)
		roots = kept
		var active_refused := active_selected != "" and not kept.has(active_selected)
		if active_refused:
			_strict_error(
				"the selection's ACTIVE pack %s was refused, so this run mounts no active pack at all" % active_selected
			)
			active_selected = ""
			active_pack_root = ""
		if active_selected == "" and not active_refused:
			_strict_error(
				"strict parity profile is active but no selection resolved a pack root; "
				+ "the run would mount NOTHING rather than fall back to ambient content"
			)
		elif active_selected != "" and not _is_strict_completion_pack(active_selected):
			_strict_error(
				"strict parity profile is active but the selected pack %s is not a strict completion build (pack.json profile_build_complete is not true)" % active_selected
			)
		for failure in supplement_failures:
			_strict_error(failure)
		for ambient_root in ambient:
			suppressed_ambient_roots.append(ambient_root)
		if not ambient.is_empty():
			print("[ModLoader] strict parity profile: refusing %d ambient pack root(s): %s" % [
				ambient.size(), ", ".join(ambient)
			])
	else:
		var merged: Array[String] = []
		merged.append_array(ambient)
		merged.append_array(roots)
		roots = merged

	var unique: Array[String] = []
	var seen: Dictionary = {}
	for root in roots:
		var key := _comparison_path(root)
		if not seen.has(key):
			seen[key] = true
			unique.append(root)
	# The active selection must load last among equal priorities: faction packs
	# ship copies of the shared base bundle objects, and the active pack's
	# documents (not a supplement's copy) must win those shared ids.
	var active_key := ""
	if active_selected != "":
		active_key = _comparison_path(active_selected)
	unique.sort_custom(func(a: String, b: String) -> bool:
		var pa := _pack_priority(a)
		var pb := _pack_priority(b)
		if pa != pb:
			return pa < pb
		var a_active := active_key != "" and _comparison_path(a) == active_key
		var b_active := active_key != "" and _comparison_path(b) == active_key
		if a_active != b_active:
			return not a_active
		return _comparison_path(a) < _comparison_path(b)
	)
	if active_selected != "":
		print("[ModLoader] content source=%s active=%s" % [active_content_source, active_selected])
	return unique


func workspace_content_root() -> String:
	## Repo-checkout content workspace holding the freshest published selection.
	## An explicit project-setting override always wins (test seam). Implicit
	## detection of <repo>/.private/content-packs applies only to non-exported
	## runs whose durable cache settings are at their defaults — a test fixture
	## that overrides the cache owns the whole resolution and must not be
	## hijacked by the developer's real workspace.
	var configured := String(ProjectSettings.get_setting(WORKSPACE_CONTENT_SETTING, ""))
	if configured != "":
		return _absolute_path(configured)
	if not OS.has_feature("editor"):
		return ""
	if ProjectSettings.has_setting(PACK_CACHE_SETTING) or ProjectSettings.has_setting(PACK_SELECTION_SETTING):
		return ""
	var workspace := _absolute_path("res://").get_base_dir().path_join(WORKSPACE_CONTENT_RELATIVE)
	if not DirAccess.dir_exists_absolute(workspace):
		return ""
	return workspace


func user_pack_cache_root() -> String:
	return String(ProjectSettings.get_setting(PACK_CACHE_SETTING, USER_PACK_CACHE))


func user_pack_selection_path() -> String:
	return String(ProjectSettings.get_setting(PACK_SELECTION_SETTING, USER_PACK_SELECTION))


func user_pack_cache_absolute() -> String:
	return _absolute_path(user_pack_cache_root())


func selected_user_pack_root(cache_root: String = "", selection_path: String = "") -> String:
	var cache := user_pack_cache_root() if cache_root == "" else cache_root
	var selection := user_pack_selection_path() if selection_path == "" else selection_path
	if not FileAccess.file_exists(selection):
		return ""
	var raw: Variant = _read_json(selection)
	if typeof(raw) != TYPE_DICTIONARY:
		_diagnose("Content selection is not a JSON object: %s" % selection)
		return ""
	var config := raw as Dictionary
	if String(config.get("schema", "")) != SELECTION_SCHEMA or int(config.get("schemaVersion", -1)) != SELECTION_VERSION:
		_diagnose("Unsupported content selection schema: %s" % selection)
		return ""
	var relative := String(config.get("activePack", ""))
	var selected := resolve_pack_path(cache, relative)
	if selected == "":
		_diagnose("Content selection has an unsafe activePack path: %s" % relative)
		return ""
	if not is_valid_pack_root(selected):
		_diagnose("Selected content pack is invalid or missing: %s" % selected)
		return ""
	return selected


func selected_pack_supplements(cache_root: String = "", selection_path: String = "") -> Array[String]:
	## Optional explicit supplement bundles named by the selection document.
	## Each entry is a cache-relative <pack-id>/<bundle-hash> root resolved
	## under the same containment and link rules as the active pack. This is
	## how a converted faction pack loads alongside the selected host bundle
	## without scanning siblings; invalid entries fail closed: they are
	## diagnosed and skipped, never searched for.
	var supplements: Array[String] = []
	supplement_failures.clear()
	var cache := user_pack_cache_root() if cache_root == "" else cache_root
	var selection := user_pack_selection_path() if selection_path == "" else selection_path
	if not FileAccess.file_exists(selection):
		return supplements
	var raw: Variant = _read_json(selection)
	if typeof(raw) != TYPE_DICTIONARY:
		return supplements
	var config := raw as Dictionary
	if String(config.get("schema", "")) != SELECTION_SCHEMA or int(config.get("schemaVersion", -1)) != SELECTION_VERSION:
		return supplements
	var entries: Variant = config.get("supplementalPacks", [])
	if typeof(entries) != TYPE_ARRAY:
		supplement_failures.append("supplementalPacks is not an array in %s" % selection)
		_diagnose("Content selection supplementalPacks is not an array: %s" % selection)
		return supplements
	for entry_value in entries as Array:
		var relative := String(entry_value)
		var supplement := resolve_pack_path(cache, relative)
		if supplement == "":
			# Recorded as well as diagnosed: outside strict mode this stays a
			# warning exactly as before, but a parity profile that quietly
			# mounts eight packs where the selection named nine is the failure
			# this whole packet exists to make visible (RULE P7).
			supplement_failures.append("declared supplement %s resolves to an unsafe path under %s" % [relative, cache])
			_diagnose("Content selection has an unsafe supplementalPacks path: %s" % relative)
			continue
		if not is_valid_pack_root(supplement):
			supplement_failures.append("declared supplement %s is missing or invalid at %s" % [relative, supplement])
			_diagnose("Supplemental content pack is invalid or missing: %s" % supplement)
			continue
		supplements.append(supplement)
	return supplements


func select_user_pack(relative_pack: String, cache_root: String = "", selection_path: String = "") -> String:
	## Persist a cache-relative selection. Returns an empty string on success.
	var cache := user_pack_cache_root() if cache_root == "" else cache_root
	var selection := user_pack_selection_path() if selection_path == "" else selection_path
	var selected := resolve_pack_path(cache, relative_pack)
	if selected == "":
		return "unsafe_active_pack_path"
	if not is_valid_pack_root(selected):
		return "invalid_pack_root"
	_ensure_dir(selection.get_base_dir())
	var file := FileAccess.open(selection, FileAccess.WRITE)
	if file == null:
		return "selection_write_failed"
	file.store_string(JSON.stringify({
		"schema": SELECTION_SCHEMA,
		"schemaVersion": SELECTION_VERSION,
		"activePack": relative_pack.replace("\\", "/"),
	}, "  ") + "\n")
	file.close()
	clear_path_caches()
	return ""


func is_valid_pack_root(pack_root: String) -> bool:
	var pack_path := resolve_pack_path(pack_root, "pack.json")
	if pack_path == "" or not FileAccess.file_exists(pack_path):
		return false
	var raw: Variant = _read_json(pack_path)
	if typeof(raw) != TYPE_DICTIONARY:
		return false
	var meta := raw as Dictionary
	if String(meta.get("id", "")).strip_edges() == "":
		return false
	if meta.has("schema"):
		if String(meta.get("schema", "")) != "openbfme.content-pack" or int(meta.get("schemaVersion", -1)) != 0:
			return false
		var policy: Variant = meta.get("dataPolicy", {})
		if typeof(policy) == TYPE_DICTIONARY and bool((policy as Dictionary).get("externalPathsAllowed", false)):
			return false
	return true


func is_safe_relative_path(relative_path: String) -> bool:
	if relative_path == "" or relative_path != relative_path.strip_edges():
		return false
	var normalized := relative_path.replace("\\", "/")
	if normalized.begins_with("/") or normalized.begins_with("~") or normalized.contains(":"):
		return false
	for segment in normalized.split("/", false):
		if segment == "" or segment == "." or segment == "..":
			return false
	return true


func resolve_pack_path(pack_root: String, relative_path: String) -> String:
	## Resolve a declared pack-relative path while enforcing lexical and physical
	## containment. A symlink/junction anywhere at or below the pack root is a
	## fail-closed boundary violation, even when its text still looks contained.
	if pack_root == "" or not is_safe_relative_path(relative_path):
		return ""
	var context := _root_context(pack_root)
	var root: String = context["root"]
	var candidate := root.path_join(relative_path.replace("\\", "/")).simplify_path()
	# FAST CONTAINMENT, STRICTLY STRONGER THAN THE ONE IT REPLACES.
	#
	# `candidate` was built from `root` by path_join, and is_safe_relative_path
	# has already rejected every segment simplify_path could have used to climb
	# out of it: "", ".", "..", a leading "/" or "~", and any ":" drive
	# specifier. So a BYTE-EXACT prefix match against `root` proves containment
	# outright, and it proves MORE than path_is_within did - that comparison
	# absolutized and case-folded both sides, so it also accepted candidates
	# whose prefix merely matched case-insensitively.
	#
	# Byte-exact success therefore implies the old predicate. Byte-exact failure
	# does NOT imply the old predicate failed (a caller may pass an unsimplified
	# root, where simplify_path rewrites the prefix), so that case still runs
	# the original check rather than rejecting a path that used to resolve.
	var contained := candidate.begins_with(context["prefix"])
	if not contained and not path_is_within(root, candidate):
		return ""
	# EXPORTED BUILDS: a res:// path lives inside the .pck, not on disk, so
	# DirAccess.open(globalize_path(...)) returns null and the link probe
	# answers "yes, links" for every bundled root - rejecting the game's OWN
	# base packs as if they were hostile symlinked content. That is exactly
	# what made an exported build report packs=2 units=0 factions=0 while the
	# .pck demonstrably contained all of it: game/data/base carries 33 unit
	# and 4 faction documents, the precise counts a working build reports.
	#
	# Bundled resources cannot contain links, so the probe is skipped for
	# them. Every user:// and absolute external path is still fully checked -
	# the boundary this guard exists to defend is unchanged.
	if not context["is_res"] and _context_has_link_component(context, candidate, contained):
		return ""
	return candidate


func _root_context(pack_root: String) -> Dictionary:
	## Everything about a pack root that resolve_pack_path used to re-derive on
	## every single call: its normalized form, the "<root>/" prefix, whether it
	## is a res:// path (link probe skipped, see resolve_pack_path), its absolute
	## form, and whether the root directory is ITSELF a link. Computed once per
	## root per generation and flushed by clear_path_caches together with every
	## other path cache, so it shares their lifetime exactly - the pack trees are
	## immutable while a load generation is active.
	_link_cache_mutex.lock()
	var cached: Variant = _root_context_cache.get(pack_root)
	_link_cache_mutex.unlock()
	if cached is Dictionary:
		return cached as Dictionary
	var root := pack_root.replace("\\", "/").trim_suffix("/")
	var context: Dictionary = {
		"root": root,
		"prefix": root + "/",
		"is_res": root.begins_with("res://"),
		"absolute": root,
		"absolute_is_root": true,
		"root_is_link": false,
	}
	if not bool(context["is_res"]):
		var absolute := _absolute_path(root).trim_suffix("/")
		context["absolute"] = absolute
		context["absolute_is_root"] = absolute == root
		# Reject a pack/cache root that is itself a link or junction. Parents
		# above the configured root define the trusted storage location and are
		# out of this pack-relative boundary. Unchanged check, asked once.
		var root_name := absolute.get_file()
		if root_name != "":
			context["root_is_link"] = _link_status(absolute.get_base_dir(), root_name) != 0
	_link_cache_mutex.lock()
	_root_context_cache[pack_root] = context
	_link_cache_mutex.unlock()
	return context


func path_is_within(root_path: String, candidate_path: String) -> bool:
	var root := _comparison_path(_absolute_path(root_path)).trim_suffix("/")
	var candidate := _comparison_path(_absolute_path(candidate_path))
	return candidate.begins_with(root + "/")


func _path_has_link_component(root_path: String, candidate_path: String) -> bool:
	## Retained entry point for callers that have not already proved containment.
	return _context_has_link_component(_root_context(root_path), candidate_path, false)


func _context_has_link_component(context: Dictionary, candidate_path: String, contained: bool) -> bool:
	var root: String = context["absolute"]
	# _absolute_path is the IDENTITY on `candidate_path` whenever the root was
	# already in absolute, forward-slash, simplified form: the candidate is that
	# root plus "/" plus segments is_safe_relative_path already proved contain no
	# backslash-vs-slash or "."/".." ambiguity, and resolve_pack_path ran
	# simplify_path over the join. Proving that once per root (absolute_is_root)
	# removes a cached-dictionary lookup keyed by a ~200-character string from
	# every one of the boot's 15,177 calls.
	var candidate := candidate_path if bool(context["absolute_is_root"]) else _absolute_path(candidate_path)
	# THE DUPLICATE PROOF, REMOVED. This used to re-run path_is_within here, on
	# the same two strings resolve_pack_path had just tested, costing two more
	# simplify_path + to_lower passes over a ~200-character path per call.
	# `contained` carries resolve_pack_path's byte-exact result forward: when it
	# is true, containment is already proven and strictly more strongly than
	# path_is_within states it (see resolve_pack_path). When it is false - the
	# only case a caller can reach through _path_has_link_component - the check
	# still runs in full.
	if not contained and not path_is_within(root, candidate):
		return true
	if bool(context["root_is_link"]):
		return true
	# A candidate whose PARENT directory is already in _clean_prefix_cache needs
	# no segment walk at all. The proof is cumulative: a directory only enters
	# that cache after every segment from the pack root down to it returned
	# status 0, so a cached parent is exactly equivalent to re-walking those
	# segments and getting 0 again. That is the whole cost for the 12,027 boot
	# assets after the first file in each of their 361 directories.
	var parent := candidate.get_base_dir()
	var parent_proven := false
	_link_cache_mutex.lock()
	parent_proven = _clean_prefix_cache.has(parent)
	_link_cache_mutex.unlock()
	if parent_proven:
		return _link_status(parent, candidate.get_file()) != 0
	# Walk the segments, but start from the deepest ancestor a previous candidate
	# already proved link-free.
	var relative := candidate.substr(root.length() + 1)
	var segments := relative.split("/", false)
	var current := root
	var first_unproven := 0
	_link_cache_mutex.lock()
	for index in segments.size():
		var ancestor := current.path_join(segments[index])
		if not _clean_prefix_cache.has(ancestor):
			break
		current = ancestor
		first_unproven = index + 1
	_link_cache_mutex.unlock()
	for index in range(first_unproven, segments.size()):
		var segment := segments[index]
		var status := _link_status(current, segment)
		if status != 0:
			return true
		current = current.path_join(segment)
		# Record only interior components. The final component is usually the
		# asset FILE, and caching one entry per file would grow the dictionary by
		# tens of thousands of strings for no reuse - prefixes are what repeat.
		if index < segments.size() - 1:
			_link_cache_mutex.lock()
			_clean_prefix_cache[current] = true
			_link_cache_mutex.unlock()
	return false


func directory_contains(directory_path: String, file_name: String) -> int:
	## 1 = the directory listing contains that FILE, 0 = it does not, -1 = the
	## directory could not be enumerated and the answer is unknown.
	##
	## WHY: proving 12,027 declared assets exist cost 596 ms of one-stat-per-file.
	## The same 12,027 names come back from 361 directory enumerations in 85 ms.
	## Callers must treat -1 and 0 as "ask the filesystem directly" so that a
	## directory this cannot enumerate (a res:// path served from the .pck, an
	## imported-only resource) can never be reported as missing - see
	## ContentDB._asset_exists.
	if directory_path == "" or file_name == "":
		return -1
	var entry := _dir_index(directory_path)
	if not bool(entry["readable"]):
		return -1
	var names: Dictionary = entry["names"]
	return 1 if names.has(_fold_entry_name(file_name)) else 0


func warm_directory_index(directories: Array) -> int:
	## Enumerate the named directories up front, in parallel, recording every
	## child name AND every child's link status. Returns how many were newly
	## indexed.
	##
	## WHY THIS EXISTS. ContentDB validates playable-unit documents across the
	## worker pool, and every asset reference in them reached the filesystem
	## through _link_status / directory_contains while holding _link_cache_mutex.
	## MEASURED: the eight-thread fan-out took 1,658 ms of wall time to do 1,101
	## ms of single-threaded work - the threads were queueing on this mutex, not
	## validating. Enumerating first turns every later probe into a lock-held
	## dictionary read with no syscall under it.
	##
	## The directory list is a HINT and nothing more. An entry that turns out not
	## to be needed only wastes one enumeration; a needed directory that is not
	## listed is still indexed lazily on first use by _dir_index. No answer this
	## function can give differs from the answer the lazy path would have given,
	## because both come from the same enumeration of the same directory.
	var pending: Array[String] = []
	_link_cache_mutex.lock()
	for value in directories:
		var directory_path := String(value)
		if directory_path != "" and not _dir_index_cache.has(directory_path):
			pending.append(directory_path)
	_link_cache_mutex.unlock()
	if pending.is_empty():
		return 0
	var entries: Array = []
	entries.resize(pending.size())
	if pending.size() == 1 or OS.get_processor_count() <= 1:
		for index in pending.size():
			entries[index] = _build_dir_index(pending[index])
	else:
		# Each element builds its OWN entry into its own pre-sized slot and
		# touches no shared state, so the parallel phase needs no lock at all.
		var group := WorkerThreadPool.add_group_task(
			func(element: int) -> void:
				entries[element] = _build_dir_index(pending[element]),
			pending.size()
		)
		WorkerThreadPool.wait_for_group_task_completion(group)
	_link_cache_mutex.lock()
	for index in pending.size():
		# A concurrent lazy _dir_index may have won the race; its entry is built
		# from the same enumeration, so either is correct. Keep the first.
		# Unreadable directories are not recorded, for the reason _dir_index
		# gives: absence is a fact about a moment, not about the pack tree.
		if not bool((entries[index] as Dictionary)["readable"]):
			continue
		if not _dir_index_cache.has(pending[index]):
			_dir_index_cache[pending[index]] = entries[index]
	_link_cache_mutex.unlock()
	return pending.size()


func _build_dir_index(directory_path: String) -> Dictionary:
	## One enumeration of one directory, with no shared state touched, so it is
	## safe to run on the worker pool. Link status is recorded EAGERLY for every
	## child here: the boot's 361 asset directories hold exactly the 12,027 files
	## it validates, so asking per child during the walk costs what asking
	## lazily costs, and it lets the fan-out run without a syscall under the lock.
	var entry: Dictionary = {"readable": false, "names": {}, "links": {}, "dir": null}
	var directory := DirAccess.open(directory_path)
	if directory == null:
		return entry
	# Hidden entries are real entries: FileAccess.file_exists reports them, so
	# the listing must too or this would answer "missing" where the filesystem
	# answers "present".
	directory.include_hidden = true
	directory.list_dir_begin()
	var names: Dictionary = entry["names"]
	var links: Dictionary = entry["links"]
	var name := directory.get_next()
	while name != "":
		if not directory.current_is_dir():
			names[_fold_entry_name(name)] = true
		links[name] = 1 if directory.is_link(name) else 0
		name = directory.get_next()
	directory.list_dir_end()
	entry["readable"] = true
	entry["dir"] = directory
	return entry


func _fold_entry_name(name: String) -> String:
	## Windows filesystems answer FileAccess.file_exists case-insensitively, so a
	## directory listing must be matched the same way or a pack that declares
	## "Foo.tga" against an on-disk "foo.tga" would newly be refused.
	return name.to_lower() if OS.get_name() == "Windows" else name


func _dir_index(directory_path: String) -> Dictionary:
	## One enumeration per directory per generation. The mutex is held across the
	## enumeration exactly as _link_status holds it across DirAccess.open: the
	## DirAccess handle stored in the entry is shared state and ContentDB
	## validates runtime documents on the worker pool. Measured cost of every
	## enumeration a boot performs: 85 ms total over 361 directories.
	_link_cache_mutex.lock()
	var cached: Variant = _dir_index_cache.get(directory_path)
	if cached is Dictionary:
		_link_cache_mutex.unlock()
		return cached as Dictionary
	var entry := _build_dir_index(directory_path)
	# NEVER CACHE A DIRECTORY THAT COULD NOT BE OPENED. A present directory's
	# contents are stable for the generation, but "this directory does not exist
	# yet" is not a fact about the pack tree - it is a fact about a moment. The
	# runtime-fixture runners create a pack directory and immediately load from
	# it, and caching the earlier negative made every asset under it unresolvable
	# for the rest of the process. Re-probing a genuinely missing directory costs
	# one failed open on a path nothing resolves anyway.
	if bool(entry["readable"]):
		_dir_index_cache[directory_path] = entry
	_link_cache_mutex.unlock()
	return entry


func _link_status(parent_path: String, child_name: String) -> int:
	## 0 = ordinary/missing entry, 1 = link/reparse point, -1 = unknown.
	## Unknown is rejected by callers so an unreadable parent cannot weaken the
	## containment boundary. The cache is mutex-guarded because threaded content
	## loaders resolve assets concurrently.
	# MEASURED: DirAccess.open dominates this function, and the boot asset walk
	# probes ~16,000 distinct FILES spread over only a few hundred directories -
	# so the old code paid a fresh open per file to ask about its parent. The
	# handle is reused per directory instead. The is_link() question asked of
	# each child is unchanged; only the directory lookup is amortised.
	#
	# SECOND PASS: the answers now live in the parent's own _dir_index entry
	# rather than in one global dictionary keyed by parent + "|" + child. That
	# key was a fresh ~200-character string built and hashed on every call, for
	# 15,177 calls per boot. The per-directory dictionary is keyed by the bare
	# child name and is reached through a lookup the caller usually needs anyway.
	#
	# The mutex covers the DirAccess use as well as the caches, because a cached
	# DirAccess is shared state and ContentDB validates runtime documents on the
	# worker pool.
	var entry := _dir_index(parent_path)
	if not bool(entry["readable"]):
		return -1
	_link_cache_mutex.lock()
	var links: Dictionary = entry["links"]
	var cached: Variant = links.get(child_name)
	if cached is int:
		_link_cache_mutex.unlock()
		return cached as int
	var parent: DirAccess = entry["dir"]
	var status := 1 if parent.is_link(child_name) else 0
	links[child_name] = status
	_link_cache_mutex.unlock()
	return status


func _ensure_dir(path: String) -> void:
	var absolute := _absolute_path(path)
	if not DirAccess.dir_exists_absolute(absolute):
		var err := DirAccess.make_dir_recursive_absolute(absolute)
		if err != OK:
			_diagnose("Could not create content directory %s (error %d)" % [absolute, err])


func _collect_packs(root: String, into: Array[String]) -> void:
	var dir := DirAccess.open(root)
	if dir == null:
		return
	if is_valid_pack_root(root):
		into.append(root)
		return
	dir.list_dir_begin()
	var name := dir.get_next()
	while name != "":
		if dir.current_is_dir() and not name.begins_with("."):
			var pack := root.path_join(name)
			if is_valid_pack_root(pack):
				into.append(pack)
		name = dir.get_next()
	dir.list_dir_end()


func _pack_priority(pack_root: String) -> int:
	var path := resolve_pack_path(pack_root, "pack.json")
	var data: Variant = _read_json(path)
	if typeof(data) == TYPE_DICTIONARY:
		return int((data as Dictionary).get("priority", 100))
	return 100


func _strict_parity_requested() -> bool:
	## Explicit request only. Either the environment asks for it (audit harness,
	## CI) or the winning selection document declares itself a parity profile.
	## Inferring it from pack contents would silently change what every existing
	## runner mounts, which is the opposite of fail-closed.
	var flag := OS.get_environment(STRICT_PARITY_PROFILE_ENV).strip_edges().to_lower()
	if flag != "" and flag != "0" and flag != "false" and flag != "no":
		return true
	if active_selection_path == "" or not active_selection_path.ends_with("selection.json"):
		return false
	var raw: Variant = _read_json(active_selection_path)
	if typeof(raw) != TYPE_DICTIONARY:
		return false
	return bool((raw as Dictionary).get("strictParityProfile", false))


func _strict_error(message: String) -> void:
	strict_parity_errors.append(message)
	# Loud on three channels on purpose: push_error reaches the editor, printerr
	# reaches the console binary's stderr (which the audit harness captures),
	# and the array reaches the machine-readable report.
	push_error("[ModLoader] STRICT PARITY PROFILE: %s" % message)
	printerr("[ModLoader] STRICT PARITY PROFILE: %s" % message)


func is_bundle_digest(name: String) -> bool:
	## A content-addressed bundle directory is exactly 64 lowercase hex digits.
	## Anything else (a hand-made "goal-official-72" folder) is a MUTABLE name:
	## its bytes can change under a selection that still reads identical.
	if name.length() != 64:
		return false
	for index in name.length():
		var character := name.substr(index, 1).to_lower()
		if not ("0123456789abcdef".contains(character)):
			return false
	return true


func pack_identity(pack_root: String) -> Dictionary:
	## Everything an audit needs to name ONE mounted pack: its declared id, the
	## bundle directory it was mounted from, and whether that directory is
	## content-addressed. Derived from the mounted root itself, never from the
	## selection document, so a runtime that mounted something other than what
	## the selection names is visible (RULE P8).
	var normalized := pack_root.replace("\\", "/").trim_suffix("/")
	var manifest := resolve_pack_path(pack_root, "pack.json")
	var raw: Variant = _read_json(manifest)
	var document: Dictionary = (raw as Dictionary) if typeof(raw) == TYPE_DICTIONARY else {}
	var leaf := normalized.get_file()
	var parent := normalized.get_base_dir().get_file()
	var content_addressed := is_bundle_digest(leaf)
	var pack_id := String(document.get("id", ""))
	var embedded := normalized.begins_with("res://")
	var ambient_mod := (
		normalized.begins_with("res://mods")
		or normalized.begins_with("user://mods")
		or normalized.contains("/mods/")
	)
	return {
		"packId": pack_id,
		"packRoot": normalized,
		"bundleDirectory": leaf,
		"bundleRelative": ("%s/%s" % [parent, leaf]) if content_addressed else leaf,
		"bundleDigest": leaf if content_addressed else "",
		"contentAddressed": content_addressed,
		"priority": int(document.get("priority", 100)),
		"strictCompletionBuild": bool(document.get("profile_build_complete", false)),
		"embeddedResource": embedded,
		"ambientMod": ambient_mod,
		"active": (
			active_pack_root != ""
			and _comparison_path(pack_root) == _comparison_path(active_pack_root)
		),
	}


func runtime_pack_report(mounted_roots: Array) -> Dictionary:
	## Machine-readable answer to "what did this process actually mount, in what
	## order, from which selection?". The parity-profile audit consumes exactly
	## this; it never re-derives the answer from the selection document, because
	## the selection document is the thing under test.
	var packs: Array = []
	for value in mounted_roots:
		packs.append(pack_identity(String(value)))
	var ordered_ids: Array = []
	for entry in packs:
		var identity: Dictionary = entry
		# DIGEST-BEARING on purpose. This list used to carry bare pack ids, so a
		# judge could only cross-check the two views at pack-id level and a run
		# that mounted the right nine packs from a WRONG bundle looked
		# self-consistent. Ambient/embedded roots keep their full path so they
		# stay classifiable as res:// base or example mods.
		if bool(identity["embeddedResource"]) or bool(identity["ambientMod"]) or String(identity["packId"]) == "":
			ordered_ids.append(String(identity["packRoot"]))
		else:
			ordered_ids.append("%s/%s" % [String(identity["packId"]), String(identity["bundleDirectory"])])
	var embedded: Array = []
	var ambient: Array = []
	for entry in packs:
		var identity: Dictionary = entry
		if bool(identity["ambientMod"]):
			ambient.append(String(identity["packRoot"]))
		elif bool(identity["embeddedResource"]):
			embedded.append(String(identity["packRoot"]))
	return {
		"activeContentSource": active_content_source,
		"activeSelectionPath": active_selection_path,
		"activePackRoot": active_pack_root,
		"strictParityProfile": strict_parity_profile,
		"strictParityErrors": strict_parity_errors.duplicate(),
		"suppressedAmbientRoots": suppressed_ambient_roots.duplicate(),
		"refusedForeignPacks": refused_foreign_packs.duplicate(),
		"diagnostics": diagnostics.duplicate(),
		"orderedPacks": packs,
		"orderedPackIds": ordered_ids,
		"embeddedResourcePacks": embedded,
		"ambientModPacks": ambient,
	}


func _is_strict_completion_pack(pack_root: String) -> bool:
	var path := resolve_pack_path(pack_root, "pack.json")
	var data: Variant = _read_json(path)
	return (
		typeof(data) == TYPE_DICTIONARY
		and bool((data as Dictionary).get("profile_build_complete", false))
	)


func _read_json(path: String) -> Variant:
	if not FileAccess.file_exists(path):
		return null
	var file: FileAccess = FileAccess.open(path, FileAccess.READ)
	if file == null:
		return null
	var text: String = file.get_as_text()
	var json := JSON.new()
	if json.parse(text) != OK:
		push_error("JSON parse failed: %s @ %s" % [json.get_error_message(), path])
		return null
	return json.data


func _absolute_path(path: String) -> String:
	## Pure function of `path`, memoized: the boot-time asset walk calls this
	## with the same handful of pack roots and the same long candidate strings
	## thousands of times, and replace()+simplify_path() over ~200-character
	## paths was measurable. Mutex-guarded because ContentDB validates playable
	## runtime documents on the worker pool.
	_link_cache_mutex.lock()
	var cached: Variant = _absolute_path_cache.get(path)
	_link_cache_mutex.unlock()
	if cached is String:
		return cached as String
	var resolved := ""
	if path.begins_with("res://") or path.begins_with("user://"):
		resolved = ProjectSettings.globalize_path(path).replace("\\", "/").simplify_path()
	else:
		resolved = path.replace("\\", "/").simplify_path()
	_link_cache_mutex.lock()
	_absolute_path_cache[path] = resolved
	_link_cache_mutex.unlock()
	return resolved


func _comparison_path(path: String) -> String:
	var normalized := path.replace("\\", "/").simplify_path()
	return normalized.to_lower() if OS.get_name() == "Windows" else normalized


## Human-readable age of a pack root, for the durable-fallback notice. The age
## is the point: "stale pack" is only actionable if you can see HOW stale.
## Returns "" when the timestamp cannot be read, rather than inventing one.
func _pack_age_description(pack_root: String) -> String:
	var manifest := pack_root.path_join("pack.json")
	if not FileAccess.file_exists(manifest):
		return ""
	var modified := FileAccess.get_modified_time(manifest)
	if modified <= 0:
		return ""
	var age_seconds := int(Time.get_unix_time_from_system()) - int(modified)
	if age_seconds < 0:
		return ""
	var days := age_seconds / 86400
	var stamp := Time.get_datetime_string_from_unix_time(int(modified), true)
	if days >= 1:
		return " (built %s, %d day%s ago)" % [stamp, days, "" if days == 1 else "s"]
	return " (built %s, today)" % stamp


func _diagnose(message: String) -> void:
	diagnostics.append(message)
	push_warning("[ModLoader] %s" % message)

## See events.gd:_init - per-autoload compile attribution for the boot profiler.
## No-op unless boot profiling is on.
func _init() -> void:
	BootProfile.mark("autoload_compiled:ModLoader")
