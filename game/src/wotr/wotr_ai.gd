extends RefCounted

## THE WAR OF THE RING OPPONENT: what an `ai` seat does when its turn arrives.
##
## ============================================================================
## WHY THIS FILE EXISTS AT ALL, AND WHAT IT IS HONEST ABOUT
## ============================================================================
##
## Until this file landed, a seat marked `ai` in the standings did NOTHING on its
## turn. The strategic layer cycled the turn index and handed control straight
## back, so the map only ever changed when the human moved: War of the Ring was a
## sandbox with a scoreboard, not a game. `wotr_strategic_gaps.gd` recorded that
## as `strategic_ai_turns` and justified it with "the document carries no AI
## decision data; an invented AI would masquerade as parity".
##
## HALF OF THAT JUSTIFICATION WAS FALSE, and it is corrected in the register now.
## Retail ships `data/ini/livingworldaitemplate.ini` - 781 bytes in the RotWK
## layer - and it is not empty. It is a single `LivingWorldAITemplate
## DefaultAITemplate` block carrying twenty authored weights, seven of which
## (`BonusPreference*`) are exactly a preference ordering over the SAME region
## bonus fields the living-world document records per region. Retail's AI does
## have recorded taste in which region it wants; the living-world DOCUMENT (the
## importer's JSON) simply does not carry it, because the importer reads
## `livingworldregions.ini` and never read this file.
##
## So this opponent is built in two clearly separated halves, and every consumer
## can tell them apart at runtime through `decision_provenance()`:
##
##   RETAIL-DERIVED - WHICH region is worth taking. Scored by retail's own
##   `BonusPreference*` weights multiplied by the document's own per-region
##   bonuses. Nothing here is invented; if the template is not loaded, the retail
##   term is ZERO and the report says so by name rather than substituting a
##   guess that would look identical on screen.
##
##   PROJECT-AUTHORED - WHEN to attack, HOW to reach a front line, and how to
##   break ties. Retail records NOTHING about any of this: no strength ratio, no
##   pathing rule, no per-turn action budget. Every constant on this side is
##   prefixed `PROJECT_`, is listed in `PROJECT_AUTHORED_RULES` with the sentence
##   that describes it, and is reported to the screen. This is the "openly
##   project-authored system" the gap register said an AI would have to be before
##   it could ship - not parity, and never dressed as parity.
##
## ============================================================================
## DETERMINISM
## ============================================================================
##
## Identical state produces identical decisions. There is no `randf()`, no
## `randi()`, no `randomize()`, no `Time` call and no engine RNG anywhere on this
## path, and the AI runner asserts that by READING THIS FILE, the same way the
## auto-resolve runner does.
##
## Where a genuine tie has to be broken (two targets that score the same), the
## order comes from `seed_for()`, which is the SHA-256 of the strategic state
## hash - a value both peers already agree on before the AI thinks at all. Two
## machines on the same state therefore break the same tie the same way.
##
## Every mutation the AI makes goes through the SAME session doors the human's
## clicks do - `session.move_armies()`, `session.commit_attack()`,
## `session.auto_resolve_pending_battle()`, `session.end_phase()`. The AI has no
## private path into authoritative state, cannot make a move the rules would
## refuse a human, and therefore cannot desynchronise the strategic hash.

const StateScript = preload("res://src/wotr/wotr_state.gd")
const BuildingsScript = preload("res://src/wotr/wotr_buildings.gd")


# =============================================================================
# RETAIL'S AI TEMPLATE
# =============================================================================

## `data/ini/livingworldaitemplate.ini`, read verbatim.
##
## THIS IS AN INI, NOT A CONVERTED BUNDLE, and that is a stated compromise rather
## than a preference. No importer stage emits an `openbfme.living-world-ai-
## template` document today, so there is nothing schema-shaped to load; the file
## is 781 bytes of one flat `Key = Integer` block and parsing it here is honest
## in a way that hardcoding its twenty numbers into GDScript would not be
## (retail payloads do not live outside `workspace`). The staging rule in
## `tools/wotr-data-staging.ps1` names the missing converter, so a release that
## ships without these weights REPORTS the AI as degraded instead of quietly
## shipping an opponent with no retail taste.
const BUNDLE_ENV := "OPENBFME_LIVING_WORLD_AI_TEMPLATE"
const FILE_NAME := "livingworldaitemplate.ini"

## Where the file sits inside a retail asset tree, searched under every root the
## caller supplies so the loader finds it the moment a bundle carries it.
const PACK_RELATIVE_DIRECTORY := "data/ini"

## An ini bigger than this is not retail's AI template (which is 781 bytes); it
## is something else that happens to share the name, and is refused rather than
## parsed.
const MAX_BYTES := 64 * 1024

const RETAIL_BLOCK_KEYWORD := "LivingWorldAITemplate"
const RETAIL_BLOCK_END := "End"

## RETAIL'S `BonusPreference<X>` -> the living-world document's own per-region
## `bonuses` field it weighs. Six of retail's seven preferences name a field the
## document records for every region, so six bind exactly and by name.
##
## The binding is NOT a guess: retail's key and the document's field are the same
## word (`BonusPreferenceResource` / `resource`), and `wotr_world.gd` reads those
## fields straight out of `livingworldregions.ini`'s own `ResourceBonus`,
## `ArmyBonus`, `LegendaryBonus`, `AttackBonus`, `DefenseBonus`,
## `ExperienceBonus` rows.
const BONUS_PREFERENCE_FIELDS := {
	"BonusPreferenceArmy": "army",
	"BonusPreferenceAttack": "attack",
	"BonusPreferenceDefense": "defense",
	"BonusPreferenceExperience": "experience",
	"BonusPreferenceLegendary": "legendary",
	"BonusPreferenceResource": "resource",
	# `BonusPreferenceTreasury` IS BOUND NOW, and it used to be this register's
	# headline unbindable weight - retail's highest, 6 in the shipped template -
	# listed as naming "no region-bonus field the document records". It names
	# `fertileTerritory`, and retail says so twice in its own string table:
	# `LW:RegionTreasuryBonus` is the display string for that field ("+%d
	# Treasure") and `STRATEGICHUD:RegionTreasuryIncomeBonusTooltip` is the
	# tooltip drawn over it ("Treasury Income Bonus"). Every other region bonus
	# retail authors has its own string naming it something that is explicitly NOT
	# treasure - `resource` is "+%d%% Resource Multiplier", `extraStartResources`
	# is "Extra Resources at the Start of Battles" - so this is an elimination
	# over retail's own vocabulary, not a resemblance between two words.
	"BonusPreferenceTreasury": "fertileTerritory",
}

## NO PREFERENCE IS UNBOUND ANY MORE. The table stays - empty, and reported
## empty through `decision_provenance()` - because a capability list that only
## ever grows is a list nobody believes, and a reader who remembers the
## `BonusPreferenceTreasury` entry that was here is entitled to see that it went
## rather than to wonder whether it was quietly dropped.
const BONUS_PREFERENCE_UNBOUND := {}

## RETAIL WEIGHTS THIS LAYER CANNOT SPEND, and the gap that is why. This used to
## carry TWO prefixes; `BuildingScore` has left it, because `plan_builds()` below
## now spends all four of those weights on retail's own four building types. The
## nine `Desired*Ratio` weights remain, because they govern RECRUITMENT and no
## army roster in the document carries a purchase cost.
const UNSPENDABLE_WEIGHT_PREFIXES := {
	"Desired": "army_recruitment_and_cp_costs",
}


var loaded := false
var source_path := ""
var template_name := ""

## Every `Key = Integer` retail authored in the block, verbatim, in retail's own
## spelling. Nothing is renamed and nothing is defaulted: a key retail did not
## write is ABSENT, and `preference()` reports absent as unweighted rather than
## as zero-with-confidence.
var values: Dictionary = {}

## Why the last `locate_and_load()` failed, empty on success.
var reason := ""


static func candidate_paths(roots: Array = []) -> Array[String]:
	var candidates: Array[String] = []
	var override := OS.get_environment(BUNDLE_ENV).strip_edges()
	if not override.is_empty():
		candidates.append(override)
	for root_value in roots:
		var root := String(root_value).strip_edges()
		if root.is_empty():
			continue
		# Beside the bundle, and inside a staged retail asset tree. Both, because
		# the converted bundles flatten their contents and the strategic-UI
		# bundle keeps retail's own `data/ini` layout.
		candidates.append(root.path_join(FILE_NAME))
		candidates.append(root.path_join(PACK_RELATIVE_DIRECTORY).path_join(FILE_NAME))
	return candidates


## Find and read retail's AI template. `{ok, path, reason}`; on failure `reason`
## names every path tried AND what the opponent loses, because "not found" alone
## sends nobody anywhere.
func locate_and_load(roots: Array = []) -> Dictionary:
	var tried: Array[String] = []
	for path in candidate_paths(roots):
		if tried.has(path):
			continue
		tried.append(path)
		if FileAccess.file_exists(path) and load_from(path):
			reason = ""
			return {"ok": true, "path": path, "reason": ""}
	loaded = false
	values = {}
	template_name = ""
	source_path = ""
	reason = (
		"NO retail AI template, so the opponent ranks regions by this project's "
		+ "own rules alone - retail's BonusPreference weights contribute nothing "
		+ "and the AI has no recorded taste in which region it wants. Looked at: %s. "
		+ "Retail ships this file as data/ini/%s; stage it with %s.") % [
			"; ".join(tried) if not tried.is_empty() else "nowhere",
			FILE_NAME, BUNDLE_ENV]
	return {"ok": false, "path": "", "reason": reason}


## Parse one `LivingWorldAITemplate <name> ... End` block. FAILS CLOSED: a body
## line that is not `Key = Integer`, a missing `End`, a second block, or a value
## that is not a whole number refuses the WHOLE file rather than loading a
## partial weight set - half a preference ordering ranks regions in an order
## neither retail nor this project chose.
func load_from(path: String) -> bool:
	loaded = false
	values = {}
	template_name = ""
	source_path = path
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null:
		return false
	if file.get_length() <= 0 or file.get_length() > MAX_BYTES:
		file.close()
		return false
	var text := file.get_as_text()
	file.close()

	var parsed: Dictionary = {}
	var name := ""
	var inside := false
	var closed := false
	for raw_line in text.split("\n"):
		var line := _strip_comment(String(raw_line))
		if line.is_empty():
			continue
		if not inside:
			if not line.begins_with(RETAIL_BLOCK_KEYWORD):
				# Anything before the block that is not a comment is a file this
				# parser does not understand.
				return false
			var rest := line.substr(RETAIL_BLOCK_KEYWORD.length()).strip_edges()
			if rest.is_empty():
				return false
			name = rest
			inside = true
			continue
		if line == RETAIL_BLOCK_END:
			inside = false
			closed = true
			continue
		if closed:
			# A second block. Retail ships exactly one; a file with two would make
			# "the default template" ambiguous, and choosing between them is not
			# something this project is going to do silently.
			return false
		var split := line.split("=", true, 1)
		if split.size() != 2:
			return false
		var key := String(split[0]).strip_edges()
		var value := String(split[1]).strip_edges()
		if key.is_empty() or not value.is_valid_int():
			return false
		if parsed.has(key):
			return false
		parsed[key] = int(value)
	if inside or not closed or parsed.is_empty():
		return false
	values = parsed
	template_name = name
	loaded = true
	return true


## Retail writes `;` comments and occasional `//` ones in the same files.
static func _strip_comment(line: String) -> String:
	var cut := line
	var semicolon := cut.find(";")
	if semicolon >= 0:
		cut = cut.substr(0, semicolon)
	var slashes := cut.find("//")
	if slashes >= 0:
		cut = cut.substr(0, slashes)
	return cut.strip_edges()


## Retail's weight for one `BonusPreference<X>`, or 0 when the template is not
## loaded or retail did not author that key. Callers that need to know WHICH of
## those two it was read `loaded` and `values`.
func preference(key: String) -> int:
	if not loaded:
		return 0
	return int(values.get(key, 0))


## Retail's authored weights this layer cannot spend yet, key -> the gap in
## `wotr_strategic_gaps.gd` that is why. Sorted.
func unspendable_weights() -> Dictionary:
	var found: Dictionary = {}
	if not loaded:
		return found
	var keys: Array[String] = []
	for key in values.keys():
		keys.append(String(key))
	keys.sort()
	for key in keys:
		for prefix in UNSPENDABLE_WEIGHT_PREFIXES.keys():
			if key.begins_with(String(prefix)):
				found[key] = String(UNSPENDABLE_WEIGHT_PREFIXES[prefix])
				break
	return found


# =============================================================================
# THE PROJECT-AUTHORED HALF
# =============================================================================
#
# Every constant below is this project's, not retail's. They are collected here
# rather than scattered through the code so that "what did OpenBFME invent?" is
# a list somebody can read, not an audit somebody has to perform.

## HOW MUCH ONE RETAIL PREFERENCE POINT IS WORTH against the project terms below.
## 1 means retail's own arithmetic is used unscaled: a region worth
## `legendary = 10` under `BonusPreferenceLegendary = 4` scores 40.
const PROJECT_RETAIL_BONUS_SCALE := 1

## What an UNDEFENDED region is worth on top of its bonuses. Retail records
## nothing about opportunism; this makes the opponent take free ground before it
## picks a fight, which is what stops it grinding on one border forever.
const PROJECT_UNDEFENDED_REGION_SCORE := 25

## What an enemy seat's CAPITAL is worth on top of its bonuses. The capital is
## real authoritative state (`players[i].capital`, retail's `StartRegion`, the
## region `loseIfCapitalLost` tests), so this is the one project term that aims
## the opponent at an authored victory condition rather than at scenery.
const PROJECT_ENEMY_CAPITAL_SCORE := 50

## THE ATTACK GATE, in thousandths. The opponent commits only when the command
## points it can stage are at least this multiple of the command points standing
## in the target. 1000 = attack at parity or better. Retail records no strength
## ratio anywhere; this number is chosen so the AI neither suicides into a
## fortress nor refuses every fight it could win.
const PROJECT_ATTACK_STRENGTH_RATIO_MILLI := 1000

## The most regions the opponent will march in one turn. A bound rather than a
## rule: without it a large empire's every idle army walks every turn, which is
## slow to simulate and unreadable on screen.
const PROJECT_MAX_MARCHES_PER_TURN := 3

## How far the opponent will look for a front line when nothing is in reach.
## Bounds the breadth-first search over its own territory.
const PROJECT_MAX_MARCH_SEARCH_DEPTH := 24

## The most structures the opponent will raise in one turn. A bound rather than
## a rule, for the same reason `PROJECT_MAX_MARCHES_PER_TURN` is one: retail's
## turn lets a seat build on every free plot it can afford, and a rich AI doing
## that in one frame is unreadable on screen and slow to simulate.
const PROJECT_MAX_BUILDS_PER_TURN := 2

## HOW MUCH TREASURE THE OPPONENT KEEPS BACK, in thousandths of the price of the
## thing it is about to buy. 0 means it will spend to the last coin. Retail
## records no reserve; this exists so the AI does not empty its purse on a farm
## the turn before it wants a fortress. It is stated rather than tuned silently.
const PROJECT_BUILD_TREASURY_RESERVE_MILLI := 0

## The single list a reader (or the screen) can print to see what this project
## decided that retail did not. Sorted by `project_authored_rules()`.
const PROJECT_AUTHORED_RULES := {
	"build_score_is_retails_building_score_weight":
		"WHICH structure to raise is retail's own BuildingScoreArmory/Barracks/"
		+ "Castle/Farm weight over retail's own four Type values; WHERE to raise "
		+ "it - the region, among every one the seat holds with a free plot - is "
		+ "this project's, broken by the same seeded rank a tie between two "
		+ "attack targets is, because retail records no siting rule",
	"builds_per_turn_bound":
		"the opponent raises at most PROJECT_MAX_BUILDS_PER_TURN structures a "
		+ "turn; retail bounds construction only by plots and treasury",
	"attack_strength_gate":
		"attack only when the staged command points are at least "
		+ "PROJECT_ATTACK_STRENGTH_RATIO_MILLI/1000 of the defender's; retail "
		+ "records no strength ratio",
	"undefended_region_bonus":
		"an undefended region scores PROJECT_UNDEFENDED_REGION_SCORE above its "
		+ "authored bonuses; retail records no opportunism rule",
	"enemy_capital_bonus":
		"an enemy seat's capital scores PROJECT_ENEMY_CAPITAL_SCORE above its "
		+ "authored bonuses; retail records no target priority",
	"march_toward_the_nearest_front":
		"an army in a region with no enemy neighbour marches one step along the "
		+ "shortest path through its own territory to a region that has one; "
		+ "retail records no strategic pathing",
	"one_attack_per_turn":
		"the opponent commits at most one attack, because the strategic layer "
		+ "ends the turn when a battle resolves; retail records no per-turn "
		+ "action budget",
	"seeded_tie_break":
		"targets that score equal are ordered by SHA-256 of the strategic state "
		+ "hash, so a tie is broken identically on every peer without an RNG",
}


static func project_authored_rules() -> PackedStringArray:
	var names: Array[String] = []
	for key in PROJECT_AUTHORED_RULES.keys():
		names.append(String(key))
	names.sort()
	return PackedStringArray(names)


# =============================================================================
# DETERMINISTIC TIE-BREAKING
# =============================================================================

## THE SEED, AND THE ONLY ONE. The SHA-256 of the strategic state hash: a value
## every peer already agrees on before the AI thinks, derived from authoritative
## state and nothing else. No clock, no engine RNG, no call ordering.
static func seed_for(state: StateScript) -> String:
	if state == null:
		return ""
	return _digest(state.state_hash())


## A stable unsigned rank for one name under one seed. Two names never collide
## into the same decision by accident, and the same (seed, name) always ranks the
## same on every machine - it is SHA-256, not `hash()`, whose value is an engine
## implementation detail.
static func tie_break_rank(seed_hex: String, name: String) -> int:
	var digest := _digest("%s|%s" % [seed_hex, name])
	var value := 0
	for index in range(8):
		value = (value << 4) | ("0123456789abcdef".find(digest[index]))
	return value


static func _digest(text: String) -> String:
	var context := HashingContext.new()
	context.start(HashingContext.HASH_SHA256)
	context.update(text.to_utf8_buffer())
	return context.finish().hex_encode()


# =============================================================================
# THE DECISION: WHICH REGION IS WORTH TAKING
# =============================================================================

## Score one candidate region for `player`, split into its two halves so a caller
## can print the retail term and the project terms separately.
##
## `{region, retail_score, project_score, score, defender, defender_cp,
##   attacker_cp, viable, reasons}`.
func score_target(
	session,
	player: int,
	from_region: String,
	target_region: String
) -> Dictionary:
	var world = session.world
	var state: StateScript = session.state
	var region: Dictionary = world.region(target_region)
	var bonuses: Dictionary = region.get("bonuses", {}) as Dictionary

	# --- RETAIL'S HALF: its own preference weights over the document's own
	# per-region bonuses. Iterated over the sorted binding table so the sum is
	# accumulated in one order on every machine.
	var retail_score := 0
	var reasons: Array[String] = []
	var preference_keys: Array[String] = []
	for key in BONUS_PREFERENCE_FIELDS.keys():
		preference_keys.append(String(key))
	preference_keys.sort()
	for key in preference_keys:
		var weight := preference(key)
		if weight == 0:
			continue
		var field := String(BONUS_PREFERENCE_FIELDS[key])
		var authored := int(bonuses.get(field, 0))
		if authored == 0:
			continue
		retail_score += weight * authored * PROJECT_RETAIL_BONUS_SCALE
		reasons.append("retail %s=%d over %s=%d" % [key, weight, field, authored])

	# --- THIS PROJECT'S HALF.
	var defender := state.owner_of(target_region)
	var defender_cp := 0
	if defender != StateScript.NEUTRAL:
		defender_cp = state.command_points_in_region(target_region, defender)
	var attacker_cp := state.command_points_in_region(from_region, player)

	var project_score := 0
	if defender_cp <= 0:
		project_score += PROJECT_UNDEFENDED_REGION_SCORE
		reasons.append("project undefended_region_bonus=%d" % PROJECT_UNDEFENDED_REGION_SCORE)
	if defender != StateScript.NEUTRAL and defender != player \
			and String((state.players[defender] as Dictionary).get("capital", "")) == target_region:
		project_score += PROJECT_ENEMY_CAPITAL_SCORE
		reasons.append("project enemy_capital_bonus=%d" % PROJECT_ENEMY_CAPITAL_SCORE)

	# THE GATE. Integer arithmetic throughout - a float comparison here would be
	# a place two platforms could disagree about whether a battle happened.
	var viable := attacker_cp * 1000 >= defender_cp * PROJECT_ATTACK_STRENGTH_RATIO_MILLI
	if not viable:
		reasons.append("project attack_strength_gate refused %d cp against %d cp" % [
			attacker_cp, defender_cp])

	return {
		"region": target_region,
		"from": from_region,
		"retail_score": retail_score,
		"project_score": project_score,
		"score": retail_score + project_score,
		"defender": defender,
		"defender_cp": defender_cp,
		"attacker_cp": attacker_cp,
		"viable": viable,
		"reasons": PackedStringArray(reasons),
	}


## Every attack the active seat could legally make this turn, best first.
##
## THE CANDIDATE SET IS THE SCREEN'S OWN. `staging_regions()` and
## `attack_targets()` are exactly the doors the human's clicks go through, so the
## AI cannot see - let alone take - a move the rules would refuse a human. If
## those functions get stricter, the AI gets stricter with them, for free.
func rank_targets(session) -> Array[Dictionary]:
	var ranked: Array[Dictionary] = []
	if session == null or session.state == null or session.world == null:
		return ranked
	var state: StateScript = session.state
	var player := state.active_player()
	if player == StateScript.NEUTRAL:
		return ranked
	var seed_hex := seed_for(state)
	# One row per TARGET, not per (staging, target) pair: `wotr_handoff.gd`
	# chooses the staging region itself (lowest-sorted adjacent owned region with
	# an army), so a second opinion here would be scored and then discarded.
	var seen: Dictionary = {}
	for from_region in session.staging_regions():
		for target in session.attack_targets(from_region):
			if seen.has(target):
				continue
			seen[target] = true
			var row := score_target(session, player, from_region, target)
			row["tie_break"] = tie_break_rank(seed_hex, String(target))
			ranked.append(row)
	ranked.sort_custom(_target_is_better)
	return ranked


## Best first: higher score wins; equal scores are ordered by the seeded rank,
## and an exact rank collision falls back to the region id so the sort is TOTAL
## (an unstable comparator is a source of platform divergence all on its own).
static func _target_is_better(a: Dictionary, b: Dictionary) -> bool:
	var left := int(a.get("score", 0))
	var right := int(b.get("score", 0))
	if left != right:
		return left > right
	var left_rank := int(a.get("tie_break", 0))
	var right_rank := int(b.get("tie_break", 0))
	if left_rank != right_rank:
		return left_rank < right_rank
	return String(a.get("region", "")) < String(b.get("region", ""))


# =============================================================================
# THE DECISION: WHERE TO MARCH WHEN THERE IS NOTHING TO FIGHT
# =============================================================================

## Regions `player` owns that touch a region they do not - their front line.
static func frontier_regions(world, state: StateScript, player: int) -> PackedStringArray:
	var frontier: Array[String] = []
	for region_id in state.regions_owned_by(player):
		for neighbour in world.neighbours(region_id):
			if state.owner_of(neighbour) != player:
				frontier.append(region_id)
				break
	frontier.sort()
	return PackedStringArray(frontier)


## The marches the opponent will make this turn: `[{from, to, distance}]`, at
## most `PROJECT_MAX_MARCHES_PER_TURN`, computed ENTIRELY from the pre-march
## board so no march can cascade into another and change the plan halfway.
##
## Retail seats each side deep inside its own territory, so on turn one an AI
## that only knows how to attack does nothing forever. This is the rule that
## makes a campaign reach a front line at all, and it is this project's: a
## breadth-first search over OWN regions only, sorted at every step, from each
## idle interior region to the nearest frontier region.
func plan_marches(session) -> Array[Dictionary]:
	var plan: Array[Dictionary] = []
	if session == null or session.state == null or session.world == null:
		return plan
	var world = session.world
	var state: StateScript = session.state
	var player := state.active_player()
	if player == StateScript.NEUTRAL:
		return plan
	var frontier: Dictionary = {}
	for region_id in frontier_regions(world, state, player):
		frontier[region_id] = true
	if frontier.is_empty():
		# Nothing this seat owns touches anything it does not. There is no front
		# line to march to, so there is nothing to plan - not a fallback, a fact.
		return plan
	for region_id in state.regions_owned_by(player):
		if frontier.has(region_id):
			continue
		var has_army := false
		for army_id in state.armies_in_region(region_id):
			if int((state.armies[army_id] as Dictionary).get("owner", StateScript.NEUTRAL)) == player:
				has_army = true
				break
		if not has_army:
			continue
		var step := _first_step_toward_frontier(world, state, player, region_id, frontier)
		if step.is_empty():
			continue
		plan.append(step)
	# Nearest front first, then region id: a total order that does not depend on
	# dictionary iteration.
	plan.sort_custom(_march_is_sooner)
	if plan.size() > PROJECT_MAX_MARCHES_PER_TURN:
		plan.resize(PROJECT_MAX_MARCHES_PER_TURN)
	return plan


static func _march_is_sooner(a: Dictionary, b: Dictionary) -> bool:
	var left := int(a.get("distance", 0))
	var right := int(b.get("distance", 0))
	if left != right:
		return left < right
	return String(a.get("from", "")) < String(b.get("from", ""))


## Breadth-first over regions `player` owns. Returns `{from, to, distance}` for
## the first step of the shortest path to any frontier region, or `{}` when no
## frontier is reachable through own territory (a detached pocket - real, and
## reported by simply not marching).
static func _first_step_toward_frontier(
	world,
	state: StateScript,
	player: int,
	start: String,
	frontier: Dictionary
) -> Dictionary:
	# `queue` holds [region, first_step, distance]. Neighbours are already sorted
	# by `wotr_world.gd`, so the frontier reached first is the same one on every
	# machine even when two are equidistant.
	var queue: Array = []
	var seen: Dictionary = {start: true}
	for neighbour in world.neighbours(start):
		if state.owner_of(neighbour) != player or seen.has(neighbour):
			continue
		seen[neighbour] = true
		queue.append([neighbour, neighbour, 1])
	var head := 0
	while head < queue.size():
		var entry: Array = queue[head]
		head += 1
		var region_id := String(entry[0])
		var first_step := String(entry[1])
		var distance := int(entry[2])
		if frontier.has(region_id):
			return {"from": start, "to": first_step, "distance": distance}
		if distance >= PROJECT_MAX_MARCH_SEARCH_DEPTH:
			continue
		for neighbour in world.neighbours(region_id):
			if state.owner_of(neighbour) != player or seen.has(neighbour):
				continue
			seen[neighbour] = true
			queue.append([neighbour, first_step, distance + 1])
	return {}


# =============================================================================
# THE DECISION: WHAT TO BUILD
# =============================================================================

## Retail's `BuildingScore<X>` weight for one of retail's four `Type` values, or
## 0 when the template is not loaded or retail did not author that key.
##
## The four bindings live in `wotr_buildings.gd` (`AI_SCORE_KEY_TYPES`) beside
## the argument for the two that are not the same word twice: `Castle` is
## retail's own name for a Fortress (its region strongholds are named
## `LW:DisplayName<X>Castle`, and there is no fifth Type for the weight to mean),
## and `Farm` is retail's own name for a `Resource` building (every one of them
## costs `WOTR_FARM_COST` and every one of their tooltips says "Structure Type:
## Farm").
func building_score(type_name: String) -> int:
	if not loaded:
		return 0
	for key in BuildingsScript.AI_SCORE_KEY_TYPES.keys():
		if String(BuildingsScript.AI_SCORE_KEY_TYPES[key]) == type_name:
			return int(values.get(String(key), 0))
	return 0


## EVERY STRUCTURE THE OPPONENT COULD RAISE THIS TURN, best first.
##
## `[{region, building, type, cost, score, retail_score, tie_break, reasons}]`.
##
## THE CANDIDATE SET IS THE SCREEN'S OWN, exactly as `rank_targets()`'s is: it is
## built from `session.build_options()`, which is the same function the build
## ring draws its buttons from and which routes every legality question through
## `wotr_state.build_refusal()`. The AI cannot raise a structure the rules would
## refuse a human, and if those rules get stricter it gets stricter with them.
##
## THE SCORE IS RETAIL'S ALONE. There is no project term added to it - the only
## project decisions here are WHICH REGION (retail records no siting rule, so the
## seeded rank breaks it) and HOW MANY (the per-turn bound above).
func rank_builds(session) -> Array[Dictionary]:
	var ranked: Array[Dictionary] = []
	if session == null or session.state == null or session.world == null:
		return ranked
	var state: StateScript = session.state
	var player := state.active_player()
	if player == StateScript.NEUTRAL:
		return ranked
	var seed_hex := seed_for(state)
	for region_id in state.regions_owned_by(player):
		if state.plot_count(region_id) <= 0:
			continue
		for row in session.build_options(region_id):
			if not bool(row.get("can_build", false)):
				continue
			var type_name := String(row.get("type", ""))
			var weight := building_score(type_name)
			var reasons: Array[String] = []
			if weight > 0:
				reasons.append("retail BuildingScore for %s = %d" % [type_name, weight])
			else:
				# A zero is REPORTED rather than hidden: it means either that the
				# template is not loaded (so the AI has no retail taste at all) or
				# that retail weighted this class at zero, and the two are different
				# facts a reader is entitled to tell apart.
				reasons.append("no retail BuildingScore for %s (%s)" % [
					type_name,
					"template not loaded" if not loaded else "retail authored 0"])
			ranked.append({
				"region": region_id,
				"building": String(row.get("building", "")),
				"type": type_name,
				"cost": int(row.get("cost", 0)),
				"income_per_turn": int(row.get("income_per_turn", 0)),
				"retail_score": weight,
				"score": weight,
				"tie_break": tie_break_rank(seed_hex, "%s|%s" % [region_id, String(row.get("building", ""))]),
				"reasons": PackedStringArray(reasons),
			})
	ranked.sort_custom(_build_is_better)
	return ranked


## Best first: higher retail weight wins, then the seeded rank, then the region
## and building ids so the order is TOTAL. An unstable comparator here would put
## a different structure on a plot on two machines from the same state, which is
## a strategic-hash divergence with no other symptom.
static func _build_is_better(a: Dictionary, b: Dictionary) -> bool:
	var left := int(a.get("score", 0))
	var right := int(b.get("score", 0))
	if left != right:
		return left > right
	var left_rank := int(a.get("tie_break", 0))
	var right_rank := int(b.get("tie_break", 0))
	if left_rank != right_rank:
		return left_rank < right_rank
	if String(a.get("region", "")) != String(b.get("region", "")):
		return String(a.get("region", "")) < String(b.get("region", ""))
	return String(a.get("building", "")) < String(b.get("building", ""))


## RAISE WHAT THE OPPONENT CAN AFFORD, best first, through the same session door
## the build ring uses. Returns one row per structure actually raised.
##
## RE-RANKED AFTER EVERY BUILD, deliberately. A build spends treasure and fills a
## plot, so the second-best candidate from the pre-build list may no longer be
## legal or affordable; carrying a stale plan forward would mean committing
## builds that `commit_build()` then refuses, and reporting a turn full of
## refusals the player never caused.
func take_builds(session) -> Array[Dictionary]:
	var raised: Array[Dictionary] = []
	if session == null or session.state == null:
		return raised
	for _step in range(PROJECT_MAX_BUILDS_PER_TURN):
		var ranked := rank_builds(session)
		if ranked.is_empty():
			break
		var choice := ranked[0]
		var built: Dictionary = session.commit_build(
			String(choice.get("region", "")), String(choice.get("building", "")))
		if not bool(built.get("ok", false)):
			break
		raised.append({
			"region": String(choice.get("region", "")),
			"building": String(built.get("building", "")),
			"type": String(built.get("type", "")),
			"plot": int(built.get("plot", -1)),
			"cost": int(built.get("cost", 0)),
			"treasury_after": int(built.get("treasury_after", 0)),
			"retail_score": int(choice.get("retail_score", 0)),
			"income_per_turn": int(choice.get("income_per_turn", 0)),
			"reasons": choice.get("reasons", PackedStringArray()),
		})
	return raised


# =============================================================================
# THE TURN
# =============================================================================

## Whether the seat whose turn it is should be played by this file.
static func active_seat_is_ai(state: StateScript) -> bool:
	if state == null:
		return false
	var player := state.active_player()
	if player == StateScript.NEUTRAL or player < 0 or player >= state.players.size():
		return false
	var seat := state.players[player] as Dictionary
	if bool(seat.get("defeated", false)):
		return false
	return String(seat.get("controller", "")) == StateScript.CONTROLLER_AI


## TAKE THE ACTIVE SEAT'S TURN, and end it.
##
## `available_map_ids` is the caller's list of maps the tactical layer can boot -
## the same argument the screen passes to `commit_attack()`. An AI attack needs a
## battlefield binding exactly as a human's does; with an empty list every attack
## refuses by name and the opponent marches instead of fighting, which is a
## degraded turn that SAYS it is degraded rather than a silent no-op.
##
## The returned report is the contract the strategic screen reads to show that
## something happened. It is PRESENTATION DATA ONLY - it enters no hash, rides no
## snapshot, and nothing in authoritative state depends on a caller reading it.
##
## `{ok, seat, acted, turn_index_before, turn_index_after, hash_before,
##   hash_after, seed, marches, attack, narrative, refusals, provenance}`
func take_turn(session, available_map_ids: Array = []) -> Dictionary:
	var report := _empty_report()
	if session == null or session.state == null or session.world == null:
		report["refusals"].append("no War of the Ring session is running")
		return report
	var state: StateScript = session.state
	if state.phase != StateScript.PHASE_TACTICAL:
		report["refusals"].append("the AI only starts a round in the tactical phase")
		return report
	if not state.pending_battle.is_empty():
		report["refusals"].append(
			"a battle is already in flight in %s; the AI does not open a second one"
			% String(state.pending_battle.get("region", "")))
		return report
	if not active_seat_is_ai(state):
		report["refusals"].append(
			"seat %d is not an undefeated AI seat, so this file must not move for it"
			% state.active_player())
		return report

	var seat := state.active_player()
	report["seat"] = seat
	report["turn_index_before"] = state.turn_index
	report["hash_before"] = state.state_hash()
	report["seed"] = seed_for(state)
	report["provenance"] = decision_provenance()

	# ---- 0. BUILD, before anything else -------------------------------------
	# FIRST because raising a structure does not spend the turn (retail puts
	# construction, training and movement in one phase - see `build_structure()`
	# in `wotr_state.gd` for the shipped sentence), and because a battle that
	# resolves ENDS the turn inside the session, so a build attempted afterwards
	# would be spending the next seat's turn.
	report["builds"] = take_builds(session)
	if not (report["builds"] as Array).is_empty():
		report["acted"] = true

	# ---- 1. FIGHT, if there is a fight worth having ------------------------
	var ranked := rank_targets(session)
	report["considered"] = ranked.size()
	for row in ranked:
		if not bool(row.get("viable", false)):
			# Ranked list is best-first by SCORE, not by viability, so a
			# non-viable best target must not stop the search: the next one down
			# may be a free region.
			continue
		var target := String(row.get("region", ""))
		var committed: Dictionary = session.commit_attack(
			target, available_map_ids, StateScript.BATTLE_TYPE_AUTO_RESOLVE)
		if not bool(committed.get("ok", false)):
			for refusal in committed.get("refusals", PackedStringArray()) as PackedStringArray:
				report["refusals"].append("attack on %s refused: %s" % [target, String(refusal)])
			# A refused commitment leaves the board untouched, so the next target
			# is still legal. Try it rather than ending a turn on a configuration
			# problem the player never caused.
			continue
		var resolved: Dictionary = session.auto_resolve_pending_battle()
		if not bool(resolved.get("ok", false)):
			for refusal in resolved.get("refusals", PackedStringArray()) as PackedStringArray:
				report["refusals"].append("battle in %s refused: %s" % [target, String(refusal)])
			# The commitment was admitted and the resolver would not decide it -
			# an army whose roster the auto-resolve bindings do not cover, most
			# often. Abandon it explicitly, because leaving a battle in flight
			# would freeze the campaign on the next human turn, and then try the
			# next target: `abandon_battle()` applies no result, so the board is
			# exactly as it was and every remaining target is still legal.
			session.abandon_battle()
			continue
		var applied := resolved.get("applied", {}) as Dictionary
		var undecided := bool(applied.get("undecided", true))
		report["attack"] = {
			"region": target,
			"from": String(row.get("from", "")),
			"score": int(row.get("score", 0)),
			"retail_score": int(row.get("retail_score", 0)),
			"project_score": int(row.get("project_score", 0)),
			"reasons": row.get("reasons", PackedStringArray()),
			"seed": String(resolved.get("seed", "")),
			"undecided": undecided,
			"winner_player": int(applied.get("winner_player", StateScript.NEUTRAL)),
			"captured": int(applied.get("winner_player", StateScript.NEUTRAL)) == seat and not undecided,
			"armies_lost": applied.get("armies_lost", PackedInt32Array()),
			"armies_reduced": applied.get("armies_reduced", PackedInt32Array()),
		}
		report["acted"] = true
		break

	# ---- 2. MARCH, when no battle was fought -------------------------------
	# A decided battle ALREADY advanced the turn (the session does that, not this
	# file), and it also moved the winning armies. Marching after it would be
	# moving on somebody else's turn.
	var battle_decided := not (report["attack"] as Dictionary).is_empty() \
		and not bool((report["attack"] as Dictionary).get("undecided", true))
	if not battle_decided:
		for step in plan_marches(session):
			var from_region := String(step.get("from", ""))
			var to_region := String(step.get("to", ""))
			var marched: Dictionary = session.move_armies(from_region, to_region)
			if bool(marched.get("ok", false)):
				report["marches"].append({
					"from": from_region,
					"to": to_region,
					"distance": int(step.get("distance", 0)),
					"armies": marched.get("moved", PackedInt32Array()),
				})
				report["acted"] = true
			else:
				for refusal in marched.get("refusals", PackedStringArray()) as PackedStringArray:
					report["refusals"].append(String(refusal))
		# ---- 3. END THE TURN ------------------------------------------------
		# Only here. An undecided auto-resolve deliberately does NOT advance the
		# turn (the session says why), but an AI turn that did not advance would
		# hand control to nobody and hang the campaign, so the opponent ends its
		# own turn exactly once, through the same door the END TURN button uses.
		session.end_phase()

	report["ok"] = true
	report["turn_index_after"] = state.turn_index
	report["hash_after"] = state.state_hash()
	report["narrative"] = narrate(session, report)
	return report


func _empty_report() -> Dictionary:
	return {
		"ok": false,
		"seat": StateScript.NEUTRAL,
		"acted": false,
		"considered": 0,
		"turn_index_before": -1,
		"turn_index_after": -1,
		"hash_before": "",
		"hash_after": "",
		"seed": "",
		"builds": [],
		"marches": [],
		"attack": {},
		"narrative": PackedStringArray(),
		"refusals": PackedStringArray(),
		"provenance": {},
	}


## WHERE EVERY DECISION CAME FROM, for the screen and for anyone reading a log.
## The point of this dictionary is that a player can never be shown an AI move
## and be left guessing whether retail chose it or this project did.
func decision_provenance() -> Dictionary:
	var bound: Dictionary = {}
	var keys: Array[String] = []
	for key in BONUS_PREFERENCE_FIELDS.keys():
		keys.append(String(key))
	keys.sort()
	for key in keys:
		bound[key] = {
			"field": String(BONUS_PREFERENCE_FIELDS[key]),
			"weight": preference(key),
		}
	return {
		"retail_template_loaded": loaded,
		"retail_template_path": source_path,
		"retail_template_name": template_name,
		"retail_template_reason": reason,
		"retail_weights_bound": bound,
		"retail_weights_unbound": BONUS_PREFERENCE_UNBOUND.duplicate(true),
		"retail_weights_unspendable": unspendable_weights(),
		# RETAIL'S BUILDING WEIGHTS, and the Type each one now buys. Reported beside
		# the region preferences because they are the same kind of fact: a number
		# retail wrote, spent on a decision a player can see the result of.
		"retail_building_scores": building_score_provenance(),
		"project_authored_rules": PROJECT_AUTHORED_RULES.duplicate(true),
	}


## Retail's four `BuildingScore*` keys -> `{type, weight}`, sorted.
func building_score_provenance() -> Dictionary:
	var bound: Dictionary = {}
	var keys: Array[String] = []
	for key in BuildingsScript.AI_SCORE_KEY_TYPES.keys():
		keys.append(String(key))
	keys.sort()
	for key in keys:
		bound[key] = {
			"type": String(BuildingsScript.AI_SCORE_KEY_TYPES[key]),
			"weight": preference(key),
		}
	return bound


## THE OPPONENT'S TURN IN THE PLAYER'S OWN WORDS, so the strategic layer shows
## that something happened instead of silently jumping back to the human. Region
## ids are passed through `session`'s own display names where it has them, so the
## line reads "Angmar marched into Carn Dum", not an id out of a data file.
func narrate(session, report: Dictionary) -> PackedStringArray:
	var lines: Array[String] = []
	var state: StateScript = session.state
	var seat := int(report.get("seat", StateScript.NEUTRAL))
	var who := _seat_name(state, seat)
	for row in report.get("builds", []) as Array:
		var build := row as Dictionary
		# The building id, not a name: retail's names are string-table keys and the
		# screen owns the table, exactly as it does for region names below.
		lines.append("%s raised %s in %s for %d." % [
			who, String(build.get("building", "")),
			_region_name(session, String(build.get("region", ""))),
			int(build.get("cost", 0))])
	for step in report.get("marches", []) as Array:
		lines.append("%s marched from %s to %s." % [
			who,
			_region_name(session, String((step as Dictionary).get("from", ""))),
			_region_name(session, String((step as Dictionary).get("to", ""))),
		])
	var attack := report.get("attack", {}) as Dictionary
	if not attack.is_empty():
		var where := _region_name(session, String(attack.get("region", "")))
		if bool(attack.get("undecided", false)):
			lines.append("%s attacked %s and neither side broke." % [who, where])
		elif bool(attack.get("captured", false)):
			lines.append("%s took %s." % [who, where])
		else:
			lines.append("%s attacked %s and was thrown back." % [who, where])
	if lines.is_empty():
		# NEVER SILENT. A turn where the opponent did nothing is a real outcome
		# and the player is told, because an unexplained instant turn reads as a
		# bug and is exactly what this whole file was written to end.
		if not (report.get("refusals", PackedStringArray()) as PackedStringArray).is_empty():
			lines.append("%s could not move: %s" % [
				who, String((report["refusals"] as PackedStringArray)[0])])
		else:
			lines.append("%s held its ground." % who)
	return PackedStringArray(lines)


static func _seat_name(state: StateScript, seat: int) -> String:
	if state == null or seat < 0 or seat >= state.players.size():
		return "The enemy"
	var template := String((state.players[seat] as Dictionary).get("template", ""))
	# `PlayerAngmar` -> `Angmar`. The strategic screen owns the retail string
	# table and renders the proper name; this is the fallback for logs and tests,
	# and it is a TRIM of retail's own key, not an invented name.
	if template.begins_with("Player"):
		return template.substr("Player".length())
	return template if not template.is_empty() else "Seat %d" % seat


static func _region_name(session, region_id: String) -> String:
	if session == null or session.world == null or region_id.is_empty():
		return region_id
	var display := String(session.world.region(region_id).get("display_name", ""))
	# Retail's display names are string-table KEYS ("LW:DisplayNameAmonSul").
	# Resolving them is the screen's job (it owns the string table); an
	# unresolved key would read worse than the id, so the id is used.
	if display.is_empty() or display.contains(":"):
		return region_id
	return display
