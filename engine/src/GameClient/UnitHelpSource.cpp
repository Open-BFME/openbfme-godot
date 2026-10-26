// OpenBFME. GPL-3.0.
// See UnitHelpSource.h (lane HOVER-1). Client-only: the help reads the logic, it never changes it.

#include "GameClient/UnitHelpSource.h"
#include "GameClient/LocalizedText.h"

#include "Common/Player.h"
#include "Common/Thing/ThingTemplate.h"
#include "Common/Upgrade.h"
#include "GameClient/GUI/GameTextSource.h"
#include "GameLogic/Economy.h"
#include "GameLogic/ExperienceLevels.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/Object/ExperienceTracker.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ObjectTemplateInfo.h"

#include <cmath>

namespace
{
UnicodeString u16(const std::string &ascii)
{
	return UnicodeString(ascii.begin(), ascii.end());
}
UnicodeString number(int v)
{
	return u16(std::to_string(v));
}
// RW 0x8EBC3B (BFME2 decomp Rva004E74E0Append.cpp, tier A): a "\n" before every line but the first
void append(UnicodeString &out, const UnicodeString &line)
{
	if (!out.empty())
	{
		out += u'\n';
	}
	out += line;
}
bool kindOf(const KindOfMaskType &mask, const char *name)
{
	const int bit = ObjectTemplateInfoBuilder::kindOfIndex(name);
	return bit >= 0 && MaskTest(mask, (unsigned)bit);
}
// DisplayMeleeDamage / DisplayRangedDamage: the row's value, else the template constructor's -1 (RW 0x73FF33 / 0x73FF39 store edi = -1): no line
int damageField(const ThingTemplate &tt, const std::string &field)
{
	const FieldValue *v = tt.findField(field);
	if (const long long *i = v ? std::get_if<long long>(v) : nullptr)
	{
		return (int)*i;
	}
	return -1;
}
// RW 0x9D25B7 (BFME2 decomp UnitHelpSource.cpp ComputeObjectRank, tier A): the current level's Rank, 0 without one
int levelRank(const ExperienceTracker &tracker)
{
	const ExperienceLevelTemplate *level = TheExperienceLevelSystem && !tracker.getLevelName().empty() ? TheExperienceLevelSystem->findLevel(tracker.getLevelName()) : nullptr;
	return level ? level->m_rank : 0;
}
// RW 0x8EBB14 (ComputeObjectMaxRank, tier A): the last level of the chain after the current one, its Rank capped by the tracker's level cap when it has one
int maxRank(const ExperienceTracker &tracker, bool multiplayer)
{
	if (!TheExperienceLevelSystem || tracker.getLevelName().empty() || !TheExperienceLevelSystem->findLevel(tracker.getLevelName()))
	{
		return 0;
	}
	const ExperienceLevelTemplate *level = TheExperienceLevelSystem->findLevel(tracker.getLevelName());
	for (const ExperienceLevelTemplate *next = TheExperienceLevelSystem->nextLevel(tracker.levelTargetName(), level->m_name, multiplayer); next;
	     next = TheExperienceLevelSystem->nextLevel(tracker.levelTargetName(), level->m_name, multiplayer))
	{
		level = next;
	}
	int rank = level->m_rank;
	// RW 0x5CB9FF (INFERENCE, S-4612): the tracker's level cap (+ 0x28) when it is set
	if (tracker.getLevelCap() > 0 && tracker.getLevelCap() < rank)
	{
		rank = tracker.getLevelCap();
	}
	return rank;
}
// RW 0x8EBA26 (BFME2 decomp UnitHelpSource.cpp rva004E72CB, tier B same-shape): the display damages with the object's modifiers. INFERENCE: the game's
// FPU runs at 24-bit precision (RW 0x440809), so each x87 step rounds to float; floor is MSVCR71's on the double, then _ftol2 truncates
void displayDamage(const ThingTemplate &tt, const Object &obj, int &melee, int &ranged)
{
	float bonus = 0.0f;
	if (!obj.attributeModifierSum(2, nullptr, bonus))
	{
		bonus = 0.0f;
	}
	const int add = (int)std::floor((double)(bonus + 0.5f));
	melee = damageField(tt, "DisplayMeleeDamage"); // + 0x5CC
	if (melee >= 0)
	{
		melee += add;
	}
	ranged = damageField(tt, "DisplayRangedDamage"); // + 0x5D0
	if (ranged >= 0)
	{
		ranged += add;
	}
	float product = 1.0f;
	if (obj.attributeModifierProduct(3, nullptr, true, product))
	{
		if (melee > 0)
		{
			melee = (int)std::floor((double)((float)melee * product + 0.5f));
		}
		if (ranged > 0)
		{
			ranged = (int)std::floor((double)((float)ranged * product + 0.5f));
		}
	}
}
} // namespace

UnicodeString UnitHelpSource::format(const UnicodeString &fmt, const std::vector<UnicodeString> &args)
{
	std::vector<LocalizedText::Argument<char16_t>> values;
	values.reserve(args.size());
	for (const UnicodeString &arg : args)
	{
		values.emplace_back(arg);
	}
	return LocalizedText::format(fmt, values);
}

UnitHelpSource::Text UnitHelpSource::compose(const Object &obj, const GameTextSource *text, GameLogic &logic, const Player *localPlayer)
{
	(void)localPlayer;
	Text t;
	const ThingTemplate *tt = obj.getTemplate() ? obj.getTemplate()->getFinalOverride() : nullptr;
	if (!tt)
	{
		return t;
	}
	const KindOfMaskType &kind = logic.templateInfo(tt).kindOf;
	const bool createAHero = kindOf(kind, "CREATE_A_HERO"); // + 0x11F bit 0x40
	if (createAHero)
	{
		t.unported.push_back("the Create-a-Hero name and template (RW 0x69194E / 0x68FA0F, S-4612)");
	}
	// the title: the object's own name (+ 0x494, not ported: empty), else the template's DisplayName text
	t.title = CommandButtonHelp::templateText(*tt, "DisplayName", text);
	// a member of a HORDE container whose own name is set: retail formats "TOOLTIP:VerteranHordeMemberName"; the port's objects carry no own name (+ 0x494),
	// so the horde's name is always empty and the title stays the member's (S-4612)
	if (!CommandButtonHelp::templateLabel(*tt, "DisplayNameInvisibleForEnemy").empty())
	{
		t.unported.push_back("the disguise label DisplayNameInvisibleForEnemy (RW 0x691B15, S-4612)");
	}
	// the rank line (RW 0x8EBE3B .. 0x8EBE9C)
	const ExperienceTracker *tracker = obj.getExperienceTracker();
	const bool multiplayer = logic.economy().isMultiplayerGame();
	const int rank = createAHero ? 1 : (tracker ? levelRank(*tracker) : 0);
	if (rank > 0)
	{
		const int top = tracker ? maxRank(*tracker, multiplayer) : 0;
		if (top > 1)
		{
			append(t.text, format(fetchOrMissing(text, "TOOLTIP:Rank"), { number(rank), number(top) }));
		}
	}
	// health and damage (RW 0x8EBE9F .. 0x8EBFCB)
	const bool skip = kindOf(kind, "HORDE") || (kindOf(kind, "DEFENSIVE_WALL") && !kindOf(kind, "HAS_HEALTH_BAR")) || kindOf(kind, "UNATTACKABLE") ||
	                  kindOf(kind, "ROCK_VENDOR") || kindOf(kind, "BASE_FOUNDATION") || createAHero;
	if (!skip)
	{
		const BodyModuleInterface *body = obj.getBodyModule();
		const int health = body ? (int)body->getMaxHealth() : 0; // vslot 0x1C, _ftol2
		if (health > 0)
		{
			append(t.text, format(fetchOrMissing(text, "TOOLTIP:MaxHealth"), { number(health) }));
		}
		int melee = 0, ranged = 0;
		displayDamage(*tt, obj, melee, ranged);
		if (melee >= 0)
		{
			append(t.text, format(fetchOrMissing(text, "TOOLTIP:MeleeDamage"), { number(melee) }));
		}
		if (ranged >= 0)
		{
			append(t.text, format(fetchOrMissing(text, "TOOLTIP:RangedDamage"), { number(ranged) }));
		}
	}
	// the upgrades (RW 0x8EBFD0 .. 0x8EC050): bit order, the Tooltip label fetched when the upgrade has one
	const UpgradeMaskType &mask = obj.getUpgradeMask();
	if (mask.any() && TheUpgradeCenter)
	{
		for (unsigned bit = 0; bit < UpgradeMaskType::BITS; ++bit)
		{
			if (!mask.test(bit))
			{
				continue;
			}
			const UpgradeTemplate *u = TheUpgradeCenter->findUpgradeByMaskBit((int)bit);
			if (u && !u->m_tooltip.empty())
			{
				append(t.text, fetchOrMissing(text, u->m_tooltip));
			}
		}
	}
	// the battles fought (Object + 0x488): not ported (S-852)
	t.unported.push_back("TOOLTIP:BattlesFought (Object + 0x488 is not ported, S-852)");
	// the description (RW 0x8EC09C): the template's Description text
	const UnicodeString description = CommandButtonHelp::templateText(*tt, "Description", text);
	if (!description.empty())
	{
		append(t.text, description);
	}
	return t;
}

std::vector<std::string> UnitHelpSource::acceptanceStops()
{
	return {
		"[S-4612] hover help (HOVER-1): ported the hero bar's help (RW 0x92BF34: the builder slot and the select-all button show their NonCommand button's "
		"help, a hero its UnitHelpSource RW 0x8EBC61 as an InGameSimpleHelp RW 0x9402E7: name, level, maximum health, damages with the modifiers, upgrades, "
		"description), the Palantir portrait's (RW 0x93099D: an own or allied object's unit help), the spell book's (RW 0x930DC4: the slot's command button), "
		"RW 0x807A81's revive / castle unpack / unit cost / science-disabled / multi-science / upgrade conflict and prerequisite branches (the repair costs "
		"and the revive Create-a-Hero name: lane ROTWK-C); not ported: the drawable's script captions, Object + 0x494 names (the veteran horde member name), the disguise "
		"label, the Create-a-Hero names and template, the battles fought (Object + 0x488, S-852); also not ported, named by the inventory of every tool tip "
		"source (tools/hover/hover_inventory.py, sources.csv): the in-game unit tips (TOOLTIP:AddSpecialAbilityNoPower, ClickToRenameThisUnit, DisbandUnit, "
		"DisbandUnitDisabled, DisbandUnitDisabledHero, DisbandUnitDisabledLeader, SupplyWarehouse, YouMayNotRenameAHero) and the War of the Ring HUD's 23 "
		"labels (STRATEGICHUD:*Tooltip, APT:StrategicBattlePromptSwapButton1Tooltip / 2Tooltip, BackButtonTooltip), "
		"and the control bar's 22 other NonCommand_* readout and menu buttons (Resources, ResourceMultiplier, CommandPoints, "
		"CommandPointsLivingWorld, Options, MultiplayerOptions, IdleWorker, Beacon, Communicator, Messenger, BriefingHistory, Objectives, "
		"PlayerStatus, UpDown, EnemyRank, PlanningMode, ObserveNextPlayer, ObservePriorPlayer, GoodPlayerExperience, EvilPlayerExperience, "
		"MaxEvenstarPower, MaxRingPower: readouts.csv); INFERENCE: a hovered hero bar / spell book "
		"button is the Apt player's current button below \"<clip>.Hero<n>\" / \"<clip>.Spell<n>\" (retail's over-button keys \"<clip>/Hero%d/\" / "
		"\"Palantir/<clip>/Spell%d/\")"
	};
}
