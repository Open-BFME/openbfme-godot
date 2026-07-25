class_name RetailMapScripts
extends RefCounted

## Deterministic bounded interpreter for decoded SAGE WorldBuilder map scripts.
##
## Input format: the ``openbfme.map-scripts`` document produced by the importer
## converter ``importer/openbfme_importer/sage_scripts.py`` (schemaVersion 1),
## or, for fixtures and the older skirmish contract extractor, the bare decoded
## Script payload dictionaries that document embeds under ``scripts[i].payload``.
##
## Execution model: call step(sim) once per sim tick after sim.tick(). Every
## active, non-subroutine script whose evaluation interval is due evaluates its
## condition blocks (OrCondition records are OR'd; Condition records inside one
## block are AND'd), then executes its ScriptAction records on true or its
## ScriptActionFalse records on false. CALL_SUBROUTINE runs a named subroutine
## script inline, bounded by MAX_SUBROUTINE_DEPTH and MAX_SUBROUTINE_CALLS.
##
## Determinism: interpreter state (counters, flags, timers, object lists,
## script activity, objective state, the event log) is a pure function of the
## loaded document and the tick index. SET_RANDOM_COUNTER draws from an
## internal splitmix64 stream seeded from the document, never from
## RandomNumberGenerator. Nothing allocates without a bound: the event log,
## object lists, and subroutine recursion are all capped and overflow is
## counted, not silently dropped.
##
## Fail-closed accounting, three buckets, recorded at load time for every
## opcode occurrence and again for every occurrence actually reached:
##   * ``implemented``  - opcodes with real interpreter/simulation semantics.
##   * ``recorded``     - retail presentation opcodes (UI, audio, camera,
##                        objectives, win/lose). These emit a deterministic
##                        event and change no simulation state. They are NEVER
##                        counted as gameplay coverage.
##   * ``unimplemented``- everything else. An unimplemented condition makes its
##                        AND-block false regardless of inversion; an
##                        unimplemented action does nothing. Both are counted.
##
## World state: the converter's `world` section seeds four registries at load
## time - named objects, teams, waypoints (with resolved paths) and trigger
## areas. Named objects and teams are live simulation state: objects are born
## alive, leave the world through NAMED_DELETE or a bound entity's death, carry
## an attitude and an order, and move between teams; teams own a roster and
## answer TEAM_HAS_UNITS / TEAM_DESTROYED over every authored object on them.
## Registry rows bind to simulation entities through bind_named_object() and,
## for the mission's authored starting objects, through
## instantiate_world_objects(); until a row is bound, orders aimed at it are
## retained on the row and counted in `deferred_orders` rather than being faked.
##
## Scripts also create objects (B4). CREATE_NAMED_ON_TEAM_AT_WAYPOINT,
## CREATE_UNNAMED_ON_TEAM_AT_WAYPOINT and CREATE_REINFORCEMENT_TEAM - the last
## instantiating a Teams entry's authored unit composition - all add real
## registry rows on a team, budgeted apart from the authored ones, and offer
## each one to the simulation. An object whose retail type the loaded content
## cannot produce stays registry-only and its type is recorded in
## `unavailable_object_types`; nothing is ever substituted for it.
##
## Those rows are what makes the trigger-area predicates (B3) answerable: the
## spatial index is the union of unbound registry rows and live simulation
## entities on the queried player's team, and it is walked in the stable name
## order under a per-tick query budget. See the B3 section for exactly what it
## can and cannot see.
##
## Campaign pacing gates on audio (B6). HAS_FINISHED_AUDIO is answered from
## authored audio-event lengths and the tick index, never from playback state -
## see the B6 section - so the presentation bucket stays a pure sink and the
## proof that it cannot perturb lockstep still holds.
##
## Shroud is per player. The MAP_REVEAL_PERMANENTLY_* family files named reveals
## a matching undo removes, and NAMED_DISCOVERED / TEAM_DISCOVERED answer over
## the union of those reveals and the sight of the player's living entities.
## The non-permanent reveal family needs an explored-but-not-visible layer this
## world does not have and is deliberately left unimplemented.
##
## SEMANTIC_CONDITIONS / SEMANTIC_ACTIONS / RECORDED_ACTIONS below are mirrored
## by SEMANTIC_CONDITION_OPCODES / SEMANTIC_ACTION_OPCODES /
## RECORDED_ACTION_OPCODES in sage_scripts.py, and
## importer/tests/test_sage_scripts.py parses this file to assert they are
## identical. The converter's coverage census therefore cannot claim support
## this interpreter does not have.

# Matches RetailSliceSim.TICK_SECONDS (10 logic ticks per second).
const TICK_SECONDS := 0.1

# SAGE comparison enum used by the COUNTER condition's argumentType-6 slot.
const COMPARE_LESS := 0
const COMPARE_LESS_EQUAL := 1
const COMPARE_EQUAL := 2
const COMPARE_GREATER_EQUAL := 3
const COMPARE_GREATER := 4
const COMPARE_NOT_EQUAL := 5

# Decoded argument type codes, each observed in the retail campaign corpus.
const ARGUMENT_INTEGER := 0
const ARGUMENT_REAL := 1
const ARGUMENT_SCRIPT_NAME := 2
const ARGUMENT_TEAM := 3
const ARGUMENT_COUNTER_NAME := 4
const ARGUMENT_FLAG_NAME := 5
const ARGUMENT_COMPARISON := 6
const ARGUMENT_WAYPOINT := 7
const ARGUMENT_BOOLEAN := 8
const ARGUMENT_TRIGGER_AREA := 9
const ARGUMENT_PLAYER := 11
const ARGUMENT_SOUND := 12
const ARGUMENT_SUBROUTINE := 13
const ARGUMENT_UNIT_NAME := 14
const ARGUMENT_OBJECT_TYPE := 15
const ARGUMENT_POSITION := 16
const ARGUMENT_DIALOG := 21
const ARGUMENT_WAYPOINT_PATH := 24
const ARGUMENT_LOCALIZED_STRING := 25
const ARGUMENT_CAMERA_WAYPOINT := 51
const ARGUMENT_OBJECT_LIST := 48
const ARGUMENT_NOTIFICATION_KIND := 77
const ARGUMENT_ATTITUDE := 20
# Presentation argument slots, each read from the retail campaign corpus rather
# than guessed: the opcode's authored signature is what names the slot.
const ARGUMENT_MUSIC_TRACK := 22
const ARGUMENT_MOVIE := 23
const ARGUMENT_RADAR_EVENT_TYPE := 30
const ARGUMENT_SCREEN_SHAKE_INTENSITY := 38
const ARGUMENT_COMMAND_BUTTON := 39
# Slot 49 is the handle a permanent map reveal is filed under; the matching
# MAP_UNDO_REVEAL_PERMANENTLY_* action takes only that handle.
const ARGUMENT_REVEAL_NAME := 49
# Slot 59 is an audio *event* name (as opposed to slot 12, a sound instance):
# AUDIO_MAKE_SOUND_IMMUNE_TO_FADE and SOUND_DISABLE_TYPE both address one.
const ARGUMENT_SOUND_TYPE := 59
const ARGUMENT_REVERB_ROOM_TYPE := 60
const ARGUMENT_HERO_BUTTON := 62
const ARGUMENT_CAMERA_ANIMATION := 64
# Distinct from ARGUMENT_OBJECT_TYPE (15). Slot 61 is the "object type or
# object-type list" argument the trigger-area comparison conditions and the
# attack-priority actions take; the retail corpus authors both a bare type name
# ("GondorFarm") and an object-list name ("Troll LIST") through it.
const ARGUMENT_OBJECT_TYPE_OR_LIST := 61
# Third slot of TEAM_INSIDE_AREA_PARTIALLY / TEAM_INSIDE_AREA_ENTIRELY. The
# retail campaign authors it 0 in 181 of 188 slots and 1 or 3 in the remaining
# seven; we have no evidence for what the non-zero values select, so only 0 is
# admitted and anything else refuses the predicate (see _team_area_filter_ok).
const ARGUMENT_AREA_MEMBER_FILTER := 37
const AREA_MEMBER_FILTER_ALL := 0

# SAGE AttitudeType. Only the five values the shared SAGE core is known to
# define are named here; the retail BFME2 corpus also authors -3 (12 slots
# across the campaign), whose meaning we have no evidence for. Attitudes are
# stored as the authored integer either way, and an unmapped value increments
# bounds_hit["attitude_unmapped"] so it can never be silently read as one of
# the known five.
const ATTITUDE_SLEEP := -2
const ATTITUDE_PASSIVE := -1
const ATTITUDE_NORMAL := 0
const ATTITUDE_ALERT := 1
const ATTITUDE_AGGRESSIVE := 2
const ATTITUDE_KNOWN_MINIMUM := -2
const ATTITUDE_KNOWN_MAXIMUM := 2

# Bounds. Every one of these is a hard cap; hitting one increments a counter
# rather than growing an allocation or recursing further.
const MAX_EVENTS := 8192
const MAX_SUBROUTINE_DEPTH := 8
const MAX_SUBROUTINE_CALLS_PER_TICK := 256
const MAX_OBJECT_LIST_ENTRIES := 512
const MAX_OBJECT_LISTS := 512
const MAX_COUNTERS := 4096
const MAX_FLAGS := 4096
const MAX_TIMERS := 4096
# World-registry bounds. The largest retail campaign map authors 48 named
# objects and 126 teams, so these caps are an order of magnitude of headroom
# and still refuse to grow without counting the refusal.
const MAX_NAMED_OBJECTS := 1024
const MAX_SCRIPT_TEAMS := 1024
const MAX_TEAM_NAMED_MEMBERS := 512
const MAX_WAYPOINTS := 2048
const MAX_WAYPOINT_PATHS := 512
const MAX_WAYPOINT_PATH_POINTS := 256
const MAX_TRIGGER_AREAS := 512
const MAX_TRIGGER_AREA_POINTS := 256
# Script-created objects (B4) are budgeted apart from authored ones so a
# reinforcement loop can never eat the authored registry, and so the two are
# separately visible when a bound is hit. A row is never recycled — a dead
# object keeps its row so NAMED_DESTROYED stays answerable — which makes this a
# whole-mission budget, not a concurrent-population one.
const MAX_CREATED_OBJECTS := 4096
# Members one CREATE_REINFORCEMENT_TEAM may instantiate. The largest authored
# campaign template asks for 12, so this refuses only a corrupt count.
const MAX_REINFORCEMENT_TEAM_MEMBERS := 256
# Trigger-area predicates walk the registry and the simulation, so the work one
# tick may do is capped; queries past the cap refuse rather than run.
const MAX_AREA_QUERIES_PER_TICK := 512
# Rows/entities one area query may test. Overflow refuses the query outright
# instead of answering from a truncated scan.
const MAX_AREA_QUERY_SCAN := 8192
# Audio waits HAS_FINISHED_AUDIO has armed but not yet retired (B6).
const MAX_AUDIO_WAITS := 512
# Named permanent map reveals live for the whole mission until their handle is
# undone, so this is a whole-mission budget like MAX_CREATED_OBJECTS.
const MAX_PERMANENT_REVEALS := 512
# Discovery predicates walk the reveal list and the simulation, so they carry
# their own per-tick budget rather than sharing the trigger-area one.
const MAX_DISCOVERY_QUERIES_PER_TICK := 512
const MAX_DISCOVERY_QUERY_SCAN := 8192

const SEMANTIC_CONDITIONS := {
	"CONDITION_TRUE": true,
	"CONDITION_FALSE": true,
	"COUNTER": true,
	"COUNTER_COUNTER": true,
	"COUNTER_SECONDS": true,
	"FLAG": true,
	"HAS_FINISHED_AUDIO": true,
	"NAMED_DESTROYED": true,
	"NAMED_DISCOVERED": true,
	"NAMED_INSIDE_AREA": true,
	"NAMED_NOT_DESTROYED": true,
	"NAMED_OWNED_BY_PLAYER": true,
	"PLAYER_HAS_COMPARISON_UNIT_TYPE_IN_TRIGGER_AREA": true,
	"PLAYER_HAS_COMPARISON_UNIT_TYPE_IN_TRIGGER_AREA_COMPLETELY_BUILT": true,
	"SKIRMISH_PLAYER_HAS_UNITS_IN_AREA": true,
	"TEAM_DESTROYED": true,
	"TEAM_DISCOVERED": true,
	"TEAM_HAS_UNITS": true,
	"TEAM_INSIDE_AREA_ENTIRELY": true,
	"TEAM_INSIDE_AREA_PARTIALLY": true,
	"TIMER_EXPIRED": true,
}

const SEMANTIC_ACTIONS := {
	"ATTACK_MOVE_NAMED_UNIT_TO": true,
	"ATTACK_MOVE_TEAM_TO": true,
	"CALL_SUBROUTINE": true,
	"CREATE_NAMED_ON_TEAM_AT_WAYPOINT": true,
	"CREATE_REINFORCEMENT_TEAM": true,
	"CREATE_UNNAMED_ON_TEAM_AT_WAYPOINT": true,
	"DISABLE_SCRIPT": true,
	"ENABLE_SCRIPT": true,
	"INCREMENT_COUNTER": true,
	"MAP_REVEAL_ALL_PERM": true,
	"MAP_REVEAL_ALL_UNDO_PERM": true,
	"MAP_REVEAL_PERMANENTLY_AT_WAYPOINT": true,
	"MAP_REVEAL_PERMANENTLY_IN_TRIGGER": true,
	"MAP_UNDO_REVEAL_PERMANENTLY_AT_WAYPOINT": true,
	"MAP_UNDO_REVEAL_PERMANENTLY_IN_TRIGGER": true,
	"MOVE_NAMED_UNIT_TO": true,
	"MOVE_TEAM_TO": true,
	"NAMED_ATTACK_FOLLOW_WAYPOINTS": true,
	"NAMED_DELETE": true,
	"NAMED_FOLLOW_WAYPOINTS": true,
	"NAMED_FOLLOW_WAYPOINTS_EXACT": true,
	"NAMED_HUNT": true,
	"NAMED_SET_ATTITUDE": true,
	"NO_OP": true,
	"OBJECTLIST_ADDOBJECTTYPE": true,
	"PLAYER_SET_MONEY": true,
	"SET_COUNTER": true,
	"SET_FLAG": true,
	"SET_MILLISECOND_TIMER": true,
	"SET_RANDOM_COUNTER": true,
	"SET_TIMER": true,
	"TEAM_ATTACK_MOVE_FOLLOW_WAYPOINTS": true,
	"TEAM_ATTACK_NAMED": true,
	"TEAM_FACE_WAYPOINT": true,
	"TEAM_FOLLOW_WAYPOINTS": true,
	"TEAM_FOLLOW_WAYPOINTS_EXACT": true,
	"TEAM_HUNT": true,
	"TEAM_MERGE_INTO_TEAM": true,
	"TEAM_SET_ATTITUDE": true,
	"TEAM_TRANSFER_TO_PLAYER": true,
	"UNIT_SET_TEAM": true,
}

# Retail presentation. Every entry here is an opcode whose whole retail effect
# is audiovisual or HUD: the minimap ping families, the audio mixer and music
# scripting, the cameo/unit flash families, the cinematic camera, and the HUD
# widgets. None of them reads or writes a single piece of simulation state, so
# each one only appends to `events`; RetailMapScriptRunner proves the whole
# bucket against a scriptless control by simulation-hash equality.
#
# Opcodes deliberately NOT here even though they look presentational:
# DESELECT (selection is simulation state), the *_FORCE_EMOTION family (BFME
# emotions drive fear/terror behaviour), UNIT_SET_MODELCONDITION* (model
# conditions can gate weapon and armor states), FREEZE_TIME/UNFREEZE_TIME and
# CAMERA_MOD_FREEZE_TIME (they stop the logic clock), and NAMED_SET_STEALTH
# _ENABLED / NAMED_SET_TOPPLE_DIRECTION. Only opcodes the retail corpus
# actually authors are declared, so every entry is backed by observed slots.
const RECORDED_ACTIONS := {
	"AUDIO_FADE_VOLUME": true,
	"AUDIO_MAKE_SOUND_IMMUNE_TO_FADE": true,
	"AUDIO_PUSH_MUSIC": true,
	"AUDIO_SET_REVERB_ROOM_TYPE": true,
	"AUDIO_SET_REVERB_SUPPRESSION_POLYGON": true,
	"CAMEO_FLASH": true,
	"CAMERA_FADE_ADD": true,
	"CAMERA_FADE_MULTIPLY": true,
	"CAMERA_FADE_SUBTRACT": true,
	"CAMERA_FOLLOW_NAMED": true,
	"CAMERA_LETTERBOX_BEGIN": true,
	"CAMERA_LETTERBOX_END": true,
	"CAMERA_LOOK_TOWARD_OBJECT": true,
	"CAMERA_LOOK_TOWARD_WAYPOINT": true,
	"CAMERA_MOD_LOOK_TOWARD": true,
	"CAMERA_MOVE_HOME": true,
	"CAMERA_RESTRICT_TO_AREA": true,
	"CAMERA_STOP_FOLLOW": true,
	"CLOSE_OBJECTIVES_SCREEN": true,
	"DEFEAT": true,
	"DISABLE_COUNTDOWN_TIMER_DISPLAY": true,
	"DISABLE_INPUT": true,
	"DISPLAY_COUNTDOWN_TIMER": true,
	"DISPLAY_COUNTER": true,
	"DISPLAY_NOTIFICATION_BOX": true,
	"DISPLAY_NOTIFICATION_BOX_WITH_OBJECT_TYPE_IMAGE_OVERRIDE": true,
	"ENABLE_COUNTDOWN_TIMER_DISPLAY": true,
	"ENABLE_HOUSE_COLOR": true,
	"ENABLE_INPUT": true,
	"ENABLE_OBJECTIVES_SCREEN": true,
	"ENABLE_OBJECT_SOUND": true,
	"EVA_SET_ENABLED_DISABLED": true,
	"FLASH_OBJECTIVES_BUTTON": true,
	"FLASH_SPELL_STORE_BUTTON": true,
	"FOCAL_LENGTH_CAMERA": true,
	"HERO_SELECT_BUTTON_FLASH": true,
	"HIDE_COUNTDOWN_TIMER": true,
	"HIDE_COUNTER": true,
	"HIDE_MISSION_OBJECTIVE": true,
	"HIDE_UI": true,
	"LOCK_CAMERA": true,
	"MARK_MISSION_OBJECTIVE_COMPLETED": true,
	"MOVE_CAMERA_ALONG_SPLINE_PATH": true,
	"MOVE_CAMERA_BY_ANIMATION": true,
	"MOVE_CAMERA_TO": true,
	"MUSIC_PLAY_TRACK_FINITE_TIMES": true,
	"MUSIC_PLAY_TRACK_FINITE_TIMES_AND_NOTIFY": true,
	"MUSIC_RESET_MUSIC_SCRIPTING_SYSTEM": true,
	"MUSIC_RETURN_TO_MUSIC_SCRIPTING": true,
	"MUSIC_SET_VOLUME": true,
	"NAMED_FLASH": true,
	"NAMED_FLASH_WHITE": true,
	"OBJECT_CREATE_RADAR_EVENT": true,
	"PITCH_CAMERA": true,
	"PLAY_MOVIE_IN_GAME": true,
	"PLAY_SOUND_EFFECT": true,
	"PLAY_SOUND_EFFECT_AT": true,
	"PLAY_SOUND_EFFECT_AT_TEAM": true,
	"QUICKVICTORY": true,
	"RADAR_CREATE_EVENT": true,
	"REFRESH_RADAR": true,
	"RESET_CAMERA": true,
	"RESUME_BACKGROUND_SOUNDS": true,
	"ROTATE_CAMERA": true,
	"SCREEN_SHAKE": true,
	"SELECT_BUILDER_BUTTON_FLASH": true,
	"SETUP_CAMERA": true,
	"SHOW_MILITARY_CAPTION": true,
	"SHOW_MISSION_OBJECTIVE": true,
	"SOUND_DISABLE_TYPE": true,
	"SOUND_PLAY_NAMED": true,
	"SPEECH_PLAY": true,
	"SUSPEND_BACKGROUND_SOUNDS": true,
	"TEAM_CREATE_RADAR_EVENT": true,
	"TEAM_FLASH": true,
	"TEAM_FLASH_WHITE": true,
	"VICTORY": true,
	"VICTORY_SCREEN": true,
	"ZOOM_CAMERA": true,
}

const RECORDED_CONDITIONS := {}

# Interpreter variable state. Counter and flag namespaces are shared across
# all loaded scripts, matching the retail per-player script environment.
var counters: Dictionary = {}
var flags: Dictionary = {}
# Timer name -> absolute interpreter tick at which the timer expires. A timer
# stays expired once its tick has passed, matching retail TIMER_EXPIRED.
var timers: Dictionary = {}
# Object-list name -> Array[String] of object type names (OBJECTLIST_ADD...).
var object_lists: Dictionary = {}
# Mission objective id -> {"shown": bool, "completed": bool}.
var mission_objectives: Dictionary = {}
var tick_index: int = 0

# Presentation surface. `events` is a bounded, ordered, deterministic log the
# future campaign presentation layer consumes; it never feeds back into the
# simulation, so it cannot perturb lockstep.
var events: Array[Dictionary] = []
var events_dropped: int = 0
var input_enabled: bool = true
var letterbox_active: bool = false
# "", "victory" or "defeat". First declaration wins.
var outcome: String = ""

# Decoded map world (waypoints, trigger areas, teams, players, named objects)
# from the converter's `world` section. Empty when only bare payloads were
# loaded; every world-dependent opcode stays unimplemented in that case.
var world: Dictionary = {}

# Player-reference text (for example "<This Player>") -> sim team integer.
var player_team_bindings: Dictionary = {}
var default_player_team: int = 0
# The SidesList player the local human controls, which is what retail's
# "<Local Player>" argument resolves to. The map cannot tell us: the campaign
# corpus authors `playerIsHuman` on two or more sides of 20 of the 28 campaign
# and tutorial missions, so reading it would be a guess. A host that loads a
# mission knows, and sets this; until it does, "<Local Player>" fails closed
# and is counted in bounds_hit["scope_relative_player"].
var local_player: String = ""

# --- World registries (B1 named objects, B2 teams) -------------------------
#
# Retail scripts address individual authored objects by name and groups of
# them by team, so both are first-class simulation state here. The converter's
# `world` section seeds them at load: every named ObjectsList entry becomes a
# registry row, and every Teams entry becomes a script team carrying its
# authored roster (named members individually, the remaining authored objects
# as a count, because an unnamed object is unaddressable and the registry is
# bounded).
#
# Lifecycle: a registry row is alive from mission start until NAMED_DELETE
# removes it or a bound simulation entity dies. `entity_id` is -1 until a host
# instantiates the object and calls bind_named_object(); orders issued to an
# object that has no simulation entity yet are retained on the row and counted
# in `deferred_orders`, so a run with unspawned objects can never be mistaken
# for one where every order actually reached the simulation.
#
# Iteration is always over the PackedStringArray name orders below, never over
# Dictionary key order, so tick behaviour is insertion-stable and identical on
# every peer.

# Object name -> row. See _new_named_row for the shape.
var named_objects: Dictionary = {}
var _named_object_order := PackedStringArray()
# Team name -> row. See _new_team_row for the shape.
var script_teams: Dictionary = {}
var _script_team_order := PackedStringArray()
# Waypoint name -> Vector2 (map document space, first authored wins).
var waypoints: Dictionary = {}
# Waypoint path label -> Array[Vector2]. Paths the converter could not order
# are refused at bind time and counted, never walked in an invented order.
var waypoint_paths: Dictionary = {}
# Trigger area name -> PackedVector2Array polygon (map document space).
var trigger_areas: Dictionary = {}
# Document-space units per simulation unit. 1.0 until a host that instantiates
# real entities sets it; only used to reconcile bound entity positions.
var world_scale: float = 1.0
# Opcode -> count of orders retained because the target had no live entity.
var deferred_orders: Dictionary = {}
var _bound_object_count: int = 0
# Registry rows the scripts themselves created (B4), budgeted apart from the
# authored ones, plus the monotonic ordinal that names the unnamed ones.
var _created_object_count: int = 0
var _created_object_ordinal: int = 0
# Objects instantiated into the simulation, and object types the loaded
# content could not instantiate. Both are reporting only.
var world_objects_instantiated: int = 0
var unavailable_object_types: Dictionary = {}
var _area_queries_this_tick: int = 0
var _discovery_queries_this_tick: int = 0

# --- Audio completion (B6) -------------------------------------------------
#
# Authored duration, not playback state. Retail's HAS_FINISHED_AUDIO does not
# consult the mixer and is not paired with the action that played the sound: on
# its first evaluation for a given audio-event name it files that name against
# `now + length(name)` and answers false, and on a later evaluation past that
# tick it answers true and retires the entry, so the next wait re-arms. Length
# comes from the audio event's own authored duration; an event the loaded
# content does not carry has length zero and therefore completes on its first
# evaluation, which is exactly what retail does with a missing event.
#
# `audio_event_durations` is that length table, in seconds, keyed by audio
# event name. A host with a cooked campaign audio pack fills it (see
# set_audio_event_durations); until it does, every wait completes immediately
# and the missing lengths are counted in
# bounds_hit["audio_event_length_unknown"], so a mission that ran without
# pacing can never be mistaken for one that had it.
var audio_event_durations: Dictionary = {}
# Audio event name -> absolute interpreter tick the wait completes on.
var _audio_waits: Dictionary = {}

# --- Shroud and discovery --------------------------------------------------
#
# Retail's NAMED_DISCOVERED / TEAM_DISCOVERED ask whether an object is
# currently unshrouded for a player, not whether it was ever seen. A player's
# unshrouded set here is the union of two things and nothing else:
#
#   * permanent map reveals - the MAP_REVEAL_PERMANENTLY_* family, each filed
#     under the handle its MAP_UNDO_REVEAL_PERMANENTLY_* sibling removes, plus
#     the whole-map MAP_REVEAL_ALL_PERM flag; and
#   * the sight of the player's living simulation entities, from the
#     `vision_range` the loaded content compiled onto each one.
#
# A registry row the simulation could not instantiate contributes no sight,
# because nothing tells us how far an uninstantiated object can see; a query
# that answers "not discovered" while such rows exist counts the shortfall in
# bounds_hit["discovery_vision_unavailable"] rather than passing itself off as
# a complete answer. The retail predicate also excludes held and undetected
# -stealthed objects; this simulation models neither, so a stealthed object
# would read as discovered here - see bounds_hit["discovery_ignores_stealth"],
# raised once per load when the world binds.
#
# The non-permanent reveal family (MAP_REVEAL_AT_WAYPOINT, MAP_REVEAL_IN_
# TRIGGER, MAP_REVEAL_ALL, MAP_SHROUD_ALL, MAP_SHROUD_AT_WAYPOINT) is
# deliberately NOT implemented: those reveal a region that then decays back
# under passive shroud, and this world has no explored-but-not-visible layer to
# decay into. They stay unimplemented and counted.
const PLAYER_ALL_REFERENCE := "<All Players>"
# Reveal handle -> {"player": String, "kind": String, ...}. Iterated through
# _permanent_reveal_order so the scan is insertion-stable on every peer.
var permanent_reveals: Dictionary = {}
var _permanent_reveal_order := PackedStringArray()
# Player name -> true while MAP_REVEAL_ALL_PERM stands for that player.
var permanently_revealed_players: Dictionary = {}

# Key prefix for a script-created object with no authored name. Retail's
# CREATE_UNNAMED_ON_TEAM_AT_WAYPOINT and CREATE_REINFORCEMENT_TEAM produce real
# objects that hold ground, die, take team orders and answer trigger-area
# predicates, but that no script can address; they therefore need registry rows
# with positions, not a bare count. The prefix is U+0001, a control character
# WorldBuilder cannot place in an authored object name, so a synthetic key can
# never collide with one and _named_row refuses to resolve a key carrying it.
const UNNAMED_KEY_PREFIX := "created#"

# Load-time opcode census (fail-closed accounting).
var implemented: Dictionary = {}
var recorded: Dictionary = {}
var unimplemented: Dictionary = {}
# Opcodes actually reached while stepping that this interpreter does not honour.
var runtime_unimplemented: Dictionary = {}
# Bound-overflow accounting, so a truncated run is never mistaken for a clean one.
var bounds_hit: Dictionary = {}

var _scripts: Array[Dictionary] = []
# Script name -> Array[int] of indices into _scripts (retail permits duplicates).
var _by_name: Dictionary = {}
var _rng_state: int = 0x9E3779B97F4A7C15
var _subroutine_calls_this_tick: int = 0
var _stealth_shortfall_noted: bool = false


# --- Loading ---------------------------------------------------------------


func load_document(document: Dictionary) -> int:
	## Loads a full `openbfme.map-scripts` document, world included.
	var world_value: Variant = document.get("world", {})
	if world_value is Dictionary and bool((world_value as Dictionary).get("available", false)):
		world = world_value
		_bind_world()
	var loaded := 0
	for row: Variant in Array(document.get("scripts", [])):
		var payload: Variant = (row as Dictionary).get("payload", {})
		if payload is Dictionary and not (payload as Dictionary).is_empty():
			load_script_payload(payload)
			loaded += 1
	# Seed the deterministic draw stream from the document identity so two
	# runs of the same mission draw the same sequence and two different
	# missions do not share one.
	var source: Dictionary = document.get("source", {})
	_rng_state = _seed_from_text(String(source.get("sourceSha256", "")))
	return loaded


func load_contract_source(source: Dictionary) -> int:
	## Loads every script from one `sources[i]` entry of the contract JSON.
	var loaded := 0
	for row: Variant in Array(source.get("scripts", [])):
		var payload: Dictionary = (row as Dictionary).get("payload", {})
		if not payload.is_empty():
			load_script_payload(payload)
			loaded += 1
	return loaded


func load_script_payloads(payloads: Array) -> int:
	var loaded := 0
	for payload: Variant in payloads:
		if payload is Dictionary and not (payload as Dictionary).is_empty():
			load_script_payload(payload)
			loaded += 1
	return loaded


func load_script_payload(payload: Dictionary) -> void:
	var condition_blocks: Array[Array] = []
	var true_actions: Array[Dictionary] = []
	var false_actions: Array[Dictionary] = []
	for record: Variant in Array(payload.get("records", [])):
		var record_row: Dictionary = record
		var record_name := String(record_row.get("name", ""))
		var value: Dictionary = record_row.get("value", {})
		if record_name == "OrCondition":
			var block: Array[Dictionary] = []
			for child: Variant in Array(value.get("records", [])):
				var child_row: Dictionary = child
				if String(child_row.get("name", "")) == "Condition":
					block.append(_content_row(child_row.get("value", {}), true))
			condition_blocks.append(block)
		elif record_name == "ScriptAction":
			true_actions.append(_content_row(value, false))
		elif record_name == "ScriptActionFalse":
			false_actions.append(_content_row(value, false))
	var interval_seconds := float(payload.get("evaluationInterval", 0))
	var script_name := String(payload.get("name", ""))
	var index := _scripts.size()
	_scripts.append({
		"name": script_name,
		"active": bool(payload.get("isActive", true)),
		"subroutine": bool(payload.get("isSubroutine", false)),
		"deactivate_upon_success": bool(payload.get("deactivateUponSuccess", false)),
		"eval_interval_ticks": maxi(1, int(round(interval_seconds / TICK_SECONDS))) if interval_seconds > 0.0 else 1,
		"condition_blocks": condition_blocks,
		"true_actions": true_actions,
		"false_actions": false_actions,
	})
	if not _by_name.has(script_name):
		_by_name[script_name] = PackedInt32Array()
	var bucket: PackedInt32Array = _by_name[script_name]
	bucket.append(index)
	_by_name[script_name] = bucket


# --- World binding ---------------------------------------------------------


func _bind_world() -> void:
	## Seeds the named-object, team, waypoint and trigger-area registries from
	## the converter's `world` section. Every registry is capped; hitting a cap
	## drops the surplus and counts it rather than growing.
	var by_id: Dictionary = {}
	for row: Variant in Array(world.get("waypoints", [])):
		var waypoint: Dictionary = row
		var position := _document_point(waypoint.get("godotPosition", []))
		var waypoint_id := int(waypoint.get("id", -1))
		if waypoint_id >= 0 and not by_id.has(waypoint_id):
			by_id[waypoint_id] = position
		var waypoint_name := String(waypoint.get("name", ""))
		if waypoint_name == "" or waypoints.has(waypoint_name):
			continue
		if waypoints.size() >= MAX_WAYPOINTS:
			_note_bound("waypoints")
			continue
		waypoints[waypoint_name] = position

	for row: Variant in Array(world.get("waypointPaths", [])):
		var path: Dictionary = row
		var label := String(path.get("label", ""))
		if label == "" or waypoint_paths.has(label):
			continue
		if not bool(path.get("ordered", false)):
			# The converter could not resolve a single chain through this
			# path's edges. Walking it in an invented order would be a
			# fabricated retail behaviour, so it is refused outright.
			_note_bound("unordered_waypoint_path")
			continue
		if waypoint_paths.size() >= MAX_WAYPOINT_PATHS:
			_note_bound("waypoint_paths")
			continue
		var points: Array[Vector2] = []
		var truncated := false
		for id_value: Variant in Array(path.get("waypointIds", [])):
			if points.size() >= MAX_WAYPOINT_PATH_POINTS:
				truncated = true
				break
			var waypoint_id := int(id_value)
			if not by_id.has(waypoint_id):
				truncated = true
				break
			points.append(by_id[waypoint_id])
		if truncated or points.is_empty():
			_note_bound("waypoint_path_points")
			continue
		waypoint_paths[label] = points

	for row: Variant in Array(world.get("triggerAreas", [])):
		var area: Dictionary = row
		var area_name := String(area.get("name", ""))
		if area_name == "" or trigger_areas.has(area_name):
			continue
		if trigger_areas.size() >= MAX_TRIGGER_AREAS:
			_note_bound("trigger_areas")
			continue
		var polygon := PackedVector2Array()
		var points_value: Array = Array(area.get("godotXZPoints", []))
		if points_value.size() < 3 or points_value.size() > MAX_TRIGGER_AREA_POINTS:
			# Fewer than three points is not a polygon and more than the cap
			# would be unbounded; either way the area is refused, not clamped.
			_note_bound("trigger_area_points")
			continue
		for point_value: Variant in points_value:
			var pair: Array = point_value
			if pair.size() < 2:
				polygon = PackedVector2Array()
				break
			polygon.append(Vector2(float(pair[0]), float(pair[1])))
		if polygon.size() < 3:
			_note_bound("trigger_area_points")
			continue
		trigger_areas[area_name] = polygon

	for row: Variant in Array(world.get("teams", [])):
		var team: Dictionary = row
		var team_name := String(team.get("name", ""))
		if team_name == "" or script_teams.has(team_name):
			continue
		if script_teams.size() >= MAX_SCRIPT_TEAMS:
			_note_bound("script_teams")
			continue
		script_teams[team_name] = _new_team_row(team)
		_script_team_order.append(team_name)

	for row: Variant in Array(world.get("namedObjects", [])):
		var object_row: Dictionary = row
		var object_name := String(object_row.get("name", ""))
		if object_name == "" or named_objects.has(object_name):
			# Retail permits duplicate authored names; the first wins and the
			# collision is counted, because a script naming it cannot say which.
			if object_name != "":
				_note_bound("duplicate_named_object")
			continue
		if named_objects.size() >= MAX_NAMED_OBJECTS:
			_note_bound("named_objects")
			continue
		named_objects[object_name] = _new_named_row(object_row)
		_named_object_order.append(object_name)


func _new_named_row(source: Dictionary) -> Dictionary:
	return {
		"name": String(source.get("name", "")),
		"type_name": String(source.get("typeName", "")),
		"owner": String(source.get("owner", "")),
		"team": String(source.get("team", "")),
		"position": _document_point(source.get("godotPosition", [])),
		"yaw": float(source.get("godotYawRadians", 0.0)),
		"attitude": ATTITUDE_NORMAL,
		"destroyed": false,
		"deleted": false,
		"entity_id": -1,
		"order": {},
	}


func _new_team_row(source: Dictionary) -> Dictionary:
	var members := PackedStringArray()
	for value: Variant in Array(source.get("namedMembers", [])):
		var member := String(value)
		if member == "" or members.has(member):
			continue
		if members.size() >= MAX_TEAM_NAMED_MEMBERS:
			_note_bound("team_named_members")
			break
		members.append(member)
	# Authored objects on this team that carry no name. They are counted, not
	# rowed: nothing can address them, so nothing can kill them either, and
	# TEAM_HAS_UNITS must still see them at mission start the way retail does.
	var anonymous := maxi(0, int(source.get("objectCount", 0)) - members.size())
	# Authored reinforcement template: the unit composition CREATE_REINFORCEMENT
	# _TEAM instantiates. Slots stay in the converter's authored slot order so
	# the draw sequence is identical on every peer.
	var units: Array[Dictionary] = []
	for value: Variant in Array(source.get("units", [])):
		var unit_row: Dictionary = value
		var type_name := String(unit_row.get("type", ""))
		if type_name == "":
			continue
		units.append({
			"type": type_name,
			"min_count": maxi(0, int(unit_row.get("minCount", 0))),
			"max_count": maxi(0, int(unit_row.get("maxCount", 0))),
		})
	return {
		"name": String(source.get("name", "")),
		"owner": String(source.get("owner", "")),
		"attitude": ATTITUDE_NORMAL,
		"members": members,
		"anonymous_members": anonymous,
		"units": units,
		"order": {},
	}


func _document_point(value: Variant) -> Vector2:
	# The converter emits [x, y, z] in Godot axes; the script world is a plan
	# view, so the XZ pair is the position and the height is dropped.
	var triple: Array = Array(value)
	if triple.size() < 3:
		return Vector2.ZERO
	return Vector2(float(triple[0]), float(triple[2]))


func script_count() -> int:
	return _scripts.size()


func active_script_names() -> Array[String]:
	var names: Array[String] = []
	for state in _scripts:
		if bool(state["active"]):
			names.append(String(state["name"]))
	return names


func coverage_summary() -> Dictionary:
	## Slot totals per bucket, for reporting. Recorded slots are presentation
	## only and are deliberately reported apart from implemented slots.
	return {
		"implementedSlots": _sum_values(implemented),
		"recordedSlots": _sum_values(recorded),
		"unimplementedSlots": _sum_values(unimplemented),
		"distinctUnimplemented": unimplemented.size(),
	}


# --- Stepping --------------------------------------------------------------


func step(sim) -> void:
	## Advances the interpreter by one tick against the supplied RetailSliceSim.
	tick_index += 1
	_subroutine_calls_this_tick = 0
	_area_queries_this_tick = 0
	_discovery_queries_this_tick = 0
	_reconcile_bound_objects(sim)
	# Iterate by index over a snapshot size: ENABLE_SCRIPT may only flip
	# activity flags on already-loaded scripts, never append, so the bound is
	# fixed for the whole tick.
	var count := _scripts.size()
	for index in range(count):
		var state: Dictionary = _scripts[index]
		if not bool(state["active"]) or bool(state["subroutine"]):
			continue
		var interval := int(state["eval_interval_ticks"])
		if interval > 1 and tick_index % interval != 0:
			continue
		_run_script(state, sim, 0)


func _run_script(state: Dictionary, sim, depth: int) -> void:
	var fired := _evaluate_condition_blocks(state["condition_blocks"], sim)
	var actions: Array[Dictionary] = state["true_actions"] if fired else state["false_actions"]
	for action in actions:
		if bool(action["enabled"]):
			_execute_action(action, sim, depth)
	if fired and bool(state["deactivate_upon_success"]):
		state["active"] = false


func _content_row(value: Dictionary, is_condition: bool) -> Dictionary:
	var internal: Dictionary = value.get("internalName", {})
	var opcode := String(internal.get("name", "<missing>"))
	var row := {
		"opcode": opcode,
		"arguments": Array(value.get("arguments", [])),
		"enabled": bool(value.get("enabled", true)),
	}
	if is_condition:
		row["inverted"] = bool(value.get("inverted", false))
	var semantic: Dictionary = SEMANTIC_CONDITIONS if is_condition else SEMANTIC_ACTIONS
	var presentation: Dictionary = RECORDED_CONDITIONS if is_condition else RECORDED_ACTIONS
	var bucket := unimplemented
	if semantic.has(opcode):
		bucket = implemented
	elif presentation.has(opcode):
		bucket = recorded
	bucket[opcode] = int(bucket.get(opcode, 0)) + 1
	return row


func _evaluate_condition_blocks(blocks: Array[Array], sim) -> bool:
	# OrCondition blocks are OR'd; conditions inside one block are AND'd.
	# A script without any condition block never fires (fail-closed).
	for block: Array in blocks:
		var block_true := true
		var considered := false
		for condition: Dictionary in block:
			if not bool(condition["enabled"]):
				continue
			considered = true
			if not _evaluate_condition(condition, sim):
				block_true = false
				break
		if block_true and considered:
			return true
	return false


func _evaluate_condition(condition: Dictionary, sim) -> bool:
	var opcode := String(condition["opcode"])
	var arguments: Array = condition["arguments"]
	var inverted := bool(condition.get("inverted", false))
	var result: bool
	match opcode:
		"CONDITION_TRUE":
			result = true
		"CONDITION_FALSE":
			result = false
		"COUNTER":
			# Retail signature: [counter name, comparison, integer].
			var name := _text_at(arguments, 0, ARGUMENT_COUNTER_NAME)
			var comparison := _int_at(arguments, 1, ARGUMENT_COMPARISON, COMPARE_EQUAL)
			var value := _int_at(arguments, 2, ARGUMENT_INTEGER, 0)
			result = _compare(int(counters.get(name, 0)), comparison, value)
		"COUNTER_COUNTER":
			# Retail signature: [counter name, comparison, counter name].
			var left := _text_at(arguments, 0, ARGUMENT_COUNTER_NAME)
			var comparison2 := _int_at(arguments, 1, ARGUMENT_COMPARISON, COMPARE_EQUAL)
			var right := _text_at(arguments, 2, ARGUMENT_COUNTER_NAME)
			result = _compare(
				int(counters.get(left, 0)), comparison2, int(counters.get(right, 0))
			)
		"COUNTER_SECONDS":
			# Retail signature: [timer name, comparison, real seconds].
			# Compares the timer's remaining seconds; an unarmed timer reads 0.
			var timer_name := _text_at(arguments, 0, ARGUMENT_COUNTER_NAME)
			var comparison3 := _int_at(arguments, 1, ARGUMENT_COMPARISON, COMPARE_EQUAL)
			var seconds := _real_at(arguments, 2, ARGUMENT_REAL, 0.0)
			var remaining_ticks := 0
			if timers.has(timer_name):
				remaining_ticks = maxi(0, int(timers[timer_name]) - tick_index)
			# Whole-tick comparison keeps this integral and therefore exactly
			# reproducible across platforms; retail timers are tick-quantised.
			result = _compare(
				remaining_ticks, comparison3, int(floor(seconds / TICK_SECONDS))
			)
		"FLAG":
			var flag_name := _text_at(arguments, 0, ARGUMENT_FLAG_NAME)
			var expected := _int_at(arguments, 1, ARGUMENT_BOOLEAN, 1) != 0
			result = bool(flags.get(flag_name, false)) == expected
		"TIMER_EXPIRED":
			var expiring := _text_at(arguments, 0, ARGUMENT_COUNTER_NAME)
			result = timers.has(expiring) and tick_index >= int(timers[expiring])
		"HAS_FINISHED_AUDIO":
			# Retail signature: [audio event]. See the B6 note above: the
			# condition itself arms the wait, so this branch is self-contained
			# and never reads the presentation bucket.
			result = _audio_wait_complete(_text_at(arguments, 0, ARGUMENT_SOUND))
		"NAMED_DISCOVERED":
			# Retail signature: [unit name, player]. True while the object is
			# unshrouded for that player.
			var discovered_row := _named_row(_text_at(arguments, 0, ARGUMENT_UNIT_NAME))
			var discovering := _resolve_player(_text_at(arguments, 1, ARGUMENT_PLAYER))
			if discovered_row.is_empty() or bool(discovered_row["destroyed"]) or discovering == "":
				result = false
			else:
				result = _position_discovered(
					discovering, discovered_row["position"], sim
				) == 1
		"TEAM_DISCOVERED":
			# Retail signature: [team, player]. True while any living member of
			# the team is unshrouded for that player.
			var discovered_team := _team_row(_text_at(arguments, 0, ARGUMENT_TEAM))
			var team_viewer := _resolve_player(_text_at(arguments, 1, ARGUMENT_PLAYER))
			result = _team_discovered(discovered_team, team_viewer, sim)
		"NAMED_DESTROYED":
			# Retail signature: [unit name]. True once the named object has
			# left the world, whether it died or NAMED_DELETE removed it.
			var destroyed_row := _named_row(_text_at(arguments, 0, ARGUMENT_UNIT_NAME))
			result = not destroyed_row.is_empty() and bool(destroyed_row["destroyed"])
		"NAMED_NOT_DESTROYED":
			# Retail signature: [unit name]. Not the strict inverse of
			# NAMED_DESTROYED: an unknown name answers false to both, because
			# the interpreter cannot claim an object it never bound is alive.
			var living_row := _named_row(_text_at(arguments, 0, ARGUMENT_UNIT_NAME))
			result = not living_row.is_empty() and not bool(living_row["destroyed"])
		"NAMED_INSIDE_AREA":
			# Retail signature: [unit name, trigger area].
			var inside_row := _named_row(_text_at(arguments, 0, ARGUMENT_UNIT_NAME))
			var area_name := _text_at(arguments, 1, ARGUMENT_TRIGGER_AREA)
			if inside_row.is_empty() or bool(inside_row["destroyed"]):
				result = false
			elif not trigger_areas.has(area_name):
				_note_bound("unknown_trigger_area")
				result = false
			else:
				result = _point_inside_polygon(
					inside_row["position"], trigger_areas[area_name]
				)
		"NAMED_OWNED_BY_PLAYER":
			# Retail signature: [unit name, player].
			var owned_row := _named_row(_text_at(arguments, 0, ARGUMENT_UNIT_NAME))
			result = (
				not owned_row.is_empty()
				and not bool(owned_row["destroyed"])
				and String(owned_row["owner"]) == _text_at(arguments, 1, ARGUMENT_PLAYER)
			)
		"TEAM_HAS_UNITS":
			# Retail signature: [team]. Every authored object on the team
			# counts, named or not, exactly as retail's team member list does.
			var populated_team := _team_row(_text_at(arguments, 0, ARGUMENT_TEAM))
			result = not populated_team.is_empty() and _team_living_count(populated_team) > 0
		"TEAM_DESTROYED":
			# Retail signature: [team].
			var dead_team := _team_row(_text_at(arguments, 0, ARGUMENT_TEAM))
			result = not dead_team.is_empty() and _team_living_count(dead_team) == 0
		"SKIRMISH_PLAYER_HAS_UNITS_IN_AREA":
			# Retail signature: [player, trigger area]. True when the player owns
			# at least one living object inside the area.
			var area_player := _resolve_player(_text_at(arguments, 0, ARGUMENT_PLAYER))
			var units_polygon := _begin_area_query(
				_text_at(arguments, 1, ARGUMENT_TRIGGER_AREA)
			)
			result = _count_player_objects_in_area(
				area_player, units_polygon, PackedStringArray(), sim
			) > 0
		"PLAYER_HAS_COMPARISON_UNIT_TYPE_IN_TRIGGER_AREA", 		"PLAYER_HAS_COMPARISON_UNIT_TYPE_IN_TRIGGER_AREA_COMPLETELY_BUILT":
			# Retail signature:
			# [player, comparison, integer, object type, trigger area].
			var typed_player := _resolve_player(_text_at(arguments, 0, ARGUMENT_PLAYER))
			var typed_comparison := _int_at(arguments, 1, ARGUMENT_COMPARISON, COMPARE_EQUAL)
			var typed_count := _int_at(arguments, 2, ARGUMENT_INTEGER, 0)
			var typed_name := _text_at(arguments, 3, ARGUMENT_OBJECT_TYPE_OR_LIST)
			var typed_polygon := _begin_area_query(
				_text_at(arguments, 4, ARGUMENT_TRIGGER_AREA)
			)
			var typed_accepted := _accepted_object_types(typed_name)
			if typed_accepted.is_empty():
				result = false
			else:
				var found := _count_player_objects_in_area(
					typed_player, typed_polygon, typed_accepted, sim
				)
				result = found >= 0 and _compare(found, typed_comparison, typed_count)
		"TEAM_INSIDE_AREA_PARTIALLY", "TEAM_INSIDE_AREA_ENTIRELY":
			# Retail signature: [team, trigger area, member filter].
			var area_team := _team_row(_text_at(arguments, 0, ARGUMENT_TEAM))
			var team_polygon := _begin_area_query(
				_text_at(arguments, 1, ARGUMENT_TRIGGER_AREA)
			)
			var filter_ok := _team_area_filter_ok(
				_int_at(arguments, 2, ARGUMENT_AREA_MEMBER_FILTER, AREA_MEMBER_FILTER_ALL)
			)
			if area_team.is_empty() or team_polygon.size() < 3 or not filter_ok:
				result = false
			else:
				var tally := _team_members_inside_area(area_team, team_polygon, sim)
				if tally.x < 0 or tally.y <= 0:
					# Unanswerable, or a team with nothing alive: a team with no
					# living member is neither partially nor entirely inside.
					result = false
				elif opcode == "TEAM_INSIDE_AREA_PARTIALLY":
					result = tally.x > 0
				else:
					result = tally.x == tally.y
		_:
			# Fail-closed: an unimplemented condition makes its AND-block
			# false regardless of inversion, and the miss is counted.
			runtime_unimplemented[opcode] = int(runtime_unimplemented.get(opcode, 0)) + 1
			return false
	return not result if inverted else result


func _execute_action(action: Dictionary, sim, depth: int) -> void:
	var opcode := String(action["opcode"])
	var arguments: Array = action["arguments"]
	match opcode:
		"NO_OP":
			pass
		"SET_COUNTER":
			# Retail signature: [counter name, integer].
			_set_counter(
				_text_at(arguments, 0, ARGUMENT_COUNTER_NAME),
				_int_at(arguments, 1, ARGUMENT_INTEGER, 0)
			)
		"INCREMENT_COUNTER":
			# Retail signature: [integer amount, counter name].
			var name := _text_at(arguments, 1, ARGUMENT_COUNTER_NAME)
			_set_counter(
				name,
				int(counters.get(name, 0)) + _int_at(arguments, 0, ARGUMENT_INTEGER, 1)
			)
		"SET_RANDOM_COUNTER":
			# Retail signature: [counter name, low, high], inclusive.
			var random_name := _text_at(arguments, 0, ARGUMENT_COUNTER_NAME)
			var low := _int_at(arguments, 1, ARGUMENT_INTEGER, 0)
			var high := _int_at(arguments, 2, ARGUMENT_INTEGER, 0)
			_set_counter(random_name, _draw_inclusive(low, high))
		"SET_FLAG":
			# Retail signature: [flag name, boolean].
			if flags.size() < MAX_FLAGS or flags.has(_text_at(arguments, 0, ARGUMENT_FLAG_NAME)):
				flags[_text_at(arguments, 0, ARGUMENT_FLAG_NAME)] = (
					_int_at(arguments, 1, ARGUMENT_BOOLEAN, 1) != 0
				)
			else:
				_note_bound("flags")
		"SET_MILLISECOND_TIMER":
			# Retail signature: [timer name, real milliseconds].
			_arm_timer(
				_text_at(arguments, 0, ARGUMENT_COUNTER_NAME),
				maxi(1, int(ceil(
					_real_at(arguments, 1, ARGUMENT_REAL, 0.0) / (TICK_SECONDS * 1000.0)
				)))
			)
		"SET_TIMER":
			# Retail signature: [timer name, integer seconds].
			_arm_timer(
				_text_at(arguments, 0, ARGUMENT_COUNTER_NAME),
				maxi(1, int(round(
					float(_int_at(arguments, 1, ARGUMENT_INTEGER, 0)) / TICK_SECONDS
				)))
			)
		"ENABLE_SCRIPT":
			_set_script_active(_text_at(arguments, 0, ARGUMENT_SCRIPT_NAME), true)
		"DISABLE_SCRIPT":
			_set_script_active(_text_at(arguments, 0, ARGUMENT_SCRIPT_NAME), false)
		"CALL_SUBROUTINE":
			_call_subroutine(_text_at(arguments, 0, ARGUMENT_SUBROUTINE), sim, depth)
		"CREATE_NAMED_ON_TEAM_AT_WAYPOINT":
			# Retail signature: [unit name, object type, team, waypoint].
			_create_named_on_team_at_waypoint(
				_text_at(arguments, 0, ARGUMENT_UNIT_NAME),
				_text_at(arguments, 1, ARGUMENT_OBJECT_TYPE),
				_text_at(arguments, 2, ARGUMENT_TEAM),
				_text_at(arguments, 3, ARGUMENT_WAYPOINT),
				sim
			)
		"CREATE_UNNAMED_ON_TEAM_AT_WAYPOINT":
			# Retail signature: [object type, team, waypoint].
			_create_unnamed_on_team_at_waypoint(
				_text_at(arguments, 0, ARGUMENT_OBJECT_TYPE),
				_text_at(arguments, 1, ARGUMENT_TEAM),
				_text_at(arguments, 2, ARGUMENT_WAYPOINT),
				sim
			)
		"CREATE_REINFORCEMENT_TEAM":
			# Retail signature: [team, waypoint].
			_create_reinforcement_team(
				_text_at(arguments, 0, ARGUMENT_TEAM),
				_text_at(arguments, 1, ARGUMENT_WAYPOINT),
				sim
			)
		"OBJECTLIST_ADDOBJECTTYPE":
			# Retail signature: [object-list name, object type].
			_object_list_add(
				_text_at(arguments, 0, ARGUMENT_OBJECT_LIST),
				_text_at(arguments, 1, ARGUMENT_OBJECT_TYPE)
			)
		"PLAYER_SET_MONEY":
			# Retail signature: [player, integer]. The only simulation write.
			var player := _text_at(arguments, 0, ARGUMENT_PLAYER)
			var amount := _int_at(arguments, 1, ARGUMENT_INTEGER, 0)
			var team := int(player_team_bindings.get(player, default_player_team))
			sim.team_resources[team] = amount
		"NAMED_DELETE":
			# Retail signature: [unit name]. Removes the object from the world;
			# a deleted object reads as destroyed to NAMED_DESTROYED.
			_named_delete(_text_at(arguments, 0, ARGUMENT_UNIT_NAME), sim)
		"NAMED_SET_ATTITUDE":
			# Retail signature: [unit name, attitude].
			_set_named_attitude(
				_text_at(arguments, 0, ARGUMENT_UNIT_NAME),
				_int_at(arguments, 1, ARGUMENT_ATTITUDE, ATTITUDE_NORMAL)
			)
		"TEAM_SET_ATTITUDE":
			# Retail signature: [team, attitude].
			_set_team_attitude(
				_text_at(arguments, 0, ARGUMENT_TEAM),
				_int_at(arguments, 1, ARGUMENT_ATTITUDE, ATTITUDE_NORMAL)
			)
		"MOVE_NAMED_UNIT_TO", "ATTACK_MOVE_NAMED_UNIT_TO":
			# Retail signature: [unit name, waypoint].
			_order_named(
				_text_at(arguments, 0, ARGUMENT_UNIT_NAME),
				_waypoint_order(opcode, _text_at(arguments, 1, ARGUMENT_WAYPOINT)),
				opcode,
				sim
			)
		"NAMED_FOLLOW_WAYPOINTS", "NAMED_FOLLOW_WAYPOINTS_EXACT", "NAMED_ATTACK_FOLLOW_WAYPOINTS":
			# Retail signature: [unit name, waypoint path].
			_order_named(
				_text_at(arguments, 0, ARGUMENT_UNIT_NAME),
				_waypoint_path_order(
					opcode,
					_text_at(arguments, 1, ARGUMENT_WAYPOINT_PATH),
					opcode == "NAMED_FOLLOW_WAYPOINTS_EXACT",
					opcode == "NAMED_ATTACK_FOLLOW_WAYPOINTS"
				),
				opcode,
				sim
			)
		"NAMED_HUNT":
			# Retail signature: [unit name]. Hunt has no destination: the unit
			# seeks hostiles across the whole map until something stops it.
			_order_named(
				_text_at(arguments, 0, ARGUMENT_UNIT_NAME),
				{"kind": "hunt", "opcode": opcode, "tick": tick_index},
				opcode,
				sim
			)
		"MOVE_TEAM_TO", "ATTACK_MOVE_TEAM_TO", "TEAM_FACE_WAYPOINT":
			# Retail signature: [team, waypoint].
			_order_team(
				_text_at(arguments, 0, ARGUMENT_TEAM),
				_waypoint_order(opcode, _text_at(arguments, 1, ARGUMENT_WAYPOINT)),
				opcode,
				sim
			)
		"TEAM_FOLLOW_WAYPOINTS", "TEAM_FOLLOW_WAYPOINTS_EXACT", "TEAM_ATTACK_MOVE_FOLLOW_WAYPOINTS":
			# Retail signature: [team, waypoint path, boolean(, boolean)].
			_order_team(
				_text_at(arguments, 0, ARGUMENT_TEAM),
				_waypoint_path_order(
					opcode,
					_text_at(arguments, 1, ARGUMENT_WAYPOINT_PATH),
					opcode == "TEAM_FOLLOW_WAYPOINTS_EXACT",
					opcode == "TEAM_ATTACK_MOVE_FOLLOW_WAYPOINTS"
				),
				opcode,
				sim
			)
		"TEAM_HUNT":
			# Retail signature: [team].
			_order_team(
				_text_at(arguments, 0, ARGUMENT_TEAM),
				{"kind": "hunt", "opcode": opcode, "tick": tick_index},
				opcode,
				sim
			)
		"TEAM_ATTACK_NAMED":
			# Retail signature: [team, unit name].
			var target_name := _text_at(arguments, 1, ARGUMENT_UNIT_NAME)
			var target_row := _named_row(target_name)
			if target_row.is_empty() or bool(target_row["destroyed"]):
				# Nothing to attack. Retail would issue no order either, but
				# the miss is counted so it cannot hide a binding failure.
				_note_bound("unknown_named_object")
			else:
				_order_team(
					_text_at(arguments, 0, ARGUMENT_TEAM),
					{
						"kind": "attack_object",
						"opcode": opcode,
						"target": target_name,
						"tick": tick_index,
					},
					opcode,
					sim
				)
		"TEAM_MERGE_INTO_TEAM":
			# Retail signature: [source team, destination team].
			_merge_team(
				_text_at(arguments, 0, ARGUMENT_TEAM),
				_text_at(arguments, 1, ARGUMENT_TEAM)
			)
		"UNIT_SET_TEAM":
			# Retail signature: [unit name, team].
			_set_named_team(
				_text_at(arguments, 0, ARGUMENT_UNIT_NAME),
				_text_at(arguments, 1, ARGUMENT_TEAM)
			)
		"MAP_REVEAL_PERMANENTLY_AT_WAYPOINT":
			# Retail signature: [waypoint, radius, player, reveal handle].
			# The radius is authored in map document units, the same space the
			# waypoint positions are in, so no conversion is involved.
			_add_permanent_reveal(
				_text_at(arguments, 3, ARGUMENT_REVEAL_NAME),
				_text_at(arguments, 2, ARGUMENT_PLAYER),
				{
					"kind": "circle",
					"waypoint": _text_at(arguments, 0, ARGUMENT_WAYPOINT),
					"radius": _real_at(arguments, 1, ARGUMENT_REAL, 0.0),
				}
			)
		"MAP_REVEAL_PERMANENTLY_IN_TRIGGER":
			# Retail signature: [trigger area, player, reveal handle].
			_add_permanent_reveal(
				_text_at(arguments, 2, ARGUMENT_REVEAL_NAME),
				_text_at(arguments, 1, ARGUMENT_PLAYER),
				{"kind": "area", "area": _text_at(arguments, 0, ARGUMENT_TRIGGER_AREA)}
			)
		"MAP_UNDO_REVEAL_PERMANENTLY_AT_WAYPOINT", "MAP_UNDO_REVEAL_PERMANENTLY_IN_TRIGGER":
			# Retail signature: [reveal handle]. The handle is what identifies
			# the reveal, so both undo opcodes are one branch.
			_remove_permanent_reveal(_text_at(arguments, 0, ARGUMENT_REVEAL_NAME))
		"MAP_REVEAL_ALL_PERM", "MAP_REVEAL_ALL_UNDO_PERM":
			# Retail signature: [player].
			for player_name: String in _reveal_players(
				_text_at(arguments, 0, ARGUMENT_PLAYER)
			):
				if opcode == "MAP_REVEAL_ALL_PERM":
					permanently_revealed_players[player_name] = true
				else:
					permanently_revealed_players.erase(player_name)
		"TEAM_TRANSFER_TO_PLAYER":
			# Retail signature: [team, player].
			_transfer_team_to_player(
				_text_at(arguments, 0, ARGUMENT_TEAM),
				_text_at(arguments, 1, ARGUMENT_PLAYER)
			)
		_:
			if RECORDED_ACTIONS.has(opcode):
				_record_presentation(opcode, arguments)
			else:
				runtime_unimplemented[opcode] = int(runtime_unimplemented.get(opcode, 0)) + 1


# --- Semantic helpers ------------------------------------------------------


func _set_counter(name: String, value: int) -> void:
	if counters.has(name) or counters.size() < MAX_COUNTERS:
		counters[name] = value
	else:
		_note_bound("counters")


func _arm_timer(name: String, duration_ticks: int) -> void:
	if timers.has(name) or timers.size() < MAX_TIMERS:
		timers[name] = tick_index + duration_ticks
	else:
		_note_bound("timers")


func _set_script_active(name: String, active: bool) -> void:
	if not _by_name.has(name):
		# Retail scripts routinely name a script in another player's list. We
		# cannot honour that from a single loaded environment, so record the
		# miss instead of pretending the toggle landed.
		_note_bound("unknown_script_name")
		return
	for index: int in (_by_name[name] as PackedInt32Array):
		(_scripts[index] as Dictionary)["active"] = active


func _call_subroutine(name: String, sim, depth: int) -> void:
	if depth >= MAX_SUBROUTINE_DEPTH:
		_note_bound("subroutine_depth")
		return
	if _subroutine_calls_this_tick >= MAX_SUBROUTINE_CALLS_PER_TICK:
		_note_bound("subroutine_calls")
		return
	if not _by_name.has(name):
		_note_bound("unknown_script_name")
		return
	for index: int in (_by_name[name] as PackedInt32Array):
		_subroutine_calls_this_tick += 1
		if _subroutine_calls_this_tick > MAX_SUBROUTINE_CALLS_PER_TICK:
			_note_bound("subroutine_calls")
			return
		_run_script(_scripts[index] as Dictionary, sim, depth + 1)


func _object_list_add(list_name: String, object_type: String) -> void:
	if not object_lists.has(list_name):
		if object_lists.size() >= MAX_OBJECT_LISTS:
			_note_bound("object_lists")
			return
		object_lists[list_name] = PackedStringArray()
	var entries: PackedStringArray = object_lists[list_name]
	if entries.size() >= MAX_OBJECT_LIST_ENTRIES:
		_note_bound("object_list_entries")
		return
	if entries.has(object_type):
		return
	entries.append(object_type)
	object_lists[list_name] = entries


# --- Named-object registry (B1) --------------------------------------------


func bind_named_object(object_name: String, entity_id: int) -> bool:
	## Binds an authored name to a live simulation entity. Hosts that
	## instantiate map objects call this; until they do, a registry row is
	## authored-but-unbound and only registry-level state applies to it.
	var row := _named_row(object_name)
	if row.is_empty() or bool(row["destroyed"]) or entity_id < 0:
		return false
	if int(row["entity_id"]) < 0:
		_bound_object_count += 1
	row["entity_id"] = entity_id
	return true


func unbind_named_object(object_name: String) -> void:
	var row := _named_row(object_name)
	if row.is_empty() or int(row["entity_id"]) < 0:
		return
	row["entity_id"] = -1
	_bound_object_count = maxi(0, _bound_object_count - 1)


func named_object_state(object_name: String) -> Dictionary:
	## Read-only view for presentation and tests. Never returns the live row.
	var row := _named_row(object_name)
	return row.duplicate(true) if not row.is_empty() else {}


func team_state(team_name: String) -> Dictionary:
	var row := _team_row(team_name)
	if row.is_empty():
		return {}
	var view := row.duplicate(true)
	view["living_members"] = _team_living_count(row)
	return view


func _named_row(object_name: String) -> Dictionary:
	if object_name.begins_with(UNNAMED_KEY_PREFIX):
		# A synthetic key for a script-created object retail gave no name. It is
		# a real registry row, but nothing may address it by name; a script that
		# somehow did is refused rather than served the wrong object.
		_note_bound("unnamed_object_key")
		return {}
	if object_name == "" or not named_objects.has(object_name):
		if object_name.begins_with("<"):
			_note_bound("scope_relative_object")
		elif object_name != "":
			# A script naming an object this mission never authored is a
			# binding failure, not a behaviour. Count it and fail closed.
			# Objects the scripts themselves create (B4) land here until a
			# creation path registers them.
			_note_bound("unknown_named_object")
		return {}
	return named_objects[object_name]


func _team_row(team_name: String) -> Dictionary:
	## Resolves a TEAM argument. Retail authors these either bare ("Attack
	## Team 1") or qualified with the owning player ("PlyrMordor/Attack Team
	## 1"); both name the same Teams entry, whose own name is the bare half.
	if team_name == "":
		return {}
	if script_teams.has(team_name):
		return script_teams[team_name]
	if team_name.begins_with("<"):
		# A scope-relative reference such as "<This Team>". The interpreter
		# has no executing-team scope to resolve it against, so it fails
		# closed and is counted apart from a genuinely unknown team.
		_note_bound("scope_relative_team")
		return {}
	var separator := team_name.rfind("/")
	if separator >= 0:
		var bare := team_name.substr(separator + 1)
		if script_teams.has(bare):
			return script_teams[bare]
	_note_bound("unknown_script_team")
	return {}


func _reconcile_bound_objects(sim) -> void:
	## Keeps registry rows honest about entities the simulation owns: a bound
	## entity that died or vanished marks its row destroyed, and a surviving
	## one refreshes the row's position. Iterates the stable name order, and
	## does nothing at all while no row is bound.
	if _bound_object_count <= 0 or sim == null:
		return
	for object_name: String in _named_object_order:
		var row: Dictionary = named_objects[object_name]
		var entity_id := int(row["entity_id"])
		if entity_id < 0:
			continue
		var entity: Dictionary = sim.entity(entity_id)
		if entity.is_empty() or int(entity.get("health", 0)) <= 0:
			row["destroyed"] = true
			row["entity_id"] = -1
			_bound_object_count = maxi(0, _bound_object_count - 1)
			continue
		if world_scale > 0.0:
			var position: Vector2 = entity.get("position", Vector2.ZERO)
			row["position"] = position * world_scale
		else:
			_note_bound("unscaled_world")


func _named_delete(object_name: String, sim) -> void:
	var row := _named_row(object_name)
	if row.is_empty() or bool(row["destroyed"]):
		return
	row["destroyed"] = true
	row["deleted"] = true
	row["order"] = {}
	if int(row["entity_id"]) >= 0:
		var entity_id := int(row["entity_id"])
		row["entity_id"] = -1
		_bound_object_count = maxi(0, _bound_object_count - 1)
		if sim != null and sim.entities.has(entity_id):
			# Retail removes the object outright, so the simulation row goes
			# with it; selection and control groups are reconciled so no
			# dangling id survives the removal.
			sim.entities.erase(entity_id)
			sim.selected_ids.erase(entity_id)
			sim.prune_control_groups()


func _set_named_attitude(object_name: String, attitude: int) -> void:
	var row := _named_row(object_name)
	if row.is_empty() or bool(row["destroyed"]):
		return
	row["attitude"] = _checked_attitude(attitude)


func _set_team_attitude(team_name: String, attitude: int) -> void:
	var row := _team_row(team_name)
	if row.is_empty():
		return
	var checked := _checked_attitude(attitude)
	row["attitude"] = checked
	for member: String in (row["members"] as PackedStringArray):
		var member_row: Dictionary = named_objects.get(member, {})
		if not member_row.is_empty() and not bool(member_row["destroyed"]):
			member_row["attitude"] = checked


func _checked_attitude(attitude: int) -> int:
	# The authored integer is always what gets stored; values outside the five
	# the shared SAGE core defines are recorded as unmapped so no behaviour
	# layer can silently read one as a neighbouring, known attitude.
	if attitude < ATTITUDE_KNOWN_MINIMUM or attitude > ATTITUDE_KNOWN_MAXIMUM:
		_note_bound("attitude_unmapped")
	return attitude


# --- Team registry (B2) ----------------------------------------------------


func _team_living_count(row: Dictionary) -> int:
	var living := int(row["anonymous_members"])
	for member: String in (row["members"] as PackedStringArray):
		var member_row: Dictionary = named_objects.get(member, {})
		if not member_row.is_empty() and not bool(member_row["destroyed"]):
			living += 1
	return living


func _merge_team(source_name: String, destination_name: String) -> void:
	if source_name == destination_name:
		return
	var source := _team_row(source_name)
	var destination := _team_row(destination_name)
	if source.is_empty() or destination.is_empty():
		return
	var members: PackedStringArray = destination["members"]
	for member: String in (source["members"] as PackedStringArray):
		if members.has(member):
			continue
		if members.size() >= MAX_TEAM_NAMED_MEMBERS:
			_note_bound("team_named_members")
			break
		members.append(member)
		var member_row: Dictionary = named_objects.get(member, {})
		if not member_row.is_empty():
			member_row["team"] = destination_name
			member_row["owner"] = String(destination["owner"])
	destination["members"] = members
	destination["anonymous_members"] = (
		int(destination["anonymous_members"]) + int(source["anonymous_members"])
	)
	source["members"] = PackedStringArray()
	source["anonymous_members"] = 0
	source["order"] = {}


func _set_named_team(object_name: String, team_name: String) -> void:
	var row := _named_row(object_name)
	var destination := _team_row(team_name)
	if row.is_empty() or destination.is_empty() or bool(row["destroyed"]):
		return
	var previous := _team_row(String(row["team"])) if String(row["team"]) != "" else {}
	if not previous.is_empty():
		var previous_members: PackedStringArray = previous["members"]
		var index := previous_members.find(object_name)
		if index >= 0:
			previous_members.remove_at(index)
			previous["members"] = previous_members
	var members: PackedStringArray = destination["members"]
	if not members.has(object_name):
		if members.size() >= MAX_TEAM_NAMED_MEMBERS:
			_note_bound("team_named_members")
			return
		members.append(object_name)
		destination["members"] = members
	row["team"] = team_name
	row["owner"] = String(destination["owner"])


func _transfer_team_to_player(team_name: String, player_name: String) -> void:
	var row := _team_row(team_name)
	if row.is_empty():
		return
	row["owner"] = player_name
	for member: String in (row["members"] as PackedStringArray):
		var member_row: Dictionary = named_objects.get(member, {})
		if not member_row.is_empty() and not bool(member_row["destroyed"]):
			member_row["owner"] = player_name


# --- Object creation (B4) --------------------------------------------------
#
# Retail campaign missions are built out of objects the scripts themselves
# create: a named hero or marker dropped on a team at a waypoint, an unnamed
# escort ship, or a whole reinforcement team instantiated from the Teams
# entry's authored unit composition. Every one of them becomes a registry row
# here, which is what makes the rest of this file work on it - a created object
# counts for TEAM_HAS_UNITS, carries an attitude, takes team and named orders,
# answers the trigger-area predicates from its position, and dies.
#
# A row is also offered to the simulation. RetailSliceSim.spawn_script_object()
# instantiates the object when the loaded content carries a rule for its retail
# type and returns -1 when it does not, which is the ordinary case for a
# campaign type that is not in the loaded faction slice. A row that could not be
# instantiated stays unbound: its orders defer and are counted exactly as
# before, and the type is recorded in `unavailable_object_types`. Nothing is
# ever substituted for a type the content does not have.


func instantiate_world_objects(sim) -> int:
	## Instantiates the mission's authored starting objects - the `world`
	## namedObjects rows - into the supplied simulation and binds each one that
	## the loaded content could produce. Hosts call this once, after
	## load_document() and before the first step(); it is opt-in, so a match
	## that never calls it behaves exactly as it did before.
	##
	## Returns the number of rows that reached the simulation. Iterates the
	## stable authored name order, so entity ids are allocated in the same
	## sequence on every peer.
	if sim == null:
		return 0
	var instantiated := 0
	for object_name: String in _named_object_order:
		var row: Dictionary = named_objects[object_name]
		if bool(row["destroyed"]) or int(row["entity_id"]) >= 0:
			continue
		if _instantiate_row(row, sim):
			instantiated += 1
	world_objects_instantiated += instantiated
	return instantiated


func _instantiate_row(row: Dictionary, sim) -> bool:
	## Offers one registry row to the simulation and binds it on success.
	if sim == null:
		return false
	var type_name := String(row["type_name"])
	if type_name == "":
		return false
	if not sim.has_method("spawn_script_object"):
		# An older or narrower simulation host. The row stays registry-only,
		# which is honest, rather than this pretending the object exists.
		_note_bound("simulation_cannot_spawn")
		return false
	var team := int(player_team_bindings.get(String(row["owner"]), default_player_team))
	var entity_id := int(sim.spawn_script_object(type_name, team, _sim_point(row["position"])))
	if entity_id < 0:
		unavailable_object_types[type_name] = int(
			unavailable_object_types.get(type_name, 0)
		) + 1
		return false
	row["entity_id"] = entity_id
	_bound_object_count += 1
	return true


func _new_created_row(
	object_name: String, type_name: String, owner: String, team_name: String, position: Vector2
) -> Dictionary:
	return {
		"name": object_name,
		"type_name": type_name,
		"owner": owner,
		"team": team_name,
		"position": position,
		"yaw": 0.0,
		"attitude": ATTITUDE_NORMAL,
		"destroyed": false,
		"deleted": false,
		"entity_id": -1,
		"order": {},
		"created": true,
	}


func _register_created_object(
	object_name: String,
	type_name: String,
	team: Dictionary,
	team_name: String,
	position: Vector2,
	sim
) -> String:
	## Adds one created object to the registry and to its team, and offers it to
	## the simulation. Returns the registry key, or "" when a bound refused it.
	if type_name == "":
		_note_bound("created_object_without_type")
		return ""
	var owner := String(team["owner"])
	var row := _new_created_row(object_name, type_name, owner, team_name, position)
	# The object joins the team at the team's current attitude, the way an
	# object authored onto that team would already be sitting at it.
	row["attitude"] = int(team["attitude"])
	if named_objects.has(object_name):
		# Retail lets a script re-create a name it has used before. The name now
		# addresses the new object; if the previous one is still alive it simply
		# loses its name - killing it would be a fabricated death - so the old
		# binding is released and the loss of addressability is counted.
		var previous: Dictionary = named_objects[object_name]
		if int(previous["entity_id"]) >= 0:
			_bound_object_count = maxi(0, _bound_object_count - 1)
		if not bool(previous["destroyed"]):
			_note_bound("recreated_live_named_object")
		# The name is about to mean something on another team, so it has to
		# leave the roster it used to be on; leaving it there would let the old
		# team keep counting a member it no longer has.
		var previous_team_name := String(previous["team"])
		if previous_team_name != "" and previous_team_name != team_name:
			var previous_team: Dictionary = script_teams.get(previous_team_name, {})
			if not previous_team.is_empty():
				var previous_members: PackedStringArray = previous_team["members"]
				var index := previous_members.find(object_name)
				if index >= 0:
					previous_members.remove_at(index)
					previous_team["members"] = previous_members
		named_objects[object_name] = row
	else:
		if _created_object_count >= MAX_CREATED_OBJECTS:
			_note_bound("created_objects")
			return ""
		if named_objects.size() >= MAX_NAMED_OBJECTS + MAX_CREATED_OBJECTS:
			_note_bound("named_objects")
			return ""
		_created_object_count += 1
		named_objects[object_name] = row
		_named_object_order.append(object_name)
	var members: PackedStringArray = team["members"]
	if not members.has(object_name):
		if members.size() >= MAX_TEAM_NAMED_MEMBERS:
			_note_bound("team_named_members")
		else:
			members.append(object_name)
			team["members"] = members
	_instantiate_row(row, sim)
	return object_name


func _next_unnamed_key() -> String:
	_created_object_ordinal += 1
	return "%s%d" % [UNNAMED_KEY_PREFIX, _created_object_ordinal]


func _create_named_on_team_at_waypoint(
	object_name: String, type_name: String, team_name: String, waypoint_name: String, sim
) -> void:
	if object_name == "" or object_name.begins_with("<"):
		# A scope-relative or empty name cannot become a registry key.
		_note_bound("created_object_without_name")
		return
	var team := _team_row(team_name)
	if team.is_empty():
		return
	if not waypoints.has(waypoint_name):
		_note_bound("unknown_waypoint")
		return
	_register_created_object(
		object_name, type_name, team, _team_key(team), waypoints[waypoint_name], sim
	)


func _create_unnamed_on_team_at_waypoint(
	type_name: String, team_name: String, waypoint_name: String, sim
) -> void:
	var team := _team_row(team_name)
	if team.is_empty():
		return
	if not waypoints.has(waypoint_name):
		_note_bound("unknown_waypoint")
		return
	_register_created_object(
		_next_unnamed_key(), type_name, team, _team_key(team), waypoints[waypoint_name], sim
	)


func _create_reinforcement_team(team_name: String, waypoint_name: String, sim) -> void:
	## Instantiates a Teams entry's authored unit composition at a waypoint.
	## Every member arrives at the waypoint point itself: retail spreads them
	## into a formation, and this interpreter has no evidence for that layout,
	## so it places them where the map says the team spawns rather than
	## inventing offsets.
	var team := _team_row(team_name)
	if team.is_empty():
		return
	if not waypoints.has(waypoint_name):
		_note_bound("unknown_waypoint")
		return
	var position: Vector2 = waypoints[waypoint_name]
	var units: Array[Dictionary] = team["units"]
	if units.is_empty():
		# A Teams entry with no authored composition creates nothing, which is
		# what retail does with an empty template. Counted so an empty spawn is
		# never mistaken for a binding failure.
		_note_bound("reinforcement_team_without_composition")
		return
	var created := 0
	for unit: Dictionary in units:
		# The authored min/max is a range retail draws inside. The draw comes
		# from this interpreter's own splitmix64 stream, never from engine RNG,
		# so every peer instantiates the same team.
		var count := _draw_inclusive(int(unit["min_count"]), int(unit["max_count"]))
		for index in range(count):
			if created >= MAX_REINFORCEMENT_TEAM_MEMBERS:
				_note_bound("reinforcement_team_members")
				return
			if _register_created_object(
				_next_unnamed_key(), String(unit["type"]), team, _team_key(team), position, sim
			) == "":
				return
			created += 1


func _team_key(team: Dictionary) -> String:
	## The name a team row is registered under, which is the bare authored team
	## name even when a script addressed it as "<player>/<team>".
	return String(team["name"])


# --- Orders ----------------------------------------------------------------
#
# An order is a plain dictionary describing what retail told the object to do.
# It is stored on the registry row and, when the row is bound to a live
# simulation entity, handed to the simulation. An order aimed at an authored
# object no host has instantiated yet is retained and counted in
# `deferred_orders`; it is never silently dropped and never faked.


func _waypoint_order(opcode: String, waypoint_name: String) -> Dictionary:
	if not waypoints.has(waypoint_name):
		_note_bound("unknown_waypoint")
		return {}
	var kind := "move"
	if opcode.begins_with("ATTACK_MOVE"):
		kind = "attack_move"
	elif opcode == "TEAM_FACE_WAYPOINT":
		kind = "face"
	return {
		"kind": kind,
		"opcode": opcode,
		"waypoint": waypoint_name,
		"destination": waypoints[waypoint_name],
		"tick": tick_index,
	}


func _waypoint_path_order(opcode: String, label: String, exact: bool, attack: bool) -> Dictionary:
	if not waypoint_paths.has(label):
		# Either the path is not authored on this map or the converter could
		# not order it. Both are refusals, not approximations.
		_note_bound("unknown_waypoint_path")
		return {}
	return {
		"kind": "follow_waypoints",
		"opcode": opcode,
		"path": label,
		"points": (waypoint_paths[label] as Array[Vector2]).duplicate(),
		"index": 0,
		"exact": exact,
		"attack": attack,
		"tick": tick_index,
	}


func _order_named(object_name: String, order: Dictionary, opcode: String, sim) -> void:
	if order.is_empty():
		return
	var row := _named_row(object_name)
	if row.is_empty() or bool(row["destroyed"]):
		return
	# Each recipient gets its own copy: an order carries per-object progress
	# (the waypoint-path leg index), so sharing one dictionary would advance
	# every member of a team at once.
	var owned := order.duplicate(true)
	row["order"] = owned
	_dispatch_order(row, owned, opcode, sim)


func _order_team(team_name: String, order: Dictionary, opcode: String, sim) -> void:
	if order.is_empty():
		return
	var team := _team_row(team_name)
	if team.is_empty():
		return
	team["order"] = order.duplicate(true)
	for member: String in (team["members"] as PackedStringArray):
		var member_row: Dictionary = named_objects.get(member, {})
		if member_row.is_empty() or bool(member_row["destroyed"]):
			continue
		var owned := order.duplicate(true)
		member_row["order"] = owned
		_dispatch_order(member_row, owned, opcode, sim)
	if int(team["anonymous_members"]) > 0:
		# The team also holds authored objects with no name. They have no
		# registry row to carry an order, so the shortfall is counted.
		_note_deferred(opcode)


func _dispatch_order(row: Dictionary, order: Dictionary, opcode: String, sim) -> void:
	var kind := String(order["kind"])
	if kind == "face":
		# Facing is registry state and needs no simulation entity: the object's
		# authored position and the waypoint fully determine it.
		var facing: Vector2 = order["destination"] - (row["position"] as Vector2)
		if facing.length_squared() > 0.0:
			row["yaw"] = atan2(facing.y, facing.x)
		return
	var entity_id := int(row["entity_id"])
	if entity_id < 0 or sim == null:
		_note_deferred(opcode)
		return
	var team := int(player_team_bindings.get(String(row["owner"]), default_player_team))
	var ids: Array[int] = [entity_id]
	match kind:
		"move":
			sim.issue_move(ids, _sim_point(order["destination"]), "order.move", team)
		"attack_move":
			sim.issue_attack_move(ids, _sim_point(order["destination"]), team)
		"follow_waypoints":
			var points: Array[Vector2] = order["points"]
			if points.is_empty():
				_note_deferred(opcode)
				return
			# The simulation carries one destination per order, so the path is
			# walked a leg at a time; the remaining legs stay on the row.
			if bool(order["attack"]):
				sim.issue_attack_move(ids, _sim_point(points[0]), team)
			else:
				sim.issue_move(ids, _sim_point(points[0]), "order.move", team)
		_:
			# hunt and attack_object have no simulation order yet. They are
			# real registry state and are counted as not yet dispatched.
			_note_deferred(opcode)


func _sim_point(document_point: Vector2) -> Vector2:
	return document_point / world_scale if world_scale > 0.0 else document_point


func _note_deferred(opcode: String) -> void:
	deferred_orders[opcode] = int(deferred_orders.get(opcode, 0)) + 1


# --- Trigger-area spatial predicates (B3) ----------------------------------
#
# Retail answers these over every object a player owns. This interpreter's view
# of "every object a player owns" is the union of two sets, and nothing else:
#
#   * registry rows - the map's authored named objects and every object the
#     scripts created (B4) - which carry a retail type name and a document
#     -space position, and are unbound unless a host instantiated them; and
#   * live simulation entities on the player's team, whose `object_id` is the
#     retail type name the loaded content compiled them from.
#
# A bound row is skipped on the registry pass, because the simulation entity it
# is bound to is counted on the second pass; a row is therefore never counted
# twice and never missed.
#
# The simulation's *structures* are deliberately outside this index. They carry
# a slice-level `structure_kind` ("farm", "barracks"), not a retail object type,
# so a type-matched query cannot honestly test them; when a query's area holds
# any living structure of the queried player the shortfall is counted in
# bounds_hit["untyped_structure_in_area"] rather than silently answered from a
# partial world.
#
# Every object this index can see is complete - authored map objects start
# built, and a script-created object is created built - so
# PLAYER_HAS_COMPARISON_UNIT_TYPE_IN_TRIGGER_AREA_COMPLETELY_BUILT and its
# plain sibling necessarily agree over it. That is why both are honoured by one
# branch rather than one of them modelling a construction state this world has
# no representation for.


func _accepted_object_types(type_or_list: String) -> PackedStringArray:
	## Resolves an ARGUMENT_OBJECT_TYPE_OR_LIST slot into the retail type names
	## it accepts. A bare type name accepts itself; a name a script built with
	## OBJECTLIST_ADDOBJECTTYPE accepts every type on that list, because a
	## counting predicate over a set of types has no coherent reading other
	## than their union. An empty result means the slot could not be resolved
	## and the caller fails closed.
	if type_or_list == "":
		_note_bound("object_type_slot_unreadable")
		return PackedStringArray()
	if object_lists.has(type_or_list):
		var entries: PackedStringArray = object_lists[type_or_list]
		if entries.is_empty():
			# The list is named but nothing has been added to it yet, so no
			# object can match it. Counted, since an empty list and an
			# unresolvable one are different failures.
			_note_bound("empty_object_list_in_area_query")
			return PackedStringArray()
		return entries
	return PackedStringArray([type_or_list])


func _resolve_player(player_reference: String) -> String:
	## Resolves a PLAYER argument to a SidesList player name, or "" when it
	## cannot be resolved. "<Local Player>" needs the host to have set
	## `local_player`; every other angle-bracketed reference is a scope this
	## interpreter has no executing scope to resolve against.
	if player_reference == "":
		return ""
	if not player_reference.begins_with("<"):
		return player_reference
	if player_reference == "<Local Player>" and local_player != "":
		return local_player
	_note_bound("scope_relative_player")
	return ""


func _begin_area_query(area_name: String) -> PackedVector2Array:
	## Shared entry point for every area predicate: resolves the polygon and
	## spends one of the tick's query budget. Returns an empty polygon when the
	## area is unknown or the budget is exhausted, and the caller fails closed.
	if not trigger_areas.has(area_name):
		_note_bound("unknown_trigger_area")
		return PackedVector2Array()
	if _area_queries_this_tick >= MAX_AREA_QUERIES_PER_TICK:
		_note_bound("area_queries")
		return PackedVector2Array()
	_area_queries_this_tick += 1
	return trigger_areas[area_name]


func _count_player_objects_in_area(
	player: String, polygon: PackedVector2Array, accepted_types: PackedStringArray, sim
) -> int:
	## Objects `player` owns inside `polygon`, restricted to the retail type
	## names in `accepted_types` (an empty array accepts any type). Returns -1
	## when the query could not be answered.
	if player == "" or polygon.size() < 3:
		return -1
	var any_type := accepted_types.is_empty()
	var team := int(player_team_bindings.get(player, -1)) if sim != null else -1
	# A bound row is normally counted on the simulation pass below, from the
	# entity's own position. When there is no simulation pass - no host, or a
	# player this host never mapped onto a simulation team - the bound rows are
	# counted here instead, from the position the reconcile step keeps current,
	# so they are never dropped and never counted twice.
	var count_bound_rows := team < 0
	var scanned := 0
	var total := 0
	for object_name: String in _named_object_order:
		scanned += 1
		if scanned > MAX_AREA_QUERY_SCAN:
			_note_bound("area_query_scan")
			return -1
		var row: Dictionary = named_objects[object_name]
		if bool(row["destroyed"]):
			continue
		if int(row["entity_id"]) >= 0 and not count_bound_rows:
			continue
		if String(row["owner"]) != player:
			continue
		if not any_type and not accepted_types.has(String(row["type_name"])):
			continue
		if _point_inside_polygon(row["position"], polygon):
			total += 1
	if sim == null:
		return total
	if team < 0:
		# The host never told us which simulation team this player is, so the
		# simulation half of the index cannot be read. The registry half above
		# is still exact, and the gap is counted.
		_note_bound("unbound_player_team")
		return total
	for entity_id: int in sim.entity_ids():
		scanned += 1
		if scanned > MAX_AREA_QUERY_SCAN:
			_note_bound("area_query_scan")
			return -1
		var entity: Dictionary = sim.entity(entity_id)
		if entity.is_empty() or int(entity.get("health", 0)) <= 0:
			continue
		if int(entity.get("team", -1)) != team:
			continue
		if not any_type and not accepted_types.has(String(entity.get("object_id", ""))):
			continue
		if _point_inside_polygon(_document_space(entity.get("position", Vector2.ZERO)), polygon):
			total += 1
	_note_untyped_structures(team, polygon, sim)
	return total


func _note_untyped_structures(team: int, polygon: PackedVector2Array, sim) -> void:
	## Records living structures of `team` inside the polygon. They carry no
	## retail object type, so they can neither be counted nor matched; counting
	## the shortfall keeps a partial answer visibly partial.
	if sim == null or not (sim.structures is Dictionary):
		return
	# Sorted, so the scan order is identical on every peer even though this
	# only ever increments a counter.
	var structure_ids: Array = (sim.structures as Dictionary).keys()
	structure_ids.sort()
	for id_value: Variant in structure_ids:
		var structure: Dictionary = sim.structures[id_value]
		if int(structure.get("health", 0)) <= 0:
			continue
		if int(structure.get("team", -1)) != team:
			continue
		if _point_inside_polygon(
			_document_space(structure.get("position", Vector2.ZERO)), polygon
		):
			_note_bound("untyped_structure_in_area")


func _document_space(simulation_point: Vector2) -> Vector2:
	return simulation_point * world_scale if world_scale > 0.0 else simulation_point


func _team_area_filter_ok(filter_value: int) -> bool:
	## The third TEAM_INSIDE_AREA_* slot. Only its dominant authored value is
	## admitted; a value whose meaning the corpus does not establish refuses the
	## predicate instead of being read as a neighbouring, known one.
	if filter_value == AREA_MEMBER_FILTER_ALL:
		return true
	_note_bound("team_area_filter_unmapped")
	return false


func _team_members_inside_area(team: Dictionary, polygon: PackedVector2Array, sim) -> Vector2i:
	## Returns (inside, living) over the team's tracked member rows, or (-1, -1)
	## when the team holds authored objects this registry never rowed, because
	## an untracked member could be inside the area and neither answer would be
	## true. Named members and script-created members are all tracked; only
	## unnamed objects an authored map put on the team are not.
	if int(team["anonymous_members"]) > 0:
		_note_bound("team_untracked_members")
		return Vector2i(-1, -1)
	var inside := 0
	var living := 0
	for member: String in (team["members"] as PackedStringArray):
		var row: Dictionary = named_objects.get(member, {})
		if row.is_empty() or bool(row["destroyed"]):
			continue
		living += 1
		var position: Vector2 = row["position"]
		if int(row["entity_id"]) >= 0 and sim != null:
			var entity: Dictionary = sim.entity(int(row["entity_id"]))
			if not entity.is_empty():
				position = _document_space(entity.get("position", Vector2.ZERO))
		if _point_inside_polygon(position, polygon):
			inside += 1
	return Vector2i(inside, living)


func _point_inside_polygon(point: Vector2, polygon: PackedVector2Array) -> bool:
	## Even-odd ray crossing, written out rather than delegated to Geometry2D so
	## the predicate is a fixed sequence of float comparisons on every peer.
	if polygon.size() < 3:
		return false
	var inside := false
	var count := polygon.size()
	var previous := polygon[count - 1]
	for index in range(count):
		var current := polygon[index]
		if (current.y > point.y) != (previous.y > point.y):
			var span := previous.y - current.y
			if span != 0.0:
				var crossing := current.x + (point.y - current.y) * (previous.x - current.x) / span
				if point.x < crossing:
					inside = not inside
		previous = current
	return inside


# --- Audio completion (B6) -------------------------------------------------


func set_audio_event_durations(durations: Dictionary) -> void:
	## Supplies authored audio-event lengths, in seconds, keyed by event name.
	## A host that has a cooked audio pack calls this once after load_document()
	## and before the first step(); non-positive and non-numeric lengths are
	## refused rather than stored, so a corrupt table cannot invent pacing.
	audio_event_durations.clear()
	var names: Array = durations.keys()
	names.sort()
	for key: Variant in names:
		var event_name := String(key)
		var seconds := float(durations[key])
		if event_name == "" or seconds <= 0.0 or not is_finite(seconds):
			_note_bound("audio_event_length_refused")
			continue
		audio_event_durations[event_name] = seconds


func _audio_wait_complete(event_name: String) -> bool:
	## Retail HAS_FINISHED_AUDIO. First evaluation files the wait, later ones
	## retire it. An audio event with no authored length completes on its first
	## evaluation, matching retail's zero-length reading of an event the loaded
	## content does not carry.
	if event_name == "":
		_note_bound("audio_event_unreadable")
		return false
	if not _audio_waits.has(event_name):
		if _audio_waits.size() >= MAX_AUDIO_WAITS:
			# The pending list is full. Refuse rather than grow it; a refused
			# wait is not complete, so the mission holds instead of racing.
			_note_bound("audio_waits")
			return false
		var duration_ticks := 0
		if audio_event_durations.has(event_name):
			duration_ticks = int(floor(
				float(audio_event_durations[event_name]) / TICK_SECONDS
			))
		else:
			_note_bound("audio_event_length_unknown")
		_audio_waits[event_name] = tick_index + maxi(0, duration_ticks)
	if tick_index < int(_audio_waits[event_name]):
		return false
	_audio_waits.erase(event_name)
	return true


# --- Shroud, permanent reveals and discovery -------------------------------


func _reveal_players(player_reference: String) -> PackedStringArray:
	## Resolves a reveal's PLAYER slot. "<All Players>" is the one scope
	## -relative reference the reveal family authors that needs no executing
	## scope to read, so it expands to the mission's SidesList players in their
	## authored order; everything else goes through _resolve_player and fails
	## closed exactly as it does elsewhere.
	var players := PackedStringArray()
	if player_reference == PLAYER_ALL_REFERENCE:
		for row: Variant in Array(world.get("players", [])):
			var player_name := String((row as Dictionary).get("name", ""))
			if player_name != "" and not players.has(player_name):
				players.append(player_name)
		if players.is_empty():
			_note_bound("reveal_without_players")
		return players
	var resolved := _resolve_player(player_reference)
	if resolved != "":
		players.append(resolved)
	return players


func _add_permanent_reveal(handle: String, player_reference: String, shape: Dictionary) -> void:
	## Files one named permanent reveal. The handle is the mission-wide key its
	## MAP_UNDO_REVEAL_PERMANENTLY_* sibling removes; retail lets a handle be
	## reused, and the latest filing wins.
	if handle == "":
		_note_bound("reveal_without_handle")
		return
	if String(shape.get("kind", "")) == "circle":
		var waypoint_name := String(shape["waypoint"])
		if not waypoints.has(waypoint_name):
			_note_bound("unknown_waypoint")
			return
		if float(shape["radius"]) <= 0.0:
			# A zero or negative radius reveals nothing. Refused rather than
			# stored, so it can never read as an unbounded reveal.
			_note_bound("reveal_radius_unreadable")
			return
		shape["center"] = waypoints[waypoint_name]
	else:
		if not trigger_areas.has(String(shape["area"])):
			_note_bound("unknown_trigger_area")
			return
	var players := _reveal_players(player_reference)
	if players.is_empty():
		return
	if not permanent_reveals.has(handle):
		if permanent_reveals.size() >= MAX_PERMANENT_REVEALS:
			_note_bound("permanent_reveals")
			return
		_permanent_reveal_order.append(handle)
	shape["players"] = players
	permanent_reveals[handle] = shape


func _remove_permanent_reveal(handle: String) -> void:
	if handle == "" or not permanent_reveals.has(handle):
		# Retail campaigns undo handles they never filed; the miss is counted
		# rather than silently ignored, because it is also what a binding
		# failure would look like.
		_note_bound("unknown_reveal_handle")
		return
	permanent_reveals.erase(handle)
	var index := _permanent_reveal_order.find(handle)
	if index >= 0:
		_permanent_reveal_order.remove_at(index)


func _position_discovered(player: String, point: Vector2, sim) -> int:
	## 1 when `point` is unshrouded for `player`, 0 when it is shrouded, -1 when
	## the query could not be answered at all.
	if player == "":
		return -1
	if _discovery_queries_this_tick >= MAX_DISCOVERY_QUERIES_PER_TICK:
		_note_bound("discovery_queries")
		return -1
	_discovery_queries_this_tick += 1
	if not _stealth_shortfall_noted:
		# Retail also excludes held and undetected-stealthed objects. This
		# simulation carries neither state, so the exclusion cannot be applied;
		# it is recorded once so the gap is visible without flooding the count.
		_stealth_shortfall_noted = true
		_note_bound("discovery_ignores_stealth")
	if bool(permanently_revealed_players.get(player, false)):
		return 1
	for handle: String in _permanent_reveal_order:
		var reveal: Dictionary = permanent_reveals[handle]
		if not (reveal["players"] as PackedStringArray).has(player):
			continue
		if String(reveal["kind"]) == "circle":
			var radius := float(reveal["radius"])
			if (point - (reveal["center"] as Vector2)).length_squared() <= radius * radius:
				return 1
		else:
			var polygon: PackedVector2Array = trigger_areas.get(
				String(reveal["area"]), PackedVector2Array()
			)
			if _point_inside_polygon(point, polygon):
				return 1
	# Sight of the player's own living entities. `vision_range` is compiled in
	# simulation units, so it is lifted into document space before comparison.
	var team := int(player_team_bindings.get(player, -1)) if sim != null else -1
	if team >= 0:
		var scanned := 0
		for entity_id: int in sim.entity_ids():
			scanned += 1
			if scanned > MAX_DISCOVERY_QUERY_SCAN:
				_note_bound("discovery_query_scan")
				return -1
			var entity: Dictionary = sim.entity(entity_id)
			if entity.is_empty() or int(entity.get("health", 0)) <= 0:
				continue
			if int(entity.get("team", -1)) != team:
				continue
			var sight := float(entity.get("vision_range", 0.0)) * world_scale
			if sight <= 0.0:
				continue
			var origin := _document_space(entity.get("position", Vector2.ZERO))
			if (point - origin).length_squared() <= sight * sight:
				return 1
	# Nothing this interpreter can see reveals the point. Say so, but count the
	# sight the world was unable to contribute. The test is deliberately the
	# whole-registry one rather than a per-player scan: it is O(1), and a
	# shortfall counter that over-reports is safe where one that under-reports
	# would let a false negative pass as a complete answer.
	if team < 0:
		_note_bound("unbound_player_team")
	elif _bound_object_count < named_objects.size():
		_note_bound("discovery_vision_unavailable")
	return 0


func _team_discovered(team: Dictionary, player: String, sim) -> bool:
	if team.is_empty() or player == "":
		return false
	if int(team["anonymous_members"]) > 0:
		# The team holds authored objects with no registry row. One of them
		# could be the discovered member, so neither answer is safe.
		_note_bound("team_untracked_members")
		return false
	for member: String in (team["members"] as PackedStringArray):
		var row: Dictionary = named_objects.get(member, {})
		if row.is_empty() or bool(row["destroyed"]):
			continue
		var position: Vector2 = row["position"]
		if int(row["entity_id"]) >= 0 and sim != null:
			var entity: Dictionary = sim.entity(int(row["entity_id"]))
			if not entity.is_empty():
				position = _document_space(entity.get("position", Vector2.ZERO))
		if _position_discovered(player, position, sim) == 1:
			return true
	return false


# --- Presentation recording -----------------------------------------------


func _record_presentation(opcode: String, arguments: Array) -> void:
	var event := {"tick": tick_index, "opcode": opcode}
	match opcode:
		"SHOW_MISSION_OBJECTIVE":
			var shown := _int_at(arguments, 0, ARGUMENT_INTEGER, 0)
			_objective(shown)["shown"] = true
			event["objective"] = shown
		"HIDE_MISSION_OBJECTIVE":
			var hidden := _int_at(arguments, 0, ARGUMENT_INTEGER, 0)
			_objective(hidden)["shown"] = false
			event["objective"] = hidden
		"MARK_MISSION_OBJECTIVE_COMPLETED":
			var done := _int_at(arguments, 0, ARGUMENT_INTEGER, 0)
			_objective(done)["completed"] = true
			event["objective"] = done
		"FLASH_OBJECTIVES_BUTTON":
			event["objective"] = _int_at(arguments, 0, ARGUMENT_INTEGER, 0)
		"DISPLAY_NOTIFICATION_BOX":
			event["kind"] = _text_at(arguments, 0, ARGUMENT_NOTIFICATION_KIND)
			event["text"] = _text_at(arguments, 1, ARGUMENT_LOCALIZED_STRING)
		"SHOW_MILITARY_CAPTION":
			event["text"] = _text_at(arguments, 0, ARGUMENT_LOCALIZED_STRING)
			event["seconds"] = _real_at(arguments, 1, ARGUMENT_REAL, 0.0)
		"DISPLAY_COUNTDOWN_TIMER":
			event["timer"] = _text_at(arguments, 0, ARGUMENT_COUNTER_NAME)
			event["text"] = _text_at(arguments, 1, ARGUMENT_LOCALIZED_STRING)
		"SPEECH_PLAY":
			event["speech"] = _text_at(arguments, 0, ARGUMENT_DIALOG)
		"PLAY_SOUND_EFFECT", "PLAY_SOUND_EFFECT_AT", "PLAY_SOUND_EFFECT_AT_TEAM", "SOUND_PLAY_NAMED":
			event["sound"] = _text_at(arguments, 0, ARGUMENT_SOUND)
		"MOVE_CAMERA_TO":
			event["target"] = _text_at(arguments, 0, ARGUMENT_CAMERA_WAYPOINT)
			event["seconds"] = _real_at(arguments, 1, ARGUMENT_REAL, 0.0)
		"RESET_CAMERA":
			event["target"] = _text_at(arguments, 0, ARGUMENT_WAYPOINT)
			event["seconds"] = _real_at(arguments, 1, ARGUMENT_REAL, 0.0)
		# --- Minimap ping family. Retail draws a radar event; nothing else. ---
		"OBJECT_CREATE_RADAR_EVENT":
			event["object"] = _text_at(arguments, 0, ARGUMENT_UNIT_NAME)
			event["radarEvent"] = _text_at(arguments, 1, ARGUMENT_RADAR_EVENT_TYPE)
		"TEAM_CREATE_RADAR_EVENT":
			event["team"] = _text_at(arguments, 0, ARGUMENT_TEAM)
			event["radarEvent"] = _text_at(arguments, 1, ARGUMENT_RADAR_EVENT_TYPE)
		"RADAR_CREATE_EVENT":
			event["position"] = _position_argument(arguments, 0)
			event["radarEvent"] = _text_at(arguments, 1, ARGUMENT_RADAR_EVENT_TYPE)
		# --- Audio mixer and music scripting. ---
		"ENABLE_OBJECT_SOUND":
			event["object"] = _text_at(arguments, 0, ARGUMENT_UNIT_NAME)
		"AUDIO_MAKE_SOUND_IMMUNE_TO_FADE", "SOUND_DISABLE_TYPE":
			event["audioEvent"] = _text_at(arguments, 0, ARGUMENT_SOUND_TYPE)
		"AUDIO_SET_REVERB_ROOM_TYPE":
			event["roomType"] = _text_at(arguments, 0, ARGUMENT_REVERB_ROOM_TYPE)
		"AUDIO_SET_REVERB_SUPPRESSION_POLYGON":
			event["area"] = _text_at(arguments, 0, ARGUMENT_TRIGGER_AREA)
		"AUDIO_PUSH_MUSIC", "MUSIC_PLAY_TRACK_FINITE_TIMES", "MUSIC_PLAY_TRACK_FINITE_TIMES_AND_NOTIFY":
			event["track"] = _text_at(arguments, 0, ARGUMENT_MUSIC_TRACK)
		"EVA_SET_ENABLED_DISABLED":
			event["enabled"] = _int_at(arguments, 0, ARGUMENT_BOOLEAN, 0) != 0
		# --- Cameo, unit and HUD-button flashes. ---
		"CAMEO_FLASH":
			event["commandButton"] = _text_at(arguments, 0, ARGUMENT_COMMAND_BUTTON)
			event["count"] = _int_at(arguments, 1, ARGUMENT_INTEGER, 0)
		"NAMED_FLASH", "NAMED_FLASH_WHITE":
			event["object"] = _text_at(arguments, 0, ARGUMENT_UNIT_NAME)
			event["count"] = _int_at(arguments, 1, ARGUMENT_INTEGER, 0)
		"TEAM_FLASH", "TEAM_FLASH_WHITE":
			event["team"] = _text_at(arguments, 0, ARGUMENT_TEAM)
			event["count"] = _int_at(arguments, 1, ARGUMENT_INTEGER, 0)
		"SELECT_BUILDER_BUTTON_FLASH", "FLASH_SPELL_STORE_BUTTON":
			event["count"] = _int_at(arguments, 0, ARGUMENT_INTEGER, 0)
		"HERO_SELECT_BUTTON_FLASH":
			event["hero"] = _text_at(arguments, 0, ARGUMENT_HERO_BUTTON)
			event["count"] = _int_at(arguments, 1, ARGUMENT_INTEGER, 0)
		# --- Cinematic camera. ---
		"CAMERA_FADE_ADD", "CAMERA_FADE_MULTIPLY", "CAMERA_FADE_SUBTRACT":
			event["from"] = _real_at(arguments, 0, ARGUMENT_REAL, 0.0)
			event["to"] = _real_at(arguments, 1, ARGUMENT_REAL, 0.0)
		"CAMERA_FOLLOW_NAMED", "CAMERA_LOOK_TOWARD_OBJECT":
			event["object"] = _text_at(arguments, 0, ARGUMENT_UNIT_NAME)
		"CAMERA_MOD_LOOK_TOWARD", "CAMERA_LOOK_TOWARD_WAYPOINT":
			event["target"] = _text_at(arguments, 0, ARGUMENT_WAYPOINT)
		"SETUP_CAMERA":
			event["target"] = _text_at(arguments, 0, ARGUMENT_WAYPOINT)
			event["lookAt"] = _text_at(arguments, 3, ARGUMENT_WAYPOINT)
		"CAMERA_RESTRICT_TO_AREA":
			event["area"] = _text_at(arguments, 0, ARGUMENT_TRIGGER_AREA)
		"MOVE_CAMERA_ALONG_SPLINE_PATH":
			event["path"] = _text_at(arguments, 0, ARGUMENT_WAYPOINT_PATH)
			event["seconds"] = _real_at(arguments, 1, ARGUMENT_REAL, 0.0)
		"MOVE_CAMERA_BY_ANIMATION":
			event["animation"] = _text_at(arguments, 0, ARGUMENT_CAMERA_ANIMATION)
		"ROTATE_CAMERA", "PITCH_CAMERA", "FOCAL_LENGTH_CAMERA":
			event["amount"] = _real_at(arguments, 0, ARGUMENT_REAL, 0.0)
			event["seconds"] = _real_at(arguments, 1, ARGUMENT_REAL, 0.0)
		"SCREEN_SHAKE":
			event["intensity"] = _int_at(arguments, 0, ARGUMENT_SCREEN_SHAKE_INTENSITY, 0)
		"LOCK_CAMERA":
			event["locked"] = _int_at(arguments, 0, ARGUMENT_BOOLEAN, 0) != 0
		# --- HUD widgets and in-game movies. ---
		"DISPLAY_COUNTER":
			event["counter"] = _text_at(arguments, 0, ARGUMENT_COUNTER_NAME)
			event["text"] = _text_at(arguments, 1, ARGUMENT_LOCALIZED_STRING)
		"HIDE_COUNTER", "HIDE_COUNTDOWN_TIMER":
			event["counter"] = _text_at(arguments, 0, ARGUMENT_COUNTER_NAME)
		"DISPLAY_NOTIFICATION_BOX_WITH_OBJECT_TYPE_IMAGE_OVERRIDE":
			event["kind"] = _text_at(arguments, 0, ARGUMENT_NOTIFICATION_KIND)
			event["text"] = _text_at(arguments, 1, ARGUMENT_LOCALIZED_STRING)
			event["image"] = _text_at(arguments, 3, ARGUMENT_OBJECT_TYPE)
		"PLAY_MOVIE_IN_GAME":
			event["movie"] = _text_at(arguments, 0, ARGUMENT_MOVIE)
		"ENABLE_HOUSE_COLOR":
			event["enabled"] = _int_at(arguments, 0, ARGUMENT_BOOLEAN, 0) != 0
		"DISABLE_INPUT":
			input_enabled = false
		"ENABLE_INPUT":
			input_enabled = true
		"CAMERA_LETTERBOX_BEGIN":
			letterbox_active = true
		"CAMERA_LETTERBOX_END":
			letterbox_active = false
		"VICTORY", "QUICKVICTORY", "VICTORY_SCREEN":
			if outcome == "":
				outcome = "victory"
		"DEFEAT":
			if outcome == "":
				outcome = "defeat"
	if events.size() >= MAX_EVENTS:
		events_dropped += 1
		return
	events.append(event)


func _objective(id: int) -> Dictionary:
	if not mission_objectives.has(id):
		mission_objectives[id] = {"shown": false, "completed": false}
	return mission_objectives[id]


func _note_bound(reason: String) -> void:
	bounds_hit[reason] = int(bounds_hit.get(reason, 0)) + 1


# --- Deterministic draw stream --------------------------------------------


func _seed_from_text(text: String) -> int:
	# FNV-1a over the document identity, so the stream is a pure function of
	# the mission and never of wall-clock time or engine RNG state.
	var hash_value := 0x2545F4914F6CDD1D
	for index in range(text.length()):
		hash_value ^= text.unicode_at(index)
		hash_value *= 0x100000001B3
	if hash_value == 0:
		hash_value = 0x9E3779B97F4A7C15
	return hash_value


func _next_random() -> int:
	# splitmix64. Integer-only, so it is bit-identical on every platform.
	_rng_state += 0x9E3779B97F4A7C15
	var z := _rng_state
	z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9
	z = (z ^ (z >> 27)) * 0x94D049BB133111EB
	return z ^ (z >> 31)


func _draw_inclusive(low: int, high: int) -> int:
	var lower := mini(low, high)
	var upper := maxi(low, high)
	var span := upper - lower + 1
	if span <= 1:
		return lower
	var draw := _next_random()
	if draw < 0:
		draw = -(draw + 1)
	return lower + (draw % span)


# --- Argument access -------------------------------------------------------
#
# Retail argument lists are positional. Each accessor takes both the slot index
# and the argumentType the retail corpus proves belongs in that slot, and
# fails closed to the supplied fallback when the wire disagrees, so a decoded
# shape we have not seen cannot be silently misread as a different type.


func _argument_at(arguments: Array, index: int, argument_type: int) -> Dictionary:
	if index < 0 or index >= arguments.size():
		return {}
	var row: Dictionary = arguments[index]
	if int(row.get("argumentType", -1)) != argument_type:
		return {}
	return row


func _text_at(arguments: Array, index: int, argument_type: int) -> String:
	return String(_argument_at(arguments, index, argument_type).get("text", ""))


func _int_at(arguments: Array, index: int, argument_type: int, fallback: int) -> int:
	var row := _argument_at(arguments, index, argument_type)
	return int(row.get("integer", fallback)) if not row.is_empty() else fallback


func _real_at(arguments: Array, index: int, argument_type: int, fallback: float) -> float:
	var row := _argument_at(arguments, index, argument_type)
	return float(row.get("real", fallback)) if not row.is_empty() else fallback


func _position_argument(arguments: Array, index: int) -> Vector2:
	## An ARGUMENT_POSITION slot, lifted into the same plan-view document space
	## the registry uses. The wire carries the raw retail triple, while the
	## converter's godotPosition is (x, z, -y); the plan view is therefore
	## (x, -y). A slot the wire did not shape as a position reads as the origin,
	## which is what the fallback accessors do everywhere else.
	var row := _argument_at(arguments, index, ARGUMENT_POSITION)
	var triple: Array = Array(row.get("position", []))
	if triple.size() < 2:
		return Vector2.ZERO
	return Vector2(float(triple[0]), -float(triple[1]))


func _compare(left: int, comparison: int, right: int) -> bool:
	match comparison:
		COMPARE_LESS:
			return left < right
		COMPARE_LESS_EQUAL:
			return left <= right
		COMPARE_EQUAL:
			return left == right
		COMPARE_GREATER_EQUAL:
			return left >= right
		COMPARE_GREATER:
			return left > right
		COMPARE_NOT_EQUAL:
			return left != right
	return false


func _sum_values(histogram: Dictionary) -> int:
	var total := 0
	for value: Variant in histogram.values():
		total += int(value)
	return total
