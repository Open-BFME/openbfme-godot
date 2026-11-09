// OpenBFME. GPL-3.0.
// BFME1 fields follow Open-BFME-1's field table VA 0x010BC738 and BloodthirstyUpdateModuleDataConstructor.cpp;
// Update/Rva00287290Update.cpp awards XP on each accepted sacrifice, not at a NumToSacrifice quota.

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "GameLogic/Module/BloodthirstyUpdate.h"

#include "Common/Audio/AudioRequests.h"
#include "Common/BfmeGame.h"
#include "Common/GameCommon.h"
#include "Common/Player.h"
#include "Common/StateHash.h"
#include "GameClient/ControlBarCommands.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ObjectFilter.h"
#include "GameLogic/ObjectFilterMatch.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/SnapshotXfer.h"

#include <cstddef>
#include <memory>

namespace
{
void parseSacrificeFilter(INI *ini, void *instance, void *, const void *userData)
{
	BloodthirstyUpdateModuleData *data = static_cast<BloodthirstyUpdateModuleData *>(instance);
	data->m_sacrificeFilter = std::make_shared<ObjectFilter>();
	ParseObjectFilter(ini, instance, data->m_sacrificeFilter.get(), userData);
}

const FieldParse kBloodthirstyUpdate[] = {
	{ "SacrificeFilter", parseSacrificeFilter, nullptr, 0 },
	{ "ExperienceModifier", INI::parseReal, nullptr, (int)offsetof(BloodthirstyUpdateModuleData, m_experienceModifier) },
	{ "InitiateVoice", INI::parseAsciiString, nullptr, (int)offsetof(BloodthirstyUpdateModuleData, m_initiateVoice) },
	{ nullptr, nullptr, nullptr, 0 }
};

const FieldParse kBfme1BloodthirstyUpdate[] = {
	{ "NumToSacrifice", INI::parseUnsignedInt, nullptr, (int)offsetof(BloodthirstyUpdateModuleData, m_numToSacrifice) },
	{ "InitiateVoice2", INI::parseAsciiString, nullptr, (int)offsetof(BloodthirstyUpdateModuleData, m_initiateVoice2) },
	{ nullptr, nullptr, nullptr, 0 }
};
const Bfme1FieldRowsRegistration kBfme1BloodthirstyRegistration(kBloodthirstyUpdate, kBfme1BloodthirstyUpdate);

unsigned statusIndex(const char *name)
{
	return (unsigned)CombatNames::status(name);
}

// RW 0x850EEC: contained members before the horde object.
void setModelConditionForHorde(Object &object, bool enabled)
{
	Object *horde = object.getHordeObject(false);
	if (!horde)
	{
		return;
	}
	if (ContainModuleInterface *contain = horde->getContain())
	{
		if (const ContainModuleInterface::ContainedItemsList *members = contain->getContainedItemsList())
		{
			for (Object *member : *members)
			{
				if (member)
				{
					member->setModelConditionState(0x40, enabled);
				}
			}
		}
	}
	horde->setModelConditionState(0x40, enabled);
}

} // namespace

void BloodthirstyUpdateModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kBloodthirstyUpdate);
}

BloodthirstyUpdate::BloodthirstyUpdate(Thing *thing, const BloodthirstyUpdateModuleData *data)
	: UpdateModule(thing, data)
	, m_data(data)
{
	setWakeFrame(getObject(), UPDATE_SLEEP_NONE);
}

BloodthirstyUpdate *BloodthirstyUpdate::find(Object &object)
{
	return dynamic_cast<BloodthirstyUpdate *>(object.findModule("BloodthirstyUpdate"));
}

bool BloodthirstyUpdate::matchesFilter(const Object &candidate) const
{
	return !m_data->m_sacrificeFilter || !ObjectFilterMatch::isValid(m_data->m_sacrificeFilter.get())
		|| ObjectFilterMatch::allows(getObject()->logic(), *m_data->m_sacrificeFilter, candidate, getObject()->getControllingPlayer());
}

bool BloodthirstyUpdate::canSacrifice(Object *candidate) const
{
	Object *horde = candidate ? candidate->getHordeObject(false) : nullptr;
	BloodthirstyUpdate *victim = horde ? find(*horde) : nullptr;
	if (!horde || !victim)
	{
		return false;
	}
	if (AIUpdateInterface *ai = horde->getAIUpdateInterface(); ai && !ai->isIdle())
	{
		return false;
	}
	if (!matchesFilter(*horde))
	{
		return false;
	}
	return (victim->m_sacrificerID == INVALID_ID || victim->m_sacrificerID == getObject()->getID()) && victim->m_bestTargetID == INVALID_ID;
}

bool BloodthirstyUpdate::isValidSacrifice(Object *candidate) const
{
	Object *horde = candidate ? candidate->getHordeObject(false) : nullptr;
	BloodthirstyUpdate *victim = horde ? find(*horde) : nullptr;
	return horde && victim && matchesFilter(*horde) && victim->m_sacrificerID == getObject()->getID() && m_bestTargetID == horde->getID();
}

const CommandButton *BloodthirstyUpdate::sacrificeButton(const Object &object)
{
	const CommandSet *set = TheCommandStore ? TheCommandStore->findCommandSet(object.getCommandSetName()) : nullptr;
	for (int i = 0; set && i < CommandSet::MAX_BUTTONS; ++i)
	{
		const CommandButton *b = set->getCommandButton(i);
		if (b && b->m_command == GUI_COMMAND_BLOODTHIRSTY)
		{
			return b;
		}
	}
	return nullptr;
}

bool BloodthirstyUpdate::relationshipAllowed(const CommandButton &button, const Player &player, const Object &target)
{
	if (!(button.m_options & 7))
	{
		return true; // RW 0x81E60B: no relationship bit, no relationship test
	}
	const Relationship rel = target.getTeam() ? player.getRelationship(target.getTeam()) : NEUTRAL;
	const std::uint32_t bit = rel == ENEMIES ? COMMAND_OPTION_NEED_TARGET_ENEMY_OBJECT : rel == NEUTRAL ? COMMAND_OPTION_NEED_TARGET_NEUTRAL_OBJECT : COMMAND_OPTION_NEED_TARGET_ALLY_OBJECT;
	return (button.m_options & bit) != 0;
}

bool BloodthirstyUpdate::isSacrificeTargetFor(const Player &issuer, Object *target) const
{
	const CommandButton *button = sacrificeButton(*getObject());
	Object *ownHorde = getObject()->getHordeObject(false);
	Object *targetHorde = target ? target->getHordeObject(false) : nullptr;
	return button && ownHorde && targetHorde && targetHorde != ownHorde && relationshipAllowed(*button, issuer, *target) && canSacrifice(target);
}

void BloodthirstyUpdate::sacrificeHorde(Object *victim, CommandSourceType source)
{
	Object *self = getObject();
	Object *horde = victim ? victim->getHordeObject(false) : nullptr;
	BloodthirstyUpdate *victimModule = horde ? find(*horde) : nullptr;
	AIUpdateInterface *ai = self ? self->getAIUpdateInterface() : nullptr;
	if (!self || !victim || !horde || !victimModule || victimModule->m_sacrificerID != INVALID_ID || !ai)
	{
		return;
	}
	m_bestTargetID = horde->getID();
	victimModule->m_sacrificerID = self->getID();
	ai->aiForceAttackObject(victim, source);
	self->setStatusAcrossHorde(statusIndex("BLOODTHIRSTY"), true);
	horde->setStatusAcrossHorde(statusIndex("IGNORE_AI_COMMAND"), true);
	AudioApi::postUnitVoice(0x7e9, self->getID());
}

void BloodthirstyUpdate::addSacrifice(Object *victim)
{
	Object *horde = victim ? victim->getHordeObject(false) : nullptr;
	if (!horde || !isValidSacrifice(victim))
	{
		return;
	}
	++m_kills;
	if (m_kills == 1)
	{
		setModelConditionForHorde(*horde, true);
	}
	Object *selfHorde = getObject()->getHordeObject(false);
	if (selfHorde)
	{
		selfHorde->grantExperienceForKill(*victim, true, m_data->m_experienceModifier);
	}
	ContainModuleInterface *contain = horde->getContain();
	if (contain && contain->getContainCount() == 1)
	{
		cancelSacrifice();
	}
}

void BloodthirstyUpdate::clearVictimState(Object &victim)
{
	Object *horde = victim.getHordeObject(false);
	if (!horde)
	{
		return;
	}
	setModelConditionForHorde(*horde, false);
	horde->setStatusAcrossHorde(statusIndex("IGNORE_AI_COMMAND"), false);
	if (AIUpdateInterface *ai = horde->getAIUpdateInterface())
	{
		ai->aiIdle(CMD_FROM_AI);
	}
}

void BloodthirstyUpdate::cancelSacrifice()
{
	GameLogic &logic = getObject()->logic();
	Object *target = logic.findObjectByID(m_bestTargetID);
	BloodthirstyUpdate *victim = target ? find(*target) : nullptr;
	if (!target || !victim || !isValidSacrifice(target))
	{
		return;
	}
	victim->m_sacrificerID = INVALID_ID;
	m_bestTargetID = INVALID_ID;
	m_kills = 0;
	getObject()->setStatusAcrossHorde(statusIndex("BLOODTHIRSTY"), false);
	clearVictimState(*target);
}

UpdateSleepTime BloodthirstyUpdate::update()
{
	if (m_sacrificerID != INVALID_ID)
	{
		Object *sacrificer = getObject()->logic().findObjectByID(m_sacrificerID);
		const unsigned bloodthirsty = statusIndex("BLOODTHIRSTY");
		if (!sacrificer || sacrificer->isEffectivelyDead() || !sacrificer->testStatus(bloodthirsty))
		{
			m_sacrificerID = INVALID_ID;
			clearVictimState(*getObject());
		}
	}
	return UPDATE_SLEEP((int)(LOGICFRAMES_PER_SECOND * 2 + 1));
}

const std::string &BloodthirstyUpdate::initiateVoice() const
{
	return m_data->m_initiateVoice;
}

const std::string &BloodthirstyUpdate::initiateVoice2() const
{
	return m_data->m_initiateVoice2;
}

void BloodthirstyUpdate::crc(StateHasher &hasher) const
{
	UpdateModule::crc(hasher);
	hasher.addU32(m_bestTargetID);
	hasher.addU32(m_sacrificerID);
	hasher.addU32(m_kills);
}

void BloodthirstyUpdate::xfer(Xfer &x)
{
	SnapshotXfer::version(x, 1, 1);
	xferUpdateModule(x);
	x.xferObjectID(&m_bestTargetID);
	x.xferObjectID(&m_sacrificerID);
	x.xferUnsignedInt(&m_kills);
}

bool cancelBloodthirstyCommand(Object &object, int command, Object *target)
{
	if (!object.testStatus(statusIndex("BLOODTHIRSTY")))
	{
		return false;
	}
	BloodthirstyUpdate *module = BloodthirstyUpdate::find(object);
	if (!module)
	{
		return false;
	}
	if (command != 0xc && command != 0x36)
	{
		module->cancelSacrifice();
		return false;
	}
	if (target && !module->isValidSacrifice(target))
	{
		module->cancelSacrifice();
	}
	return false;
}
