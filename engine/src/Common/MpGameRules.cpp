// OpenBFME. GPL-3.0.
// See Common/MpGameRules.h for the target facts and the addresses.

#include "Common/MpGameRules.h"

namespace
{
// RW 0xDB77BC (mode 0) -> { RW 0xDB77..: InitialResources, CommandPointFactor }
const MpRuleChoice kInitialResources[] = { { "VALUE:Value", 500 }, { "VALUE:Value", 750 }, { "VALUE:Value", 1000 }, { "VALUE:Value", 1200 },
	{ "VALUE:Value", 1400 }, { "VALUE:Value", 1600 }, { "VALUE:Value", 1800 }, { "VALUE:Value", 2000 }, { "VALUE:Value", 2500 }, { "VALUE:Value", 3000 },
	{ "VALUE:Value", 4000 } };
const MpRuleCombo kInitialResourcesCombo = { RULE_INITIAL_RESOURCES, kInitialResources, 11, 2 };
const MpRuleChoice kCommandPointFactor[] = { { "VALUE:ThirdX", 33 }, { "VALUE:HalfX", 50 }, { "VALUE:1X", 100 }, { "VALUE:2X", 200 }, { "VALUE:4X", 400 },
	{ "VALUE:8X", 800 }, { "VALUE:100X", 10000 } };
const MpRuleCombo kCommandPointFactorCombo = { RULE_COMMAND_POINT_FACTOR, kCommandPointFactor, 7, 2 };
// RW 0xDB77D0 (mode 1): StrategicPhaseTimer, BattleType, BattleChoice, AutoResolveType, VictoryType (RotWK's fifth row)
const MpRuleChoice kStrategicPhaseTimer[] = { { "VALUE:NoTimer", 0 }, { "VALUE:Seconds", 5 }, { "VALUE:Seconds", 10 }, { "VALUE:Seconds", 30 },
	{ "VALUE:Seconds", 45 }, { "VALUE:Seconds", 60 }, { "VALUE:Seconds", 120 }, { "VALUE:Seconds", 180 }, { "VALUE:Seconds", 500 } };
const MpRuleCombo kStrategicPhaseTimerCombo = { RULE_STRATEGIC_PHASE_TIMER, kStrategicPhaseTimer, 9, 0 };
const MpRuleChoice kBattleType[] = { { "VALUE:AutoResolveAndRTS", 0 }, { "VALUE:AutoResolve", 1 }, { "VALUE:RTS", 2 } };
const MpRuleCombo kBattleTypeCombo = { RULE_BATTLE_TYPE, kBattleType, 3, 0 };
const MpRuleChoice kBattleChoice[] = { { "VALUE:AutoResolve", 0 }, { "VALUE:RTS", 1 } };
const MpRuleCombo kBattleChoiceCombo = { RULE_BATTLE_CHOICE, kBattleChoice, 2, 0 };
const MpRuleChoice kAutoResolveType[] = { { "VALUE:Dynamic", 0 }, { "VALUE:Quick", 1 } };
const MpRuleCombo kAutoResolveTypeCombo = { RULE_AUTO_RESOLVE_TYPE, kAutoResolveType, 2, 0 };
const MpRuleChoice kVictoryType[] = { { "LWScenario:WOTRGameType002", 0 }, { "LWScenario:WOTRGameType001", 1 }, { "LWScenario:WOTRGameType004", 2 },
	{ "VALUE:WorldDomA", 3 }, { "VALUE:WorldDomB", 4 }, { "VALUE:WorldDomC", 5 }, { "VALUE:WorldDomAll", 6 } };
const MpRuleCombo kVictoryTypeCombo = { RULE_VICTORY_TYPE, kVictoryType, 7, 0 };

// RW 0xDB77C4 / 0xDB77E4: the check entries (mode 0 points at all three, mode 1 at the first two)
const MpRuleCheck kChecks[] = { { RULE_ALLOW_CUSTOM_HEROES, true }, { RULE_ALLOW_RING_HEROES, true }, { RULE_CLAN_GAME, false } };

const MpRuleCombo *const kMode0Combos[] = { &kInitialResourcesCombo, &kCommandPointFactorCombo };
const MpRuleCombo *const kMode1Combos[] = { &kStrategicPhaseTimerCombo, &kBattleTypeCombo, &kBattleChoiceCombo, &kAutoResolveTypeCombo, &kVictoryTypeCombo };
const MpRuleCheck *const kMode0Checks[] = { &kChecks[0], &kChecks[1], &kChecks[2] };
const MpRuleCheck *const kMode1Checks[] = { &kChecks[0], &kChecks[1] };

// RW 0xDB7794
const char *const kRuleLabels[NUM_GAME_RULES] = { "RULE:AllowCustomHeroes", "RULE:ClanGame", "RULE:AllowRingHeroes", "RULE:CommandPointFactor",
	"RULE:InitialResources", "RULE:VictoryType", "RULE:StrategicPhaseTimer", "RULE:BattleType", "RULE:BattleChoice", "RULE:AutoResolveType" };
}

int MpGameRules::numComboBoxes(int mode)
{
	// RW 0x960A46
	switch (mode)
	{
		case RULES_MODE_SKIRMISH:
			return 2;
		case RULES_MODE_WOTR:
			return 5;
		default:
			return 0;
	}
}

int MpGameRules::numCheckBoxes(int mode)
{
	// RW 0x960A5D
	switch (mode)
	{
		case RULES_MODE_SKIRMISH:
			return 3;
		case RULES_MODE_WOTR:
			return 2;
		default:
			return 0;
	}
}

const MpRuleCombo *const *MpGameRules::comboBoxes(int mode)
{
	// RW 0x960A10
	switch (mode)
	{
		case RULES_MODE_SKIRMISH:
			return kMode0Combos;
		case RULES_MODE_WOTR:
			return kMode1Combos;
		default:
			return nullptr;
	}
}

const MpRuleCheck *const *MpGameRules::checkBoxes(int mode)
{
	// RW 0x960A2B
	switch (mode)
	{
		case RULES_MODE_SKIRMISH:
			return kMode0Checks;
		case RULES_MODE_WOTR:
			return kMode1Checks;
		default:
			return nullptr;
	}
}

GameRules MpGameRules::none()
{
	GameRules r;
	r.fill(-1);
	return r;
}

bool MpGameRules::isNone(const GameRules &rules)
{
	for (std::int32_t v : rules)
	{
		if (v != -1)
		{
			return false;
		}
	}
	return true;
}

GameRules MpGameRules::defaults(int mode, int demoMode)
{
	// RW 0x960A74: memset(store, 0xFF, 40); mode -1 returns; the combos' default values, then the checks' values; GlobalData + 0x9D4 & 3 clears a set rule 0
	GameRules r = none();
	if (mode == RULES_MODE_NONE)
	{
		return r;
	}
	const int combos = numComboBoxes(mode);
	const MpRuleCombo *const *c = comboBoxes(mode);
	for (int i = 0; i < combos; ++i)
	{
		if (c[i])
		{
			r[(size_t)c[i]->id] = c[i]->choices[c[i]->defaultIndex].value;
		}
	}
	const int checks = numCheckBoxes(mode);
	const MpRuleCheck *const *k = checkBoxes(mode);
	for (int i = 0; i < checks; ++i)
	{
		r[(size_t)k[i]->id] = k[i]->value ? 1 : 0; // movzx of the byte
	}
	if (demoRestrictsRules(demoMode) && r[RULE_ALLOW_CUSTOM_HEROES] != -1)
	{
		r[RULE_ALLOW_CUSTOM_HEROES] = 0;
	}
	return r;
}

const char *MpGameRules::ruleLabel(int rule)
{
	// RW 0x960B14: unsigned compare with 10
	return (unsigned)rule < (unsigned)NUM_GAME_RULES ? kRuleLabels[rule] : nullptr;
}

int MpGameRules::countDifferences(const GameRules &a, const GameRules &b)
{
	int n = 0;
	for (size_t i = 0; i < a.size(); ++i)
	{
		n += a[i] != b[i] ? 1 : 0;
	}
	return n;
}

const MpRuleCombo *MpGameRules::findCombo(int mode, int rule)
{
	const MpRuleCombo *const *c = comboBoxes(mode);
	for (int i = 0; c && i < numComboBoxes(mode); ++i)
	{
		if (c[i] && c[i]->id == rule)
		{
			return c[i];
		}
	}
	return nullptr;
}

const MpRuleCheck *MpGameRules::findCheck(int mode, int rule)
{
	const MpRuleCheck *const *k = checkBoxes(mode);
	for (int i = 0; k && i < numCheckBoxes(mode); ++i)
	{
		if (k[i] && k[i]->id == rule)
		{
			return k[i];
		}
	}
	return nullptr;
}

float LegendaryHeroes::damageDealtFactor(int factor)
{
	// (N + 5) / 6 as the nearest floats (7/6 = 0x1.2aaaaap+0, 4/3 = 0x1.555556p+0): literals, no arithmetic
	switch (factor)
	{
		case 2:
			return 1.16666663f;
		case 3:
			return 1.33333337f;
		case 4:
			return 1.5f;
		default:
			return 1.0f;
	}
}

std::u16string LegendaryHeroes::label()
{
	return u"Legendary Heroes (OpenBFME rule)";
}

std::u16string CheatModeRule::label()
{
	return u"Cheat Mode (OpenBFME rule)";
}

std::u16string LegendaryHeroes::choiceText(int factor)
{
	if (factor <= kOff)
	{
		return u"Off";
	}
	return std::u16string(1, (char16_t)(u'0' + factor)) + u"x";
}
