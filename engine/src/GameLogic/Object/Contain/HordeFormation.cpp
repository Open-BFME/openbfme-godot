// OpenBFME. GPL-3.0.
// Lane HORDE-2: the formation swap (Command_ToggleFormation, MSG_HORDE_TOGGLE_FORMATION 1107).
//
// TARGET FACTS (RotWK game.dat, caveat S-001; read with Ghidra):
//   * the dispatcher case RW 0x77BE83 (jump table RW 0x77D127, index byte table RW 0x77D1E3): argument 0 is the horde's object id; its HordeContainInterface's slot 0x5C
//     (RW 0x8700AC) must agree, then slot 0x60 (RW 0x86C7F6) swaps; a locally controlled horde sets a UI flag (client);
//   * RW 0x8700AC: false without members; AlternateFormation (module data + 0x1B0) names a template (RW 0x6D1305); its first module whose data is a HordeContain's decides:
//     false when that data is not ThisFormationIsTheMainFormation (+0x1D8) and the member count is below its MinimumHordeSize (+0x270); else true;
//   * RW 0x875FB2 (slot 0x68 with the alternate template): an object of the alternate template is made on the horde's team; RW 0x68C0C5 then SWAPS the two objects' contain
//     modules (the module pointer and the module's object pointer), so the horde object keeps its id, AI, body, selection and transform and gets the alternate HordeContain;
//     the member list is taken from the old contain (the banner carrier, H+0x26C, taken out of it), a new porcupine formation (contain slot 0xF0) idles the horde AI;
//     the new contain adopts the members (slot 0x78 RW 0x876A08: each member is added through slot 0x2C, the ForcedLocomotorSet applied, then the banner check RW 0x8719E4
//     and the formation refresh); the banner carrier joins the new contain (slot 0x74) when it has none, else it is destroyed; the old contain leaves with the other
//     object, which is destroyed.
// INFERENCE (stop S-589): the port makes the alternate HordeContain module directly on the horde object (the alternate object itself would only carry it: no payload is
// created, the slots are built as at creation with the same RandomOffset draws); the members join without being teleported (they walk to their new slots); the
// PRIMARY_FORMATION / ALTERNATE_FORMATION model conditions and the ALTERNATE_FORMATION armor set follow ThisFormationIsTheMainFormation (B1
// BfmeHordeContainApplyMemberFormationState.cpp; the RotWK writer is not read); RW 0x756C87 (the porcupine call), the experience copy (RW 0x79D8EF) and the auto heal refresh
// RW 0x68C213 are not ported (the stance call RW 0x861E6E / 0x8620DD is, lane INTEG-1).

#include "Common/Thing/ModuleFactory.h"
#include "Common/Thing/ThingFactory.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/GameLogicDispatch.h"
#include "GameLogic/GameMessage.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/HordeContain.h"
#include "GameLogic/Module/StancesBehavior.h"
#include "GameLogic/Object/Contain/HordeContainBehaviorData.h"
#include "GameLogic/Object/Contain/HordeContainCore.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"
#include "GameLogic/Object/Object.h"

#include <algorithm>

namespace
{
const char *const kStop =
	"[S-589] formation swap: MSG_HORDE_TOGGLE_FORMATION (RW 0x77BE83) asks RW 0x8700AC (members, the AlternateFormation template's HordeContain: main formation or MinimumHordeSize) "
	"and swaps like RW 0x875FB2 / 0x68C0C5 (the horde object keeps id, AI, selection and transform and takes the alternate HordeContain; members and banner carrier move over, a "
	"porcupine formation idles the AI); INFERENCE: the alternate module is made directly on the horde object (no payload, slots built as at creation), the members walk to their "
	"new slots, the PRIMARY / ALTERNATE_FORMATION conditions and armor set follow ThisFormationIsTheMainFormation (B1); NOT ported: RW 0x756C87, the experience copy, "
	"RW 0x68C213 (the stance call RW 0x8620DD is, lane INTEG-1)";

const ThingTemplate::Nugget *hordeContainNugget(const ThingTemplate &tt)
{
	for (const ThingTemplate::Nugget &n : tt.behaviorModules().nuggets())
	{
		if (dynamic_cast<const HordeContainBehaviorData *>(n.data.get()))
		{
			return &n;
		}
	}
	return nullptr;
}

const ThingTemplate *alternateTemplate(GameLogic &logic, const HordeContainModuleData &data)
{
	return data.m_alternateFormation.empty() ? nullptr : logic.things().findTemplate(data.m_alternateFormation);
}

void applyFormationState(Object &member, bool main)
{
	member.setModelConditionState(CombatNames::modelCondition("PRIMARY_FORMATION"), main);
	member.setModelConditionState(CombatNames::modelCondition("ALTERNATE_FORMATION"), !main);
	member.setArmorSetFlag(CombatNames::armorSetBit("ALTERNATE_FORMATION"), !main);
}
} // namespace

const char *HordeContain::formationStopLine()
{
	return kStop;
}

// RW 0x8700AC
bool HordeContain::canToggleFormation() const
{
	const unsigned count = (unsigned)m_members.size();
	if (count == 0)
	{
		return false;
	}
	const ThingTemplate *alt = alternateTemplate(getObject()->logic(), m_data->horde);
	if (!alt)
	{
		return false;
	}
	if (const ThingTemplate::Nugget *n = hordeContainNugget(*alt->getFinalOverride()))
	{
		const HordeContainModuleData &d = static_cast<const HordeContainBehaviorData *>(n->data.get())->horde;
		if (!d.m_thisFormationIsTheMainFormation && count < d.m_minimumHordeSize)
		{
			return false;
		}
	}
	return true;
}

// RW 0x86C7F6 -> RW 0x875FB2
HordeContain *HordeContain::toggleFormation()
{
	Object *horde = getObject();
	GameLogic &logic = horde->logic();
	const ThingTemplate *alt = alternateTemplate(logic, m_data->horde);
	const ThingTemplate::Nugget *n = alt ? hordeContainNugget(*alt->getFinalOverride()) : nullptr;
	if (!n)
	{
		return nullptr;
	}
	std::unique_ptr<Module> made = logic.modules().newModule(horde, n->name, n->data.get(), MODULETYPE_BEHAVIOR);
	HordeContain *fresh = dynamic_cast<HordeContain *>(made.get());
	if (!fresh)
	{
		logic.reportError("HordeContain formation swap: " + alt->getName() + "'s " + n->name + " did not make a HordeContain");
		return nullptr;
	}
	made.release();
	std::unique_ptr<BehaviorModule> freshOwned(fresh);
	fresh->onObjectCreated(); // the slot table, as at the alternate object's creation (RW 0x877751)
	fresh->m_payloadCreated = true;
	// the members leave the old contain without the "last member gone" destruction; the banner carrier separately
	removeFormationModifiers(); // RW 0x876032: the old contain's AttributeModifiers leave the members (slot 0x1E8; lane COMBAT-3)
	std::vector<Object *> members(m_members.begin(), m_members.end());
	const ObjectID banner = m_bannerCarrier;
	m_members.clear();
	m_producedMembers.clear();
	m_bannerCarrier = 0;
	for (Object *m : members)
	{
		m_core->removeMember(m->getID(), false);
		m->friend_setContainedBy(nullptr);
	}
	// the swap (RW 0x68C0C5): the horde object now carries the alternate contain; the scheduler drops the old module
	UpdateModule *oldUpdate = asUpdateModule();
	std::unique_ptr<BehaviorModule> old = horde->friend_replaceModule(this, std::move(freshOwned));
	logic.friend_replaceUpdateModule(oldUpdate, fresh);
	// a porcupine formation idles the horde's AI (RW 0x8760D7 .. 0x87610B)
	if (fresh->m_data->horde.m_isPorcupineFormation)
	{
		if (AIUpdateInterface *ai = horde->getAIUpdateInterface())
		{
			ai->aiIdle(CMD_FROM_AI);
		}
	}
	// RW 0x876A08: the members join the new contain (they walk to their slots)
	const bool main = fresh->m_data->horde.m_thisFormationIsTheMainFormation;
	Object *bannerObj = banner ? logic.findObjectByID(banner) : nullptr;
	for (Object *m : members)
	{
		if (m == bannerObj)
		{
			continue;
		}
		fresh->acceptCreatedMember(m);
		fresh->m_producedMembers.erase(std::remove(fresh->m_producedMembers.begin(), fresh->m_producedMembers.end(), m->getID()), fresh->m_producedMembers.end());
		applyFormationState(*m, main);
		const int forced = fresh->m_data->horde.m_forcedLocomotorSet;
		if (forced != -1)
		{
			if (AIUpdateInterface *ai = m->getAIUpdateInterface())
			{
				ai->chooseLocomotorSet(forced);
			}
		}
	}
	fresh->bannerCheck(false);
	if (bannerObj)
	{
		if (fresh->m_bannerCarrier == 0)
		{
			fresh->acceptCreatedMember(bannerObj); // slot 0x74
			fresh->m_producedMembers.erase(std::remove(fresh->m_producedMembers.begin(), fresh->m_producedMembers.end(), banner), fresh->m_producedMembers.end());
			fresh->m_bannerCarrier = banner;
		}
		else
		{
			logic.destroyObject(bannerObj);
		}
	}
	fresh->m_dirty = true;
	fresh->updateFormation();
	fresh->applyFormationModifiers(); // RW 0x876B10 (the end of the adoption RW 0x876A08) and RW 0x876248: the new contain's AttributeModifiers (slot 0x1E4; lane COMBAT-3)
	(void)old; // the old contain goes here (RW: with the other object)
	// RW 0x876277 .. 0x87629F (lane INTEG-1): the horde's StancesBehavior (RW 0x861E6E / 0x68BDA5) takes Porcupine when the new contain is a porcupine formation
	// (slot 0xF0), else Battle (RW 0x8620DD)
	if (StancesBehavior *stances = StancesBehavior::of(*horde))
	{
		stances->setStance(fresh->hordeData().m_isPorcupineFormation ? STANCE_PORCUPINE : STANCE_BATTLE);
	}
	return fresh;
}

// RW 0x86C92D (interface slot 0x1E4; lane COMBAT-3)
void HordeContain::applyFormationModifiers()
{
	for (const std::string &name : m_data->horde.m_attributeModifiers)
	{
		addAttributeModifier(name, -1); // slot 0x1D8 (name, player 0, duration -1)
	}
}

// RW 0x86C962 (interface slot 0x1E8; lane COMBAT-3)
void HordeContain::removeFormationModifiers()
{
	for (const std::string &name : m_data->horde.m_attributeModifiers)
	{
		removeAttributeModifier(name); // slot 0x1DC (name, 0)
	}
}

namespace HordeCommands
{
// the dispatcher case RW 0x77BE83
void registerHandlers(GameLogicDispatch &d)
{
	d.registerHandler(MSG_HORDE_TOGGLE_FORMATION, "HORDE-2", [](GameLogic &logic, const GameMessage &m) {
		const GameMessageArgument *a = m.getArgument(0);
		if (!a || a->type != ARGUMENTDATATYPE_OBJECTID)
		{
			return false;
		}
		Object *horde = logic.findObjectByID(a->objectID);
		if (!horde || !horde->getContain())
		{
			return true;
		}
		HordeContain *hc = dynamic_cast<HordeContain *>(horde->getContain());
		if (hc && hc->canToggleFormation())
		{
			hc->toggleFormation();
		}
		return true;
	});
}
} // namespace HordeCommands
