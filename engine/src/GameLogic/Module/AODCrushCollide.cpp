// OpenBFME. GPL-3.0. See AODCrushCollide.h for both edition sources.
#if defined(__GNUC__) || defined(__clang__)
// Local 169: RW 0x8C0145 (SmallFXList)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif
#include "GameLogic/Module/AODCrushCollide.h"
#include "Common/AsciiString.h"
#include "Common/StateHash.h"
#include "Common/Thing/ModuleFactory.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Damage.h"
#include "GameLogic/FXEvents.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/PhysicsBehavior.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"
#include "GameLogic/ObjectCreationList.h"
#include "GameLogic/ObjectFilterMatch.h"
#include "GameLogic/SnapshotXfer.h"
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <variant>

namespace {
using Data = AODCrushCollideModuleData;
const char *const kDeathNames[] = { "NORMAL", "NONE", "CRUSHED", "BURNED", "EXPLODED", "POISONED", "TOPPLED", "FLOODED", "SUICIDED", "LASERED", "DETONATED", "SPLATTED", "POISONED_BETA", "EXTRA_2", "EXTRA_3", "EXTRA_4", "EXTRA_5", "EXTRA_6", "EXTRA_7", "EXTRA_8", "KNOCKBACK", "SUPERNATURAL", "FADED", "SLAUGHTERED", nullptr };
void parseName(INI *ini, void *, void *store, const void *)
{
	const std::string value = ini->getNextToken();
	*static_cast<std::string *>(store) = AsciiStringUtil::compareNoCase(value, "None") == 0 ? std::string() : value;
}
#define AOD_OFF(m) (int)offsetof(Data, m)
// The original table RW 0xC710A8, including all three sizes and the filter.
const FieldParse kFields[] = {
	{ "SmallFXList", parseName, nullptr, AOD_OFF(fx[0]) },
	{ "SmallObjectCreationList", parseName, nullptr, AOD_OFF(ocl[0]) },
	{ "MediumFXList", parseName, nullptr, AOD_OFF(fx[1]) },
	{ "MediumObjectCreationList", parseName, nullptr, AOD_OFF(ocl[1]) },
	{ "LargeFXList", parseName, nullptr, AOD_OFF(fx[2]) },
	{ "LargeObjectCreationList", parseName, nullptr, AOD_OFF(ocl[2]) },
	{ "Damage", INI::parseReal, nullptr, AOD_OFF(damage) },
	{ "DamageType", INI::parseIndexList, TheDamageNames, AOD_OFF(damageType) },
	{ "DeathType", INI::parseIndexList, kDeathNames, AOD_OFF(deathType) },
	{ "SpecialDamage", INI::parseReal, nullptr, AOD_OFF(specialDamage) },
	{ "SpecialDamageType", INI::parseIndexList, TheDamageNames, AOD_OFF(specialDamageType) },
	{ "SpecialDeathType", INI::parseIndexList, kDeathNames, AOD_OFF(specialDeathType) },
	{ "SelfDamage", INI::parseReal, nullptr, AOD_OFF(selfDamage) },
	{ "SelfDamageType", INI::parseIndexList, TheDamageNames, AOD_OFF(selfDamageType) },
	{ "SelfDeathType", INI::parseIndexList, kDeathNames, AOD_OFF(selfDeathType) },
	{ "SpecialObject", ParseObjectFilter, nullptr, AOD_OFF(specialObject) },
	{ nullptr, nullptr, nullptr, 0 }
};
#undef AOD_OFF
int targetSize(const Object &obj)
{
	const FieldValue *field = obj.getTemplate()->findField("DeadCollideSize");
	if (!field || std::holds_alternative<std::monostate>(*field)) return 0; // RW 0x73FFAF
	if (const long long *n = std::get_if<long long>(field)) return *n == 0 ? 0 : *n == 1 ? 1 : 2;
	throw std::logic_error("AODCrushCollide: DeadCollideSize is not an index");
}
}
void AODCrushCollideModuleData::buildFieldParse(MultiIniFieldParse &p) { p.add(kFields); }
const char *AODCrushCollide::hordeStop() { return "[S-6007] AODCrushCollide: the containing horde's collision callback RW 0x87E894 is ported from RotWK; BFME2 0x87B628 parity is not verified"; }
const char *AODCrushCollide::oclStop() { return ""; } // PORT-237 closed S-6008: RW 0x8C0418 calls OCL::create(other, self)
AODCrushCollide::AODCrushCollide(Thing *thing, const Data *data) : UpdateModule(thing, data), m_data(data) {}

void AODCrushCollide::onCollide(Object *other, const Coord3D *, const Coord3D *)
{
	if (!other) return;
	Object *self = getObject();
	PhysicsBehavior *physics = PhysicsBehavior::find(*other);
	if (physics && physics->isStunned()) return;
	static const int oath = CombatNames::modelCondition("OATH_FULLFILLED");
	if (self->isEffectivelyDead() || self->testModelCondition(oath)) return;
	PhysicsBehavior *ownPhysics = PhysicsBehavior::find(*self);
	if (ownPhysics && ownPhysics->isStunned()) return;
	if (other->getRelationship(*self) == ALLIES) return; // RW 0x8C0268: the candidate's relationship
	GameLogic &logic = self->logic();
	Object *horde = self->getContainedBy(); // RW Object +0x27C; BFME2 +0x274
	if (AODHordeContain *aod = horde ? dynamic_cast<AODHordeContain *>(horde->getContain()) : nullptr)
	{
		aod->onLargeObjectCollide(self, other); // RW 0x87E894
		if (!other->isEffectivelyDead() && targetSize(*other) == 2)
			logic.noteStop(hordeStop()); // BFME2 0x87B628 parity unverified (S-6007)
	}
	if (other->isEffectivelyDead()) return;
	physics = PhysicsBehavior::find(*other);
	if (!physics || physics->isStunned()) return;
	if (!m_firing)
	{
		m_firing = true;
		static const int firing = CombatNames::modelCondition("FIRING_A");
		self->setModelConditionState(firing, true);
		setWakeFrame(self, UPDATE_SLEEP(2 * LOGICFRAMES_PER_SECOND));
	}
	m_clearFrame = logic.getFrame() + 2 * LOGICFRAMES_PER_SECOND;
	const bool special = ObjectFilterMatch::allows(logic, m_data->specialObject, *other, self->getControllingPlayer());
	DamageInfo damage;
	damage.m_input.m_sourceID = self->getID();
	damage.m_input.m_damageType = special ? m_data->specialDamageType : m_data->damageType;
	damage.m_input.m_deathType = special ? m_data->specialDeathType : m_data->deathType;
	damage.m_input.m_amount = special ? m_data->specialDamage : m_data->damage;
	other->attemptDamage(damage);
	m_lastTarget = other->getID();
	if (special)
	{
		DamageInfo selfDamage;
		selfDamage.m_input.m_sourceID = other->getID();
		selfDamage.m_input.m_damageType = m_data->selfDamageType;
		selfDamage.m_input.m_deathType = m_data->selfDeathType;
		selfDamage.m_input.m_amount = m_data->selfDamage;
		self->attemptDamage(selfDamage);
	}
	const int size = targetSize(*other);
	if (!m_data->fx[size].empty())
	{
		FXEvent e = FXEventLog::objectEvent(FXEvent::OBJECT_FX, "AODCrushCollide", logic.getFrame(), m_data->fx[size], *other);
		e.secondary = self->getID();
		logic.fxEvents().emit(e);
	}
	if (!m_data->ocl[size].empty())
	{
		const ObjectCreationList *ocl = TheObjectCreationListStore ? TheObjectCreationListStore->findObjectCreationList(m_data->ocl[size]) : nullptr;
		if (ocl) ocl->create(logic, other, *self->getPosition()); // RW 0x8C0418 -> 0x5F0126
		else logic.reportError("AODCrushCollide: ObjectCreationList '" + m_data->ocl[size] + "' was not found");
	}
}

UpdateSleepTime AODCrushCollide::update()
{
	if (!m_firing) return UPDATE_SLEEP_FOREVER;
	if (m_clearFrame < getObject()->logic().getFrame())
	{
		static const int firing = CombatNames::modelCondition("FIRING_A");
		getObject()->setModelConditionState(firing, false);
		m_firing = false;
	}
	return UPDATE_SLEEP_NONE;
}
void AODCrushCollide::crc(StateHasher &h) const { UpdateModule::crc(h); h.addU32(m_clearFrame); h.addU32(m_lastTarget); h.addBool(m_firing); }
// RW 0x008C004C (DONOR BFME2 0x004BBF03): AODCrushCollide::xfer (version 1, UpdateModule::xfer, m_clearFrame +0x24, m_lastTarget +0x28, m_firing +0x2C)
void AODCrushCollide::xfer(Xfer &x) { SnapshotXfer::version(x, 1, 1); xferUpdateModule(x); x.xferUnsignedInt(&m_clearFrame); x.xferObjectID(&m_lastTarget); x.xferBool(&m_firing); }
// RW 0x008C0046 (getModuleName), RW 0x008C008D (getModuleNameKey, DONOR BFME2 0x004BBF44), RW 0x008C0199 (scalar deleting dtor -> ~AODCrushCollide RW 0x008C0026)
void AODCrushCollide::registerClass(ModuleFactory &modules)
{
	modules.bindTypedData<Data>("AODCrushCollide", MODULETYPE_BEHAVIOR);
	modules.bindModuleProc("AODCrushCollide", MODULETYPE_BEHAVIOR, [](Thing *thing, const ModuleData *data, const ModuleFactory::ModuleTemplate &) -> std::unique_ptr<Module> {
		const auto *typed = dynamic_cast<const Data *>(data);
		if (!typed) throw std::logic_error("AODCrushCollide: the module data is not typed");
		return std::make_unique<AODCrushCollide>(thing, typed);
	});
}
