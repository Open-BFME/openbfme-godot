class_name RetailSliceAudio
extends Node

const MAX_OBSERVABILITY_LOG_ENTRIES := 2048
const OBSERVABILITY_LOG_TRIM_COUNT := 512
## Routes deterministic simulation intents through contained retail audio
## manifests. Logical SAGE event IDs, not filename guesses, select the leaves.

const UserSettingsScript = preload("res://src/ui/user_settings.gd")
const PlayableUnitAdapter = preload("res://src/retail_slice/playable_unit_runtime_adapter.gd")
const ClipFrameClockScript = preload("res://src/retail_slice/clip_frame_clock.gd")
const MusicDirectorScript = preload("res://src/core/music_director.gd")
const DiagLogScript = preload("res://src/core/diag_log.gd")

const SOLDIER_OBJECT_ID := "bfme2.object.gondor-fighter"
const ARCHER_OBJECT_ID := "bfme2.object.gondor-archer"
const TOWER_GUARD_OBJECT_ID := "bfme2.object.gondor-tower-guard"
const KNIGHT_OBJECT_ID := "bfme2.object.gondor-knight"
const ROSTER_OBJECT_IDS: Array[String] = [
	SOLDIER_OBJECT_ID,
	ARCHER_OBJECT_ID,
	TOWER_GUARD_OBJECT_ID,
	KNIGHT_OBJECT_ID,
]
const REQUIRED_VOICE_KINDS: Array[String] = ["select", "move", "attack", "death"]
## Voice kind -> the `SoundState` key retail names for it inside a
## `ModelConditionSoundSelectorClientBehavior` block (eomer.ini:726-730).
## Only the keys retail actually authors there are mapped; nothing is invented.
const SOUND_STATE_VOICE_KEYS := {
	"select": "VoiceSelect",
	"move": "VoiceMove",
	"attack_charge": "VoiceAttackCharge",
}
const ROSTER_VOICE_EVENT_IDS: Dictionary = {
	SOLDIER_OBJECT_ID: {
		"select": ["GondorSoldierVoiceSelectMS", "gondorFighter.select"],
		"move": ["GondorSoldierVoiceMove"],
		"attack": ["GondorSoldierVoiceAttack", "gondorFighter.attack"],
		"attack_structure": ["GondorSoldierVoiceAttackBuilding", "gondorFighter.attackBuilding"],
		"death": ["HumanVoiceDie"],
	},
	ARCHER_OBJECT_ID: {
		"select": ["GondorArcherVoiceSelectMS"],
		"move": ["GondorArcherVoiceMove"],
		"attack": ["GondorArcherVoiceAttack"],
		"attack_structure": ["GondorArcherVoiceAttackBuilding"],
		"death": ["HumanVoiceDie"],
	},
	TOWER_GUARD_OBJECT_ID: {
		"select": ["TowerGuardVoiceSelectMS"],
		"move": ["TowerGuardVoiceMove"],
		"attack": ["TowerGuardVoiceAttack"],
		"attack_structure": ["TowerGuardVoiceAttackBuilding"],
		"death": ["HumanVoiceDie"],
	},
	KNIGHT_OBJECT_ID: {
		"select": ["GondorKnightVoiceSelectMS"],
		"move": ["GondorKnightVoiceMove"],
		"attack": ["GondorKnightVoiceAttack"],
		"attack_structure": ["GondorKnightVoiceAttackBuilding"],
		"death": ["HumanVoiceDie"],
	},
}
const REQUIRED_SFX_EVENT_IDS: Array[String] = [
	"ArrowDrawBow",
	"SwordShingClean1ForHordes",
	"BodyFallSoldier",
	"ImpactHorse",
	"BuildingLightDamageStone",
	"BuildingHeavyDamageStone",
	"BuildingSink",
]
const LEGACY_ATTACK_EVENT_IDS: Array[String] = [
	"gondorFighter.attack",
	"gondorFighter.attackBuilding",
	"gondorFighter.attackCharge",
]
## Voice kinds beyond the required four. "attack_structure" splits the
## retail VoiceAttackStructure/VoiceAttackMachine acks from the generic
## VoiceAttack candidates; "build" is the porter VoiceBuildResponse ack.
const EXTRA_VOICE_KINDS: Array[String] = ["attack_structure", "build"]
const FORDS_AMBIENT_TYPE_EVENT_IDS: Dictionary = {
	"Amb_BirdsAmonHen1": "Amb_BirdsAmonHen1",
	"Amb_BirdsAmonHen2": "Amb_BirdsAmonHen2",
	"Amb_BirdsIthilien1Loop": "Amb_MTBirds1Loop",
	"Amb_BirdsIthilien2Loop": "Amb_MTBirds2Loop",
	"Amb_CritterDesert1": "Amb_CritterDesert1",
	"Amb_WaterRiver1Loop": "Amb_WaterRiver1Loop",
	"AmbStream_AmonHenForest1": "AmbientAmonHenForest1Stream",
}
const FORDS_AMBIENT_PLACEMENT_COUNT := 50
## Retail soundeffects.ini:229-230. The audio-event document preserves macro
## tokens, so runtime resolves only these two exact authored defines.
const FORDS_AMBIENT_RANGE_DEFINES := {
	"AMB_MIN_RANGE": 300.0,
	"AMB_MAX_RANGE": 800.0,
}

## HAND-TUNED DEFAULTS, NOT AUTHORED — for world-typed SFX whose AudioEvent
## block authors no MinRange/MaxRange (e.g. `ImpactHorse`,
## soundeffects.ini:16673-16684 authors `Type = world shrouded everyone` and no
## range rows; `ImpactSword01` at :9394-9412 authors `MinRange = 200` and no
## MaxRange). Retail resolved the unauthored side from the AudioEventInfo
## constructor default, which the decomp oracle does not carry. These values
## mirror retail's most common authored world ranges instead of inventing a
## curve: voice.ini:59-60 `MinRange = 250 / MaxRange = 700`, the EMOTION_*
## defines 300/700 (soundeffects.ini:236-251), AMB_* 300/800 (:229-230).
## Every event that fell back to them is named once in `unranged_world_sfx`.
const WORLD_SFX_DEFAULT_MIN_RANGE := 250.0
const WORLD_SFX_DEFAULT_MAX_RANGE := 700.0

## Effective retail soundeffects.ini:8050-8070. The currently published Men
## packs contain these exact WAV leaves (because GondorFarm references them)
## but their global audio-events registry omitted the two logical event rows.
## Keep this bounded compatibility bridge source-exact and fail closed if any
## authored leaf is absent; it can be retired once the registry emits them.
const AUTHORED_STRUCTURE_DAMAGE_EVENTS := {
	"BuildingLightDamageWood": {
		"samples": ["WIBuild_damaW1a", "WIBuild_damaW1b", "WIBuild_damaR1d"],
		"parameters": [
			{"field": "Volume", "value": "110"}, {"field": "VolumeShift", "value": "-10"},
			{"field": "PitchShift", "value": "-10 10"}, {"field": "Priority", "value": "normal"},
			{"field": "Type", "value": "world shrouded everyone"}, {"field": "SubmixSlider", "value": "SoundFX"},
		],
	},
	"BuildingHeavyDamageWood": {
		"samples": ["WIBuild_damaW2a", "WIBuild_damaW2b"],
		"parameters": [
			{"field": "Volume", "value": "110"}, {"field": "VolumeShift", "value": "-10"},
			{"field": "PitchShift", "value": "-10 10"}, {"field": "Priority", "value": "normal"},
			{"field": "Type", "value": "world shrouded everyone"}, {"field": "SubmixSlider", "value": "SoundFX"},
		],
	},
}

const MUSIC_STATES: Array[String] = ["explore", "battle", "victory", "defeat"]
const MUSIC_CROSSFADE_SECONDS := 1.5

var pack_root := ""
var playback_enabled := true
# Intent and routing histories exist for focused diagnostics only. Production
# keeps the latest route status but does not retain per-event dictionary copies.
var observability_enabled := false
# music_player always references the currently active (audible / most recently
# selected) player; _music_player_alt holds the other side of the crossfade pair.
var music_player: AudioStreamPlayer
var _music_player_alt: AudioStreamPlayer
var voice_player: AudioStreamPlayer
## `sfx_player` is the HEAD of `sfx_players`, kept as a named field because the
## slice, the settings lane and the existing gates all reference it directly.
var sfx_player: AudioStreamPlayer
## THE SFX LANE IS A POOL, NOT ONE PLAYER.
##
## A single AudioStreamPlayer meant `stream = ...; play()` per event, so every
## effect CUT OFF the previous one mid-sample. At horde combat rates (one
## `combat.member_swing` per member per attack) a ~1s clip survived 50-100ms:
## a machine-gun of clipped transients, which is the mechanical cause of the
## "attack sounds awful" playtest report. Round-robin over N players lets
## simultaneous effects overlap the way the retail mixer does.
##
## These stay non-positional AudioStreamPlayers, but `Type = world` semantics
## are now honored PRESENTATION-SIDE at play time: the combat events still
## carry only attacker/target ids, so `_consume_event` resolves a sim-plane
## position through `spatial_position_probe` (the slice answers from its own
## read-only view of the sim rows) and `_play_sfx` applies the retail shroud
## cull + inverse-distance listener falloff (`_spatial_sfx_semantics`).
## A world-typed effect that reaches `_play_sfx` with NO resolvable position
## still plays flat and is still recorded as `unsupported-type:world:<event>`
## in `sfx_semantics_gaps`.
const SFX_POOL_SIZE := 8
var sfx_players: Array[AudioStreamPlayer] = []
var _sfx_cursor := 0
## event_id -> how many times the authored `Limit` voice cap dropped a request.
## Retail AudioEvent `Limit = 3` means at most three concurrent instances; the
## cap is HONORED (the pack carries the field) and every drop is counted.
var sfx_limit_drops: Dictionary = {}
## Retail AudioEvent parameters this lane still cannot honor, as
## `unsupported-<field>:<event_id>` strings. Never invented, only reported.
var sfx_semantics_gaps: Dictionary = {}
## PRESENTATION-SIDE spatial context for world-typed SFX. All three probes are
## wired by the slice (`configure_spatial_audio`); when unset, every effect
## keeps the pre-existing flat behavior and the world gap stays recorded.
## (entity_or_structure_id: int, is_structure: bool) -> Vector2 sim-plane
## position, or null when the row no longer exists.
var spatial_position_probe := Callable()
## (position: Vector2) -> bool: is that ground CLEAR for the local player.
## Retail authority: GameSounds.cpp:262-268 (`SoundManager::canPlayNow`) —
## an `ST_SHROUDED` sound whose position is not CELLSHROUD_CLEAR for the
## local player is refused outright, which is why retail plays NOTHING for
## battles under the fog of war.
var spatial_visibility_probe := Callable()
## () -> Vector2: the listener's sim-plane position (the camera focus).
var spatial_listener_probe := Callable()
## Source-unit -> sim-plane multiplier (source_map_data.local_transform_scale);
## authored MinRange/MaxRange are source units.
var spatial_transform_scale := 1.0
## event_id -> how many world/shrouded effects were culled under the shroud.
var shrouded_sfx_drops: Dictionary = {}
## event_id -> how many world effects were culled beyond MaxRange
## (MilesAudioManager.cpp:2723-2726: volume hits zero at m_maxDistance).
var distance_sfx_drops: Dictionary = {}
## event_id -> true for world effects that authored no usable MinRange/MaxRange
## and took the hand-tuned WORLD_SFX_DEFAULT_* values. Named once, never silent.
var unranged_world_sfx: Dictionary = {}
## The last spatial decision, for deterministic runner assertions.
var last_spatial_sfx_receipt: Dictionary = {}
# music_streams keeps the legacy state -> primary AudioStream view (the exact
# <state>.mp3 leaf) so existing closure gates stay intact.
var music_streams: Dictionary = {}
# music_playlists maps state -> Array[AudioStream]; music_playlist_paths mirrors
# it with the resolved on-disk paths for deterministic runner assertions.
var music_playlists: Dictionary = {}
var music_playlist_paths: Dictionary = {}
# Deterministic playlist bookkeeping. current_music_track_index tracks the leaf
# playing for current_music_state. Focused verification may opt into the bounded
# transition history; production retains current state/index instead.
var current_music_track_index := -1
## Non-empty once a music pack has bound this player's faction: state -> the
## authored slot/playlist that answered it. Runners assert on this instead of
## on audible playback.
var music_faction_slots: Dictionary = {}
var music_director: RefCounted = null
var music_diagnostics: Array[String] = []
var music_transition_log: Array[Dictionary] = []
var _music_active_index := -1
var _music_last_index := -1
var _music_fade_tween: Tween = null
var _music_rng := RandomNumberGenerator.new()
# Compatibility views used by the existing slice gate. Strict roster readiness
# is exposed separately and never substitutes Soldier samples for another unit.
var voice_streams: Dictionary = {"select": [], "attack": []}
var audio_event_routes: Dictionary = {}
var typed_selector_intents: Array[Dictionary] = []
var declared_structure_lifecycle_audio_active := false
var roster_voice_routes: Dictionary = {}
## Alt-form (mounted/knight) voice candidates per object/kind, bound from the
## same converted audioRoutes rows as the base set. Empty when a unit authors
## a single form (almost everyone).
var roster_voice_form_routes: Dictionary = {}
var playable_unit_audio_events: Dictionary = {}
var playable_unit_categories: Dictionary = {}
## object_id -> {"fire": event_id, "swing": event_id} weapon-class SFX routed
## from converted doc evidence (siege fire, monster melee classes).
var playable_unit_weapon_sfx: Dictionary = {}
## object_id -> authored bodyfall event id for siege/monster units (their own
## class — a machine or monster never borrows the human BodyFallSoldier).
var playable_unit_bodyfall: Dictionary = {}
## object_id -> authored AnimationSoundClientBehavior rows (clip + frame).
## When present, bodyfall/weapon animation sounds pick by clip, not lowest id.
var playable_unit_animation_sounds: Dictionary = {}
var last_animation_sound_clock_receipt: Dictionary = {}
var _animation_sound_sequence := 0
var _consumed_clip_frames: Dictionary = {}
## object_id -> {STATE_TOKEN -> {SoundKey -> eventId}} from the authored
## `ModelConditionSoundSelectorClientBehavior` rows
## (`data/ini/object/goodfaction/units/men/eomer.ini:726-730`:
## `SoundState = MOUNTED / VoiceMove = EomerVoiceMoveMounted`). This is the
## AUTHORED alt-form table; the `_voice_candidate_form` id-name heuristic below
## remains only for units that ship no selector module.
var playable_unit_sound_states: Dictionary = {}
## object_id -> the UPPERCASED stem of the object's DEFAULT model
## (`eomer.ini:25 DefaultModelConditionState / Model = RUEomer_SKN` → RUEOMER).
## Retail keys every `AnimationSound` row by `Skeleton.Clip`, so this stem is
## what separates Eomer's own foot rows (`RUEomer_SKL.RUEomer_RUNA`) from the
## rows authored on the mount's skeleton (`RUHHs_Theo_SKL.*`).
var playable_unit_base_skeletons: Dictionary = {}
## object_id -> active model-condition sound-state tokens, e.g. ["MOUNTED"].
var playable_unit_sound_state_active: Dictionary = {}
## Named footstep gaps. A unit with no authored footstep row plays NOTHING and
## is listed here; a generic step sample is never substituted.
var footstep_gaps: Dictionary = {}
var last_footstep_receipt: Dictionary = {}
## object id -> the unit's OWN authored `SoundImpact` AudioEvent id.
##
## NOT the per-hit sound. Retail's `SoundImpact` is the CRUSH / KNOCKBACK thud:
## `data/ini/object/goodfaction/units/men/gondorfighter.ini:768` authors
## `SoundImpact = ImpactHorse` (a horse-trample leaf) on an INFANTRY object,
## and the same file's knockback death module at :913-919
## (`Behavior = SlowDeathBehavior ModuleTag_07 / DeathTypes = NONE +KNOCKBACK`)
## carries the comment "Same as normal death, but no sound (sound already
## played by SoundImpact = ... )". The field is kept indexed here so a future
## crush/knockback event can route it (`route_crush_impact`); it must never be
## routed from `combat.hit`.
var playable_unit_impact: Dictionary = {}
## object id -> how many weapon swings that unit had to drop because no
## converted pack carries its weapon's FXList sound. This is the measured size
## of the remaining "this unit has no swing sound" gap; see
## `_route_weapon_swing`. The old class defaults (`SwordShingClean1ForHordes` /
## `ArrowDrawBow`) were INVENTED substitutions no retail weapon chain names for
## these units, so the gap is now a counted silence instead of a wrong sound.
var generic_weapon_swing_fallbacks: Dictionary = {}
## damage type (retail `DamageType`, e.g. SLASH/PIERCE) -> how many hits landed
## with no per-hit sound because the DamageFX table is not imported. See the
## `combat.hit` branch in `sync_events`.
var damage_fx_gaps: Dictionary = {}
## Per-structure-kind converted audio contract projected by the slice from the
## faction's playable-structure documents (select/damage/collapse/EVA ids and
## the damaged-state health fractions). Kinds absent here keep legacy routing.
var structure_audio_contract: Dictionary = {}
## Retail eva.ini side name for the local player (Men/Elves/Dwarves/...).
var faction_side := ""
var eva_last_played_msec: Dictionary = {}
var eva_arbitration_msec := -1
var eva_arbitration_priority := -1
var eva_diagnostics: Array[String] = []
## Folded eva id -> sim-clock ms until which that event is muted because a
## playing event's retail OtherEvaEventsToBlock list (compiled `blockEvents`)
## names it. Presentation-only, like the arbitration state above.
var eva_blocked_until_msec: Dictionary = {}
## Announcements deferred by retail MillisecondsToWaitBeforePlaying (compiled
## `delayMs`): {eva_id, due_msec, sequence} in the sim-clock domain.
var eva_pending_delays: Array[Dictionary] = []
var _structure_damage_stage: Dictionary = {}
## When non-empty, these playable-unit documents replace the ContentDB-wide
## registry for roster voices and readiness, so a faction match is gated on
## exactly the units it can field (never on another faction's cohabiting pack
## or on units the roster composition excluded).
var scoped_playable_unit_documents: Dictionary = {}
var route_failures: Dictionary = {}
var missing_required_events: Array[String] = []
## Roster voices NO MOUNTED PACK CAN ANSWER, as named rows. These are reported
## and warned about, but they do not refuse the launch: see
## `_created_hero_voice_is_unanswerable` for the deliberately narrow rule that
## puts a row here instead of into `missing_required_events`.
var unvoiced_roster_degradations: Array[String] = []
## object id -> true for runtime-synthesized created heroes (their runtime
## document carries `registration.createAHero`). Filled by
## `_load_playable_unit_audio_routes` alongside the voice tables.
var playable_unit_created_hero: Dictionary = {}
## object id -> the audio event ids the unit's OWN document ships bindings for,
## folded lower-case. A created hero ships none; a converted pack unit ships one
## per authored event.
var playable_unit_declared_bindings: Dictionary = {}
var intent_log: Array[Dictionary] = []
var routing_log: Array[Dictionary] = []
var ambient_players: Array[AudioStreamPlayer3D] = []
var ambient_emitters: Array[Dictionary] = []
var ambient_diagnostics: Array[String] = []
var ambient_contract_declared := false
var ambient_parity_ready := false
var current_music_state := ""
var last_route_result: Dictionary = {}
var _stream_cache: Dictionary = {}
var _entity_object_ids: Dictionary = {}
var _next_event_index := 0
var _next_ui_sequence := 1000000000
var local_team := 0
var music_volume := UserSettingsScript.DEFAULT_MUSIC_VOLUME
var voice_sfx_volume := UserSettingsScript.DEFAULT_VOICE_SFX_VOLUME
var muted := UserSettingsScript.DEFAULT_MUTED


func configure(selected_pack_root: String, enable_playback: bool = true, active_playable_unit_documents: Dictionary = {}, faction_structure_audio: Dictionary = {}, player_faction_side: String = "", player_team: int = 0) -> bool:
	pack_root = selected_pack_root
	playback_enabled = enable_playback
	scoped_playable_unit_documents = active_playable_unit_documents.duplicate(true)
	structure_audio_contract = faction_structure_audio.duplicate(true)
	faction_side = player_faction_side
	local_team = player_team
	_ensure_players()
	# THE MUSIC HANDOFF. The shell playlist plays on the GameAudio autoload,
	# which survives the scene change into a match, so the menu theme would
	# otherwise keep playing underneath this object's per-faction ladder. Taking
	# the handoff here - at the one place the in-match audio object is armed -
	# keeps it out of every launch call site. Only a PLAYING configure claims it;
	# an analysis configure (enable_playback = false, what the runners use) must
	# not silence a menu it was never going to speak over.
	if playback_enabled:
		var shell_audio: Node = _autoload("GameAudio")
		if shell_audio != null and shell_audio.has_method("stop_music"):
			shell_audio.call("stop_music")
	_reset_music_playback()
	music_streams.clear()
	music_playlists.clear()
	music_playlist_paths.clear()
	music_transition_log.clear()
	music_faction_slots.clear()
	music_diagnostics.clear()
	music_director = null
	voice_streams = {"select": [], "attack": []}
	audio_event_routes.clear()
	typed_selector_intents.clear()
	roster_voice_routes.clear()
	roster_voice_form_routes.clear()
	playable_unit_audio_events.clear()
	playable_unit_categories.clear()
	playable_unit_weapon_sfx.clear()
	playable_unit_bodyfall.clear()
	playable_unit_animation_sounds.clear()
	playable_unit_sound_states.clear()
	playable_unit_base_skeletons.clear()
	playable_unit_sound_state_active.clear()
	footstep_gaps.clear()
	playable_unit_impact.clear()
	generic_weapon_swing_fallbacks.clear()
	damage_fx_gaps.clear()
	sfx_limit_drops.clear()
	sfx_semantics_gaps.clear()
	shrouded_sfx_drops.clear()
	distance_sfx_drops.clear()
	unranged_world_sfx.clear()
	last_spatial_sfx_receipt.clear()
	route_failures.clear()
	missing_required_events.clear()
	unvoiced_roster_degradations.clear()
	playable_unit_created_hero.clear()
	playable_unit_declared_bindings.clear()
	intent_log.clear()
	routing_log.clear()
	last_route_result.clear()
	_stream_cache.clear()
	_structure_damage_stage.clear()
	eva_last_played_msec.clear()
	eva_arbitration_msec = -1
	eva_arbitration_priority = -1
	eva_diagnostics.clear()
	eva_blocked_until_msec.clear()
	eva_pending_delays.clear()
	declared_structure_lifecycle_audio_active = false
	# Entity→object identity is carried on the sim's voice-relevant events and
	# learned as they stream; no static roster pin exists (S1: the retired
	# INITIAL_ENTITY_OBJECT_IDS table voiced every faction's id 1 as a Gondor
	# Soldier).
	_entity_object_ids = {}
	_next_event_index = 0
	current_music_state = ""
	var profile_boot := OS.get_environment("OPENBFME_PROFILE_BOOT") == "1"
	var boot_mark := Time.get_ticks_msec()
	_load_user_settings()
	_load_music()
	if profile_boot:
		print("BOOT_PROFILE audio.load_music_ms=%d" % (Time.get_ticks_msec() - boot_mark))
		boot_mark = Time.get_ticks_msec()
	_load_declared_audio_routes()
	if profile_boot:
		print("BOOT_PROFILE audio.declared_routes_ms=%d" % (Time.get_ticks_msec() - boot_mark))
		boot_mark = Time.get_ticks_msec()
	_configure_fords_ambient_from_pack()
	if profile_boot:
		print("BOOT_PROFILE audio.fords_ambient_ms=%d" % (Time.get_ticks_msec() - boot_mark))
		boot_mark = Time.get_ticks_msec()
	_bind_roster_voice_routes()
	_refresh_compatibility_views()
	_collect_readiness_diagnostics()
	if profile_boot:
		print("BOOT_PROFILE audio.voice_bind_readiness_ms=%d" % (Time.get_ticks_msec() - boot_mark))
	return has_complete_audio_closure()


func configure_spatial_audio(position_probe: Callable, visibility_probe: Callable, listener_probe: Callable, transform_scale: float) -> void:
	## Presentation-only wiring: the slice hands this module read-only views of
	## the sim rows (positions), the local player's shroud state, and the camera
	## focus. Nothing here writes sim state or touches the event stream.
	spatial_position_probe = position_probe
	spatial_visibility_probe = visibility_probe
	spatial_listener_probe = listener_probe
	spatial_transform_scale = transform_scale if transform_scale > 0.0 else 1.0


func _ensure_players() -> void:
	if music_player == null or not is_instance_valid(music_player):
		music_player = AudioStreamPlayer.new()
		music_player.name = "RetailMusic"
		add_child(music_player)
		music_player.finished.connect(_on_music_finished.bind(music_player))
	if _music_player_alt == null or not is_instance_valid(_music_player_alt):
		_music_player_alt = AudioStreamPlayer.new()
		_music_player_alt.name = "RetailMusicAlt"
		add_child(_music_player_alt)
		_music_player_alt.finished.connect(_on_music_finished.bind(_music_player_alt))
	if voice_player == null or not is_instance_valid(voice_player):
		voice_player = AudioStreamPlayer.new()
		voice_player.name = "RetailVoice"
		add_child(voice_player)
	# The SFX pool. Index 0 is also published as `sfx_player` so every existing
	# caller and gate keeps its handle; indices 1..N-1 exist purely so a second
	# simultaneous effect does not truncate the first.
	var rebuilt: Array[AudioStreamPlayer] = []
	for index in SFX_POOL_SIZE:
		var existing: AudioStreamPlayer = sfx_players[index] if index < sfx_players.size() else null
		if existing == null or not is_instance_valid(existing):
			existing = AudioStreamPlayer.new()
			existing.name = "RetailSfx" if index == 0 else "RetailSfx%d" % index
			add_child(existing)
		rebuilt.append(existing)
	sfx_players = rebuilt
	sfx_player = sfx_players[0]
	_apply_volume_levels()


func _load_user_settings() -> void:
	var settings: Dictionary = UserSettingsScript.load_audio()
	music_volume = float(settings["music_volume"])
	voice_sfx_volume = float(settings["voice_sfx_volume"])
	muted = bool(settings["muted"])
	_apply_volume_levels()


func set_music_volume(value: float, persist: bool = false) -> void:
	music_volume = clampf(value, 0.0, 1.0)
	_apply_volume_levels()
	if persist:
		UserSettingsScript.save_audio(music_volume, voice_sfx_volume, muted)


func get_music_volume() -> float:
	return music_volume


func set_voice_sfx_volume(value: float, persist: bool = false) -> void:
	voice_sfx_volume = clampf(value, 0.0, 1.0)
	_apply_volume_levels()
	if persist:
		UserSettingsScript.save_audio(music_volume, voice_sfx_volume, muted)


func get_voice_sfx_volume() -> float:
	return voice_sfx_volume


func set_voice_volume(value: float, persist: bool = false) -> void:
	set_voice_sfx_volume(value, persist)


func get_voice_volume() -> float:
	return get_voice_sfx_volume()


func set_muted(value: bool, persist: bool = false) -> void:
	muted = value
	_apply_volume_levels()
	if persist:
		UserSettingsScript.save_audio(music_volume, voice_sfx_volume, muted)


func is_muted() -> bool:
	return muted


func _apply_volume_levels() -> void:
	# A volume/mute change is authoritative: finalize any in-flight crossfade so
	# the active player lands exactly on the configured level and the idle side
	# stays silent. This keeps mute/volume deterministic instead of racing a tween.
	_finalize_music_fade()
	if music_player != null and is_instance_valid(music_player):
		music_player.volume_db = _music_target_db()
	if _music_player_alt != null and is_instance_valid(_music_player_alt):
		_music_player_alt.volume_db = UserSettingsScript.SILENT_DB
	if voice_player != null and is_instance_valid(voice_player):
		voice_player.volume_db = UserSettingsScript.volume_to_db(voice_sfx_volume, muted)
	# The whole SFX pool tracks the user slider; a per-event retail `Volume`
	# offset is added on top at play time (see `_play_sfx`).
	for pooled_sfx in sfx_players:
		if pooled_sfx != null and is_instance_valid(pooled_sfx):
			pooled_sfx.volume_db = UserSettingsScript.volume_to_db(voice_sfx_volume, muted)
	for ambient_player in ambient_players:
		if ambient_player != null and is_instance_valid(ambient_player):
			ambient_player.volume_db = UserSettingsScript.volume_to_db(voice_sfx_volume, muted)


func has_complete_audio_closure() -> bool:
	# Kept as the playable-slice compatibility gate until the private profile
	# contains all four units. It does not claim strict roster readiness.
	return _has_music_closure() and (voice_streams["select"] as Array).size() > 0 and (voice_streams["attack"] as Array).size() > 0


func has_complete_roster_audio_closure() -> bool:
	return _has_music_closure() and missing_required_events.is_empty()


func readiness_diagnostics() -> Array[String]:
	return missing_required_events.duplicate()


func has_complete_fords_ambient_closure() -> bool:
	return ambient_contract_declared and ambient_parity_ready and ambient_emitters.size() == FORDS_AMBIENT_PLACEMENT_COUNT


func fords_ambient_readiness_diagnostics() -> Array[String]:
	return ambient_diagnostics.duplicate()


func _has_music_closure() -> bool:
	return music_streams.has("explore") and music_streams.has("battle") and music_streams.has("victory") and music_streams.has("defeat")


func _load_music() -> void:
	# Build a per-state playlist from every music leaf the pack actually ships.
	# A file belongs to a state when its basename equals the state or is prefixed
	# with "<state>-"/"<state>_" (e.g. battle-alternate.mp3 joins the battle
	# playlist). Orphan tracks that match no state (e.g. building.mp3) are ignored
	# rather than invented into a playlist. music_streams keeps the exact
	# <state>.mp3 leaf as the primary so legacy closure gates stay identical.
	var music_dir := pack_root.path_join("assets/audio/music")
	var files_by_state := _scan_music_files(music_dir)
	for state in MUSIC_STATES:
		var ordered_names: Array = files_by_state.get(state, [])
		var streams: Array[AudioStream] = []
		var paths: Array[String] = []
		for name in ordered_names:
			var path := music_dir.path_join(String(name))
			var stream := _load_stream(path)
			if stream != null:
				streams.append(stream)
				paths.append(path)
		if streams.is_empty():
			continue
		music_playlists[state] = streams
		music_playlist_paths[state] = paths
		music_streams[state] = streams[0]
	_apply_faction_music()


func _autoload(singleton_name: String) -> Node:
	var loop := Engine.get_main_loop()
	if loop == null or not (loop is SceneTree):
		return null
	var tree := loop as SceneTree
	if tree.root == null:
		return null
	return tree.root.get_node_or_null(NodePath(singleton_name))


func _apply_faction_music() -> void:
	## Replace the filename-convention playlists above with the ones RETAIL
	## authors for this player's faction, when a music pack is installed.
	##
	## The convention scan is a fallback, not a contract: it buckets whatever
	## `<state>*.mp3` leaves a faction pack happens to ship. The music pack
	## carries the real binding - the side token -> Multisound table extracted
	## from music.ini plus the Music_MusicScripts_Single library - so when it is
	## present it WINS, per state, and only for states it can actually answer.
	## A state the document does not bind keeps the fallback rather than going
	## silent.
	music_faction_slots.clear()
	music_director = null
	if faction_side == "":
		return
	# Looked up as a node, NOT as the `ContentDB` global: this script is
	# preloaded by headless --script runners, which compile it before autoloads
	# are registered, and a compile-time global reference there fails the whole
	# dependent-script chain (observed: stage15_menu_runner.gd).
	var content_db: Node = _autoload("ContentDB")
	if content_db == null:
		return
	var document: Dictionary = content_db.get("music_document")
	if document == null or document.is_empty():
		return
	var director := MusicDirectorScript.new()
	# The director joins pack faction id -> side; this call site already holds
	# the resolved side, so bind it to itself and skip the second lookup.
	if not director.configure(document, {faction_side.to_lower(): faction_side}):
		music_diagnostics.append_array(director.diagnostics)
		return
	music_director = director
	for state in MUSIC_STATES:
		var slot := director.slot_for_state(state)
		if slot == "":
			continue
		var paths: Array[String] = director.track_paths_for(faction_side.to_lower(), slot)
		if paths.is_empty():
			music_diagnostics.append(
				"music: %s/%s resolved no authored track" % [faction_side, slot]
			)
			continue
		var streams: Array[AudioStream] = []
		var loaded: Array[String] = []
		for path in paths:
			var stream := _load_stream(path)
			if stream != null:
				streams.append(stream)
				loaded.append(path)
		if streams.is_empty():
			music_diagnostics.append(
				"music: %s/%s bound %d tracks but none loaded" % [faction_side, slot, paths.size()]
			)
			continue
		music_playlists[state] = streams
		music_playlist_paths[state] = loaded
		music_streams[state] = streams[0]
		music_faction_slots[state] = {
			"slot": slot,
			"playlist": director.playlist_id_for(faction_side.to_lower(), slot),
			"tracks": loaded.size(),
			"shuffle": director.shuffles(faction_side.to_lower(), slot),
			"loop": director.loops(faction_side.to_lower(), slot),
		}


func _scan_music_files(music_dir: String) -> Dictionary:
	var result: Dictionary = {}
	for state in MUSIC_STATES:
		result[state] = []
	var directory := DirAccess.open(music_dir)
	if directory == null:
		return result
	var names: Array[String] = []
	directory.list_dir_begin()
	var file_name := directory.get_next()
	while file_name != "":
		if not directory.current_is_dir() and file_name.get_extension().to_lower() == "mp3":
			names.append(file_name)
		file_name = directory.get_next()
	directory.list_dir_end()
	names.sort_custom(func(a: String, b: String) -> bool: return a.to_lower() < b.to_lower())
	for name in names:
		var state := _music_state_for_file(name)
		if state != "":
			(result[state] as Array).append(name)
	# Ensure the exact <state>.mp3 leaf leads its playlist so the primary view is
	# stable regardless of alphabetical ordering.
	for state in MUSIC_STATES:
		var ordered: Array = result[state]
		var exact := "%s.mp3" % state
		if ordered.has(exact) and String(ordered[0]).to_lower() != exact:
			ordered.erase(exact)
			ordered.insert(0, exact)
	return result


func _music_state_for_file(file_name: String) -> String:
	var basename := file_name.get_basename().to_lower()
	for state in MUSIC_STATES:
		if basename == state or basename.begins_with("%s-" % state) or basename.begins_with("%s_" % state):
			return state
	return ""


func _load_declared_audio_routes() -> void:
	# ORDER MATTERS. `_voice_event_ids_for_object` answers a document-driven
	# unit (every playable-unit document, every created hero) from
	# `playable_unit_audio_events`, which `_load_playable_unit_audio_routes`
	# fills. Collecting the roster's requests BEFORE that index existed meant a
	# created hero's registry-referenced voice events (HeroWestMaleVoice*) were
	# never requested, never routed, and the hero was reported mute even when
	# the mounted registry defined and could deliver them (owner 2026-08-22).
	_load_playable_unit_audio_routes()
	var requested: Dictionary = {}
	for object_id in _active_roster_object_ids():
		var by_kind: Dictionary = _voice_event_ids_for_object(object_id)
		for kind in REQUIRED_VOICE_KINDS:
			for event_id in Array(by_kind.get(kind, [])):
				requested[String(event_id).to_lower()] = String(event_id)
	for event_id in REQUIRED_SFX_EVENT_IDS:
		requested[event_id.to_lower()] = event_id
	for event_id in FORDS_AMBIENT_TYPE_EVENT_IDS.values():
		requested[String(event_id).to_lower()] = String(event_id)
	for event_id in requested.values():
		var built := _build_content_db_route(String(event_id))
		if bool(built.get("ok", false)):
			audio_event_routes[String(event_id).to_lower()] = built["route"]
		else:
			route_failures[String(event_id).to_lower()] = String(built.get("reason", "invalid_event"))
	_load_v0_pack_routes()


func _build_content_db_route(event_id: String) -> Dictionary:
	var content_db := get_node_or_null("/root/ContentDB")
	if content_db == null:
		return {"ok": false, "reason": "content_db_unavailable"}
	var event_definition: Dictionary = content_db.call("get_retail_audio_event", event_id)
	var collected := _collect_content_db_leaves(content_db, event_id, {})
	if not bool(collected.get("ok", false)):
		return collected
	var leaves: Array = collected.get("leaves", [])
	if leaves.is_empty():
		return {"ok": false, "reason": "event_has_no_samples"}
	return {
		"ok": true,
		"route": {
			"event_id": event_id,
			"source": "content-db-v1",
			"definition": event_definition.duplicate(true),
			"leaves": leaves,
		},
	}


func _configure_fords_ambient_from_pack() -> void:
	_clear_ambient_players()
	ambient_emitters.clear()
	ambient_diagnostics.clear()
	ambient_contract_declared = false
	ambient_parity_ready = false
	var mod_loader := get_node_or_null("/root/ModLoader")
	if mod_loader == null or pack_root == "":
		_add_ambient_diagnostic("map-contract:mod-loader-unavailable")
		return
	var map_path := String(mod_loader.call("resolve_pack_path", pack_root, "maps/fords-of-isen-ii/map.json"))
	var map_value: Variant = _read_json_without_diagnostics(map_path)
	if typeof(map_value) != TYPE_DICTIONARY:
		_add_ambient_diagnostic("map-contract:missing-fords-map")
		return
	var map_document := map_value as Dictionary
	if String(map_document.get("schema", "")) != "openbfme.map" or String(map_document.get("id", "")) != "bfme2.map.fords-of-isen-ii":
		_add_ambient_diagnostic("map-contract:invalid-fords-map")
		return
	var objects_relative := String(map_document.get("objects", ""))
	if objects_relative == "" or not bool(mod_loader.call("is_safe_relative_path", objects_relative)):
		_add_ambient_diagnostic("map-contract:invalid-objects-path")
		return
	var objects_path := String(mod_loader.call("resolve_pack_path", pack_root, "maps/fords-of-isen-ii".path_join(objects_relative)))
	var objects_value: Variant = _read_json_without_diagnostics(objects_path)
	if typeof(objects_value) != TYPE_DICTIONARY or typeof((objects_value as Dictionary).get("objects", null)) != TYPE_ARRAY:
		_add_ambient_diagnostic("map-contract:invalid-objects-document")
		return
	var placements: Array = []
	for placement_value in Array((objects_value as Dictionary).get("objects", [])):
		if typeof(placement_value) != TYPE_DICTIONARY:
			continue
		var placement := placement_value as Dictionary
		if FORDS_AMBIENT_TYPE_EVENT_IDS.has(String(placement.get("typeName", ""))):
			placements.append(placement)
	placements.sort_custom(func(a: Variant, b: Variant) -> bool: return int((a as Dictionary).get("index", -1)) < int((b as Dictionary).get("index", -1)))
	ambient_contract_declared = placements.size() == FORDS_AMBIENT_PLACEMENT_COUNT
	if not ambient_contract_declared:
		_add_ambient_diagnostic("map-contract:placement-count:%d" % placements.size())
	for placement_value in placements:
		_instantiate_fords_ambient_emitter(placement_value as Dictionary)
	ambient_parity_ready = ambient_contract_declared and ambient_emitters.size() == placements.size() and ambient_diagnostics.is_empty()


func _instantiate_fords_ambient_emitter(placement: Dictionary) -> void:
	var type_name := String(placement.get("typeName", ""))
	var event_id := String(FORDS_AMBIENT_TYPE_EVENT_IDS.get(type_name, ""))
	var placement_index := int(placement.get("index", -1))
	var position_value: Variant = placement.get("godotPosition", null)
	if event_id == "" or placement_index < 0 or typeof(position_value) != TYPE_ARRAY or (position_value as Array).size() != 3:
		_add_ambient_diagnostic("map-contract:invalid-placement:%d" % placement_index)
		return
	var route: Dictionary = audio_event_routes.get(event_id.to_lower(), {})
	if route.is_empty() or String(route.get("source", "")) != "content-db-v1":
		_add_ambient_diagnostic("missing-event:%s:%s" % [type_name, event_id])
		return
	var selected := _select_ambient_leaf(route, placement_index + 1)
	if selected.is_empty():
		_add_ambient_diagnostic("invalid-route:%s" % event_id)
		return
	var stream: Variant = _stream_for_leaf(selected)
	var path := String(selected.get("path", ""))
	if not (stream is AudioStream) or not _is_resolved_audio_path(path):
		_add_ambient_diagnostic("invalid-stream:%s" % event_id)
		return
	var definition: Dictionary = route.get("definition", {})
	var parameters := _ambient_parameters(definition)
	var runtime_parameters := _ambient_runtime_parameters(event_id, parameters, placement_index)
	for gap in _ambient_parameter_gaps(event_id, definition, parameters):
		_add_ambient_diagnostic(gap)
	var player := AudioStreamPlayer3D.new()
	player.name = "RetailAmbient_%04d_%s" % [placement_index, type_name]
	player.stream = stream as AudioStream
	var source_position := position_value as Array
	player.position = Vector3(float(source_position[0]), float(source_position[1]), float(source_position[2]))
	var min_range := float(runtime_parameters.get("min_range", -1.0))
	var max_range := float(runtime_parameters.get("max_range", -1.0))
	if min_range > 0.0 and max_range >= min_range:
		player.unit_size = min_range
		player.max_distance = max_range
	player.pitch_scale = float(runtime_parameters.get("pitch_scale", 1.0))
	var linear_gain := float(runtime_parameters.get("linear_gain", 1.0))
	player.volume_db = linear_to_db(linear_gain) if linear_gain > 0.0 else -80.0
	player.set_meta("retail_audio_event_id", event_id)
	player.set_meta("retail_audio_type_name", type_name)
	player.set_meta("retail_audio_placement_index", placement_index)
	player.set_meta("retail_audio_path", path)
	player.set_meta("retail_audio_parameters", parameters.duplicate(true))
	player.set_meta("retail_audio_runtime_parameters", runtime_parameters.duplicate(true))
	player.set_meta("retail_audio_loop_control", String(parameters.get("control", "")).to_lower() == "loop")
	add_child(player)
	ambient_players.append(player)
	ambient_emitters.append({
		"event_id": event_id,
		"type_name": type_name,
		"placement_index": placement_index,
		"path": path,
		"position": player.position,
		"min_range": min_range,
		"max_range": max_range,
		"loop": String(parameters.get("control", "")).to_lower() == "loop",
		"player": player,
	})
	_apply_volume_levels()
	if playback_enabled:
		player.play()


func _select_ambient_leaf(route: Dictionary, sequence: int) -> Dictionary:
	var leaves: Array = route.get("leaves", [])
	var total_weight := 0
	for leaf_value in leaves:
		if typeof(leaf_value) != TYPE_DICTIONARY:
			return {}
		total_weight += maxi(0, int((leaf_value as Dictionary).get("weight", 0)))
	if total_weight <= 0:
		return {}
	var slot := posmod(sequence - 1, total_weight)
	for leaf_value in leaves:
		var leaf := leaf_value as Dictionary
		var weight := maxi(0, int(leaf.get("weight", 0)))
		if slot < weight:
			return leaf
		slot -= weight
	return {}


func _ambient_parameters(definition: Dictionary) -> Dictionary:
	var result: Dictionary = {}
	var values: Variant = definition.get("parameters", null)
	if typeof(values) != TYPE_ARRAY:
		return result
	for row_value in values as Array:
		if typeof(row_value) != TYPE_DICTIONARY:
			continue
		var row := row_value as Dictionary
		var field := String(row.get("field", "")).strip_edges().to_lower()
		if field != "":
			result[field] = String(row.get("value", "")).strip_edges()
	return result


func _ambient_parameter_gaps(event_id: String, definition: Dictionary, parameters: Dictionary = {}) -> Array[String]:
	var parsed := parameters if not parameters.is_empty() else _ambient_parameters(definition)
	var gaps: Array[String] = []
	var min_range := _resolve_ambient_distance(String(parsed.get("minrange", "")))
	var max_range := _resolve_ambient_distance(String(parsed.get("maxrange", "")))
	if min_range <= 0.0 or max_range < min_range:
		gaps.append("unsupported-range:%s:%s:%s" % [event_id, String(parsed.get("minrange", "missing")), String(parsed.get("maxrange", "missing"))])
	else:
		gaps.append("unsupported-attenuation-curve:%s" % event_id)
	if String(parsed.get("control", "")).to_lower() == "loop":
		gaps.append("unsupported-loop-scheduler:%s" % event_id)
	else:
		gaps.append("unsupported-control:%s:%s" % [event_id, String(parsed.get("control", "missing"))])
	if parsed.has("priority"):
		gaps.append("unproven-priority-arbitration:%s:%s" % [event_id, String(parsed.priority)])
	if parsed.has("limit"):
		gaps.append("unproven-concurrent-limit-scheduler:%s:%s" % [event_id, String(parsed.limit)])
	if parsed.has("pitchshift") or parsed.has("delay") or parsed.has("volumeshift"):
		gaps.append("unproven-retail-audio-rng-seed:%s" % event_id)
	if parsed.has("volume") or parsed.has("volumeshift"):
		gaps.append("unproven-miles-to-godot-gain-equivalence:%s" % event_id)
	if parsed.has("submixslider"):
		gaps.append("unsupported-submixslider:%s:%s" % [event_id, String(parsed.submixslider)])
	if parsed.has("attack") or parsed.has("decay"):
		gaps.append("unsupported-attack-decay-envelope:%s" % event_id)
	if not String(parsed.get("type", "")).to_lower().split(" ").has("world"):
		gaps.append("unsupported-type:%s:%s" % [event_id, String(parsed.get("type", "missing"))])
	return gaps


func _ambient_runtime_parameters(event_id: String, parameters: Dictionary, placement_index: int) -> Dictionary:
	var pitch := _number_range(String(parameters.get("pitchshift", "0")), 0.0)
	var delay := _number_range(String(parameters.get("delay", "0")), 0.0)
	var volume_shift := _number_range(String(parameters.get("volumeshift", "0")), 0.0)
	var pitch_fraction := _ambient_fraction("%s:%d:pitch" % [event_id, placement_index])
	var delay_fraction := _ambient_fraction("%s:%d:delay" % [event_id, placement_index])
	var volume_fraction := _ambient_fraction("%s:%d:volume" % [event_id, placement_index])
	var pitch_percent := lerpf(float(pitch.x), float(pitch.y), pitch_fraction)
	var delay_ms := lerpf(float(delay.x), float(delay.y), delay_fraction)
	# Retail AudioEventRTS chooses VolumeShift uniformly between 1+shift and 1.
	var shift_percent := lerpf(float(volume_shift.x), float(volume_shift.y), volume_fraction)
	var volume_percent := clampf(float(String(parameters.get("volume", "100"))), 0.0, 100.0)
	return {
		"min_range": _resolve_ambient_distance(String(parameters.get("minrange", ""))),
		"max_range": _resolve_ambient_distance(String(parameters.get("maxrange", ""))),
		"pitch_percent": pitch_percent,
		"pitch_scale": maxf(0.01, 1.0 + pitch_percent / 100.0),
		"delay_ms": maxf(0.0, delay_ms),
		"volume_percent": volume_percent,
		"volume_shift_percent": shift_percent,
		"linear_gain": (volume_percent / 100.0) * clampf(1.0 + shift_percent / 100.0, 0.0, 1.0),
		"control": String(parameters.get("control", "")).to_lower(),
		"priority": String(parameters.get("priority", "")).to_lower(),
		"limit": maxi(0, int(String(parameters.get("limit", "0")))),
		"type_tokens": Array(String(parameters.get("type", "")).to_lower().split(" ", false)),
		"submix": String(parameters.get("submixslider", "")),
		"attack_sample_id": String(parameters.get("attack", "")),
		"decay_sample_id": String(parameters.get("decay", "")),
		"selection": "deterministic-substitute-unproven-retail-rng-seed",
	}


func _resolve_ambient_distance(value: String) -> float:
	if FORDS_AMBIENT_RANGE_DEFINES.has(value):
		return float(FORDS_AMBIENT_RANGE_DEFINES[value])
	return _strict_positive_float(value)


func _number_range(value: String, fallback: float) -> Vector2:
	var tokens := value.split(" ", false)
	if tokens.is_empty() or not String(tokens[0]).is_valid_float():
		return Vector2(fallback, fallback)
	var first := float(tokens[0])
	var second := first
	if tokens.size() >= 2 and String(tokens[1]).is_valid_float():
		second = float(tokens[1])
	return Vector2(minf(first, second), maxf(first, second))


func _ambient_fraction(key: String) -> float:
	var prefix := key.sha256_text().substr(0, 8)
	return float(prefix.hex_to_int()) / 4294967295.0


func _strict_positive_float(value: String) -> float:
	if not value.is_valid_float():
		return -1.0
	var parsed := value.to_float()
	return parsed if parsed > 0.0 else -1.0


func _add_ambient_diagnostic(value: String) -> void:
	if value != "" and not ambient_diagnostics.has(value):
		ambient_diagnostics.append(value)
		ambient_diagnostics.sort()


func _clear_ambient_players() -> void:
	for player in ambient_players:
		if player != null and is_instance_valid(player):
			player.stop()
			player.free()
	ambient_players.clear()


func _collect_content_db_leaves(content_db: Node, event_id: String, stack: Dictionary) -> Dictionary:
	var key := event_id.to_lower()
	if stack.has(key):
		return {"ok": false, "reason": "cyclic_event_definition"}
	stack[key] = true
	var event: Dictionary = content_db.call("get_retail_audio_event", event_id)
	if not event.is_empty():
		var result := _collect_sample_references(content_db, event.get("sounds", []))
		stack.erase(key)
		return result
	var multisound: Dictionary = content_db.call("get_retail_audio_multisound", event_id)
	if multisound.is_empty():
		stack.erase(key)
		return {"ok": false, "reason": "missing_event"}
	var references: Variant = multisound.get("subsounds", [])
	if typeof(references) != TYPE_ARRAY or (references as Array).is_empty():
		stack.erase(key)
		return {"ok": false, "reason": "multisound_has_no_subsounds"}
	var leaves: Array[Dictionary] = []
	for reference_value in references:
		if typeof(reference_value) != TYPE_DICTIONARY:
			stack.erase(key)
			return {"ok": false, "reason": "invalid_multisound_reference"}
		var reference := reference_value as Dictionary
		var child_id := String(reference.get("id", ""))
		var weight := _reference_weight(reference)
		if child_id == "" or weight <= 0:
			stack.erase(key)
			return {"ok": false, "reason": "invalid_multisound_reference"}
		var child := _collect_content_db_leaves(content_db, child_id, stack)
		if not bool(child.get("ok", false)):
			stack.erase(key)
			return child
		for leaf_value in Array(child.get("leaves", [])):
			var leaf := (leaf_value as Dictionary).duplicate()
			leaf["weight"] = mini(1_000_000, int(leaf.get("weight", 1)) * weight)
			leaves.append(leaf)
	stack.erase(key)
	return {"ok": true, "leaves": leaves}


func _collect_sample_references(content_db: Node, references: Variant) -> Dictionary:
	if typeof(references) != TYPE_ARRAY or (references as Array).is_empty():
		return {"ok": false, "reason": "event_has_no_samples"}
	var samples_value: Variant = content_db.get("retail_audio_samples")
	if typeof(samples_value) != TYPE_DICTIONARY:
		return {"ok": false, "reason": "sample_index_unavailable"}
	var sample_index := samples_value as Dictionary
	var leaves: Array[Dictionary] = []
	for reference_value in references:
		if typeof(reference_value) != TYPE_DICTIONARY:
			return {"ok": false, "reason": "invalid_sample_reference"}
		var reference := reference_value as Dictionary
		var sample_id := String(reference.get("id", ""))
		var weight := _reference_weight(reference)
		if sample_id == "" or weight <= 0:
			return {"ok": false, "reason": "invalid_sample_reference"}
		var sample: Dictionary = sample_index.get(sample_id.to_lower(), {})
		if sample.is_empty():
			return {"ok": false, "reason": "missing_sample:%s" % sample_id}
		var path := String(content_db.call("resolve_retail_audio_sample_path", sample_id))
		# ContentDB can expose hundreds of leaves through the small set of
		# required logical events.  Prove containment and the file header now,
		# then decode only the leaf selected for playback.  This keeps startup
		# bounded without weakening the retail closure check.
		if not _is_resolved_audio_path(path) or not _has_supported_audio_header(path):
			return {"ok": false, "reason": "invalid_sample:%s" % sample_id}
		leaves.append({
			"sample_id": sample_id,
			"path": path,
			"stream": null,
			"validated_path": true,
			"weight": weight,
		})
	return {"ok": true, "leaves": leaves}


func _reference_weight(reference: Dictionary) -> int:
	var value: Variant = reference.get("weight", 1)
	if typeof(value) != TYPE_INT and typeof(value) != TYPE_FLOAT:
		return 0
	return clampi(int(value), 0, 1_000_000)


func _load_v0_pack_routes() -> void:
	var mod_loader := get_node_or_null("/root/ModLoader")
	if mod_loader == null or pack_root == "":
		return
	var pack_path := String(mod_loader.call("resolve_pack_path", pack_root, "pack.json"))
	var pack: Variant = _read_json_without_diagnostics(pack_path)
	if typeof(pack) != TYPE_DICTIONARY:
		return
	var files: Variant = (pack as Dictionary).get("files", {})
	if typeof(files) != TYPE_DICTIONARY:
		return
	var relative := String((files as Dictionary).get("audioEvents", ""))
	var manifest_path := String(mod_loader.call("resolve_pack_path", pack_root, relative))
	var document: Variant = _read_json_without_diagnostics(manifest_path)
	if typeof(document) != TYPE_DICTIONARY:
		return
	var manifest := document as Dictionary
	if String(manifest.get("schema", "")) != "openbfme.audio-events" or int(manifest.get("schemaVersion", -1)) != 0:
		return
	var events: Variant = manifest.get("events", {})
	if typeof(events) != TYPE_DICTIONARY:
		return
	for event_key in (events as Dictionary).keys():
		var event_id := String(event_key)
		var row_value: Variant = (events as Dictionary)[event_key]
		if event_id == "" or typeof(row_value) != TYPE_DICTIONARY:
			continue
		var built := _build_v0_route(mod_loader, event_id, row_value as Dictionary)
		var key := event_id.to_lower()
		if bool(built.get("ok", false)):
			audio_event_routes[key] = built["route"]
			route_failures.erase(key)
		else:
			route_failures[key] = String(built.get("reason", "invalid_event"))


func _build_v0_route(mod_loader: Node, event_id: String, row: Dictionary) -> Dictionary:
	var relative_directory := String(row.get("directory", ""))
	var pattern := String(row.get("pattern", ""))
	if relative_directory == "" or not bool(mod_loader.call("is_safe_relative_path", relative_directory)):
		return {"ok": false, "reason": "unsafe_event_directory"}
	if pattern == "" or pattern.contains("/") or pattern.contains("\\") or not pattern.to_lower().ends_with(".wav"):
		return {"ok": false, "reason": "invalid_event_pattern"}
	var directory_path := String(mod_loader.call("resolve_pack_path", pack_root, relative_directory))
	var directory := DirAccess.open(directory_path)
	if directory == null:
		return {"ok": false, "reason": "missing_event_directory"}
	var names: Dictionary = {}
	directory.list_dir_begin()
	var file_name := directory.get_next()
	while file_name != "":
		if not directory.current_is_dir() and file_name.matchn(pattern):
			names[file_name.to_lower()] = file_name
		file_name = directory.get_next()
	directory.list_dir_end()
	var additional: Variant = row.get("additional", [])
	if typeof(additional) != TYPE_ARRAY:
		return {"ok": false, "reason": "invalid_additional_samples"}
	for additional_value in additional:
		var additional_name := String(additional_value)
		if additional_name == "" or additional_name.get_file() != additional_name or not additional_name.to_lower().ends_with(".wav"):
			return {"ok": false, "reason": "invalid_additional_sample"}
		names[additional_name.to_lower()] = additional_name
	var ordered_names: Array = names.values()
	ordered_names.sort_custom(func(a: Variant, b: Variant) -> bool: return String(a).to_lower() < String(b).to_lower())
	if ordered_names.is_empty():
		return {"ok": false, "reason": "event_has_no_samples"}
	var leaves: Array[Dictionary] = []
	for name_value in ordered_names:
		var name := String(name_value)
		var path := String(mod_loader.call("resolve_pack_path", pack_root, relative_directory.path_join(name)))
		var stream := _load_stream(path)
		if stream == null:
			return {"ok": false, "reason": "invalid_sample:%s" % name}
		leaves.append({
			"sample_id": name.get_basename(),
			"path": path,
			"stream": stream,
			"weight": 1,
		})
	return {
		"ok": true,
		"route": {
			"event_id": event_id,
			"source": "declared-pack-v0",
			"leaves": leaves,
		},
	}


func _read_json_without_diagnostics(path: String) -> Variant:
	if path == "" or not FileAccess.file_exists(path):
		return null
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null:
		return null
	var parser := JSON.new()
	if parser.parse(file.get_as_text()) != OK:
		return null
	return parser.data


func _bind_roster_voice_routes() -> void:
	for object_id in _active_roster_object_ids():
		var by_kind: Dictionary = _voice_event_ids_for_object(object_id)
		var bound: Dictionary = {}
		var form_bound: Dictionary = {}
		for kind in _all_voice_kinds():
			for event_id_value in Array(by_kind.get(kind, [])):
				var event_id := String(event_id_value)
				var route: Dictionary = audio_event_routes.get(event_id.to_lower(), {})
				if route.is_empty():
					continue
				if not bound.has(kind):
					bound[kind] = route
				var form := _voice_candidate_form(event_id)
				if form != "" and not form_bound.has("%s/%s" % [form, kind]):
					form_bound["%s/%s" % [form, kind]] = route
		roster_voice_routes[object_id] = bound
		if not form_bound.is_empty():
			roster_voice_form_routes[object_id] = form_bound


func _all_voice_kinds() -> Array[String]:
	var kinds: Array[String] = []
	kinds.append_array(REQUIRED_VOICE_KINDS)
	kinds.append_array(EXTRA_VOICE_KINDS)
	return kinds


func _voice_candidate_form(event_id: String) -> String:
	## Alt-form hero sets author their form in the event id (Theoden*MountedMS,
	## FaramirKnight*). A unit whose only set names the form (GondorKnight*) is
	## that form by default, so classifying it changes nothing for it.
	var lowered := event_id.to_lower()
	if lowered.contains("mounted") or lowered.contains("knight"):
		return "mounted"
	return ""


func _refresh_compatibility_views() -> void:
	voice_streams = {"select": [], "attack": []}
	var soldier: Dictionary = roster_voice_routes.get(SOLDIER_OBJECT_ID, {})
	_append_unique_streams(voice_streams["select"] as Array, soldier.get("select", {}))
	for event_id in LEGACY_ATTACK_EVENT_IDS:
		_append_unique_streams(voice_streams["attack"] as Array, audio_event_routes.get(event_id.to_lower(), {}))
	if (voice_streams["attack"] as Array).is_empty():
		_append_unique_streams(voice_streams["attack"] as Array, soldier.get("attack", {}))


func _append_unique_streams(target: Array, route_value: Variant) -> void:
	if typeof(route_value) != TYPE_DICTIONARY:
		return
	var seen_paths: Dictionary = {}
	for existing in target:
		if existing is AudioStream and existing.has_meta("retail_audio_path"):
			seen_paths[String(existing.get_meta("retail_audio_path"))] = true
	for leaf_value in Array((route_value as Dictionary).get("leaves", [])):
		if typeof(leaf_value) != TYPE_DICTIONARY:
			continue
		var leaf := leaf_value as Dictionary
		var stream: Variant = _stream_for_leaf(leaf)
		var path := String(leaf.get("path", ""))
		if stream is AudioStream and not seen_paths.has(path):
			(stream as AudioStream).set_meta("retail_audio_path", path)
			target.append(stream)
			seen_paths[path] = true


func _collect_readiness_diagnostics() -> void:
	missing_required_events.clear()
	unvoiced_roster_degradations.clear()
	for state in ["explore", "battle", "victory", "defeat"]:
		if not music_streams.has(state):
			missing_required_events.append("missing_music_state:%s" % state)
	for object_id in _active_roster_object_ids():
		var bound: Dictionary = roster_voice_routes.get(object_id, {})
		var expected: Dictionary = _voice_event_ids_for_object(object_id)
		for kind in REQUIRED_VOICE_KINDS:
			var candidates: Array = expected.get(kind, [])
			if candidates.is_empty():
				# A unit with no authored candidates for a kind has nothing to
				# bind (builders author no attack voice): absent, not missing.
				continue
			if bound.has(kind):
				continue
			if _created_hero_voice_is_unanswerable(object_id, candidates):
				unvoiced_roster_degradations.append(
					"unvoiced_created_hero:%s:%s:%s" % [object_id, kind, String(candidates[0])]
				)
				continue
			missing_required_events.append("missing_voice_event:%s:%s:%s" % [object_id, kind, String(candidates[0])])
	for event_id in REQUIRED_SFX_EVENT_IDS:
		if not audio_event_routes.has(event_id.to_lower()):
			missing_required_events.append("missing_sfx_event:%s" % event_id)
	missing_required_events.sort()
	unvoiced_roster_degradations.sort()
	if not unvoiced_roster_degradations.is_empty():
		# NAMED, NOT WARNED, and the severity is deliberate - the same call the
		# arrow-art lane made for the shared-Good borrow (retail_battalion.gd,
		# `DIAGNOSTIC_SEVERITY`). Until a pack ships the retail hero voice sets in
		# its audio registry, EVERY created hero in EVERY match lands here. An
		# engine warning for a known universal condition either reddens every
		# proof gate (which reads a stray WARNING as a defect) or trains readers
		# to scroll past warnings. A structured DiagLog row plus a printed line
		# keeps it loud in the place that survives the run; the moment a mounted
		# pack CAN answer these events the rows stop appearing on their own.
		var summary := ", ".join(PackedStringArray(unvoiced_roster_degradations))
		print("[RetailSliceAudio] created-hero-voice-degraded %s" % summary)
		DiagLogScript.emit("info", "audio.created_hero_voice_unavailable", {
			"rows": unvoiced_roster_degradations.duplicate(),
			"packRoot": pack_root,
			"factionSide": faction_side,
		})


func _marker_is_truthy(value: Variant) -> bool:
	## Truthiness for a JSON-shaped document marker. `bool(value)` is NOT usable
	## here: GDScript has no bool constructor for Dictionary/Array and throws a
	## runtime error that unwinds the whole configure (observed through the proof
	## harness). A real `createAHero` marker is a non-empty Dictionary; `false`,
	## `{}`, `[]`, `""` and `0` all mean "not a created hero", which is the
	## strict side.
	match typeof(value):
		TYPE_NIL:
			return false
		TYPE_BOOL:
			return value
		TYPE_INT, TYPE_FLOAT:
			return float(value) != 0.0
		TYPE_STRING, TYPE_STRING_NAME:
			return String(value).strip_edges() != ""
		TYPE_DICTIONARY:
			return not (value as Dictionary).is_empty()
		TYPE_ARRAY:
			return not (value as Array).is_empty()
	return false


func _record_audio_provenance(object_id: String, created_hero: bool, declared_bindings: Dictionary) -> void:
	## Merge, never overwrite, and merge in the STRICTER direction on both axes.
	##
	## Two runtime documents can name the same id (a horde's container id is its
	## member's unit id, and a created hero is registered under one id twice).
	## Last-writer-wins would let a created hero's marker land on a converted
	## unit's id - degrading a voice that must stay strict - or drop a converted
	## unit's declared bindings off an id a hero also claims. So: an id is a
	## created hero only when EVERY document claiming it says so, and its
	## declared-binding set is the UNION of every claim. Both directions add
	## strictness, so a collision can only ever refuse more, never less.
	if playable_unit_created_hero.has(object_id):
		playable_unit_created_hero[object_id] = bool(playable_unit_created_hero[object_id]) and created_hero
	else:
		playable_unit_created_hero[object_id] = created_hero
	var merged: Dictionary = playable_unit_declared_bindings.get(object_id, {}) as Dictionary
	for key_value in declared_bindings.keys():
		merged[key_value] = true
	playable_unit_declared_bindings[object_id] = merged


func _created_hero_voice_is_unanswerable(object_id: String, candidates: Array) -> bool:
	## Can ANY mounted pack voice this created hero, for this kind?
	##
	## A created hero is not converted content: it is synthesized at runtime from
	## the mounted `cah.system` table, and that table names its voice events BY
	## REFERENCE ("HeroWestMaleVoiceAttack") into the retail audio registry. It
	## therefore ships no samples and no `audioBindings` of its own, unlike every
	## converted playable unit, whose importer resolved each authored event to
	## leaves (or to an explicit `authored-silent` resolution) inside the unit's
	## own document. When the mounted registry defines NONE of the referenced
	## events, no mounted pack can speak this line at all. The honest answer is a
	## silent hero in a match that plays, reported by name - not a refusal that
	## takes the whole faction offline. (Retail ships those hero voice sets; our
	## converted registry does not carry them yet.)
	##
	## DELIBERATELY NARROW, so the fail-closed rule this sits inside stays intact:
	##   - only runtime-synthesized created heroes (`registration.createAHero`);
	##   - only when the hero's own document declares NO binding or resolution for
	##     the event, so a hero that DOES carry converted audio is still strict;
	##   - only when the registry DEFINES NONE of the candidates. An event the
	##     registry defines but cannot deliver (missing samples, broken
	##     multisound, cyclic definition) is a BROKEN PACK and still fails closed,
	##     exactly as `pack_capability` treats a declared-but-absent surface.
	if not bool(playable_unit_created_hero.get(object_id, false)):
		return false
	var content_db := get_node_or_null("/root/ContentDB")
	if content_db == null:
		# No registry to ask means no evidence of absence. Stay strict.
		return false
	var declared_bindings: Dictionary = playable_unit_declared_bindings.get(object_id, {}) as Dictionary
	for candidate_value in candidates:
		var event_id := String(candidate_value)
		if event_id == "":
			continue
		if declared_bindings.has(event_id.to_lower()):
			return false
		if not (content_db.call("get_retail_audio_event", event_id) as Dictionary).is_empty():
			return false
		if not (content_db.call("get_retail_audio_multisound", event_id) as Dictionary).is_empty():
			return false
	return true


func sync_events(events: Array[Dictionary]) -> void:
	while _next_event_index < events.size():
		var event: Dictionary = events[_next_event_index]
		_consume_event(event)
		_next_event_index += 1


func acknowledge_event_history_compaction(retained_count: int) -> void:
	_next_event_index = retained_count


func set_declared_structure_lifecycle_audio_active(active: bool) -> void:
	declared_structure_lifecycle_audio_active = active


func _consume_event(event: Dictionary) -> void:
	var kind := String(event.get("kind", ""))
	var sequence := int(event.get("sequence", 0))
	var entity_id := int(event.get("entity_id", 0))
	var target_id := int(event.get("target_id", 0))
	var eva_clock := int(event.get("tick", -1)) * 100 if event.has("tick") else -1
	if eva_clock >= 0:
		# Any event clock doubles as the EVA clock: announcements deferred by
		# retail MillisecondsToWaitBeforePlaying sound at the first event at or
		# after their due time.
		flush_due_eva_events(eva_clock)
	if kind == "production.complete":
		var produced_object_id := String(event.get("object_id", ""))
		if produced_object_id != "" and _active_roster_object_ids().has(produced_object_id):
			_entity_object_ids[target_id] = produced_object_id
		_play_first_unit_event(produced_object_id, "created", sequence)
	elif kind == "unit.summoned":
		# Summons spawn outside the production lane; the event carries the
		# created object's own id so its voice set resolves immediately.
		var summoned_object_id := String(event.get("object_id", ""))
		if summoned_object_id != "" and entity_id > 0:
			_entity_object_ids[entity_id] = summoned_object_id
		_play_first_unit_event(summoned_object_id, "created", sequence)
	elif kind == "production.queued":
		_play_first_unit_event(String(event.get("unit_type", "")), "purchase", sequence, String(event.get("command_id", "")))
	elif kind == "audio.typed_selector":
		var event_id := String(event.get("event_id", ""))
		var routed := route_audio_event(event_id, sequence)
		var byte_check := _typed_selector_byte_receipt_check(event, routed)
		var accepted := bool(routed.get("ok", false)) and bool(byte_check.get("match", false))
		var reason := String(routed.get("reason", ""))
		if bool(routed.get("ok", false)) and not bool(byte_check.get("match", false)):
			reason = "wav-byte-identity-receipt-mismatch"
		var receipt := {"sequence": sequence, "entity_id": entity_id, "object_id": String(event.get("object_id", "")), "event_id": event_id, "logical_event_ids": Array(event.get("logical_event_ids", [event_id])).duplicate(), "sound_role": String(event.get("sound_role", "")), "priority": int(event.get("priority", 0)), "rng_receipt": String(event.get("rng_receipt", "")), "selector_receipt": String(event.get("selector_receipt", "")), "wav_set_byte_identity_receipt": (event.get("wav_set_byte_identity_receipt", {}) as Dictionary).duplicate(true), "selected_path": String(byte_check.get("selected_path", "")), "selected_sha256": String(byte_check.get("selected_sha256", "")), "byte_identity_match": bool(byte_check.get("match", false)), "accepted": accepted, "reason": reason}
		typed_selector_intents.append(receipt)
		if accepted: _play_routed(routed, voice_player)
	elif kind == "transport.enter":
		# The contain module's mechanical EnterSound and the passenger object's
		# VoiceEnterUnit* acknowledgement are separate authored routes. The sim
		# emits this event only after containment succeeds, so rejected/full loads
		# cannot make either sound. Specific-ship candidates precede the generic
		# transport candidate; take the first resolvable route exactly once.
		var enter_sound := String(event.get("sound", ""))
		if enter_sound != "":
			_play_sfx(route_audio_event(enter_sound, sequence))
		for candidate_value in event.get("voice_candidates", []) as Array:
			var candidate := String(candidate_value)
			if candidate == "":
				continue
			var acknowledgement := route_audio_event(candidate, sequence)
			if bool(acknowledgement.get("ok", false)):
				_play_routed(acknowledgement, voice_player)
				break
	elif kind.begins_with("music."):
		_set_music(kind.trim_prefix("music."))
	elif kind == "voice.select":
		_play_routed(route_roster_voice(_object_id_for_event(event, entity_id), "select", sequence, String(event.get("form", ""))), voice_player)
	elif kind == "order.move" or kind == "voice.move":
		_play_routed(route_roster_voice(_object_id_for_event(event, entity_id), "move", sequence, String(event.get("form", ""))), voice_player)
	elif kind == "voice.attack":
		# SHROUD GATE (owner round 12: "I can hear the wild mobs being
		# attacked"). Combat SFX already obey retail's ST_SHROUDED rule; this
		# acknowledgement lane did not, so a creep fight in unexplored fog
		# still shouted. A speaker on non-CLEAR ground is refused, exactly
		# like its swing sounds.
		var voice_at: Variant = _spatial_event_position([entity_id])
		if (
			voice_at != null
			and spatial_visibility_probe.is_valid()
			and not bool(spatial_visibility_probe.call(voice_at))
		):
			return
		var attacker_object_id := _object_id_for_event(event, entity_id)
		var form := String(event.get("form", ""))
		var ack_kind := "attack_structure" if String(event.get("target_kind", "")) == "structure" else "attack"
		var ack := route_roster_voice(attacker_object_id, ack_kind, sequence, form)
		if not bool(ack.get("ok", false)) and ack_kind != "attack":
			# A unit with no authored VoiceAttackStructure ack answers with its
			# generic attack set (retail objects do the same when only
			# VoiceAttack is authored).
			ack = route_roster_voice(attacker_object_id, "attack", sequence, form)
		_play_routed(ack, voice_player)
	elif kind == "construction.started":
		# The porter's own VoiceBuildResponse acknowledges the build order and
		# the site starts its hammering loop bed.
		_play_routed(route_roster_voice(_object_id_for_event(event, entity_id), "build", sequence), voice_player)
		_play_sfx(route_audio_event("BuildingConstructionLoop", sequence))
	elif kind == "construction.completed":
		# Retail EVA "construction complete" sting (GenericBuildingComplete-Builder),
		# local player team only (team 0 mirrors the sim's PLAYER_TEAM).
		if int(event.get("team", -1)) == local_team:
			_play_eva_announcement("GenericBuildingComplete-Builder", sequence, eva_clock)
	elif kind == "battalion.defeated":
		var defeated_object_id := _object_id_for_event(event, target_id)
		var death_anim := resolve_death_animation(event)
		_play_routed(route_roster_voice(defeated_object_id, "death", sequence), voice_player)
		# Position: the fallen battalion first; when its row is already gone by
		# presentation time, the attacker standing over it is the honest stand-in.
		_play_sfx(_route_bodyfall(defeated_object_id, sequence, String(death_anim.get("clip", "")), int(death_anim.get("frame", -1))), _spatial_event_position([target_id, entity_id]))
		_entity_object_ids.erase(target_id)
		# Pure RotWK 2.01 has no generic UnitLost/BattalionLost EVA block.
		# The unit's own authored death voice above is therefore the complete,
		# fail-closed retail route; never substitute another faction's announcer.
	elif kind == "battalion.member_defeated":
		# Every fallen member lands its own class bodyfall (horde wipes are not
		# a single thud); the battalion's die voice still fires once at defeat.
		var member_death := resolve_death_animation(event)
		_play_sfx(_route_bodyfall(_object_id_for_event(event, target_id), sequence, String(member_death.get("clip", "")), int(member_death.get("frame", -1))), _spatial_event_position([target_id, entity_id]))
	elif kind == "combat.swing":
		# BATTALION-LEVEL CADENCE MARKER ONLY - deliberately silent.
		#
		# `combat.swing` fires ONCE for the whole horde when an attack cycle
		# begins (retail_slice_sim.gd:15743). Retail's weapon sound is a WEAPON
		# FireFX, which fires per weapon discharge, i.e. once per MEMBER:
		# `data/ini/weapon.ini:5514 Weapon GondorSword` authors
		# `FireFX = FX_GondorSwordHit` and every soldier in the horde carries
		# that weapon. Routing it from the battalion event made eight swordsmen
		# produce one clink. The per-member hop below is the retail-shaped one;
		# the battalion's own acknowledgement is the `voice.attack` ack handled
		# above.
		#
		# IT IS NOT USELESS: it is the only combat event that carries the
		# battalion's `object_id` (retail_slice_sim.gd:15745). The per-member
		# event that follows it in the same attack cycle carries only
		# member_index (retail_slice_sim.gd:15754-15760), so without this hop a
		# starting-army battalion - one never seen via production.complete or
		# unit.summoned - would have no object to resolve its weapon sound from.
		var swinging_object_id := String(event.get("object_id", ""))
		if swinging_object_id != "" and entity_id > 0 and _active_roster_object_ids().has(swinging_object_id):
			_entity_object_ids[entity_id] = swinging_object_id
	elif kind == "combat.member_swing":
		# ONE WEAPON SOUND PER MEMBER PER SWING, matching retail's per-weapon
		# FireFX. The sim has emitted this event at retail_slice_sim.gd:15754
		# and NOTHING in game/src consumed it.
		_play_sfx(_route_weapon_swing(_object_id_for_event(event, entity_id), sequence), _spatial_event_position([entity_id]))
	elif kind == "combat.hit":
		# THE PER-HIT LAYER IS A NAMED, COUNTED GAP - NOT SoundImpact.
		#
		# This branch used to route the target object's `SoundImpact`. That is
		# the wrong retail field twice over:
		#
		# 1. `SoundImpact` is the CRUSH / KNOCKBACK thud, not the per-hit sound.
		#    `gondorfighter.ini:768` authors `SoundImpact = ImpactHorse` on an
		#    INFANTRY object, and the same file's knockback death module
		#    (`gondorfighter.ini:913-919`, `SlowDeathBehavior ModuleTag_07 /
		#    DeathTypes = NONE +KNOCKBACK`) is commented "Same as normal death,
		#    but no sound (sound already played by SoundImpact = ... )".
		#    Measured over the mounted packs, `SoundImpact` resolves to
		#    `ImpactHorse` for 168 of the 168 playable-unit documents that carry
		#    it - a shared object macro, not a per-unit choice. Since
		#    `combat.hit` fires once per member per damage application
		#    (retail_slice_sim.gd:17202), a horde fight emitted a continuous
		#    stream of Volume-90 horse-trample thuds. That is the dominant part
		#    of the "attack sounds still sound awful" report.
		#
		# 2. Retail's real per-hit layer is DamageFX: the weapon declares a
		#    `DamageFXType` (`data/ini/weapon.ini:5514 Weapon GondorSword` ->
		#    `DamageFXType = SWORD_SLASH`) and `data/ini/damagefx.ini` resolves
		#    DamageFXType x armor to an FXList. For a sword hit that resolves to
		#    NOTHING: damagefx.ini's `NormalDamageFX` block authors
		#    `MajorFX = SWORD_SLASH  FX_NONE`. Retail plays no sound at all on a
		#    sword hit - the sound of melee is the SWING.
		#
		# The DamageFX tables are not imported, so this lane cannot resolve the
		# correct per-hit FX for the damage types that DO author one
		# (GOOD_ARROW_PIERCE -> FX_GoodArrowHit, MAGIC -> FX_MagicHit, ...).
		# Rather than substitute a wrong sound the hit is SILENT, and the gap
		# keeps a number per damage type in `damage_fx_gaps`.
		var hit_damage_type := String(event.get("damage_type", "")).to_upper()
		if hit_damage_type == "":
			hit_damage_type = "UNDECLARED"
		damage_fx_gaps[hit_damage_type] = int(damage_fx_gaps.get(hit_damage_type, 0)) + 1
	elif kind == "combat.hit_structure":
		_consume_structure_damage(event, sequence, _spatial_event_position([entity_id], [target_id]))
	elif kind == "structure.destroyed":
		_consume_structure_destroyed(event, sequence, _spatial_event_position([entity_id], [target_id]))
	elif kind in ["battalion_upgrade.completed", "upgrade.completed"] and int(event.get("team", -1)) == local_team:
		var upgrade_eva := _upgrade_complete_eva_id(String(event.get("upgrade_id", "")))
		if upgrade_eva != "":
			_play_eva_announcement(upgrade_eva, sequence, eva_clock)
	elif kind == "power.cast":
		# Spellbook casts carry the document's initiateSoundId; route_audio_event
		# lazily promotes ContentDB-registered spell events (fail closed when the
		# pack cannot resolve them).
		var cast_sound_id := String(event.get("sound_id", ""))
		if cast_sound_id != "":
			_play_sfx(route_audio_event(cast_sound_id, sequence))
	elif kind == "power.purchased":
		# The authored palantir spellbook purchase click (Gui_PalantirChoosePowerClick);
		# the powers orb emits no chrome sound of its own on a successful pick.
		_play_sfx(route_audio_event("Gui_PalantirChoosePowerClick", sequence))
	elif kind == "eva.base_under_attack":
		_play_structure_eva(event, "eva_damaged", sequence, eva_clock)
	elif kind == "eva.building_lost":
		_play_structure_eva(event, "eva_die", sequence, eva_clock)
	elif kind == "eva.ally_defeated":
		_play_eva_announcement("AllyDefeated", sequence, eva_clock)
	elif kind == "eva.enemy_defeated":
		_play_eva_announcement("EnemyDefeated", sequence, eva_clock)
	elif kind == "eva.hero_created":
		_play_created_eva(event, sequence, eva_clock)
	_append_bounded_observability(intent_log, event.duplicate(true))


func _spatial_event_position(entity_ids: Array, structure_ids: Array = []) -> Variant:
	## Resolve a combat event's sim-plane position through the slice's read-only
	## probe. Structure ids are the true sound source and win over the attacker
	## fallback; the first id whose row still exists answers. Returns null when
	## nothing resolves (the effect then plays flat and the world gap records).
	if not spatial_position_probe.is_valid():
		return null
	for id_value in structure_ids:
		var structure_id := int(id_value)
		if structure_id <= 0:
			continue
		var structure_found: Variant = spatial_position_probe.call(structure_id, true)
		if typeof(structure_found) == TYPE_VECTOR2:
			return structure_found
	for id_value in entity_ids:
		var entity_id := int(id_value)
		if entity_id <= 0:
			continue
		var entity_found: Variant = spatial_position_probe.call(entity_id, false)
		if typeof(entity_found) == TYPE_VECTOR2:
			return entity_found
	return null


func _route_weapon_swing(object_id: String, sequence: int) -> Dictionary:
	## Weapon-class swing/fire SFX from converted doc evidence: siege fires its
	## authored launch sound (trebuchet → TrebuchetLaunchVoice, never a bow
	## draw), monsters swing with their own authored class; everyone else keeps
	## the legacy ranged/melee split.
	var weapon_sfx: Dictionary = playable_unit_weapon_sfx.get(object_id, {})
	var category := String(playable_unit_categories.get(object_id, ""))
	# HIGHEST priority: the unit's own authored weapon FireFX sound, converted
	# by the importer as the `weapon` audioRoutes owner (Weapon BoromirSword /
	# FireFX = FX_GondorSwordHit -> FXList Sound Name = ImpactSword01). This
	# closes the old "every melee unit swings with the same clip" gap for any
	# pack that carries the chain.
	if String(weapon_sfx.get("weapon", "")) != "":
		return route_audio_event(String(weapon_sfx["weapon"]), sequence)
	if category == "siege" and String(weapon_sfx.get("fire", "")) != "":
		return route_audio_event(String(weapon_sfx["fire"]), sequence)
	if category == "monster" and String(weapon_sfx.get("swing", "")) != "":
		return route_audio_event(String(weapon_sfx["swing"]), sequence)
	# REMAINING NAMED GAP - FAIL VISIBLE, NOT FAIL PRETTY.
	#
	# A unit reaching this point has NO bound weapon FireFX sound in its pack:
	# either the weapon's FXList genuinely authors no Sound (recorded by the
	# importer in presentation.weaponAudioGaps as `fxlist-authors-no-sound`) or
	# the loaded pack predates the weapon-chain emission.
	#
	# This used to play `ArrowDrawBow` for anything ranged and
	# `SwordShingClean1ForHordes` for everything else. Both are INVENTED
	# substitutions: no retail weapon chain reachable from these objects names
	# either event, so the runtime was manufacturing a sound the retail data
	# never asked for. With 173 of the 187 loaded unit documents carrying a real
	# bound weapon event, the honest answer for the remaining handful is silence
	# plus a number, so the gap gets closed by importing the missing chain
	# instead of hidden behind a plausible clink.
	generic_weapon_swing_fallbacks[object_id] = int(generic_weapon_swing_fallbacks.get(object_id, 0)) + 1
	return _rejection("no_authored_weapon_sfx", "", object_id, "sfx", sequence)


func route_crush_impact(object_id: String, sequence: int) -> Dictionary:
	## THE UNIT'S AUTHORED `SoundImpact`, PRESERVED FOR ITS REAL RETAIL USE.
	##
	## Retail's `SoundImpact` is the crush / knockback thud, not a per-hit
	## sound: `gondorfighter.ini:768` gives an INFANTRY object
	## `SoundImpact = ImpactHorse`, and that same file's knockback death module
	## (`gondorfighter.ini:913-919`) suppresses its own death sound with the
	## comment "sound already played by SoundImpact = ... ".
	##
	## NOTHING CALLS THIS YET. The sim emits no crush / knockback / trample
	## event for this lane to hang it on; when one lands, this is the route it
	## should use, and it must NOT be re-attached to `combat.hit` (see that
	## branch for why).
	var impact_id := String(playable_unit_impact.get(object_id, ""))
	if impact_id == "":
		return _rejection("no_authored_sound_impact", "", object_id, "sfx", sequence)
	return route_audio_event(impact_id, sequence)


func resolve_death_animation(event: Dictionary) -> Dictionary:
	## Prefer clip/frame already on the sim event. When the combat lane has
	## not threaded them yet, use the live battalion's playing death clip so
	## AnimationSound does not fall back to lowest-id.
	var clip := String(event.get("clip", ""))
	var frame := int(event.get("frame", -1))
	if clip != "":
		return {"clip": clip, "frame": frame, "source": "event"}
	var pick := _live_battalion_death_pick(int(event.get("target_id", 0)), int(event.get("member_index", -1)))
	if not pick.is_empty() and String(pick.get("clip", "")) != "":
		if frame >= 0:
			pick["frame"] = frame
		return pick
	return {"clip": "", "frame": frame, "source": "absent"}


func _live_battalion_death_pick(target_id: int, member_index: int) -> Dictionary:
	var tree := get_tree()
	if tree == null or target_id <= 0:
		return {}
	for node in tree.get_nodes_in_group("retail_battalion"):
		if node == null or not node.has_method("death_animation_pick"):
			continue
		if int(node.get("entity_id")) != target_id:
			continue
		var pick: Dictionary = node.call("death_animation_pick", member_index)
		if typeof(pick) == TYPE_DICTIONARY:
			return pick
	return {}


func _route_bodyfall(object_id: String, sequence: int, clip: String = "", frame: int = -1) -> Dictionary:
	## THE UNIT'S OWN authored bodyfall first, for every category — see
	## `_bodyfall_id_for_document`. Only a unit that binds none at all reaches
	## the class rule below (horse impact for cavalry, the generic soldier leaf
	## for infantry/heroes), and siege/monster still fails closed rather than
	## borrowing a human thud.
	##
	## When typed AnimationSoundClientBehavior rows are attached, a clip pick
	## wins. The lowest-id bodyfall is only the authored-clip-absent fallback.
	var typed := select_animation_sound(object_id, clip, frame, "bodyfall")
	if not typed.is_empty():
		return route_audio_event(String(typed.get("eventId", "")), sequence)
	if playable_unit_animation_sounds.has(object_id) and clip != "":
		if frame >= 0:
			return _rejection("animation-sound-frame-pending", clip, object_id, "sfx", sequence)
		return _rejection("animation-sound-clip-unmatched", clip, object_id, "sfx", sequence)
	var doc_bodyfall := String(playable_unit_bodyfall.get(object_id, ""))
	if doc_bodyfall != "":
		return route_audio_event(doc_bodyfall, sequence)
	var category := String(playable_unit_categories.get(object_id, ""))
	if category in ["siege", "monster"]:
		return _rejection("no_authored_bodyfall", "", object_id, "sfx", sequence)
	return route_audio_event("ImpactHorse" if _is_cavalry_object(object_id) else "BodyFallSoldier", sequence)


func _consume_structure_damage(event: Dictionary, sequence: int, world_position: Variant = null) -> void:
	var structure_kind := String(event.get("structure_kind", ""))
	var damaged_id := _structure_contract_event("damaged", structure_kind)
	var really_id := _structure_contract_event("really_damaged", structure_kind)
	if damaged_id == "" and really_id == "":
		# No converted per-structure evidence: legacy generic stone damage.
		_play_sfx(route_audio_event("BuildingLightDamageStone", sequence), world_position)
		return
	# Retail SoundOnDamaged/SoundOnReallyDamaged fire on ENTERING the damaged
	# bands (the structure doc's own maxHealthDamaged/ReallyDamaged fractions),
	# not on every hit; the stage is tracked per structure id.
	var maximum := maxi(1, int(event.get("maximum_health", 0)))
	var fraction := clampf(float(int(event.get("health", 0))) / float(maximum), 0.0, 1.0)
	var really_floor := _structure_contract_fraction("really_damaged_fraction", structure_kind)
	var damaged_floor := _structure_contract_fraction("damaged_fraction", structure_kind)
	var stage := "intact"
	if really_id != "" and fraction <= really_floor:
		stage = "really_damaged"
	elif damaged_id != "" and fraction <= damaged_floor:
		stage = "damaged"
	var structure_id := int(event.get("target_id", 0))
	var previous := String(_structure_damage_stage.get(structure_id, "intact"))
	if stage == previous:
		return
	_structure_damage_stage[structure_id] = stage
	match stage:
		"damaged":
			_play_structure_stage_sound(damaged_id, "BuildingLightDamageStone", sequence, world_position)
		"really_damaged":
			_play_structure_stage_sound(really_id, "BuildingHeavyDamageStone", sequence, world_position)


func _consume_structure_destroyed(event: Dictionary, sequence: int, world_position: Variant = null) -> void:
	var structure_kind := String(event.get("structure_kind", ""))
	_structure_damage_stage.erase(int(event.get("target_id", 0)))
	_play_structure_stage_sound(_structure_contract_event("really_damaged", structure_kind), "BuildingHeavyDamageStone", sequence, world_position)
	_play_structure_stage_sound(_structure_contract_event("collapse", structure_kind), "BuildingSink", sequence, world_position)


func _play_structure_stage_sound(doc_id: String, generic_id: String, sequence: int, world_position: Variant = null) -> void:
	## The structure document's converted event leads; when the mounted packs
	## cannot route it (the id or its samples are not cooked yet) the legacy
	## generic plays instead — the same fallback the slice always used — so a
	## converted-but-unmounted id never silences a building.
	if doc_id != "":
		_ensure_authored_structure_damage_route(doc_id)
		var doc_result := route_audio_event(doc_id, sequence)
		if bool(doc_result.get("ok", false)):
			_play_sfx(doc_result, world_position)
			return
	_play_sfx(route_audio_event(generic_id, sequence), world_position)


func _ensure_authored_structure_damage_route(event_id: String) -> void:
	var key := event_id.to_lower()
	if audio_event_routes.has(key) or not AUTHORED_STRUCTURE_DAMAGE_EVENTS.has(event_id):
		return
	var content_db := get_node_or_null("/root/ContentDB")
	if content_db == null:
		return
	var authored: Dictionary = AUTHORED_STRUCTURE_DAMAGE_EVENTS[event_id]
	var leaves: Array[Dictionary] = []
	for sample_value in authored.get("samples", []):
		var sample_id := String(sample_value)
		var relative := "audio/men/%s.wav" % sample_id.to_lower()
		var path := String(content_db.call("resolve_asset", relative, pack_root))
		if path == "" or not _is_resolved_audio_path(path) or not _has_supported_audio_header(path):
			return
		leaves.append({"sample_id": sample_id, "path": path, "stream": null, "validated_path": true, "weight": 1})
	if leaves.size() != Array(authored.get("samples", [])).size():
		return
	audio_event_routes[key] = {
		"event_id": event_id,
		"source": "retail-source-exact-registry-compatibility",
		"definition": {"parameters": Array(authored.get("parameters", [])).duplicate(true)},
		"leaves": leaves,
	}
	route_failures.erase(key)


func _structure_contract_event(role: String, structure_kind: String) -> String:
	if structure_kind == "":
		return ""
	var rows: Variant = structure_audio_contract.get(role, {})
	return String((rows as Dictionary).get(structure_kind, "")) if typeof(rows) == TYPE_DICTIONARY else ""


func _structure_contract_fraction(role: String, structure_kind: String) -> float:
	var rows: Variant = structure_audio_contract.get(role, {})
	if typeof(rows) != TYPE_DICTIONARY or not (rows as Dictionary).has(structure_kind):
		return 0.0
	return clampf(float((rows as Dictionary)[structure_kind]), 0.0, 1.0)


func _play_structure_eva(event: Dictionary, role: String, sequence: int, now_msec: int = -1) -> void:
	var eva_id := _structure_contract_event(role, String(event.get("structure_kind", "")))
	if eva_id != "":
		_play_eva_announcement(eva_id, sequence, now_msec)


func _play_created_eva(event: Dictionary, sequence: int, now_msec: int) -> void:
	## Retail keys creation announcements per OBJECT: VoiceCreated = EVA:<event>
	## (ChildObject inherits the parent) and spawn-FX EvaEventOwner on
	## InitialSpawnFX (fortress heroes whose VoiceCreated was rehooked) are
	## compiled into createdEvents. "HeroCreated" itself is authored by no
	## retail side, so it only remains as the legacy path for packs predating
	## the createdEvents schema, where it fails closed exactly as before. An
	## object retail gives no create hook fails closed here; a unit whose
	## event authors no line for the local side fails closed one step deeper,
	## on the side map. Production object_id is the adapter slug
	## (`bfme2.object.mordor-sauron-ring-hero`), not the CST name the map
	## stores; `_created_event_for_object` bridges those key spaces.
	var created_map: Variant = structure_audio_contract.get("eva_created_events", {})
	if typeof(created_map) != TYPE_DICTIONARY or (created_map as Dictionary).is_empty():
		_play_eva_announcement("HeroCreated", sequence, now_msec)
		return
	var object_id := String(event.get("object_id", ""))
	var eva_id := _created_event_for_object(object_id)
	if eva_id == "":
		_eva_rejection("eva_created_unauthored", object_id, sequence)
		return
	_play_eva_announcement(eva_id, sequence, now_msec)


func _created_event_for_object(object_id: String) -> String:
	if object_id == "":
		return ""
	var created_map: Variant = structure_audio_contract.get("eva_created_events", {})
	if typeof(created_map) != TYPE_DICTIONARY:
		return ""
	var table := created_map as Dictionary
	var direct := String(table.get(object_id, ""))
	if direct != "":
		return direct
	var folded := object_id.to_lower()
	# Production eva.hero_created carries PlayableUnitAdapter._runtime_id
	# (`bfme2.object.` + slug of the CST name). createdEvents is keyed by
	# those CST names. Exact / casefold never hit `bfme2.object.mordor-sauron-
	# ring-hero` against `MordorSauron_RingHero`; slug with the same function
	# the adapter used to emit.
	var want_runtime := object_id
	if not want_runtime.begins_with("bfme2.object."):
		want_runtime = PlayableUnitAdapter._runtime_id(object_id)
	for key_value in table.keys():
		var key := String(key_value)
		if key.to_lower() == folded:
			return String(table.get(key_value, ""))
		if PlayableUnitAdapter._runtime_id(key) == want_runtime:
			return String(table.get(key_value, ""))
	return ""


func play_eva_event(eva_id: String, sequence: int = 0, now_msec: int = -1) -> Dictionary:
	return _play_eva_announcement(eva_id, sequence, now_msec)


func play_ring_pickup_event(relationship: String, authored_local_eva: String, sequence: int, now_msec: int) -> Dictionary:
	## Perspective is resolved by the presentation owner before this call. An
	## absent carrier or ring contract is a named refusal, never an enemy guess.
	var eva_id := ""
	match relationship:
		"local":
			eva_id = authored_local_eva
			if eva_id == "":
				return _eva_rejection("ring_eva_unavailable", "RingPickedUpLocal", sequence)
		"allied":
			eva_id = "AlliedPlayerGainsRing"
		"enemy":
			eva_id = "EnemyPlayerGainsRing"
		"carrier-unavailable":
			return _eva_rejection("ring_carrier_unavailable", "RingPickedUpLocal", sequence)
		_:
			return _eva_rejection("ring_perspective_unavailable", "RingPickedUpLocal", sequence)
	return _play_eva_announcement(eva_id, sequence, now_msec)


func _eva_rejection(reason: String, eva_id: String, sequence: int) -> Dictionary:
	var diagnostic := "%s:%s:%d" % [reason, eva_id, sequence]
	if not eva_diagnostics.has(diagnostic):
		eva_diagnostics.append(diagnostic)
		eva_diagnostics.sort()
	return _rejection(reason, eva_id, faction_side, "eva", sequence)


func _play_eva_announcement(eva_id: String, sequence: int, now_msec: int = -1, allow_delay_deferral: bool = true) -> Dictionary:
	## Retail eva.ini binds each announcement to a per-side Camp* sound set.
	## The slice carries that side map in its structure audio contract; a side
	## or event the converted evidence does not cover fails closed to silence.
	if now_msec < 0:
		return _eva_rejection("eva_clock_unavailable", eva_id, sequence)
	var eva_events: Variant = structure_audio_contract.get("eva_events", {})
	if typeof(eva_events) != TYPE_DICTIONARY or faction_side == "":
		return _eva_rejection("eva_contract_unavailable", eva_id, sequence)
	var side_sounds: Variant = (eva_events as Dictionary).get(eva_id, {})
	if typeof(side_sounds) != TYPE_DICTIONARY:
		return _eva_rejection("eva_event_unavailable", eva_id, sequence)
	var sound_id := String((side_sounds as Dictionary).get(faction_side, ""))
	if sound_id == "":
		return _eva_rejection("eva_side_unavailable", eva_id, sequence)
	var semantics_all: Variant = structure_audio_contract.get("eva_semantics", {})
	if typeof(semantics_all) != TYPE_DICTIONARY or not (semantics_all as Dictionary).has(eva_id):
		return _eva_rejection("eva_semantics_unavailable", eva_id, sequence)
	var semantics: Dictionary = (
		(semantics_all as Dictionary).get(eva_id, {}) as Dictionary
		if typeof(semantics_all) == TYPE_DICTIONARY else {}
	)
	var priority := int(semantics.get("priority", 0))
	var cooldown_ms := int(semantics.get("cooldownMs", 0))
	var clock := now_msec
	# Retail OtherEvaEventsToBlock (compiled `blockEvents`): while the blocking
	# event plays, the events it names do not start. Old packs carry no
	# blockEvents, so this state stays empty and nothing changes for them.
	if clock < int(eva_blocked_until_msec.get(eva_id.to_lower(), -1)):
		return _eva_rejection("eva_blocked", eva_id, sequence)
	var previous := int(eva_last_played_msec.get(eva_id, -cooldown_ms - 1))
	if cooldown_ms > 0 and clock - previous < cooldown_ms:
		return _eva_rejection("eva_cooldown", eva_id, sequence)
	# Requests surfaced in one presentation timestamp arbitrate by retail
	# Priority. A lower/equal event never interrupts a higher one; a higher
	# event may replace it. This state is presentation-only and never hashed.
	if clock == eva_arbitration_msec and priority <= eva_arbitration_priority:
		return _eva_rejection("eva_priority", eva_id, sequence)
	# Retail MillisecondsToWaitBeforePlaying (compiled `delayMs`, e.g.
	# MountainTrollCreated's "Wait until really ready" 3000 ms): the first
	# accepted request does not sound; it is held and played once the wait has
	# elapsed (see `flush_due_eva_events`). Repeat requests while held keep the
	# original due clock rather than stacking a second announcement.
	var delay_ms := int(semantics.get("delayMs", 0))
	if allow_delay_deferral and delay_ms > 0:
		for pending in eva_pending_delays:
			if String(pending.get("eva_id", "")) == eva_id:
				var held := _rejection("eva_delay", eva_id, faction_side, "eva", sequence)
				held["due_msec"] = int(pending.get("due_msec", clock + delay_ms))
				return held
		var due := clock + delay_ms
		eva_pending_delays.append({"eva_id": eva_id, "due_msec": due, "sequence": sequence})
		var deferral := _rejection("eva_delay", eva_id, faction_side, "eva", sequence)
		deferral["due_msec"] = due
		return deferral
	var routed := route_audio_event(sound_id, sequence)
	if not bool(routed.get("ok", false)):
		return routed
	eva_last_played_msec[eva_id] = clock
	eva_arbitration_msec = clock
	eva_arbitration_priority = priority
	var block_events: Variant = semantics.get("blockEvents", [])
	if typeof(block_events) == TYPE_ARRAY and not (block_events as Array).is_empty():
		# The bytes never state the block window; the field name reads as
		# "while this event plays", so the window is the played sound's decoded
		# length - deterministic from the pack bytes. With no measurable stream
		# the event's own cooldown is the conservative stand-in.
		var block_window_ms := cooldown_ms
		var stream: Variant = routed.get("stream")
		if stream is AudioStream:
			block_window_ms = maxi(int(round((stream as AudioStream).get_length() * 1000.0)), 0)
		if block_window_ms > 0:
			for blocked_value in block_events as Array:
				var blocked_id := String(blocked_value).to_lower()
				eva_blocked_until_msec[blocked_id] = maxi(
					int(eva_blocked_until_msec.get(blocked_id, -1)), clock + block_window_ms
				)
	_play_sfx(routed)
	routed["eva_id"] = eva_id
	routed["priority"] = priority
	routed["cooldown_ms"] = cooldown_ms
	return routed


func flush_due_eva_events(now_msec: int) -> Array:
	## Play every delay-deferred announcement whose wait has elapsed at this
	## clock. Driven by the event stream (`_consume_event`) and callable
	## directly; a deferred line whose game goes quiet sounds at the next
	## announcement clock, never instantly and never twice.
	var results: Array = []
	var kept: Array[Dictionary] = []
	for pending in eva_pending_delays:
		if int(pending.get("due_msec", 0)) <= now_msec:
			results.append(_play_eva_announcement(
				String(pending.get("eva_id", "")), int(pending.get("sequence", 0)), now_msec, false
			))
		else:
			kept.append(pending)
	eva_pending_delays = kept
	return results


func _upgrade_complete_eva_id(upgrade_id: String) -> String:
	var folded := upgrade_id.to_lower()
	for row in [
		["banner", "UpgradeBannerCarrierTechnologyReady"],
		["flamearrow", "UpgradeFlameArrowsReady"],
		["flamingarrow", "UpgradeFlameArrowsReady"],
		["firearrow", "UpgradeFlameArrowsReady"],
		["forgedblade", "UpgradeForgedBladesReady"],
		["heavyarmor", "UpgradeHeavyArmorReady"],
	]:
		if folded.contains(String(row[0])):
			return String(row[1])
	return ""


func _object_id_for_event(event: Dictionary, entity_id: int) -> String:
	var declared := String(event.get("object_id", ""))
	if _active_roster_object_ids().has(declared):
		return declared
	return String(_entity_object_ids.get(entity_id, ""))


func route_roster_voice(object_id: String, kind: String, sequence: int, form: String = "") -> Dictionary:
	if not _active_roster_object_ids().has(object_id):
		return _rejection("unknown_roster_object", "", object_id, kind, sequence)
	if not _all_voice_kinds().has(kind):
		return _rejection("unknown_voice_kind", "", object_id, kind, sequence)
	# AUTHORED SOUND STATE FIRST. `eomer.ini:726-730` names the mounted event
	# outright (`SoundState = MOUNTED / VoiceMove = EomerVoiceMoveMounted`); the
	# `_voice_candidate_form` id-spelling heuristic is only for units that ship
	# no `ModelConditionSoundSelectorClientBehavior`.
	var state_key := String(SOUND_STATE_VOICE_KEYS.get(kind, ""))
	if state_key != "":
		var state_event := sound_state_event(object_id, state_key)
		if state_event != "":
			var state_route: Dictionary = audio_event_routes.get(state_event.to_lower(), {})
			if not state_route.is_empty():
				return _route_definition(state_route, sequence, object_id, kind)
	if form != "":
		var form_route: Dictionary = (roster_voice_form_routes.get(object_id, {}) as Dictionary).get("%s/%s" % [form, kind], {})
		if not form_route.is_empty():
			return _route_definition(form_route, sequence, object_id, kind)
	var by_kind: Dictionary = roster_voice_routes.get(object_id, {})
	if not by_kind.has(kind):
		var expected: Dictionary = _voice_event_ids_for_object(object_id)
		var candidates: Array = expected.get(kind, [])
		return _rejection("missing_event", String(candidates[0]) if not candidates.is_empty() else "", object_id, kind, sequence)
	return _route_definition(by_kind[kind], sequence, object_id, kind)


func _active_roster_object_ids() -> Array[String]:
	var active := ROSTER_OBJECT_IDS.duplicate()
	for runtime in _playable_unit_runtime_documents().values():
		var object_id := PlayableUnitAdapter.runtime_member_id(runtime as Dictionary)
		if object_id != "" and not active.has(object_id):
			active.append(object_id)
	return active


func _playable_unit_runtime_documents() -> Dictionary:
	if not scoped_playable_unit_documents.is_empty():
		return scoped_playable_unit_documents
	var content_db := get_node_or_null("/root/ContentDB")
	return content_db.get_playable_unit_runtimes() if content_db != null else {}


func _voice_event_ids_for_object(object_id: String) -> Dictionary:
	if ROSTER_VOICE_EVENT_IDS.has(object_id):
		return ROSTER_VOICE_EVENT_IDS[object_id]
	return (playable_unit_audio_events.get(object_id, {}) as Dictionary).get("voice", {})


func _load_playable_unit_audio_routes() -> void:
	var content_db := get_node_or_null("/root/ContentDB")
	if content_db == null:
		return
	var voice_kinds: Array[String] = []
	voice_kinds.append_array(REQUIRED_VOICE_KINDS)
	voice_kinds.append_array(EXTRA_VOICE_KINDS)
	for document_value in _playable_unit_runtime_documents().values():
		var document := document_value as Dictionary
		var object_id := PlayableUnitAdapter.runtime_member_id(document)
		var unit_id := PlayableUnitAdapter.runtime_unit_id(document)
		var category := String(document.get("category", ""))
		var voice: Dictionary = {}
		for kind in voice_kinds:
			voice[kind] = PlayableUnitAdapter.audio_event_ids(document, kind)
		var production := PlayableUnitAdapter.production_audio_event_ids(document)
		playable_unit_audio_events[object_id] = {"voice": voice, "production": production}
		playable_unit_categories[object_id] = category
		if unit_id != object_id:
			playable_unit_audio_events[unit_id] = {"voice": voice, "production": production}
			playable_unit_categories[unit_id] = category
		var registration: Dictionary = document.get("registration", {}) as Dictionary
		var bindings: Dictionary = registration.get("audioBindings", {}) as Dictionary
		var resolutions: Dictionary = registration.get("audioResolution", {}) as Dictionary
		# Provenance the readiness rule below needs: WHO authored this document
		# and WHAT it brought with it. A converted pack unit ships one binding
		# (or an `authored-silent` resolution) per authored event; a created hero
		# is synthesized at runtime from cah.system and ships neither.
		# TRUTH-CHECKED, not merely present: `createAHero: false` (or an empty
		# marker) is not a created hero and must stay strict. `has()` would have
		# handed the degradation to any document that carried the key at all.
		var created_hero := _marker_is_truthy(registration.get("createAHero", false))
		var declared_bindings: Dictionary = {}
		for binding_key in bindings.keys():
			declared_bindings[String(binding_key).to_lower()] = true
		for resolution_key in resolutions.keys():
			declared_bindings[String(resolution_key).to_lower()] = true
		_record_audio_provenance(object_id, created_hero, declared_bindings)
		if unit_id != object_id:
			# The container id is an ALIAS several documents can legitimately
			# claim (a horde and its member share one). The sibling tables above
			# are last-writer-wins on that alias; these two are not, because
			# either direction of a stray overwrite loosens the closure rule:
			# see _record_audio_provenance.
			_record_audio_provenance(unit_id, created_hero, declared_bindings)
		for event_id_value in bindings.keys():
			var event_id := String(event_id_value)
			var leaves: Array[Dictionary] = []
			for relative_value in Array(bindings[event_id_value]):
				var path := String(content_db.call("resolve_asset", String(relative_value), String(document.get("_pack_root", ""))))
				if path == "":
					continue
				leaves.append({"sample_id": path.get_file().get_basename(), "path": path, "weight": 1, "validated_path": true})
			if not leaves.is_empty():
				# THE AUTHORED AudioEvent PARAMETERS TRAVEL WITH THE ROUTE.
				#
				# A playable-unit binding resolves straight to cooked sample
				# paths, so this table used to carry no `definition` at all and
				# `_sfx_semantics` handed every one of them neutral values:
				# full volume, no `Limit`, no `PitchShift`. That is the owner's
				# "weird footsteps for heroes like Eomer": retail authors
				# `soundeffects.ini:19127 AudioEvent FootstepDirtA` with
				# `Volume = 30`, `Limit = 3`, `PitchShift = -10 10`, and we were
				# playing those 36 dirt leaves at 0 dB with unlimited
				# concurrency. The pack already ships the block verbatim in
				# `data/audio_events.json`; attach it so the authored mix
				# applies. An event the pack does not define stays neutral —
				# absent parameters are never invented.
				var route_row: Dictionary = {"event_id": event_id, "source": "playable-unit-runtime", "leaves": leaves}
				var authored_definition: Variant = content_db.call("get_retail_audio_event", event_id)
				if typeof(authored_definition) == TYPE_DICTIONARY and not (authored_definition as Dictionary).is_empty():
					route_row["definition"] = (authored_definition as Dictionary).duplicate(true)
				audio_event_routes[event_id.to_lower()] = route_row
			elif String(resolutions.get(event_id_value, "")) == "authored-silent":
				audio_event_routes[event_id.to_lower()] = {"event_id": event_id, "source": "authored-silent", "leaves": [], "authored_silent": true}
		var weapon_sfx := _weapon_sfx_for_document(document, category, bindings)
		if not weapon_sfx.is_empty():
			playable_unit_weapon_sfx[object_id] = weapon_sfx
			if unit_id != object_id:
				playable_unit_weapon_sfx[unit_id] = weapon_sfx
		var bodyfall_id := _bodyfall_id_for_document(bindings)
		if bodyfall_id != "":
			playable_unit_bodyfall[object_id] = bodyfall_id
			if unit_id != object_id:
				playable_unit_bodyfall[unit_id] = bodyfall_id
		bind_animation_sound_contracts(object_id, PlayableUnitAdapter.module_contracts(document))
		bind_sound_state_contracts(object_id, PlayableUnitAdapter.module_contracts(document))
		bind_base_skeleton(object_id, document)
		if unit_id != object_id:
			bind_animation_sound_contracts(unit_id, PlayableUnitAdapter.module_contracts(document))
			bind_sound_state_contracts(unit_id, PlayableUnitAdapter.module_contracts(document))
			bind_base_skeleton(unit_id, document)
		var impact_id := _impact_id_for_document(document, bindings)
		if impact_id != "":
			playable_unit_impact[object_id] = impact_id
			if unit_id != object_id:
				playable_unit_impact[unit_id] = impact_id


func bind_animation_sound_contracts(object_id: String, contracts: Array) -> int:
	## Attach compile-shaped AnimationSoundClientBehavior rows. Empty object_id
	## is ignored. Existing rows for the id are replaced.
	if object_id == "":
		return 0
	var sounds: Array = []
	for contract_value in contracts:
		if typeof(contract_value) != TYPE_DICTIONARY:
			continue
		var contract := contract_value as Dictionary
		if String(contract.get("module", "")) != "AnimationSoundClientBehavior":
			continue
		if String(contract.get("extraction", "")) != "typed":
			continue
		var status := String(contract.get("runtimeStatus", contract.get("runtime_status", "")))
		if status != "executable":
			continue
		var fields: Dictionary = contract.get("fields", {}) as Dictionary
		for sound_value in fields.get("AnimationSound", []) as Array:
			if typeof(sound_value) != TYPE_DICTIONARY:
				continue
			var sound := sound_value as Dictionary
			var event_id := String(sound.get("eventId", ""))
			var animation := String(sound.get("animation", ""))
			if event_id == "" or animation == "":
				continue
			sounds.append({
				"eventId": event_id,
				"animation": animation,
				"frames": (sound.get("frames", []) as Array).duplicate(),
			})
	# NAME THE FOOTSTEP GAP AT BIND TIME, never at play time with a substitute.
	# Retail authors footsteps per object as frame-keyed `AnimationSound` rows
	# (`eomer.ini:746-747`), and most objects author NONE — `menporter.json`,
	# `gondorfighterhorde.json` and every other horde in the shipped men pack
	# carry zero footstep rows, so retail plays nothing for their steps and so
	# must we. This row exists so "silent" is visibly *authored* silence.
	var footstep_rows := 0
	for sound_value in sounds:
		if _is_footstep_event(String((sound_value as Dictionary).get("eventId", ""))):
			footstep_rows += 1
	if footstep_rows == 0:
		footstep_gaps["no-authored-footstep-rows:%s" % object_id] = true
	else:
		footstep_gaps.erase("no-authored-footstep-rows:%s" % object_id)
	if sounds.is_empty():
		playable_unit_animation_sounds.erase(object_id)
		return 0
	playable_unit_animation_sounds[object_id] = sounds
	return sounds.size()


func bind_sound_state_contracts(object_id: String, contracts: Array) -> int:
	## Attach the authored `ModelConditionSoundSelectorClientBehavior` table.
	## `eomer.ini:726-730` authors `SoundState = MOUNTED` with
	## `VoiceMove = EomerVoiceMoveMounted`, `VoiceSelect = EomerVoiceSelectMountedMS`
	## and `VoiceAttackCharge = EomerVoiceAttackChargeMounted`. That table — not
	## an event-id spelling heuristic — is what decides a mounted hero's voice.
	if object_id == "":
		return 0
	var states: Dictionary = {}
	for contract_value in contracts:
		if typeof(contract_value) != TYPE_DICTIONARY:
			continue
		var contract := contract_value as Dictionary
		if String(contract.get("module", "")) != "ModelConditionSoundSelectorClientBehavior":
			continue
		if String(contract.get("extraction", "")) != "typed":
			continue
		var status := String(contract.get("runtimeStatus", contract.get("runtime_status", "")))
		if status != "executable":
			continue
		var fields: Dictionary = contract.get("fields", {}) as Dictionary
		for row_value in fields.get("SoundState", []) as Array:
			if typeof(row_value) != TYPE_DICTIONARY:
				continue
			var row := row_value as Dictionary
			var sounds: Dictionary = row.get("sounds", {}) as Dictionary
			if sounds.is_empty():
				continue
			var resolved: Dictionary = {}
			for sound_key in sounds.keys():
				var entry: Variant = sounds[sound_key]
				var event_id := _sound_state_event_id(entry)
				if event_id != "":
					resolved[_scalar_text(sound_key)] = event_id
			if resolved.is_empty():
				continue
			for condition_value in row.get("conditions", []) as Array:
				var token := String(condition_value).strip_edges().to_upper()
				if token == "":
					continue
				var merged: Dictionary = states.get(token, {}) as Dictionary
				merged.merge(resolved, false)
				states[token] = merged
	if states.is_empty():
		playable_unit_sound_states.erase(object_id)
		return 0
	playable_unit_sound_states[object_id] = states
	return states.size()


func bind_base_skeleton(object_id: String, document: Dictionary) -> String:
	## The object's DEFAULT model, as authored. `eomer.ini:25` hangs
	## `RUEomer_SKN` off `DefaultModelConditionState`; the converter records it
	## as the default visual component. Its stem (`RUEOMER`) is the authored
	## separator between the object's own animation rows and rows authored on a
	## different skeleton (the mount's `RUHHs_Theo_SKL`).
	if object_id == "":
		return ""
	var registration: Dictionary = document.get("registration", {}) as Dictionary
	var visual: Dictionary = registration.get("visual", {}) as Dictionary
	var identifier := ""
	for component_value in visual.get("components", []) as Array:
		if typeof(component_value) != TYPE_DICTIONARY:
			continue
		var component := component_value as Dictionary
		if not bool(component.get("default", false)):
			continue
		for occurrence_value in component.get("authoredOccurrences", []) as Array:
			if typeof(occurrence_value) != TYPE_DICTIONARY:
				continue
			var occurrence := occurrence_value as Dictionary
			if String(occurrence.get("usage", "")) != "model":
				continue
			if not (occurrence.get("conditions", []) as Array).is_empty():
				continue
			identifier = String(occurrence.get("identifier", ""))
			break
		if identifier != "":
			break
	var stem := _skeleton_stem(identifier)
	if stem == "":
		playable_unit_base_skeletons.erase(object_id)
		return ""
	playable_unit_base_skeletons[object_id] = stem
	return stem


func set_unit_sound_state(object_id: String, tokens: Array) -> void:
	## Model-condition sound state for an object (["MOUNTED"] or []). The sim
	## owns the condition; this lane only consumes it.
	if object_id == "":
		return
	var normalized: Array[String] = []
	for token_value in tokens:
		var token := String(token_value).strip_edges().to_upper()
		if token != "" and not normalized.has(token):
			normalized.append(token)
	if normalized.is_empty():
		playable_unit_sound_state_active.erase(object_id)
		return
	playable_unit_sound_state_active[object_id] = normalized


func active_sound_state_tokens(object_id: String) -> Array:
	var tokens: Variant = playable_unit_sound_state_active.get(object_id, [])
	return (tokens as Array) if typeof(tokens) == TYPE_ARRAY else []


func sound_state_event(object_id: String, sound_key: String) -> String:
	## The authored event id for `sound_key` under the object's ACTIVE sound
	## state, or "" when no state is active or the state authors no such key.
	var states: Dictionary = playable_unit_sound_states.get(object_id, {}) as Dictionary
	if states.is_empty():
		return ""
	for token_value in active_sound_state_tokens(object_id):
		var state: Dictionary = states.get(String(token_value), {}) as Dictionary
		var event_id := String(state.get(sound_key, ""))
		if event_id != "":
			return event_id
	return ""


func _scalar_text(value: Variant) -> String:
	## `String(x)` is NOT total in GDScript — it throws on several Variant
	## types, and one throw inside `configure()` unwinds the whole audio bind
	## (observed: a SoundState row whose value was not a plain scalar aborted
	## `_load_playable_unit_audio_routes` mid-roster). Scalars convert; anything
	## else is "absent", which is the strict side.
	match typeof(value):
		TYPE_STRING, TYPE_STRING_NAME:
			return String(value).strip_edges()
		TYPE_INT, TYPE_FLOAT, TYPE_BOOL:
			return str(value).strip_edges()
	return ""


func _sound_state_event_id(entry: Variant) -> String:
	## A converted SoundState entry is `{"value": "EomerVoiceMoveMounted", ...}`.
	## A bare string is accepted too; a list takes its first scalar. Anything
	## else contributes no event id rather than a guessed one.
	match typeof(entry):
		TYPE_DICTIONARY:
			var row := entry as Dictionary
			var text := _scalar_text(row.get("value", null))
			if text != "":
				return text
			return _scalar_text(row.get("authored", null))
		TYPE_ARRAY:
			for item in entry as Array:
				var first := _sound_state_event_id(item)
				if first != "":
					return first
			return ""
	return _scalar_text(entry)


func _is_footstep_event(event_id: String) -> bool:
	## Retail spells the class into the event id: `FootstepDirtA`,
	## `HorseMoveFootsteps`, `BatteringRamGroupFootstep` (soundeffects.ini).
	return event_id.to_lower().contains("footstep")


func _skeleton_stem(identifier: String) -> String:
	## `RUEomer_SKL.RUEomer_RUNA` → RUEOMER; `RUEomer_SKN` → RUEOMER.
	var head := identifier.strip_edges()
	if head == "":
		return ""
	if head.contains("."):
		head = head.get_slice(".", 0)
	var upper := head.to_upper()
	if upper.ends_with("_SKL") or upper.ends_with("_SKN"):
		upper = upper.substr(0, upper.length() - 4)
	return upper


func route_footstep(object_id: String, sequence: int, clip: String = "", frame: int = -1) -> Dictionary:
	## THE UNIT'S OWN authored footstep row, or NOTHING.
	##
	## Retail hangs footsteps on frame-keyed `AnimationSoundClientBehavior` rows
	## (`eomer.ini:744-747`: `FootstepDirtA` on `RUEomer_SKL.RUEomer_RUNA`
	## frames 4 15, on `RUEomer_RUNB` frames 5 15 26 36). There is no class rule
	## behind them: an object that authors no footstep row makes no footstep
	## sound in retail either, and this function must never reach for a generic
	## step sample the way `_route_bodyfall` reaches for `BodyFallSoldier`.
	##
	## MOUNTED (`eomer.ini:65 ModelConditionState = MOUNTED`, whose animation
	## states at :153-242 are all `RUHHs_Theo_SKL.*`) rides the mount's
	## skeleton, so a mounted hero can only sound the rows authored on that
	## skeleton — never his own foot rows.
	if not playable_unit_animation_sounds.has(object_id):
		footstep_gaps["no-authored-footstep-rows:%s" % object_id] = true
		return _rejection("no_authored_footstep", "", object_id, "sfx", sequence)
	var typed := select_animation_sound(object_id, clip, frame, "footstep")
	if typed.is_empty():
		if frame >= 0:
			return _rejection("animation-sound-frame-pending", clip, object_id, "sfx", sequence)
		return _rejection("animation-sound-clip-unmatched", clip, object_id, "sfx", sequence)
	var gate := footstep_state_gate(object_id, String(typed.get("animation", "")))
	if gate != "":
		footstep_gaps[gate] = true
		return _rejection("mounted_footstep_suppressed", String(typed.get("eventId", "")), object_id, "sfx", sequence)
	return route_audio_event(String(typed.get("eventId", "")), sequence)


func footstep_state_gate(object_id: String, animation: String) -> String:
	## "" when the row may sound. Otherwise the named gap explaining why the
	## authored row is being withheld under the active model-condition state.
	if not active_sound_state_tokens(object_id).has("MOUNTED"):
		return ""
	var base_stem := String(playable_unit_base_skeletons.get(object_id, ""))
	if base_stem == "":
		# No authored default model recorded: refuse to guess which rows belong
		# to the mount, and say so rather than sounding a foot step on a horse.
		return "mounted-base-skeleton-unknown:%s" % object_id
	if _skeleton_stem(animation) != base_stem:
		return ""
	return "mounted-footstep-clip-missing:%s:%s" % [object_id, animation]


func select_animation_sound(object_id: String, clip: String, frame: int = -1, event_contains: String = "") -> Dictionary:
	## Pick the authored AnimationSound for this clip (and optional frame).
	## Clip match is exact, then suffix-after-dot, then casefolded containment.
	## A supplied frame must appear in the binding's Frames list. Unknown clips
	## fail closed (empty) instead of falling back to lowest event id.
	if clip == "" or not playable_unit_animation_sounds.has(object_id):
		return {}
	var clip_folded := clip.to_lower()
	var clip_leaf := _animation_clip_leaf(clip_folded)
	var needle := event_contains.to_lower()
	for sound_value in playable_unit_animation_sounds[object_id] as Array:
		var sound := sound_value as Dictionary
		var event_id := String(sound.get("eventId", ""))
		if needle != "" and not event_id.to_lower().contains(needle):
			continue
		var animation := String(sound.get("animation", "")).to_lower()
		var anim_leaf := _animation_clip_leaf(animation)
		var matched := (
			animation == clip_folded
			or anim_leaf == clip_folded
			or anim_leaf == clip_leaf
			or clip_folded.ends_with(anim_leaf)
			or animation.ends_with(clip_leaf)
		)
		if not matched:
			continue
		if frame >= 0:
			var frames: Array = sound.get("frames", []) as Array
			var hit := false
			for frame_value in frames:
				if int(frame_value) == frame:
					hit = true
					break
			if not hit:
				continue
		return {
			"eventId": event_id,
			"animation": String(sound.get("animation", "")),
			"frames": (sound.get("frames", []) as Array).duplicate(),
			"source": "typed-animation-sound",
		}
	return {}


func advance_animation_sounds(
	object_id: String,
	clip: String,
	previous_frame: float,
	current_frame: float,
	length_frames: float = -1.0,
	backwards: bool = false
) -> Array:
	## Fire each authored (clip, frame) pair the playhead just crossed.
	## LNDA frame 1 and frame 14 are different events on the same clip.
	if clip == "" or not playable_unit_animation_sounds.has(object_id):
		return []
	var fired: Array = []
	var clip_folded := clip.to_lower()
	var clip_leaf := _animation_clip_leaf(clip_folded)
	for sound_value in playable_unit_animation_sounds[object_id] as Array:
		var sound := sound_value as Dictionary
		var animation := String(sound.get("animation", "")).to_lower()
		var anim_leaf := _animation_clip_leaf(animation)
		var matched := (
			animation == clip_folded
			or anim_leaf == clip_folded
			or anim_leaf == clip_leaf
			or clip_folded.ends_with(anim_leaf)
			or animation.ends_with(clip_leaf)
		)
		if not matched:
			continue
		for frame_value in sound.get("frames", []) as Array:
			var cue := float(frame_value)
			if not ClipFrameClockScript.crossed(previous_frame, current_frame, cue, length_frames, backwards):
				continue
			fired.append({
				"eventId": String(sound.get("eventId", "")),
				"animation": String(sound.get("animation", "")),
				"frame": int(cue),
				"source": "typed-animation-sound-clock",
			})
	return fired


func consume_battalion_animation_sound_clocks() -> Array:
	## Live path: fire authored AnimationSound rows as each battalion clock
	## crosses their frames. Primed seeks (previousFrame < 0) do not fire.
	var tree := get_tree()
	if tree == null:
		return []
	var fired: Array = []
	var suppressed: Array = []
	for node in tree.get_nodes_in_group("retail_battalion"):
		if node == null or not node.has_method("member_clip_frame"):
			continue
		var object_id := String(node.get("object_id"))
		# The model-condition sound state the presentation is actually in. The
		# battalion owns the mount flag; this lane reads it and never writes it.
		_sync_sound_state_from_battalion(node, object_id)
		var member_count := int(node.get("member_count"))
		for member_index in range(member_count):
			var clock: Dictionary = node.call("member_clip_frame", member_index)
			if typeof(clock) != TYPE_DICTIONARY:
				continue
			var clip := String(clock.get("clip", ""))
			if clip == "":
				clip = String((node.get("member_current_clips") as Dictionary).get(member_index, ""))
			var current := float(clock.get("frame", -1.0))
			if current < 0.0 or clip == "":
				continue
			var cursor_key := "%s:%d:%s" % [int(node.get("entity_id")), member_index, clip]
			var previous := float(_consumed_clip_frames.get(cursor_key, -1.0))
			_consumed_clip_frames[cursor_key] = current
			if previous < 0.0 or is_equal_approx(previous, current):
				continue
			var crossed: Array = advance_animation_sounds(
				object_id,
				clip,
				previous,
				current,
				float(clock.get("lengthFrames", -1.0)),
				bool(clock.get("backwards", false))
			)
			for row_value in crossed:
				var row := row_value as Dictionary
				var row_event := String(row.get("eventId", ""))
				if _is_footstep_event(row_event):
					# Footsteps take the gated path: the MOUNTED state must not
					# sound a foot row (eomer.ini:65 / :744-747).
					var gap := footstep_state_gate(object_id, String(row.get("animation", "")))
					if gap != "":
						footstep_gaps[gap] = true
						suppressed.append(row)
						continue
				_animation_sound_sequence += 1
				_play_sfx(route_audio_event(row_event, _animation_sound_sequence))
				fired.append(row)
	last_animation_sound_clock_receipt = {
		"source": "typed-animation-sound-clock",
		"applied": fired.size(),
		"events": fired.duplicate(true),
		"suppressed": suppressed.duplicate(true),
	}
	return fired


func _sync_sound_state_from_battalion(node: Node, object_id: String) -> void:
	## Read-only probe of the presentation's mount flag. `retail_battalion.gd`
	## sets `_mount_presentation_applied` from `sync_mount_presentation()`; this
	## lane does not own that file and must not write it. A battalion that does
	## not expose the flag simply has no active sound state.
	if object_id == "" or node == null:
		return
	if not playable_unit_sound_states.has(object_id) and not playable_unit_animation_sounds.has(object_id):
		return
	var flag: Variant = node.get("_mount_presentation_applied")
	if typeof(flag) != TYPE_BOOL:
		return
	set_unit_sound_state(object_id, ["MOUNTED"] if bool(flag) else [])


func _process(_delta: float) -> void:
	if playable_unit_animation_sounds.is_empty():
		return
	consume_battalion_animation_sound_clocks()


func _animation_clip_leaf(name: String) -> String:
	## Last dotted token, never Godot's get_basename (that treats DIEA as an extension).
	var folded := name.to_lower().replace("\\", "/").get_file()
	if folded.contains("."):
		return folded.get_slice(".", folded.get_slice_count(".") - 1)
	return folded


func _bodyfall_id_for_document(bindings: Dictionary) -> String:
	## THE UNIT'S OWN AUTHORED BODYFALL, for every category.
	##
	## This used to answer only for siege and monster, which meant every
	## infantry, cavalry and hero death played one hardcoded `BodyFallSoldier`
	## - a leaf most of them do not even bind. Gondor Fighters author
	## `BodyFallGeneric1`; Boromir authors `BodyFallGenericNoArmor`. Both were
	## already shipped in the packs and neither was ever played.
	##
	## Lowest-id remains the authored-clip-absent fallback. Typed
	## AnimationSoundClientBehavior rows pick by clip/frame via
	## `select_animation_sound` when a death event names the playing clip,
	## or when `resolve_death_animation` reads it from the live battalion.
	var ids: Array = bindings.keys()
	ids.sort_custom(func(a: Variant, b: Variant) -> bool: return String(a).to_lower() < String(b).to_lower())
	for event_id_value in ids:
		if String(event_id_value).to_lower().contains("bodyfall"):
			return String(event_id_value)
	return ""


func _impact_id_for_document(document: Dictionary, bindings: Dictionary) -> String:
	## The object's own `SoundImpact` AudioEvent, straight off the converted
	## `registration.audioRoutes` (retail authors it on the Object block, e.g.
	## `SoundImpact = ImpactHorse` in gondorfighter.ini). Only accepted when the
	## same document also BINDS the event to real samples, so this can never
	## name a leaf the pack cannot play.
	var registration: Dictionary = document.get("registration", {}) as Dictionary
	var routes: Dictionary = registration.get("audioRoutes", {}) as Dictionary
	for owner_value in routes.values():
		if typeof(owner_value) != TYPE_DICTIONARY:
			continue
		for field_value in (owner_value as Dictionary).keys():
			if String(field_value).to_lower() != "soundimpact":
				continue
			for row_value in Array((owner_value as Dictionary)[field_value]):
				if typeof(row_value) != TYPE_DICTIONARY:
					continue
				var event_id := String((row_value as Dictionary).get("id", ""))
				if event_id != "" and bindings.has(event_id):
					return event_id
	return ""


func _weapon_sfx_for_document(document: Dictionary, category: String, bindings: Dictionary) -> Dictionary:
	## Retail weapon SFX class per unit, from converted doc evidence only.
	## Siege fire comes from the object's authored attack-animation
	## AnimationSound (trebuchet: TrebuchetLaunchVoice on the ATK clips);
	## monster melee uses the authored swing/impact leaves the doc binds
	## (Treebeard: TrollTreeSwingLight, ImpactEntGenericPunch/Kick).
	var registration: Dictionary = document.get("registration", {}) as Dictionary
	var routes: Dictionary = registration.get("audioRoutes", {}) as Dictionary
	## FIRST: the converted weapon chain (`weapon` owner) — retail authors the
	## swing/fire sound on the WEAPON (`Weapon BoromirSword / FireFX =
	## FX_GondorSwordHit` -> `FXList FX_GondorSwordHit / Sound / Name =
	## ImpactSword01`). Prefer the default-set PRIMARY FireFX row; only rows
	## the pack actually BINDS are eligible, so this can never name a leaf the
	## pack cannot play. ProjectileDetonationFX is the projectile IMPACT, not
	## the swing, and never routes here.
	var weapon_owner: Dictionary = routes.get("weapon", {}) as Dictionary
	if not weapon_owner.is_empty():
		var best_id := ""
		var best_rank := 99
		for row_value in Array(weapon_owner.get("FireFX", [])):
			if typeof(row_value) != TYPE_DICTIONARY:
				continue
			var row := row_value as Dictionary
			var event_id := String(row.get("id", ""))
			if event_id == "" or not bindings.has(event_id):
				continue
			var rank := 0 if bool(row.get("defaultSet", false)) else 2
			if String(row.get("weaponSlot", "")) != "PRIMARY":
				rank += 1
			if rank < best_rank:
				best_rank = rank
				best_id = event_id
		if best_id != "":
			return {"weapon": best_id}
	match category:
		"siege":
			for owner_value in routes.values():
				if typeof(owner_value) != TYPE_DICTIONARY:
					continue
				for row_value in Array((owner_value as Dictionary).get("AnimationSound", [])):
					if typeof(row_value) != TYPE_DICTIONARY:
						continue
					var event_id := String((row_value as Dictionary).get("id", ""))
					if event_id != "" and bindings.has(event_id):
						return {"fire": event_id}
		"monster":
			for event_id_value in bindings.keys():
				var lowered := String(event_id_value).to_lower()
				if lowered.contains("swing"):
					return {"swing": String(event_id_value)}
			for event_id_value in bindings.keys():
				var lowered := String(event_id_value).to_lower()
				if lowered.begins_with("impact") and (lowered.contains("punch") or lowered.contains("kick")):
					return {"swing": String(event_id_value)}
	return {}


func _play_first_unit_event(object_id: String, kind: String, sequence: int, command_id: String = "") -> void:
	var events: Dictionary = playable_unit_audio_events.get(object_id, {})
	var production: Dictionary = events.get("production", {})
	var candidates: Array = production.get(kind, [])
	if kind == "purchase" and command_id != "":
		candidates = (production.get("purchase_by_command", {}) as Dictionary).get(command_id, [])
	if not candidates.is_empty():
		_play_routed(route_audio_event(String(candidates[0]), sequence), voice_player)


func _is_ranged_object(object_id: String) -> bool:
	return object_id == ARCHER_OBJECT_ID or String(playable_unit_categories.get(object_id, "")) in ["ranged-infantry", "siege", "naval"]


func _is_cavalry_object(object_id: String) -> bool:
	return object_id == KNIGHT_OBJECT_ID or String(playable_unit_categories.get(object_id, "")) == "cavalry"


func route_audio_event(event_id: String, sequence: int) -> Dictionary:
	var key := event_id.to_lower()
	if not audio_event_routes.has(key):
		# Lazily promote any ContentDB-registered event into the route table on
		# first use (structure selects, UI clicks, the construction loop, spell
		# ids, EVA lines) — the same promotion the spell lane prototyped. A
		# failed build stays recorded and the rejection remains fail-closed.
		var built := _build_content_db_route(event_id)
		if bool(built.get("ok", false)):
			audio_event_routes[key] = built["route"]
			route_failures.erase(key)
		else:
			route_failures[key] = String(built.get("reason", "invalid_event"))
	var route: Dictionary = audio_event_routes.get(key, {})
	if route.is_empty():
		return _rejection(String(route_failures.get(key, "missing_event")), event_id, "", "sfx", sequence)
	return _route_definition(route, sequence, "", "sfx")


func _typed_selector_byte_receipt_check(event: Dictionary, routed: Dictionary) -> Dictionary:
	var receipt_value: Variant = event.get("wav_set_byte_identity_receipt", {})
	if typeof(receipt_value) != TYPE_DICTIONARY or (receipt_value as Dictionary).is_empty():
		return {"match": true, "selected_path": String(routed.get("path", "")), "selected_sha256": ""}
	if not bool(routed.get("ok", false)):
		return {"match": false, "selected_path": "", "selected_sha256": ""}
	var receipt := receipt_value as Dictionary
	var logical_ids: Variant = event.get("logical_event_ids", [])
	if typeof(logical_ids) != TYPE_ARRAY or logical_ids != receipt.get("logicalEventIds", []):
		return {"match": false, "selected_path": String(routed.get("path", "")), "selected_sha256": ""}
	var selected_path := String(routed.get("path", ""))
	if selected_path == "" or not FileAccess.file_exists(selected_path):
		return {"match": false, "selected_path": selected_path, "selected_sha256": ""}
	var selected_sha := FileAccess.get_sha256(selected_path).to_lower()
	var normalized_path := selected_path.replace("\\", "/").to_lower()
	for leaf_value in receipt.get("leaves", []) as Array:
		if typeof(leaf_value) != TYPE_DICTIONARY: continue
		var leaf := leaf_value as Dictionary
		var cooked_path := String(leaf.get("cookedPath", "")).replace("\\", "/").to_lower()
		var expected_sha := String(leaf.get("cookedSha256", ""))
		if cooked_path != "" and normalized_path.ends_with("/" + cooked_path) and expected_sha == selected_sha:
			return {"match": true, "selected_path": selected_path, "selected_sha256": selected_sha}
	return {"match": false, "selected_path": selected_path, "selected_sha256": selected_sha}


# Retail select voices for the legacy men structure roster, kept as the
# fallback for kinds whose structure document authors no VoiceSelect route.
const STRUCTURE_SELECT_EVENT_IDS := {
	"fortress": "MenFortressSelect",
	"barracks": "GondorBarracksSelect",
	"archery_range": "GondorArcherySelect",
	"stable": "GondorStableSelect",
	"farm": "GondorFarmSelect",
	"workshop": "GondorWorkshopSelect",
	"forge": "GondorForgeSelect",
	"well": "GondorWellSelect",
	"marketplace": "GondorMarketSelect",
	"statue": "GondorStatueSelect",
	"battle_tower": "MenArrowTowerExpansionSelect",
	"wall_hub": "NeutralWallHubSelect",
}


func play_structure_select(structure_kind: String) -> Dictionary:
	# Prefer the structure document's own authored VoiceSelect event (every
	# faction, not just men); fall back to the legacy men table for kinds the
	# docs do not cover. The route builds lazily from the pack registry.
	var event_id := _structure_contract_event("select", structure_kind)
	if event_id == "":
		event_id = String(STRUCTURE_SELECT_EVENT_IDS.get(structure_kind, ""))
	if event_id == "":
		return {}
	var sequence := _next_ui_sequence
	_next_ui_sequence += 1
	var result := route_audio_event(event_id, sequence)
	if bool(result.get("ok", false)):
		_play_routed(result, voice_player)
	return result


func play_ui_event(event_id: String) -> Dictionary:
	var sequence := _next_ui_sequence
	_next_ui_sequence += 1
	var result := route_audio_event(event_id, sequence)
	if bool(result.get("ok", false)):
		_play_sfx(result)
	_append_bounded_observability(intent_log, {
		"kind": "ui.sound",
		"sequence": sequence,
		"event_id": event_id,
		"accepted": bool(result.get("ok", false)),
		"reason": String(result.get("reason", "")),
	})
	return _observable_route_result(result) if bool(result.get("ok", false)) else result.duplicate(true)


func play_declared_structure_event(event_id: String, sequence: int, structure_id: int, phase: String) -> Dictionary:
	## Lifecycle contracts provide the event ID. This seam deliberately refuses
	## empty/unknown IDs and never substitutes the generic building sounds used
	## by legacy simulation events.
	if event_id == "" or structure_id <= 0 or phase == "":
		return _rejection("invalid_structure_lifecycle_event", event_id, "", phase, sequence)
	var result := route_audio_event(event_id, sequence)
	result["structure_id"] = structure_id
	result["phase"] = phase
	if bool(result.get("ok", false)):
		_play_sfx(result)
	_append_bounded_observability(intent_log, {
		"kind": "structure.lifecycle.audio",
		"sequence": sequence,
		"entity_id": structure_id,
		"phase": phase,
		"event_id": event_id,
		"accepted": bool(result.get("ok", false)),
		"reason": String(result.get("reason", "")),
	})
	return _observable_route_result(result) if bool(result.get("ok", false)) else result.duplicate(true)


func play_sealed_scenario_event(
	event_id: String,
	audio_closure: Dictionary,
	outputs: Array,
	route_pack_root: String,
	origin: Vector3,
	sequence: int
) -> Dictionary:
	## A neutral-prop death closure is intentionally self-contained: it may be
	## supplied before the newly addressed pack has been selected, so it cannot
	## borrow a same-named ContentDB event. Rebuild the one authored AudioEvent
	## from its sealed closure and send it through the normal retail SFX mixer.
	## There is no filename/event fallback and every sealed leaf is contained and
	## header-validated before deterministic weighted selection.
	if (
		event_id == "" or not event_id.is_valid_identifier()
		or route_pack_root == "" or origin == Vector3.INF or sequence <= 0
		or typeof(audio_closure.get("rootIds")) != TYPE_ARRAY
		or not (audio_closure.get("rootIds", []) as Array).has(event_id)
		or typeof(audio_closure.get("sampleIds")) != TYPE_ARRAY
		or outputs.is_empty()
	):
		return _rejection("invalid_sealed_scenario_event", event_id, "", "scenario-death-fx", sequence)
	var sample_ids := audio_closure.get("sampleIds", []) as Array
	if sample_ids.size() != outputs.size():
		return _rejection("sealed_scenario_sample_binding_drifted", event_id, "", "scenario-death-fx", sequence)
	var mod_loader := get_node_or_null("/root/ModLoader")
	if mod_loader == null:
		return _rejection("sealed_scenario_pack_resolver_is_unavailable", event_id, "", "scenario-death-fx", sequence)
	var output_by_sample: Dictionary = {}
	for index in sample_ids.size():
		var sample_id := String(sample_ids[index])
		var relative := String(outputs[index])
		var resolved := String(mod_loader.call("resolve_pack_path", route_pack_root, relative))
		if (
			sample_id == "" or output_by_sample.has(sample_id.to_lower())
			or resolved == "" or not FileAccess.file_exists(resolved)
			or not bool(mod_loader.call("path_is_within", route_pack_root, resolved))
			or not _is_resolved_audio_path(resolved)
			or not _has_supported_audio_header(resolved)
		):
			return _rejection("sealed_scenario_sample_is_invalid", event_id, "", "scenario-death-fx", sequence)
		output_by_sample[sample_id.to_lower()] = {"id": sample_id, "path": resolved}
	var definition: Dictionary = {}
	for row_value in audio_closure.get("events", []) as Array:
		if typeof(row_value) != TYPE_DICTIONARY:
			return _rejection("sealed_scenario_audio_closure_is_invalid", event_id, "", "scenario-death-fx", sequence)
		var row := row_value as Dictionary
		if String(row.get("id", "")).nocasecmp_to(event_id) == 0:
			if not definition.is_empty():
				return _rejection("sealed_scenario_event_is_ambiguous", event_id, "", "scenario-death-fx", sequence)
			definition = row
	if definition.is_empty() or typeof(definition.get("sounds")) != TYPE_ARRAY:
		return _rejection("sealed_scenario_event_definition_is_missing", event_id, "", "scenario-death-fx", sequence)
	var leaves: Array[Dictionary] = []
	for reference_value in definition.get("sounds", []) as Array:
		if typeof(reference_value) != TYPE_DICTIONARY:
			return _rejection("sealed_scenario_event_reference_is_invalid", event_id, "", "scenario-death-fx", sequence)
		var reference := reference_value as Dictionary
		var sample_id := String(reference.get("id", ""))
		var weight := _reference_weight(reference)
		var bound := output_by_sample.get(sample_id.to_lower(), {}) as Dictionary
		if bound.is_empty() or weight <= 0:
			return _rejection("sealed_scenario_event_reference_is_unbound", event_id, "", "scenario-death-fx", sequence)
		leaves.append({
			"sample_id": String(bound.get("id", "")),
			"path": String(bound.get("path", "")),
			"stream": null,
			"validated_path": true,
			"weight": weight,
		})
	if leaves.is_empty():
		return _rejection("sealed_scenario_event_has_no_leaves", event_id, "", "scenario-death-fx", sequence)
	var result := _route_definition({
		"event_id": event_id,
		"source": "sealed-neutral-prop-death-fx",
		"definition": definition.duplicate(true),
		"leaves": leaves,
	}, sequence, "", "scenario-death-fx")
	result["origin"] = origin
	result["sealedLeafCount"] = leaves.size()
	if bool(result.get("ok", false)):
		_play_sfx(result)
	_append_bounded_observability(intent_log, {
		"kind": "scenario.prop.death.audio", "sequence": sequence,
		"event_id": event_id, "origin": origin,
		"accepted": bool(result.get("ok", false)),
		"reason": String(result.get("reason", "")),
	})
	return _observable_route_result(result) if bool(result.get("ok", false)) else result.duplicate(true)


func _route_definition(route: Dictionary, sequence: int, object_id: String, kind: String) -> Dictionary:
	var event_id := String(route.get("event_id", ""))
	var leaves_value: Variant = route.get("leaves", [])
	if bool(route.get("authored_silent", false)) and typeof(leaves_value) == TYPE_ARRAY and (leaves_value as Array).is_empty():
		return _rejection("authored_silent", event_id, object_id, kind, sequence)
	if event_id == "" or typeof(leaves_value) != TYPE_ARRAY or (leaves_value as Array).is_empty():
		return _rejection("invalid_event", event_id, object_id, kind, sequence)
	var leaves := leaves_value as Array
	var total_weight := 0
	for leaf_value in leaves:
		if typeof(leaf_value) != TYPE_DICTIONARY:
			return _rejection("corrupt_event", event_id, object_id, kind, sequence)
		var leaf := leaf_value as Dictionary
		var stream: Variant = leaf.get("stream")
		var path := String(leaf.get("path", ""))
		var weight := int(leaf.get("weight", 0))
		var lazy_validated := bool(leaf.get("validated_path", false))
		if (not (stream is AudioStream) and not lazy_validated) or path == "" or weight <= 0 or not _is_resolved_audio_path(path):
			return _rejection("corrupt_event", event_id, object_id, kind, sequence)
		total_weight += weight
	if total_weight <= 0:
		return _rejection("corrupt_event", event_id, object_id, kind, sequence)
	var slot := posmod(sequence - 1, total_weight)
	var selected_index := 0
	var selected: Dictionary = leaves[0]
	for index in leaves.size():
		var leaf := leaves[index] as Dictionary
		var weight := int(leaf.get("weight", 1))
		if slot < weight:
			selected = leaf
			selected_index = index
			break
		slot -= weight
	var selected_stream := _stream_for_leaf(selected)
	if selected_stream == null:
		return _rejection("corrupt_event", event_id, object_id, kind, sequence)
	var semantics := _sfx_semantics(route, sequence)
	var result := {
		"ok": true,
		"event_id": event_id,
		"object_id": object_id,
		"kind": kind,
		"sequence": sequence,
		"variation_index": selected_index,
		"sample_id": String(selected.get("sample_id", "")),
		"path": String(selected.get("path", "")),
		"stream": selected_stream,
		"source": String(route.get("source", "")),
		# Retail AudioEvent mix parameters carried by the pack; see `_play_sfx`
		# for exactly which are honored and which stay named gaps.
		"volume_db": float(semantics["volume_db"]),
		"pitch_scale": float(semantics["pitch_scale"]),
		"limit": int(semantics["limit"]),
		"type_tokens": Array(semantics.get("type_tokens", [])),
		"min_range": float(semantics.get("min_range", -1.0)),
		"max_range": float(semantics.get("max_range", -1.0)),
		"has_definition": bool(semantics.get("has_definition", false)),
	}
	last_route_result = result.duplicate()
	_append_bounded_observability(routing_log, _observable_route_result(result))
	return result


func _stream_for_leaf(leaf: Dictionary) -> AudioStream:
	var existing: Variant = leaf.get("stream")
	if existing is AudioStream:
		return existing as AudioStream
	if not bool(leaf.get("validated_path", false)):
		return null
	var stream := _load_stream(String(leaf.get("path", "")))
	if stream != null:
		leaf["stream"] = stream
	return stream


func _rejection(reason: String, event_id: String, object_id: String, kind: String, sequence: int) -> Dictionary:
	var result := {
		"ok": false,
		"reason": reason,
		"event_id": event_id,
		"object_id": object_id,
		"kind": kind,
		"sequence": sequence,
	}
	last_route_result = result.duplicate()
	_append_bounded_observability(routing_log, result.duplicate())
	return result


func _observable_route_result(result: Dictionary) -> Dictionary:
	var copy := result.duplicate()
	copy.erase("stream")
	return copy


func _play_routed(result: Dictionary, player: AudioStreamPlayer) -> void:
	if not bool(result.get("ok", false)) or not playback_enabled or player == null:
		return
	player.stream = result.get("stream") as AudioStream
	player.play()


func _play_sfx(result: Dictionary, world_position: Variant = null) -> void:
	## THE SFX ENTRY POINT. Everything routed to the effects lane goes through
	## here so it lands on a FREE pool player instead of stamping over whatever
	## was already sounding, and so the retail AudioEvent parameters the pack
	## actually carries are applied.
	##
	## Honored (the pack's `data/audio_events.json` carries these fields
	## verbatim from retail's AudioEvent blocks, e.g. `ImpactHorse` ships
	## `Limit = 3`, `Volume = 90`, `PitchShift = -10 10`):
	##   Limit      - concurrent instances of the same event id, hard capped.
	##   Volume     - amplitude percent, applied as a dB offset on top of the
	##                user slider.
	##   PitchShift - authored low/high percent range, resolved DETERMINISTICALLY
	##                from the event sequence (never randf) so replays match.
	##   Type = world / shrouded - honored when the consuming event resolved a
	##                sim-plane position (`world_position`): the retail shroud
	##                cull and inverse-distance listener falloff from
	##                `_spatial_sfx_semantics` apply. Without a position the
	##                effect plays flat and the gap is recorded, as before.
	## NOT honored, and reported rather than invented (`sfx_semantics_gaps`):
	##   VolumeShift  - retail's units for this field are ambiguous between a
	##                  dB trim and a percent trim; guessing would be a made-up
	##                  mix decision.
	##   Priority     - there is no ducking/eviction model in this lane yet.
	if not bool(result.get("ok", false)) or not playback_enabled:
		return
	if sfx_players.is_empty():
		return
	var event_id := String(result.get("event_id", ""))
	var spatial := _spatial_sfx_semantics(result, world_position)
	if bool(spatial.get("culled", false)):
		return
	var limit := int(result.get("limit", 0))
	if limit > 0:
		var sounding := 0
		for pooled in sfx_players:
			if pooled != null and is_instance_valid(pooled) and pooled.playing and String(pooled.get_meta("retail_event_id", "")) == event_id:
				sounding += 1
		if sounding >= limit:
			sfx_limit_drops[event_id] = int(sfx_limit_drops.get(event_id, 0)) + 1
			return
	var player := _next_free_sfx_player()
	if player == null:
		return
	player.set_meta("retail_event_id", event_id)
	player.stream = result.get("stream") as AudioStream
	player.volume_db = UserSettingsScript.volume_to_db(voice_sfx_volume, muted) + float(result.get("volume_db", 0.0)) + float(spatial.get("attenuation_db", 0.0))
	player.pitch_scale = float(result.get("pitch_scale", 1.0))
	player.play()


func _spatial_sfx_semantics(result: Dictionary, world_position: Variant) -> Dictionary:
	## Retail world-sound spatial semantics, PRESENTATION-SIDE ONLY.
	##
	## SHROUD CULL — GameSounds.cpp:232-269 (`SoundManager::canPlayNow`): a
	## sound whose type carries `shrouded` is refused outright when the local
	## player's shroud at its position is not CELLSHROUD_CLEAR. Combat SFX
	## author `Type = world shrouded everyone` (soundeffects.ini:16682
	## ImpactHorse, :9411 ImpactSword01), which is why retail battles under the
	## fog of war are SILENT — the owner-reported "combat through the fog" bug.
	##
	## DISTANCE — MilesAudioManager::getEffectiveVolume (ZH reference the
	## engine forked from, MilesAudioManager.cpp:2681-2740): full volume inside
	## MinRange, gain = MinRange / distance beyond it (inverse distance), zero
	## at distance >= MaxRange (also refused up front, canPlayNow item 1).
	## Authored ranges are source units; the sim plane is source *
	## `spatial_transform_scale`. The listener is the camera focus point.
	var event_id := String(result.get("event_id", ""))
	var type_tokens: Array = result.get("type_tokens", [])
	var has_definition := bool(result.get("has_definition", false))
	var world_typed := type_tokens.has("world")
	if has_definition and not world_typed:
		# Authored non-world effect (ui/global): flat by authoring.
		return {"attenuation_db": 0.0}
	var have_context: bool = typeof(world_position) == TYPE_VECTOR2 \
		and spatial_visibility_probe.is_valid() and spatial_listener_probe.is_valid() \
		and spatial_transform_scale > 0.0
	if not have_context:
		# A world-typed effect playing FLAT because no position reached this
		# call: the pre-existing named gap, kept as the honest record.
		if world_typed:
			sfx_semantics_gaps["unsupported-type:world:%s" % event_id] = true
		return {"attenuation_db": 0.0}
	if not has_definition:
		# Runtime asset bindings (the per-unit weapon/bodyfall routes resolved
		# straight to WAV paths) carry no parameter block in the pack, but
		# retail authors those events `Type = world shrouded everyone`
		# (soundeffects.ini:9411 ImpactSword01). Spatialize them, and name the
		# assumption per event instead of hiding it.
		sfx_semantics_gaps["assumed-world-shrouded-unparameterized:%s" % event_id] = true
	var position := world_position as Vector2
	var shroud_gated: bool = (not has_definition) or type_tokens.has("shrouded")
	if shroud_gated and not bool(spatial_visibility_probe.call(position)):
		shrouded_sfx_drops[event_id] = int(shrouded_sfx_drops.get(event_id, 0)) + 1
		last_spatial_sfx_receipt = {"event_id": event_id, "culled": "shroud", "position": position}
		return {"culled": true}
	var listener_value: Variant = spatial_listener_probe.call()
	if typeof(listener_value) != TYPE_VECTOR2:
		if world_typed:
			sfx_semantics_gaps["unsupported-type:world:%s" % event_id] = true
		return {"attenuation_db": 0.0}
	var min_range := float(result.get("min_range", -1.0))
	var max_range := float(result.get("max_range", -1.0))
	var ranges_authored: bool = min_range > 0.0 and max_range >= min_range
	if not ranges_authored:
		# Hand-tuned defaults for the unauthored side(s); the event is named
		# once in `unranged_world_sfx` (see the WORLD_SFX_DEFAULT_* comment).
		if min_range <= 0.0:
			min_range = WORLD_SFX_DEFAULT_MIN_RANGE
		if max_range < min_range:
			max_range = maxf(min_range, WORLD_SFX_DEFAULT_MAX_RANGE)
		unranged_world_sfx[event_id] = true
	var min_local := min_range * spatial_transform_scale
	var max_local := max_range * spatial_transform_scale
	var distance := position.distance_to(listener_value as Vector2)
	if distance >= max_local:
		distance_sfx_drops[event_id] = int(distance_sfx_drops.get(event_id, 0)) + 1
		last_spatial_sfx_receipt = {"event_id": event_id, "culled": "distance", "position": position, "distance": distance, "max_range_local": max_local}
		return {"culled": true}
	var gain: float = 1.0 if distance <= min_local else min_local / distance
	var attenuation_db := linear_to_db(gain)
	last_spatial_sfx_receipt = {
		"event_id": event_id,
		"culled": "",
		"position": position,
		"listener": listener_value,
		"distance": distance,
		"min_range_local": min_local,
		"max_range_local": max_local,
		"gain": gain,
		"attenuation_db": attenuation_db,
		"ranges_authored": ranges_authored,
	}
	return {"attenuation_db": attenuation_db}


func _next_free_sfx_player() -> AudioStreamPlayer:
	## Prefer an IDLE player so nothing audible is truncated; only when every
	## pool slot is busy does the oldest cursor slot get reused (retail's mixer
	## has finite voices too, and the cap above usually prevents reaching here).
	for offset in sfx_players.size():
		var index := (_sfx_cursor + offset) % sfx_players.size()
		var candidate := sfx_players[index]
		if candidate != null and is_instance_valid(candidate) and not candidate.playing:
			_sfx_cursor = (index + 1) % sfx_players.size()
			return candidate
	var fallback := sfx_players[_sfx_cursor % sfx_players.size()]
	_sfx_cursor = (_sfx_cursor + 1) % sfx_players.size()
	return fallback if fallback != null and is_instance_valid(fallback) else null


func _sfx_semantics(route: Dictionary, sequence: int) -> Dictionary:
	## Retail AudioEvent parameters for a ContentDB-backed route. Routes with no
	## `definition` (playable-unit-runtime voice/weapon bindings resolved
	## straight to asset paths) carry no parameters at all — those get neutral
	## values and are NOT guessed at.
	var definition: Variant = route.get("definition", null)
	if typeof(definition) != TYPE_DICTIONARY:
		return {"volume_db": 0.0, "pitch_scale": 1.0, "limit": 0, "type_tokens": [], "min_range": -1.0, "max_range": -1.0, "has_definition": false}
	var parameters := _ambient_parameters(definition as Dictionary)
	if parameters.is_empty():
		return {"volume_db": 0.0, "pitch_scale": 1.0, "limit": 0, "type_tokens": [], "min_range": -1.0, "max_range": -1.0, "has_definition": false}
	var event_id := String(route.get("event_id", ""))
	var volume_db := 0.0
	if parameters.has("volume"):
		var volume_percent := String(parameters["volume"])
		if volume_percent.is_valid_float() and volume_percent.to_float() > 0.0:
			volume_db = linear_to_db(clampf(volume_percent.to_float() / 100.0, 0.0001, 4.0))
	var pitch_scale := 1.0
	if parameters.has("pitchshift"):
		var bounds := String(parameters["pitchshift"]).split(" ", false)
		if bounds.size() == 2 and bounds[0].is_valid_float() and bounds[1].is_valid_float():
			var low := bounds[0].to_float()
			var high := bounds[1].to_float()
			# Deterministic position inside the authored range: a cheap integer
			# hash of the event sequence, so a replay of the same event stream
			# produces the same pitch every time.
			var spread := float(posmod(sequence * 2654435761, 1009)) / 1008.0
			pitch_scale = clampf(1.0 + (low + spread * (high - low)) / 100.0, 0.25, 4.0)
	var limit := 0
	if parameters.has("limit"):
		var limit_text := String(parameters["limit"])
		if limit_text.is_valid_int():
			limit = maxi(0, limit_text.to_int())
	for unsupported in ["volumeshift", "priority"]:
		if parameters.has(unsupported):
			sfx_semantics_gaps["unsupported-%s:%s" % [unsupported, event_id]] = true
	# `Type = world` is no longer recorded as a gap HERE: `_play_sfx` honors it
	# when the consuming event resolved a position, and records the gap only for
	# a world effect that had to play flat (see `_spatial_sfx_semantics`).
	return {
		"volume_db": volume_db,
		"pitch_scale": pitch_scale,
		"limit": limit,
		"type_tokens": Array(String(parameters.get("type", "")).to_lower().split(" ", false)),
		"min_range": _strict_positive_float(String(parameters.get("minrange", ""))),
		"max_range": _strict_positive_float(String(parameters.get("maxrange", ""))),
		"has_definition": true,
	}


func _set_music(state: String) -> void:
	# Sim entry point. A repeated state event is a no-op so an unrelated event
	# burst never restarts the current playlist; a genuine change crossfades to
	# the first leaf of the target state's playlist.
	if state == current_music_state and music_playlists.has(state) and _music_active_index >= 0:
		current_music_state = state
		return
	current_music_state = state
	if not music_playlists.has(state):
		# No playlist for this state (never happens for the shipped four states);
		# preserve deterministic bookkeeping without inventing silence sources.
		_music_last_index = _music_active_index
		_music_active_index = -1
		current_music_track_index = -1
		return
	_transition_music(state, _music_entry_index(state), "state-change")


func _music_entry_index(state: String) -> int:
	## Retail's faction playlists are authored `Control = PLAY_ONE`, i.e. "start
	## one of these", so entering a state picks a leaf rather than always
	## replaying the first. Only authored playlists get this: the
	## filename-convention fallback keeps its deterministic index-0 entry, which
	## is what every existing runner asserts.
	var binding: Variant = music_faction_slots.get(state, null)
	if typeof(binding) != TYPE_DICTIONARY or not bool((binding as Dictionary).get("shuffle", false)):
		return 0
	var playlist: Array = music_playlists.get(state, [])
	if playlist.size() <= 1:
		return 0
	return _music_rng.randi_range(0, playlist.size() - 1)


func _transition_music(state: String, target_index: int, reason: String) -> void:
	var playlist: Array = music_playlists.get(state, [])
	if playlist.is_empty():
		return
	var index := clampi(target_index, 0, playlist.size() - 1)
	current_music_state = state
	_music_last_index = _music_active_index
	_music_active_index = index
	current_music_track_index = index
	var target_stream := playlist[index] as AudioStream
	# Swap the crossfade pair so music_player always references the incoming leaf.
	var fade_out := music_player
	var fade_in := _music_player_alt
	music_player = fade_in
	_music_player_alt = fade_out
	music_player.stream = target_stream
	_append_bounded_observability(music_transition_log, {
		"state": state,
		"from_index": _music_last_index,
		"to_index": index,
		"reason": reason,
		"crossfade": playback_enabled,
	})
	if not playback_enabled:
		# Headless bookkeeping only: keep the observable level coherent without
		# emitting audio so runners can assert playlist state deterministically.
		music_player.volume_db = _music_target_db()
		_music_player_alt.volume_db = UserSettingsScript.SILENT_DB
		return
	_start_music_crossfade(fade_out)


func _start_music_crossfade(fade_out: AudioStreamPlayer) -> void:
	if _music_fade_tween != null and _music_fade_tween.is_valid():
		_music_fade_tween.kill()
	var target_db := _music_target_db()
	music_player.volume_db = UserSettingsScript.SILENT_DB
	music_player.play()
	var fade_out_active := fade_out != null and is_instance_valid(fade_out) and fade_out.playing
	_music_fade_tween = create_tween()
	_music_fade_tween.set_parallel(true)
	_music_fade_tween.tween_property(music_player, "volume_db", target_db, MUSIC_CROSSFADE_SECONDS)
	if fade_out_active:
		_music_fade_tween.tween_property(fade_out, "volume_db", UserSettingsScript.SILENT_DB, MUSIC_CROSSFADE_SECONDS)
	_music_fade_tween.chain().tween_callback(_stop_faded_out_player.bind(fade_out))


func _stop_faded_out_player(player: AudioStreamPlayer) -> void:
	if player != null and is_instance_valid(player) and player != music_player:
		player.stop()
		player.volume_db = UserSettingsScript.SILENT_DB


func _finalize_music_fade() -> void:
	if _music_fade_tween != null and _music_fade_tween.is_valid():
		_music_fade_tween.kill()
	_music_fade_tween = null
	if _music_player_alt != null and is_instance_valid(_music_player_alt):
		_music_player_alt.stop()
		_music_player_alt.volume_db = UserSettingsScript.SILENT_DB


func _on_music_finished(finished_player: AudioStreamPlayer) -> void:
	# Only the active leaf reaching its natural end advances the playlist; the
	# fading-out side is stopped, not naturally finished, so it never advances.
	if finished_player != music_player:
		return
	if current_music_state == "" or not music_playlists.has(current_music_state):
		return
	var next_index := _choose_next_music_index(current_music_state, _music_active_index)
	_transition_music(current_music_state, next_index, "advance")


func _choose_next_music_index(state: String, current_index: int) -> int:
	# Single-track states loop that track (never silence). Multi-track states pick
	# a random index that is never the one just played (shuffle, no immediate
	# repeat).
	var playlist: Array = music_playlists.get(state, [])
	var count := playlist.size()
	if count <= 0:
		return -1
	if count == 1:
		return 0
	var next := current_index
	while next == current_index:
		next = _music_rng.randi_range(0, count - 1)
	return next


func _music_target_db() -> float:
	return UserSettingsScript.volume_to_db(music_volume, muted)


func _reset_music_playback() -> void:
	_finalize_music_fade()
	if music_player != null and is_instance_valid(music_player):
		music_player.stop()
		music_player.stream = null
	if _music_player_alt != null and is_instance_valid(_music_player_alt):
		_music_player_alt.stop()
		_music_player_alt.stream = null
	_music_active_index = -1
	_music_last_index = -1
	current_music_track_index = -1
	_music_rng.randomize()


func _load_stream(path: String) -> AudioStream:
	# The selected retail manifest intentionally reuses many physical samples
	# across logical SAGE events.  Decode each immutable pack leaf once during a
	# configure pass instead of rereading and reparsing the same WAV repeatedly.
	# Routes still carry the concrete AudioStream, so corrupt injected routes
	# continue to fail closed in _route_definition.
	var cache_key := path.replace("\\", "/").to_lower()
	var cached: Variant = _stream_cache.get(cache_key)
	if cached is AudioStream:
		return cached as AudioStream
	if not _is_resolved_audio_path(path) or not _has_supported_audio_header(path):
		return null
	var stream: AudioStream = null
	match path.get_extension().to_lower():
		"wav":
			stream = AudioStreamWAV.load_from_file(path)
		"ogg":
			stream = AudioStreamOggVorbis.load_from_file(path)
		"mp3":
			stream = AudioStreamMP3.load_from_file(path)
	if stream != null:
		_stream_cache[cache_key] = stream
	return stream


func _is_resolved_audio_path(path: String) -> bool:
	var content_db := get_node_or_null("/root/ContentDB")
	return content_db != null and content_db.has_method("is_resolved_asset_path") and bool(content_db.call("is_resolved_asset_path", path))


func _has_supported_audio_header(path: String) -> bool:
	var extension := path.get_extension().to_lower()
	if extension != "wav" and extension != "ogg" and extension != "mp3":
		return false
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null:
		return false
	var header := file.get_buffer(12)
	if extension == "wav":
		return header.size() >= 12 and _ascii_at(header, 0, 4) == "RIFF" and _ascii_at(header, 8, 4) == "WAVE"
	if extension == "ogg":
		return header.size() >= 4 and _ascii_at(header, 0, 4) == "OggS"
	return header.size() >= 3 and (_ascii_at(header, 0, 3) == "ID3" or (int(header[0]) == 0xff and (int(header[1]) & 0xe0) == 0xe0))


func _ascii_at(bytes: PackedByteArray, offset: int, length: int) -> String:
	if offset < 0 or length < 0 or offset + length > bytes.size():
		return ""
	return bytes.slice(offset, offset + length).get_string_from_ascii()


func count_voice_kind(kind: String) -> int:
	return (voice_streams.get(kind, []) as Array).size()


func count_roster_voice_kind(object_id: String, kind: String) -> int:
	var by_kind: Dictionary = roster_voice_routes.get(object_id, {})
	var route: Dictionary = by_kind.get(kind, {})
	return Array(route.get("leaves", [])).size()


func _append_bounded_observability(log: Array[Dictionary], row: Dictionary) -> void:
	if not observability_enabled:
		return
	log.append(row)
	if log.size() <= MAX_OBSERVABILITY_LOG_ENTRIES:
		return
	var retained := log.slice(OBSERVABILITY_LOG_TRIM_COUNT)
	log.clear()
	log.append_array(retained)


func stop_all() -> void:
	_finalize_music_fade()
	var live: Array = [music_player, _music_player_alt, voice_player]
	live.append_array(sfx_players)
	for player in live:
		if player != null and is_instance_valid(player):
			player.stop()
			player.stream = null
	music_streams.clear()
	music_playlists.clear()
	music_playlist_paths.clear()
	music_transition_log.clear()
	_music_active_index = -1
	_music_last_index = -1
	current_music_track_index = -1
	voice_streams = {"select": [], "attack": []}
	audio_event_routes.clear()
	roster_voice_routes.clear()
	roster_voice_form_routes.clear()
	playable_unit_weapon_sfx.clear()
	playable_unit_bodyfall.clear()
	playable_unit_animation_sounds.clear()
	playable_unit_sound_states.clear()
	playable_unit_base_skeletons.clear()
	playable_unit_sound_state_active.clear()
	footstep_gaps.clear()
	_structure_damage_stage.clear()
	_stream_cache.clear()
	_clear_ambient_players()
	ambient_emitters.clear()
	ambient_contract_declared = false
	ambient_parity_ready = false
	declared_structure_lifecycle_audio_active = false


func dispose() -> void:
	stop_all()
	var owned: Array = [music_player, _music_player_alt, voice_player]
	owned.append_array(sfx_players)
	for player in owned:
		if player != null and is_instance_valid(player):
			player.free()
	music_player = null
	_music_player_alt = null
	voice_player = null
	sfx_players.clear()
	sfx_player = null
	_sfx_cursor = 0


func _exit_tree() -> void:
	stop_all()
