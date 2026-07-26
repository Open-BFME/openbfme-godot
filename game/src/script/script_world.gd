class_name SageScriptWorld
extends RefCounted

## The interface a script handler is allowed to see of "the game".
##
## WHY THIS EXISTS
## ---------------
## The script vocabulary must be testable headless, long before most of the
## simulation exists, and must not drag the simulation into every test that
## touches a counter. Handlers therefore never import retail_slice_sim.gd (or
## any other concrete world); they take a SageScriptWorld. Two implementations
## are expected: SageScriptWorldStub for tests, and a thin adapter over the real
## simulation for the game.
##
## FAIL-LOUD CONTRACT
## ------------------
## Every mutator returns bool and every accessor returns a value plus, where
## meaningful, a "supported" answer. The base class implements NOTHING: readers
## return neutral values and writers return false. A world that does not
## implement a capability makes its handler report WORLD_REFUSED, which the
## dispatcher records as a structured gap. Nothing silently no-ops.
##
## GROWING THIS INTERFACE
## ----------------------
## Add a method here only when a handler tranche needs it, and add it as a
## capability (see `supports()`) so a partial world can answer honestly. Keep it
## free of Godot node types: a world is data, not a scene.

## Capability tokens. A world advertises what it can actually do.
const CAP_PLAYER_MONEY := "player_money"
const CAP_RANDOM := "random"
const CAP_DEBUG_OUTPUT := "debug_output"


func supports(capability: String) -> bool:
	## Base world supports nothing. Override with the tokens you implement.
	return false


# --- Time -----------------------------------------------------------------


func world_frame() -> int:
	## The world's own frame counter, for actions that read game time. The
	## script environment keeps its own tick count; this is the world's.
	return 0


# --- Deterministic randomness --------------------------------------------
#
# SAGE distinguishes simulation-random (must be identical on every peer) from
# client-random (explicitly desync-prone; the reference warns about it). Only
# simulation-random is exposed here. A world that cannot supply a deterministic
# stream must report CAP_RANDOM as unsupported rather than reaching for
# randi(), which would break lockstep.


func random_int(low: int, high: int) -> int:
	return low


func random_real(low: float, high: float) -> float:
	return low


# --- Player economy -------------------------------------------------------


func player_money(player: String) -> int:
	return 0


func set_player_money(player: String, amount: int) -> bool:
	return false


# --- Diagnostics ----------------------------------------------------------


func debug_message(channel: String, text: String) -> bool:
	## Destination for DEBUG_STRING / DEBUG_MESSAGE_BOX / DEBUG_CRASH_BOX.
	## `channel` is the originating action name.
	return false
