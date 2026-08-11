extends SceneTree
## FAST GATE for the combat SFX layer — the three defects behind the owner
## playtest report "attack sounds still sound awful; it's not the default
## attack sounds for any of the units".
##
## Each check below names the retail authority it enforces:
##
## (A) `combat.hit` must be SILENT and counted, not routed to the target's
##     `SoundImpact`. `data/ini/weapon.ini:5514 Weapon GondorSword` declares
##     `DamageFXType = SWORD_SLASH`; `data/ini/damagefx.ini` resolves that in
##     `NormalDamageFX` to `MajorFX = SWORD_SLASH  FX_NONE`, i.e. retail plays
##     NOTHING on a sword hit. `SoundImpact` is the crush/knockback thud
##     (`gondorfighter.ini:768 SoundImpact = ImpactHorse` on an INFANTRY object,
##     and its knockback death module at :913-919 is commented "sound already
##     played by SoundImpact"), and it resolves to the same shared `ImpactHorse`
##     macro for every unit that carries it.
##
## (C) The authored weapon FireFX fires PER MEMBER. Retail hangs the sound on
##     the WEAPON (`Weapon GondorSword / FireFX = FX_GondorSwordHit`) and every
##     member of the horde carries that weapon, so the consumer must be
##     `combat.member_swing` (retail_slice_sim.gd:15754), not the once-per-horde
##     `combat.swing` (retail_slice_sim.gd:15743).
##
## (B) Simultaneous effects must not truncate each other. One shared
##     AudioStreamPlayer with `stream = ...; play()` per event turns a horde
##     fight into clipped 50-100ms transients.
##
## (D) A unit with no bound weapon event FAILS VISIBLE (counted silence), never
##     an invented `SwordShingClean1ForHordes` / `ArrowDrawBow` substitute.

const AudioScript = preload("res://src/retail_slice/retail_slice_audio.gd")
const PackCapabilityScript = preload("res://src/content/pack_capability.gd")
const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")

const FIGHTER_ID := "bfme2.object.gondor-fighter"
const PORTER_ID := "bfme2.object.men-porter"

var passed := 0
var failed := 0
var _runner_watchdog := RunnerWatchdogScript.new()


func _initialize() -> void:
	_runner_watchdog.start(self, "RETAIL_COMBAT_SFX_LAYER_RUNNER")
	call_deferred("_run")


func _run() -> void:
	var content_db := root.get_node_or_null("ContentDB")
	var mod_loader := root.get_node_or_null("ModLoader")
	_check("autoloads_available", content_db != null and mod_loader != null)
	if content_db == null or mod_loader == null:
		_finish()
		return
	content_db.reload()
	var pack_root := String(PackCapabilityScript.resolve_host_slice_pack(content_db.pack_meta).get("root", ""))
	_check("host_slice_pack_resolved", pack_root != "", pack_root)
	if pack_root == "":
		_finish()
		return

	var audio = AudioScript.new()
	root.add_child(audio)
	audio.configure(pack_root, true)
	audio.observability_enabled = true
	_check("retail_playback_is_enabled", audio.playback_enabled)

	# The two documents this runner leans on must actually be loaded, otherwise
	# every assertion below would be vacuously satisfiable.
	var fighter_weapon: Dictionary = audio.playable_unit_weapon_sfx.get(FIGHTER_ID, {})
	_check(
		"fighter_binds_authored_weapon_firefx",
		String(fighter_weapon.get("weapon", "")) == "ImpactSword01",
		str(fighter_weapon)
	)
	_check(
		"porter_binds_no_weapon_firefx",
		audio.playable_unit_weapon_sfx.get(PORTER_ID, {}).is_empty()
			and audio.playable_unit_categories.has(PORTER_ID),
		str(audio.playable_unit_weapon_sfx.get(PORTER_ID, {}))
	)
	_check(
		"fighter_sound_impact_is_the_shared_horse_macro",
		String(audio.playable_unit_impact.get(FIGHTER_ID, "")) == "ImpactHorse",
		String(audio.playable_unit_impact.get(FIGHTER_ID, ""))
	)

	# ---- (B) the SFX lane is a pool, not one restarted player -------------
	# NOTE ON `audio.get(...)` BELOW: every new field/method this lane adds is
	# read through `get()` / `has_method()` so this runner can be pointed at the
	# PRE-FIX source and report a real failing count instead of unwinding on an
	# "invalid access to property" error. That is what makes the failing-first
	# RED number quotable.
	var pool: Array = _pool(audio)
	var pool_ok := pool.size() >= 4
	if pool_ok:
		for pooled in pool:
			if not (pooled is AudioStreamPlayer):
				pool_ok = false
	_check("sfx_lane_is_a_player_pool", pool_ok, "pool_size=%d" % pool.size())
	_check(
		"sfx_player_is_the_pool_head",
		not pool.is_empty() and audio.sfx_player == pool[0]
	)

	# ---- (C) one authored weapon sound PER MEMBER -------------------------
	audio._entity_object_ids[7] = FIGHTER_ID
	audio._next_event_index = 0
	var sword_before := _count(audio.routing_log, "ImpactSword01", true)
	audio.sync_events([
		{"sequence": 1, "kind": "combat.member_swing", "entity_id": 7, "target_id": 8, "member_index": 0},
		{"sequence": 2, "kind": "combat.member_swing", "entity_id": 7, "target_id": 8, "member_index": 1},
		{"sequence": 3, "kind": "combat.member_swing", "entity_id": 7, "target_id": 8, "member_index": 2},
		{"sequence": 4, "kind": "combat.member_swing", "entity_id": 7, "target_id": 8, "member_index": 3},
		{"sequence": 5, "kind": "combat.member_swing", "entity_id": 7, "target_id": 8, "member_index": 4},
	])
	var sword_per_member := _count(audio.routing_log, "ImpactSword01", true) - sword_before
	_check(
		"five_member_swings_play_five_authored_weapon_sounds",
		sword_per_member == 5,
		"routed=%d" % sword_per_member
	)

	# (B) again, but as the thing the owner actually HEARS: those five swings
	# fired inside one sync_events call with no frame in between, so with a
	# single player only one could still be sounding. A pool overlaps them.
	var simultaneously_sounding := 0
	for pooled in _pool(audio):
		if pooled.playing:
			simultaneously_sounding += 1
	_check(
		"simultaneous_swings_do_not_truncate_each_other",
		simultaneously_sounding >= 2,
		"sounding=%d" % simultaneously_sounding
	)

	# ---- (C) the battalion-cadence event is a silent marker ---------------
	audio._next_event_index = 0
	var sword_before_battalion := _count(audio.routing_log, "ImpactSword01", true)
	audio.sync_events([
		{"sequence": 6, "kind": "combat.swing", "entity_id": 7, "target_id": 8, "object_id": FIGHTER_ID},
	])
	_check(
		"battalion_swing_is_a_silent_cadence_marker",
		_count(audio.routing_log, "ImpactSword01", true) - sword_before_battalion == 0
	)

	# ---- (A) the per-hit layer is silent and counted ----------------------
	audio._next_event_index = 0
	var horse_before := _count(audio.routing_log, "ImpactHorse", true)
	audio.sync_events([
		{"sequence": 7, "kind": "combat.hit", "entity_id": 8, "target_id": 7, "target_object_id": FIGHTER_ID, "damage_type": "SLASH"},
		{"sequence": 8, "kind": "combat.hit", "entity_id": 8, "target_id": 7, "target_object_id": FIGHTER_ID, "damage_type": "SLASH"},
		{"sequence": 9, "kind": "combat.hit", "entity_id": 8, "target_id": 7, "target_object_id": FIGHTER_ID, "damage_type": "PIERCE"},
	])
	var horse_from_hits := _count(audio.routing_log, "ImpactHorse", true) - horse_before
	_check(
		"sword_hit_plays_nothing_per_retail_damagefx_fx_none",
		horse_from_hits == 0,
		"routed_horse_impacts=%d" % horse_from_hits
	)
	_check(
		"per_hit_damagefx_gap_is_counted_by_damage_type",
		int(_dict(audio, "damage_fx_gaps").get("SLASH", 0)) == 2 and int(_dict(audio, "damage_fx_gaps").get("PIERCE", 0)) == 1,
		str(_dict(audio, "damage_fx_gaps"))
	)
	# SoundImpact survives for its real retail owner, just not on `combat.hit`.
	var crush: Dictionary = audio.route_crush_impact(FIGHTER_ID, 1) if audio.has_method("route_crush_impact") else {"reason": "route_crush_impact_absent"}
	_check(
		"sound_impact_preserved_for_a_future_crush_event",
		bool(crush.get("ok", false)) and String(crush.get("event_id", "")) == "ImpactHorse",
		str(crush.get("reason", ""))
	)

	# ---- (D) no invented substitute for an unbound weapon ------------------
	audio._entity_object_ids[9] = PORTER_ID
	audio._next_event_index = 0
	var shing_before := _count(audio.routing_log, "SwordShingClean1ForHordes", true)
	var bow_before := _count(audio.routing_log, "ArrowDrawBow", true)
	audio.sync_events([
		# BOTH swing kinds, deliberately. The invented substitute used to be
		# reached from `combat.swing`; the per-member hop is where the real
		# weapon sound lives now. Neither may manufacture a sound for a unit
		# whose pack binds no weapon event.
		{"sequence": 10, "kind": "combat.swing", "entity_id": 9, "target_id": 8, "object_id": PORTER_ID},
		{"sequence": 11, "kind": "combat.member_swing", "entity_id": 9, "target_id": 8, "member_index": 0},
	])
	_check(
		"unbound_weapon_plays_no_invented_substitute",
		_count(audio.routing_log, "SwordShingClean1ForHordes", true) - shing_before == 0
			and _count(audio.routing_log, "ArrowDrawBow", true) - bow_before == 0
	)
	_check(
		"unbound_weapon_fails_visible_with_a_named_reason",
		String(audio.last_route_result.get("reason", "")) == "no_authored_weapon_sfx",
		String(audio.last_route_result.get("reason", ""))
	)
	_check(
		"unbound_weapon_gap_is_counted_per_object",
		int(audio.generic_weapon_swing_fallbacks.get(PORTER_ID, 0)) == 1,
		str(audio.generic_weapon_swing_fallbacks)
	)

	# ---- (B) retail AudioEvent mix parameters the pack really carries ------
	# `ImpactHorse` ships `Limit = 3`, `Volume = 90`, `PitchShift = -10 10` in
	# the pack's data/audio_events.json, verbatim from retail's AudioEvent block.
	var horse_route: Dictionary = audio.route_audio_event("ImpactHorse", 3)
	_check(
		"retail_limit_is_read_from_the_pack",
		int(horse_route.get("limit", 0)) == 3,
		str(horse_route.get("limit", "absent"))
	)
	_check(
		"retail_volume_is_applied_as_a_db_offset",
		absf(float(horse_route.get("volume_db", 999.0)) - linear_to_db(0.90)) < 0.01,
		str(horse_route.get("volume_db", "absent"))
	)
	var pitch := float(horse_route.get("pitch_scale", 1.0))
	_check(
		"retail_pitchshift_stays_inside_the_authored_range",
		pitch >= 0.90 and pitch <= 1.10 and not is_equal_approx(pitch, 1.0),
		str(pitch)
	)
	_check(
		"retail_pitchshift_is_deterministic_for_a_sequence",
		is_equal_approx(float(audio.route_audio_event("ImpactHorse", 3).get("pitch_scale", -1.0)), pitch)
	)
	# The voice cap is enforced, not merely reported.
	if audio.has_method("_play_sfx"):
		for burst in 6:
			audio.call("_play_sfx", audio.route_audio_event("ImpactHorse", burst + 1))
	_check(
		"limit_cap_drops_the_excess_concurrent_instances",
		int(_dict(audio, "sfx_limit_drops").get("ImpactHorse", 0)) == 3,
		str(_dict(audio, "sfx_limit_drops"))
	)
	var semantics_gaps := _dict(audio, "sfx_semantics_gaps")
	_check(
		"positional_and_volumeshift_semantics_stay_named_gaps",
		semantics_gaps.has("unsupported-type:world:ImpactHorse")
			and semantics_gaps.has("unsupported-volumeshift:ImpactHorse")
			and semantics_gaps.has("unsupported-priority:ImpactHorse"),
		str(semantics_gaps.keys())
	)

	audio.dispose()
	audio.free()
	_finish()


func _pool(audio) -> Array:
	## Tolerant read of the SFX pool: the pre-fix source has no `sfx_players`,
	## and it must degrade to the single legacy player rather than crash.
	var value: Variant = audio.get("sfx_players")
	if value is Array:
		return value as Array
	return [audio.sfx_player] if audio.sfx_player != null else []


func _dict(audio, field: String) -> Dictionary:
	## Tolerant read of a bookkeeping dictionary this lane introduces.
	var value: Variant = audio.get(field)
	return value as Dictionary if value is Dictionary else {}


func _count(log: Array[Dictionary], event_id: String, expected_ok: bool) -> int:
	var total := 0
	for row in log:
		if String(row.get("event_id", "")) == event_id and bool(row.get("ok", false)) == expected_ok:
			total += 1
	return total


func _check(label: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("PASS %s" % label)
	else:
		failed += 1
		print("FAIL %s%s" % [label, " :: %s" % detail if detail != "" else ""])


func _finish() -> void:
	print("RETAIL_COMBAT_SFX_LAYER_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)
