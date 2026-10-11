// OpenBFME. GPL-3.0.
//
// MpGameRules (lane RULES-1): the game rules of the lobby's Rules tab and of GameInfo (RotWK GameInfo + 0x60: ten ints), plus OpenBFME's own
// "Legendary Heroes" rule.
//
// TARGET FACTS (rotwk201_game.exe, caveat S-001), read from the binary:
//   * the rule labels RW 0x960B14 (59 B, tier A of BFME2 0x95A04C): a rule < 10 is the game text of the tag table RW 0xDB7794
//     {RULE:AllowCustomHeroes, RULE:ClanGame, RULE:AllowRingHeroes, RULE:CommandPointFactor, RULE:InitialResources, RULE:VictoryType,
//     RULE:StrategicPhaseTimer, RULE:BattleType, RULE:BattleChoice, RULE:AutoResolveType}, else the literal "Invalid Rule" (RW 0xC8292C). Rule 5 is
//     RotWK's RULE:VictoryType (BFME2's table had RULE:MapRevealMode there);
//   * the per-mode tables: combo boxes RW 0x960A10 (mode 0 -> RW 0xDB77BC, mode 1 -> RW 0xDB77D0, else null), check boxes RW 0x960A2B (0 -> RW 0xDB77C4,
//     1 -> RW 0xDB77E4), their counts RW 0x960A46 (combo: 2 / 5, RotWK added the mode 1 VictoryType row; BFME2 0x959F7E had 2 / 4) and RW 0x960A5D
//     (check: 3 / 2). A combo descriptor is { id, choices, count, default index }, a choice { game text label, value }, a check { id, value (byte) };
//     the values below are read from RotWK's .rdata (tools: /tmp scan in the RULES-1 report; pinned by test_rules1.cpp against RW_GAME_DAT);
//   * the defaults RW 0x960A74 (tier A of BFME2 0x959FAC): every rule -1 (memset 0xFF, 40 bytes); mode -1 stops there; else every combo's default
//     choice value, then every check's value; finally, when GlobalData + 0x9D4 & 3 (TheWritableGlobalData RW 0xDE4364) and rule 0 is set, rule 0 = 0;
//   * GlobalData + 0x9D4 is the demo mode (RULES-1 review): the Apt externs InBetaDemo / InDreamMachineDemo (names RW 0xC4FEC0 / 0xC4FEAC, registered at
//     RW 0x815E6A ..) answer "1" when it is 1 / 2 (provider RW 0x81545A), PlayerTemplateStore's parse (RW 0x5FE9AF) tests it in 1..2. Its only writer is
//     GlobalData's constructor, which stores 0 (RW 0x642E66); no other instruction, INI field table row or data table writes the offset (the one
//     0x9D4 field row, RW 0xC128B0, is InGameUI's HeroInitialSpawnNotificationTimeout). The 2.01 game is therefore never in a demo mode: kDemoMode is 0;
//   * mode 0 is the skirmish / LAN / online custom game (Skirmish's AptSkirmish::InitTheGame RW 0x9288D0 copies the mode's defaults into
//     TheSkirmishGameInfo + 0x60 when there are no saved skirmish preferences), mode 1 the War of the Ring game.
// DONOR (open-bfme-2 Common/RTS/MpGameRules.cpp, byte-matched for BFME2 1.06; Rva00559EDCCompare.cpp; Rva00559F11Parse.cpp): the same structure; the
// compare counts the rules that differ (the lobby's "non default settings" icon, RW 0x802B04).
//
// OpenBFME (NOT retail; the enhanced profile's own rule, owner request 2026-10-10): Legendary Heroes, a factor of 1 (off), 2, 3 or 4 applied at the game
// start to every hero of a playable side: max health and health x N, damage taken / N (the body damage scalar), damage dealt x (N + 5) / 6
// (1, 7/6, 4/3, 1.5). It travels in the game setup (SkirmishGameInfo::legendaryHeroes), never in the retail rule block.

#pragma once

#include <array>
#include <cstdint>
#include <string>

enum GameRuleId
{
	RULE_ALLOW_CUSTOM_HEROES = 0,
	RULE_CLAN_GAME = 1,
	RULE_ALLOW_RING_HEROES = 2,
	RULE_COMMAND_POINT_FACTOR = 3,
	RULE_INITIAL_RESOURCES = 4,
	RULE_VICTORY_TYPE = 5,
	RULE_STRATEGIC_PHASE_TIMER = 6,
	RULE_BATTLE_TYPE = 7,
	RULE_BATTLE_CHOICE = 8,
	RULE_AUTO_RESOLVE_TYPE = 9,
	NUM_GAME_RULES = 10
};

// RotWK GameInfo + 0x60 (the GR= block of the options string parsed by RW 0x802DBA; the ten u32 the LAN game buffer writer RW 0x84A976 sends)
using GameRules = std::array<std::int32_t, NUM_GAME_RULES>;

// the lobby modes of the rule tables (MpGameRules mode, AptMpGameRules + 0x60)
enum GameRulesMode
{
	RULES_MODE_NONE = -1,
	RULES_MODE_SKIRMISH = 0, ///< skirmish, LAN and online custom games
	RULES_MODE_WOTR = 1      ///< War of the Ring
};

struct MpRuleChoice
{
	const char *label; ///< a game text label; its text is a format with one %d (the value)
	std::int32_t value;
};
struct MpRuleCombo
{
	int id;
	const MpRuleChoice *choices;
	int count;
	int defaultIndex;
};
struct MpRuleCheck
{
	int id;
	bool value;
};

namespace MpGameRules
{
// RW 0x960A46 / 0x960A5D
int numComboBoxes(int mode);
int numCheckBoxes(int mode);
// RW 0x960A10 / 0x960A2B (null for another mode)
const MpRuleCombo *const *comboBoxes(int mode);
const MpRuleCheck *const *checkBoxes(int mode);
// GlobalData + 0x9D4, the demo mode (1 beta demo, 2 Dream Machine demo): RotWK 2.01's value, 0 (see above)
constexpr int kDemoMode = 0;
// a demo mode (demoMode & 3): AllowCustomHeroes is forced off (RW 0x960A74) and rules 0 / 1 shown disabled (RW 0x986810)
inline bool demoRestrictsRules(int demoMode) { return (demoMode & 3) != 0; }
// RW 0x960A74: the defaults of the mode under the demo mode `demoMode` (GlobalData + 0x9D4)
GameRules defaults(int mode, int demoMode = kDemoMode);
// every rule -1: GameInfo::reset (RW 0x801AAF calls RW 0x960A74 with -1): "no rule block"
GameRules none();
bool isNone(const GameRules &rules);
// RW 0x960B14's tag (nullptr for rule >= 10: retail shows "Invalid Rule")
const char *ruleLabel(int rule);
// Rva00559EDCCompare (BFME2 0x959EDC): how many rules differ
int countDifferences(const GameRules &a, const GameRules &b);
// the combo of `rule` in `mode` (null when the mode has no combo for it)
const MpRuleCombo *findCombo(int mode, int rule);
const MpRuleCheck *findCheck(int mode, int rule);
}

// ---- OpenBFME: Legendary Heroes (not a retail rule) ----------------------------------------------------------------------------------------------
namespace LegendaryHeroes
{
constexpr int kOff = 1;
constexpr int kMax = 4;
// the factors the Rules tab offers: Off, 2x, 3x, 4x
constexpr std::array<int, 4> kChoices{ { 1, 2, 3, 4 } };
inline bool valid(int factor) { return factor >= kOff && factor <= kMax; }
// max health and health multiplier
inline float healthFactor(int factor) { return (float)factor; }
// damage taken: the body damage scalar is divided by this
inline float armorFactor(int factor) { return (float)factor; }
// damage dealt: (N + 5) / 6 as exact float constants (1, 7/6, 4/3, 1.5): decided by RULES-1, 1.5 at 4x as the owner suggested, linear below
float damageDealtFactor(int factor);
// the Rules tab's texts (OpenBFME has no game text labels of its own: English, marked as an OpenBFME rule)
std::u16string label();
std::u16string choiceText(int factor);
}
