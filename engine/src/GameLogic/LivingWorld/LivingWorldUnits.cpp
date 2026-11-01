// OpenBFME. GPL-3.0.
// See GameLogic/LivingWorld/LivingWorldUnits.h.  Lanes WOTR-1 / WOTR-2.

#include "GameLogic/LivingWorld/LivingWorldUnits.h"

#include "Common/BuildAssistant.h"
#include "Common/Thing/ThingFactory.h"
#include "GameLogic/ExperienceLevels.h"
#include "GameLogic/LivingWorld/LivingWorldAutoResolveData.h"
#include "GameLogic/Module/ExperienceUpgradeModules.h"
#include "GameLogic/Object/Contain/HordeContainBehaviorData.h"
#include "GameLogic/Object/RetailObjectWorld.h"
#include "GameLogic/ObjectFilterMatch.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/RankInfo.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>

namespace LivingWorld
{

namespace
{
std::vector<std::string> splitWords(const std::string &text)
{
	std::vector<std::string> out;
	std::istringstream in(text);
	std::string w;
	while (in >> w)
	{
		out.push_back(w);
	}
	return out;
}

// a raw row value: the macro-expanded first token of a RawTokens slot, or a string slot
std::string fieldString(const FieldValue *v)
{
	if (!v)
	{
		return std::string();
	}
	if (const std::string *s = std::get_if<std::string>(v))
	{
		return *s;
	}
	if (const RawTokens *r = std::get_if<RawTokens>(v))
	{
		if (r->tokens.empty())
		{
			return std::string();
		}
		return r->macros.size() == r->tokens.size() && r->macros[0].isMacro ? r->macros[0].value : r->tokens[0];
	}
	throw std::logic_error("LivingWorldUnits: an AutoResolve row holds an unexpected value type");
}

int unitTypeOf(const ThingTemplate &tt)
{
	const FieldValue *v = tt.findField("AutoResolveUnitType");
	if (!v)
	{
		return AR_INVALID; // RW 0x73FF94
	}
	if (const long long *i = std::get_if<long long>(v))
	{
		return (int)*i;
	}
	const std::string name = fieldString(v);
	const char *const *names = autoResolveUnitTypeNames();
	for (int i = 0; names[i]; ++i)
	{
		if (name == names[i])
		{
			return i;
		}
	}
	throw std::runtime_error("[S-1323] " + tt.getName() + ": unknown AutoResolveUnitType " + name);
}

// the AutoResolveArmor / AutoResolveWeapon sub-blocks (table RW 0xC80638: Armor/Weapon name, RequiredUpgrades, ExcludedUpgrades), parse order
void readSets(const ThingTemplate &tt, const char *blockName, const char *row, std::vector<UnitInfo::AutoResolveSet> &out)
{
	for (const RawBlock &b : tt.rawBlocks())
	{
		if (b.field != blockName)
		{
			continue;
		}
		UnitInfo::AutoResolveSet set;
		for (size_t i = 1; i < b.lines.size(); ++i)
		{
			std::string text = b.lines[i].text;
			const size_t semi = text.find(';');
			if (semi != std::string::npos)
			{
				text.resize(semi);
			}
			const size_t eq = text.find('=');
			const std::vector<std::string> head = splitWords(eq == std::string::npos ? text : text.substr(0, eq));
			if (head.empty())
			{
				continue;
			}
			const std::vector<std::string> values = eq == std::string::npos ? std::vector<std::string>() : splitWords(text.substr(eq + 1));
			if (head[0] == "End" || head[0] == "END" || head[0] == "end")
			{
				break;
			}
			if (head[0] == row)
			{
				if (values.empty())
				{
					throw std::runtime_error("[S-1323] " + tt.getName() + ": " + blockName + " without a name");
				}
				set.name = values[0];
			}
			else if (head[0] == "RequiredUpgrades")
			{
				set.requiredUpgrades = values;
			}
			else if (head[0] == "ExcludedUpgrades")
			{
				set.excludedUpgrades = values;
			}
			else
			{
				throw std::runtime_error("[S-1323] " + tt.getName() + ": unknown field " + head[0] + " in " + blockName);
			}
		}
		out.push_back(set);
	}
}

// a name-list row (WorldMapArmoryUpgradesAllowed, parser RW 0x66F603): every token, macro-expanded as parsed
std::vector<std::string> fieldWords(const FieldValue *v)
{
	if (!v)
	{
		return {};
	}
	if (const std::vector<std::string> *l = std::get_if<std::vector<std::string>>(v))
	{
		return *l;
	}
	if (const std::string *s = std::get_if<std::string>(v))
	{
		return splitWords(*s);
	}
	if (const RawTokens *r = std::get_if<RawTokens>(v))
	{
		std::vector<std::string> out;
		for (size_t i = 0; i < r->tokens.size(); ++i)
		{
			out.push_back(i < r->macros.size() && r->macros[i].isMacro ? r->macros[i].value : r->tokens[i]);
		}
		return out;
	}
	throw std::logic_error("LivingWorldUnits: a name-list row holds an unexpected value type");
}

UnitInfo buildUnitInfo(const ThingFactory &things, const ExperienceLevelSystem *levels, int heroBit, const std::string &name)
{
	UnitInfo u;
	if (const ThingTemplate *tt = things.findTemplate(name))
	{
		u.known = true;
		u.buildCost = (int)BuildAssistant::buildCost(*tt);
		u.commandPoints = BuildAssistant::commandPoints(*tt);
		const ObjectTemplateInfo info = ObjectTemplateInfoBuilder::build(*tt);
		u.hero = heroBit >= 0 && MaskTest(info.kindOf, (unsigned)heroBit);
		static const int cahBit = ObjectTemplateInfoBuilder::kindOfIndex("CREATE_A_HERO");
		u.createAHero = cahBit >= 0 && MaskTest(info.kindOf, (unsigned)cahBit);
		u.strength = u.hero ? u.buildCost * 2 : u.buildCost;
		// WOTR-2: the autoresolve rows
		u.autoResolveUnitType = unitTypeOf(*tt);
		u.autoResolveBody = fieldString(tt->findField("AutoResolveBody"));
		u.autoResolveCombatChain = fieldString(tt->findField("AutoResolveCombatChain"));
		u.autoResolveLeadership = fieldString(tt->findField("AutoResolveLeadership"));
		readSets(*tt, "AutoResolveArmor", "Armor", u.autoResolveArmor);
		readSets(*tt, "AutoResolveWeapon", "Weapon", u.autoResolveWeapon);
		if (levels)
		{
			std::string current;
			for (const ExperienceLevelTemplate *l = levels->nextLevel(tt->getName(), current, false); l;
				 l = levels->nextLevel(tt->getName(), current, false))
			{
				UnitInfo::Level lv;
				lv.rank = l->m_rank;
				lv.requiredExperience = l->m_requiredExperience;
				lv.experienceAward = l->m_experienceAward;
				u.levels.push_back(lv);
				current = l->m_name;
			}
		}
		// WOTR2 milestone 5: the armory set (+0x404) and the LevelUpUpgrade modules (UpgradeTroops, spec K)
		u.armoryUpgrades = fieldWords(tt->findField("WorldMapArmoryUpgradesAllowed"));
		for (const ThingTemplate::Nugget &n : tt->behaviorModules().nuggets())
		{
			if (n.name != "LevelUpUpgrade")
			{
				continue;
			}
			const LevelUpUpgradeModuleData *data = dynamic_cast<const LevelUpUpgradeModuleData *>(n.data.get());
			if (!data)
			{
				throw std::runtime_error("LivingWorldUnits: " + name + ": the LevelUpUpgrade module data is not typed");
			}
			UnitInfo::LevelUp lu;
			lu.triggeredBy = data->m_triggeredBy;
			lu.conflictsWith = data->m_conflictsWith;
			lu.levelsToGain = data->m_levelsToGain;
			lu.levelCap = data->m_levelCap;
			u.levelUps.push_back(lu);
		}
		for (const ThingTemplate::Nugget &n : tt->behaviorModules().nuggets())
		{
			if (n.name == "HordeContain" || n.name == "HorseHordeContain")
			{
				u.horde = true;
				const HordeContainBehaviorData *data = dynamic_cast<const HordeContainBehaviorData *>(n.data.get());
				if (!data)
				{
					throw std::runtime_error("LivingWorldUnits: " + name + ": the HordeContain module data is not typed");
				}
				for (const InitialPayloadEntry &e : data->horde.m_transport.m_initialPayload)
				{
					u.hordePayload.push_back(std::make_pair(e.name, e.count));
				}
				break;
			}
		}
	}
	return u;
}
} // namespace

namespace
{
// ObjectFilterMatch::allows (RW 0x763543) with no players (the description visitor passes null, null): a side or relationship test fails; the S: names,
// the other names (isEquivalentTo), the exclude KindOf mask, then the rule
bool allowsTemplate(const ObjectFilter &f, const ThingTemplate &t, const ThingFactory &things)
{
	if (f.side != ObjectFilter::SIDE_ANY || f.relationships != 0)
	{
		return false;
	}
	auto named = [&](const std::vector<std::string> &names) {
		for (const std::string &n : names)
		{
			if (n.compare(0, 2, "S:") == 0)
			{
				if (t.getName() == n.substr(2))
				{
					return true;
				}
			}
			else if (const ThingTemplate *o = things.findTemplate(n))
			{
				if (ObjectFilterMatch::isEquivalentTo(&t, o))
				{
					return true;
				}
			}
		}
		return false;
	};
	if (named(f.includeNames))
	{
		return true;
	}
	if (named(f.excludeNames))
	{
		return false;
	}
	const KindOfMaskType kind = ObjectTemplateInfoBuilder::build(t).kindOf;
	bool any = false, excludeHit = false, subset = true, includeEmpty = true;
	for (size_t i = 0; i < kind.size(); ++i)
	{
		excludeHit = excludeHit || (f.excludeKindOf[i] & kind[i]) != 0;
		any = any || (f.includeKindOf[i] & kind[i]) != 0;
		subset = subset && (f.includeKindOf[i] & ~kind[i]) == 0;
		includeEmpty = includeEmpty && f.includeKindOf[i] == 0;
	}
	if (excludeHit)
	{
		return false;
	}
	if (includeEmpty)
	{
		return f.rule == ObjectFilter::RULE_ALL || f.rule == 0;
	}
	if (f.rule == ObjectFilter::RULE_ANY)
	{
		return any;
	}
	if (f.rule == ObjectFilter::RULE_NONE)
	{
		return subset;
	}
	return true;
}
} // namespace

ArmyDescription describeArmy(const ArmySummaryDescription &d, const ThingFactory &things, const std::vector<std::pair<std::string, int>> &entries)
{
	ArmyDescription out;
	std::vector<std::string> heroes;
	std::vector<int> counts(d.categories.size(), 0);
	for (const auto &e : entries)
	{
		const ThingTemplate *t = things.findTemplate(e.first);
		for (int k = 0; t && k < e.second; ++k)
		{
			if (allowsTemplate(d.heroFilter, *t, things))
			{
				if (std::find(heroes.begin(), heroes.end(), t->getName()) == heroes.end())
				{
					heroes.push_back(t->getName());
				}
				continue;
			}
			for (size_t i = 0; i < d.categories.size(); ++i)
			{
				if (allowsTemplate(d.categories[i].filter, *t, things))
				{
					++counts[i];
					break;
				}
			}
		}
	}
	for (const std::string &h : heroes)
	{
		out.heroLabels.push_back(fieldString(things.findTemplate(h)->findField("DisplayName")));
	}
	for (size_t i = 0; i < counts.size(); ++i)
	{
		if (counts[i] > 0)
		{
			out.lines.push_back(std::make_pair(counts[i] == 1 ? d.categories[i].name : d.categories[i].pluralName, counts[i]));
		}
	}
	return out;
}

UnitInfoSource makeUnitInfoSource(const ThingFactory &things, const ExperienceLevelSystem *levels)
{
	auto cache = std::make_shared<std::map<std::string, UnitInfo>>(); // ordered: no iteration-order dependence (only lookups)
	const int heroBit = ObjectTemplateInfoBuilder::kindOfIndex("HERO");
	return [&things, levels, cache, heroBit](const std::string &name) -> UnitInfo {
		auto it = cache->find(name);
		if (it != cache->end())
		{
			return it->second;
		}
		const UnitInfo u = buildUnitInfo(things, levels, heroBit, name);
		(*cache)[name] = u;
		return u;
	};
}

UnitInfoSource makeUnitInfoSource(const RetailObjectWorld &world)
{
	// the cache holds values only (no pointer into the stores): the same template gives the same facts in every reload of the same data
	auto cache = std::make_shared<std::map<std::string, UnitInfo>>();
	const int heroBit = ObjectTemplateInfoBuilder::kindOfIndex("HERO");
	return [&world, cache, heroBit](const std::string &name) -> UnitInfo {
		auto it = cache->find(name);
		if (it != cache->end())
		{
			return it->second;
		}
		const UnitInfo u = buildUnitInfo(world.things(), &world.experienceLevels(), heroBit, name);
		(*cache)[name] = u;
		return u;
	};
}

void setRankData(RetailObjectWorld &world, GameSetup &setup)
{
	setup.ranks.clear();
	const RankInfoStore &ranks = world.spellStores().ranks();
	for (int r = 1; r <= ranks.getRankLevelCount(); ++r)
	{
		SciencePurchasePointTracker::RankRow row;
		if (const PlayerRankInfo *ri = ranks.getRankInfo(r)) // RW 0x5FF99F
		{
			row.present = true;
			row.sciencePurchasePointsGranted = ri->m_sciencePurchasePointsGranted;
			row.skillPointsNeeded = ri->getSkillPointsNeeded(std::string(), true);
		}
		setup.ranks.push_back(row);
	}
	setup.rankTemplates.clear();
	for (int i = 0; i < world.playerTemplates().getPlayerTemplateCount(); ++i)
	{
		const ::PlayerTemplate *pt = world.playerTemplates().getNthPlayerTemplate(i);
		GameSetup::RankTemplate t;
		t.maxLevelSP = pt->m_maxLevelSP;
		t.intrinsicSciencePurchasePoints = pt->m_intrinsicSciencePurchasePoints;
		setup.rankTemplates.emplace(pt->getName(), t);
	}
}

} // namespace LivingWorld
