extends RefCounted

## WP23 misc-verbs - the six remaining misc unit/player script actions:
## UNIT_SET_MODELCONDITION_FOR_DURATION (6 retail call sites),
## PLAYER_RELATES_PLAYER (2), DESELECT (2), IDLE_ALL_UNITS (1),
## PLAYER_FORCE_EMOTION (1) and FIND_HOME_BASE_OF_PLAYER (1).
##
## Table rows served, verbatim from script_action_table.gd (name | params |
## category | game | confidence):
##
##   "UNIT_SET_MODELCONDITION_FOR_DURATION|UNIT,MODEL_CONDITION,REAL,PERCENT|Unit|SHARED|S"  (line 925)
##   "PLAYER_RELATES_PLAYER|PLAYER,PLAYER,RELATION|Player|SHARED|S"                          (line 515)
##   "DESELECT|-|Player|SHARED|S"                                                            (line 183)
##   "IDLE_ALL_UNITS|-|Scripting|SHARED|S"                                                   (line 265)
##   "PLAYER_FORCE_EMOTION|PLAYER,EMOTION,REAL|Player|SHARED|S"                              (line 504)
##   "FIND_HOME_BASE_OF_PLAYER|PLAYER,UNIT_REF,BOOLEAN|-|SHARED|S"                           (line 229)
##
## World surfaces used (each facet docstring names the action it serves):
##   units().set_model_condition(object_name, condition, enabled, duration_ticks)
##   players().set_relation_to(player, other, relation)
##   ui().emit(op, values)                                  - presentation sink
##   orders().idle_for_ticks(scope, name, ticks)
##   players().force_emotion(player, emotion, duration_ticks)
##   players().home_base(player) + units().set_reference(reference, object_name)
##
##
## THE ARGUMENT TRAPS
## ==================
## Every read below is BY INDEX, with the signature written directly above it.
##   * UNIT_SET_MODELCONDITION_FOR_DURATION leads with TWO text-carried names
##     (the subject, then the condition) that no type-search could tell apart.
##   * PLAYER_RELATES_PLAYER carries TWO PLAYERS and the order decides WHOSE
##     diplomacy row is written - a swap silently inverts who befriends whom.
##   * FIND_HOME_BASE_OF_PLAYER puts the queried PLAYER first and the bound
##     UNIT_REF second; rotated, it would query a reference name as a player.
## The runner pins each signature against logged world calls whose fixture
## names are mutually distinguishable, so a swapped read is a DIFFERENT string.
##
##
## DURATIONS, AND ONE SENTINEL COLLISION
## =====================================
## Both REAL durations are authored in SECONDS and both facet slots take
## interpreter TICKS (script_world.gd argument conventions), converted with
## env.ticks_from_seconds - the same rounding as the timer actions. A negative
## duration has no meaning in the reference and is BAD_ARGUMENTS (the WP11
## verdict). The two facets then differ at zero:
##   * players.force_emotion documents NO sentinel: 0 ticks is a plain
##     zero-length emotion, exactly expressible, served.
##   * units.set_model_condition documents duration_ticks <= 0 (with enabled)
##     as the INDEFINITE variant, so an authored 0-second flash CANNOT be
##     expressed - converting it would turn a momentary condition into a
##     forever-condition, an inversion, not an approximation. 0 refuses and
##     names the sentinel (the WP11 TEAM_GUARD_FOR_SECONDS verdict, exactly).
##
##
## THE PERCENT GUARD (why a served action still refuses one value range)
## =====================================================================
## UNIT_SET_MODELCONDITION_FOR_DURATION's fourth argument is a PERCENT the
## facet surface has no slot for: units.set_model_condition carries subject,
## condition, enable and duration - nothing else - even though its docstring
## claims this exact action. The facet contract and the sourced signature can
## both be honoured only where the percent CHANGES NOTHING: 100 is the
## identity value under either available reading (100% of the subject / full
## intensity), so 100 is served with the percent consumed here. Any other
## value would change the outcome the map authored, and the rule applied
## throughout this repo is that an outcome-changing argument may never be
## dropped (WP19's orientation variant) - so a non-identity percent refuses as
## WORLD_REFUSED, naming the missing slot. The guard costs no expressiveness
## the surface actually has.
##
##
## DESELECT IS PRESENTATION, AND WHY THE SINK IS SANCTIONED
## ========================================================
## DESELECT authors NO arguments: it clears the acting seat's CURRENT
## SELECTION, a per-seat UI notion. Selection is deliberately excluded from
## the simulation state_hash, and WP07 already ruled on this exact family:
## OBJECT_FORCE_SELECT routes to the UI presentation sink because the one
## facet that touches selection - units.set_selected, whose docstring names
## SELECT_OBJECT / DESELECT - takes ONE OBJECT NAME, and this action authors
## none, the identical shape mismatch WP07 documented. So DESELECT emits on
## the UI sink under its own InternalName with zero values (WP07's
## _emit_no_arguments shape): an ACKNOWLEDGED one-way presentation emit, not a
## silent no-op - a world with no UI adapter refuses loudly, and if selection
## is later ruled simulation state this handler moves with that ruling, same
## as WP07's.
##
##
## IDLE_ALL_UNITS FANS OUT, IT DOES NOT PICK A SEAT
## ================================================
## Retail's IDLE_ALL_UNITS idles EVERY unit on the map. The facet is
## orders.idle_for_ticks, whose docstring reserves Scope.PLAYER for this exact
## action - but the action authors no player, and naming any single seat here
## (the bound script player, say) would silently shrink the order's scope,
## which is an outcome change. So the handler passes the retail all-players
## token and lets the WORLD own its resolution, the same ownership split WP20
## applied to "<This Player>": a world that cannot fan an order out over every
## player refuses loudly (the current retail slice does exactly that) rather
## than this handler guessing a seat. Ticks are 0: idle_for_ticks documents no
## indefinite sentinel, so 0 is a plain momentary idle - a stop order - which
## is what the retail action means.
##
##
## FIND_HOME_BASE_OF_PLAYER BINDS A RESULT
## =======================================
## Its second parameter is a UNIT_REF: this is a RESULT BINDER, not an order.
## The handler queries players.home_base (whose docstring names this exact
## action and answers the base's object name) and binds the answer through
## units.set_reference - destination first, the WP16 reference-store shape,
## into the WORLD's one object-name/reference namespace. Either leg's refusal
## surfaces as WORLD_REFUSED with the world's own detail; a refused query
## binds NOTHING, because a guessed binding would let every downstream
## NAMED_/UNIT_REF reader resolve a base the world never answered. The
## trailing BOOLEAN has no slot on either surface: the facet designer gave
## players.home_base a player-only signature while naming this action, ruling
## the flag a query modifier the world does not model separately - that
## interface verdict is followed here (the WP16 "the facet's design, not this
## file's shortcut" precedent) and recorded as an open question against the
## facet contract rather than re-litigated per call.

const PACKAGE := "WP23-misc-verbs"

const Dispatch := preload("res://src/script/script_dispatch.gd")

## The retail all-players token (see the retail slice's player-token table).
## IDLE_ALL_UNITS is map-wide; the WORLD owns what "every player" resolves to.
const ALL_PLAYERS := "<All Players>"

## The only PERCENT value units.set_model_condition can express - see the
## class comment. Anything else refuses rather than being dropped.
const IDENTITY_PERCENT := 100


static func register(reg: SageScriptHandlerRegistry.Registrar) -> void:
	reg.action(
		"UNIT_SET_MODELCONDITION_FOR_DURATION", _unit_set_modelcondition_for_duration
	)                                                                   # 6
	reg.action("PLAYER_RELATES_PLAYER", _player_relates_player)         # 2
	reg.action("DESELECT", _deselect)                                   # 2
	reg.action("IDLE_ALL_UNITS", _idle_all_units)                       # 1
	reg.action("PLAYER_FORCE_EMOTION", _player_force_emotion)           # 1
	reg.action("FIND_HOME_BASE_OF_PLAYER", _find_home_base_of_player)   # 1


# --- Shared tails ----------------------------------------------------------


static func _served(ctx: Dictionary, method: String, accepted: bool) -> int:
	## Every facet command in this file ends here. A facet that returns false
	## has already told the world (via Facet._refuse_command) which method
	## refused; this turns that into the status the dispatcher records as a
	## gap, naming the same method so the gap points at the missing WORLD
	## surface rather than at the action.
	if not accepted:
		ctx["detail"] = "world does not implement %s" % method
		return Dispatch.Status.WORLD_REFUSED
	return Dispatch.Status.OK


# --- Actions ---------------------------------------------------------------


static func _unit_set_modelcondition_for_duration(ctx: Dictionary) -> int:
	# UNIT_SET_MODELCONDITION_FOR_DURATION(UNIT, MODEL_CONDITION, REAL, PERCENT)
	#   "<UNIT> gets modelcondition <MODEL_CONDITION> for <REAL> seconds."
	#
	# TWO text-carried names lead the signature - subject first, condition
	# second - and only position separates them. The FOR_DURATION spelling is
	# an ENABLE (the plain UNIT_SET_MODELCONDITION carries the BOOLEAN; this
	# one does not), the seconds become ticks, and the percent is consumed by
	# the identity guard - see the class comment for both refusals below.
	var args: SageScriptArgs = ctx["args"]
	var seconds := args.real(2)
	if seconds < 0.0:
		ctx["detail"] = (
			"UNIT_SET_MODELCONDITION_FOR_DURATION was authored with a negative "
			+ "duration (%s seconds); the reference gives no meaning to a negative one"
		) % str(seconds)
		return Dispatch.Status.BAD_ARGUMENTS
	if seconds == 0.0:
		ctx["detail"] = (
			"units.set_model_condition treats duration_ticks <= 0 with enabled as "
			+ "'set the condition indefinitely', so an authored duration of 0 "
			+ "seconds cannot be expressed - serving it would turn a momentary "
			+ "flash into a forever-condition; NEEDED: an explicit indefinite "
			+ "flag instead of the <= 0 sentinel"
		)
		return Dispatch.Status.WORLD_REFUSED
	var percent := args.integer(3)
	if percent != IDENTITY_PERCENT:
		ctx["detail"] = (
			"units.set_model_condition(object_name, condition, enabled, "
			+ "duration_ticks) has no percent slot, and the authored percent "
			+ "(%d) is not the identity value 100 - dropping it would change "
			+ "the outcome the map authored; NEEDED: a percent parameter on "
			+ "units.set_model_condition"
		) % percent
		return Dispatch.Status.WORLD_REFUSED
	var env: SageScriptEnv = ctx["env"]
	return _served(
		ctx, "units.set_model_condition",
		(ctx["world"] as SageScriptWorld).units().set_model_condition(
			args.text(0),                        # UNIT (the subject)
			args.text(1),                        # MODEL_CONDITION
			true,                                # FOR_DURATION is an enable
			env.ticks_from_seconds(seconds)      # REAL seconds -> ticks
		)
	)


static func _player_relates_player(ctx: Dictionary) -> int:
	# PLAYER_RELATES_PLAYER(PLAYER, PLAYER, RELATION)
	#   "<PLAYER> considers <PLAYER> to be <RELATION>."
	#
	# TWO PLAYERS, AND THE ORDER DECIDES WHOSE DIPLOMACY ROW IS WRITTEN.
	# Argument 0 is the SUBJECT whose stance changes; argument 1 is the player
	# it is now felt toward. Both are PLAYER-typed strings, so nothing but
	# position could recover a swap - which would not fail, it would quietly
	# invert who befriends whom. The RELATION is the sourced integer enum
	# (Enemy 0 / Neutral 1 / Friend 2) and passes to the world VERBATIM: the
	# world owns its diplomacy model, including what it rejects.
	var args: SageScriptArgs = ctx["args"]
	return _served(
		ctx, "players.set_relation_to",
		(ctx["world"] as SageScriptWorld).players().set_relation_to(
			args.text(0),      # PLAYER (the subject)
			args.text(1),      # PLAYER (felt toward)
			args.integer(2)    # RELATION
		)
	)


static func _deselect(ctx: Dictionary) -> int:
	# DESELECT() - clears the acting seat's current selection.
	#
	# Emitted on the UI presentation sink with zero values (the signature has
	# no arguments to emit) - see the class comment for why the sink, and not
	# units.set_selected, is the sanctioned route. A world with no UI adapter
	# refuses, and that refusal is a WORLD_REFUSED gap naming the action; it
	# never becomes OK, because a deselect that silently did not happen is the
	# exact failure the sink subsystem exists to make visible.
	var world: SageScriptWorld = ctx["world"]
	if not world.ui().emit(String(ctx["name"]), []):
		ctx["detail"] = "world has no UI presentation sink"
		return Dispatch.Status.WORLD_REFUSED
	return Dispatch.Status.OK


static func _idle_all_units(ctx: Dictionary) -> int:
	# IDLE_ALL_UNITS() - every unit on the map goes idle.
	#
	# Scope.PLAYER on the all-players token, 0 ticks: the facet docstring
	# reserves this exact routing, the token keeps the fan-out decision in the
	# world, and 0 is a plain momentary idle because idle_for_ticks documents
	# no indefinite sentinel (WP11's TEAM_IDLE_FOR_SECONDS reading). See the
	# class comment for why no single seat may be substituted here.
	return _served(
		ctx, "orders.idle_for_ticks",
		(ctx["world"] as SageScriptWorld).orders().idle_for_ticks(
			SageScriptWorld.Scope.PLAYER, ALL_PLAYERS, 0
		)
	)


static func _player_force_emotion(ctx: Dictionary) -> int:
	# PLAYER_FORCE_EMOTION(PLAYER, EMOTION, REAL)
	#   "<PLAYER>'s units are forced to emote <EMOTION> for <REAL> seconds."
	#
	# EMOTION is the sourced integer enum and passes verbatim. The REAL is
	# seconds -> ticks; a negative duration has no sourced meaning and is
	# BAD_ARGUMENTS, while 0 is exactly expressible (players.force_emotion
	# documents no sentinel) and therefore served, not refused.
	var args: SageScriptArgs = ctx["args"]
	var seconds := args.real(2)
	if seconds < 0.0:
		ctx["detail"] = (
			"PLAYER_FORCE_EMOTION was authored with a negative duration "
			+ "(%s seconds); the reference gives no meaning to a negative one"
		) % str(seconds)
		return Dispatch.Status.BAD_ARGUMENTS
	var env: SageScriptEnv = ctx["env"]
	return _served(
		ctx, "players.force_emotion",
		(ctx["world"] as SageScriptWorld).players().force_emotion(
			args.text(0),                        # PLAYER
			args.integer(1),                     # EMOTION
			env.ticks_from_seconds(seconds)      # REAL seconds -> ticks
		)
	)


static func _find_home_base_of_player(ctx: Dictionary) -> int:
	# FIND_HOME_BASE_OF_PLAYER(PLAYER, UNIT_REF, BOOLEAN)
	#   "Find <PLAYER>'s home base and bind it to <UNIT_REF>."
	#
	# QUERY, THEN BIND - and each leg may refuse on its own. The PLAYER is
	# argument 0 and the reference DESTINATION argument 1; rotated, this would
	# ask the world for a reference name's home base, which a fixture-driven
	# world refuses but a permissive one might answer. A refused query binds
	# NOTHING (see the class comment), and the bind goes destination-first
	# through units.set_reference into the world's single object-name/
	# reference namespace, exactly as WP16's SET_UNIT_REFERENCE does. The
	# trailing BOOLEAN is arity-checked but consumed by the facet contract -
	# class comment, "FIND_HOME_BASE_OF_PLAYER BINDS A RESULT".
	var args: SageScriptArgs = ctx["args"]
	var world := ctx["world"] as SageScriptWorld
	var query := world.players().home_base(args.text(0))
	if not query.ok:
		ctx["detail"] = query.detail
		return Dispatch.Status.WORLD_REFUSED
	return _served(
		ctx, "units.set_reference",
		world.units().set_reference(
			args.text(1),              # UNIT_REF (the destination)
			String(query.value)        # the answered home-base object name
		)
	)
