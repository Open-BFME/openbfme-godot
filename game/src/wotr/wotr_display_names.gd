extends RefCounted

## THE NAMES A PLAYER READS, resolved from retail's own string tables and from
## nowhere else.
##
## The strategic HUD used to print data keys verbatim - "PLAYERANGMAR" in the
## masthead, "PlayerDwarves" in the seat table, "Amon Sul (Amon_Sul)" and
## "The Barrow-downs (Barrow_Downs)" in the territory list. Those are ids, not
## names, and an adversarial blind review called every one of them individually
## disqualifying. This class is the ONE place ids become English, and it is the
## one place that says, by name, which ids it could not.
##
## THERE IS NO HAND-WRITTEN id -> name MAP ANYWHERE IN HERE. Inventing one would
## manufacture parity, and retail's own data is the proof it cannot be done by
## rule: `LW:DisplayNameArnor` reads "Arthedain", `Buckland` reads "The North
## Downs", `PlayerWild` reads "Goblins". None of those is recoverable from the id.
##
## ------------------------------------------------------------------------------
## THE TWO LADDERS
## ------------------------------------------------------------------------------
##
## A SEAT (`LivingWorldPlayerTemplate` -> the name over the turn band, the seat
## plaque and the garrison card):
##
##   1. `wotr_setup_bindings.ARMY_SIDE_KEYS` binds the template to a `SIDE:` key.
##      It is the SAME seven-row table the GAME SETUP screen resolves its Army
##      column through, with its evidence documented at that table. It is a TABLE
##      and not a rule because stripping "Player" and looking up `SIDE:<rest>`
##      answers confidently and WRONGLY for the goblins: retail's template is
##      `PlayerWild`, its key is `SIDE:Wild`, and its text is "Goblins".
##   2. The `SIDE:` text comes out of retail's `data/lotr.str` through the setup
##      string bundle (`wotr-setup-strings.json`), which converts the shell
##      namespaces the strategic bundle deliberately leaves out.
##
## A REGION OR A TERRITORY (`displayName = "LW:DisplayNameArnor"` -> the region
## card, the palantir label, the territory member list):
##
##   1. The living-world string bundle (`strings.json`), asked for the region's
##      OWN authored key, exactly as authored.
##   2. THE SAME KEY FOLDED TO UPPER CASE, and only when the fold is
##      UNAMBIGUOUS. This rung exists because retail's own two documents disagree
##      about the case of exactly one key: the region ini asks for
##      `LW:DisplayNameBrownLands` and `data/lotr.str` writes
##      `LW:DisplayNameBrownlands` ("Old Brown Lands"). SAGE's string manager
##      folds case, so retail ships a working name and only a case-preserving
##      converter loses it.
##
##      IT IS BOUNDED BY MEASUREMENT, not by hope. `data/lotr.str` carries 10,973
##      label lines; exactly THREE spellings collide when folded
##      (`OBJECT:ElvenBattleShip`/`...ship`, `OBJECT:ElvenBombardShip`/`...ship`,
##      `UPGRADE:WildBasicTraining`/`Upgrade:...`), and inside the strategic
##      bundle's own namespaces exactly three more
##      (`CONTROLBAR:LW_UNIT_Arnor{Archer,Fighter,Ranger}HordePlural` against
##      their `LW_Unit_` twins - and the archer pair genuinely disagrees, reading
##      "Arnor Archer Battalions" against "Dunedain Archer Battalions"). So a
##      fold that lands on MORE THAN ONE authored key is refused as an ambiguous
##      fold and recorded as a gap; it is never a coin toss. No `LW:DisplayName*`
##      key is in any collision, and with this rung every one of the 52 playable
##      regions and all 7 territories resolve.
##
## WHAT HAPPENS ON A MISS. The caller is handed the CLEANED id - the retail id
## with its underscores turned back into spaces - and the miss is recorded in
## `gaps` with the reason, so the diagnostics panel can name it. The cleaning is
## whitespace, not translation: "Barrow_Downs" becomes "Barrow Downs" and nothing
## is spelled, capitalised, hyphenated or expanded. Retail's own text for that
## region may well be something else entirely (it is "The Barrow-downs"), which
## is exactly why the miss is a NAMED GAP rather than a silent success.
##
## Nothing here reads or writes simulation state.

const SetupStringsScript = preload("res://src/wotr/wotr_setup_strings.gd")
const SetupBindingsScript = preload("res://src/wotr/wotr_setup_bindings.gd")

## The setup string bundle (the `SIDE:` namespace), or null with the reason.
var strings: SetupStringsScript = null
var reason := ""
## The living-world string bundle (`wotr_strings.gd`), bound by the screen after
## it loads it. Null until then, and every region name is then a recorded gap.
var living_world = null
## `id -> why its name did not resolve`, in first-miss order. Public so the
## diagnostics panel can list every name the HUD is standing in for.
var gaps: Dictionary = {}

## `FOLDED KEY -> [authored keys]`, built once off the living-world bundle. A
## folded key with more than one authored spelling is refused, never picked from.
var _folded: Dictionary = {}
var _folded_built := false


## Look for the setup string bundle in the same roots the strategic string table
## is looked for in (`wotr_setup_strings.candidate_paths` owns the search order).
## A miss keeps `strings` null and records why; nothing substitutes.
func locate_and_load(roots: Array = []) -> Dictionary:
	var table := SetupStringsScript.new()
	var found: Dictionary = table.locate_and_load(roots)
	if bool(found.get("ok", false)):
		strings = table
		reason = ""
	else:
		strings = null
		reason = String(found.get("reason", ""))
	return found


## Bind the living-world string table the screen already loads, so region and
## territory keys resolve through the same gap register seats do.
func bind_living_world(table) -> void:
	living_world = table
	_folded = {}
	_folded_built = false


# --- seats -----------------------------------------------------------------------

## RETAIL'S NAME FOR A SEAT TEMPLATE, or "" with the gap recorded.
func seat_name(template: String) -> String:
	if template.is_empty():
		return ""
	var key := String(SetupBindingsScript.ARMY_SIDE_KEYS.get(template, ""))
	if key.is_empty():
		_gap(template, "no row in wotr_setup_bindings.ARMY_SIDE_KEYS binds this template to a SIDE: key")
		return ""
	if strings == null:
		_gap(template, "the setup string bundle is not converted, so %s cannot be read" % key)
		return ""
	var text := strings.text(key)
	if text.is_empty():
		_gap(template, "the setup string bundle carries no entry for %s" % key)
		return ""
	return text


## The label the screen actually prints for a seat: retail's name when it
## resolves, the CLEANED template id when it does not - never the raw key, and
## never a name derived from it.
func seat_label(template: String) -> String:
	var name := seat_name(template)
	return name if not name.is_empty() else clean_id(template)


# --- regions, territories and every other LW: key ---------------------------------

## RETAIL'S TEXT FOR A LIVING-WORLD STRING KEY, by the two-rung ladder at the top
## of this file, or "" with the gap recorded against `subject` (the region or
## territory id, so the diagnostics list reads as ids rather than as keys).
func living_world_text(key: String, subject: String) -> String:
	if key.is_empty():
		_gap(subject, "retail's own data authors no display-name key for it")
		return ""
	if living_world == null or not living_world.loaded:
		_gap(subject, "the living-world string bundle is not converted, so %s cannot be read" % key)
		return ""
	var exact := String(living_world.text(key))
	if not exact.is_empty():
		return exact
	# THE FOLD, and only when it is unambiguous. See the header for the census
	# that bounds this and for the one key retail itself spells two ways.
	_build_fold()
	var candidates: Array = _folded.get(key.to_upper(), []) as Array
	if candidates.size() == 1:
		return String(living_world.text(String(candidates[0])))
	if candidates.size() > 1:
		_gap(subject, ("%s is not authored, and folding its case lands on %d different "
			+ "authored keys (%s), so no fold is picked") % [
				key, candidates.size(), ", ".join(candidates.map(func(v: Variant) -> String: return String(v)))])
		return ""
	_gap(subject, "retail's converted string table carries no entry for %s in any case" % key)
	return ""


## The label a region or territory is shown under: retail's own English, or the
## CLEANED id with the gap already recorded. Never the raw key, never a name
## spelled out of the id.
func living_world_label(key: String, subject: String) -> String:
	var text := living_world_text(key, subject)
	return text if not text.is_empty() else clean_id(subject)


## RETAIL'S OWN TEXT FOR A SHELL (`APT:`) KEY, or "" with the gap recorded.
##
## The living-world HUD's own furniture - the TERRITORY / ARMIES / STRUCTURES
## tab captions, MAIN MENU, AUTO-RESOLVE, the "Command Points" caption - are all
## strings retail authors in `data/lotr.str` under the `APT:` namespace, which
## the SETUP string bundle converts (the living-world bundle deliberately carries
## only `LW:`/`LWA:`/`CONTROLBAR:`). They were literals in this project's own
## source until now; a literal that happens to agree with retail is still this
## project's wording, and the moment retail's differs nobody finds out.
##
## A miss is a RECORDED GAP and returns "", so the caller keeps whatever literal
## it had and the diagnostics panel says the caption is not retail's own.
func shell_text(key: String) -> String:
	if key.is_empty():
		return ""
	if strings == null:
		_gap(key, "the setup string bundle is not converted, so retail's own text for %s cannot be read" % key)
		return ""
	var text := strings.text(key)
	if text.is_empty():
		_gap(key, "the setup string bundle carries no entry for %s" % key)
		return ""
	return text


## The caption a control actually prints: retail's own text when it resolves, and
## the caller's own literal when it does not - with the gap already recorded, so
## a stand-in caption can never quietly become a claim about retail's wording.
func shell_label(key: String, fallback: String) -> String:
	var text := shell_text(key)
	return text if not text.is_empty() else fallback


## A retail id made readable WITHOUT being translated: underscores become the
## spaces they stand for and nothing else changes. `Barrow_Downs` -> `Barrow
## Downs`. This is never a claim about retail's wording - the caller has already
## recorded the gap that says retail's own name was not readable.
static func clean_id(id: String) -> String:
	return id.replace("_", " ").strip_edges()


func _build_fold() -> void:
	if _folded_built:
		return
	_folded_built = true
	if living_world == null or not living_world.loaded:
		return
	for key_value in living_world.strings.keys():
		var key := String(key_value)
		var folded := key.to_upper()
		var bucket: Array = _folded.get(folded, []) as Array
		bucket.append(key)
		_folded[folded] = bucket


func _gap(subject: String, why: String) -> void:
	if not gaps.has(subject):
		gaps[subject] = why


## The gap register as sorted lines, for the diagnostics panel.
func gap_lines() -> Array[String]:
	var subjects: Array[String] = []
	for key in gaps.keys():
		subjects.append(String(key))
	subjects.sort()
	var lines: Array[String] = []
	for subject in subjects:
		lines.append("%s - %s" % [subject, String(gaps[subject])])
	return lines
