// OpenBFME. GPL-3.0.
//
// Lane STEALTH-2: ToggleHiddenSpecialAbilityUpdate and InvisibilitySpecialPower (see the header for the binary facts).

#include "GameLogic/Module/StealthAbilityModules.h"
#include "Common/INI.h"
#include "Common/SpecialPower.h"
#include "Common/StateHash.h"
#include "Common/Thing/ModuleFactory.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Locomotor.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/InvisibilityModules.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/PartitionManager.h"
#include "GameLogic/ObjectFilterMatch.h"

#include <cstddef>
#include <stdexcept>

namespace
{
struct HiddenBits
{
	int status = CombatNames::status("HIDDEN");                  // 0x10
	int condition = CombatNames::modelCondition("HIDDEN");       // 0x103 (Object + 0x12C bit 3)
	int weaponSet = CombatNames::weaponSetBit("HIDDEN");         // 0x3D
};
const HiddenBits &hiddenBits()
{
	static const HiddenBits b;
	return b;
}

#define TH_OFF(m) (int)offsetof(ToggleHiddenSpecialAbilityUpdateModuleData, m)
const FieldParse kToggleHidden[] = { // RW 0xC05A8C
	{ "ShowPalantirTimer", INI::parseBool, nullptr, TH_OFF(m_showPalantirTimer) },
	{ nullptr, nullptr, nullptr, 0 },
};
#undef TH_OFF

#define IS_OFF(m) (int)offsetof(InvisibilitySpecialPowerModuleData, m)
const FieldParse kInvisibilitySpecialPower[] = { // RW 0xC730E8
	{ "InvisibilityNugget", InvisibilityNugget::parse, nullptr, IS_OFF(m_nugget) },
	{ "BroadcastRadius", INI::parseReal, nullptr, IS_OFF(m_broadcastRadius) },
	{ "ObjectFilter", ParseObjectFilter, nullptr, IS_OFF(m_objectFilter) },
	{ "Duration", INI::parseDurationUnsignedInt, nullptr, IS_OFF(m_duration) },
	{ nullptr, nullptr, nullptr, 0 },
};
#undef IS_OFF

void noteStop(const Object &obj, const std::string &what)
{
	obj.logic().noteStop("[S-1043] " + what);
}

template <class Runtime, class Data>
void bind(ModuleFactory &modules, const char *name)
{
	modules.bindTypedData<Data>(name, MODULETYPE_BEHAVIOR);
	modules.bindModuleProc(name, MODULETYPE_BEHAVIOR, [name](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
		const Data *typed = dynamic_cast<const Data *>(data);
		if (!typed)
		{
			throw std::logic_error(std::string(name) + ": the module data is not typed");
		}
		return std::make_unique<Runtime>(thing, typed);
	});
}

// RW 0x68BDA5 with the name key "InvisibilityUpdate": the object's first such module
InvisibilityUpdate *firstInvisibilityUpdate(const Object &obj)
{
	return dynamic_cast<InvisibilityUpdate *>(obj.findModule("InvisibilityUpdate"));
}
} // namespace

void ToggleHiddenSpecialAbilityUpdateModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	SpecialAbilityUpdateModuleData::buildFieldParse(p);
	p.add(kToggleHidden);
}

void InvisibilitySpecialPowerModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	SpecialPowerModuleData::buildFieldParse(p);
	p.add(kInvisibilitySpecialPower);
}

// ---- ToggleHiddenSpecialAbilityUpdate -----------------------------------------------------------------------------------------------
ToggleHiddenSpecialAbilityUpdate::ToggleHiddenSpecialAbilityUpdate(Thing *thing, const ToggleHiddenSpecialAbilityUpdateModuleData *data)
	: SpecialAbilityUpdate(thing, data)
	, m_th(data)
{
}

ToggleHiddenSpecialAbilityUpdate *ToggleHiddenSpecialAbilityUpdate::of(const Object &obj)
{
	return dynamic_cast<ToggleHiddenSpecialAbilityUpdate *>(obj.findModule("ToggleHiddenSpecialAbilityUpdate"));
}

// RW 0x8B1962
UpdateSleepTime ToggleHiddenSpecialAbilityUpdate::update()
{
	UpdateSleepTime sleep = SpecialAbilityUpdate::update();
	const unsigned duration = m_data->m_effectDuration; // data + 0x7C
	Object *obj = getObject();
	if (duration != 0 && obj && obj->testStatus((unsigned)hiddenBits().status))
	{
		if (now() >= m_hideFrame + duration)
		{
			unhide();
		}
		else
		{
			sleep = UPDATE_SLEEP((int)duration);
		}
	}
	return sleep;
}

// RW 0x8B42F3
bool ToggleHiddenSpecialAbilityUpdate::continuePreparation()
{
	if (m_triggerCount == 2)
	{
		return false;
	}
	return SpecialAbilityUpdate::continuePreparation();
}

// RW 0x8B1909
void ToggleHiddenSpecialAbilityUpdate::triggerAbilityEffect()
{
	SpecialAbilityUpdate::triggerAbilityEffect();
	if (m_triggerCount != 1)
	{
		return;
	}
	if (getObject()->testStatus((unsigned)hiddenBits().status))
	{
		unhide();
	}
	else
	{
		hide();
	}
}

// RW 0x851B6E: SpecialAbilityUpdate's conditions (RW 0x851551), then the garrison enums 0x27 / 0x28 need room in the object's contain (contain slots 0x114 /
// 0x70: not ported, S-1043)
bool ToggleHiddenSpecialAbilityUpdate::canHide() const
{
	if (!conditionsAllow())
	{
		return false;
	}
	const SpecialPowerTemplate *t = m_data->m_specialPowerTemplate;
	const int type = t ? t->getSpecialPowerType() : 0;
	if (type != 0x27 && type != 0x28)
	{
		return true;
	}
	noteStop(*getObject(), "ToggleHiddenSpecialAbilityUpdate: the garrison enum's room test (RW 0x851BA2) is not ported; the hide does not happen");
	return false;
}

// RW 0x8B19AC
void ToggleHiddenSpecialAbilityUpdate::unhide()
{
	Object *obj = getObject();
	const HiddenBits &b = hiddenBits();
	if (!obj || !obj->testStatus((unsigned)b.status))
	{
		return;
	}
	obj->setStatus((unsigned)b.status, false); // RW 0x62684D(0x10, 0)
	if (obj->testModelCondition(b.condition))
	{
		obj->setModelConditionState(b.condition, false); // RW 0x8B19EA: Object + 0x12C bit 3, RW 0x68B53C
	}
	if (ObjectWeapons *w = obj->getWeapons())
	{
		w->setWeaponSetFlag(b.weaponSet, false); // RW 0x691106(0x3D)
	}
	if (SpecialPowerModule *spm = mySpecialPowerModule()) // RW 0x68C26D
	{
		spm->pauseCountdown(false); // slot 0x24
	}
	if (InvisibilityUpdate *inv = firstInvisibilityUpdate(*obj))
	{
		inv->setActive(false); // RW 0x8A7049(0)
	}
	++m_unhides;
}

// RW 0x8B1A70
void ToggleHiddenSpecialAbilityUpdate::hide()
{
	if (!canHide())
	{
		return;
	}
	Object *obj = getObject();
	const HiddenBits &b = hiddenBits();
	if (!obj || obj->testStatus((unsigned)b.status))
	{
		return;
	}
	AIUpdateInterface *ai = obj->getAIUpdateInterface(); // Object + 0x260
	if (!ai)
	{
		return;
	}
	if (SpecialPowerModule *spm = mySpecialPowerModule())
	{
		spm->pauseCountdown(true);
	}
	// RW 0x8B1AD5: RW 0x68BE97 (the locomotor's speed, RW 0x68B34C, above 0.1f: RW 0xBD83D4) or the AI's current state active (vslot 0x1BC)
	const Locomotor *loco = ai->curLocomotor();
	const float speed = loco && loco->speed() > 0.0f ? loco->speed() : 0.0f;
	if (speed > 0.1f || ai->isStateActive())
	{
		ai->aiIdle(CMD_FROM_AI); // RW 0x5E821A(2)
	}
	m_hideFrame = now(); // RW 0x8B1B06: TheGameLogic + 0x40
	obj->setStatus((unsigned)b.status, true);
	if (ObjectWeapons *w = obj->getWeapons())
	{
		w->setWeaponSetFlag(b.weaponSet, true); // RW 0x691059(0x3D)
	}
	if (!obj->testModelCondition(b.condition))
	{
		obj->setModelConditionState(b.condition, true);
	}
	if (InvisibilityUpdate *inv = firstInvisibilityUpdate(*obj))
	{
		inv->setActive(true); // RW 0x8A7049(1)
	}
	++m_hides;
}

void ToggleHiddenSpecialAbilityUpdate::crc(StateHasher &h) const
{
	SpecialAbilityUpdate::crc(h);
	h.addU32(m_hideFrame); // + 0x88 (xfer RW 0x8B1938)
	h.addU32(m_hides);
	h.addU32(m_unhides);
}

// ---- InvisibilitySpecialPower -------------------------------------------------------------------------------------------------------
InvisibilitySpecialPower::InvisibilitySpecialPower(Thing *thing, const InvisibilitySpecialPowerModuleData *data)
	: SpecialPowerModule(thing, data)
	, m_data(data)
{
}

// RW 0x8C65D2
void InvisibilitySpecialPower::doSpecialPowerAtObject(Object *target, unsigned options)
{
	SpecialPowerModule::doSpecialPowerAtObject(target, options); // RW 0x8980EF
	Object *obj = getObject();
	m_lastAffected.assign(1, obj->getID());
	obj->logic().invisibility().applyNugget(obj, m_data->m_duration, m_data->m_nugget); // RW 0x81C217: the caster itself
	++m_given;
}

// RW 0x8C66EB
void InvisibilitySpecialPower::doSpecialPowerAtLocation(const Coord3D &loc, unsigned options)
{
	SpecialPowerModule::doSpecialPowerAtLocation(loc, options); // RW 0x89816C (its answer is not tested)
	Object *obj = getObject();
	GameLogic &logic = obj->logic();
	const Player *player = obj->getControllingPlayer(); // RW 0x68B678
	// RW 0x8C670D .. 0x8C679F: the filters RW 0xC1D660 (not the caster), RW 0xC10E20 (alive), RW 0xC0F374 (the same Object + 0x458 bit 3: never set) and the
	// ObjectFilter for the player (RW 0xBE4CC8, flag 1); BroadcastRadius, distance type 0, near to far (lane MODULES-2's partition)
	PartitionFilterFn filter([&](Object &o) {
		if (&o == obj || o.isEffectivelyDead())
		{
			return false;
		}
		return ObjectFilterMatch::allows(logic, m_data->m_objectFilter, o, player);
	});
	std::vector<Object *> hits;
	for (const PartitionHit &hit : logic.partition().iterateObjectsInRange(loc, m_data->m_broadcastRadius, FROM_CENTER_2D, { &filter }, ITER_SORTED_NEAR_TO_FAR))
	{
		hits.push_back(hit.object);
	}
	m_lastAffected.clear();
	InvisibilityManager &mgr = logic.invisibility();
	for (Object *o : hits)
	{
		mgr.applyNugget(o, m_data->m_duration, m_data->m_nugget); // RW 0x8C67CC
		m_lastAffected.push_back(o->getID());
		++m_given;
	}
}

void InvisibilitySpecialPower::crc(StateHasher &h) const
{
	SpecialPowerModule::crc(h);
	h.addU64(m_given);
}

void StealthAbilityModules::registerAll(ModuleFactory &modules)
{
	bind<ToggleHiddenSpecialAbilityUpdate, ToggleHiddenSpecialAbilityUpdateModuleData>(modules, "ToggleHiddenSpecialAbilityUpdate");
	bind<InvisibilitySpecialPower, InvisibilitySpecialPowerModuleData>(modules, "InvisibilitySpecialPower");
}

std::vector<std::string> StealthAbilityModules::stopLines()
{
	return { "[S-1043] stealth abilities (lane STEALTH-2): ToggleHiddenSpecialAbilityUpdate (RW 0x8B1909 / 0x8B19AC / 0x8B1A70 / 0x8B1962) and "
		"InvisibilitySpecialPower (RW 0x8C65D2 / 0x8C66EB) run; not ported: ShowPalantirTimer and the EffectDuration report to the player (the palantir "
		"timer, client), the hide's room test for the garrison enums 0x27 / 0x28 (no retail ToggleHidden ability uses them: counted, no hide)" };
}
