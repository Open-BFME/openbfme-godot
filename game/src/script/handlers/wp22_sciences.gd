extends RefCounted

## WP22-sciences - the science / spellpoint script names around the spellbook
## that WP09's AI pair (PLAYER_PURCHASE_SCIENCE / PLAYER_CAN_PURCHASE_SCIENCE)
## does not own: availability control, the acquired-science query, the spell
## point cap, and the rank-level-cap gate.
##
## SCOPE, AND WHICH KIND EACH NAME IS
## ==================================
## Four names, 23 call sites total: PLAYER_SCIENCE_AVAILABILITY (12),
## PLAYER_ACQUIRED_SCIENCE (9), PLAYER_SET_MAX_SPELLPOINTS (1),
## PLAYER_HAS_REACHED_LEVEL_CAP (1). The kinds come from the sourced tables,
## NOT from the names - "science availability" reads like a question and is a
## COMMAND (it sets whether a science may be bought at all; see the three-way
## grant/purchase/availability distinction documented in wp09_ai_core.gd).
##
## Table rows served, verbatim (name | params | category | game | confidence):
##
## ACTIONS, from script_action_table.gd:
##   "PLAYER_SCIENCE_AVAILABILITY|PLAYER,SCIENCE,SCIENCE_AVAILABILITY|Player|SHARED|S" (row 520)
##   "PLAYER_SET_MAX_SPELLPOINTS|PLAYER,INT|Player|SHARED|S"                           (row 528)
##
## CONDITIONS, from script_condition_table.gd:
##   "PLAYER_ACQUIRED_SCIENCE|PLAYER,SCIENCE|Player|SHARED|S"        (row 148)
##   "PLAYER_HAS_REACHED_LEVEL_CAP|PLAYER|Player|SHARED|S"           (row 192)
##
## World surface used (all four already implemented by the retail slice
## adapter - retail_slice_script_world.gd:4790, :1572, :4429, :1525):
##   progression().set_science_availability(player, science, availability)
##   players().set_max_spell_points(player, amount)
##   progression().has_science(player, science)
##   players().reached_level_cap(player)
##
## THE ARGUMENT TRAP
## =================
## Every read below is BY INDEX, with the signature written directly above it.
## PLAYER_SCIENCE_AVAILABILITY authors three text-payload slots side by side,
## where no type-search could tell the science from the availability token -
## swapped, "make SCIENCE_X hidden" becomes "make science 'Hidden' available
## as SCIENCE_X", accepted without an error since sage_scb.py populates
## integer, real AND text for every argument. SCIENCE and SCIENCE_AVAILABILITY
## have no observed wire code and no enum row in script_param_types.gd, so
## both are text payloads passed VERBATIM: the world owns its science
## namespace and its availability token vocabulary, and folding or mapping
## either here would invent a vocabulary this repo has not sourced.
##
## CONDITIONS MAY NOT GUESS, AND MAY NOT WRITE
## ===========================================
## The two conditions write ctx.result ONLY - never the env, never the world.
## And a read the world cannot answer must never come back as a plain false:
## "the player does not own the science" and "the world cannot say" are
## different answers, and only the first may keep (or release) a gate on the
## record's own authority. Both conditions check SageWorldQuery.ok first and
## return WORLD_REFUSED otherwise, which the dispatcher turns into
## CONDITION_FALLBACK (false) *and* a structured gap. The two actions hold the
## mirror-image line: a world command returning false means "cannot model
## this" (the facet contract in script_world.gd), and that is WORLD_REFUSED
## with the player and science named - never OK-and-nothing-happened.

const PACKAGE := "WP22-sciences"

const Dispatch := preload("res://src/script/script_dispatch.gd")


static func register(reg: SageScriptHandlerRegistry.Registrar) -> void:
	reg.action("PLAYER_SCIENCE_AVAILABILITY", _player_science_availability)
	reg.action("PLAYER_SET_MAX_SPELLPOINTS", _player_set_max_spellpoints)
	reg.condition("PLAYER_ACQUIRED_SCIENCE", _player_acquired_science)
	reg.condition("PLAYER_HAS_REACHED_LEVEL_CAP", _player_has_reached_level_cap)


# --- Shared tail -------------------------------------------------------------


static func _unanswered(ctx: Dictionary, query: SageWorldQuery) -> int:
	## Every condition in this file takes this path when the world could not
	## answer. It must NEVER be replaced by "return false": a refusal is
	## recoverable, a silently wrong answer is not (see the class comment).
	ctx["detail"] = query.detail
	return Dispatch.Status.WORLD_REFUSED


# --- Actions -----------------------------------------------------------------


static func _player_science_availability(ctx: Dictionary) -> int:
	# PLAYER_SCIENCE_AVAILABILITY(PLAYER, SCIENCE, SCIENCE_AVAILABILITY)
	#   table row 520: "PLAYER_SCIENCE_AVAILABILITY|PLAYER,SCIENCE,SCIENCE_AVAILABILITY|Player|SHARED|S"
	#   arg 0: PLAYER               - whose spellbook is changed, text payload
	#   arg 1: SCIENCE              - the science name, text payload
	#   arg 2: SCIENCE_AVAILABILITY - the availability token, text payload
	#
	# An ACTION despite the noun phrase: it sets whether the science may be
	# bought at all (the third leg of the grant/purchase/availability triangle
	# wp09_ai_core.gd documents; this file owns only this leg). All three slots
	# are text, so only position separates them - read per the row above, passed
	# verbatim. The availability token vocabulary is the world's: no observed
	# enum exists in script_param_types.gd, and inventing one here would turn an
	# unknown token into a silent no-op instead of the world's own verdict.
	var args: SageScriptArgs = ctx["args"]
	var world: SageScriptWorld = ctx["world"]
	var player := args.text(0)
	var science := args.text(1)
	var availability := args.text(2)
	if not world.progression().set_science_availability(player, science, availability):
		ctx["detail"] = (
			"world does not implement progression.set_science_availability; science "
			+ "'%s' for player '%s' was NOT set to '%s'"
		) % [science, player, availability]
		return Dispatch.Status.WORLD_REFUSED
	return Dispatch.Status.OK


static func _player_set_max_spellpoints(ctx: Dictionary) -> int:
	# PLAYER_SET_MAX_SPELLPOINTS(PLAYER, INT)
	#   table row 528: "PLAYER_SET_MAX_SPELLPOINTS|PLAYER,INT|Player|SHARED|S"
	#   arg 0: PLAYER - whose cap is set, text payload
	#   arg 1: INT    - the new maximum, INTEGER payload
	#
	# The one mixed-payload signature in this file: the cap lives in the
	# integer field of slot 1, next to a text player in slot 0 - and
	# sage_scb.py fills integer, real and text for every argument, so a
	# wrong-field read succeeds silently. The value passes through as authored;
	# what the cap means (and whether it clamps current points) is the world's
	# progression model, not this handler's.
	var args: SageScriptArgs = ctx["args"]
	var world: SageScriptWorld = ctx["world"]
	var player := args.text(0)
	var amount := args.integer(1)
	if not world.players().set_max_spell_points(player, amount):
		ctx["detail"] = (
			"world does not implement players.set_max_spell_points; player '%s' "
			+ "did not get its spell point cap set to %d"
		) % [player, amount]
		return Dispatch.Status.WORLD_REFUSED
	return Dispatch.Status.OK


# --- Conditions --------------------------------------------------------------
#
# Both may answer true, may answer false, or may refuse - and the third is not
# the second.


static func _player_acquired_science(ctx: Dictionary) -> int:
	# PLAYER_ACQUIRED_SCIENCE(PLAYER, SCIENCE)
	#   table row 148: "PLAYER_ACQUIRED_SCIENCE|PLAYER,SCIENCE|Player|SHARED|S"
	#   arg 0: PLAYER  - whose spellbook is asked, text payload
	#   arg 1: SCIENCE - the science name, text payload
	#
	# ACQUIRED is not CAN-ACQUIRE: this asks whether the player already OWNS
	# the science (however it arrived - purchased or granted), which is
	# progression.has_science, one question answered by the world. Its
	# neighbour can_purchase_science answers a different question and belongs
	# to WP09. The world may itself refuse a science it has never heard of
	# rather than answer false (the slice does exactly that), and that refusal
	# passes through here unlaundered.
	var args: SageScriptArgs = ctx["args"]
	var query := (ctx["world"] as SageScriptWorld).progression().has_science(
		args.text(0), args.text(1)
	)
	if not query.ok:
		return _unanswered(ctx, query)
	ctx["result"] = query.as_bool()
	return Dispatch.Status.OK


static func _player_has_reached_level_cap(ctx: Dictionary) -> int:
	# PLAYER_HAS_REACHED_LEVEL_CAP(PLAYER)
	#   table row 192: "PLAYER_HAS_REACHED_LEVEL_CAP|PLAYER|Player|SHARED|S"
	#   arg 0: PLAYER - whose rank level is asked, text payload
	#
	# One question, answered by the world: whether the player's rank level has
	# reached its limit. NOT reconstructed here from rank_level() and a limit
	# this handler would have to guess - the world owns both numbers and their
	# comparison (the slice compares rank_level against rank_level_limit,
	# retail_slice_script_world.gd:1525).
	var args: SageScriptArgs = ctx["args"]
	var query := (ctx["world"] as SageScriptWorld).players().reached_level_cap(args.text(0))
	if not query.ok:
		return _unanswered(ctx, query)
	ctx["result"] = query.as_bool()
	return Dispatch.Status.OK
