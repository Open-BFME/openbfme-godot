extends RefCounted

## WP06-audio-video - sound effects, music, speech, reverb, volume classes,
## movies, cinematic text and AVI capture.
##
## PRESENTATION-ONLY. Not one member of this package may touch the simulation.
## Every action goes to the AUDIO PRESENTATION SINK (`world.audio().emit()`),
## which is a one-way channel by design - see the PresentationSink comment in
## script_world.gd. Nothing here reads the world, nothing here writes ctx.env,
## and nothing here can influence lockstep.
##
##
## WHY THERE IS ONE HANDLER BODY AND NOT FIFTY-THREE
## ================================================
## Every implemented member of this package does exactly the same thing: decode
## its arguments in declaration order and hand them to the sink under its own
## InternalName. That is the sink's entire contract ("a sink has no semantics -
## only a destination"), so 53 hand-written bodies would be 53 copies of four
## lines, differing only in a name the registration already states.
##
## That is not a shortcut, it is the safer construction. This package contains
## the worst argument-ordering traps in the whole catalog:
##
##     AUDIO_FADE_VOLUME(REAL, REAL, REAL, REAL, REAL)
##     MUSIC_SET_TRACK(MUSIC, BOOLEAN, BOOLEAN)
##     AUDIO_POP_MUSIC(BOOLEAN, BOOLEAN)
##     MUSIC_PLAY_TRACK_FINITE_TIMES(MUSIC, INT, BOOLEAN, BOOLEAN)
##     MUSIC_PUSH_TRACK_FINITE_TIMES_AND_NOTIFY(MUSIC, INT, BOOLEAN, BOOLEAN, FLAG)
##
## Five interchangeable REALs, and eleven signatures carrying two ADJACENT
## BOOLEANs that mean fadeout and fadein. A type-search cannot tell those apart
## at all, and hand-copying fifty-three bodies is exactly how a fadeout/fadein
## swap gets in - it would compile, run, record something plausible, and be
## invisible in review. `_emit` instead walks `0..signature().size()` and takes
## the payload type for each slot from the VOCABULARY ENTRY itself, so the
## ordering cannot drift from the declared signature and cannot be typo'd
## per-action.
##
## This is distinct from the shared handler behind blocked names (see
## handlers/_registry.gd), which is shared so that refusals cannot be mistaken
## for implementations. Here the shared body IS the implementation.
##
##
## WHAT THE SINK RECEIVES, EXACTLY
## ===============================
## `values` is the signature's arguments, positionally, and nothing else. No
## re-ordering, no defaults, no context. Two consequences worth stating because
## both look like omissions:
##
##   * DURATIONS ARE NOT CONVERTED TO TICKS. The world's argument conventions
##     say durations reaching a FACET are interpreter ticks. The sink is not a
##     facet; its contract is the decoded argument list verbatim. So
##     AUDIO_FADE_VOLUME emits seconds as the map wrote them, and
##     DISPLAY_CINEMATIC_TEXT emits its INT seconds unconverted. Converting here
##     would put a number in the recording that appears in no map.
##   * NOTHING IS RESOLVED. PLAY_SOUND_EFFECT_AT emits the WAYPOINT NAME, not a
##     position; PLAY_SOUND_EFFECT_AT_TEAM emits the TEAM NAME, not a roster.
##     Resolving either would mean reading the simulation from a presentation
##     handler, which is the coupling this package must not create. The
##     presentation adapter owns that lookup.
##
## BOOLEAN is the one decoded type. SageScriptArgs.value() picks the storage
## FIELD implied by the parameter type, and BOOLEAN is stored in the integer
## field, so `value()` would hand the sink 1 and 0. A SAGE BOOLEAN is a bool
## (the reference spells it "wire-encoded false=0 / true=1"); emitting `true`
## rather than `1` is decoding the declared type, not embellishing it, and it
## makes a fadeout/fadein assertion readable instead of a row of digits.
##
##
## THE MUSIC "AND NOTIFY" FAMILY SETS NO FLAG - DELIBERATELY
## ========================================================
## The four MUSIC_*_AND_NOTIFY actions are documented as "play <MUSIC> <INT>
## times ..., then set <FLAG> to TRUE". This package does not set that flag, and
## that is a decision, not an oversight:
##
##   * Setting it NOW would be false - the track has not played.
##   * Setting it LATER would make LOCAL AUDIO PLAYBACK TIMING drive interpreter
##     state. Peers with different audio hardware, muted audio or a skipped
##     track would set the flag on different frames and desync a lockstep match.
##     That is precisely what the one-way sink exists to prevent.
##
## The FLAG name is part of the signature, so it reaches the sink in the
## recording and the gap is visible rather than silent. A map gating on that
## flag simply never fires, which is the safe direction for an unevaluable gate.
## Recorded here as a known limit; a fix needs a deterministic music-completion
## clock, which is a design decision for the presentation adapter's owner.
##
##
## GAP-REGISTERED MEMBERS (6 of 59)
## ================================
## See BLOCKED_CONDITIONS / BLOCKED_ACTIONS below. Five conditions and one
## action are registered as blocked rather than implemented, each with a stated
## subsystem, so a map that uses one produces a `blocked-subsystem` gap naming
## it - never a silent skip, and never counted as coverage.

const PACKAGE := "WP06-audio-video"

const Dispatch := preload("res://src/script/script_dispatch.gd")
const World := preload("res://src/script/script_world.gd")

## Which presentation channel this package's actions go to. Every member is a
## WP06 member and the sink header in script_world.gd assigns WP06 the audio
## channel, so movies, cinematic text and AVI capture ride it too. The channel
## is a DESTINATION, not a claim about the medium - the op name carries the
## meaning, and splitting one package across two channels would only make a
## recording harder to read back.
const CHANNEL := "audio"


# --- The implemented surface, by family -----------------------------------
#
# Signatures are written beside each name because the registration list is the
# only place a reader can see them; `_emit` reads them from the vocabulary.


## AUDIO_FADE_VOLUME(REAL from, REAL to, REAL rise_seconds, REAL hold_seconds,
##                   REAL fall_seconds)
## Five interchangeable REALs - the single worst type-search trap in the
## catalog. Meaning per the reference: fade all audio FROM volume TO volume,
## taking `rise` seconds to increase, holding `hold` seconds, then `fall`
## seconds to decrease.
const FADE_ACTIONS := [
	"AUDIO_FADE_VOLUME",
	"AUDIO_MAKE_ALL_SOUNDS_SUBJECT_TO_FADE",  # ()
	"AUDIO_MAKE_SOUND_IMMUNE_TO_FADE",        # (AUDIO)
	"AUDIO_MAKE_SOUND_SUBJECT_TO_FADE",       # (AUDIO)
]

## Per-sound-event volume overrides. AUDIO_OVERRIDE_VOLUME_TYPE puts the NAME
## before the VALUE; the counter package's note about value-before-name applies
## in reverse here, which is why neither is searched for.
const VOLUME_TYPE_ACTIONS := [
	"AUDIO_OVERRIDE_VOLUME_TYPE",    # (AUDIO, REAL percent_of_full)
	"AUDIO_RESTORE_VOLUME_TYPE",     # (AUDIO)
	"AUDIO_RESTORE_VOLUME_ALL_TYPE",  # ()
]

## The three global volume classes. Each takes a percent 0-100 as a REAL.
const VOLUME_CLASS_ACTIONS := [
	"MUSIC_SET_VOLUME",   # (REAL)
	"SOUND_SET_VOLUME",   # (REAL)
	"SPEECH_SET_VOLUME",  # (REAL)
]

const REVERB_ACTIONS := [
	"AUDIO_SET_REVERB_ROOM_TYPE",                   # (REVERB_ROOM_TYPE)
	"AUDIO_REMOVE_REVERB",                          # ()
	"AUDIO_SET_REVERB_SUPPRESSION_POLYGON",         # (TRIGGER_AREA, PERCENT)
	"AUDIO_REMOVE_REVERB_SUPPRESSION_POLYGON",      # (TRIGGER_AREA)
	"AUDIO_REMOVE_ALL_REVERB_SUPPRESSION_POLYGONS",  # ()
]

## Sound effects. The three positional variants emit the WAYPOINT / UNIT / TEAM
## NAME - see the class comment on why nothing is resolved here.
const SOUND_EFFECT_ACTIONS := [
	"PLAY_SOUND_EFFECT",          # (SOUND)
	"PLAY_SOUND_EFFECT_AT",       # (SOUND, WAYPOINT)
	"PLAY_SOUND_EFFECT_AT_TEAM",  # (SOUND, TEAM)
	"SOUND_PLAY_NAMED",           # (SOUND, UNIT)
	"ENABLE_OBJECT_SOUND",        # (UNIT)
	"DISABLE_OBJECT_SOUND",       # (UNIT)
]

## Sound-event class enable/disable. SOUND_REMOVE_ALL_DISABLED is marked
## OBSOLETE by the reference ("disable type now does all the work") but is still
## a real name a shipped map may carry, so it is served rather than dropped.
const SOUND_EVENT_ACTIONS := [
	"SOUND_ENABLE_TYPE",         # (AUDIO)
	"SOUND_DISABLE_TYPE",        # (AUDIO)
	"SOUND_ENABLE_ALL",          # ()
	"SOUND_REMOVE_TYPE",         # (AUDIO)
	"SOUND_REMOVE_ALL_DISABLED",  # ()
]

const AMBIENT_ACTIONS := [
	"SOUND_AMBIENT_PAUSE",       # ()
	"SOUND_AMBIENT_RESUME",      # ()
	"SUSPEND_BACKGROUND_SOUNDS",  # ()
	"RESUME_BACKGROUND_SOUNDS",  # ()
]

## SPEECH_PLAY(DIALOG, BOOLEAN allow_overlap).
const SPEECH_ACTIONS := [
	"SPEECH_PLAY",
]

## Music track and stack control. EVERY entry below ends in the adjacent pair
## (BOOLEAN fadeout, BOOLEAN fadein) - eleven chances to swap two arguments that
## no type check could ever catch.
const MUSIC_ACTIONS := [
	"MUSIC_SET_TRACK",                          # (MUSIC, BOOLEAN, BOOLEAN)
	"MUSIC_PLAY_TRACK_FINITE_TIMES",            # (MUSIC, INT, BOOLEAN, BOOLEAN)
	"MUSIC_PLAY_TRACK_FINITE_TIMES_AND_NOTIFY",  # (MUSIC, INT, BOOLEAN, BOOLEAN, FLAG)
	"AUDIO_PUSH_MUSIC",                         # (MUSIC, BOOLEAN, BOOLEAN)
	"AUDIO_POP_MUSIC",                          # (BOOLEAN, BOOLEAN)
	"MUSIC_PUSH_TRACK_FINITE_TIMES",            # (MUSIC, INT, BOOLEAN, BOOLEAN)
	"MUSIC_PUSH_TRACK_FINITE_TIMES_AND_NOTIFY",  # (MUSIC, INT, BOOLEAN, BOOLEAN, FLAG)
]

## The music SCRIPTING system - a separate stack the reference marks
## "music.scb only!". Same shapes as above; a distinct set of names, so the two
## must not be collapsed even though their signatures match.
const MUSIC_SCRIPT_ACTIONS := [
	"MUSIC_SCRIPT_SET_TRACK",                          # (MUSIC, BOOLEAN, BOOLEAN)
	"MUSIC_SCRIPT_PLAY_TRACK_FINITE_TIMES",            # (MUSIC, INT, BOOLEAN, BOOLEAN)
	"MUSIC_SCRIPT_PLAY_TRACK_FINITE_TIMES_AND_NOTIFY",  # (..., FLAG)
	"MUSIC_SCRIPT_PUSH_MUSIC",                         # (MUSIC, BOOLEAN, BOOLEAN)
	"MUSIC_SCRIPT_POP_MUSIC",                          # (BOOLEAN, BOOLEAN)
	"MUSIC_SCRIPT_PUSH_TRACK_FINITE_TIMES",            # (MUSIC, INT, BOOLEAN, BOOLEAN)
	"MUSIC_SCRIPT_PUSH_TRACK_FINITE_TIMES_AND_NOTIFY",  # (..., FLAG)
	"MUSIC_TURN_OFF_MUSIC_SCRIPTING",                  # (BOOLEAN fadeout)
	"MUSIC_RETURN_TO_MUSIC_SCRIPTING",                 # (BOOLEAN, BOOLEAN)
	"MUSIC_RESET_MUSIC_SCRIPTING_SYSTEM",              # (BOOLEAN fadeout)
]

const MOVIE_ACTIONS := [
	"MOVIE_PLAY_FULLSCREEN",  # (MOVIE, BOOLEAN skip_for_low_detail)
	"MOVIE_PLAY_RADAR",       # (MOVIE) - the palantir window
	"PLAY_MOVIE_IN_GAME",     # (MOVIE, BOOLEAN allow_cancel)
]

## DISPLAY_CINEMATIC_TEXT(LOCALIZED_TEXT, FONT_TYPE, INT seconds) - the bottom
## letterbox string that accompanies a cutscene. The INT is SECONDS and is
## emitted unconverted; see the class comment on durations.
const CINEMATIC_TEXT_ACTIONS := [
	"DISPLAY_CINEMATIC_TEXT",
]

## TOGGLE_AVI_CAPTURE() - a developer capture toggle, no arguments.
const CAPTURE_ACTIONS := [
	"TOGGLE_AVI_CAPTURE",
]


# --- Gap-registered members -----------------------------------------------


## The four playback-completion reads.
##
## WHY THESE CANNOT BE ANSWERED, AND WHY THAT IS NOT A MISSING FEATURE
## ------------------------------------------------------------------
## The presentation sink is a ONE-WAY channel: `emit()` returns bool and there
## is no read-back surface anywhere on SageScriptWorld. So there is no world
## method these could call - reported as a finding, not added here.
##
## But the deeper reason is that answering them at all is a determinism hazard.
## Whether a sound, a line of speech or a movie has finished depends on LOCAL
## playback: audio hardware, muted audio, a movie the player skipped, the
## low-detail setting that suppresses MOVIE_PLAY_FULLSCREEN entirely. Peers
## would answer differently on the same frame and a script branching on the
## answer would desync a lockstep match. The reference itself flags this on
## MUSIC_TRACK_HAS_COMPLETED: it warns the action can only be used to start
## other music, and that "USING THIS SCRIPT IN ANY OTHER WAY WILL CAUSE REPLAYS
## TO NOT WORK" - which is the same hazard, observed by the original authors.
##
## Registering them blocked means a map that uses one produces a
## `blocked-subsystem` gap naming the condition, and `evaluate_condition`
## returns CONDITION_FALLBACK (false) for it - the same answer any unevaluable
## condition gets, so an unevaluable gate never fires. It is never counted as
## coverage. What they need is a DETERMINISTIC playback clock driven by the
## simulation rather than by the audio device; that is a design decision for the
## owner of the world interface.
const BLOCKED_PLAYBACK_CONDITIONS := [
	"HAS_FINISHED_AUDIO",        # (SOUND)
	"HAS_FINISHED_SPEECH",       # (DIALOG)
	"HAS_FINISHED_VIDEO",        # (MOVIE)
	"MUSIC_TRACK_HAS_COMPLETED",  # (MUSIC, INT times)
]

## MUSIC_IS_PLAYING_FROM_SCRIPT() - "the music scripting system is on".
##
## Separate from the four above because the obstacle is different. This is not a
## playback-timing race: it is a plain on/off bit that
## MUSIC_TURN_OFF_MUSIC_SCRIPTING and MUSIC_RETURN_TO_MUSIC_SCRIPTING set. But
## those two actions are presentation and were emitted into the one-way sink, so
## the bit lives behind the sink with no way back. Mirroring it into
## SageScriptEnv instead would be worse than useless: env is interpreter state
## that the simulation hashes, so a presentation toggle would start feeding
## lockstep, and a condition may not mutate env anyway.
const BLOCKED_MUSIC_STATE_CONDITIONS := [
	"MUSIC_IS_PLAYING_FROM_SCRIPT",
]

## CALL_IN_REINFORCEMENTS_WITHOUT_MOVIE(TEXT_STRING army_name).
##
## The one member of this package that is NOT presentation. "WITHOUT_MOVIE"
## describes only what it SKIPS - the action still calls in a reinforcement
## army, which creates objects and is simulation state. Emitting it to the audio
## sink because its name mentions a movie would be exactly the silent no-op this
## package exists to avoid: the map would show no reinforcements and no gap.
##
## The world interface does not route it. Its doc comments name
## `ai.create_reinforcement_team` for WP11 CREATE_REINFORCEMENT_TEAM,
## `ai.remove_reinforcement_army` for WP11 REMOVE_REINFORCEMENT_ARMY, and
## `meta.living_world_command` for WP08's LivingWorld family plus
## REINFORCEMENTS_DISPLAY_BANNER. This action appears in none of them, and
## neither existing method fits its single TEXT_STRING: create_reinforcement_team
## needs a player, a team and a target, and remove_reinforcement_army is the
## inverse operation and also needs a player. Improvising a route into either
## would invent a signature; funnelling it through WP08's LivingWorld command
## would claim a member of another package's funnel.
##
## So it is registered blocked on the campaign reinforcement-army subsystem,
## which no world implements, and reported. The desired world surface is stated
## in the package report.
const BLOCKED_REINFORCEMENT_ACTIONS := [
	"CALL_IN_REINFORCEMENTS_WITHOUT_MOVIE",
]


static func register(reg: SageScriptHandlerRegistry.Registrar) -> void:
	for family: Array in [
		FADE_ACTIONS,
		VOLUME_TYPE_ACTIONS,
		VOLUME_CLASS_ACTIONS,
		REVERB_ACTIONS,
		SOUND_EFFECT_ACTIONS,
		SOUND_EVENT_ACTIONS,
		AMBIENT_ACTIONS,
		SPEECH_ACTIONS,
		MUSIC_ACTIONS,
		MUSIC_SCRIPT_ACTIONS,
		MOVIE_ACTIONS,
		CINEMATIC_TEXT_ACTIONS,
		CAPTURE_ACTIONS,
	]:
		for name: Variant in family:
			reg.action(String(name), _emit)

	reg.blocked_conditions(
		BLOCKED_PLAYBACK_CONDITIONS,
		(
			"deterministic audio/video playback-completion state (the presentation "
			+ "sink is one-way by design, and local playback timing differs per peer)"
		)
	)
	reg.blocked_conditions(
		BLOCKED_MUSIC_STATE_CONDITIONS,
		(
			"music-scripting-system on/off state read-back (the toggle was emitted "
			+ "into the one-way presentation sink and no world method exposes it)"
		)
	)
	reg.blocked_actions(
		BLOCKED_REINFORCEMENT_ACTIONS,
		"campaign reinforcement armies"
	)


static func implemented_action_names() -> Array[String]:
	## Every name this package serves, sorted. Used by the runner to assert the
	## registration matches the package's stated membership rather than whatever
	## the file happens to contain.
	var out: Array[String] = []
	for family: Array in [
		FADE_ACTIONS,
		VOLUME_TYPE_ACTIONS,
		VOLUME_CLASS_ACTIONS,
		REVERB_ACTIONS,
		SOUND_EFFECT_ACTIONS,
		SOUND_EVENT_ACTIONS,
		AMBIENT_ACTIONS,
		SPEECH_ACTIONS,
		MUSIC_ACTIONS,
		MUSIC_SCRIPT_ACTIONS,
		MOVIE_ACTIONS,
		CINEMATIC_TEXT_ACTIONS,
		CAPTURE_ACTIONS,
	]:
		for name: Variant in family:
			out.append(String(name))
	out.sort()
	return out


# --- The one handler ------------------------------------------------------


static func _emit(ctx: Dictionary) -> int:
	## Decode this action's arguments in DECLARATION ORDER and hand them to the
	## audio presentation sink under the action's own InternalName.
	var args: SageScriptArgs = ctx["args"]
	var params: Array = args.signature()

	# The dispatcher validates arity before a handler runs, but entries the
	# source reference marks uncertain are arity-exempt there. Nothing in this
	# package is uncertain, so a length disagreement here means the vocabulary
	# and the record have genuinely diverged. Reading positionally past that
	# point would emit a short or truncated argument list that looks like a real
	# one in the recording, so it hard-fails instead.
	if args.size() != params.size():
		ctx["detail"] = (
			"signature declares %d argument(s) %s but the record carries %d; "
			+ "refusing to emit a positional list that does not match the signature"
		) % [params.size(), str(params), args.size()]
		return Dispatch.Status.BAD_ARGUMENTS

	var values: Array = []
	for index in range(params.size()):
		values.append(_decode(args, String(params[index]), index))

	if not (ctx["world"] as SageScriptWorld).audio().emit(String(ctx["name"]), values):
		ctx["detail"] = "world has no %s presentation sink" % CHANNEL
		return Dispatch.Status.WORLD_REFUSED
	return Dispatch.Status.OK


static func _decode(args: SageScriptArgs, param_type: String, index: int) -> Variant:
	## One slot, by INDEX, using the payload type the DECLARED parameter implies.
	##
	## The parameter type comes from the vocabulary entry, never from a guess
	## about the value, which is what makes the five REALs of AUDIO_FADE_VOLUME
	## and the adjacent fadeout/fadein BOOLEANs safe to read.
	##
	## BOOLEAN is the one type not left to SageScriptArgs.value(): that reads the
	## storage FIELD (integer, so 1/0), and a SAGE BOOLEAN is semantically a
	## bool. See the class comment.
	if param_type == "BOOLEAN" or param_type == "BOOL":
		return args.boolean(index)
	return args.value(index)
