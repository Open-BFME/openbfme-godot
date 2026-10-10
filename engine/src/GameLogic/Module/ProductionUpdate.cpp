// OpenBFME. GPL-3.0.
// See GameLogic/Module/ProductionUpdate.h for the sources and what is not ported.

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "GameLogic/Module/ProductionUpdate.h"

#include "Common/Audio/AudioRequests.h"

#include "Common/Upgrade.h"

#include "GameLogic/SimMath.h"

#include "Common/AsciiString.h"
#include "Common/BuildAssistant.h"
#include "Common/GameCommon.h"
#include "Common/NumericState.h"
#include "Common/Player.h"
#include "Common/PlayerHeroList.h"
#include "Common/StateHash.h"
#include "Common/Team.h"
#include "Common/Thing/ModuleFactory.h"
#include "Common/Thing/ThingFactory.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/HeroSystem.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/HeroModules.h"
#include "GameLogic/Object/ExperienceTracker.h"
#include "GameLogic/Map/TerrainLogic.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ObjectFilter.h"
#include "GameLogic/ObjectTemplateInfo.h"

#include <algorithm>
#include <cstddef>
#include <stdexcept>

namespace
{
// DisabledType names (RW 0xDAD904, 12 entries; bit = index)
const char *const kDisabledNames[] = { "DEFAULT", "DISABLED_USER_PARALYZED", "DISABLED_EMP", "DISABLED_HELD", "DISABLED_PARALYZED", "DISABLED_UNMANNED", "DISABLED_UNDERPOWERED",
	"DISABLED_FREEFALL", "DISABLED_TEMPORARILY_BUSY", "DISABLED_SCRIPT_DISABLED", "DISABLED_SCRIPT_UNDERPOWERED", "DISABLED_USER_FROZEN", nullptr };

// RW 0x8A1700 -> 0x8A1521: the same bit string loop as every BitFlags<N>::parse (GameLogic/BitFlags.h ParseBitFlags)
void parseDisabledMask(INI *ini, void *, void *store, const void *)
{
	std::uint32_t word = *static_cast<std::uint32_t *>(store);
	ParseBitFlags(ini, &word, 1, kDisabledNames);
	*static_cast<std::uint32_t *>(store) = word;
}

// RW 0x8A3633: `QuantityModifier = Name [count]`, appended; the count defaults to 1
void parseQuantityModifier(INI *ini, void *, void *store, const void *)
{
	std::vector<QuantityModifier> *v = static_cast<std::vector<QuantityModifier> *>(store);
	QuantityModifier q;
	q.templateName = ini->getNextToken();
	const char *count = ini->getNextTokenOrNull();
	q.quantity = count ? ini->scanInt(count) : 1;
	v->push_back(std::move(q));
}

// ModifierFilter: RW 0x76392F into a filter the entry owns
void parseModifierFilter(INI *ini, void *instance, void *, const void *userData)
{
	ProductionModifier *m = static_cast<ProductionModifier *>(instance);
	m->filter = std::make_shared<ObjectFilter>();
	ParseObjectFilter(ini, instance, m->filter.get(), userData);
}

#define PM_OFF(member) (int)offsetof(ProductionModifier, member)
// RW 0xC67CB8
const FieldParse kProductionModifierFieldParse[] = {
	{ "RequiredUpgrade", INI::parseAsciiString, nullptr, PM_OFF(requiredUpgrade) },
	{ "ModifierFilter", parseModifierFilter, nullptr, 0 },
	{ "CostMultiplier", INI::parseReal, nullptr, PM_OFF(costMultiplier) },
	{ "TimeMultiplier", INI::parseReal, nullptr, PM_OFF(timeMultiplier) },
	{ "HeroPurchase", INI::parseBool, nullptr, PM_OFF(heroPurchase) },
	{ "HeroRevive", INI::parseBool, nullptr, PM_OFF(heroRevive) },
	{ nullptr, nullptr, nullptr, 0 }
};
#undef PM_OFF

// RW 0x8A160B: a nested block appended to the module data's list
void parseProductionModifier(INI *ini, void *, void *store, const void *)
{
	std::list<ProductionModifier> *list = static_cast<std::list<ProductionModifier> *>(store);
	list->emplace_back();
	ini->initFromINI(&list->back(), kProductionModifierFieldParse);
}

#define PU_OFF(member) (int)offsetof(ProductionUpdateModuleData, member)
// RW 0xC68128, in the binary's order
const FieldParse kProductionUpdateFieldParse[] = {
	{ "MaxQueueEntries", INI::parseInt, nullptr, PU_OFF(m_maxQueueEntries) },
	{ "NumDoorAnimations", INI::parseInt, nullptr, PU_OFF(m_numDoorAnimations) },
	{ "DoorOpeningTime", INI::parseDurationUnsignedInt, nullptr, PU_OFF(m_doorOpeningTime) },
	{ "DoorWaitOpenTime", INI::parseDurationUnsignedInt, nullptr, PU_OFF(m_doorWaitOpenTime) },
	{ "DoorCloseTime", INI::parseDurationUnsignedInt, nullptr, PU_OFF(m_doorClosingTime) },
	{ "ConstructionCompleteDuration", INI::parseDurationUnsignedInt, nullptr, PU_OFF(m_constructionCompleteDuration) },
	{ "QuantityModifier", parseQuantityModifier, nullptr, PU_OFF(m_quantityModifiers) },
	{ "ProductionModifier", parseProductionModifier, nullptr, PU_OFF(m_productionModifiers) },
	{ "DisabledTypesToProcess", parseDisabledMask, nullptr, PU_OFF(m_disabledTypesToProcess) },
	{ "GiveNoXP", INI::parseBool, nullptr, PU_OFF(m_giveNoXP) },
	{ "UnitInvulnerableTime", INI::parseDurationUnsignedInt, nullptr, PU_OFF(m_unitInvulnerableTime) },
	{ "SpecialPrepModelconditionTime", INI::parseDurationUnsignedInt, nullptr, PU_OFF(m_specialPrepModelConditionTime) },
	{ "VeteranUnitsFromVeteranFactory", INI::parseBool, nullptr, PU_OFF(m_veteranUnitsFromVeteranFactory) },
	{ "SetBonusModelConditionOnSpeedBonus", INI::parseBool, nullptr, PU_OFF(m_setBonusModelConditionOnSpeedBonus) },
	{ "BonusForType", INI::parseAsciiString, nullptr, PU_OFF(m_bonusForType) },
	{ "SpeedBonusAudioLoop", INI::parseAsciiString, nullptr, PU_OFF(m_speedBonusAudioLoop) }, // RW 0x73B217 resolves an audio event (S-203)
	{ "SecondaryQueue", INI::parseBool, nullptr, PU_OFF(m_secondaryQueue) },
	{ nullptr, nullptr, nullptr, 0 }
};
#undef PU_OFF

// model condition bit numbers by name (RW 0xD9FAD8: the binary's order, found by name so a name table change cannot move them)
int mcBit(const char *name)
{
	int bit = -1;
	for (int i = 0; TheModelConditionNames[i]; ++i)
	{
		if (AsciiStringUtil::compareNoCase(TheModelConditionNames[i], name) == 0)
		{
			bit = i;
			break;
		}
	}
	if (bit < 0)
	{
		throw std::logic_error(std::string("ModelCondition name table lacks ") + name);
	}
	return bit;
}

struct DoorBits
{
	int opening[DOOR_COUNT_MAX], closing[DOOR_COUNT_MAX], waitingOpen[DOOR_COUNT_MAX], waitingToClose[DOOR_COUNT_MAX];
	int constructionComplete = 0, jumpBuilt = 0;
	DoorBits()
	{
		for (int i = 0; i < DOOR_COUNT_MAX; ++i)
		{
			const std::string n = "DOOR_" + std::to_string(i + 1) + "_";
			opening[i] = mcBit((n + "OPENING").c_str());
			closing[i] = mcBit((n + "CLOSING").c_str());
			waitingOpen[i] = mcBit((n + "WAITING_OPEN").c_str());
			waitingToClose[i] = mcBit((n + "WAITING_TO_CLOSE").c_str());
		}
		constructionComplete = mcBit("CONSTRUCTION_COMPLETE");
	}
};
const DoorBits &doorBits()
{
	static const DoorBits bits;
	return bits;
}

unsigned statusBit(const char *name)
{
	const int bit = ObjectTemplateInfoBuilder::objectStatusIndex(name);
	if (bit < 0)
	{
		throw std::logic_error(std::string("ObjectStatus name table lacks ") + name);
	}
	return (unsigned)bit;
}

HordeContainInterface *hordeOf(Object *o)
{
	return o && o->getContain() ? o->getContain()->getHordeContainInterface() : nullptr;
}
} // namespace

void ProductionUpdateModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kProductionUpdateFieldParse);
}

void ProductionUpdate::registerClass(ModuleFactory &modules)
{
	modules.bindTypedData<ProductionUpdateModuleData>("ProductionUpdate", MODULETYPE_BEHAVIOR);
	modules.bindModuleProc("ProductionUpdate", MODULETYPE_BEHAVIOR, [](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
		const ProductionUpdateModuleData *typed = dynamic_cast<const ProductionUpdateModuleData *>(data);
		if (!typed)
		{
			throw std::logic_error("ProductionUpdate: the module data is not typed");
		}
		return std::make_unique<ProductionUpdate>(thing, typed);
	});
}

// RW 0x8A17D8
ProductionUpdate::ProductionUpdate(Thing *thing, const ProductionUpdateModuleData *data)
	: UpdateModule(thing, data)
	, m_data(data)
{
}

void ProductionUpdate::setBit(Flags &f, int bit, bool on)
{
	if (bit < 0 || bit >= 19 * 32)
	{
		return;
	}
	if (on)
	{
		f[(size_t)bit >> 5] |= 1u << (bit & 31);
	}
	else
	{
		f[(size_t)bit >> 5] &= ~(1u << (bit & 31));
	}
}

int ProductionUpdate::canQueueCreateUnit() const
{
	if (m_factoryDisabled)
	{
		return CANMAKE_FACTORY_IS_DISABLED;
	}
	return (int)m_queue.size() < m_data->m_maxQueueEntries ? CANMAKE_OK : CANMAKE_QUEUE_FULL;
}

const ProductionEntry *ProductionUpdate::nextProduction(const ProductionEntry *e) const
{
	for (size_t i = 0; i < m_queue.size(); ++i)
	{
		if (m_queue[i].get() == e)
		{
			return i + 1 < m_queue.size() ? m_queue[i + 1].get() : nullptr;
		}
	}
	return nullptr;
}

// RW 0x8A0D8A
void ProductionUpdate::setHoldDoorOpen(int door, bool hold)
{
	if (door < 0 || door >= DOOR_COUNT_MAX)
	{
		return;
	}
	m_doors[door].holdOpen = hold;
	if (hold && m_doors[door].openedFrame == 0 && m_doors[door].waitOpenFrame == 0 && m_doors[door].closedFrame == 0)
	{
		// RW 0x8A0DAE .. 0x8A0DB9: a held door opens at once only when all three timestamps are zero (a closing door is only held; the body beyond is S-203)
		m_doors[door].openedFrame = getObject()->logic().getFrame();
		setBit(m_setFlags, doorBits().opening[door], true);
		m_flagsDirty = true;
	}
}

// RW 0x763543 over a TEMPLATE (what RW 0x8A093D / 0x8A09B7 pass: the template, no object, no other player). Order of the binary: a relationship
// requirement fails without an object (+0x84); the `+` names (equivalence, RW 0x73D5C2) accept at once, the `-` names reject; the exclude KindOf mask
// rejects; then the rule decides: ALL accepts, NONE / ANY accept only through a non-empty include KindOf mask (NONE: every bit present, ANY: one bit).
// INFERENCE (S-203): which of the table's name vectors is the `+` one follows the parser's own order (the positive vector accepts, RW 0x7636A6, the
// negative rejects, RW 0x7636D0); a side requirement (+0x90) reads the object the call does not have, retail dereferences null there and this port
// refuses with an error instead of guessing.
static bool filterMatchesTemplate(const ObjectFilter &f, const ThingTemplate &tt, GameLogic &logic)
{
	if (f.side != ObjectFilter::SIDE_ANY)
	{
		throw std::logic_error("ProductionModifier ModifierFilter with an EVIL / GOOD requirement cannot be evaluated over a template (retail dereferences a null object, RW 0x7635C2)");
	}
	if (f.relationships != 0)
	{
		return false;
	}
	for (const std::string &n : f.includeNames)
	{
		if (BuildAssistant::isEquivalentTo(&tt, logic.things().findTemplate(n)))
		{
			return true;
		}
	}
	for (const std::string &n : f.excludeNames)
	{
		if (BuildAssistant::isEquivalentTo(&tt, logic.things().findTemplate(n)))
		{
			return false;
		}
	}
	const KindOfMaskType &kinds = logic.templateInfo(tt.getFinalOverride()).kindOf;
	auto any = [](const KindOfMaskType &m) {
		for (std::uint32_t w : m)
		{
			if (w)
			{
				return true;
			}
		}
		return false;
	};
	auto intersects = [](const KindOfMaskType &a, const KindOfMaskType &b) {
		for (size_t i = 0; i < a.size(); ++i)
		{
			if (a[i] & b[i])
			{
				return true;
			}
		}
		return false;
	};
	if (any(f.excludeKindOf) && intersects(kinds, f.excludeKindOf))
	{
		return false;
	}
	if (f.rule == ObjectFilter::RULE_NONE && any(f.includeKindOf))
	{
		bool all = true;
		for (size_t i = 0; i < kinds.size(); ++i)
		{
			all = all && (kinds[i] & f.includeKindOf[i]) == f.includeKindOf[i];
		}
		return all;
	}
	if (f.rule == ObjectFilter::RULE_ANY && any(f.includeKindOf))
	{
		return intersects(kinds, f.includeKindOf);
	}
	return f.rule == ObjectFilter::RULE_ALL;
}

// RW 0x8A093D / 0x8A09B7: the ProductionModifier entries whose filter allows the template, and whose RequiredUpgrade the object has (an entry with no
// RequiredUpgrade applies; RW 0x66F5E5 looks the upgrade up in the UpgradeCenter, UPGRADE-1's: here the object's own upgrade names, S-205), multiply in
// list order. A modifier without a parsed filter keeps the parser's default (ALL).
static float modifierProduct(const ProductionUpdateModuleData &data, const ThingTemplate *tt, bool cost, const Object &producer)
{
	float m = 1.0f;
	for (const ProductionModifier &e : data.m_productionModifiers)
	{
		if (e.filter && !filterMatchesTemplate(*e.filter, *tt, producer.logic()))
		{
			continue;
		}
		if (!e.requiredUpgrade.empty() && !producer.hasUpgrade(e.requiredUpgrade))
		{
			continue;
		}
		m = SimMath::mulf32(m, cost ? e.costMultiplier : e.timeMultiplier);
	}
	return m;
}

float ProductionUpdate::productionCostMultiplier(const ThingTemplate *type) const
{
	return modifierProduct(*m_data, type, true, *getObject());
}

float ProductionUpdate::productionTimeMultiplier(const ThingTemplate *type) const
{
	return modifierProduct(*m_data, type, false, *getObject());
}

// lane HERO-1: RW 0x8A0A31 / 0x8A0AB0 (slots 28 / 29): the ProductionModifier entries with HeroRevive (a revive record) or HeroPurchase (a purchase record) whose
// RequiredUpgrade is empty or the producer's (RW 0x66F5E5 + 0x691421; the same name test as modifierProduct, S-205) multiply in list order; the ModifierFilter is
// not consulted
static float heroModifierProduct(const ProductionUpdateModuleData &data, bool revive, bool cost, const Object &producer)
{
	float m = 1.0f;
	for (const ProductionModifier &e : data.m_productionModifiers)
	{
		if (!((e.heroRevive && revive) || (e.heroPurchase && !revive)))
		{
			continue;
		}
		if (!e.requiredUpgrade.empty() && !producer.hasUpgrade(e.requiredUpgrade))
		{
			continue;
		}
		m = SimMath::mulf32(cost ? e.costMultiplier : e.timeMultiplier, m);
	}
	return m;
}

float ProductionUpdate::heroCostMultiplier(bool revive) const
{
	return heroModifierProduct(*m_data, revive, true, *getObject());
}

float ProductionUpdate::heroTimeMultiplier(bool revive) const
{
	return heroModifierProduct(*m_data, revive, false, *getObject());
}

// RW 0x8A11D2 (lane HERO-1: the build-index branch RW 0x8A123D .. 0x8A1312)
bool ProductionUpdate::queueCreateUnit(const ThingTemplate *unitType, int buildIndex, ProductionID productionID, int value30, bool batch, const std::string &name, bool flag44)
{
	Object *obj = getObject();
	GameLogic &logic = obj->logic();
	const bool fromIndex = buildIndex != -1;
	if (fromIndex)
	{
		unitType = nullptr; // RW 0x8A11EF
	}
	if (BuildAssistant::canMakeUnit(*obj, unitType, buildIndex) != CANMAKE_OK)
	{
		return false;
	}
	if ((int)m_queue.size() >= m_data->m_maxQueueEntries)
	{
		return false;
	}
	Player *player = obj->getControllingPlayer();
	int cost = 0;
	static const int buildForFree = ObjectTemplateInfoBuilder::kindOfIndex("BUILD_FOR_FREE");
	if (fromIndex)
	{
		cost = player->heroes().costAt(logic, *player, buildIndex, obj); // RW 0x8A123D: (player + 0x758)->cost(index, producer)
	}
	else if (!(buildForFree >= 0 && MaskTest(logic.templateInfo(unitType->getFinalOverride()).kindOf, (unsigned)buildForFree)))
	{
		cost = BuildAssistant::calcCostToBuild(*unitType, player, obj, -1);
	}
	int count = batch ? 5 : 1;
	bool first = true;
	while (count != 0)
	{
		if ((int)m_queue.size() >= m_data->m_maxQueueEntries)
		{
			return true;
		}
		// RW 0x8A1292 .. 0x8A12AD: withdrawn through the player's score keeper (money spent), then the spend by kind with the unit template (null for a
		// build-index entry: RW 0x8A11EF cleared it) and the cost asked for (lane END-2)
		player->withdrawMoney((std::uint32_t)cost, true);
		player->getScoreKeeper().addMoneySpentByKind(logic, unitType, cost);
		auto e = std::make_unique<ProductionEntry>();
		e->productionID = first ? productionID : requestUniqueUnitID();
		first = false;
		e->quantityProduced = 0;
		e->quantityTotal = 1;
		if (fromIndex)
		{
			// RW 0x8A12F1 .. 0x8A1312: the record starts (production id, the logic frame); a record that cannot start drops the entry and the queue call fails (the
			// money already withdrawn stays withdrawn, S-855); the entry's template is the record's
			e->type = PRODUCTION_BUILD_INDEX;
			if (!player->heroes().startProduction(buildIndex, e->productionID, logic.getFrame()))
			{
				return false;
			}
			e->objectToProduce = player->heroes().templateAt(logic, buildIndex);
		}
		else
		{
			for (const QuantityModifier &q : m_data->m_quantityModifiers)
			{
				const ThingTemplate *t2 = logic.things().findTemplate(q.templateName);
				if (t2 && ThingTemplateEquivalence::isEquivalentTo(t2, unitType))
				{
					e->quantityTotal = q.quantity;
					break;
				}
			}
			e->type = PRODUCTION_UNIT;
			e->objectToProduce = unitType;
		}
		e->exitDoor = DOOR_NONE_AVAILABLE;
		e->value30 = value30;
		e->cost = cost;
		e->name = name;
		e->flag44 = flag44;
		m_queue.push_back(std::move(e)); // RW 0x8A0C99 addToProductionQueue (the wake flag: the module is scheduled every frame already)
		--count;
		if (player->getMoney()->countMoney() < (std::uint32_t)cost)
		{
			return true;
		}
	}
	return true;
}

// ---- lane UPGRADE-1: upgrade entries ---------------------------------------------------------------------------------------------------
// RW 0x8A06ED
int ProductionUpdate::canQueueUpgrade(const UpgradeTemplate *) const
{
	if ((int)m_queue.size() >= m_data->m_maxQueueEntries)
	{
		return 4;
	}
	static const unsigned kDefected = statusBit("TEMPORARILY_DEFECTED"); // RW 0x8A0700: status bit 0x3E
	return getObject()->testStatus(kDefected) ? 3 : 0;
}

// RW 0x8A053D
bool ProductionUpdate::isUpgradeInQueue(const UpgradeTemplate *upgrade) const
{
	for (const auto &e : m_queue)
	{
		if (e->type == PRODUCTION_UPGRADE && e->upgradeToResearch == upgrade)
		{
			return true;
		}
	}
	return false;
}

// RW 0x8A0FDA
bool ProductionUpdate::queueUpgrade(const UpgradeTemplate *upgrade)
{
	if (!upgrade)
	{
		return false;
	}
	Object *obj = getObject();
	Player *player = obj->getControllingPlayer();
	if (!player)
	{
		throw std::logic_error("ProductionUpdate::queueUpgrade: the producer has no controlling player (RW 0x8A0FF0 reads it unchecked)");
	}
	const bool playerType = upgrade->getUpgradeType() == UPGRADE_TYPE_PLAYER;
	if (playerType && !UpgradeCenter::canAffordUpgrade(player, upgrade, obj)) // RW 0x66F492
	{
		return false;
	}
	if (!playerType && (obj->hasUpgrade(upgrade) || !obj->affectedByUpgrade(upgrade))) // RW 0x691421, 0x694914
	{
		return false;
	}
	if (isUpgradeInQueue(upgrade)) // slot 6
	{
		return false;
	}
	if (playerType && (player->hasUpgradeComplete(upgrade) || player->hasUpgradeInProduction(upgrade))) // RW 0x6AC2AF, 0x6AB2E1
	{
		return false;
	}
	if ((int)m_queue.size() >= m_data->m_maxQueueEntries)
	{
		return false;
	}
	auto e = std::make_unique<ProductionEntry>();
	e->productionID = 0; // RW 0x8A1060: +0x10 = 0
	e->type = PRODUCTION_UPGRADE;
	e->upgradeToResearch = upgrade;
	// RW 0x8A107B: the cost through a float (fild; fistp-free truncation of an exact integer) and withdrawn through the score keeper (RW 0x7B17EF)
	const int cost = upgrade->calcCostToBuild(player, obj);
	e->cost = cost;
	player->withdrawMoney((std::uint32_t)cost, true);
	// RW 0x8A10A0 .. 0x8A10C8: a castle member records the cost at + 0x33C (not ported: S-486); otherwise an OBJECT upgrade adds it to Object + 0x340
	if (!playerType)
	{
		obj->addUpgradeCostPaid(SimMath::sseFromInt32(cost));
	}
	m_queue.push_back(std::move(e)); // RW 0x8A0C99
	player->addUpgrade(upgrade, Player::UPGRADE_STATUS_IN_PRODUCTION); // RW 0x8A10D7 (also for an OBJECT upgrade)
	return true;
}

// RW 0x8A1140
void ProductionUpdate::cancelUpgrade(const UpgradeTemplate *upgrade)
{
	if (!upgrade)
	{
		return;
	}
	Player *player = getObject()->getControllingPlayer();
	const bool playerType = upgrade->getUpgradeType() == UPGRADE_TYPE_PLAYER;
	if (playerType && !(player && player->hasUpgradeInProduction(upgrade)))
	{
		return;
	}
	for (auto &up : m_queue)
	{
		ProductionEntry *e = up.get();
		if (e->type == PRODUCTION_UPGRADE && e->upgradeToResearch == upgrade)
		{
			if (player)
			{
				player->depositMoney((std::uint32_t)e->cost, true); // RW 0x7B18B8: the stored cost
			}
			removeFromProductionQueue(e);
			if (playerType && player)
			{
				player->removeUpgrade(upgrade); // RW 0x6AE60C (the in-production record; an OBJECT upgrade's player bit stays, as in RW)
			}
			return;
		}
	}
}

// RW 0x8A2015 .. 0x8A22FD (the castle refund RW 0x79D322 / 0x79D833, the UI messages, the research sound and Eva event are not ported: S-486)
void ProductionUpdate::completeUpgrade(ProductionEntry *entry, Player *player)
{
	const UpgradeTemplate *upgrade = entry->upgradeToResearch;
	if (upgrade)
	{
		// lane FX-2: RW 0x8A223B .. 0x8A22B4: a non-empty UpgradeFX (+0x3C, RW 0x4DD1D9) is looked up in the FXList store (RW 0x5E20A2: an unknown name plays
		// nothing) and played as doFXObj(fx, the producer, null) through RW 0x4B1B5A, before the upgrade is granted
		GameLogic &logic = getObject()->logic();
		logic.fxEvents().emit(FXEventLog::objectEvent(FXEvent::OBJECT_FX, "UpgradeFX", logic.getFrame(), upgrade->m_upgradeFX, *getObject()));
		if (upgrade->getUpgradeType() == UPGRADE_TYPE_PLAYER)
		{
			player->addUpgrade(upgrade, Player::UPGRADE_STATUS_COMPLETE); // RW 0x6AEE22
		}
		else
		{
			getObject()->giveUpgrade(upgrade); // RW 0x69388B
		}
	}
	removeFromProductionQueue(entry); // RW 0x8A22D9
}

// RW 0x8A0CF2
void ProductionUpdate::removeFromProductionQueue(ProductionEntry *e)
{
	if (e->type == PRODUCTION_UNIT && e->exitDoor != DOOR_NONE_AVAILABLE)
	{
		if (ExitInterface *exit = getObject()->getObjectExitInterface())
		{
			exit->unreserveDoorForExit((ExitDoorType)e->exitDoor);
		}
	}
	for (auto it = m_queue.begin(); it != m_queue.end(); ++it)
	{
		if (it->get() == e)
		{
			m_queue.erase(it);
			break;
		}
	}
}

// RW 0x8A13EE
void ProductionUpdate::cancelUnitCreate(ProductionID productionID)
{
	Object *obj = getObject();
	for (auto &up : m_queue)
	{
		ProductionEntry *e = up.get();
		const bool completedWaiting = e->percentComplete >= 100.0f && m_exitingObjectID != INVALID_ID; // RW 0xBD88D8 = 100.0f
		if (e->productionID == productionID && (m_cancellingAll || !completedWaiting))
		{
			Player *p = obj->getControllingPlayer();
			if (!completedWaiting && p)
			{
				// RW 0x8A144B .. 0x8A1471: the stored cost (not a recomputed one) back through the score keeper (money earned), then the spend by kind
				// takes it back with the entry's template (+ 8) (lane END-2)
				p->depositMoney((std::uint32_t)e->cost, true);
				p->getScoreKeeper().addMoneySpentByKind(obj->logic(), e->objectToProduce, -e->cost);
			}
			if (e->type == PRODUCTION_BUILD_INDEX && p)
			{
				p->heroes().cancelProduction(e->productionID); // lane HERO-1: RW 0x8A1460 -> 0x780C64
			}
			removeFromProductionQueue(e); // frees the entry
			return;
		}
	}
}

// RW 0x8A03EC (slot 11, lane HERO-1): the first unit or hero entry from the head whose template the type is equivalent to is cancelled (slot 10)
void ProductionUpdate::cancelFirstUnitOfType(const ThingTemplate *type)
{
	for (const auto &up : m_queue)
	{
		ProductionEntry *e = up.get();
		if ((e->type == PRODUCTION_UNIT || e->type == PRODUCTION_BUILD_INDEX) && type && e->objectToProduce && ThingTemplateEquivalence::isEquivalentTo(type, e->objectToProduce))
		{
			cancelUnitCreate(e->productionID);
			return;
		}
	}
}

// RW 0x8A047B (slot 12, lane HERO-1): MSG_CANCEL_UNIT_CREATE with the build-index flag: the template of the record at the index (RW 0x780D9F) through slot 11
void ProductionUpdate::cancelUnitCreateByBuildIndex(int buildIndex)
{
	Player *player = getObject()->getControllingPlayer();
	if (!player)
	{
		return;
	}
	cancelFirstUnitOfType(player->heroes().templateAt(getObject()->logic(), buildIndex));
}

// RW 0x8A0669 (lane HERO-1): a hero entry whose player cannot afford its template's command points (RW 0x6A7F79(template, 1)) pushes its record's start one frame
void ProductionUpdate::pauseHeroEntries()
{
	Object *obj = getObject();
	Player *player = obj->getControllingPlayer();
	if (!player)
	{
		return;
	}
	GameLogic &logic = obj->logic();
	for (const auto &up : m_queue)
	{
		ProductionEntry *e = up.get();
		if (e->type != PRODUCTION_BUILD_INDEX || !e->objectToProduce)
		{
			continue;
		}
		if (BuildAssistant::commandPointsAvailable(*player, *e->objectToProduce, logic))
		{
			continue;
		}
		const int index = player->heroes().findIndex(*e->objectToProduce, e->productionID, 0);
		if (HeroRecord *r = player->heroes().at(index))
		{
			++r->startFrame;
		}
	}
}

// RW 0x8A0428: from the TAIL, up to 5 entries (1 unless `all`)
void ProductionUpdate::cancelUnitCreateByType(const ThingTemplate *type, bool all)
{
	int n = all ? 5 : 1;
	while (n)
	{
		ProductionEntry *found = nullptr;
		for (size_t i = m_queue.size(); i-- > 0;)
		{
			ProductionEntry *e = m_queue[i].get();
			if ((e->type == PRODUCTION_UNIT || e->type == PRODUCTION_BUILD_INDEX) && type && e->objectToProduce && ThingTemplateEquivalence::isEquivalentTo(type, e->objectToProduce))
			{
				found = e;
				break;
			}
		}
		if (!found)
		{
			return;
		}
		cancelUnitCreate(found->productionID);
		--n;
	}
}

// RW 0x8A056C
int ProductionUpdate::countUnitTypeInQueue(const ThingTemplate *type) const
{
	int n = 0;
	for (const auto &e : m_queue)
	{
		if (e->type == PRODUCTION_UNIT && ThingTemplateEquivalence::isEquivalentTo(type, e->objectToProduce))
		{
			++n;
		}
	}
	return n;
}

// RW 0x8A05B2: the cancel-all flag makes completed entries refund-less (RW 0x8A13EE)
void ProductionUpdate::cancelAllProduction()
{
	m_cancellingAll = true;
	while (!m_queue.empty())
	{
		const ProductionEntry *head = m_queue.front().get();
		if (head->type == PRODUCTION_UNIT || head->type == PRODUCTION_BUILD_INDEX)
		{
			cancelUnitCreate(head->productionID);
		}
		else if (head->type == PRODUCTION_UPGRADE)
		{
			const size_t before = m_queue.size();
			cancelUpgrade(head->upgradeToResearch); // RW 0x8A05E6 (slot 4)
			if (m_queue.size() == before)
			{
				throw std::logic_error("ProductionUpdate::cancelAllProduction: the upgrade entry was not cancelled (RW 0x8A05B2 would loop forever)");
			}
		}
		else
		{
			removeFromProductionQueue(m_queue.front().get());
		}
	}
	m_cancellingAll = false;
}

// RW 0x8A072F
ProductionEntry *ProductionUpdate::currentEntry()
{
	for (auto &e : m_queue)
	{
		if (e->quantityRemaining() != e->quantityTotal)
		{
			return e.get(); // an entry that already produced something goes first
		}
	}
	static const int dozer = ObjectTemplateInfoBuilder::kindOfIndex("DOZER");
	GameLogic &logic = getObject()->logic();
	for (auto &e : m_queue)
	{
		if ((e->type == PRODUCTION_UNIT || e->type == PRODUCTION_BUILD_INDEX) && e->objectToProduce && dozer >= 0 && // RW 0x729661: type 1 or 3
			MaskTest(logic.templateInfo(e->objectToProduce->getFinalOverride()).kindOf, (unsigned)dozer))
		{
			return e.get();
		}
	}
	// RW 0x8A078A (lane HERO-1): a hero entry whose record's progress reached 1.0
	if (Player *player = getObject()->getControllingPlayer())
	{
		for (auto &e : m_queue)
		{
			if (e->type != PRODUCTION_BUILD_INDEX || !e->objectToProduce)
			{
				continue;
			}
			const int index = player->heroes().findIndex(*e->objectToProduce, e->productionID, 0);
			if (player->heroes().progressAt(logic, *player, index, getObject()) >= 1.0)
			{
				return e.get();
			}
		}
	}
	return m_queue.empty() ? nullptr : m_queue.front().get();
}

// RW 0x8A04DA
int ProductionUpdate::totalProductionFrames(const ProductionEntry &e, Player *player) const
{
	if (e.type == PRODUCTION_UNIT)
	{
		Object *obj = getObject();
		const int frames = BuildAssistant::calcTimeToBuild(*e.objectToProduce, player, obj, -1, obj->logic().productionSettings(), obj->logic());
		return frames < 1 ? 1 : frames;
	}
	if (e.type == PRODUCTION_UPGRADE && e.upgradeToResearch)
	{
		const int frames = e.upgradeToResearch->calcTimeToBuild(player); // RW 0x66F1A8
		return frames > 0 ? frames : 1;
	}
	if (e.type == PRODUCTION_BUILD_INDEX && player)
	{
		const int frames = player->heroes().framesForProductionID(getObject()->logic(), *player, e.productionID, getObject()); // RW 0x780B72 (lane HERO-1)
		return frames > 0 ? frames : 1;
	}
	return 1;
}

void ProductionUpdate::flushModelConditions()
{
	if (m_flagsDirty)
	{
		getObject()->clearAndSetModelConditionFlags(m_clearFlags, m_setFlags);
		m_clearFlags.fill(0);
		m_setFlags.fill(0);
		m_flagsDirty = false;
	}
}

// RW 0x8A0B2F: the door state machine, every comparison `now - frame > time` in unsigned frames
void ProductionUpdate::updateDoors()
{
	const UnsignedInt now = getObject()->logic().getFrame();
	const DoorBits &bits = doorBits();
	for (int i = 0; i < DOOR_COUNT_MAX; ++i)
	{
		DoorInfo &d = m_doors[i];
		if (d.openedFrame)
		{
			if (now - d.openedFrame > m_data->m_doorOpeningTime)
			{
				d.openedFrame = 0;
				d.waitOpenFrame = now;
				setBit(m_clearFlags, bits.opening[i], true);
				setBit(m_setFlags, bits.opening[i], false);
				setBit(m_setFlags, bits.waitingOpen[i], true);
				m_flagsDirty = true;
			}
		}
		else if (d.waitOpenFrame)
		{
			if (now - d.waitOpenFrame > m_data->m_doorWaitOpenTime && !d.holdOpen)
			{
				d.waitOpenFrame = 0;
				d.closedFrame = now;
				setBit(m_clearFlags, bits.waitingOpen[i], true);
				setBit(m_setFlags, bits.waitingOpen[i], false);
				setBit(m_setFlags, bits.closing[i], true);
				m_flagsDirty = true;
			}
		}
		else if (d.closedFrame && !d.holdOpen)
		{
			if (now - d.closedFrame > m_data->m_doorClosingTime)
			{
				d.closedFrame = 0;
				setBit(m_clearFlags, bits.closing[i], true);
				setBit(m_setFlags, bits.closing[i], false);
				m_flagsDirty = true;
			}
		}
	}
}

// RW 0x8A1B9F
UpdateSleepTime ProductionUpdate::update()
{
	Object *us = getObject();
	GameLogic &logic = us->logic();
	const UnsignedInt now = logic.getFrame();
	const DoorBits &bits = doorBits();
	// (the pending template name set RW +0x130 (RW 0x8A1B52) is filled by a slot whose caller is not located)
	pauseHeroEntries(); // RW 0x8A0669 (lane HERO-1)
	ProductionEntry *entry = currentEntry();
	Player *player = us->getControllingPlayer();
	// RW 0x8A1C25 .. 0x8A1D4F: the speed bonus model condition and audio loop belong to the player bonus map and the audio lane (S-203)
	if (m_data->m_numDoorAnimations > 0)
	{
		updateDoors();
	}
	if (m_heroCountdown > 0)
	{
		// RW 0x8A1D60 (esi = module + 0x10, so [esi + 0x118] is module + 0x128): the only counter decremented at the top of the update is the hero
		// countdown; the post-exit delay (module + 0x118) counts down inside the completion (RW 0x8A2EEE) (lane HERO-1, PROD-1's reading corrected)
		--m_heroCountdown;
	}
	if (m_constructionCompleteFrame)
	{
		if (now - m_constructionCompleteFrame > m_data->m_constructionCompleteDuration)
		{
			m_constructionCompleteFrame = 0;
			setBit(m_clearFlags, bits.constructionComplete, true);
			setBit(m_setFlags, bits.constructionComplete, false);
			m_flagsDirty = true;
		}
	}
	flushModelConditions();
	if (!entry)
	{
		return UPDATE_SLEEP_NONE;
	}
	static const unsigned kSold = statusBit("SOLD");
	if (us->testStatus(kSold))
	{
		return UPDATE_SLEEP_NONE;
	}
	if (!player)
	{
		removeFromProductionQueue(entry);
		return UPDATE_SLEEP_NONE;
	}
	if ((entry->type == PRODUCTION_UNIT || entry->type == PRODUCTION_BUILD_INDEX) && m_exitingObjectID == INVALID_ID && m_postExitDelay == 0)
	{
		// RW 0x8A1E02 .. 0x8A1E82
		const bool allowed = BuildAssistant::playerAllowedToBuild(*player, entry->objectToProduce, logic);
		const bool commandPoints = BuildAssistant::commandPointsAvailable(*player, *entry->objectToProduce, logic);
		if (!((allowed && commandPoints) || entry->flag34))
		{
			return UPDATE_SLEEP_NONE; // blocked: no progress (the EVA message to the local player is the audio lane's)
		}
	}
	if (entry->type == PRODUCTION_UPGRADE)
	{
		// RW 0x8A1E8D: an OBJECT upgrade the producer is no longer affected by is cancelled (slot 4)
		const UpgradeTemplate *u = entry->upgradeToResearch;
		if (u && u->getUpgradeType() == UPGRADE_TYPE_OBJECT && !us->affectedByUpgrade(u))
		{
			cancelUpgrade(u);
			return UPDATE_SLEEP_NONE;
		}
	}
	// RW 0x8A1EBC: the progress rate is 1.0f modified by the object's attribute modifiers of type 0xD (RW 0x68C82D; not ported: S-203)
	float rate = 1.0f;
	entry->framesUnderConstruction = SimMath::addf32(entry->framesUnderConstruction, rate);
	const int total = totalProductionFrames(*entry, player);
	entry->percentComplete = SimMath::mulf32(SimMath::divf32(entry->framesUnderConstruction, (float)total), 100.0f); // RW 0xBD88D8 = 100.0f; the divide before the multiply
	entry->percentPerFrame = SimMath::divf32(100.0f, (float)total);
	if (entry->type == PRODUCTION_BUILD_INDEX)
	{
		// RW 0x8A1F12 .. 0x8A1F3B (lane HERO-1): the record's progress (not the entry's percent) and no hero countdown
		const int index = player->heroes().findIndex(*entry->objectToProduce, entry->productionID, 0);
		if (player->heroes().progressAt(logic, *player, index, us) < 1.0 || m_heroCountdown != 0)
		{
			return UPDATE_SLEEP_NONE;
		}
	}
	else if (entry->percentComplete < 100.0f)
	{
		return UPDATE_SLEEP_NONE;
	}
	if (entry->type == PRODUCTION_UPGRADE)
	{
		completeUpgrade(entry, player);
		return UPDATE_SLEEP_NONE;
	}
	completeUnit(entry, player, now);
	return UPDATE_SLEEP_NONE;
}

// RW 0x8A1F8A .. 0x8A2EC4
void ProductionUpdate::completeUnit(ProductionEntry *entry, Player *player, UnsignedInt now)
{
	Object *us = getObject();
	GameLogic &logic = us->logic();
	const DoorBits &bits = doorBits();
	static const unsigned kLeavingFactory = statusBit("IS_LEAVING_FACTORY");
	static const int kGiantBird = ObjectTemplateInfoBuilder::kindOfIndex("GIANT_BIRD");
	static const int kInvulnerable = mcBit("INVULNERABLE");
	static const int kJustBuilt = mcBit("JUST_BUILT");
	static const int kComingOut = mcBit("COMING_OUT_OF_FACTORY");
	static const int kPreparing = mcBit("PREPARING");
	ExitInterface *exit = us->getObjectExitInterface();
	if (!exit)
	{
		logic.reportError("ProductionUpdate: cannot create " + entry->objectToProduce->getName() + ": producer " + us->getTemplate()->getName() + " has no exit interface");
		removeFromProductionQueue(entry);
		return;
	}
	const int numberToTry = entry->quantityRemaining();
	for (int i = 0; i < numberToTry; ++i)
	{
		int door = entry->exitDoor;
		if (kGiantBird >= 0 && MaskTest(logic.templateInfo(entry->objectToProduce->getFinalOverride()).kindOf, (unsigned)kGiantBird))
		{
			door = DOOR_NONE_NEEDED; // RW 0x8A1FC7: tt + 0x121 & 0x80
		}
		if (door == DOOR_NONE_AVAILABLE)
		{
			door = exit->reserveDoorForExit(entry->objectToProduce, nullptr);
			entry->exitDoor = door;
			if (door == DOOR_NONE_AVAILABLE)
			{
				continue;
			}
		}
		DoorInfo *doorInfo = (door >= 0 && door < DOOR_COUNT_MAX) ? &m_doors[door] : nullptr;
		if (m_data->m_numDoorAnimations > 0 && doorInfo)
		{
			if (doorInfo->openedFrame == 0 && doorInfo->waitOpenFrame == 0 && doorInfo->closedFrame == 0)
			{
				doorInfo->openedFrame = now;
				setBit(m_setFlags, bits.opening[door], true);
				m_flagsDirty = true;
			}
			else if (doorInfo->waitOpenFrame != 0)
			{
				doorInfo->waitOpenFrame = now;
			}
			else if (doorInfo->closedFrame != 0)
			{
				doorInfo->waitOpenFrame = now;
				setBit(m_clearFlags, bits.opening[door], true);
				setBit(m_clearFlags, bits.closing[door], true);
				setBit(m_setFlags, bits.opening[door], false);
				setBit(m_setFlags, bits.closing[door], false);
				setBit(m_setFlags, bits.waitingOpen[door], true);
				m_flagsDirty = true;
			}
		}
		if (m_constructionCompleteFrame == 0)
		{
			m_constructionCompleteFrame = now;
			setBit(m_setFlags, bits.constructionComplete, true);
			m_flagsDirty = true;
		}
		// RW 0x8A2457 .. 0x8A247F: with door animations the object is made only once the door waits open
		if (m_data->m_numDoorAnimations > 0 && doorInfo && doorInfo->waitOpenFrame == 0)
		{
			continue;
		}
		Object *newObj = nullptr;
		if (m_exitingObjectID == INVALID_ID)
		{
			Coord3D pos{};
			float angle = 0.0f;
			const bool hasPos = exit->getExitPosition(&pos, &angle);
			const bool revive = entry->type == PRODUCTION_BUILD_INDEX; // lane HERO-1 (RW 0x8A24DC .. 0x8A25E2)
			int heroLevelCap = 0;
			if (revive)
			{
				// RW 0x8A24E4: the record's level cap (+ 0xD8) is read before the record goes; RW 0x78142F makes the object at the exit position
				if (const HeroRecord *r = player->heroes().findByProductionID(entry->productionID))
				{
					heroLevelCap = r->levelCap;
				}
				newObj = HeroSystem::produce(logic, *player, entry->productionID, pos);
				if (!newObj)
				{
					removeFromProductionQueue(entry); // RW 0x8A2513 -> 0x8A2EF6
					return;
				}
				newObj->setProducer(us); // RW 0x8A2788 (RW 0x625E0A, the object's group, is not ported)
			}
			else
			{
				ObjectStatusMaskType status{}; // RW 0x8A25DC: bit 55 CREATE_DRAWABLE_WITH_LOW_DETAIL when the AIData flag at +0xBB is set (not ported)
				logic.setPendingProducer(us->getID());
				newObj = logic.newObject(entry->objectToProduce, player->getDefaultTeam(), status);
				logic.setPendingProducer(INVALID_ID);
				if (!newObj)
				{
					logic.reportError("ProductionUpdate: newObject made nothing for " + entry->objectToProduce->getName());
					removeFromProductionQueue(entry);
					return;
				}
				// RW 0x8A2750: the producer's colour index goes to the new object (not ported); RW 0x8A275F UnitInvulnerableTime
				if (m_data->m_unitInvulnerableTime > 0)
				{
					newObj->setSpecialModelConditionState(kInvulnerable, m_data->m_unitInvulnerableTime);
				}
				// RW 0x8A276C .. 0x8A2774: cvtsi2ss [entry + 0x28]; movss [newObject + 0x33C]: the object remembers what the entry cost (RefundDie reads it). The entry's own
				// cost is the stored one (a horde's entry is zeroed below once the horde exists, so its members remember 0, as retail's do)
				newObj->setBuildCostPaid(NumericState::sseFromInt32(entry->cost));
			}
			m_exitingObjectID = newObj->getID();
			// RW 0x8A27D4 .. 0x8A283B: VeteranUnitsFromVeteranFactory and the XP for the producer need the experience tracker (S-203)
			if (hasPos)
			{
				newObj->setOrientation(angle);
				// RW 0x8A28BC: TerrainLogic::isUnderwater(x, y, &z) writes the water height into z over standing water; no ground snap
				if (const TerrainLogic *terrain = logic.terrain())
				{
					float waterZ = 0.0f;
					if (terrain->getStandingWaterHeight(pos.x, pos.y, waterZ))
					{
						pos.z = waterZ;
					}
				}
				newObj->setPosition(&pos);
			}
			newObj->setStatus(kLeavingFactory, true); // RW 0x8A28D8: IS_LEAVING_FACTORY
			float fade = 0.0f; // BuildFadeInOnCreateTime, tt + 0x354
			if (const FieldValue *v = newObj->getTemplate()->findField("BuildFadeInOnCreateTime"))
			{
				if (const float *f = std::get_if<float>(v))
				{
					fade = *f;
				}
			}
			// lane MOVE-3 r3 (S-202): RW 0x8A28E0 .. 0x8A291D: a unit that fades in (BuildFadeInOnCreateTime > 0, COMISS against 0 RW 0xC1B594) and an exit with a
			// natural rally point (exit slot 0x24, getNaturalRallyPoint(out, true)) ask the allies on the cell line from the producer's position to that point to
			// move away (RW 0x6F85A6, the queue exit's call, RW 0x8A41D9; the ignored obstacle is the new unit's own, RW 0x662DA5)
			if (fade > 0.0f)
			{
				Coord3D natural;
				AIWorld *world = logic.aiWorld();
				AIUpdateInterface *nai = newObj->getAIUpdateInterface();
				if (exit->getNaturalRallyPoint(&natural, true) && world && nai)
				{
					world->moveAlliesAwayFromDestination(*newObj, *us->getPosition(), natural, (ObjectID)nai->mover().ignoredObstacleID());
				}
			}
			// RW 0x8A2944 .. 0x8A29EC: the player's upgrades are applied to the new unit (UPGRADE-1), voice / EVA events (audio), the player's unit created
			// hook, then CreateModule::onBuildComplete on every create module
			for (const std::unique_ptr<BehaviorModule> &m : newObj->modules())
			{
				if (CreateModuleInterface *create = m->getCreate())
				{
					create->onBuildComplete();
				}
			}
			// RW 0x8A29F1 .. 0x8A2A4E: for the first unit of the entry (quantityProduced, entry + 0x24, still 0) the unit's VoiceCreated (voice event 0x7DA with
			// the producer in the info): client audio, fire-and-forget (AUDIO-2)
			if (entry->quantityProduced == 0)
			{
				AudioApi::postUnitVoice(0x7DA, newObj->getID(), getObject()->getID());
			}
			// RW 0x8A2A8C: BuildFadeInOnCreateTime (tt + 0x354, read above) times 5 frames delays the exit and holds the unit disabled
			m_postExitDelay = (unsigned)SimMath::truncToInt32(SimMath::mulf32(fade, (float)LOGICFRAMES_PER_SECOND));
			newObj->setSpecialModelConditionState(kJustBuilt, m_postExitDelay);
			newObj->setSpecialModelConditionState(kComingOut, 5);
			if (m_postExitDelay > 0)
			{
				newObj->setDisabled(3, now + m_postExitDelay); // RW 0x8A2AB5: DISABLED_HELD until the fade in ends
			}
			// RW 0x8A2AF3: the BuildFadeInOnCreateList extra objects (a field of names): each is made at the exit and told COMING_OUT_OF_FACTORY
			if (const FieldValue *v = newObj->getTemplate()->findField("BuildFadeInOnCreateList"))
			{
				if (const std::vector<std::string> *names = std::get_if<std::vector<std::string>>(v))
				{
					m_extraObjects.clear();
					for (const std::string &n : *names)
					{
						const ThingTemplate *extra = logic.things().findTemplate(n);
						if (!extra)
						{
							continue;
						}
						Object *x = logic.newObject(extra, player->getDefaultTeam(), ObjectStatusMaskType{});
						if (x)
						{
							if (hasPos)
							{
								x->setPosition(&pos);
								x->setOrientation(angle);
							}
							x->setSpecialModelConditionState(kComingOut, 5);
							m_extraObjects.push_back(x->getID());
						}
					}
					m_extrasProcessed = false;
				}
			}
			// RW 0x8A2BEB .. 0x8A2C3E (lane HERO-1): a revived hero with a RespawnUpdate plays its (re)spawn (RW 0x8B38A2 with the producer), gets the record's level cap,
			// and the producer waits 4 * LOGICFRAMES_PER_SECOND frames before the next hero; the hero has NO_COLLISIONS until then (RW 0x6907D7)
			if (revive)
			{
				if (RespawnUpdate *ru = dynamic_cast<RespawnUpdate *>(newObj->findModule("RespawnUpdate")))
				{
					ru->onRevived(us);
					if (ExperienceTracker *t = newObj->getExperienceTracker())
					{
						t->setLevelCap(heroLevelCap);
					}
					m_heroCountdown = (unsigned)LOGICFRAMES_PER_SECOND * 4u;
					static const unsigned kNoCollisions = statusBit("NO_COLLISIONS");
					newObj->setStatus(kNoCollisions, true);
					newObj->setNoCollisionsUntil(now + (UnsignedInt)LOGICFRAMES_PER_SECOND * 4u);
				}
			}
			// RW 0x8A2C6A: a horde object turns the entry into the entry of its members
			if (HordeContainInterface *hci = hordeOf(newObj))
			{
				const std::string member = hci->getPayloadMemberTemplateName();
				const ThingTemplate *memberTemplate = member.empty() ? nullptr : logic.things().findTemplate(member);
				if (memberTemplate)
				{
					entry->cost = 0;
					entry->objectToProduce = memberTemplate;
					entry->quantityTotal = hci->getSlotCapacity() + 1;
					entry->flag34 = true;
				}
			}
		}
		else
		{
			newObj = logic.findObjectByID(m_exitingObjectID);
			if (!newObj)
			{
				m_exitingObjectID = INVALID_ID;
				continue;
			}
		}
		if (!m_extrasProcessed)
		{
			m_extrasProcessed = true; // RW 0x8A2CD6: the extras' drawables fade in (client)
		}
		if (m_postExitDelay != 0)
		{
			--m_postExitDelay; // RW 0x8A2D75 -> 0x8A2EEE: the exit waits for the fade in, one frame per completion pass, and the pass ends
			return;
		}
		// RW 0x8A2D82: finalize the exit
		static const int kJustBuiltBit = kJustBuilt;
		if (newObj->testModelCondition(kJustBuiltBit))
		{
			newObj->setModelConditionState(kJustBuiltBit, false);
		}
		if (m_data->m_specialPrepModelConditionTime > 0)
		{
			newObj->setSpecialModelConditionState(kPreparing, m_data->m_specialPrepModelConditionTime);
		}
		newObj->clearDisabled(3); // RW 0x8A2DBD
		m_exitingObjectID = INVALID_ID;
		if (!newObj->findModule("RespawnUpdate"))
		{
			exit->exitObjectViaDoor(newObj, (ExitDoorType)(door >= 0 ? door : DOOR_1));
		}
		++entry->quantityProduced;
		entry->exitDoor = DOOR_NONE_AVAILABLE;
		if (entry->quantityRemaining() != 0)
		{
			continue;
		}
		break; // RW 0x8A2DF9: the entry is complete (the producer's contain notification RW 0x8A2E18 is the horde lane's)
	}
	if (entry->quantityRemaining() == 0)
	{
		removeFromProductionQueue(entry);
		exit->releaseLastExit(); // RW 0x8A2EE9: slot 11 after the last entry's removal
	}
}

void ProductionUpdate::crc(StateHasher &h) const
{
	UpdateModule::crc(h);
	h.addU32((std::uint32_t)m_queue.size());
	for (const auto &e : m_queue)
	{
		h.addU32((std::uint32_t)e->type);
		h.addU32(e->objectToProduce ? (std::uint32_t)e->objectToProduce->getTemplateID() : 0u);
		h.addU32(e->productionID);
		h.addFloat(e->percentComplete);
		h.addFloat(e->percentPerFrame);
		h.addFloat(e->framesUnderConstruction);
		h.addI32(e->quantityTotal);
		h.addI32(e->quantityProduced);
		h.addI32(e->cost);
		h.addI32(e->exitDoor);
		h.addI32(e->value30);
		h.addBool(e->flag34);
		h.addU32(e->hordeTarget);
		h.addString(e->name);
		h.addBool(e->flag44);
		h.addI32(e->upgradeToResearch ? e->upgradeToResearch->getMaskBit() : -1);
	}
	h.addU32(m_uniqueID);
	h.addU32(m_constructionCompleteFrame);
	for (const DoorInfo &d : m_doors)
	{
		h.addU32(d.openedFrame);
		h.addU32(d.waitOpenFrame);
		h.addU32(d.closedFrame);
		h.addBool(d.holdOpen);
	}
	for (std::uint32_t w : m_clearFlags)
	{
		h.addU32(w);
	}
	for (std::uint32_t w : m_setFlags)
	{
		h.addU32(w);
	}
	h.addBool(m_flagsDirty);
	h.addU32(m_postExitDelay);
	h.addBool(m_extrasProcessed);
	h.addU32(m_exitingObjectID);
	h.addU32((std::uint32_t)m_extraObjects.size());
	for (ObjectID id : m_extraObjects)
	{
		h.addU32(id);
	}
	h.addBool(m_cancellingAll);
	h.addBool(m_factoryDisabled);
	h.addU32(m_heroCountdown); // lane HERO-1
}
