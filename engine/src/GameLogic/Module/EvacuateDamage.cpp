// OpenBFME. GPL-3.0.
// See GameLogic/Module/EvacuateDamage.h for the target facts and what is inference.

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "GameLogic/Module/EvacuateDamage.h"

#include "Common/StateHash.h"
#include "Common/Thing/ModuleFactory.h"
#include "Common/Xfer.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/Damage.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/SnapshotXfer.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/WeaponState.h"

#include <cstddef>
#include <stdexcept>
#include <vector>

namespace
{
typedef EvacuateDamageModuleData Data;

// stop S-7590: the inferences of EvacuateDamage.h's WHAT IS INFERENCE, noted when the weapon orders a building's occupants out
const char kStop[] =
	"[S-7590] EvacuateDamage (RW 0x8BF12E, 0x8BF178 .. 0x8BF1EA) orders the occupants out; WeaponThatCausesEvacuation is kept by name without the weapon store, "
	"the orders walk a copy of the contained list (retail: the live list) and an occupant without an AI is skipped (retail reads its null AI)";

// RW 0xC70A88
const FieldParse kFields[] = {
	{ "WeaponThatCausesEvacuation", INI::parseAsciiString, nullptr, (int)offsetof(Data, m_weaponThatCausesEvacuation) }, // RW 0x8BEDF8
	{ "DamageTypeToTrack", INI::parseIndexList, TheDamageNames, (int)offsetof(Data, m_damageTypeToTrack) },
	{ "DamageToPanicThreshold", INI::parsePercentToReal, nullptr, (int)offsetof(Data, m_damageToPanicThreshold) },
	{ "TrackingTimeSpan", INI::parseDurationUnsignedInt, nullptr, (int)offsetof(Data, m_trackingTimeSpan) },
	{ nullptr, nullptr, nullptr, 0 }
};
} // namespace

void EvacuateDamageModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kFields);
}

void EvacuateDamage::registerModule(ModuleFactory &modules)
{
	static const char kName[] = "EvacuateDamage";
	modules.bindTypedData<Data>(kName, MODULETYPE_BEHAVIOR);
	modules.bindModuleProc(kName, MODULETYPE_BEHAVIOR, [](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
		const Data *typed = dynamic_cast<const Data *>(data);
		if (!typed)
		{
			throw std::logic_error(std::string(kName) + ": the module data is not typed");
		}
		return std::make_unique<EvacuateDamage>(thing, typed);
	});
}

EvacuateDamage::EvacuateDamage(Thing *thing, const Data *data)
	: BehaviorModule(thing, data)
	, m_data(data)
{
}

// RW 0x8BF12E
void EvacuateDamage::onDamage(const DamageInfo &info)
{
	const Object *source = getObject()->logic().findObjectByID(info.m_input.m_sourceID); // RW 0x8BF147
	const ObjectWeapons *weapons = source ? source->getWeapons() : nullptr;
	const Weapon *weapon = weapons ? weapons->currentWeapon() : nullptr; // RW 0x68B58C(0)
	if (!weapon)
	{
		return;
	}
	if (weapon->getTemplate()->getName() == m_data->m_weaponThatCausesEvacuation) // RW 0x8BF165 .. 0x8BF176
	{
		evacuate();
	}
	else if (info.m_input.m_damageType == m_data->m_damageTypeToTrack) // RW 0x8BF1EC
	{
		trackDamage(info);
	}
}

void EvacuateDamage::evacuate()
{
	Object *obj = getObject();
	obj->logic().noteStop(kStop);
	ContainModuleInterface *contain = obj->getContain();
	const ContainModuleInterface::ContainedItemsList *list = contain ? contain->getContainedItemsList() : nullptr; // vslot 0x118
	if (!list)
	{
		return;
	}
	static const int horde = CombatNames::kindOf("HORDE");
	const std::vector<Object *> riders(list->begin(), list->end());
	for (Object *rider : riders)
	{
		AIUpdateInterface *ai = rider->getAIUpdateInterface();
		if (!ai)
		{
			continue;
		}
		if (rider->isKindOf((unsigned)horde)) // RW 0x8BF1BB
		{
			ai->aiHordeExit(obj, CMD_FROM_AI); // RW 0x8BF1D2
		}
		else
		{
			ai->aiExit(obj, CMD_FROM_AI); // RW 0x8BF1DC
		}
	}
}

// RW 0x8BEF0A
float EvacuateDamage::trackedDamage()
{
	const int oldest = (int)(getObject()->logic().getFrame() - m_data->m_trackingTimeSpan);
	float sum = 0.0f;
	for (std::list<Hit>::iterator it = m_hits.begin(); it != m_hits.end();)
	{
		if (it->frame <= oldest) // cmp / jg: signed
		{
			it = m_hits.erase(it);
			continue;
		}
		sum = SimMath::sseAdd(it->amount, sum);
		++it;
	}
	return sum;
}

// RW 0x8BF09E
void EvacuateDamage::trackDamage(const DamageInfo &info)
{
	Object *obj = getObject();
	const BodyModuleInterface *body = obj->getBodyModule(); // Object + 0x25C
	if (!body)
	{
		return;
	}
	GameLogic &logic = obj->logic();
	m_hits.push_back(Hit{ info.m_input.m_amount, (int)logic.getFrame() });
	const float maxHealth = body->getMaxHealth(); // vslot 0x1C
	const float sum = trackedDamage();
	if (!(sum >= SimMath::pc24Mul(maxHealth, m_data->m_damageToPanicThreshold))) // RW 0x8BF0FF .. 0x8BF10B: fcompi, jb
	{
		return;
	}
	Object *source = logic.findObjectByID(info.m_input.m_sourceID);
	AIUpdateInterface *ai = obj->getAIUpdateInterface(); // RW 0x8BF076: Object + 0x260
	if (!source || !ai)
	{
		return;
	}
	m_hits.clear();
	ai->aiFleeFromObject(source, CMD_FROM_AI); // RW 0x881FF3
}

void EvacuateDamage::crc(StateHasher &h) const
{
	BehaviorModule::crc(h);
	for (const Hit &hit : m_hits)
	{
		h.addFloat(hit.amount);
		h.addI32(hit.frame);
	}
}

// RW 0x8BEDCC saves the base class only (OpenBFME native snapshot, S-4190: the list too)
// Version 2 is the port's block. Version 1 is the block an OpenBFME build wrote while the class was an unported shell (UnportedBehaviorModule::xfer: the
// BehaviorModule block alone): such a module tracked no hits, so it loads with an empty list
void EvacuateDamage::xfer(Xfer &x)
{
	XferVersion v{ 1, 2 };
	x.xferVersion(v);
	xferBehaviorModule(x);
	if (v.current < 2)
	{
		m_hits.clear();
		return;
	}
	SnapshotXfer::each(x, m_hits, 8, [&x](Hit &hit) {
		x.xferReal(&hit.amount);
		x.xferInt(&hit.frame);
	});
}
