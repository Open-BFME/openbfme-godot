extends "res://src/retail_slice/retail_sim_subsystem.gd"
## Map-script world subsystem carved out of retail_slice_sim.gd (drawer 15): named-object namespace, building permissions, script teams/references, sequential scripts, executors, entity flags/status, diplomacy, logic random, object-type queries.
## State stays on the sim; the sim keeps one-line delegates under the original names.



func configure_map_named_object_namespace(names: Array) -> void:
	## Declare the installed map's complete named-object table. Sorted before
	## insertion so byte-equal configuration hashes identically on every peer
	## regardless of caller iteration order (the sim.unpackable_bases discipline).
	var sorted := names.duplicate()
	sorted.sort()
	var table: Dictionary = {}
	for name_value in sorted:
		table[String(name_value)] = true
	sim.map_named_object_namespace = {"names": table}


func map_named_object_namespace_declared() -> bool:
	return not sim.map_named_object_namespace.is_empty()


func map_declares_named_object(name: String) -> bool:
	## Case-sensitive, like retail's strcmp over the name table.
	return (sim.map_named_object_namespace.get("names", {}) as Dictionary).has(name)


# --- Script unit references (the WP16 shared namespace, SIM-owned) ---------
#
# team:int -> {reference_name: structure_id}. Owned by the SIM rather than
# the script-world adapter, DELIBERATELY: a bound reference changes what a
# later script action does (build_base_building at "AI_REF" succeeds or
# refuses on whether AI_REF is bound), which makes it sim-outcome-bearing
# state - and no such state may live outside the snapshot/hash boundary that
# save/load and late-join reproduce. A peer that adopts a snapshot must
# resolve every reference exactly as the peer that minted it, or byte-equal
# sims diverge on the very next scripted action.
#
# Keyed by TEAM because retail executes each AI player's script libraries in
# that player's own context: one world instance per script player, each with
# its own reference namespace. The team id is already authoritative state, so
# the key introduces nothing platform- or load-order-dependent.
#
# HASH INERTNESS: participates in the authoritative state ONLY when non-empty
# (empty-is-absent, the sim.unpackable_bases discipline), so a match whose
# scripts never bind a reference contributes zero bytes to state_hash() and
# the frozen cross-platform pin stands untouched.

## See the block comment above. setup() clears it (match state, not config).
## Values are int (a structure id) or String (a base-flag name) - the two
## kinds of entry the shared namespace carries; see
## bind_script_unit_reference_to_base for why the flag kind exists.
## team -> reference name -> entity id. Companion to sim.script_unit_references
## (structure/base-flag store). Entity nearest-ref binds land here so AI
## SET_REF_TO_NEREST_* is not bag-only.
## Pending CreateObjectDie spawns: {team, position, creation_list, source_entity, tick}.
## Observable sim state when an executable CreateObjectDie fires on death.
## Match-scoped script construction permissions:
## team -> exact retail object type -> false. Absence means retail's default
## allowed state; an allow write erases the override and returns to pristine.
## Stored in the authoritative snapshot only while nonempty.


func set_building_allowed(team: int, object_type: String, allowed: bool) -> bool:
	if not sim._team_roster.has(team) or object_type == "":
		return false
	var permissions: Dictionary = sim.building_permissions_by_team.get(team, {})
	var matching_key := ""
	for authored_value in permissions.keys():
		if String(authored_value).to_lower() == object_type.to_lower():
			matching_key = String(authored_value)
			break
	if matching_key != "":
		permissions.erase(matching_key)
	if not allowed:
		permissions[object_type] = false
	if permissions.is_empty():
		sim.building_permissions_by_team.erase(team)
	else:
		sim.building_permissions_by_team[team] = permissions
	return true


func _building_object_identity_key(value: String) -> String:
	## Source names are case-insensitive SAGE identifiers; converted runtime
	## ids are their punctuation-separated slugs. Collapse both to the same
	## alphanumeric key so MENFORTRESS, MenFortress, and
	## bfme2.object.men-fortress retain one identity.
	var folded := value.to_lower()
	var runtime_separator := folded.find(".object.")
	if runtime_separator >= 0:
		folded = folded.substr(runtime_separator + 8)
	var key := ""
	for index in folded.length():
		var code := folded.unicode_at(index)
		if (code >= 97 and code <= 122) or (code >= 48 and code <= 57):
			key += String.chr(code)
	return key


func building_permission_for_kind(team: int, structure_kind: String) -> Dictionary:
	var permissions: Dictionary = sim.building_permissions_by_team.get(team, {})
	if permissions.is_empty():
		return {"known": true, "allowed": true}
	var source_types: Dictionary = {}
	var runtime_types: Dictionary = {}
	var identity_keys: Dictionary = {}
	var registry = _structure_source_registry(sim.team_manifest_for(team))
	for source_value in registry.keys():
		if String(registry[source_value]) == structure_kind:
			source_types[String(source_value).to_lower()] = true
			identity_keys[_building_object_identity_key(String(source_value))] = true
	var manifest_structure_ids: Dictionary = (
		sim.team_manifest_for(team).get("structure_object_ids", {}) as Dictionary
	)
	if manifest_structure_ids.has(structure_kind):
		var structure_object_id := String(manifest_structure_ids[structure_kind])
		runtime_types[structure_object_id] = true
		identity_keys[_building_object_identity_key(structure_object_id)] = true
	var expansion_rule: Dictionary = sim._expansion_build_rules.get(structure_kind, {})
	var expansion_object_id := String(expansion_rule.get("object_id", ""))
	if expansion_object_id != "":
		runtime_types[expansion_object_id] = true
		identity_keys[_building_object_identity_key(expansion_object_id)] = true
	if source_types.is_empty() and runtime_types.is_empty():
		return {
			"known": false,
			"allowed": false,
			"reason": "no retail object identity is registered for structure kind '%s'" % structure_kind,
		}
	var authored_types: Array = permissions.keys()
	authored_types.sort()
	for authored_value in authored_types:
		var authored_type := String(authored_value)
		if (
			source_types.has(authored_type.to_lower())
			or runtime_types.has(authored_type)
			or runtime_types.has(sim.PlayableUnitAdapter.runtime_object_id(authored_type))
			or identity_keys.has(_building_object_identity_key(authored_type))
		):
			return {
				"known": true,
				"allowed": false,
				"object_type": authored_type,
			}
	return {"known": true, "allowed": true}


func bind_script_entity_reference(team: int, reference: String, entity_id: int) -> bool:
	if team < 0 or reference == "" or entity_id <= 0:
		return false
	if not sim.entities.has(entity_id):
		return false
	if not sim.script_entity_references.has(team):
		sim.script_entity_references[team] = {}
	(sim.script_entity_references[team] as Dictionary)[reference] = entity_id
	return true


func script_entity_reference(team: int, reference: String) -> int:
	return int((sim.script_entity_references.get(team, {}) as Dictionary).get(reference, 0))


func bind_script_unit_reference(team: int, reference: String, structure_id: int) -> bool:
	## Bind (or re-point) `reference` for `team` to a concrete structure id.
	## References are mutable by design (retail re-points
	## AI_CURRENT_CONSTRUCTION_SITE constantly). An empty reference binds
	## nothing (vacuous true). A base-flag name is refused loudly - callers
	## are expected to have cleared the shadow check BEFORE mutating the sim,
	## so tripping this backstop means a caller skipped it.
	if reference == "":
		return true
	if sim.unpackable_bases.has(reference):
		push_error(
			"bind_script_unit_reference refused: '%s' names a base flag; " % reference
			+ "flag names are owned by the unpackable-base table (callers must "
			+ "check the shadow rejection before mutating the sim)"
		)
		return false
	if not sim.script_unit_references.has(team):
		sim.script_unit_references[team] = {}
	(sim.script_unit_references[team] as Dictionary)[reference] = structure_id
	return true


func bind_script_unit_reference_to_base(team: int, reference: String, base_name: String) -> bool:
	## Bind (or re-point) `reference` for `team` to a BASE FLAG by name.
	##
	## WHY A SECOND BINDING KIND, ON RETAIL EVIDENCE. SET_UNIT_REFERENCE's
	## subject slot in the shipped AI libraries is a base-flag name at 32 of
	## its 40 call sites (BASE_FLAG_1..16; the other 8 are BASE_SPAWN_1..8,
	## which this simulation does not model) - the AI aims
	## AI_CURRENT_CONSTRUCTION_SITE / AI_CURRENT_DEF_CONSTRUCTION_SITE at a
	## flag it has NOT yet unpacked and then builds through the reference. A
	## packed flag has no structure id (structure_id is 0 until unpack), so a
	## structure-id-only store could only refuse those 32 sites. Storing the
	## FLAG NAME is not "storing the source string" in the aliasing sense the
	## class comment forbids: a flag name is match configuration, immutable
	## for the match, so it cannot be re-aimed under the reference the way a
	## rebindable reference name could.
	##
	## Same store, same key, same empty-is-absent discipline - the VALUE is a
	## String here and an int for structure bindings, which is exactly the
	## "two kinds of entry" the shared object / unit-reference namespace is
	## documented to carry. Refuses (loudly) a reference that would shadow a
	## flag, and refuses an unknown base name rather than binding a dangling
	## handle.
	if reference == "":
		return true
	if sim.unpackable_bases.has(reference):
		push_error(
			"bind_script_unit_reference_to_base refused: '%s' names a base flag; " % reference
			+ "flag names are owned by the unpackable-base table (callers must "
			+ "check the shadow rejection before mutating the sim)"
		)
		return false
	if not sim.unpackable_bases.has(base_name):
		push_error(
			"bind_script_unit_reference_to_base refused: '%s' is not a base flag " % base_name
			+ "this simulation models; binding it would leave a dangling handle"
		)
		return false
	if not sim.script_unit_references.has(team):
		sim.script_unit_references[team] = {}
	(sim.script_unit_references[team] as Dictionary)[reference] = base_name
	return true


func script_unit_reference(team: int, reference: String) -> int:
	## The structure id bound to `reference` for `team`; 0 when unbound OR
	## when the binding is a base-flag name (structure ids are never 0, and a
	## flag-valued binding is not a structure id - callers that need it must
	## read script_unit_reference_handle).
	var bound: Variant = (sim.script_unit_references.get(team, {}) as Dictionary).get(reference, 0)
	return int(bound) if typeof(bound) == TYPE_INT else 0


func script_unit_reference_base(team: int, reference: String) -> String:
	## The base-flag name bound to `reference` for `team`; "" when unbound or
	## when the binding is a structure id.
	var bound: Variant = (sim.script_unit_references.get(team, {}) as Dictionary).get(reference, 0)
	return String(bound) if typeof(bound) == TYPE_STRING else ""


# --- Script object-type lists (OBJECT_TYPE_LIST stores, SIM-owned) ----------
#
# Named, script-mutable SETS of retail object-type names: the subjects of
# OBJECTLIST_ADDOBJECTTYPE / OBJECTLIST_REMOVEOBJECTTYPE and the resolution
# target of every OBJECT_TYPE_LIST-typed script argument. Retail authors BOTH
# spellings in those slots - a declared list name ("Offensive_Units") and a
# plain object type ("IsengardUrukPit") - and resolves list-first with a
# single-type fallback; resolve_object_type_names below mirrors that exactly.
#
# STATE, NOT CONFIGURATION - decided on retail evidence, not assumption. The
# retail engine's ScriptEngine owns ONE global ObjectTypeList table per match,
# mutated mid-match by script actions and PERSISTED IN SAVE GAMES (OpenSAGE's
# ScriptingSystem.Persist serializes _objectTypeLists; each list is a
# HashSet<string> keyed by name). Mutable-by-actions plus save-file membership
# puts the table inside the snapshot/hash boundary, exactly like
# sim.script_unit_references. UNLIKE the references it is NOT keyed by team: the
# retail store is engine-global (one namespace per match, however many script
# players run), and every peer executes the same lockstep script stream, so
# the converged table is identical on every peer by construction.
#
# CANONICAL FORM: member arrays are kept SORTED and UNIQUE (retail's HashSet
# has no order; a sorted array is the canonical serialization of a set), and
# a list whose last member is removed loses its KEY too, so an emptied table
# returns to the exact pristine hash.
#
# HASH INERTNESS: participates in the authoritative state ONLY when non-empty
# (empty-is-absent, the sim.unpackable_bases discipline), so a match whose
# scripts never build a list contributes zero bytes to state_hash() and the
# frozen cross-platform pin stands untouched. setup() clears it (match state).

## list name -> sorted unique Array of retail object-type name Strings.
## See the block comment above. setup() clears it; hashed only when non-empty.


func change_object_type_list(list_name: String, object_type: String, add: bool) -> Dictionary:
	## OBJECTLIST_ADDOBJECTTYPE (`add` true) / OBJECTLIST_REMOVEOBJECTTYPE
	## (`add` false). Set semantics, matching retail's HashSet store: a
	## duplicate add and an absent remove are successful no-ops. Empty names
	## refuse - "" is neither a list nor a type in the retail vocabulary, and
	## admitting it would mint an unreachable store entry.
	if list_name == "":
		return {"ok": false, "reason": "empty-list-name"}
	if object_type == "":
		return {"ok": false, "reason": "empty-object-type"}
	if add:
		var members: Array = sim.script_object_type_lists.get(list_name, [])
		if not members.has(object_type):
			members.append(object_type)
			members.sort()
		sim.script_object_type_lists[list_name] = members
		return {"ok": true, "reason": ""}
	if sim.script_object_type_lists.has(list_name):
		var members: Array = sim.script_object_type_lists[list_name]
		members.erase(object_type)
		if members.is_empty():
			# Empty-is-absent inside the container too: no empty list may
			# linger as a hash-visible key.
			sim.script_object_type_lists.erase(list_name)
	return {"ok": true, "reason": ""}


func object_type_list_names() -> Array[String]:
	var names: Array[String] = []
	for name_value in sim.script_object_type_lists.keys():
		names.append(String(name_value))
	names.sort()
	return names


func has_object_type_list(list_name: String) -> bool:
	return sim.script_object_type_lists.has(list_name)


func resolve_object_type_names(object_type_list: String) -> Array:
	## Retail's OBJECT_TYPE_LIST argument resolution: a declared list answers
	## its members; any other name IS a single object type (the retail engine
	## looks the name up in the list table and falls back to reading the
	## string as one type - the authored corpus uses both spellings). This is
	## also the correct answer BEFORE list-building scripts have run: retail
	## in that state has no list either and reads the single type. Read-only.
	if sim.script_object_type_lists.has(object_type_list):
		return (sim.script_object_type_lists[object_type_list] as Array).duplicate()
	return [object_type_list]


# --- Script-team registry (named sub-player teams, SIM-owned) ---------------
#
# SAGE teams are independent identities even when they share a player owner.
# Definitions come from decoded map configuration, but membership and flags
# affect gameplay and therefore live inside snapshots/state_hash. Handles are
# typed because entity and structure ids occupy overlapping id spaces.
# Imported objectCount is evidence only: it can never mint a phantom member.


func _script_owner_exists(owner: int) -> bool:
	return sim._is_combatant_team(owner) or owner == sim.NEUTRAL_TEAM or owner == sim.CREEP_TEAM


func register_script_team(
	team_name: String,
	owner: int,
	default_team: bool = false,
	handles: Array = [],
	membership_complete: bool = true,
	unresolved_members: Array = [],
	unmodeled_object_count: int = 0,
	dynamic_default_roster: bool = true,
	marker_only: bool = false
) -> Dictionary:
	if team_name == "":
		return {"ok": false, "reason": "script-team name is empty"}
	if not _script_owner_exists(owner):
		return {"ok": false, "reason": "script-team owner %d is unavailable" % owner}
	if sim.script_teams.has(team_name):
		var existing := sim.script_teams[team_name] as Dictionary
		if (
			int(existing.get("configured_owner", existing.get("owner", -1))) != owner
			or bool(existing.get("default", false)) != default_team
			or bool(existing.get("membership_incomplete", false)) == membership_complete
			or (existing.get("unresolved_members", []) as Array) != unresolved_members
			or int(existing.get("unmodeled_object_count", 0)) != unmodeled_object_count
			or bool(existing.get("explicit_default_membership", false))
			== dynamic_default_roster
			or bool(existing.get("marker_only", false)) != marker_only
		):
			return {"ok": false, "reason": "script team '%s' was rebound" % team_name}
		return {"ok": true, "reason": ""}
	if unmodeled_object_count < 0:
		return {"ok": false, "reason": "script team '%s' has a negative unmodeled count" % team_name}
	var canonical_unresolved: Array[String] = []
	for unresolved_value in unresolved_members:
		if typeof(unresolved_value) != TYPE_STRING or String(unresolved_value) == "":
			return {"ok": false, "reason": "script team '%s' has a malformed unresolved member name" % team_name}
		var unresolved_name := String(unresolved_value)
		canonical_unresolved.append(unresolved_name)
	canonical_unresolved.sort()
	var members: Array = []
	for handle_value in handles:
		if typeof(handle_value) != TYPE_DICTIONARY:
			return {"ok": false, "reason": "script team '%s' has a malformed member handle" % team_name}
		var handle := handle_value as Dictionary
		if (
			typeof(handle.get("kind")) != TYPE_STRING
			or not ["entity", "structure"].has(handle.get("kind"))
			or typeof(handle.get("id")) != TYPE_INT
			or typeof(handle.get("id")) == TYPE_BOOL
		):
			return {"ok": false, "reason": "script team '%s' has a malformed typed member handle" % team_name}
		var kind := String(handle["kind"])
		var object_id := int(handle["id"])
		var row: Dictionary
		if kind == "entity" and sim.entities.has(object_id):
			row = sim.entities[object_id] as Dictionary
		elif kind == "structure" and sim.structures.has(object_id):
			row = sim.structures[object_id] as Dictionary
		else:
			return {"ok": false, "reason": "script team '%s' references an unavailable %s %d" % [team_name, kind, object_id]}
		if int(row.get("team", -1)) != owner:
			return {"ok": false, "reason": "script team '%s' member owner disagrees" % team_name}
		var canonical := {"kind": kind, "id": object_id}
		if not members.has(canonical):
			members.append(canonical)
	members.sort_custom(_script_member_less)
	var record := {
		"owner": owner,
		# Match configuration is immutable even when a retail script later
		# changes the team's controlling player. Keeping the original owner
		# lets setup() reset the match and lets the snapshot view omit teams
		# whose controlling owner was never changed.
		"configured_owner": owner,
	}
	if default_team:
		record["default"] = true
		if not dynamic_default_roster:
			record["explicit_default_membership"] = true
	if not membership_complete:
		record["membership_incomplete"] = true
		if not canonical_unresolved.is_empty():
			record["unresolved_members"] = canonical_unresolved
		if unmodeled_object_count > 0:
			record["unmodeled_object_count"] = unmodeled_object_count
	if not members.is_empty():
		record["members"] = members
	if marker_only:
		if default_team or not members.is_empty():
			return {
				"ok": false,
				"reason": "marker-only team '%s' cannot be default or materialized" % team_name,
			}
		record["marker_only"] = true
	sim.script_teams[team_name] = record
	mark_team_created(team_name)
	return {"ok": true, "reason": ""}


func _script_member_less(a: Dictionary, b: Dictionary) -> bool:
	var a_kind := String(a.get("kind", ""))
	var b_kind := String(b.get("kind", ""))
	return a_kind < b_kind or (a_kind == b_kind and int(a.get("id", -1)) < int(b.get("id", -1)))


func script_team_owner(team_name: String) -> Dictionary:
	if not sim.script_teams.has(team_name):
		return {"ok": false, "reason": "script team '%s' is not registered" % team_name}
	return {"ok": true, "owner": int((sim.script_teams[team_name] as Dictionary).get("owner", -1))}


func transfer_script_team_controlling_player(
	team_name: String, destination_owner: int
) -> Dictionary:
	## Retail TEAM_TRANSFER_TO_PLAYER calls Team::setControllingPlayer. It
	## changes the Team identity's controlling player; it is not Player asset
	## transfer and it does not merge/capture the Team's Object list.
	##
	## The measured BFME2/RotWK AI sites address civilian inheritance teams
	## containing tactical markers, which this sim deliberately does not
	## materialize. Restrict this surface to that exact safe shape. A later
	## combat-team transfer needs entity ownership, CP, upgrade, queue and
	## spatial invariants and must be a separate packet.
	if sim.winner != -1:
		return {"ok": false, "reason": "the match is already resolved"}
	if not sim.script_teams.has(team_name):
		return {"ok": false, "reason": "script team '%s' is not registered" % team_name}
	if not _script_owner_exists(destination_owner):
		return {
			"ok": false,
			"reason": "destination player %d is unavailable" % destination_owner,
		}
	if not sim._is_combatant_team(destination_owner):
		return {
			"ok": false,
			"reason": "destination player %d is not a combatant" % destination_owner,
		}
	var record := sim.script_teams[team_name] as Dictionary
	if bool(record.get("default", false)):
		return {
			"ok": false,
			"reason": "default team '%s' is outside the retail inheritance-team scope" % team_name,
		}
	if not bool(record.get("marker_only", false)):
		return {
			"ok": false,
			"reason": "team '%s' lacks source-attested marker-only evidence" % team_name,
		}
	if int(record.get("configured_owner", -1)) != sim.NEUTRAL_TEAM:
		return {
			"ok": false,
			"reason": "marker-only team '%s' was not configured under the civilian owner" % team_name,
		}
	if not (record.get("members", []) as Array).is_empty():
		return {
			"ok": false,
			"reason": "team '%s' has materialized members and requires entity transfer" % team_name,
		}
	record["owner"] = destination_owner
	sim.script_teams[team_name] = record
	return {"ok": true, "reason": ""}


func script_team_members(team_name: String, living_only: bool = true) -> Dictionary:
	if not sim.script_teams.has(team_name):
		return {"ok": false, "reason": "script team '%s' is not registered" % team_name}
	var record := sim.script_teams[team_name] as Dictionary
	var owner := int(record.get("owner", -1))
	var source: Array = []
	if (
		bool(record.get("default", false))
		and not bool(record.get("explicit_default_membership", false))
	):
		for id_value in sim.entity_ids():
			var entity_id := int(id_value)
			if int((sim.entities[entity_id] as Dictionary).get("team", -1)) == owner:
				source.append({"kind": "entity", "id": entity_id})
		for id_value in sim.structure_ids():
			var structure_id := int(id_value)
			if int((sim.structures[structure_id] as Dictionary).get("team", -1)) == owner:
				source.append({"kind": "structure", "id": structure_id})
	else:
		source = (record.get("members", []) as Array).duplicate(true)
	var answer: Array = []
	for handle_value in source:
		var handle := handle_value as Dictionary
		var kind := String(handle.get("kind", ""))
		var object_id := int(handle.get("id", -1))
		var row: Dictionary
		if kind == "entity" and sim.entities.has(object_id):
			row = sim.entities[object_id] as Dictionary
		elif kind == "structure" and sim.structures.has(object_id):
			row = sim.structures[object_id] as Dictionary
		else:
			continue
		if int(row.get("team", -1)) != owner:
			continue
		if living_only and int(row.get("health", 0)) <= 0:
			continue
		answer.append({"kind": kind, "id": object_id})
	answer.sort_custom(_script_member_less)
	return {
		"ok": true,
		"members": answer,
		"complete": not bool(record.get("membership_incomplete", false)),
		"unresolved_members": (record.get("unresolved_members", []) as Array).duplicate(),
		"unmodeled_object_count": int(record.get("unmodeled_object_count", 0)),
	}


func _script_team_definition(record: Dictionary) -> Dictionary:
	var configured_owner := int(record.get("configured_owner", record.get("owner", -1)))
	var definition := {
		"owner": configured_owner,
		"configured_owner": configured_owner,
	}
	for key in [
		"default",
		"explicit_default_membership",
		"membership_incomplete",
		"unresolved_members",
		"unmodeled_object_count",
		"marker_only",
	]:
		if record.has(key):
			var value: Variant = record[key]
			definition[key] = value.duplicate(true) if value is Array or value is Dictionary else value
	return definition


func set_script_team_recruitable(team_name: String, enabled: bool) -> Dictionary:
	if not sim.script_teams.has(team_name):
		return {"ok": false, "reason": "script team '%s' is not registered" % team_name}
	var record := sim.script_teams[team_name] as Dictionary
	# This is a tri-state override in retail: never-set, explicitly true, and
	# explicitly false are distinct. Team::tryToRecruit first checks whether
	# setRecruitable() was called, then lets that value override the default
	# team / prototype setting. Erasing false would silently turn a scripted
	# refusal back into the prototype default.
	record["recruitable"] = enabled
	sim.script_teams[team_name] = record
	return {"ok": true, "reason": ""}


func _script_team_state_view() -> Dictionary:
	## Team definitions are match configuration and are rebuilt when worlds are
	## installed. Only mutable membership/recruitable state crosses the dynamic
	## snapshot boundary. A changed controlling owner is mutable too. Default-
	## team aliases with none of these contribute zero
	## bytes, preserving the pristine-hash contract of merely binding a world.
	var view: Dictionary = {}
	var names = sim.script_teams.keys()
	names.sort()
	for name_value in names:
		var name := String(name_value)
		var record := sim.script_teams[name] as Dictionary
		if (
			record.has("members")
			or record.has("recruitable")
			or int(record.get("owner", -1))
			!= int(record.get("configured_owner", record.get("owner", -1)))
		):
			view[name] = record
	return view


# --- Team behavior state (TEAM_STATE + custom-state tokens, SIM-owned) ------
#
# The retail AI's own blackboard: the per-team state token TEAM_SET_STATE
# writes and TEAM_STATE_IS/_IS_NOT read, plus the custom-state token set
# TEAM_SET_CUSTOM_STATE toggles and TEAM_HAS_CUSTOM_STATE tests. The attack
# loops gate on these constantly (112 AI call sites across the six members).
#
# RETAIL SEMANTICS, SOURCED:
#   * TEAM_STATE is one plain string per team - Team.h:200 in the GPL
#     Generals/ZH reference declares `AsciiString m_state`, the BFME1
#     decompilation's Team.cpp matches it field-for-field, and
#     ScriptActions.cpp doSetTeamState is a bare setState(). No enum, no
#     validation, no case folding (AsciiString::operator== is strcmp), and no
#     engine consumer besides the two conditions - STORAGE IS THE ENTIRE
#     SEMANTIC. The default is the empty string (m_state is absent from the
#     Team constructor's initializer list), so a team never set IS in state ""
#     and TEAM_STATE_IS against any non-empty token is a truthful false.
#   * TEAM_STATE IS SAVE-PERSISTED: Team::xfer writes m_state right after the
#     member-id list (BFME decomp Team.cpp:2677, identical in ZH). Mutable by
#     script action plus save-file membership puts it inside the
#     snapshot/hash boundary, the sim.script_object_type_lists rule.
#   * CUSTOM STATES: both engine source trees carry only the metadata (action
#     id 490, parameter types [TEAM, TEAM_STATE, BOOLEAN]; condition id 143);
#     the BFME implementation is not decompiled. The SET reading below -
#     enabled inserts the token, disabled removes it, HAS is membership, a
#     never-set token is false - is the inference the signature forces
#     (retail authors the same token with both booleans: AI_ADVANCING is
#     authored 34x enabled AND 34x disabled, independently of AI_ASSAULTING),
#     recorded as an ASSUMPTION. What would falsify it: the custom-state
#     handler in the retail BFME1 binary storing something other than a
#     per-team token set. Custom-state save persistence is likewise
#     unevidenced; outcome-bearing mutability alone puts it inside the
#     boundary here regardless.
#
# The token vocabulary is CONTENT-DEFINED (AI_ATTACKING, AI_DEFENDING,
# READY_TO_AMBUSH, ... and 46 distinct custom tokens in the retail AI
# libraries); nothing here validates tokens against a table, exactly like
# retail. Comparisons are exact and case-sensitive.
#
# CANONICAL FORM: a team's record holds "state" only when non-empty (setting
# "" returns to the default and drops the key - retail's default IS "") and
# "custom" only when tokens are enabled (sorted unique Array; disabling the
# last token drops the key). A team whose record empties loses its team key,
# so state returned to pristine values returns to the pristine hash exactly.
#
# HASH INERTNESS: participates in the authoritative state ONLY when non-empty
# (empty-is-absent, the sim.unpackable_bases discipline), so a match whose
# scripts never touch team state contributes zero bytes to state_hash() and
# the frozen cross-platform pin stands untouched. setup() clears it (match
# state, exactly like the OBJECT_TYPE_LIST stores).

## script team name -> {"state": String (present iff != ""),
## "custom": sorted unique Array[String] (present iff non-empty)}.
## See the block comment above. setup() clears it; hashed only when non-empty.


func set_team_behavior_state(team: String, token: String) -> Dictionary:
	## TEAM_SET_STATE: overwrite the team's single state string. Any token is
	## admitted, including ones no condition ever reads (retail validates
	## nothing). Setting "" IS meaningful - it returns the team to the retail
	## default - and canonically drops the key rather than storing "".
	if not sim.script_teams.has(team):
		return {"ok": false, "reason": "script team '%s' is not registered" % team}
	if token == "":
		_prune_team_behavior_key(team, "state")
		return {"ok": true, "reason": ""}
	var record: Dictionary = sim.team_behavior_states.get(team, {})
	record["state"] = token
	sim.team_behavior_states[team] = record
	return {"ok": true, "reason": ""}


func team_behavior_state(team: String) -> Dictionary:
	## The team's current state string. {"ok": true, "state": String} - "" for
	## a rostered team never set, which is retail's default, not a dodge.
	if not sim.script_teams.has(team):
		return {"ok": false, "reason": "script team '%s' is not registered" % team}
	return {
		"ok": true,
		"state": String((sim.team_behavior_states.get(team, {}) as Dictionary).get("state", "")),
	}


func set_team_custom_state(team: String, token: String, enabled: bool) -> Dictionary:
	## TEAM_SET_CUSTOM_STATE: enable inserts `token` into the team's set,
	## disable removes it. Duplicate enables and absent disables are
	## successful no-ops (set semantics - the assumption block above). An
	## empty token refuses: "" names nothing in the retail vocabulary and
	## would mint an unreachable membership entry.
	if not sim.script_teams.has(team):
		return {"ok": false, "reason": "script team '%s' is not registered" % team}
	if token == "":
		return {"ok": false, "reason": "empty custom-state token names nothing"}
	if enabled:
		var record: Dictionary = sim.team_behavior_states.get(team, {})
		var tokens: Array = record.get("custom", [])
		if not tokens.has(token):
			tokens.append(token)
			tokens.sort()
		record["custom"] = tokens
		sim.team_behavior_states[team] = record
		return {"ok": true, "reason": ""}
	if sim.team_behavior_states.has(team):
		var record: Dictionary = sim.team_behavior_states[team]
		var tokens: Array = record.get("custom", [])
		tokens.erase(token)
		if tokens.is_empty():
			_prune_team_behavior_key(team, "custom")
		else:
			record["custom"] = tokens
	return {"ok": true, "reason": ""}


func team_custom_states(team: String) -> Dictionary:
	## The team's enabled custom-state tokens, sorted, as a defensive copy.
	## {"ok": true, "tokens": Array} - empty for a team never toggled.
	if not sim.script_teams.has(team):
		return {"ok": false, "reason": "script team '%s' is not registered" % team}
	return {
		"ok": true,
		"tokens": ((sim.team_behavior_states.get(team, {}) as Dictionary).get("custom", []) as Array).duplicate(),
	}


func _prune_team_behavior_key(team: String, key: String) -> void:
	## Drop `key` from the team's record, and the record itself when it
	## empties - the canonical form the hash discipline requires (a lingering
	## empty record would be a hash-visible phantom, the e56a0d4 class).
	if not sim.team_behavior_states.has(team):
		return
	var record: Dictionary = sim.team_behavior_states[team]
	record.erase(key)
	if record.is_empty():
		sim.team_behavior_states.erase(team)


# --- Sequential scripts (SIM-owned, ScriptEngine m_sequentialScripts) ------
#
# Retail ScriptEngine holds a global vector of SequentialScript heads, one
# active head per team/object, with further scripts chained via
# m_nextScriptInSequence. TEAM_EXECUTE_SEQUENTIAL_SCRIPT appends;
# evaluateAndProgressAllSequentialScripts steps idle heads one action at a
# time with m_conditionTeam latched for <This Team>.
#
# Mapping:
#   * times_to_loop: -1 forever, 0 run once (no re-append), matching the
#     boolean facet (looping true/false). Finite counts >1 stay refused at
#     the handler (retail AI sites pass 0).
#   * current_instruction starts at -1; progress increments before execute.
#   * frames_to_wait: -1 none, 0 force progress, >0 countdown each tick.
#   * idle: set true at queue (doTeamStartSequentialScript groupIdle); order
#     verbs that take control of members call mark_team_sequential_busy.
#   * empty-is-absent for hash inertness on scriptless matches.
#
# Unit sequential scripts are not stored here (separate object-id key space).

## script team name -> Array of sequential entries (head + chained nexts).

const _SEQUENTIAL_SPIN_LIMIT := 32


func queue_team_sequential_script(
	script_team: String, script_name: String, times_to_loop: int
) -> Dictionary:
	## ScriptActions::doTeamStartSequentialScript. Requires the named script
	## to be loaded on at least one registered executor (loud refuse rather
	## than retail's silent no-op when findScriptByName fails).
	if not sim.script_teams.has(script_team):
		return {"ok": false, "reason": "script team '%s' is not registered" % script_team}
	if sim.winner != -1:
		return {"ok": false, "reason": "the match is already resolved"}
	if script_name.strip_edges() == "":
		return {"ok": false, "reason": "sequential script name is empty"}
	if _executor_for_script(script_name) == null:
		return {
			"ok": false,
			"reason": "script '%s' is not loaded on any registered executor" % script_name,
		}
	var owner := int((sim.script_teams[script_team] as Dictionary).get("owner", -1))
	if _script_owner_exists(owner):
		# Retail groupIdle so the sequential head can start immediately.
		sim.issue_stop(sim.living_ids(owner), owner)
	var entry := {
		"script_name": script_name,
		"times_to_loop": times_to_loop,
		"current_instruction": -1,
		"frames_to_wait": -1,
		"dont_advance": false,
		"idle": true,
	}
	var chain: Array = sim.sequential_script_queues.get(script_team, []) as Array
	chain.append(entry)
	sim.sequential_script_queues[script_team] = chain
	return {"ok": true, "reason": ""}


func clear_team_sequential_scripts(script_team: String) -> Dictionary:
	if not sim.script_teams.has(script_team):
		return {"ok": false, "reason": "script team '%s' is not registered" % script_team}
	sim.sequential_script_queues.erase(script_team)
	return {"ok": true, "reason": ""}


func mark_team_sequential_busy(script_team: String) -> void:
	## Order verbs that assign AI work clear sequential idle so further
	## instructions wait (retail ai/aigroup isIdle gate).
	if not sim.sequential_script_queues.has(script_team):
		return
	var chain: Array = sim.sequential_script_queues[script_team]
	if chain.is_empty():
		return
	var head := chain[0] as Dictionary
	head["idle"] = false
	chain[0] = head
	sim.sequential_script_queues[script_team] = chain


func mark_team_sequential_idle(script_team: String) -> void:
	if not sim.sequential_script_queues.has(script_team):
		return
	var chain: Array = sim.sequential_script_queues[script_team]
	if chain.is_empty():
		return
	var head := chain[0] as Dictionary
	head["idle"] = true
	chain[0] = head
	sim.sequential_script_queues[script_team] = chain


# --- Object status bits (script Object_STATUS vocabulary, SIM-owned) --------
#
# Retail ObjectStatus bits ride the object; scripts set/clear named bits via
# UNIT/TEAM_CHANGE_OBJECT_STATUS and read them via UNIT_HAS_OBJECT_STATUS /
# TEAM_ALL/SOME_HAVE_OBJECT_STATUS. Storage is a per-entity map of exact
# authored status names -> true. Absent name is false. Empty map is erased
# (empty-is-absent on the entity row) so scriptless matches keep frozen
# hashes. The bit vocabulary is content-defined; unknown names are still
# stored (retail admits authoring any token).


func set_entity_object_status(
	entity_id: int, status: String, enabled: bool
) -> Dictionary:
	if sim.winner != -1:
		return {"ok": false, "reason": "the match is already resolved"}
	if status.strip_edges() == "":
		return {"ok": false, "reason": "empty object status names nothing"}
	if not sim.entities.has(entity_id):
		return {"ok": false, "reason": "entity %d is not in the simulation" % entity_id}
	var row = sim.entities[entity_id] as Dictionary
	if int(row.get("health", 0)) <= 0:
		return {"ok": false, "reason": "entity %d is not living" % entity_id}
	var flags: Dictionary = row.get("object_status", {}) as Dictionary
	if enabled:
		flags[status] = true
		row["object_status"] = flags
	else:
		flags.erase(status)
		if flags.is_empty():
			row.erase("object_status")
		else:
			row["object_status"] = flags
	sim.entities[entity_id] = row
	return {"ok": true, "reason": ""}


func entity_has_object_status(entity_id: int, status: String) -> bool:
	if not sim.entities.has(entity_id) or status.strip_edges() == "":
		return false
	var row = sim.entities[entity_id] as Dictionary
	if int(row.get("health", 0)) <= 0:
		return false
	var flags: Dictionary = row.get("object_status", {}) as Dictionary
	return bool(flags.get(status, false))


func set_entities_object_status(
	target_entity_ids: Array, status: String, enabled: bool
) -> Dictionary:
	if sim.winner != -1:
		return {"ok": false, "reason": "the match is already resolved"}
	if status.strip_edges() == "":
		return {"ok": false, "reason": "empty object status names nothing"}
	for id_value in target_entity_ids:
		var result := set_entity_object_status(int(id_value), status, enabled)
		if not bool(result.get("ok", false)):
			return result
	return {"ok": true, "reason": ""}


# --- Production / AI build-loop control flags (script surface) -------------
#
# Retail AI toggles base construction, factories, auto-build, and per-type
# unit construction. Absence of an override means retail's default (enabled /
# speed 1.0). Non-default values are match state and hash-visible.

## object_type -> buildability enum int (content-defined). Empty = untouched.
## owner team id -> reference name -> script team name (TEAM_REF store).


func _production_controls_for(team: int) -> Dictionary:
	return sim.production_controls_by_team.get(team, {}) as Dictionary


func _set_production_control_flag(team: int, key: String, enabled: bool) -> Dictionary:
	if sim.winner != -1:
		return {"ok": false, "reason": "the match is already resolved"}
	if not _script_owner_exists(team):
		return {"ok": false, "reason": "team %d is unavailable" % team}
	var row: Dictionary = _production_controls_for(team).duplicate(true)
	# Default is enabled; store only explicit disables, erase on re-enable.
	if enabled:
		row.erase(key)
	else:
		row[key] = false
	if row.is_empty():
		sim.production_controls_by_team.erase(team)
	else:
		sim.production_controls_by_team[team] = row
	return {"ok": true, "reason": ""}


func set_auto_build_enabled(team: int, enabled: bool) -> Dictionary:
	return _set_production_control_flag(team, "auto_build", enabled)


func set_base_construction_enabled(team: int, enabled: bool) -> Dictionary:
	return _set_production_control_flag(team, "base_construction", enabled)


func set_factories_enabled(team: int, enabled: bool) -> Dictionary:
	return _set_production_control_flag(team, "factories", enabled)


func set_base_construction_speed(team: int, factor: float) -> Dictionary:
	if sim.winner != -1:
		return {"ok": false, "reason": "the match is already resolved"}
	if not _script_owner_exists(team):
		return {"ok": false, "reason": "team %d is unavailable" % team}
	if factor < 0.0:
		return {"ok": false, "reason": "construction speed factor cannot be negative"}
	var row: Dictionary = _production_controls_for(team).duplicate(true)
	if is_equal_approx(factor, 1.0):
		row.erase("base_construction_speed")
	else:
		row["base_construction_speed"] = factor
	if row.is_empty():
		sim.production_controls_by_team.erase(team)
	else:
		sim.production_controls_by_team[team] = row
	return {"ok": true, "reason": ""}


func set_unit_construction_enabled(
	team: int, object_type: String, enabled: bool
) -> Dictionary:
	if sim.winner != -1:
		return {"ok": false, "reason": "the match is already resolved"}
	if not _script_owner_exists(team):
		return {"ok": false, "reason": "team %d is unavailable" % team}
	if object_type.strip_edges() == "":
		return {"ok": false, "reason": "empty object type names nothing"}
	var row: Dictionary = _production_controls_for(team).duplicate(true)
	var unit_map: Dictionary = row.get("unit_construction", {}) as Dictionary
	var match_key := ""
	for key_value in unit_map.keys():
		if String(key_value).to_lower() == object_type.to_lower():
			match_key = String(key_value)
			break
	if match_key != "":
		unit_map.erase(match_key)
	if not enabled:
		unit_map[object_type] = false
	if unit_map.is_empty():
		row.erase("unit_construction")
	else:
		row["unit_construction"] = unit_map
	if row.is_empty():
		sim.production_controls_by_team.erase(team)
	else:
		sim.production_controls_by_team[team] = row
	return {"ok": true, "reason": ""}


func production_control_enabled(team: int, key: String) -> bool:
	## Default true when no override is stored.
	var row := _production_controls_for(team)
	if not row.has(key):
		return true
	return bool(row.get(key, true))


func unit_construction_enabled(team: int, object_type: String) -> bool:
	var row := _production_controls_for(team)
	var unit_map: Dictionary = row.get("unit_construction", {}) as Dictionary
	for key_value in unit_map.keys():
		if String(key_value).to_lower() == object_type.to_lower():
			return bool(unit_map[key_value])
	return true


func set_tech_buildability(object_type: String, buildability: int) -> Dictionary:
	if sim.winner != -1:
		return {"ok": false, "reason": "the match is already resolved"}
	if object_type.strip_edges() == "":
		return {"ok": false, "reason": "empty object type names nothing"}
	sim.tech_buildability[object_type] = buildability
	return {"ok": true, "reason": ""}


func has_prerequisite_to_build(team: int, object_type: String) -> Dictionary:
	## True when every authored prerequisite upgrade for the unit type is
	## completed for the team, or the type has no prerequisite map (fieldable
	## without tech). Unknown/unmapped types refuse rather than guessing.
	if not _script_owner_exists(team):
		return {"ok": false, "reason": "team %d is unavailable" % team}
	if object_type.strip_edges() == "":
		return {"ok": false, "reason": "empty object type names nothing"}
	var unit_type = sim.trainable_unit_type_for(team, object_type)
	if unit_type == "":
		# Fall back to casefold match against production-rule keys.
		for key_value in sim._unit_production_rules.keys():
			if String(key_value).to_lower() == object_type.to_lower():
				unit_type = String(key_value)
				break
	if unit_type == "" and not sim._unit_production_rules.has(object_type):
		return {
			"ok": false,
			"reason": (
				"object type '%s' is not a production rule this simulation models"
				% object_type
			),
		}
	if unit_type == "":
		unit_type = object_type
	var prereq_value: Variant = sim._unit_prerequisites.get(unit_type, {})
	var completed: Dictionary = sim.team_upgrades.get(team, {}) as Dictionary
	if typeof(prereq_value) == TYPE_STRING:
		var upgrade_id := String(prereq_value)
		if upgrade_id == "":
			return {"ok": true, "value": true}
		return {"ok": true, "value": completed.has(upgrade_id)}
	if typeof(prereq_value) != TYPE_DICTIONARY:
		return {"ok": true, "value": true}
	var by_producer: Dictionary = prereq_value
	if by_producer.is_empty():
		return {"ok": true, "value": true}
	# Trainable if ANY producer path has all its prerequisites completed.
	for producer_key in by_producer.keys():
		var reqs: Array = by_producer[producer_key] as Array
		var ok_path := true
		for req_value in reqs:
			if not completed.has(String(req_value)):
				ok_path = false
				break
		if ok_path:
			return {"ok": true, "value": true}
	return {"ok": true, "value": false}


func bind_script_team_reference(
	owner_team: int, reference: String, script_team: String
) -> Dictionary:
	if sim.winner != -1:
		return {"ok": false, "reason": "the match is already resolved"}
	if reference.strip_edges() == "":
		return {"ok": false, "reason": "empty team reference names nothing"}
	if not sim.script_teams.has(script_team):
		return {
			"ok": false,
			"reason": "script team '%s' is not registered" % script_team,
		}
	var table: Dictionary = sim.script_team_references.get(owner_team, {}) as Dictionary
	table[reference] = script_team
	sim.script_team_references[owner_team] = table
	return {"ok": true, "reason": ""}


func script_team_reference(owner_team: int, reference: String) -> String:
	var table: Dictionary = sim.script_team_references.get(owner_team, {}) as Dictionary
	return String(table.get(reference, ""))


func set_entity_stopping_distance(entity_id: int, distance: float) -> Dictionary:
	if sim.winner != -1:
		return {"ok": false, "reason": "the match is already resolved"}
	if not sim.entities.has(entity_id):
		return {"ok": false, "reason": "entity %d is not in the simulation" % entity_id}
	if distance < 0.0:
		return {"ok": false, "reason": "stopping distance cannot be negative"}
	var row = sim.entities[entity_id] as Dictionary
	if is_equal_approx(distance, 0.0):
		row.erase("stopping_distance")
	else:
		row["stopping_distance"] = distance
	sim.entities[entity_id] = row
	return {"ok": true, "reason": ""}


func set_entities_idle_until(target_entity_ids: Array, until_tick: int) -> Dictionary:
	if sim.winner != -1:
		return {"ok": false, "reason": "the match is already resolved"}
	for id_value in target_entity_ids:
		var entity_id := int(id_value)
		if not sim.entities.has(entity_id):
			continue
		var row = sim.entities[entity_id] as Dictionary
		if int(row.get("health", 0)) <= 0:
			continue
		row["script_idle_until"] = until_tick
		row["state"] = "idle"
		row.erase("target_id")
		sim._clear_pending_route(row, true)
		sim.entities[entity_id] = row
	return {"ok": true, "reason": ""}


func set_entities_spin_until(target_entity_ids: Array, until_tick: int) -> Dictionary:
	if sim.winner != -1:
		return {"ok": false, "reason": "the match is already resolved"}
	for id_value in target_entity_ids:
		var entity_id := int(id_value)
		if not sim.entities.has(entity_id):
			continue
		var row = sim.entities[entity_id] as Dictionary
		if int(row.get("health", 0)) <= 0:
			continue
		row["script_spin_until"] = until_tick
		row["state"] = "idle"
		sim.entities[entity_id] = row
	return {"ok": true, "reason": ""}


func issue_hunt(ids: Array[int], team: int = sim.PLAYER_TEAM) -> int:
	## TEAM/NAMED/PLAYER_HUNT with empty command button: aggressive stance and
	## clear hold so the existing acquire loop hunts hostiles.
	var count := 0
	for id_value in ids:
		var entity_id := int(id_value)
		if not sim.entities.has(entity_id):
			continue
		var row = sim.entities[entity_id] as Dictionary
		if int(row.get("team", -1)) != team or int(row.get("health", 0)) <= 0:
			continue
		row["stance"] = "Aggressive"
		row.erase("script_idle_until")
		sim.entities[entity_id] = row
		count += 1
	sim.issue_set_stance(ids, "Aggressive", team)
	return count



# --- Bulk script surface stores (flags, progression, sim.containment, events) ---
#
# These back high-volume script facet methods with exact authored keys and
# empty-is-absent defaults. They do not invent pathfinding, area geometry, or
# presentation. Combat effects of flags (stealth vision, etc.) remain for
# later subsystems; storage and script readback are truthful.



func _player_prog(team: int) -> Dictionary:
	return sim.player_progression.get(team, {}) as Dictionary


func _set_player_prog_value(team: int, key: String, value: Variant) -> Dictionary:
	if sim.winner != -1:
		return {"ok": false, "reason": "the match is already resolved"}
	if not _script_owner_exists(team):
		return {"ok": false, "reason": "team %d is unavailable" % team}
	var row: Dictionary = _player_prog(team).duplicate(true)
	row[key] = value
	sim.player_progression[team] = row
	return {"ok": true, "reason": ""}


func set_diplomacy_override(from_team: int, to_team: int, relation: int) -> Dictionary:
	## Hash-backed relation override (player/team script diplomacy).
	if sim.winner != -1:
		return {"ok": false, "reason": "the match is already resolved"}
	if not _script_owner_exists(from_team):
		return {"ok": false, "reason": "from team unavailable"}
	var table: Dictionary = sim.player_diplomacy_overrides.get(from_team, {}) as Dictionary
	table[to_team] = relation
	sim.player_diplomacy_overrides[from_team] = table
	return {"ok": true, "reason": ""}


func clear_diplomacy_override(from_team: int, to_team: int) -> Dictionary:
	if not sim.player_diplomacy_overrides.has(from_team):
		return {"ok": true, "reason": ""}
	var table: Dictionary = sim.player_diplomacy_overrides[from_team]
	table.erase(to_team)
	if table.is_empty():
		sim.player_diplomacy_overrides.erase(from_team)
	else:
		sim.player_diplomacy_overrides[from_team] = table
	return {"ok": true, "reason": ""}


func clear_all_diplomacy_overrides(from_team: int) -> Dictionary:
	sim.player_diplomacy_overrides.erase(from_team)
	return {"ok": true, "reason": ""}


func set_attack_priority_entry(
	set_name: String, target_kind: String, target_name: String, priority: int
) -> void:
	## Hash-backed attack-priority table (script AI vocabulary).
	if not sim.match_script_flags.has("attack_priority_sets"):
		sim.match_script_flags["attack_priority_sets"] = {}
	var sets: Dictionary = sim.match_script_flags["attack_priority_sets"]
	var set_row: Dictionary = sets.get(set_name, {}) as Dictionary
	set_row["%s:%s" % [target_kind, target_name]] = priority
	sets[set_name] = set_row
	sim.match_script_flags["attack_priority_sets"] = sets


func set_default_attack_priority_entry(set_name: String, priority: int) -> void:
	if not sim.match_script_flags.has("attack_priority_defaults"):
		sim.match_script_flags["attack_priority_defaults"] = {}
	var defaults: Dictionary = sim.match_script_flags["attack_priority_defaults"]
	defaults[set_name] = priority
	sim.match_script_flags["attack_priority_defaults"] = defaults


func set_team_ai_priority(team: int, priority: int) -> Dictionary:
	if sim.winner != -1:
		return {"ok": false, "reason": "the match is already resolved"}
	if not _script_owner_exists(team):
		return {"ok": false, "reason": "team unavailable"}
	if not sim.match_script_flags.has("team_ai_priority"):
		sim.match_script_flags["team_ai_priority"] = {}
	var table: Dictionary = sim.match_script_flags["team_ai_priority"]
	table[team] = priority
	sim.match_script_flags["team_ai_priority"] = table
	return {"ok": true, "reason": ""}


func adjust_team_ai_priority(team: int, delta: int) -> Dictionary:
	var table: Dictionary = sim.match_script_flags.get("team_ai_priority", {}) as Dictionary
	var current := int(table.get(team, 0))
	return set_team_ai_priority(team, current + delta)


func _add_player_prog_value(team: int, key: String, delta: int) -> Dictionary:
	var row := _player_prog(team)
	var current := int(row.get(key, 0))
	return _set_player_prog_value(team, key, current + delta)


func set_entity_bool_flag(entity_id: int, flag: String, enabled: bool) -> Dictionary:
	if sim.winner != -1:
		return {"ok": false, "reason": "the match is already resolved"}
	if not sim.entities.has(entity_id):
		return {"ok": false, "reason": "entity %d missing" % entity_id}
	var row = sim.entities[entity_id] as Dictionary
	var flags: Dictionary = row.get("script_bool_flags", {}) as Dictionary
	if enabled:
		flags[flag] = true
		row["script_bool_flags"] = flags
	else:
		flags.erase(flag)
		if flags.is_empty():
			row.erase("script_bool_flags")
		else:
			row["script_bool_flags"] = flags
	sim.entities[entity_id] = row
	return {"ok": true, "reason": ""}


func entity_bool_flag(entity_id: int, flag: String) -> bool:
	if not sim.entities.has(entity_id):
		return false
	var flags: Dictionary = (sim.entities[entity_id] as Dictionary).get("script_bool_flags", {})
	return bool(flags.get(flag, false))


func set_entities_bool_flag(target_entity_ids: Array, flag: String, enabled: bool) -> Dictionary:
	for id_value in target_entity_ids:
		var result := set_entity_bool_flag(int(id_value), flag, enabled)
		if not bool(result.get("ok", false)):
			return result
	return {"ok": true, "reason": ""}


func set_entity_string_state(entity_id: int, key: String, value: String) -> Dictionary:
	if sim.winner != -1:
		return {"ok": false, "reason": "the match is already resolved"}
	if not sim.entities.has(entity_id):
		return {"ok": false, "reason": "entity %d missing" % entity_id}
	var row = sim.entities[entity_id] as Dictionary
	var store: Dictionary = row.get("script_string_state", {}) as Dictionary
	if value == "":
		store.erase(key)
	else:
		store[key] = value
	if store.is_empty():
		row.erase("script_string_state")
	else:
		row["script_string_state"] = store
	sim.entities[entity_id] = row
	return {"ok": true, "reason": ""}


func entity_string_state(entity_id: int, key: String) -> String:
	if not sim.entities.has(entity_id):
		return ""
	var store: Dictionary = (sim.entities[entity_id] as Dictionary).get("script_string_state", {})
	return String(store.get(key, ""))


func set_entity_timed_flag(entity_id: int, flag: String, until_tick: int) -> Dictionary:
	if sim.winner != -1:
		return {"ok": false, "reason": "the match is already resolved"}
	if not sim.entities.has(entity_id):
		return {"ok": false, "reason": "entity %d missing" % entity_id}
	var row = sim.entities[entity_id] as Dictionary
	var store: Dictionary = row.get("script_timed_flags", {}) as Dictionary
	if until_tick < 0:
		store.erase(flag)
	else:
		store[flag] = until_tick
	if store.is_empty():
		row.erase("script_timed_flags")
	else:
		row["script_timed_flags"] = store
	sim.entities[entity_id] = row
	return {"ok": true, "reason": ""}


func entity_timed_flag_active(entity_id: int, flag: String) -> bool:
	if not sim.entities.has(entity_id):
		return false
	var store: Dictionary = (sim.entities[entity_id] as Dictionary).get("script_timed_flags", {})
	if not store.has(flag):
		return false
	return sim.tick_index <= int(store[flag])


func script_set_health_percent(entity_id: int, percent: float) -> Dictionary:
	if sim.winner != -1:
		return {"ok": false, "reason": "the match is already resolved"}
	if not sim.entities.has(entity_id):
		return {"ok": false, "reason": "entity %d missing" % entity_id}
	var row = sim.entities[entity_id] as Dictionary
	var maximum := maxi(1, int(row.get("maximum_health", row.get("health", 1))))
	var clamped := clampf(percent, 0.0, 100.0)
	var new_health := int(round(maximum * clamped / 100.0))
	row["health"] = new_health
	var defeated_members: Array[int] = []
	if row.has("member_health") and row["member_health"] is Array:
		var members: Array = row["member_health"]
		if not members.is_empty():
			var each := int(new_health / members.size())
			var rebuilt: Array = []
			for member_index in members.size():
				if int(members[member_index]) > 0 and each <= 0:
					defeated_members.append(member_index)
				rebuilt.append(each)
			row["member_health"] = rebuilt
	sim.entities[entity_id] = row
	if new_health <= 0:
		var death_policy = sim._bookkeep_battalion_death(
			entity_id, row, "NORMAL", defeated_members
		)
		if bool(row.get("is_banner_carrier", false)):
			sim._on_banner_carrier_defeated(row)
		sim._emit_event("battalion.defeated", 0, entity_id, {"reason": "script-kill"})
		if bool(death_policy.get("destroy_object", false)) or bool(row.get("is_banner_carrier", false)):
			sim.entities.erase(entity_id)
	return {"ok": true, "reason": ""}


func script_kill_entity(entity_id: int) -> Dictionary:
	return script_set_health_percent(entity_id, 0.0)


func script_damage_entity(entity_id: int, amount: float) -> Dictionary:
	if not sim.entities.has(entity_id):
		return {"ok": false, "reason": "entity %d missing" % entity_id}
	var row = sim.entities[entity_id] as Dictionary
	var maximum := maxi(1, int(row.get("maximum_health", 1)))
	var health := int(row.get("health", 0))
	var pct := 100.0 * float(health) / float(maximum)
	var damage_pct := 100.0 * amount / float(maximum)
	return script_set_health_percent(entity_id, pct - damage_pct)


func contain_entity(structure_id: int, entity_id: int) -> Dictionary:
	if sim.winner != -1:
		return {"ok": false, "reason": "the match is already resolved"}
	if not sim.structures.has(structure_id) and not sim.entities.has(structure_id):
		return {"ok": false, "reason": "container %d missing" % structure_id}
	if not sim.entities.has(entity_id):
		return {"ok": false, "reason": "entity %d missing" % entity_id}
	if sim.entity_container.has(entity_id):
		return {"ok": false, "reason": "entity already contained"}
	var passengers: Array = sim.containment.get(structure_id, []) as Array
	passengers.append(entity_id)
	sim.containment[structure_id] = passengers
	sim.entity_container[entity_id] = structure_id
	return {"ok": true, "reason": ""}


func exit_entity_container(entity_id: int) -> Dictionary:
	if not sim.entity_container.has(entity_id):
		return {"ok": true, "reason": ""}  # vacuous
	var structure_id := int(sim.entity_container[entity_id])
	var passengers: Array = sim.containment.get(structure_id, []) as Array
	passengers.erase(entity_id)
	if passengers.is_empty():
		sim.containment.erase(structure_id)
	else:
		sim.containment[structure_id] = passengers
	sim.entity_container.erase(entity_id)
	return {"ok": true, "reason": ""}


func passenger_count(structure_id: int) -> int:
	return (sim.containment.get(structure_id, []) as Array).size()


func register_script_area(name: String, center: Vector2, radius: float) -> void:
	sim.script_areas[name] = {"center": center, "radius": radius, "impassable": false}


func register_script_waypoint(name: String, position: Vector2) -> void:
	sim.script_waypoints[name] = position


func register_script_waypoint_path(name: String, points: Array) -> void:
	sim.script_waypoint_paths[name] = points.duplicate()


func area_contains(name: String, position: Vector2) -> Dictionary:
	if not sim.script_areas.has(name):
		return {"ok": false, "reason": "area '%s' is not registered" % name}
	var area: Dictionary = sim.script_areas[name]
	var center: Vector2 = area.get("center", Vector2.ZERO)
	var radius := float(area.get("radius", 0.0))
	return {"ok": true, "value": center.distance_to(position) <= radius}


func bump_script_event(key: String, amount: int = 1) -> void:
	sim.script_event_counts[key] = int(sim.script_event_counts.get(key, 0)) + amount


func script_event_count(key: String) -> int:
	return int(sim.script_event_counts.get(key, 0))


func mark_team_created(script_team: String) -> void:
	sim.team_created_edge[script_team] = true


func team_created_is_set(script_team: String) -> bool:
	## Retail Team::isCreated is cleared by Team::updateState once per frame,
	## not by the condition read itself. Probe/hash-safe: reading does not
	## mutate. Clear edges in _step_script_executors after scripts run.
	return bool(sim.team_created_edge.get(script_team, false))


func clear_team_created_edges() -> void:
	sim.team_created_edge.clear()


func set_entity_team(entity_id: int, new_team: int) -> Dictionary:
	if sim.winner != -1:
		return {"ok": false, "reason": "the match is already resolved"}
	if not sim.entities.has(entity_id):
		return {"ok": false, "reason": "entity %d missing" % entity_id}
	if not _script_owner_exists(new_team):
		return {"ok": false, "reason": "team %d unavailable" % new_team}
	var row = sim.entities[entity_id] as Dictionary
	var old_team := int(row.get("team", -1))
	if old_team == new_team:
		return {"ok": true, "reason": ""}
	if sim.base_loop_enabled and not bool(row.get("command_points_released", false)):
		var commitment = sim._entity_command_point_commitment(row)
		sim.team_command_points[old_team] = maxi(0, sim.command_points_for_team(old_team) - commitment)
		sim.team_command_points[new_team] = sim.command_points_for_team(new_team) + commitment
	row["team"] = new_team
	sim.entities[entity_id] = row
	return {"ok": true, "reason": ""}


func delete_entity(entity_id: int) -> Dictionary:
	if sim.winner != -1:
		return {"ok": false, "reason": "the match is already resolved"}
	if not sim.entities.has(entity_id):
		return {"ok": false, "reason": "entity %d missing" % entity_id}
	exit_entity_container(entity_id)
	var row = sim.entities[entity_id] as Dictionary
	sim._summon_despawn_ticks.erase(entity_id)
	sim._summon_aura_source_ids.erase(entity_id)
	sim.selected_ids.erase(entity_id)
	sim._release_command_points_once(row)
	sim.entities.erase(entity_id)
	sim.prune_control_groups()
	return {"ok": true, "reason": ""}



func _executor_for_script(script_name: String) -> SageScriptExecutor:
	for team_key in _sorted_dictionary_keys(sim._script_executors):
		var executor: SageScriptExecutor = (
			sim._script_executors[team_key] as WeakRef
		).get_ref()
		if executor != null and executor.has_script(script_name):
			return executor
	return null


func _step_sequential_scripts() -> void:
	## ScriptEngine::evaluateAndProgressAllSequentialScripts, bounded to team
	## heads. Runs after ordinary executor.tick() so newly queued scripts from
	## this frame's AI scripts can start the same logic frame (retail steps
	## sequential scripts in the same ScriptEngine::update).
	if sim.sequential_script_queues.is_empty() or sim.winner != -1:
		return
	var team_names = sim.sequential_script_queues.keys()
	team_names.sort()
	for team_name_value in team_names:
		var script_team := String(team_name_value)
		var spin := 0
		while spin < _SEQUENTIAL_SPIN_LIMIT:
			spin += 1
			if not sim.sequential_script_queues.has(script_team):
				break
			var chain: Array = sim.sequential_script_queues[script_team]
			if chain.is_empty():
				sim.sequential_script_queues.erase(script_team)
				break
			var head := (chain[0] as Dictionary).duplicate(true)
			var frames := int(head.get("frames_to_wait", -1))
			if frames > 0:
				head["frames_to_wait"] = frames - 1
				chain[0] = head
				sim.sequential_script_queues[script_team] = chain
				break
			var can_progress := bool(head.get("idle", false)) or frames == 0
			if not can_progress:
				break
			if bool(head.get("dont_advance", false)):
				head["dont_advance"] = false
			else:
				head["current_instruction"] = int(head.get("current_instruction", -1)) + 1
			var script_name := String(head.get("script_name", ""))
			var executor := _executor_for_script(script_name)
			if executor == null:
				# Script unloaded or executors dropped; fail closed by clearing.
				sim.sequential_script_queues.erase(script_team)
				break
			var actions_result: Dictionary = executor.true_actions_for_script(script_name)
			if not bool(actions_result.get("ok", false)):
				sim.sequential_script_queues.erase(script_team)
				break
			var actions: Array = actions_result.get("actions", []) as Array
			var instruction := int(head.get("current_instruction", 0))
			if instruction < 0 or instruction >= actions.size():
				# Finished the action list. Re-append when looping.
				var times := int(head.get("times_to_loop", 0))
				chain.remove_at(0)
				if times != 0:
					var requeue := {
						"script_name": script_name,
						"times_to_loop": times if times < 0 else times - 1,
						"current_instruction": -1,
						"frames_to_wait": -1,
						"dont_advance": false,
						"idle": true,
					}
					chain.append(requeue)
				if chain.is_empty():
					sim.sequential_script_queues.erase(script_team)
				else:
					sim.sequential_script_queues[script_team] = chain
				# Allow the next chained script to start this frame.
				continue
			var action: Dictionary = actions[instruction]
			head["frames_to_wait"] = -1
			chain[0] = head
			sim.sequential_script_queues[script_team] = chain
			var world: RetailSliceScriptWorld = executor.world as RetailSliceScriptWorld
			if world != null:
				world.latch_script_team_context(script_team, true)
			executor.execute_action_record(action, script_name)
			if world != null:
				world.clear_script_team_context()
			# Re-read head: the action may have stopped/queued/busy-marked.
			if not sim.sequential_script_queues.has(script_team):
				break
			chain = sim.sequential_script_queues[script_team]
			if chain.is_empty():
				sim.sequential_script_queues.erase(script_team)
				break
			head = chain[0] as Dictionary
			if bool(head.get("dont_advance", false)):
				break
			if not bool(head.get("idle", false)):
				break
			# Still idle: retail allows another instruction this frame.


# --- Logic random stream (SIM-owned, retail GameLogic generator) ------------
#
# The deterministic random stream retail script actions draw from
# (SET_RANDOM_COUNTER / SET_RANDOM_TIMER / SET_RANDOM_MSEC_TIMER /
# SET_RANDOM_COUNTER_IN_SECONDS). This is retail's LOGIC stream, not its
# client stream: RandomValue.cpp keeps three independent generators
# (theGameLogicSeed / theGameClientSeed / theGameAudioSeed) precisely so the
# GameLogic "remains deterministic, regardless of the effects displayed on
# the GameClient" (RandomValue.cpp:138-142), and the script engine's random
# actions call GameLogicRandomValue (ScriptEngine.cpp setTimer, lines
# 6746-6760 in the GPL Zero Hour source) - the logic stream. The client
# stream stays refused here (SET_COUNTER_TO_CLIENT_RANDOM_VALUE is a
# DELIBERATE gap): it is desync-prone by design.
#
# ONE GLOBAL STREAM, NOT PER-PLAYER - retail's shape. theGameLogicSeed is a
# single static array; every logic draw by every subsystem and every script
# player advances the same sequence, and the script engine passes no player
# context into GameLogicRandomValue. Draw ORDER is therefore part of the
# contract: draws happen only inside script/handler execution, which the sim
# steps in ascending team order (register_script_executor's guarantee), so
# every peer interleaves draws identically.
#
# THE GENERATOR IS RETAIL'S, TRANSCRIBED, NOT APPROXIMATED: the Michael
# Booth (Jan 1998) lagged add-with-carry over six 32-bit words from the GPL
# Generals/Zero Hour RandomValue.cpp (randomValue/seedRandom), which the
# BFME1 binary still exports (GetGameLogicRandomValueReal thunk in the
# decompilation's masm dumps, same signature). Two deliberate fidelity
# points, verbatim from retail even where a clean-room design would differ:
#   * the ADC carry is retail's `C = (SUM < A) || (SUM < B)` on the WRAPPED
#     sum - which misses a true carry in the a=b=0xFFFFFFFF,c=1 edge. Bit
#     identity with retail beats mathematical tidiness.
#   * the range map is retail's biased modulo (delta = hi-lo+1 as uint32;
#     delta==0 answers hi WITHOUT consuming a draw; otherwise one draw,
#     `draw % delta + lo`, inclusive of BOTH bounds). The modulo bias is
#     <= delta/2^32 - immaterial for script ranges like [1..3], and matching
#     retail's mapping exactly matters more than uniformity.
#
# CROSS-PLATFORM BIT-IDENTITY: integer arithmetic only, every value masked
# to 32 bits, all intermediates far below 2^63 - GDScript's int is 64-bit
# signed on every platform, so no operation here can overflow or vary.
# Deliberately NOT Godot's RandomNumberGenerator/randi(): their algorithm is
# an engine implementation detail with no cross-version output guarantee.
# This section IS the specification - it can be re-implemented identically
# from this file alone (and was, in Python, to mint the pinned vectors).
#
# SEEDING is match configuration: sim._rules["logic_random_seed"] (absent means
# 0), read at first draw. Rules are agreed match configuration on every peer
# and a hashed static key, so a disagreeing seed diverges the state hash
# immediately. Retail seeds the same way - InitGameLogicRandom(getSeed())
# from the lobby-shared game seed (LANAPICallbacks.cpp:267,
# SkirmishGameOptionsMenu.cpp:434); a lobby-varied seed is a follow-up that
# only needs to set this rules key.
#
# STATE AND HASH INERTNESS: the six words ARE the entire stream state (the
# draw count is not needed to continue the sequence). They live in
# sim._logic_random_state, empty until the first draw (lazy seeding), hashed and
# snapshotted empty-is-absent - a match that never draws contributes ZERO
# bytes, so the frozen cross-platform pin stands untouched. setup() clears
# the stream (match state); a peer adopting a mid-match snapshot receives
# the words and continues the identical sequence.

const _U32 := 0xFFFFFFFF

## The six 32-bit words of the logic stream; [] until the first draw (the
## empty-is-absent form). See the block comment above. setup() clears it.

## Process-local draw tally, DIAGNOSTIC only (like frame_conversions): an
## adopting peer reports its own draws, not the minter's. Never hashed.


static func _logic_random_seed_words(seed_value: int) -> Array:
	## Retail seedRandom() with the incremental constant additions telescoped:
	## after step k the accumulator is exactly SEED + constant_k (mod 2^32).
	var seed32 := seed_value & _U32
	return [
		(seed32 + 0xF22D0E56) & _U32,
		(seed32 + 0x883126E9) & _U32,
		(seed32 + 0xC624DD2F) & _U32,
		(seed32 + 0x0702C49C) & _U32,
		(seed32 + 0x9E353F7D) & _U32,
		(seed32 + 0x6FDF3B64) & _U32,
	]


static func _logic_random_draw32(words: Array) -> int:
	## One raw 32-bit draw, mutating `words` in place: retail randomValue() -
	## five chained ADCs from words[5] down to words[0] (each ADC uses
	## retail's carry rule on the wrapped sum), then the increment cascade
	## that bubbles a +1 up from words[5], bumping the RETURN VALUE too when
	## it reaches words[0].
	var w0 := int(words[0])
	var w1 := int(words[1])
	var w2 := int(words[2])
	var w3 := int(words[3])
	var w4 := int(words[4])
	var w5 := int(words[5])
	var carry := 0
	var ax := (w5 + w4 + carry) & _U32
	carry = 1 if (ax < w5 or ax < w4) else 0
	w4 = ax
	var prev := ax
	ax = (prev + w3 + carry) & _U32
	carry = 1 if (ax < prev or ax < w3) else 0
	w3 = ax
	prev = ax
	ax = (prev + w2 + carry) & _U32
	carry = 1 if (ax < prev or ax < w2) else 0
	w2 = ax
	prev = ax
	ax = (prev + w1 + carry) & _U32
	carry = 1 if (ax < prev or ax < w1) else 0
	w1 = ax
	prev = ax
	ax = (prev + w0 + carry) & _U32
	w0 = ax
	w5 = (w5 + 1) & _U32
	if w5 == 0:
		w4 = (w4 + 1) & _U32
		if w4 == 0:
			w3 = (w3 + 1) & _U32
			if w3 == 0:
				w2 = (w2 + 1) & _U32
				if w2 == 0:
					w1 = (w1 + 1) & _U32
					if w1 == 0:
						w0 = (w0 + 1) & _U32
						ax = (ax + 1) & _U32
	words[0] = w0
	words[1] = w1
	words[2] = w2
	words[3] = w3
	words[4] = w4
	words[5] = w5
	return ax


func logic_random_int(low: int, high: int) -> int:
	## Retail GetGameLogicRandomValue(lo, hi): inclusive of BOTH bounds.
	## delta = hi - lo + 1 as uint32; delta == 0 (hi == lo - 1 mod 2^32)
	## answers hi without consuming a draw; low == high consumes a draw and
	## answers low (delta 1) - retail does both, and stream POSITION is
	## contract, so neither shortcut may be "optimized". The unsigned draw is
	## reinterpreted as int32 and the final sum wrapped to int32, matching
	## retail's x86 Int arithmetic on the (unreachable-by-authored-scripts)
	## degenerate ranges too.
	if sim._logic_random_state.is_empty():
		sim._logic_random_state = _logic_random_seed_words(int(sim._rules.get("logic_random_seed", 0)))
	var delta := (high - low + 1) & _U32
	if delta == 0:
		return high
	sim.logic_random_draws += 1
	var drawn := _logic_random_draw32(sim._logic_random_state) % delta
	if drawn >= 0x80000000:
		drawn -= 0x100000000
	return ((drawn + low + 0x80000000) & _U32) - 0x80000000


func logic_random_real(low: float, high: float) -> float:
	## Retail GetGameLogicRandomValueReal: unlike the integer helper this always
	## consumes one logic draw, including a 0..1 roll tested against 100%.
	if sim._logic_random_state.is_empty():
		sim._logic_random_state = _logic_random_seed_words(int(sim._rules.get("logic_random_seed", 0)))
	sim.logic_random_draws += 1
	var unit := float(_logic_random_draw32(sim._logic_random_state)) / 4294967295.0
	return low + (high - low) * unit


# --- Script-engine environment state (SageScriptEnv, SIM-owned) -------------
#
# The script interpreter's own mutable memory - counters, flags, timers,
# per-script enable bits and its tick counter. This is the LARGEST instance of
# the e56a0d4 defect class: FLAG/SET_FLAG/COUNTER/ENABLE_SCRIPT account for
# 64.7% of all retail-AI call sites, every one a read or write of exactly this
# state, mutated mid-match by script actions. Two peers whose counters diverge
# run completely different AI while their sim hashes agree - unless the state
# lives HERE, inside the snapshot/hash boundary.
#
# Keyed by TEAM (the script player's), like sim.script_unit_references: retail
# runs each AI player's script libraries in that player's own environment
# (SageScriptEnv's own doc: "Counter and flag namespaces are global across all
# loaded scripts, which matches the retail per-player script environment").
#
# HOW THE ENV REACHES IT: attach_script_env(env, team) hands the env this
# Dictionary BY REFERENCE (see SageScriptEnv.attach_state_store); the env then
# reads and writes sim.script_env_state[team] directly. No object reference in
# either direction, so no RefCounted cycle, and the env keeps working with no
# sim at all (its standalone backing) - tests and the bare executor construct
# it that way. BECAUSE the reference is shared, setup() and restore() must
# mutate this dictionary IN PLACE (clear()/merge()), never rebind the
# variable, or every attached env would silently keep writing to an orphan.
#
# WHAT EACH FIELD IS, decided deliberately:
#   * counters/flags/timers/script_enabled - STATE (script actions write them,
#     later conditions read them; the retail save persists the script engine).
#   * "tick" - the interpreter's tick counter, STATE. It anchors every timer
#     (a timer is {"remaining" ticks, decremented once per env.advance()}) and
#     phases interval-gated scripts (tick % interval). It is deliberately NOT
#     aliased to the sim's own sim.tick_index: the executor<->sim tick cadence is
#     not production-wired yet, and aliasing would bake in an unenforced
#     "sim.step() exactly once before executor.tick()" contract whose
#     violation would silently double- or zero-advance timers. Both clocks are
#     authoritative state in the same snapshot, so they cannot drift APART
#     between peers - a mid-match adopter inherits the interpreter tick with
#     the timers anchored to it and expires them on the same absolute tick as
#     the peer that armed them.
#   * env.ticks_per_second / retail_frames_per_second - CONFIGURATION (never
#     mutated mid-match; every tick count they produce lands in hashed timer
#     state, so a misconfigured peer diverges visibly on first use).
#   * env.frame_conversions - DIAGNOSTIC, process-local (nothing in script
#     logic reads it; hashing observability would let a non-outcome counter
#     desync a match).
#
# CANONICAL FORM AND HASH INERTNESS: the raw store is the env's working
# memory; _script_env_state_view() below is what gets hashed and serialized.
# The view prunes zero counters and false flags (indistinguishable from
# absent by every read: counter() defaults 0, flag() defaults false), keeps
# every explicit script_enabled bit (absent means "authored default", so
# false is NOT absent), keeps every timer (an unset timer answers
# timer_expired false; a set one answers from "remaining"), drops tick 0,
# empty collections, and empty team entries, and sorts every level - so an
# untouched match contributes ZERO bytes (the frozen pin stands), and state
# that returns to pristine values returns to the pristine hash EXACTLY.
# Enforcing this at the boundary (one choke point) rather than in every
# mutator also keeps direct dictionary writes - which tests use - canonical.
# Pruning applies ONLY to fields the view understands: an unrecognised field
# is carried verbatim and reported loudly, never silently dropped (see
# _script_env_state_view).

## team id -> {"tick": int, "counters": {}, "flags": {}, "timers": {},
## "script_enabled": {}}. See the block comment above. setup() clears it;
## hashed/serialized through _script_env_state_view (empty-is-absent).

## The team-entry fields (and timer-row fields) _script_env_state_view()
## understands. Anything else in the store is a boundary violation: it is
## reported loudly (once per field) and carried VERBATIM into the hash and
## snapshot, so state added without teaching the view can never silently
## escape the boundary (0dce37e review: it used to be invisible to the hash
## and dropped on peer adoption).
const SCRIPT_ENV_VIEW_FIELDS: Array[String] = [
	"tick", "counters", "flags", "timers", "script_enabled",
]
const SCRIPT_ENV_TIMER_FIELDS: Array[String] = ["remaining", "running"]

## Diagnostic count of unrecognised script-env fields the view has carried.
## Process-local observability like sim.script_wiring_faults, never hashed.
## "team|path" keys already reported, so a persistent stray field is loud
## once instead of once per hash. Cleared by setup() with the store.


func attach_script_env(env: SageScriptEnv, team: int) -> bool:
	## Route `env`'s state through sim.script_env_state[team] (see above). Refuses
	## loudly for a null env or an unrostered team; SageScriptEnv itself
	## refuses an env that is already attached or already holds local state.
	if env == null:
		push_error("attach_script_env refused: null env")
		return false
	if not _script_owner_exists(team):
		push_error("attach_script_env refused: team %d is not a script-capable owner" % team)
		return false
	# This sim is the store's lifetime witness: if it is freed while the env
	# lives on, the env's store becomes an orphan outside every hash and
	# snapshot, and the env must refuse loudly instead of writing into it.
	return env.attach_state_store(sim.script_env_state, team, sim)


func _script_env_state_view() -> Dictionary:
	## Canonical, pruned, sorted copy of sim.script_env_state for state_hash() and
	## snapshot() - the boundary choke point described in the block comment.
	##
	## FAIL LOUD, NEVER PRUNE THE UNKNOWN: pruning applies only to the fields
	## this view UNDERSTANDS (SCRIPT_ENV_VIEW_FIELDS / the timer-row pair). A
	## field it does not recognise is carried VERBATIM into the view - so it
	## reaches the hash, the snapshot and every adopting peer - and reported
	## loudly once (_report_script_env_view_fault). The 0dce37e review proved
	## the previous whitelist silently dropped such a field from both the hash
	## and the snapshot: a collection added to the env without updating this
	## view would have been invisible to the desync barrier and lost on peer
	## adoption, the exact silent-fallback class e56a0d4 closed.
	var view := {}
	var team_keys = sim.script_env_state.keys()
	team_keys.sort()
	for team_key in team_keys:
		var entry: Dictionary = sim.script_env_state[team_key]
		var entry_view := {}
		var tick := int(entry.get("tick", 0))
		if tick != 0:
			entry_view["tick"] = tick
		var counters: Dictionary = entry.get("counters", {})
		var counters_view := {}
		for name in _sorted_dictionary_keys(counters):
			var count := int(counters[name])
			if count != 0:
				counters_view[name] = count
		if not counters_view.is_empty():
			entry_view["counters"] = counters_view
		var flags: Dictionary = entry.get("flags", {})
		var flags_view := {}
		for name in _sorted_dictionary_keys(flags):
			if bool(flags[name]):
				flags_view[name] = true
		if not flags_view.is_empty():
			entry_view["flags"] = flags_view
		var timers: Dictionary = entry.get("timers", {})
		var timers_view := {}
		for name in _sorted_dictionary_keys(timers):
			var timer: Dictionary = timers[name]
			var timer_view := {
				"remaining": float(timer.get("remaining", 0.0)),
				"running": bool(timer.get("running", false)),
			}
			for field in _sorted_dictionary_keys(timer):
				if SCRIPT_ENV_TIMER_FIELDS.has(field):
					continue
				_report_script_env_view_fault(
					team_key, "timers/%s/%s" % [str(name), str(field)]
				)
				timer_view[field] = timer[field]
			timers_view[name] = timer_view
		if not timers_view.is_empty():
			entry_view["timers"] = timers_view
		var enabled: Dictionary = entry.get("script_enabled", {})
		var enabled_view := {}
		for name in _sorted_dictionary_keys(enabled):
			enabled_view[name] = bool(enabled[name])
		if not enabled_view.is_empty():
			entry_view["script_enabled"] = enabled_view
		for field in _sorted_dictionary_keys(entry):
			if SCRIPT_ENV_VIEW_FIELDS.has(field):
				continue
			_report_script_env_view_fault(team_key, str(field))
			entry_view[field] = entry[field]
		if not entry_view.is_empty():
			view[team_key] = entry_view
	return view


func _report_script_env_view_fault(team_key: Variant, path: String) -> void:
	## Loud once per (team, field), like SageScriptEnv._report_stale: the first
	## sighting is the defect report; one per hash call would bury the log.
	var report_key := "%s|%s" % [str(team_key), path]
	if sim._script_env_view_reported.has(report_key):
		return
	sim._script_env_view_reported[report_key] = true
	sim.script_env_view_faults += 1
	push_error(
		(
			"script env state: team %s carries unrecognised field '%s'; "
			+ "_script_env_state_view does not understand it, so it is carried "
			+ "VERBATIM into the hash and snapshot rather than silently dropped - "
			+ "teach the view (SCRIPT_ENV_VIEW_FIELDS) about it"
		) % [str(team_key), path]
	)


func _sorted_dictionary_keys(source: Dictionary) -> Array:
	var keys := source.keys()
	keys.sort()
	return keys


# --- Script executors wired into the match loop (the production seam) -------
#
# THE SEAM. Registered SageScriptExecutors are stepped by tick() itself -
# _step_script_executors() below - NOT by the vertical slice's frame loop or
# the lockstep session. The sim is the only object every driving path (single
# player _process, lockstep advance_if_ready, control-server save/load, every
# test runner) already funnels through, so putting the step inside tick()
# makes the cadence contract STRUCTURAL: no caller can double-step or skip
# the script engine without also double-stepping or skipping the simulation.
#
# THE TICK-ORDERING CONTRACT, exact and enforced:
#
#   Each registered executor ticks EXACTLY ONCE per gameplay-advancing sim
#   tick, in ascending team order, after that tick's commands are applied
#   and before any gameplay subsystem (economy, production, AI controllers,
#   entity stepping) runs. Ticks in which gameplay is frozen (clock paused,
#   match decided) step NO scripts.
#
# "After commands, before gameplay" means a script evaluating on tick N sees
# the world exactly as tick N-1 left it plus tick N's player commands, and
# every mutation it makes is visible to all of tick N's gameplay - the same
# slot the SAGE script engine occupies at the top of the logic frame.
# 87cf636 deliberately refused to alias the env clock to the sim clock
# because this contract was unenforced then; it is enforced now, two ways:
#
#   * STRUCTURALLY: the only production call site of executor.tick() is
#     inside sim.tick(), behind the same pause/sim.winner gates as gameplay.
#   * MECHANICALLY: the sim tracks, per executor, the interpreter-tick value
#     it last left that executor's env at (seeded from the env's hashed
#     clock at registration, so an adopting peer derives the minter's
#     value). Every step checks the env clock against that expectation
#     BEFORE ticking (catches an out-of-band executor.tick() by any other
#     caller) and re-checks AFTER (catches an executor whose tick advanced
#     the clock by anything but 1). NOTE the expectation is a tracked value,
#     not a constant offset from sim.tick_index: decided-match and lockstep-
#     paused ticks advance the sim clock while deliberately stepping no
#     scripts, so the two clocks legitimately drift APART across frozen
#     ticks - what must never happen is the ENV clock moving except under
#     this function. A violation quarantines the executor loudly -
#     push_error naming team, expected and actual, sim.script_wiring_faults
#     incremented, no further steps - rather than silently re-syncing,
#     because by then the hashed env tick has already diverged from every
#     correct peer and hiding it would be a silent desync. Both clocks ride
#     the snapshot, so the hash barrier catches whatever the quarantine
#     reports.
#
# WIRING, NOT STATE. The registration table is process-local plumbing like
# frame_conversions: script BODIES are match configuration (identical bytes
# on every peer, from the content pack), and everything the scripts DO lands
# in sim.script_env_state / the sim's own hashed rows. Registrations are held by
# WEAKREF - the match owner (vertical slice, test runner) keeps the executor
# alive - so sim -> executor -> world -> sim never forms a RefCounted cycle.
# A registration whose executor was freed is reported loudly and dropped,
# never skipped silently.
#
# INERT BY DEFAULT. With no registered executor _step_script_executors()
# returns before touching anything, no env is attached, and no script state
# key exists - a scriptless match is bit-identical to one built before this
# seam existed, which the frozen b177804c pin proves on every run.

## team id -> WeakRef of the SageScriptExecutor running that team's scripts.
## team id -> the env interpreter tick _step_script_executors last left that
## executor at. Seeded from the env's hashed clock at registration and rebased
## by setup()/restore() from the same hashed values every peer holds, so the
## expectation is derived, deterministic, and identical on every peer.
## team id -> true once quarantined by a cadence fault. Cleared by setup().
## Diagnostic count of wiring faults (freed executor, stale env, cadence
## violation). Process-local observability, never hashed.


func register_script_executor(executor: SageScriptExecutor, team: int) -> bool:
	## Wire `executor` to run team `team`'s scripts inside tick(). Refuses
	## loudly rather than guessing: null executors, unrostered teams, a team
	## that already has a live executor, and - the choke point that makes env
	## lifetime detection airtight - an executor whose env is not attached to
	## THIS sim's store UNDER THIS TEAM (attach_script_env(env, team) first).
	## Both halves of that check are load-bearing: an env attached to another
	## sim would run scripts against state this sim never hashes, and an env
	## attached to this sim under a DIFFERENT team would run in team `team`'s
	## step slot while writing the other team's state key - the 0dce37e review
	## registered a team-0 env under team 1 (and a swapped PAIR) and both were
	## accepted, silently inverting the ascending-team-order guarantee with
	## zero faults.
	if executor == null:
		push_error("register_script_executor refused: null executor")
		return false
	if not _script_owner_exists(team):
		push_error("register_script_executor refused: team %d is not a script-capable owner" % team)
		return false
	if sim._script_executors.has(team) and (sim._script_executors[team] as WeakRef).get_ref() != null:
		push_error("register_script_executor refused: team %d already has a registered executor" % team)
		return false
	if executor.env == null or not executor.env.attached_to(sim):
		push_error(
			"register_script_executor refused: the executor's env is not attached "
			+ "to this sim's state store (call attach_script_env(executor.env, %d) first)" % team
		)
		return false
	var env_key: Variant = executor.env.attachment_key()
	if typeof(env_key) != TYPE_INT or int(env_key) != team:
		push_error(
			(
				"register_script_executor refused: the executor's env is attached "
				+ "under state-store key %s, not registration team %d - stepping it "
				+ "in team %d's slot would run its scripts against another team's "
				+ "hashed state (attach_script_env(executor.env, %d) first)"
			) % [str(env_key), team, team, team]
		)
		return false
	sim._script_executors[team] = weakref(executor)
	sim._script_executor_expected_ticks[team] = executor.env.tick_index
	sim._script_executor_faults.erase(team)
	return true


func unregister_script_executor(team: int) -> bool:
	if not sim._script_executors.has(team):
		return false
	sim._script_executors.erase(team)
	sim._script_executor_expected_ticks.erase(team)
	sim._script_executor_faults.erase(team)
	return true


func registered_script_executor_teams() -> Array:
	return _sorted_dictionary_keys(sim._script_executors)


func _step_script_executors() -> void:
	## The contract's enforcement point - see the block comment above.
	if sim._script_executors.is_empty():
		return
	for team_key in _sorted_dictionary_keys(sim._script_executors):
		var executor_ref: WeakRef = sim._script_executors[team_key]
		var executor: SageScriptExecutor = executor_ref.get_ref()
		if executor == null:
			sim.script_wiring_faults += 1
			push_error(
				"script wiring: the executor registered for team %s was freed while "
				% str(team_key)
				+ "registered; dropping the registration - its scripts stop HERE, loudly"
			)
			sim._script_executors.erase(team_key)
			sim._script_executor_expected_ticks.erase(team_key)
			sim._script_executor_faults.erase(team_key)
			continue
		if sim._script_executor_faults.get(team_key, false):
			continue  # quarantined; the fault was reported once when it happened
		if executor.env.attachment_stale():
			_quarantine_script_executor(team_key, "its env's backing store owner was freed")
			continue
		var expected := int(sim._script_executor_expected_ticks[team_key])
		var before := executor.env.tick_index
		if before != expected:
			_quarantine_script_executor(
				team_key,
				"cadence violation before the step: env tick %d, expected %d - something ticked this executor outside sim.tick()"
				% [before, expected]
			)
			continue
		executor.tick()
		if executor.env.tick_index != before + 1:
			_quarantine_script_executor(
				team_key,
				"cadence violation during the step: one executor tick moved the env clock %d -> %d (must be exactly +1)"
				% [before, executor.env.tick_index]
			)
			continue
		sim._script_executor_expected_ticks[team_key] = before + 1
	# Sequential heads progress after ordinary script evaluation so the same
	# logic frame can both queue (TEAM_EXECUTE_SEQUENTIAL_*) and advance.
	_step_sequential_scripts()
	# Retail clears Team::m_created after the script pass (updateState).
	clear_team_created_edges()


func _quarantine_script_executor(team_key: Variant, reason: String) -> void:
	sim.script_wiring_faults += 1
	sim._script_executor_faults[team_key] = true
	push_error(
		"script wiring: quarantining team %s's executor - %s. Its scripts no "
		% [str(team_key), reason]
		+ "longer run; the hashed interpreter clock already carries the divergence."
	)


func _rebase_script_executor_offsets() -> void:
	## The expected env clock is DERIVED wiring: the interpreter tick is
	## hashed state, so whenever it moves out-of-band-but-legitimately
	## (setup() zeroes it, restore() sets it from a snapshot) the expectation
	## is recomputed from the same value every peer holds.
	for team_key in sim._script_executors.keys():
		var executor: SageScriptExecutor = (sim._script_executors[team_key] as WeakRef).get_ref()
		if executor != null and not executor.env.attachment_stale():
			sim._script_executor_expected_ticks[team_key] = executor.env.tick_index


# --- Retail object-type identity (derived reads over existing hashed rows) --
#
# Counting and nearest-object queries by RETAIL object-type name. No new sim
# state: every identity consulted here already lives inside the hash/snapshot
# boundary - entity rows carry their compiled rule's provenance
# (retail_rule_provenance.source_object_id, the document's retail objectId)
# and their runtime ids (unit_type, a deterministic slug of the retail
# container name); structure rows carry structure_kind, resolved through the
# team manifest's producer_kind_registry (retail source object id -> kind,
# part of the hashed rules/config) plus the expansion build rules; creep camps
# carry their retail type_name verbatim. Matching is therefore EXACT string
# identity over recorded facts, never a heuristic: retail names fold case
# (SAGE INI object lookups are case-insensitive), runtime ids compare exactly.
#
# KNOWN LIMIT, recorded rather than papered over: rows whose identity was
# never recorded cannot be matched. That is (a) the legacy tiny-pack's
# hand-written synthetic ids where the id was not derived from the retail
# name by the standard slug (the tower guard: "gondor-tower-guard" vs retail
# GondorTowerShieldGuardHorde), and (b) creep GUARD battalions, whose ids are
# synthetic creep-family keys. Pack-driven content - the shipping path -
# records provenance on every unit rule, so its censuses are exact.


func count_objects_of_types(team: int, type_names: Array, include_dead: bool) -> int:
	## Census of `team`'s objects (battalion rows AND structure rows) whose
	## retail type matches any name in `type_names`. STRICTLY READ-ONLY: this
	## backs the retail AI's highest-traffic condition
	## (PLAYER_HAS_OBJECT_COMPARISON) and conditions are evaluated an
	## unpredictable number of times.
	##
	## `include_dead` counts rows regardless of health - rows that still
	## EXIST. Structure rows persist after razing; battalion rows persist
	## until corpse expiry (CORPSE_LIFETIME_TICKS), after which retail has
	## deleted the object too. Living-only is the default reading.
	##
	## The count is exact over the enumerable object census: every countable
	## row's identity is recorded (see the block comment), so a name matching
	## zero rows is a true zero about THIS match, not a guess - a type the
	## simulation cannot field has no instances here by construction.
	var probe := _object_type_probe(type_names)
	var total := 0
	for id in sim.entity_ids():
		var row: Dictionary = sim.entities[id]
		if int(row.get("team", -1)) != team:
			continue
		if not include_dead and int(row.get("health", 0)) <= 0:
			continue
		if _entity_matches_types(row, probe):
			total += 1
	for structure_id in sim.structure_ids(team):
		var row: Dictionary = sim.structures[structure_id]
		if not include_dead and int(row.get("health", 0)) <= 0:
			continue
		if _structure_matches_types(row, probe):
			total += 1
	return total


func living_object_levels_of_types(team: int, type_names: Array) -> Array[int]:
	## Current rank of every LIVING battalion or structure owned by `team`
	## whose recorded retail identity matches `type_names`. This is a derived,
	## read-only view over the same authoritative rows and exact matcher used
	## by count_objects_of_types; it adds no veterancy cache or history.
	var probe := _object_type_probe(type_names)
	var levels: Array[int] = []
	for id in sim.entity_ids():
		var row: Dictionary = sim.entities[id]
		if (
			int(row.get("team", -1)) == team
			and int(row.get("health", 0)) > 0
			and _entity_matches_types(row, probe)
		):
			levels.append(int(row.get("level", 1)))
	for structure_id in sim.structure_ids(team):
		var row: Dictionary = sim.structures[structure_id]
		if int(row.get("health", 0)) > 0 and _structure_matches_types(row, probe):
			levels.append(int(row.get("level", 1)))
	return levels


func nearest_object_of_types(origin: Vector2, type_names: Array, owner_teams: Array) -> Dictionary:
	## Nearest LIVING object (battalion or structure) whose retail type
	## matches any of `type_names`, owned by any team in `owner_teams` (empty
	## = any owner, creep and neutral rows included). Read-only.
	##
	## DETERMINISM: candidates are visited in sorted id order and the sim.winner
	## is the minimum under an exact TOTAL order - strictly-less squared
	## distance, ties to battalions before sim.structures (the two id spaces may
	## overlap numerically), then to the LOWEST id. Never is_equal_approx: a
	## tolerance comparison is not transitive and cannot define a total order.
	## Answers {"found": false} or {"found": true, "kind": "battalion"|
	## "structure", "id": int, "position": Vector2}.
	var probe := _object_type_probe(type_names)
	var owner_filter := {}
	for team_value in owner_teams:
		owner_filter[int(team_value)] = true
	var found := false
	var best_id := 0
	var best_rank := 0
	var best_distance := 0.0
	var best_position := Vector2.ZERO
	for id in sim.entity_ids():
		var row: Dictionary = sim.entities[id]
		if int(row.get("health", 0)) <= 0:
			continue
		if not owner_filter.is_empty() and not owner_filter.has(int(row.get("team", -1))):
			continue
		if not _entity_matches_types(row, probe):
			continue
		var distance := origin.distance_squared_to(Vector2(row.get("position", Vector2.ZERO)))
		var wins := not found or distance < best_distance
		if not wins and distance == best_distance:
			wins = 0 < best_rank or (best_rank == 0 and id < best_id)
		if wins:
			found = true
			best_id = id
			best_rank = 0
			best_distance = distance
			best_position = Vector2(row.get("position", Vector2.ZERO))
	for structure_id in sim.structure_ids():
		var row: Dictionary = sim.structures[structure_id]
		if int(row.get("health", 0)) <= 0:
			continue
		if not owner_filter.is_empty() and not owner_filter.has(int(row.get("team", -1))):
			continue
		if not _structure_matches_types(row, probe):
			continue
		var distance := origin.distance_squared_to(Vector2(row.get("position", Vector2.ZERO)))
		var wins := not found or distance < best_distance
		if not wins and distance == best_distance:
			wins = 1 < best_rank or (best_rank == 1 and structure_id < best_id)
		if wins:
			found = true
			best_id = structure_id
			best_rank = 1
			best_distance = distance
			best_position = Vector2(row.get("position", Vector2.ZERO))
	if not found:
		return {"found": false}
	return {
		"found": true,
		"kind": "battalion" if best_rank == 0 else "structure",
		"id": best_id,
		"position": best_position,
	}


func fieldable_object_type(name: String) -> bool:
	## Whether THIS simulation could ever field an object of the retail type
	## `name` - derived from match configuration only (unit rules, per-team
	## manifests, expansion rules, creep families), so it is identical on
	## every peer and never moves with match state. Callers use it to
	## distinguish "zero of a type this match can express" (a truthful no-op)
	## from "a type outside this simulation's model entirely" (a refusal that
	## keeps the modeling gap visible - the retail AI's tactical-marker moves
	## land there). Order-independent: a pure any() over configuration sets.
	if name == "":
		return false
	var folded := name.to_lower()
	var runtime_id = sim.PlayableUnitAdapter.runtime_object_id(name)
	var unit_rules: Dictionary = sim._rules.get("unit_rules", {}) as Dictionary
	for object_id_value in unit_rules.keys():
		var rule: Dictionary = unit_rules[object_id_value] as Dictionary
		var source := String((rule.get("provenance", {}) as Dictionary).get("source_object_id", ""))
		if source != "" and source.to_lower() == folded:
			return true
		if String(object_id_value) == runtime_id or String(rule.get("horde_id", "")) == runtime_id:
			return true
	for team_value in sim._roster_team_ids():
		var team := int(team_value)
		var manifest = sim.team_manifest_for(team)
		var registry: Dictionary = _structure_source_registry(manifest)
		for source_value in registry.keys():
			if String(source_value).to_lower() == folded:
				return true
		for object_id_value in (manifest.get("structure_object_ids", {}) as Dictionary).values():
			if String(object_id_value) == runtime_id:
				return true
		if sim.unit_production_rules_for_team(team).has(runtime_id):
			return true
	for kind_value in sim._expansion_build_rules.keys():
		var expansion_object_id = String((sim._expansion_build_rules[kind_value] as Dictionary).get("object_id", ""))
		if expansion_object_id == runtime_id or expansion_object_id.to_lower() == folded:
			return true
	for registry_key in ["scenario_unit_runtimes", "scenario_structure_runtimes"]:
		for object_id_value in (sim._rules.get(registry_key, {}) as Dictionary).keys():
			if String(object_id_value).to_lower() == folded:
				return true
	return false


func _object_type_probe(type_names: Array) -> Dictionary:
	## Per-query matching keys: case-folded retail names, their derived
	## runtime ids, and a lazily filled per-team structure-kind cache.
	var folded := {}
	var runtime_ids := {}
	for name_value in type_names:
		var name := String(name_value)
		if name == "":
			continue
		folded[name.to_lower()] = true
		runtime_ids[sim.PlayableUnitAdapter.runtime_object_id(name)] = true
	return {"folded": folded, "runtime_ids": runtime_ids, "kinds_by_team": {}}


func _entity_matches_types(row: Dictionary, probe: Dictionary) -> bool:
	## A battalion row matches on its recorded provenance (the retail source
	## object id, authoritative) or on its runtime container id (unit_type is
	## the deterministic slug of the retail container name for every
	## pack-driven rule). The MEMBER id (row.object_id) deliberately does not
	## match: a row is ONE retail horde object, and counting the member name
	## as the horde would answer 1 where retail counts 15 members - the exact
	## granularity lie the class comment forbids.
	var provenance: Dictionary = row.get("retail_rule_provenance", {}) as Dictionary
	var source := String(provenance.get("source_object_id", ""))
	if source != "" and (probe["folded"] as Dictionary).has(source.to_lower()):
		return true
	return (probe["runtime_ids"] as Dictionary).has(String(row.get("unit_type", "")))


func _structure_matches_types(row: Dictionary, probe: Dictionary) -> bool:
	## A structure row matches through its team's kind registry (retail source
	## object id -> structure kind), the manifest/expansion runtime ids, or its
	## descriptor-backed scenario source identity.
	var scenario_type := String(row.get("scenario_source_object_id", row.get("source_object_id", "")))
	if scenario_type != "" and (probe["folded"] as Dictionary).has(scenario_type.to_lower()):
		return true
	var team := int(row.get("team", -1))
	var kinds_by_team: Dictionary = probe["kinds_by_team"]
	if not kinds_by_team.has(team):
		kinds_by_team[team] = _structure_kinds_matching_probe(team, probe)
	return (kinds_by_team[team] as Dictionary).has(String(row.get("structure_kind", "")))


func _structure_kinds_matching_probe(team: int, probe: Dictionary) -> Dictionary:
	## The set of structure kinds (for `team`'s manifest) that the probe's
	## names denote. Built as a SET, so source-dictionary iteration order
	## cannot affect any answer.
	var kinds := {}
	var folded: Dictionary = probe["folded"]
	var runtime_ids: Dictionary = probe["runtime_ids"]
	var manifest = sim.team_manifest_for(team)
	var registry := _structure_source_registry(manifest)
	for source_value in registry.keys():
		if folded.has(String(source_value).to_lower()):
			kinds[String(registry[source_value])] = true
	for kind_value in (manifest.get("structure_object_ids", {}) as Dictionary).keys():
		if runtime_ids.has(String((manifest.get("structure_object_ids", {}) as Dictionary)[kind_value])):
			kinds[String(kind_value)] = true
	for kind_value in sim._expansion_build_rules.keys():
		# Expansion rules record either the runtime id (the vertical slice's
		# doc-driven path) or a plain source-style name (synthetic fixtures);
		# both compare exactly against their own key form.
		var expansion_object_id = String((sim._expansion_build_rules[kind_value] as Dictionary).get("object_id", ""))
		if runtime_ids.has(expansion_object_id) or folded.has(expansion_object_id.to_lower()):
			kinds[String(kind_value)] = true
	return kinds


func _structure_source_registry(manifest: Dictionary) -> Dictionary:
	## retail structure source object id -> structure kind, from the team's
	## manifest; the vertical slice's global registry is the fallback for the
	## legacy manifest-free rules shape.
	var registry: Variant = manifest.get("producer_kind_registry")
	if typeof(registry) == TYPE_DICTIONARY and not (registry as Dictionary).is_empty():
		return registry as Dictionary
	return sim._rules.get("producer_kind_by_source_object", {}) as Dictionary


