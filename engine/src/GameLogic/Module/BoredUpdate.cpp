// OpenBFME. GPL-3.0.
// See GameLogic/Module/BoredUpdate.h for the target facts, the donors and what is inference.

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "GameLogic/Module/BoredUpdate.h"

#include "Common/GameCommon.h"
#include "Common/INIException.h"
#include "Common/Player.h"
#include "Common/SpecialPower.h"
#include "Common/StateHash.h"
#include "Common/Thing/ModuleFactory.h"
#include "Common/Xfer.h"
#include "GameLogic/ActionManager.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/SpecialPowerModules.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/PartitionManager.h"
#include "GameLogic/ObjectFilterMatch.h"
#include "GameLogic/SnapshotXfer.h"

#include <cstddef>
#include <stdexcept>

namespace
{
typedef BoredUpdateModuleData Data;

const char *const kStop =
	"[S-7161] BoredUpdate (lane MODCOMPAT-MOD1, ported: RW 0x899F9D with the at-object test RW 0x82D925): NOT ported: the targetless form (BoredFilter "
	"not valid: RW 0x82D5DA(obj, power, CMD_FROM_AI, 2, 1) and Object::doSpecialPower RW 0x68E5A3; no 2.01 template leaves BoredFilter unset): nothing is done";

const int kMcAttacking = 0x25; // RW 0x899FC4 (TheModelConditionNames ATTACKING)
const int kMcMoving = 0x3D;    // RW 0x899FD2 (MOVING)

void parseFilter(INI *ini, void *instance, void *store, const void *)
{
	ObjectFilter f;
	ParseObjectFilter(ini, instance, &f, nullptr); // RW 0x76392F
	*static_cast<ObjectFilter *>(store) = std::move(f);
}

// RW 0x73B22F: the template by name from TheSpecialPowerStore
void parseSpecialPowerTemplate(INI *ini, void *instance, void *, const void *)
{
	Data *d = static_cast<Data *>(instance);
	d->m_specialPowerTemplateName = ini->getNextToken();
	if (!TheSpecialPowerStore)
	{
		throw INIException(3, "SpecialPowerTemplate '%s': no special power store (RW 0x73B22F throws 0xDEAD0001)", d->m_specialPowerTemplateName.c_str());
	}
	d->m_specialPowerTemplate = TheSpecialPowerStore->findSpecialPowerTemplate(d->m_specialPowerTemplateName); // an unknown name: null (RW 0x69C146)
}

// RW 0xC65CC0
const FieldParse kFields[] = {
	{ "ScanDelayTime", INI::parseDurationUnsignedInt, nullptr, (int)offsetof(Data, m_scanDelayTime) },                 // RW 0x73A429
	{ "ScanDistance", INI::parseReal, nullptr, (int)offsetof(Data, m_scanDistance) },                                  // RW 0x42ED00
	{ "BoredFilter", parseFilter, nullptr, (int)offsetof(Data, m_boredFilter) },                                       // RW 0x76392F
	{ "CanScanWhileAttackingOrMoving", INI::parseBool, nullptr, (int)offsetof(Data, m_canScanWhileAttackingOrMoving) }, // RW 0x42E558
	{ "SpecialPowerTemplate", parseSpecialPowerTemplate, nullptr, 0 },                                                 // RW 0x73B22F
	{ nullptr, nullptr, nullptr, 0 }
};
} // namespace

void BoredUpdateModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kFields);
}

void BoredUpdate::registerModule(ModuleFactory &modules)
{
	static const char kName[] = "BoredUpdate";
	modules.bindTypedData<Data>(kName, MODULETYPE_BEHAVIOR);
	modules.bindModuleProc(kName, MODULETYPE_BEHAVIOR, [](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
		const Data *typed = dynamic_cast<const Data *>(data);
		if (!typed)
		{
			throw std::logic_error(std::string(kName) + ": the module data is not typed");
		}
		return std::make_unique<BoredUpdate>(thing, typed);
	});
}

std::vector<std::string> BoredUpdate::stopLines()
{
	return { kStop };
}

// RW 0x899F1F: + 0x20 = 0 + GameLogicRandomValue(0, ScanDelayTime) (no wake call)
BoredUpdate::BoredUpdate(Thing *thing, const Data *data)
	: UpdateModule(thing, data)
	, m_data(data)
{
	m_countdown += (unsigned)getObject()->logic().random().getValue(0, (int)data->m_scanDelayTime, "BoredUpdate.cpp", 0x46); // RW 0x6D328E
}

// RW 0x899F9D
UpdateSleepTime BoredUpdate::update()
{
	const SpecialPowerTemplate *power = m_data->m_specialPowerTemplate; // data + 0x18
	Object *obj = getObject();
	if (!power || obj->isEffectivelyDead()) // Object + 0x458 bit 0
	{
		return UPDATE_SLEEP_NONE;
	}
	const bool allowed = m_data->m_canScanWhileAttackingOrMoving || !(obj->testModelCondition(kMcAttacking) || obj->testModelCondition(kMcMoving)); // RW 0x46E918
	// RW 0x8B66B4 (first: it counts down even when not allowed)
	bool scan;
	if (m_countdown == 0)
	{
		m_countdown = m_data->m_scanDelayTime;
		scan = true;
	}
	else
	{
		--m_countdown;
		scan = false;
	}
	if (!scan || !allowed)
	{
		return UPDATE_SLEEP_NONE;
	}
	++m_scans;
	GameLogic &logic = obj->logic();
	if (!ObjectFilterMatch::isValid(&m_data->m_boredFilter)) // RW 0x762977
	{
		logic.noteStop(kStop);
		return UPDATE_SLEEP_NONE;
	}
	const Player *owner = obj->getControllingPlayer(); // RW 0x68B678
	PartitionFilterFn allies([&](Object &o) { return obj->getRelationship(o) == ALLIES; }); // RW 0xC11DC0(obj, 4, 0) -> RW 0x660B85 / 0x68D7AB
	PartitionFilterFn notSelf([&](Object &o) { return &o != obj; });                        // RW 0xC1D660 -> RW 0x661220
	PartitionFilterFn bored([&](Object &o) { return ObjectFilterMatch::allows(logic, m_data->m_boredFilter, o, owner); }); // RW 0xBE4CC8 (flag 1)
	Object *target = logic.partition().getClosestObject(*obj->getPosition(), m_data->m_scanDistance, FROM_CENTER_2D, { &allies, &notSelf, &bored }); // RW 0xA39090
	if (target && ActionManager::canDoSpecialPowerAtObject(*obj, target, CMD_FROM_AI, power, 2u, true)) // RW 0x82D925
	{
		++m_uses;
		SpecialPowerModules::doSpecialPowerAtObject(*obj, power, target, 2u, false); // RW 0x68E67A(power, target, 2, 0)
	}
	return UPDATE_SLEEP_NONE;
}

void BoredUpdate::crc(StateHasher &h) const
{
	UpdateModule::crc(h);
	h.addU32(m_countdown);
}

// RW 0x8B0125's field (OpenBFME native snapshot, S-4190)
void BoredUpdate::xfer(Xfer &x)
{
	SnapshotXfer::version(x, 1, 1);
	xferUpdateModule(x); // RW 0x850C60
	x.xferUnsignedInt(&m_countdown);
	SnapshotXfer::u64(x, m_scans);
	SnapshotXfer::u64(x, m_uses);
}
