// OpenBFME. GPL-3.0.
// See Common/BuildAssistant.h.

#include "Common/BuildAssistant.h"

#include "Common/AsciiString.h"
#include "Common/GameCommon.h"
#include "Common/NumericState.h"
#include "GameLogic/SimMath.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameClient/ControlBarCommands.h"
#include "GameLogic/UpgradeTypes.h"
#include "GameLogic/Economy.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ObjectTemplateInfo.h"

#include <algorithm>
#include <stdexcept>
#include <variant>

namespace
{
long long integerField(const ThingTemplate &tt, const char *name)
{
	const FieldValue *v = tt.getFinalOverride()->findField(name);
	if (!v)
	{
		return 0;
	}
	if (const long long *i = std::get_if<long long>(v))
	{
		return *i;
	}
	throw std::logic_error(std::string("ThingTemplate field ") + name + " is not an integer");
}

// x87 `fistp` / _ftol2 truncation of a float32 (RW 0xA3CFA4), cvttss2si
int truncToInt(float f)
{
	return SimMath::truncToInt32(f);
}
} // namespace

unsigned BuildAssistant::buildCost(const ThingTemplate &tt) { return (unsigned)(integerField(tt, "BuildCost") & 0xFFFF); }
int BuildAssistant::commandPoints(const ThingTemplate &tt) { return (int)integerField(tt, "CommandPoints"); }
int BuildAssistant::buildable(const ThingTemplate &tt) { return (int)integerField(tt, "Buildable"); }
int BuildAssistant::buildCompletion(const ThingTemplate &tt) { return (int)integerField(tt, "BuildCompletion"); }

float BuildAssistant::buildTime(const ThingTemplate &tt)
{
	const FieldValue *v = tt.getFinalOverride()->findField("BuildTime");
	if (!v)
	{
		return 0.0f;
	}
	if (const float *f = std::get_if<float>(v))
	{
		return *f;
	}
	throw std::logic_error("ThingTemplate field BuildTime is not a real");
}

// RW 0x73C25F
int BuildAssistant::calcCostToBuild(const ThingTemplate &tt, const Player *player, const Object *producer, int costOverride)
{
	int cost = costOverride == -1 ? (int)buildCost(tt) : costOverride;
	if (!player)
	{
		return cost;
	}
	// lane HERO-2, RW 0x73C28F .. 0x73C2DA: a CREATE_A_HERO built at its own cost by a player with a slot hero costs the record's power cost more
	if (costOverride == -1 || costOverride == (int)buildCost(tt))
	{
		static const int kCreateAHero = ObjectTemplateInfoBuilder::kindOfIndex("CREATE_A_HERO");
		if (player->getCreateAHeroSurcharge() >= 0 && kCreateAHero >= 0 && MaskTest(ObjectTemplateInfoBuilder::build(tt).kindOf, (unsigned)kCreateAHero))
		{
			cost += player->getCreateAHeroSurcharge();
		}
	}
	float P = 1.0f;
	if (producer)
	{
		if (ProductionUpdateInterface *pu = producer->getProductionUpdate())
		{
			P = pu->productionCostMultiplier(&tt);
		}
	}
	float pct = 0.0f;
	if (const PlayerTemplate *pt = player->getPlayerTemplate())
	{
		auto it = pt->m_productionCostChanges.find(tt.getName());
		if (it != pt->m_productionCostChanges.end())
		{
			pct = it->second;
		}
	}
	float X = NumericState::pc24Add(1.0f, pct);         // fadd [1.0]; fstp (x87 PC24)
	// RW 0x73C316 .. 0x73C32C: getProductionCostChangeBasedOnKindOf(tt, false) (RW 0x6AD8A7: 1.0 + the CostModifierUpgrade percentage, lane UPGRADE-1 wiring
	// of the ECON-1 entries), then fmul by the stored X and fstp (x87 PC24)
	const float K = player->getProductionCostChangeBasedOnKindOf(tt, false);
	X = NumericState::pc24Mul(K, X);
	const float h = 1.0f;                               // the handicap table RW 0x7B19BF (S-204)
	const float r = NumericState::pc24Mul(NumericState::pc24Mul(NumericState::pc24Mul((float)cost, h), X), P);
	return truncToInt(r);                               // cvttss2si; the Brutal AI discount (RW 0x6AA61B) is not ported (S-204)
}

// RW 0x73C39E
int BuildAssistant::calcTimeToBuild(const ThingTemplate &tt, const Player *player, const Object *producer, int secondsOverride, const ProductionSettings &settings, GameLogic &logic)
{
	if (!settings.loaded)
	{
		throw std::logic_error("calcTimeToBuild: the GameData build time settings are not loaded (ProductionSettings::load)");
	}
	const float base = secondsOverride == -1 ? buildTime(tt) : (float)secondsOverride;
	const float h = 1.0f; // RW 0x7B19BF handicap (S-204)
	int t = truncToInt(base);
	t = truncToInt(NumericState::pc24Mul(h, (float)t)); // fimul + _ftol
	float pct = 0.0f;
	if (player && player->getPlayerTemplate())
	{
		const PlayerTemplate *pt = player->getPlayerTemplate();
		auto it = pt->m_productionTimeChanges.find(tt.getName());
		if (it != pt->m_productionTimeChanges.end())
		{
			pct = it->second;
		}
	}
	t = truncToInt(NumericState::pc24Mul(NumericState::pc24Add(1.0f, pct), (float)t));
	if (producer)
	{
		if (ProductionUpdateInterface *pu = producer->getProductionUpdate())
		{
			t = truncToInt(NumericState::pc24Mul(pu->productionTimeMultiplier(&tt), (float)t));
		}
	}
	// the energy ratio (RW 0x8E3503): no energy system is ported, the ratio is 1.0 (S-204)
	const float e = 1.0f;
	float rate = SimMath::subf32(1.0f, SimMath::mulf32(SimMath::subf32(1.0f, e), settings.lowEnergyPenaltyModifier));
	rate = rate > settings.minLowEnergyProductionSpeed ? rate : settings.minLowEnergyProductionSpeed;
	if (1.0f > e)
	{
		rate = std::min(rate, settings.maxLowEnergyProductionSpeed);
	}
	if (!(0.0f < rate))
	{
		rate = 0.01f;
	}
	int sec = truncToInt(SimMath::divf32((float)t, rate));
	if (buildCompletion(tt) == 1 && settings.multipleFactory != 1.0f)
	{
		// RW 0x73C1BF / 0x6ABAF7: the player's count of the facility's objects scales sec by MultipleFactory per extra factory (S-204)
		throw std::logic_error("calcTimeToBuild: GameData MultipleFactory is not 1.0 for an APPEARS_AT_RALLY_POINT template; the facility count is not ported (S-204)");
	}
	// RW 0x625456: LAN / internet / skirmish games scale by the number of live players (GameData MultiPlay*SpeedMult MPn)
	{
		int n = 0;
		PlayerList &players = logic.players();
		for (int i = 0; i < players.getPlayerCount() && i < 20; ++i)
		{
			const Player *p = players.getNthPlayer(i);
			if (p && p->getPlayerTemplate() && p->getPlayerTemplate()->m_playableSide && !p->isObserver())
			{
				++n;
			}
		}
		float f = 1.0f;
		if (n >= 1 && n - 1 < settings.multiPlayEntries)
		{
			const int structureBit = ObjectTemplateInfoBuilder::kindOfIndex("STRUCTURE");
			const bool isStructure = structureBit >= 0 && MaskTest(logic.templateInfo(tt.getFinalOverride()).kindOf, (unsigned)structureBit);
			f = isStructure ? settings.multiPlayBuildingSpeedMult[n - 1] : settings.multiPlayUnitSpeedMult[n - 1];
		}
		sec = truncToInt(NumericState::pc24Mul((float)sec, f));
	}
	return (int)LOGICFRAMES_PER_SECOND * sec; // RW 0xD9F608: 5
}

int BuildAssistant::maxSimultaneousOfType(const ThingTemplate &tt) { return (int)(integerField(tt, "MaxSimultaneousOfType") & 0xFFFF); }

namespace
{
bool kindOfTemplate(GameLogic &logic, const ThingTemplate &tt, const char *name)
{
	const int bit = ObjectTemplateInfoBuilder::kindOfIndex(name);
	return bit >= 0 && MaskTest(logic.templateInfo(tt.getFinalOverride()).kindOf, (unsigned)bit);
}

const std::vector<std::string> *stringList(const ThingTemplate &tt, const char *field)
{
	const FieldValue *v = tt.getFinalOverride()->findField(field);
	return v ? std::get_if<std::vector<std::string>>(v) : nullptr;
}

bool listHas(const std::vector<std::string> *list, const std::string &name)
{
	if (!list)
	{
		return false;
	}
	for (const std::string &n : *list)
	{
		if (AsciiStringUtil::compareNoCase(n, name) == 0)
		{
			return true;
		}
	}
	return false;
}
} // namespace

// RW 0x73D5C2
bool BuildAssistant::isEquivalentTo(const ThingTemplate *a, const ThingTemplate *b)
{
	if (!a || !b)
	{
		return false;
	}
	if (a == b || a->getFinalOverride() == b->getFinalOverride())
	{
		return true;
	}
	if (listHas(stringList(*a, "EquivalentTo"), b->getName()) || listHas(stringList(*b, "EquivalentTo"), a->getName()))
	{
		return true;
	}
	// RW +0x330 is both the BuildVariations field and the reskinned-from list (RW 0x73FA52 appends to it): ThingTemplate keeps the second apart
	const std::vector<std::string> &ra = a->getFinalOverride()->reskinnedFrom(), &rb = b->getFinalOverride()->reskinnedFrom();
	return listHas(stringList(*a, "BuildVariations"), b->getName()) || listHas(stringList(*b, "BuildVariations"), a->getName()) || listHas(&ra, b->getName()) || listHas(&rb, a->getName());
}

// RW 0x6AC856
bool BuildAssistant::playerAllowedToBuild(const Player &player, const ThingTemplate *tt, GameLogic &logic)
{
	if (!tt)
	{
		return false;
	}
	const bool structure = kindOfTemplate(logic, *tt, "STRUCTURE");
	if (!player.canBuildBase() && structure)
	{
		return false;
	}
	if (!player.canBuildUnits() && !structure)
	{
		return false;
	}
	return !player.isTemplateDisabled(tt->getTemplateID());
}

// RW 0x6AC927
bool BuildAssistant::playerCanBuild(const Player &player, const ThingTemplate *tt, GameLogic &logic)
{
	if (!tt || !playerAllowedToBuild(player, tt, logic))
	{
		return false;
	}
	const int b = buildable(*tt);
	if (b == 2)
	{
		return false;
	}
	if (b == 1)
	{
		return true;
	}
	if (b == 3 && player.getPlayerType() != PLAYER_COMPUTER)
	{
		return false;
	}
	for (const RawBlock &block : tt->getFinalOverride()->rawBlocks())
	{
		if (block.field == "Prerequisites")
		{
			logic.reportError("template " + tt->getName() + " has a Prerequisites block: the ProductionPrerequisite grammar and test (RW 0x74057B / 0x8F9ADD) are not ported (S-207); it is treated as unsatisfied");
			return false;
		}
	}
	return true;
}

// RW 0x6A7F79: the economy's rule (lane ECON-1 owns the command point model; Economy::canAffordCommandPoints)
bool BuildAssistant::commandPointsAvailable(const Player &player, const ThingTemplate &tt, GameLogic &logic)
{
	return logic.economy().canAffordCommandPoints(player, tt);
}

// RW 0x794F38
bool BuildAssistant::isInProducersCommandSet(Object &builder, const ThingTemplate *what, int buildIndex)
{
	if (!what && buildIndex == -1)
	{
		return false;
	}
	GameLogic &logic = builder.logic();
	if (buildIndex != -1)
	{
		// lane HERO-1: RW 0x795006 .. 0x795036: the producer's set must hold a buildIndex + 1-th REVIVE button (counted over the slots in order; no NeededUpgrade
		// test on this path, RW jumps straight to the answer), and the answer is the owner's hero list record at the index existing and not in production
		// (RW 0x780C46). The module offers of RW 0x794F69 need a template (none here)
		if (!TheCommandStore)
		{
			throw std::logic_error("canMakeUnit: no CommandStore (TheCommandStore) is set");
		}
		const CommandSet *set = TheCommandStore->findCommandSet(builder.getCommandSetName());
		if (!set)
		{
			return false;
		}
		int revives = 0;
		for (int i = 0; i < CommandSet::MAX_BUTTONS; ++i)
		{
			const CommandButton *button = set->getCommandButton(i);
			if (!button || button->m_command != GUI_COMMAND_REVIVE)
			{
				continue;
			}
			if (revives == buildIndex)
			{
				const Player *player = builder.getControllingPlayer();
				return player && player->heroes().isAvailable(buildIndex);
			}
			++revives;
		}
		return false;
	}
	// RW 0x794F69: the builder's modules that offer extra templates by name (the interface at +0xC vslot 0x2C): none is ported (S-207)
	if (!TheCommandStore)
	{
		throw std::logic_error("canMakeUnit: no CommandStore (TheCommandStore) is set");
	}
	const CommandSet *set = TheCommandStore->findCommandSet(builder.getCommandSetName());
	if (!set)
	{
		return false;
	}
	Player *player = builder.getControllingPlayer();
	for (int i = 0; i < CommandSet::MAX_BUTTONS; ++i)
	{
		const CommandButton *button = set->getCommandButton(i);
		if (!button)
		{
			continue;
		}
		if (!(button->m_command == GUI_COMMAND_UNIT_BUILD || button->m_command == GUI_COMMAND_DOZER_CONSTRUCT || button->m_command == GUI_COMMAND_FOUNDATION_CONSTRUCT))
		{
			continue;
		}
		const ThingTemplate *offered = button->getThingTemplate();
		if (!offered || !isEquivalentTo(offered, what))
		{
			continue;
		}
		if (button->hasOption(COMMAND_OPTION_NEED_UPGRADE))
		{
			const UpgradeTypeTable *types = logic.upgradeTypes();
			if (!types)
			{
				throw std::logic_error("canMakeUnit: no UpgradeTypeTable is set (GameLogic::setUpgradeTypes)");
			}
			size_t satisfied = 0;
			bool any = false;
			for (const std::string &u : button->m_neededUpgrade)
			{
				const int type = types->typeOf(u);
				bool have = false;
				if (type == UpgradeTypeTable::UPGRADE_TYPE_OBJECT)
				{
					have = builder.hasUpgrade(u);
				}
				else if (type == UpgradeTypeTable::UPGRADE_TYPE_PLAYER)
				{
					have = player && player->hasUpgradeComplete(u);
				}
				if (have)
				{
					++satisfied;
					any = true;
					if (button->m_neededUpgradeAny)
					{
						break;
					}
				}
			}
			const bool ok = button->m_neededUpgradeAny ? any : satisfied == button->m_neededUpgrade.size();
			if (!ok)
			{
				continue;
			}
		}
		if (player && playerCanBuild(*player, offered, logic))
		{
			return true;
		}
	}
	return false;
}

// RW 0x793ECB
bool BuildAssistant::isLineBuildTemplate(GameLogic &logic, const ThingTemplate *what, const Object *builder)
{
	// RW 0x793E33: test [what + 0x118], 0x10000000 and the same bit of the builder's template (object + 4)
	const int bit = ObjectTemplateInfoBuilder::kindOfIndex("WALL_HUB");
	if (!what || !builder || bit < 0)
	{
		return false;
	}
	return MaskTest(logic.templateInfo(what->getFinalOverride()).kindOf, (unsigned)bit) && builder->isKindOf((unsigned)bit);
}

CanMakeType BuildAssistant::canMakeUnit(Object &builder, const ThingTemplate *what, int buildIndex)
{
	if (!what && buildIndex == -1)
	{
		return CANMAKE_NO_PREREQUISITES;
	}
	GameLogic &logic = builder.logic();
	const ThingTemplate *pointsTemplate = what; // RW 0x794035: the command point test's template (lane HERO-1: the hero record's on the build-index path)
	// RW 0x793EEF: Object + 0x458 bit 0 is the dead flag; RW 0x793F39: the two script status bits (SCRIPT_DISABLED, SCRIPT_UNPOWERED, set by RW 0x693182) are not
	// ported (S-207); the wall hub rule of a DozerAI builder (RW 0x793EFA) belongs to the dozer lane
	if (builder.isDestroyed())
	{
		return CANMAKE_FACTORY_IS_DISABLED;
	}
	if (!isInProducersCommandSet(builder, what, buildIndex))
	{
		return CANMAKE_NO_PREREQUISITES;
	}
	if (ProductionUpdateInterface *pu = builder.getProductionUpdate())
	{
		const int r = pu->canQueueCreateUnit();
		if (r != CANMAKE_OK)
		{
			return (CanMakeType)r;
		}
	}
	Player *player = builder.getControllingPlayer();
	if (!player)
	{
		return CANMAKE_NO_PREREQUISITES;
	}
	// RW 0x793F8C: a dozer's reserved cost of its pending foundations (the AI's vslot 0x78) is not ported: 0
	const std::uint32_t reserved = 0;
	if (buildIndex == -1)
	{
		if (what && !kindOfTemplate(logic, *what, "BUILD_FOR_FREE"))
		{
			const int cost = calcCostToBuild(*what, player, &builder, -1);
			if ((std::uint32_t)cost > player->getMoney()->countMoney() + reserved)
			{
				return CANMAKE_NO_MONEY;
			}
		}
	}
	else
	{
		// lane HERO-1: RW 0x794004 .. 0x794030: the record's cost (RW 0x780AD3 with the producer) against the money, then the record's template (RW 0x780C2F)
		const int cost = player->heroes().costAt(logic, *player, buildIndex, &builder);
		if ((std::uint32_t)cost > player->getMoney()->countMoney() + reserved)
		{
			return CANMAKE_NO_MONEY;
		}
		pointsTemplate = player->heroes().templateAt(logic, buildIndex);
		if (!pointsTemplate)
		{
			// RW 0x6A7F79 reads the template unchecked: a record whose template is gone is no retail state
			logic.reportError("canMakeUnit: hero list record " + std::to_string(buildIndex) + " names no template");
			return CANMAKE_UNKNOWN_7;
		}
	}
	if (pointsTemplate && !commandPointsAvailable(*player, *pointsTemplate, logic))
	{
		return CANMAKE_UNKNOWN_7;
	}
	const int maxOfType = what ? maxSimultaneousOfType(*what) : 0;
	if (what && maxOfType != 0)
	{
		// RW 0x794054 countObjectsByThingTemplate + 0x79406E the player's queued units of the type (callback RW 0x793D12)
		int count = 0, queued = 0;
		for (const Object *o = logic.getFirstObject(); o; o = o->getNextObject())
		{
			if (o->getControllingPlayer() != player || o->isDestroyed())
			{
				continue;
			}
			if (isEquivalentTo(what, o->getTemplate()))
			{
				++count;
			}
			if (ProductionUpdateInterface *pu = o->getProductionUpdate())
			{
				queued += pu->countUnitTypeInQueue(what);
			}
		}
		if (count >= maxOfType || count + queued >= maxOfType)
		{
			return CANMAKE_MAXED_OUT_FOR_PLAYER;
		}
	}
	return CANMAKE_OK;
}
