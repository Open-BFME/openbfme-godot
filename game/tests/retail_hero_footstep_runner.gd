extends SceneTree
## FAST GATE for hero footsteps — the owner playtest report "weird footsteps
## for heroes like Eomer" (queue Q42).
##
## Retail authority, all from
## `data/ini/object/goodfaction/units/men/eomer.ini`:
##
##   :744  ClientBehavior = AnimationSoundClientBehavior ModuleTag_AnimAudioBehavior
##   :746    AnimationSound = Sound:FootstepDirtA  Animation:RUEomer_SKL.RUEomer_RUNA  Frames:4 15
##   :747    AnimationSound = Sound:FootstepDirtA  Animation:RUEomer_SKL.RUEomer_RUNB  Frames:5 15 26 36
##   :759    AnimationSound = Sound:HorseMoveFootsteps Animation:RUHHs_Theo_SKL.RUHHs_Theo_ACCL Frames:15 32
##   :726    ClientBehavior = ModelConditionSoundSelectorClientBehavior
##   :727      SoundState = MOUNTED
##   :729        VoiceMove = EomerVoiceMoveMounted
##   :712  SoundImpact = ImpactHorse
##   :25   DefaultModelConditionState / Model = RUEomer_SKN
##
## and `data/ini/soundeffects.ini:19127 AudioEvent FootstepDirtA`, which
## authors `Volume = 30`, `Limit = 3`, `PitchShift = -10 10` over 36 dirt
## leaves. Playing those leaves at 0 dB with unlimited concurrency — which is
## what a `playable-unit-runtime` route with no attached definition did — is
## the defect the owner heard.
##
## Every check names its authority. A unit that authors no footstep row
## (`menporter.json`, every horde in the shipped men pack) plays NOTHING and is
## listed as a named gap; a generic step sample is never substituted.

const AudioScript = preload("res://src/retail_slice/retail_slice_audio.gd")
const PackCapabilityScript = preload("res://src/content/pack_capability.gd")
const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")

const EOMER_ID := "bfme2.object.rohan-eomer"
const PORTER_ID := "bfme2.object.men-porter"
const RUNA := "rueomer_runa"
const RUNB := "rueomer_runb"
const MOUNTED_ACCL := "ruhhs_theo_accl"

var passed := 0
var failed := 0
var _runner_watchdog := RunnerWatchdogScript.new()


func _initialize() -> void:
	_runner_watchdog.start(self, "RETAIL_HERO_FOOTSTEP_RUNNER")
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

	# --- THE DEFECT, stated in pre-change API only --------------------------
	# The route the hero's footstep resolves through must carry the authored
	# `AudioEvent FootstepDirtA` block (soundeffects.ini:19127). Before this
	# lane it carried none, so the 36 dirt leaves played at 0 dB with no
	# `Limit` — the owner's "weird footsteps".
	var footstep_route: Dictionary = audio.audio_event_routes.get("footstepdirta", {})
	_check("footstep_route_exists", not footstep_route.is_empty())
	_check(
		"footstep_route_carries_authored_audioevent",
		typeof(footstep_route.get("definition", null)) == TYPE_DICTIONARY,
		str(footstep_route.get("source", ""))
	)
	var raw_step: Dictionary = audio.route_audio_event("FootstepDirtA", 1)
	_check(
		"footstep_is_not_played_at_full_volume",
		bool(raw_step.get("ok", false)) and float(raw_step.get("volume_db", 0.0)) < -5.0,
		str(raw_step.get("volume_db", 0.0))
	)
	_check(
		"footstep_concurrency_is_capped",
		int(raw_step.get("limit", 0)) == 3,
		str(raw_step.get("limit", 0))
	)

	# --- the documents this runner leans on are really loaded ---------------
	var rows: Array = audio.playable_unit_animation_sounds.get(EOMER_ID, [])
	_check("eomer_animation_sound_rows_bound", rows.size() >= 12, str(rows.size()))
	_check(
		"eomer_base_skeleton_is_authored_default_model",
		String(audio.playable_unit_base_skeletons.get(EOMER_ID, "")) == "RUEOMER",
		String(audio.playable_unit_base_skeletons.get(EOMER_ID, ""))
	)

	# --- (1) RUNA fires FootstepDirtA at frames 4 and 15, and nowhere else ---
	var runa := _footstep_frames(audio, EOMER_ID, RUNA, 60)
	_check("runa_footstep_frames_are_4_and_15", runa == [4, 15], str(runa))
	# --- (2) RUNB fires at 5 15 26 36 ---------------------------------------
	var runb := _footstep_frames(audio, EOMER_ID, RUNB, 60)
	_check("runb_footstep_frames_are_5_15_26_36", runb == [5, 15, 26, 36], str(runb))

	# --- (3) the authored AudioEvent mix reaches the routed footstep --------
	var step: Dictionary = audio.route_footstep(EOMER_ID, 1, RUNA, 4)
	_check("runa_frame_4_routes_footstepdirta", bool(step.get("ok", false)) and String(step.get("event_id", "")) == "FootstepDirtA", str(step.get("event_id", "")) + "/" + String(step.get("reason", "")))
	# soundeffects.ini:19134 `Volume = 30` -> linear_to_db(0.30)
	var expected_db := linear_to_db(0.30)
	_check(
		"footstep_honours_authored_volume_30",
		absf(float(step.get("volume_db", 0.0)) - expected_db) < 0.01,
		"%f vs %f" % [float(step.get("volume_db", 0.0)), expected_db]
	)
	# soundeffects.ini:19130 `Limit = 3`
	_check("footstep_honours_authored_limit_3", int(step.get("limit", 0)) == 3, str(step.get("limit", 0)))
	# soundeffects.ini:19131 `PitchShift = -10 10` -> never a flat 1.0 span
	_check(
		"footstep_honours_authored_pitchshift_range",
		float(step.get("pitch_scale", 1.0)) >= 0.9 and float(step.get("pitch_scale", 1.0)) <= 1.1,
		str(step.get("pitch_scale", 1.0))
	)
	# A frame the row does not author makes no sound.
	var off_frame: Dictionary = audio.route_footstep(EOMER_ID, 2, RUNA, 7)
	_check("unauthored_frame_makes_no_footstep", not bool(off_frame.get("ok", false)), String(off_frame.get("reason", "")))

	# --- (4) MOUNTED: no foot sample, authored mounted voice, ImpactHorse ---
	audio.set_unit_sound_state(EOMER_ID, ["MOUNTED"])
	var mounted_step: Dictionary = audio.route_footstep(EOMER_ID, 3, RUNA, 4)
	_check(
		"mounted_hero_plays_no_foot_sample",
		not bool(mounted_step.get("ok", false)) and String(mounted_step.get("reason", "")) == "mounted_footstep_suppressed",
		String(mounted_step.get("reason", ""))
	)
	_check(
		"mounted_foot_suppression_is_a_named_gap",
		audio.footstep_gaps.has("mounted-footstep-clip-missing:%s:%s" % [EOMER_ID, "RUEomer_SKL.RUEomer_RUNA"]),
		str(audio.footstep_gaps.keys())
	)
	# The mount's own skeleton rows still sound while MOUNTED (eomer.ini:759).
	var horse_step: Dictionary = audio.route_footstep(EOMER_ID, 4, MOUNTED_ACCL, 15)
	_check(
		"mounted_hero_still_sounds_mount_skeleton_rows",
		bool(horse_step.get("ok", false)) and String(horse_step.get("event_id", "")) == "HorseMoveFootsteps",
		String(horse_step.get("event_id", "")) + "/" + String(horse_step.get("reason", ""))
	)
	_check(
		"mounted_state_selects_authored_voicemove",
		audio.sound_state_event(EOMER_ID, "VoiceMove") == "EomerVoiceMoveMounted",
		audio.sound_state_event(EOMER_ID, "VoiceMove")
	)
	var mounted_move: Dictionary = audio.route_roster_voice(EOMER_ID, "move", 1)
	_check(
		"mounted_move_voice_routes_the_authored_event",
		bool(mounted_move.get("ok", false)) and String(mounted_move.get("event_id", "")) == "EomerVoiceMoveMounted",
		String(mounted_move.get("event_id", "")) + "/" + String(mounted_move.get("reason", ""))
	)
	# eomer.ini:712 SoundImpact = ImpactHorse — indexed, not invented.
	_check(
		"eomer_sound_impact_is_impacthorse",
		String(audio.playable_unit_impact.get(EOMER_ID, "")) == "ImpactHorse",
		String(audio.playable_unit_impact.get(EOMER_ID, ""))
	)
	audio.set_unit_sound_state(EOMER_ID, [])
	var dismounted_move: Dictionary = audio.route_roster_voice(EOMER_ID, "move", 1)
	_check(
		"dismounted_move_voice_is_not_the_mounted_event",
		String(dismounted_move.get("event_id", "")) != "EomerVoiceMoveMounted",
		String(dismounted_move.get("event_id", ""))
	)
	var dismounted_step: Dictionary = audio.route_footstep(EOMER_ID, 5, RUNA, 4)
	_check("dismounted_hero_footstep_returns", bool(dismounted_step.get("ok", false)), String(dismounted_step.get("reason", "")))

	# --- (5) a unit with no authored footstep row: silence + named gap ------
	_check("porter_document_is_loaded", audio.playable_unit_categories.has(PORTER_ID))
	var porter_step: Dictionary = audio.route_footstep(PORTER_ID, 1, "muporter_runa", 4)
	_check(
		"porter_makes_no_footstep_sound",
		not bool(porter_step.get("ok", false)) and String(porter_step.get("event_id", "")) == "",
		String(porter_step.get("event_id", "")) + "/" + String(porter_step.get("reason", ""))
	)
	_check(
		"porter_footstep_absence_is_a_named_gap",
		audio.footstep_gaps.has("no-authored-footstep-rows:%s" % PORTER_ID),
		str(audio.footstep_gaps.keys())
	)

	_finish()


func _footstep_frames(audio, object_id: String, clip: String, upto: int) -> Array:
	## Walk the clip frame by frame the way the live clip-frame clock does and
	## collect every FOOTSTEP frame the authored rows fire.
	var frames: Array = []
	for frame in range(1, upto + 1):
		var crossed: Array = audio.advance_animation_sounds(object_id, clip, float(frame - 1), float(frame), float(upto), false)
		for row_value in crossed:
			var row := row_value as Dictionary
			if String(row.get("eventId", "")).to_lower().contains("footstep"):
				frames.append(int(row.get("frame", -1)))
	frames.sort()
	return frames


func _check(label: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("PASS %s" % label)
	else:
		failed += 1
		print("FAIL %s %s" % [label, detail])


func _finish() -> void:
	print("RETAIL_HERO_FOOTSTEP_RUNNER passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)
