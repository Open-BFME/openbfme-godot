// OpenBFME. GPL-3.0.
// See GameLogic/Module/CommandButtonHuntUpdate.h for the target facts, the donors and what is inference.

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "GameLogic/Module/CommandButtonHuntUpdate.h"

#include "Common/SpecialPower.h"
#include "Common/StateHash.h"
#include "Common/Thing/ModuleFactory.h"
#include "Common/Thing/ThingTemplate.h"
#include "Common/Xfer.h"
#include "GameClient/ControlBarCommands.h"
#include "GameLogic/ActionManager.h"
#include "GameLogic/Combat/CombatQueries.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/SpecialAbilityModules.h"
#include "GameLogic/Module/SpecialPowerModules.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/ObjectCommandButton.h"
#include "GameLogic/Object/PartitionManager.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/ScriptEngine/AttackPriority.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/SnapshotXfer.h"

#include <cmath>
#include <cstddef>
#include <stdexcept>

namespace
{
typedef CommandButtonHuntUpdateModuleData Data;

// RW 0xC653F8
const FieldParse kFields[] = {
	{ "ScanRate", INI::parseDurationUnsignedInt, nullptr, (int)offsetof(Data, m_scanFrames) }, // RW 0x73A429
	{ "ScanRange", INI::parseReal, nullptr, (int)offsetof(Data, m_scanRange) },                // RW 0x42ED00
	{ nullptr, nullptr, nullptr, 0 }
};
} // namespace

void CommandButtonHuntUpdateModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kFields);
}

void CommandButtonHuntUpdate::registerModule(ModuleFactory &modules)
{
	static const char kName[] = "CommandButtonHuntUpdate";
	modules.bindTypedData<Data>(kName, MODULETYPE_BEHAVIOR);
	modules.bindModuleProc(kName, MODULETYPE_BEHAVIOR, [](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
		const Data *typed = dynamic_cast<const Data *>(data);
		if (!typed)
		{
			throw std::logic_error(std::string(kName) + ": the module data is not typed");
		}
		return std::make_unique<CommandButtonHuntUpdate>(thing, typed);
	});
}

// RW 0x898A5D
CommandButtonHuntUpdate::CommandButtonHuntUpdate(Thing *thing, const Data *data)
	: UpdateModule(thing, data)
	, m_data(data)
{
	setWakeFrame(getObject(), UPDATE_SLEEP_FOREVER); // RW 0x850C32(obj, 0x3FFFFFFF)
}

const SpecialPowerTemplate *CommandButtonHuntUpdate::buttonPower() const
{
	if (!m_commandButton || m_commandButton->m_specialPowerName.empty() || !TheSpecialPowerStore)
	{
		return nullptr;
	}
	return TheSpecialPowerStore->findSpecialPowerTemplate(m_commandButton->m_specialPowerName);
}

// the object's command set (RW 0x69156B -> 0x71EFA2), its 33 slots (RW 0x80C837, bound 0x21): the first button whose name equals `name`;
// setCommandButton (RW 0x898EE8) also skips an empty name, the xfer's load (RW 0x898B7C) does not
const CommandButton *CommandButtonHuntUpdate::findInCommandSet(const std::string &name, bool skipEmptyNames) const
{
	const CommandSet *set = TheCommandStore ? TheCommandStore->findCommandSet(getObject()->getCommandSetName()) : nullptr;
	for (int i = 0; set && i < CommandSet::MAX_BUTTONS; ++i)
	{
		const CommandButton *b = set->getCommandButton(i);
		if (b && !(skipEmptyNames && b->m_name.empty()) && b->m_name == name)
		{
			return b;
		}
	}
	return nullptr;
}

// RW 0x898EE8
void CommandButtonHuntUpdate::setCommandButton(const std::string &buttonName)
{
	Object *obj = getObject();
	m_commandButtonName = buttonName;
	m_commandButton = findInCommandSet(m_commandButtonName, true);
	AIUpdateInterface *ai = obj->getAIUpdateInterface(); // Object + 0x260
	if (!m_commandButton || !ai)
	{
		return;
	}
	ai->aiIdle(CMD_FROM_AI); // RW 0x5E821A(2)
	update();                // the update interface's slot 0 (its answer is not used)
	setWakeFrame(obj, UPDATE_SLEEP_NONE); // RW 0x850C32(obj, 1)
}

// RW 0x89903C
UpdateSleepTime CommandButtonHuntUpdate::update()
{
	AIUpdateInterface *ai = getObject()->getAIUpdateInterface();
	if (!ai || !m_commandButton)
	{
		return UPDATE_SLEEP_FOREVER;
	}
	if (ai->lastCommandSource() != CMD_FROM_AI) // AI vslot 0x23C
	{
		m_commandButton = nullptr;
		m_commandButtonName.clear();
		return UPDATE_SLEEP_FOREVER;
	}
	switch (m_commandButton->m_command) // + 0x14
	{
	case GUI_COMMAND_FIRE_WEAPON:   // 0x17
	case GUI_COMMAND_SWITCH_WEAPON: // 0x1B
		return huntWeapon(*ai);
	case GUI_COMMAND_SPECIAL_POWER: // 0x18
	case GUI_COMMAND_SPELL_BOOK:    // 0x26
		return huntSpecialPower(*ai);
	default:
		return UPDATE_SLEEP_FOREVER;
	}
}

// RW 0x898F8F
UpdateSleepTime CommandButtonHuntUpdate::huntWeapon(AIUpdateInterface &ai)
{
	if (ai.isIdle()) // vslot 0x1B8
	{
		ai.aiHunt(CMD_FROM_AI); // RW 0x6AF150(2)
	}
	ObjectWeapons::setObjectWeaponLock(*getObject(), m_commandButton->m_weaponSlot, LOCKED_TEMPORARILY); // RW 0x69121A(button + 0x80, 1)
	++m_weaponFrames;
	return UPDATE_SLEEP_NONE; // 1
}

// RW 0x898FCE
UpdateSleepTime CommandButtonHuntUpdate::huntSpecialPower(AIUpdateInterface &ai)
{
	Object *obj = getObject();
	const UpdateSleepTime scan = UPDATE_SLEEP((int)m_data->m_scanFrames); // data + 8
	if (!ai.isIdle()) // vslot 0x1B8
	{
		return scan;
	}
	if (const SpecialPowerTemplate *power = buttonPower()) // button + 0x44
	{
		// RW 0x68C461(the final override's type): the first special power update module (behaviour slot 0x64, its vslot 4) driving a power of that
		// type (RW 0x851D10); only SpecialAbilityUpdate answers that slot in the port (as AISpecialPowerUpdate RW 0x68C461)
		const int type = power->getFinalOverride()->getSpecialPowerType();
		const SpecialAbilityUpdate *ability = nullptr;
		for (const std::unique_ptr<BehaviorModule> &m : obj->modules())
		{
			const SpecialAbilityUpdate *u = dynamic_cast<const SpecialAbilityUpdate *>(m.get());
			const SpecialPowerTemplate *ut = u ? u->abilityData()->m_specialPowerTemplate : nullptr;
			if (ut && ut->getSpecialPowerType() == type)
			{
				ability = u;
				break;
			}
		}
		if (!ability)
		{
			return UPDATE_SLEEP_FOREVER;
		}
		if (ability->isActive()) // interface + 0x20, vslot 2
		{
			return scan;
		}
	}
	if (Object *victim = scanClosestTarget()) // RW 0x898C17
	{
		ObjectCommandButton::doCommandButtonAtObject(*obj, m_commandButton, victim, CMD_FROM_AI); // RW 0x697890(button, victim, 2, 0)
		++m_casts;
	}
	return scan;
}

// RW 0x898C17
Object *CommandButtonHuntUpdate::scanClosestTarget()
{
	Object *me = getObject();
	GameLogic &logic = me->logic();
	const SpecialPowerTemplate *power = m_commandButton ? buttonPower() : nullptr;
	if (!power)
	{
		return nullptr;
	}
	const SpecialPowerTemplate &final = *power->getFinalOverride(); // RW 0x688D3C
	const bool isCapture = final.getSpecialPowerType() == 0x1D;
	const bool isPlaceExplosive = final.getSpecialPowerType() == 0x17 || final.getSpecialPowerType() == 0x19;
	// RW 0xC10E20 (alive: Object + 0x458 bit 0 clear), RW 0xC0F374 (the same Object + 0x458 bit 3 as the hunter: never set, S-856), and unless capturing
	// RW 0xC11DC0(me, 1, false): the hunter's relationship to it is ENEMIES
	PartitionFilterFn filters([&](Object &o) { return !o.isEffectivelyDead() && (isCapture || me->getRelationship(o) == ENEMIES); });
	const PartitionHits hits = logic.partition().iterateObjectsInRange(*me->getPosition(), m_data->m_scanRange, FROM_CENTER_2D, { &filters }, ITER_SORTED_NEAR_TO_FAR);
	Object *best = nullptr;
	float effectivePriority = 0.0f; // xorps
	float actualPriority = 0.0f;
	AIUpdateInterface *ai = me->getAIUpdateInterface();
	const AttackPriorityInfo *info = ai ? ai->attackInfo() : nullptr; // AI + 0x70
	if (!SpecialPowerModules::findModule(*me, power)) // RW 0x68C26D
	{
		return nullptr;
	}
	static const int kMine = ObjectTemplateInfoBuilder::kindOfIndex("MINE"); // KindOf 0x37
	const float modifier = logic.settings().attackPriorityDistanceModifier;  // AIData + 0x54
	for (const PartitionHit &hit : hits)
	{
		Object *other = hit.object;
		if (isCapture)
		{
			if (me->getControllingPlayer() == other->getControllingPlayer()) // RW 0x68B678 (both)
			{
				continue;
			}
			if (me->getRelationship(*other) == ALLIES) // RW 0x68D7AB == 2
			{
				continue;
			}
		}
		if (!ActionManager::canDoSpecialPowerAtObject(*me, other, CMD_FROM_AI, power, 0, true)) // RW 0x82D925(me, it, 2, power, 0, 1)
		{
			continue;
		}
		if (isPlaceExplosive)
		{
			// RW 0xA39090(its position, ViewObjectRange, distance type 1, [KindOf MINE (RW 0x444D39 / 0x445139), the hunter's player RW 0xC10E14])
			const Player *mine = me->getControllingPlayer();
			PartitionFilterFn ownMine([&](Object &o) { return kMine >= 0 && o.isKindOf((unsigned)kMine) && o.getControllingPlayer() == mine; });
			if (logic.partition().getClosestObject(*other->getPosition(), final.m_viewObjectRange, FROM_CENTER_3D, { &ownMine }))
			{
				continue;
			}
		}
		// RW 0x66352C(me, it): the squared 2D edge distance; fstp double, CRT sqrt (RW 0xA3CF96), fstp float
		const float distSqr = CombatQueries::edgeDistanceSquared2D(*me, *me->getPosition(), *other, *other->getPosition());
		const float dist = SimMath::fstpDword(SimMath::sqrtd((double)distSqr)); // SimMath: IEEE sqrt, the store as RW's fstp dword
		const float priority = info ? info->getPriority(*me, *other, true) : SimMath::subf32(m_data->m_scanRange, dist); // RW 0x7597E9 (fstp) / subss
		if (priority == 0.0f) // ucomiss [0xC1B594]: exactly zero skips, a NaN goes on
		{
			continue;
		}
		float modPriority = SimMath::subf32(priority, SimMath::divf32(dist, modifier)); // divss, subss
		if (1.0f > modPriority)                                                          // comiss [0xBD1908]
		{
			modPriority = 1.0f;
		}
		if (modPriority > effectivePriority) // comiss; jbe
		{
			effectivePriority = modPriority;
			actualPriority = priority;
			best = other;
		}
		if (modPriority == effectivePriority && priority > actualPriority) // ucomiss (jp: unordered or unequal skips); comiss
		{
			effectivePriority = modPriority;
			actualPriority = priority;
			best = other;
		}
	}
	return best;
}

void CommandButtonHuntUpdate::crc(StateHasher &hasher) const
{
	UpdateModule::crc(hasher);
	hasher.addString(m_commandButtonName);
	hasher.addBool(m_commandButton != nullptr);
	hasher.addU64(m_casts);
	hasher.addU64(m_weaponFrames);
}

// RW 0x898B7C's fields (OpenBFME native snapshot): the name; a load re-finds the button from the command set
void CommandButtonHuntUpdate::xfer(Xfer &x)
{
	SnapshotXfer::version(x, 1, 1);
	xferUpdateModule(x); // RW 0x850C60
	x.xferAsciiString(&m_commandButtonName);
	if (x.isLoading())
	{
		m_commandButton = m_commandButtonName.empty() ? nullptr : findInCommandSet(m_commandButtonName, false);
	}
	SnapshotXfer::u64(x, m_casts);
	SnapshotXfer::u64(x, m_weaponFrames);
}
