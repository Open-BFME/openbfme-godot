extends RefCounted

## ============================================================================
## THE AUTHORED BINDINGS TABLE FOR THE WAR OF THE RING GAME SETUP SCREEN.
##
## EVERYTHING IN THIS FILE IS AUTHORED. NONE OF IT IS CONVERTED.
##
## READ THAT AGAIN BEFORE TRUSTING A LINE OF IT. Every other table in this
## lane - the string bundle, the living-world document, the region geometry,
## the atlas crops - is produced by a converter from a retail file, and can be
## regenerated and diffed. This one cannot, because the fact it records IS NOT
## IN ANY SHIPPED FILE.
##
## WHY. Retail's `data/ini` carries the LABELS (`RULE:` x12) and the VALUES
## (`VALUE:` x24) as string-table keys, and it carries the victory types, the
## handicap levels and the colours as INI blocks. It does NOT carry the thing
## that turns those into a screen: WHICH values populate WHICH row, in WHICH
## order, and WHICH one is selected when the screen opens. There is no
## `mprules.ini`. That table is compiled into `game.dat`, and retail's own
## `LivingWorldVictoryTypes.inc` says so out loud:
##
##     // VERY,VERY, VERY important since the MPRules uses hardcoded values
##     // the order that these load in must match them!!!!
##
## So the bindings have to be WRITTEN DOWN BY A PERSON. They are written down
## HERE, once, in one table, rather than scattered through the UI code where
## nobody would find them to check them. Each row carries `evidence`, which is
## the strongest thing that can be said for it:
##
##   "data"        - transcribed from a shipped retail file, named in `source`.
##                   Checkable against that file, line by line.
##   "screenshot"  - read off a retail screenshot of this screen.
##   "authored"    - a judgement call by this project, with the reasoning in
##                   the comment above it. NOT a claim about retail.
##
## AND THE ABSENCES ARE HERE TOO. A row whose options are not in any shipped
## file carries them as an EMPTY list plus `absent_reason`, and the screen
## draws it unavailable and says why. It never gets invented options. The
## timer row is the clearest case: retail's `VALUE:NoTimer` resolves, and the
## numeric second counts beside it exist only in the executable, so this table
## offers "No Timer" and NOTHING ELSE rather than guessing at 30/60/90.
##
## WHAT REACHES THE SIMULATION, AND WHAT DOES NOT. `reaches` on each row is the
## honest answer to "does choosing this change anything":
##
##   "strategic"   - the choice is an argument to `WotrSession.begin()`, so it
##                   lands in the strategic state and rides the hash.
##   "presentation"- per seat, never hashed, never handed to the simulation.
##   ""            - NOTHING CARRIES IT YET. The strategic layer has no field
##                   for it, and adding one as an extra argument to a battle
##                   is precisely the desync this branch has fixed twice. Such
##                   a row is drawn LOCKED with its reason on screen, showing
##                   retail's default, rather than pretending to be a control.
## ============================================================================


# --- the screen's own chrome, key by key --------------------------------------
#
# WHICH string key goes on WHICH element. AUTHORED: retail makes this binding
# inside `LivingWorldUI.apt`, 799 KB of Flash this project does not read. The
# keys themselves are retail's and resolve through the setup string bundle;
# only the assignment of key to element is this project's.
#
# Note two traps that make a "derive it from the name" rule impossible:
#   * the Army column header is `APT:Side` - retail's key says Side, its text
#     says "Army";
#   * the Scenario label is `Apt:Scenario` with a lowercase tail, and
#     `APT:Scenario` does not exist at all.
const SHELL_KEYS := {
	"title": "APT:WarOfTheRing",              # "WAR OF THE RING"
	"heading": "APT:GameSetup",               # "GAME SETUP"
	"tab_map": "APT:TabMap",                  # "MAP"
	"tab_rules": "APT:TabRules",              # "RULES"
	"scenario": "Apt:Scenario",               # "Scenario"
	"scenario_description": "APT:ScenarioDescription",
	"territory_description": "APT:TerritoryDescription",
	"column_player": "APT:Player",            # "Player"
	"column_army": "APT:Side",                # "Army"
	"column_hero": "APT:HeaderHero",          # "Hero"
	"column_team": "APT:HeaderTeam",          # "Team"
	"column_color": "APT:Color",              # "Color"
	"column_handicap": "APT:HdrHandicap",     # "Handicap"
	"button_main_menu": "APT:MainMenu",       # "MAIN MENU"
	"button_profile": "APT:Profile",          # "PROFILE"
	"button_play": "APT:StartGame",           # "PLAY"
	"button_reset": "APT:Reset",              # "RESET"
}


# --- the RULES tab, row by row -------------------------------------------------
#
# Retail's RULES tab carries FIVE dropdowns and TWO checkboxes. Retail's table
# carries TWELVE `RULE:` keys. The other seven belong to the skirmish and
# network lobbies, not here - `UNUSED_RULE_KEYS` below names every one of them
# and why, so "the screen is missing a row" and "retail does not put that row
# here" can be told apart.
#
# `values` are `VALUE:` keys in the order this screen offers them.
# `default` is an index into `values`, or -1 when the row has no options.
const RULE_ROWS: Array[Dictionary] = [
	{
		"id": "tactical_phase_timer",
		"label_key": "RULE:StrategicPhaseTimer",   # text: "Tactical Phase Timer"
		"tooltip_key": "ToolTip:WaroftheRing_Rules_TacticalPhaseTimer",
		"values": ["VALUE:NoTimer"],
		"default": 0,
		"evidence": "data",
		"source": "lotr.str RULE:StrategicPhaseTimer / VALUE:NoTimer",
		"reaches": "",
		"locked_reason": "the strategic layer has no turn timer, so a choice here would reach nothing",
		# THE ABSENCE, NAMED. Retail also offers numeric second counts on this
		# row. `VALUE:Seconds` is the format string "%d seconds" and `VALUE:Value`
		# is "%d" - the NUMBERS THEY ARE FILLED WITH ARE IN NO SHIPPED FILE. They
		# are not guessed at here.
		"absent_reason": "retail's numeric second counts are hardcoded in the executable; only VALUE:NoTimer is in a shipped file",
	},
	{
		"id": "battle_type",
		"label_key": "RULE:BattleType",            # text: "Battle Type"
		"tooltip_key": "ToolTip:WaroftheRing_Rules_BattleType",
		"values": ["VALUE:AutoResolveAndRTS", "VALUE:AutoResolve", "VALUE:RTS"],
		"default": 0,                              # "Auto Resolve and RTS"
		"evidence": "screenshot",
		"source": "retail GAME SETUP screenshot shows 'Auto Resolve and RTS'",
		# UNLOCKED. It was locked because no auto-resolve path existed; one does
		# now. The value is an argument to `WotrSession.begin()`, lands on
		# `state.battle_type`, is inside `authoritative_state()` and therefore
		# inside the hash, and decides which of the two resolution paths a battle
		# takes. "Auto Resolve and RTS" - retail's own default - means BOTH are
		# offered, so the strategic screen shows both buttons and the choice is
		# recorded per battle in the commitment's `battle_type`.
		"reaches": "strategic",
		"state_field": "battle_type",
		"values_state": ["auto_resolve_and_rts", "auto_resolve", "rts"],
		"locked_reason": "",
		"absent_reason": "",
	},
	{
		"id": "battle_type_priority",
		"label_key": "RULE:BattleChoice",          # text: "Battle Type Priority"
		"tooltip_key": "ToolTip:WaroftheRing_Rules_BattleTypePriority",
		# Retail's own tooltip states the row's meaning exactly: it decides
		# "whether a battle will be decided through real-time or auto-resolve if
		# players choose differently". Two outcomes, two VALUE: keys.
		"values": ["VALUE:RTS", "VALUE:AutoResolve"],
		"default": 1,
		"evidence": "screenshot",
		"source": "option set follows ToolTip:WaroftheRing_Rules_BattleTypePriority; the DEFAULT is read off the owner's own retail RULES tab, which shows \"Auto Resolve*\" - it was authored as RTS and corrected",
		# UNLOCKED. There is now something for it to arbitrate between. It lands
		# on `state.battle_type_priority`, rides the hash, and is recorded in
		# every commitment beside the resolved type it produced.
		"reaches": "strategic",
		"state_field": "battle_type_priority",
		"values_state": ["rts", "auto_resolve"],
		"locked_reason": "",
		"absent_reason": "",
	},
	{
		"id": "auto_resolve_display",
		"label_key": "RULE:AutoResolveType",       # text: "Auto-Resolve Display"
		"tooltip_key": "ToolTip:WaroftheRing_Rules_AutoResolveDisplay",
		"values": ["VALUE:Dynamic", "VALUE:Quick"],
		"default": 0,                              # "Dynamic"
		"evidence": "screenshot",
		"source": "retail GAME SETUP screenshot shows 'Dynamic'",
		# STILL LOCKED, but the reason it used to give has stopped being true, so
		# it has been replaced rather than left standing. Battles DO auto-resolve
		# now; what does not exist is retail's animated presentation of one. Open
		# BFME shows the working as a readable report instead, so "Dynamic" and
		# "Quick" would render exactly the same screen.
		"reaches": "",
		"locked_reason": "battles do auto-resolve now, but the result is shown as a written working rather than retail's animation, so Dynamic and Quick would draw the same screen",
		"absent_reason": "",
	},
	{
		"id": "victory_type",
		"label_key": "RULE:VictoryType",           # text: "Victory Type"
		"tooltip_key": "",                         # retail authored none
		# Filled at runtime from VICTORY_TYPES below plus the document's own
		# `victoryTypes` block, because whether these exist at all depends on
		# which edition's document is loaded. See `victory_options()`.
		"values": [],
		"default": 0,
		"evidence": "data",
		"source": "campaigns/common/LivingWorldVictoryTypes.inc, in file order",
		"reaches": "",
		"locked_reason": "the strategic layer decides a campaign by elimination only; no other victory condition is implemented",
		"absent_reason": "",
	},
]

## The two checkboxes, in retail's order. Both labels are real `RULE:` keys.
## Neither has a `VALUE:` pair - retail's table carries no Yes/No in that
## namespace - so the box is drawn as a box, which is what retail draws.
const RULE_CHECKBOXES: Array[Dictionary] = [
	{
		"id": "allow_custom_heroes",
		"label_key": "RULE:AllowCustomHeroes",     # text: "Allow Custom Heroes"
		"default": true,
		"evidence": "screenshot",
		"source": "read off the owner's own retail RULES tab, which shows this ticked - it was authored as false and corrected. Create-A-Hero is not implemented in Open BFME, which is why the row stays LOCKED even though the default is now right",
		"reaches": "",
		"locked_reason": "Create-A-Hero is not implemented, so there are no custom heroes to allow",
	},
	{
		"id": "allow_ring_heroes",
		"label_key": "RULE:AllowRingHeroes",       # text: "Allow Ring Heroes"
		"default": true,
		"evidence": "screenshot",
		"source": "read off the owner's own retail RULES tab, which shows this ticked - it was authored as false and corrected",
		"reaches": "",
		"locked_reason": "no ring-hero rule reaches the strategic layer or the commitment",
	},
]

## The seven `RULE:` keys retail's table carries that this screen does NOT show,
## and why each one is somewhere else. Here so a reviewer can check the screen
## against the table and find twelve accounted for rather than five.
const UNUSED_RULE_KEYS := {
	"RULE:CommandPointFactor": "skirmish and network lobby rule, not on the War of the Ring rules tab",
	"RULE:ClanGame": "online clan-match flag; Open BFME has no online service",
	"RULE:InitialResources": "skirmish and network lobby rule",
	"RULE:StrategicDialogTimer": "text reads 'Dialog Timer'; retail does not place it on this tab",
	"RULE:MapRevealMode": "text reads 'Map Shroud Settings'; a skirmish rule",
	"RULE:AllowCustomHeroes": "shown here, as a checkbox",
	"RULE:AllowRingHeroes": "shown here, as a checkbox",
}


# --- the victory types ---------------------------------------------------------
#
# TRANSCRIBED, in file order, from RotWK's
# `data/ini/campaigns/common/LivingWorldVictoryTypes.inc`. THE ORDER IS
# LOAD-BEARING and retail says so in the file's own header comment (quoted at
# the top of this file), because the compiled rules table indexes into it.
#
# `label_key` is the block's own `DisplayGameType`. Four of the seven blocks
# share `LWScenario:WOTRGameType003` ("World Domination") - retail's own comment
# on them is "Oops, we need a unique game type if we use DisplayGameType for
# lookup" - so those four carry `qualifier_key`, the `VALUE:WorldDom*` key that
# tells them apart, and the screen shows the qualifier because the display name
# alone would put the same word in the list four times.
#
# THIS FILE IS ROTWK'S. BFME2 does not ship it, and the BFME2 living-world
# document's `victoryTypes` block is empty as a result. `victory_options()`
# reports that rather than offering RotWK's list over a BFME2 campaign.
const VICTORY_TYPES: Array[Dictionary] = [
	{"index": 0, "label_key": "LWScenario:WOTRGameType002", "qualifier_key": "",
		"objectives_key": "LWScenario:WOTRObjectives002", "regions_to_win": 0},
	{"index": 1, "label_key": "LWScenario:WOTRGameType001", "qualifier_key": "",
		"objectives_key": "LWScenario:WOTRObjectives001", "regions_to_win": 0},
	{"index": 2, "label_key": "LWScenario:WOTRGameType004", "qualifier_key": "",
		"objectives_key": "LWScenario:WOTRObjectives006", "regions_to_win": 0},
	{"index": 3, "label_key": "LWScenario:WOTRGameType003", "qualifier_key": "VALUE:WorldDomA",
		"objectives_key": "LWScenario:WOTRObjectives003", "regions_to_win": 20},
	{"index": 4, "label_key": "LWScenario:WOTRGameType003", "qualifier_key": "VALUE:WorldDomB",
		"objectives_key": "LWScenario:WOTRObjectives004", "regions_to_win": 30},
	{"index": 5, "label_key": "LWScenario:WOTRGameType003", "qualifier_key": "VALUE:WorldDomC",
		"objectives_key": "LWScenario:WOTRObjectives007", "regions_to_win": 40},
	{"index": 6, "label_key": "LWScenario:WOTRGameType003", "qualifier_key": "VALUE:WorldDomAll",
		"objectives_key": "LWScenario:WOTRObjectives005", "regions_to_win": 52},
]

## Retail's own "whatever the scenario says" entry, offered FIRST because a
## scenario carries its own `DisplayGameType` and this screen has one selected.
const VICTORY_SCENARIO_DEFAULT_KEY := "VALUE:UseScenarioDefaults"


# --- the handicap column -------------------------------------------------------
#
# TRANSCRIBED from `data/ini/livingworldautoresolvehandicaps.ini`: 21
# `AutoResolveHandicapLevel` blocks, whose `GUIDisplayedLevel` values are
# 0,5,...,100. Retail's own comment beside them reads "; GUI displayed -N% (we
# ignore the negative)", which is why the column shows a plain percentage.
#
# NO STRING KEYS EXIST FOR THE LEVELS. Retail renders the number. The only
# handicap text in the whole table is the column header (`APT:HdrHandicap`) and
# one tooltip, and both are used.
const HANDICAP_LEVELS: Array[int] = [
	0, 5, 10, 15, 20, 25, 30, 35, 40, 45, 50,
	55, 60, 65, 70, 75, 80, 85, 90, 95, 100,
]
const HANDICAP_DEFAULT := 0
const HANDICAP_TOOLTIP_KEY := "TOOLTIP:Skirmish/tooltipHandicap"
## UNLOCKED. Handicap multiplies auto-resolve weapon and armour strength; both
## of those now happen, and both ends of the carrier exist: the seat's
## `handicap` is authoritative strategic state, and the commitment carries
## `attacker_handicap` and `defender_handicap` at schema version 3. It reaches
## the arithmetic AND the dice, because the commitment it sits in is what the
## dice are seeded from.
## KEPT AS AN EMPTY STRING RATHER THAN DELETED, so a mod or a screen that still
## reads it gets "there is no reason, it is not locked" instead of a missing
## constant - and so the fact that this row USED to be locked, and why it no
## longer is, stays readable right here.
const HANDICAP_LOCKED_REASON := ""
## What choosing a rung actually does, shown beside the column so a player knows
## which direction helps them. Retail's own comment beside the ladder reads
## "; GUI displayed -N% (we ignore the negative)".
const HANDICAP_NOTE := "retail's own ladder: a higher rung scales that seat's auto-resolve weapon down and its armour up by exactly reciprocal multipliers"


# --- the colour swatches -------------------------------------------------------
#
# TRANSCRIBED from `data/ini/multiplayer.ini`: the six `MultiplayerColor` blocks
# that carry `AvailableInWotR = Yes`, in retail's own slot order. Four more
# blocks exist (SkyBlue, Pink, Gray, White) and are NOT here because retail
# marks them unavailable in War of the Ring.
#
# The RGB is each block's `LivingWorldColor`, NOT its `RGBColor`: the strategic
# map is what these paint, and retail authors a separate, more saturated value
# for exactly that. On these six the two differ; on the four excluded ones they
# do not, which is its own small confirmation that the six are the strategic set.
const COLORS: Array[Dictionary] = [
	{"slot": 0, "block": "ColorBlue", "name_key": "Color:Blue", "rgb": Color8(68, 40, 255)},
	{"slot": 1, "block": "ColorRed", "name_key": "Color:Red", "rgb": Color8(146, 17, 2)},
	{"slot": 2, "block": "ColorGold", "name_key": "Color:Gold", "rgb": Color8(189, 157, 16)},
	{"slot": 3, "block": "ColorGreen", "name_key": "Color:Green", "rgb": Color8(82, 229, 53)},
	{"slot": 4, "block": "ColorOrange", "name_key": "Color:Orange", "rgb": Color8(255, 102, 0)},
	{"slot": 6, "block": "ColorPurple", "name_key": "Color:Purple", "rgb": Color8(177, 68, 255)},
]


# --- the Army column -----------------------------------------------------------
#
# LIVING-WORLD PLAYER TEMPLATE -> the `SIDE:` key whose text goes in the cell.
#
# A TABLE, NOT A RULE, for the same reason `WotrSession.FACTION_BINDINGS` is one:
# stripping "Player" and looking up `SIDE:<rest>` would answer confidently and
# WRONGLY for the goblins - retail's template is `PlayerWild`, its key is
# `SIDE:Wild`, and the text a player reads is "Goblins".
#
# `INI:Faction*` carries the same English text on the `PlayerTemplate` block
# itself. `SIDE:` is used because `playertemplate.ini` states that the presence
# of a `SIDE:` block is what makes a side appear in multiplayer setup, which is
# the screen this is.
const ARMY_SIDE_KEYS := {
	"PlayerAngmar": "SIDE:Angmar",
	"PlayerDwarves": "SIDE:Dwarves",
	"PlayerElves": "SIDE:Elves",
	"PlayerIsengard": "SIDE:Isengard",
	"PlayerMen": "SIDE:Men",
	"PlayerMordor": "SIDE:Mordor",
	"PlayerWild": "SIDE:Wild",          # reads "Goblins"
}


# --- the Player column ---------------------------------------------------------
#
# Retail puts a profile name in a human seat's Player cell and an AI TIER in a
# machine seat's. The tier names are retail's (`GUI:*AI`); which tier a seat
# runs at is NOT a choice this screen offers, because `main_menu.wotr_team_
# descriptors()` fixes the tier at the simulation's own default on purpose - a
# per-session tier would be a value in front of the simulation that no strategic
# hash ever saw. The cell therefore shows the fixed tier and says it is fixed.
const AI_TIER_KEYS := {
	"easy": "GUI:EasyAI",
	"medium": "GUI:MediumAI",
	"hard": "GUI:HardAI",
	"brutal": "GUI:BrutalAI",
}
## The simulation's own default, matching `main_menu.RETAIL_AI_DEFAULT_DIFFICULTY`.
const AI_TIER_FIXED := "medium"
const AI_TIER_LOCKED_REASON := "the AI tier is the simulation's default and is deliberately not per-session; a chosen tier would not ride the strategic hash"


# --- what the seat rows may change ---------------------------------------------
#
# The four columns a player can actually operate, and where each one lands.
# ARMY and TEAM are arguments to `WotrSession.begin()`, so they are inside the
# strategic state before a single turn is taken and ride every hash after it.
# COLOUR is presentation, per seat, and is dropped by `handoff_payload()` the
# same way a hover highlight is.
const SEAT_COLUMN_REACH := {
	"player": "strategic",     # human or machine - state.players[].controller
	"army": "strategic",       # the living-world player template - state.players[].template
	"hero": "",                # derived from the scenario's own act armies; not chosen
	"team": "strategic",       # state.players[].team
	"color": "presentation",   # never hashed, never handed to the simulation
	# state.players[].handicap, and from there the commitment's two handicap
	# fields at schema version 3. It scales retail's auto-resolve multipliers AND
	# it is inside the digest the dice are seeded from, so two seats with
	# different handicaps do not merely take different damage - they roll
	# different dice.
	"handicap": "strategic",
}


# --- helpers over the tables above ---------------------------------------------

## The victory-type options for a document, or an empty list plus the reason.
##
## `document_victory_types` is the living-world document's own `victoryTypes`
## block. BFME2's is EMPTY - `LivingWorldVictoryTypes.inc` is a RotWK file - and
## this reports that instead of offering RotWK's seven over a BFME2 campaign.
static func victory_options(document_victory_types: Array) -> Dictionary:
	if document_victory_types.is_empty():
		return {
			"options": [],
			"reason": (
				"this living-world document carries no victoryTypes block, so there "
				+ "are no victory conditions to choose between. "
				+ "LivingWorldVictoryTypes.inc is a Rise of the Witch-king file; the "
				+ "BFME2 campaign does not ship one."),
		}
	var options: Array[Dictionary] = []
	for row in VICTORY_TYPES:
		if int(row["index"]) >= document_victory_types.size():
			continue
		options.append(row)
	return {"options": options, "reason": ""}


## The colour a seat starts on: slot order, wrapping. AUTHORED and presentation
## only - retail's own assignment is per lobby seat and is not in any file.
static func default_color(seat_index: int) -> Dictionary:
	return COLORS[seat_index % COLORS.size()]
