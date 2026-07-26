class_name SageScriptParamTypes
extends RefCounted

## Parameter-type vocabulary for SAGE map scripts and SAGE Lua.
##
## Two different numbering systems meet in this file and must not be confused:
##
## 1. PARAMETER TYPE TOKENS ("COUNTER", "FLAG", "COMPARISON", ...) - the names
##    the WorldBuilder / Lua reference uses in a signature. These are what
##    script_action_table.gd and script_condition_table.gd store.
##
## 2. SCB ARGUMENT TYPE CODES - the u32 `argumentType` written in front of every
##    argument in a decoded .map / .scb script chunk (see
##    importer/openbfme_importer/sage_scb.py `_parse_argument`). These are
##    engine-internal and, like the action/condition ids themselves, are NOT
##    guaranteed portable between SAGE titles.
##
## The mapping between the two (ARGUMENT_TYPE_FOR_PARAM below) is deliberately
## PARTIAL. It contains only codes this repository has actually observed in
## decoded BFME2 skirmish data. Anything not listed resolves to UNKNOWN_CODE and
## argument validation then declines to assert rather than guessing - a wrong
## assertion here would silently mis-read a script argument, which is the exact
## silent-fallback failure mode this repo treats as a defect class.

## Sentinel for "this repo has not observed a code for that parameter type".
const UNKNOWN_CODE := -1

## Argument type code used for a 3-float map position (sage_scb.py
## `_POSITION_ARGUMENT_TYPE`). Position arguments carry no integer/real/text.
const ARGUMENT_POSITION := 16

## Observed SCB argument type codes.
##
## PROVENANCE: transcribed from the constants already in use in
## game/src/retail_slice/retail_map_scripts.gd, which were derived from decoded
## BFME2 skirmish map scripts, plus _POSITION_ARGUMENT_TYPE from sage_scb.py.
## Codes for every other parameter type are UNSOURCED, not zero.
const ARGUMENT_INTEGER := 0
const ARGUMENT_REAL := 1
const ARGUMENT_COUNTER_NAME := 4
const ARGUMENT_FLAG_NAME := 5
const ARGUMENT_COMPARISON := 6
const ARGUMENT_BOOLEAN := 8
const ARGUMENT_PLAYER := 11

## Parameter type token -> observed SCB argument type code.
## Partial by design; absence means "unsourced", never "code 0".
const ARGUMENT_TYPE_FOR_PARAM := {
	"INT": ARGUMENT_INTEGER,
	"REAL": ARGUMENT_REAL,
	"COUNTER": ARGUMENT_COUNTER_NAME,
	"FLAG": ARGUMENT_FLAG_NAME,
	"COMPARISON": ARGUMENT_COMPARISON,
	"BOOLEAN": ARGUMENT_BOOLEAN,
	"PLAYER": ARGUMENT_PLAYER,
}

## Which decoded argument field carries the payload for a parameter type.
## "integer", "real", "text" or "position". Used by SageScriptArgs to pick the
## field to read. Parameter types absent here default to "text", because every
## remaining SAGE parameter type in the reference names a game asset (a unit, a
## team, an upgrade, a waypoint, ...) and is carried as a string.
const PAYLOAD_FIELD_FOR_PARAM := {
	"INT": "integer",
	"PERCENT": "integer",
	"SECONDS": "real",
	"REAL": "real",
	"ANGLE": "real",
	"BOOLEAN": "integer",
	"BOOL": "integer",
	"COMPARISON": "integer",
	"MATH_OPERATOR": "integer",
	"RELATION": "integer",
	"AI_MOOD": "integer",
	"SURFACE_TYPE": "integer",
	"COORD3D": "position",
	# Every remaining ENUMS member. An enum is a named integer by definition, so
	# omitting one here silently routed it to the "text" default and a handler
	# read an empty string where the map authored a number - with no error, since
	# sage_scb.py populates integer, real AND text for every argument. Ten of the
	# sixteen enums were missing; ENUMS_HAVE_PAYLOAD_ROWS below keeps that from
	# happening again rather than relying on the next person noticing.
	"BUILDABILITY_TYPE": "integer",
	"EMOTION": "integer",
	"MISSION_OBJECTIVE_STATUS": "integer",
	"NEAR_OR_FAR": "integer",
	"PRODUCTION_QUEUE_TAB": "integer",
	"RADAR_EVENT": "integer",
	"SCRIPT_EVENT": "integer",
	"SHAKE_INTENSITY": "integer",
	"SKIRMISH_APPROACH_PATH": "integer",
	"STANCE_TYPE": "integer",
	# Not an enum, but numeric: a packed colour value, white = -1.
	"COLOR": "integer",
}

## Every ENUMS member must have a PAYLOAD_FIELD_FOR_PARAM row, because an enum
## is a named integer and the table's default is "text". Call this from a test:
## the failure it prevents is silent, since sage_scb.py fills integer, real and
## text for every argument, so a wrong-field read yields "" rather than an error.
static func enums_missing_payload_rows() -> Array:
	var absent: Array = []
	for key in ENUMS.keys():
		if not PAYLOAD_FIELD_FOR_PARAM.has(key):
			absent.append(key)
	absent.sort()
	return absent


## SAGE comparison operator enum.
## PROVENANCE: reference section "PARAMETER TYPES", COMPARISON table.
const COMPARE_LESS := 0
const COMPARE_LESS_EQUAL := 1
const COMPARE_EQUAL := 2
const COMPARE_GREATER_EQUAL := 3
const COMPARE_GREATER := 4
const COMPARE_NOT_EQUAL := 5

## SAGE arithmetic operator enum (used by COUNTER_MATH_*).
## PROVENANCE: reference section "PARAMETER TYPES", MATH_OPERATOR table.
const MATH_ADD := 0
const MATH_SUBTRACT := 1
const MATH_MULTIPLY := 2
const MATH_DIVIDE := 3

## Integer-valued parameter enums transcribed verbatim from the reference's
## "PARAMETER TYPES FOR ExecuteAction and EvaluateCondition" section.
##
## Only the small, engine-wide enums are carried here. The reference also lists
## the full KINDOF and OBJECT_STATUS bit vocabularies (KINDOF_BFMEIIROTWK has
## 222 entries); those belong with the object model, not with the script
## vocabulary, and are deliberately not duplicated here.
const ENUMS := {
	"COMPARISON": {"<": 0, "<=": 1, "==": 2, ">=": 3, ">": 4, "~=": 5},
	"BOOLEAN": {"false": 0, "true": 1},
	"RELATION": {"Enemy": 0, "Neutral": 1, "Friend": 2},
	"AI_MOOD": {
		"Peaceful": 0, "Sleep": 1, "Passive": 2, "Normal": 3, "Alert": 4,
		"Agressive": 5,
	},
	"SKIRMISH_APPROACH_PATH": {"Center": 0, "Backdoor": 1, "Flank": 2, "Special": 3},
	"RADAR_EVENT": {
		"Information": 0, "Construction": 1, "Upgrade": 2, "UnderAttack": 3,
		"Infiltration": 4, "Banner": 5,
	},
	"MATH_OPERATOR": {"Add": 0, "Subtract": 1, "Multiply": 2, "Divide": 3},
	"NEAR_OR_FAR": {"near": 0, "far": 1},
	"SHAKE_INTENSITY": {
		"SUBTLE": 0, "NORMAL": 1, "STRONG": 2, "SEVERE": 3, "CINE_EXTREME": 4,
		"CINE_INSANE": 5,
	},
	"STANCE_TYPE": {"GUARD": 0, "AGGRESSIVE": 1, "HOLD_POSITION": 2, "HOLD_FIRE": 3},
	"MISSION_OBJECTIVE_STATUS": {"HIDDEN": 0, "ACTIVE": 1, "COMPLETED": 2, "FAILED": 3},
	"SURFACE_TYPE": {
		"GROUND": 0, "WATER": 1, "CLIFF": 2, "AIR": 3, "RUBBLE": 4, "OBSTACLE": 5,
		"IMPASSABLE": 6, "DEEP_WATER": 7, "WALL_RAILING": 8,
		"CRUSHABLE_OBSTACLE": 9,
	},
	"PRODUCTION_QUEUE_TAB": {
		"MAIN_STRUCTURE": 0, "OTHER_STRUCTURE": 1, "INFANTRY": 2, "VEHICLE": 3,
		"AIRCRAFT": 4, "UPGRADE": 5,
	},
	"BUILDABILITY_TYPE": {
		"YES": 0, "IGNORE_PREREQUISITES": 1, "NO": 2, "ONLY_BY_AI": 3,
	},
	"EMOTION": {
		"TAUNT": 0, "CHEER": 1, "HERO_CHEER": 2, "POINT": 3, "FEAR": 4,
		"UNCONTROLLABLE_FEAR": 5, "TERROR": 6, "DOOM": 7, "QUARRELSOME": 8,
		"BRACE_FOR_BEING_CRUSHED": 9, "ALERT": 10, "CHEER_FOR_ABOUT_TO_CRUSH": 11,
	},
	"SCRIPT_EVENT": {
		"ATTACK_MOVE_ISSUED": 0, "WAYPOINT_MODE_ENTERED": 1,
		"CONTROL_GROUP_CREATED": 2,
	},
}

## Parameter type tokens the reference itself leaves unresolved. A signature
## containing any of these is flagged CONFIDENCE "U" in the generated tables.
const UNCERTAIN_TOKENS := ["?", "...", "SHADER?", "BOOL?"]


static func argument_type_for_param(param: String) -> int:
	return int(ARGUMENT_TYPE_FOR_PARAM.get(param, UNKNOWN_CODE))


static func payload_field_for_param(param: String) -> String:
	return String(PAYLOAD_FIELD_FOR_PARAM.get(param, "text"))


static func is_uncertain_token(token: String) -> bool:
	return UNCERTAIN_TOKENS.has(token)


static func compare(left: float, comparison: int, right: float) -> bool:
	match comparison:
		COMPARE_LESS:
			return left < right
		COMPARE_LESS_EQUAL:
			return left <= right
		COMPARE_EQUAL:
			return is_equal_approx(left, right)
		COMPARE_GREATER_EQUAL:
			return left >= right
		COMPARE_GREATER:
			return left > right
		COMPARE_NOT_EQUAL:
			return not is_equal_approx(left, right)
	return false


static func compare_int(left: int, comparison: int, right: int) -> bool:
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
