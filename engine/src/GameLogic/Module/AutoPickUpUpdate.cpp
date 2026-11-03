// OpenBFME. GPL-3.0.
// See GameLogic/Module/AutoPickUpUpdate.h for the target facts, the donors and what is inference.

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "GameLogic/Module/AutoPickUpUpdate.h"

#include "Common/AsciiString.h"
#include "Common/GameCommon.h"
#include "Common/INIException.h"
#include "Common/Player.h"
#include "Common/SpecialPower.h"
#include "Common/StateHash.h"
#include "Common/Thing/ModuleFactory.h"
#include "Common/Thing/ThingTemplate.h"
#include "Common/Xfer.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/Module/SpecialPowerModules.h"
#include "GameLogic/Module/UnportedModule.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/PartitionManager.h"
#include "GameLogic/ObjectFilterMatch.h"
#include "GameLogic/ObjectTypes.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/SnapshotXfer.h"

#include <cstddef>
#include <cstring>
#include <stdexcept>

namespace
{
typedef AutoPickUpUpdateModuleData Data;

const char *const kStop =
	"[S-7160] AutoPickUpUpdate (lane MODCOMPAT-MOD1, ported: RW 0x89958F / 0x899562, the WAKE_AUTO_PICKUP button RW 0x68C8DE, the grab / eat through "
	"the object's SPECIAL_GRAB_PASSENGER power, GrabPassengerSpecialPower): NOT ported: AutoThrowObject's throw at the closest enemy (RW 0x8995FB .. "
	"0x89978F; no 2.01 template sets it) and the terrain tree point of a tree pick-up (RW 0x67F52B: the step goes on as without a point); an unported "
	"special power module met before the power (it may be the power) is reported and the step goes on without it";

const int kMcAttacking = 0x25;      // RW 0x8995C6 (TheModelConditionNames ATTACKING)
const int kMcMoving = 0x3D;         // RW 0x8995D4 (MOVING)
const unsigned kKindOfTree = 0x5E;  // RW 0x89986B (TheKindOfNames TREE)
const unsigned kKindOfOrc = 0x6C;   // RW 0x899804 (ORC)
const int kSpecialGrabPassenger = 0x27; // RW 0x691526(0x27): SPECIAL_GRAB_PASSENGER (RW 0xDA5C90 index 39)

// RW 0x899C7D: `MyHealth:<percent> TargetHealth:<percent> Filter:<object filter>`
void expectLabel(INI *ini, const char *label, const char *const wrongName, const char *const wrongCase)
{
	const char *token = ini->getNextTokenOrNull(ini->getSepsColon()); // RW 0x42DBF5(ini + 0x420)
	if (!token || AsciiStringUtil::compareNoCase(token, label) != 0) // _strcmpi
	{
		throw INIException(3, wrongName, token ? token : "(null)");
	}
	if (std::strcmp(token, label) != 0)
	{
		throw INIException(3, wrongCase, token);
	}
}

void parseEatObjectEntry(INI *ini, void *instance, void *store, const void *)
{
	AutoPickUpEatObjectEntry entry; // RW 0x76406F, MyHealth 0, TargetHealth 1.0 (RW 0xBD1908)
	expectLabel(ini, "MyHealth", "AutoPickUpUpdateModuleData::iniParseEatObjectEntry -- Expecting 'MyHealth' entry. You specified %s.",
	            "AutoPickUpUpdateModuleData::iniParseEatObjectEntry -- Entry for 'MyHealth' is case sensitive. You specified %s.");
	INI::parsePercentToReal(ini, instance, &entry.m_myHealth, nullptr); // RW 0x42EEFA
	expectLabel(ini, "TargetHealth", "AutoPickUpUpdateModuleData::iniParseEatObjectEntry -- Expecting 'TargetHealth' entry. You specified %s.",
	            "AutoPickUpUpdateModuleData::iniParseEatObjectEntry -- Entry for 'TargetHealth' is case sensitive. You specified %s.");
	INI::parsePercentToReal(ini, instance, &entry.m_targetHealth, nullptr);
	expectLabel(ini, "Filter", "AutoPickUpUpdateModuleData::iniParseEatObjectEntry -- Expecting 'Filter' entry. You specified %s.",
	            "AutoPickUpUpdateModuleData::iniParseEatObjectEntry -- Entry for 'Filter' is case sensitive. You specified %s.");
	ParseObjectFilter(ini, instance, &entry.m_filter, nullptr); // RW 0x76392F
	static_cast<std::vector<AutoPickUpEatObjectEntry> *>(store)->push_back(std::move(entry)); // RW 0x899C49
}

void parseFilter(INI *ini, void *instance, void *store, const void *)
{
	ObjectFilter f;
	ParseObjectFilter(ini, instance, &f, nullptr); // RW 0x76392F
	*static_cast<ObjectFilter *>(store) = std::move(f);
}

// RW 0xC65B60
const FieldParse kFields[] = {
	{ "ScanDelayTime", INI::parseDurationUnsignedInt, nullptr, (int)offsetof(Data, m_scanDelayTime) },                 // RW 0x73A429
	{ "PickUpFilter", parseFilter, nullptr, (int)offsetof(Data, m_pickUpFilter) },                                     // RW 0x76392F
	{ "ScanDistance", INI::parseReal, nullptr, (int)offsetof(Data, m_scanDistance) },                                  // RW 0x42ED00
	{ "EatObjectEntry", parseEatObjectEntry, nullptr, (int)offsetof(Data, m_eatObjectEntries) },                       // RW 0x899C7D
	{ "AutoThrowObject", INI::parseBool, nullptr, (int)offsetof(Data, m_autoThrowObject) },                            // RW 0x42E558
	{ "RunFromButton", INI::parseBool, nullptr, (int)offsetof(Data, m_runFromButton) },                                // RW 0x42E558
	{ "RunFromButtonNumber", INI::parseInt, nullptr, (int)offsetof(Data, m_runFromButtonNumber) },                     // RW 0x42EC5E
	{ "CanScanWhileAttackingOrMoving", INI::parseBool, nullptr, (int)offsetof(Data, m_canScanWhileAttackingOrMoving) }, // RW 0x42E558
	{ nullptr, nullptr, nullptr, 0 }
};

// BFME1 VA 0x010BB9D0: PickUpKindOf is a mask, not an ObjectFilter expression.
// Preserve +/- edits and empty lines just as ParseKindOfMask does. An ANY filter
// with only this include mask has the same kind intersection test at runtime.
void parseLegacyPickUpMask(INI *ini, void *instance, void *store, const void *)
{
	auto &filter = *static_cast<ObjectFilter *>(store);
	ParseKindOfMask(ini, instance, &filter.includeKindOf, nullptr);
	filter = ObjectFilter::none(filter.includeKindOf, {});
	filter.rule = ObjectFilter::RULE_ANY;
}

using Bfme1Data = Bfme1AutoPickUpUpdateModuleData;
const FieldParse kBfme1Fields[] = {
	{ "ScanDelayTime", INI::parseDurationUnsignedInt, nullptr, (int)offsetof(Bfme1Data, m_scanDelayTime) },
	{ "PickUpKindOf", parseLegacyPickUpMask, nullptr, (int)offsetof(Bfme1Data, m_pickUpFilter) },
	{ "ScanDistance", INI::parseReal, nullptr, (int)offsetof(Bfme1Data, m_scanDistance) },
	{ "Bored", INI::parseBool, nullptr, (int)offsetof(Bfme1Data, m_bored) },
	{ "BoredFilter", parseFilter, nullptr, (int)offsetof(Bfme1Data, m_boredFilter) },
	{ "EatObjectEntry", parseEatObjectEntry, nullptr, (int)offsetof(Bfme1Data, m_eatObjectEntries) },
	{ "AutoThrowObject", INI::parseBool, nullptr, (int)offsetof(Bfme1Data, m_autoThrowObject) },
	{ "RunFromButton", INI::parseBool, nullptr, (int)offsetof(Bfme1Data, m_runFromButton) },
	{ "RunFromButtonNumber", INI::parseInt, nullptr, (int)offsetof(Bfme1Data, m_runFromButtonNumber) },
	{ "CanScanWhileAttackingOrMoving", INI::parseBool, nullptr, (int)offsetof(Bfme1Data, m_canScanWhileAttackingOrMoving) },
	{ nullptr, nullptr, nullptr, 0 }
};

class Bfme1AutoPickUpUpdate : public AutoPickUpUpdate
{
public:
	OBFME_MODULE_XFER(Bfme1AutoPickUpUpdate)
	Bfme1AutoPickUpUpdate(Thing *thing, const Bfme1Data *data)
		: AutoPickUpUpdate(thing, data), m_legacyData(data) {}

protected:
	bool tryBoredAction() override
	{
		// BFME1 RVA 0x00281F40: after eating, before pickup; the caller has
		// already established an empty contain. This scan has no readiness gate.
		if (!m_legacyData->m_bored) return false;
		Object *obj = getObject();
		GameLogic &logic = obj->logic();
		const Player *owner = obj->getControllingPlayer();
		PartitionFilterFn bored([&](Object &o) { return ObjectFilterMatch::allows(logic, m_legacyData->m_boredFilter, o, owner); });
		PartitionFilterFn allies([&](Object &o) { return obj->getRelationship(o) == ALLIES; });
		PartitionFilterFn notSelf([&](Object &o) { return &o != obj; });
		Object *target = logic.partition().getClosestObject(*obj->getPosition(), m_legacyData->m_scanDistance, FROM_CENTER_2D, { &bored, &allies, &notSelf });
		if (!target) return false;
		for (const auto &m : obj->modules())
		{
			if (m->isUnported())
			{
				if (unportedInterfaceMask(*m) & MODULEINTERFACE_SPECIAL_POWER)
				{
					logic.noteStop(kStop);
					return true;
				}
				continue;
			}
			SpecialPowerModuleInterface *power = m->getSpecialPower();
			const SpecialPowerTemplate *tmpl = power ? power->getSpecialPowerTemplate() : nullptr;
			// BFME1's SPECIAL_MTTROLL_BORED is 0x41 in the donor. The port's
			// SpecialPower parser stores the shared RotWK name ordinal, 0x40.
			if (tmpl && tmpl->getSpecialPowerType() == 0x40)
			{
				SpecialPowerModules::doSpecialPowerAtObject(*obj, tmpl, target, 2u, false);
				break;
			}
		}
		return true; // a target ends this scan even when there is no matching power
	}

private:
	const Bfme1Data *m_legacyData;
};

void Bfme1AutoPickUpUpdate::xfer(Xfer &x)
{
	// This variant adds template data only, so its runtime snapshot is unchanged.
	AutoPickUpUpdate::xfer(x);
}

// body slot 0x14: health / max health in the FPU (HeroAbilityModules' form of RW 0x8C1D75)
double healthRatio(const Object &o)
{
	const BodyModuleInterface *body = o.getBodyModule();
	if (!body)
	{
		throw std::logic_error("AutoPickUpUpdate: " + o.getTemplate()->getName() + " has no body (RW 0x8997A9 calls the body's slot 0x14)");
	}
	const float maxHealth = body->getMaxHealth();
	return maxHealth > 0.0f ? SimMath::pc24DivW((double)body->getHealth(), (double)maxHealth) : 0.0;
}
} // namespace

void AutoPickUpUpdateModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kFields);
}

void Bfme1AutoPickUpUpdateModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kBfme1Fields);
}

void AutoPickUpUpdate::registerModule(ModuleFactory &modules)
{
	static const char kName[] = "AutoPickUpUpdate";
	modules.bindDataProc(kName, MODULETYPE_BEHAVIOR, [](INI *ini) -> std::shared_ptr<ModuleData> {
		if (ini && ini->bfme1Data())
		{
			auto data = std::make_shared<Bfme1Data>();
			ini->initFromINIMultiProc(data.get(), &Bfme1Data::buildFieldParse);
			return data;
		}
		auto data = std::make_shared<Data>();
		if (ini) ini->initFromINIMultiProc(data.get(), &Data::buildFieldParse);
		return data;
	});
	modules.bindModuleProc(kName, MODULETYPE_BEHAVIOR, [](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
		if (const auto *legacy = dynamic_cast<const Bfme1Data *>(data))
		{
			return std::make_unique<Bfme1AutoPickUpUpdate>(thing, legacy);
		}
		const Data *typed = dynamic_cast<const Data *>(data);
		if (!typed)
		{
			throw std::logic_error(std::string(kName) + ": the module data is not typed");
		}
		return std::make_unique<AutoPickUpUpdate>(thing, typed);
	});
}

std::vector<std::string> AutoPickUpUpdate::stopLines()
{
	return { kStop };
}

// RW 0x8994BF: + 0x24 = 0 + GameLogicRandomValue(0, ScanDelayTime), + 0x28 = + 0x29 = 0 (no wake call)
AutoPickUpUpdate::AutoPickUpUpdate(Thing *thing, const Data *data)
	: UpdateModule(thing, data)
	, m_data(data)
{
	m_countdown += (unsigned)getObject()->logic().random().getValue(0, (int)data->m_scanDelayTime, "AutoPickUpUpdate.cpp", 0x83); // RW 0x6D328E
}

// RW 0x68C8DE: the behaviour modules in order; each whose interface (slot 0x6C) answers the number is woken (interface slot 2)
void AutoPickUpUpdate::wakeAutoPickUp(Object &obj, int number)
{
	for (const auto &m : obj.modules())
	{
		if (AutoPickUpUpdate *a = dynamic_cast<AutoPickUpUpdate *>(m.get()))
		{
			if (a->getRunFromButtonNumber() == number)
			{
				a->wakeFromButton();
			}
		}
	}
}

// RW 0x899562
bool AutoPickUpUpdate::gate()
{
	if (m_data->m_runFromButton) // data + 0x21
	{
		return m_buttonWake; // + 0x28
	}
	if (m_countdown == 0)
	{
		m_countdown = m_data->m_scanDelayTime;
		return true;
	}
	--m_countdown;
	return false;
}

// RW 0x691526(0x27): the first module whose special power's template is of type SPECIAL_GRAB_PASSENGER. An unported special power module met before
// one is found may be it (GrabPassengerSpecialPower is): reported (S-7160), no power
SpecialPowerModuleInterface *AutoPickUpUpdate::grabPower()
{
	Object *obj = getObject();
	for (const auto &m : obj->modules())
	{
		if (m->isUnported())
		{
			if (unportedInterfaceMask(*m) & MODULEINTERFACE_SPECIAL_POWER)
			{
				obj->logic().noteStop(kStop);
				return nullptr;
			}
			continue;
		}
		SpecialPowerModuleInterface *sp = m->getSpecialPower();
		const SpecialPowerTemplate *t = sp ? sp->getSpecialPowerTemplate() : nullptr;
		if (t && t->getSpecialPowerType() == kSpecialGrabPassenger)
		{
			return sp;
		}
	}
	return nullptr;
}

// RW 0x89958F
UpdateSleepTime AutoPickUpUpdate::update()
{
	Object *obj = getObject();
	GameLogic &logic = obj->logic();
	if (obj->isEffectivelyDead()) // Object + 0x458 bit 0
	{
		return UPDATE_SLEEP_NONE;
	}
	const bool allowed = m_data->m_canScanWhileAttackingOrMoving || !(obj->testModelCondition(kMcAttacking) || obj->testModelCondition(kMcMoving)); // RW 0x46E918
	if (!gate() || !allowed) // RW 0x8995EA: the gate first
	{
		return UPDATE_SLEEP_NONE;
	}
	m_buttonWake = false; // + 0x28
	++m_scans;
	const Player *owner = obj->getControllingPlayer(); // RW 0x68B678
	const auto &partition = logic.partition();
	ContainModuleInterface *contain = obj->getContain(); // Object + 0x258
	// 1. AutoThrowObject (RW 0x8995FB): not ported (S-7160); the step goes on as when no enemy is found
	if (m_data->m_autoThrowObject && obj->getContainedBy() && contain && contain->getContainCount() == 1)
	{
		logic.noteStop(kStop);
	}
	// 2. contained with an empty contain (RW 0x8997A0 .. 0x8997DB): the closest ORC that PickUpFilter allows
	if (obj->getContainedBy() && contain && contain->getContainCount() == 0)
	{
		PartitionFilterFn orc([&](Object &o) { return o.isKindOf(kKindOfOrc); }); // RW 0x797E8D -> 0x660DC6 (RW 0x70C548)
		PartitionFilterFn allowed2([&](Object &o) { return ObjectFilterMatch::allows(logic, m_data->m_pickUpFilter, o, owner); }); // RW 0xBE4CC8 (flag 1)
		if (Object *found = partition.getClosestObject(*obj->getPosition(), m_data->m_scanDistance, FROM_CENTER_2D, { &orc, &allowed2 })) // RW 0xA39090
		{
			if (SpecialPowerModuleInterface *power = grabPower())
			{
				power->doSpecialPowerAtObject(found, 0x2000); // slot 0x2C
				return UPDATE_SLEEP_NONE;
			}
		}
	}
	else if (obj->getContainedBy() && !contain)
	{
		throw std::logic_error("AutoPickUpUpdate: contained " + obj->getTemplate()->getName() + " has no contain (RW 0x89978F calls the contain's slot 0x114)");
	}
	// 3. RW 0x8997E0
	if (!contain)
	{
		return UPDATE_SLEEP_FOREVER;
	}
	// 4. the EatObjectEntry list (RW 0x899800 .. 0x8998D8)
	if (contain->getContainCount() == 0)
	{
		for (const AutoPickUpEatObjectEntry &e : m_data->m_eatObjectEntries)
		{
			if (!(healthRatio(*obj) <= (double)e.m_myHealth))
			{
				continue;
			}
			PartitionFilterFn alive([](Object &o) { return !o.isEffectivelyDead(); }); // RW 0xC10E20
			PartitionFilterFn eat([&](Object &o) { return ObjectFilterMatch::allows(logic, e.m_filter, o, owner); }); // RW 0xBE4CC8 (the entry's filter, flag 1)
			Object *found = partition.getClosestObject(*obj->getPosition(), m_data->m_scanDistance, FROM_CENTER_2D, { &alive, &eat });
			if (found && healthRatio(*found) <= (double)e.m_targetHealth)
			{
				if (SpecialPowerModuleInterface *power = grabPower())
				{
					power->doSpecialPowerAtObject(found, 0x4000);
					return UPDATE_SLEEP_NONE;
				}
			}
		}
	}
	// 5. RW 0x8998E0
	if (contain->getContainCount() != 0)
	{
		return UPDATE_SLEEP_NONE;
	}
	if (tryBoredAction()) return UPDATE_SLEEP_NONE;
	KindOfMaskType tree{};
	MaskSet(tree, kKindOfTree, true); // RW 0x444D39(0, 0x5E)
	if (ObjectFilterMatch::allowsKindOf(m_data->m_pickUpFilter, tree)) // RW 0x76379C
	{
		logic.noteStop(kStop); // the terrain tree point RW 0x67F52B is not ported: as without a point
	}
	PartitionFilterFn pick([&](Object &o) { return ObjectFilterMatch::allows(logic, m_data->m_pickUpFilter, o, owner); }); // RW 0xBE4CC8 (flag 1)
	if (Object *found = partition.getClosestObject(*obj->getPosition(), m_data->m_scanDistance, FROM_CENTER_2D, { &pick }))
	{
		if (SpecialPowerModuleInterface *power = grabPower())
		{
			power->doSpecialPowerAtObject(found, 0x2000);
		}
	}
	return UPDATE_SLEEP_NONE;
}

void AutoPickUpUpdate::crc(StateHasher &h) const
{
	UpdateModule::crc(h);
	h.addU32(m_countdown);
	h.addBool(m_buttonWake);
	h.addBool(m_buttonWoken);
}

// RW 0x899405's fields (OpenBFME native snapshot, S-4190)
void AutoPickUpUpdate::xfer(Xfer &x)
{
	SnapshotXfer::version(x, 1, 1);
	xferUpdateModule(x); // RW 0x850C60
	x.xferUnsignedInt(&m_countdown);
	x.xferBool(&m_buttonWake);
	x.xferBool(&m_buttonWoken);
	SnapshotXfer::u64(x, m_scans);
}
