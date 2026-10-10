// OpenBFME. GPL-3.0.
// See GameLogic/Module/ExtraDieModules.h for the target facts, the donors and what is inference.

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/Module/ExtraDieModules.h"

#include "Common/AsciiString.h"
#include "Common/Player.h"
#include "Common/StateHash.h"
#include "Common/Thing/ThingTemplate.h"
#include "Common/Upgrade.h"
#include "GameLogic/BitFlags.h"
#include "GameLogic/Combat/WeaponDelivery.h"
#include "GameLogic/GameLogic.h"
#include "Common/SpecialPower.h"
#include "GameLogic/Module/SpecialPowerModules.h"
#include "GameLogic/ObjectCreationList.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/Weapon.h"

#include <cstddef>

namespace
{
// RW 0x73A368 / 0x73AE79: a name, "None" (stricmp) stores none
void parseNoneableName(INI *ini, void *, void *store, const void *)
{
	const std::string name = ini->getNextToken();
	*static_cast<std::string *>(store) = AsciiStringUtil::compareNoCase(name, "None") == 0 ? std::string() : name;
}

#define DM_ROWS(T)                                                                                                                                       \
	{ "DeathTypes", ParseDeathTypeFlags, nullptr, (int)offsetof(T, m_dieMux.m_deathTypes) },                                                              \
	{ "ExemptStatus", ParseObjectStatusMask, nullptr, (int)offsetof(T, m_dieMux.m_exemptStatus) },                                                        \
	{ "RequiredStatus", ParseObjectStatusMask, nullptr, (int)offsetof(T, m_dieMux.m_requiredStatus) },                                                    \
	{ "DamageAmountRequired", INI::parseReal, nullptr, (int)offsetof(T, m_dieMux.m_damageAmountRequired) },                                               \
	{ "MinKillerAngle", INI::parseAngleReal, nullptr, (int)offsetof(T, m_dieMux.m_minKillerAngle) },                                                      \
	{ "MaxKillerAngle", INI::parseAngleReal, nullptr, (int)offsetof(T, m_dieMux.m_maxKillerAngle) },                                                      \
	{ nullptr, nullptr, nullptr, 0 }

const FieldParse kCreateObjectDieMux[] = { DM_ROWS(CreateObjectDieModuleData) };
const FieldParse kFireWeaponWhenDeadMux[] = { DM_ROWS(FireWeaponWhenDeadBehaviorModuleData) };
const FieldParse kUpgradeDieMux[] = { DM_ROWS(UpgradeDieModuleData) };
const FieldParse kHeroDieMux[] = { DM_ROWS(HeroDieModuleData) };
#undef DM_ROWS

// RW 0xC61120
const FieldParse kCreateObjectDie[] = {
	{ "CreationList", parseNoneableName, nullptr, (int)offsetof(CreateObjectDieModuleData, m_creationList) },
	{ "DebrisPortionOfSelf", INI::parseAsciiString, nullptr, (int)offsetof(CreateObjectDieModuleData, m_debrisPortionOfSelf) },
	{ "UpgradeRequired", INI::parseAsciiStringVector, nullptr, (int)offsetof(CreateObjectDieModuleData, m_upgradeRequired) },
	{ nullptr, nullptr, nullptr, 0 }
};

// RW 0xC06BF0
const FieldParse kUpgradeDie[] = {
	{ "UpgradeToRemove", INI::parseAsciiString, nullptr, (int)offsetof(UpgradeDieModuleData, m_upgradeToRemove) },
	{ nullptr, nullptr, nullptr, 0 }
};

// RW 0x73B22F: the template by name from TheSpecialPowerStore (null when unknown)
void parseSpecialPowerTemplate(INI *ini, void *instance, void *, const void *)
{
	HeroDieModuleData *d = static_cast<HeroDieModuleData *>(instance);
	d->m_specialPowerTemplateName = ini->getNextToken();
	d->m_specialPowerTemplate = TheSpecialPowerStore ? TheSpecialPowerStore->findSpecialPowerTemplate(d->m_specialPowerTemplateName) : nullptr;
}
const FieldParse kHeroDie[] = { // RW 0xC06BC0
	{ "SpecialPowerTemplate", parseSpecialPowerTemplate, nullptr, 0 },
	{ nullptr, nullptr, nullptr, 0 }
};

// RW 0xC067A0
const FieldParse kFireWeaponWhenDead[] = {
	{ "StartsActive", INI::parseBool, nullptr, (int)offsetof(FireWeaponWhenDeadBehaviorModuleData, m_startsActive) },
	{ "ActiveDuringConstruction", INI::parseBool, nullptr, (int)offsetof(FireWeaponWhenDeadBehaviorModuleData, m_activeDuringConstruction) },
	{ "DelayTime", INI::parseDurationUnsignedInt, nullptr, (int)offsetof(FireWeaponWhenDeadBehaviorModuleData, m_delayTime) },
	{ "DeathWeapon", parseNoneableName, nullptr, (int)offsetof(FireWeaponWhenDeadBehaviorModuleData, m_deathWeapon) },
	{ "WeaponOffset", INI::parseCoord3D, nullptr, (int)offsetof(FireWeaponWhenDeadBehaviorModuleData, m_weaponOffset) },
	{ nullptr, nullptr, nullptr, 0 }
};

// RW 0x70BFD1: a model-space point through the thing's transform (row-major basis, the translation is the position); the products are summed in RW's order
Coord3D transformPoint(const Object &obj, const Coord3D &p)
{
	const float *m = obj.getBasis();
	const Coord3D *t = obj.getPosition();
	using namespace SimMath;
	Coord3D out;
	out.x = addf32(addf32(addf32(mulf32(m[2], p.z), mulf32(m[1], p.y)), mulf32(m[0], p.x)), t->x);
	out.y = addf32(addf32(addf32(mulf32(m[5], p.z), mulf32(m[3], p.x)), mulf32(m[4], p.y)), t->y);
	out.z = addf32(addf32(addf32(mulf32(m[8], p.z), mulf32(m[6], p.x)), mulf32(m[7], p.y)), t->z);
	return out;
}

void reportAngle(const Object &obj, const char *cls)
{
	obj.logic().noteStop(std::string("[S-980] ") + cls + " of " + obj.getTemplate()->getName() +
		": MinKillerAngle / MaxKillerAngle are not ported (the die applies as if the killer were in the window)");
}
} // namespace

void CreateObjectDieModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kCreateObjectDieMux); // RW 0xC76BD8
	p.add(kCreateObjectDie);
}

void UpgradeDieModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kUpgradeDieMux); // RW 0xC76BD8
	p.add(kUpgradeDie);
}

void HeroDieModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kHeroDieMux); // RW 0xC76BD8
	p.add(kHeroDie);
}

void FireWeaponWhenDeadBehaviorModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kFireWeaponWhenDead);   // RW 0xC067A0
	buildBaseFieldParse(p);       // RW 0xC76AD8
	p.add(kFireWeaponWhenDeadMux); // RW 0xC76BD8
}

// ---- CreateObjectDie ----------------------------------------------------------------------------------------------------------------
// RW 0x888F04
void CreateObjectDie::onDie(const DieModuleInterface::Event &event)
{
	Object *obj = getObject();
	bool angle = false;
	if (!m_data->m_dieMux.isDieApplicable(*obj, event, &angle))
	{
		return;
	}
	if (angle)
	{
		reportAngle(*obj, "CreateObjectDie");
	}
	for (const std::string &name : m_data->m_upgradeRequired)
	{
		const UpgradeTemplate *u = TheUpgradeCenter ? TheUpgradeCenter->findUpgrade(name) : nullptr; // RW 0x66F5E5
		if (!u)
		{
			continue;
		}
		bool has = false;
		if (u->getUpgradeType() == UPGRADE_TYPE_PLAYER)
		{
			const Player *p = obj->getControllingPlayer(); // RW 0x68B678
			has = p && p->hasUpgradeComplete(u);           // RW 0x6AC2AF
		}
		else
		{
			has = obj->hasUpgrade(u); // RW 0x691421
		}
		if (!has)
		{
			return;
		}
	}
	if (m_data->m_creationList.empty())
	{
		return;
	}
	GameLogic &logic = obj->logic();
	const ObjectCreationList *ocl = TheObjectCreationListStore ? TheObjectCreationListStore->findObjectCreationList(m_data->m_creationList) : nullptr;
	if (!ocl)
	{
		return; // RW: an unknown name parsed to NULL, nothing is made
	}
	logic.noteStop("[S-980] CreateObjectDie: the death's ObjectCreationList runs the position variant of OCL::create at the dead object (RW calls the object-pair "
		"variant with the killer as secondary); see S-530 for the nugget kinds that run");
	const Coord3D at = *obj->getPosition();
	m_created += (unsigned)ocl->create(logic, obj, at).size();
}

void CreateObjectDie::crc(StateHasher &hasher) const
{
	hasher.addU32(m_created);
}

// ---- FireWeaponWhenDeadBehavior -----------------------------------------------------------------------------------------------------
// RW 0x885EB6
FireWeaponWhenDeadBehavior::FireWeaponWhenDeadBehavior(Thing *thing, const FireWeaponWhenDeadBehaviorModuleData *data)
	: UpdateModule(thing, data)
	, UpgradeMux(thing ? thing->asObject() : nullptr, data)
	, m_data(data)
{
	if (data->m_startsActive)
	{
		giveSelfUpgrade(); // RW 0x855388
	}
	m_delay = data->m_delayTime > 0 ? data->m_delayTime : 0;
	setWakeFrame(getObject(), UPDATE_SLEEP_FOREVER);
}

// RW 0x885FF3
void FireWeaponWhenDeadBehavior::onDie(const DieModuleInterface::Event &event)
{
	Object *obj = getObject();
	if (m_delay > 0)
	{
		m_stored = event;                       // RW 0x7441E0
		setWakeFrame(obj, UPDATE_SLEEP(1));     // RW 0x850C32(obj, 1)
		return;
	}
	if (!isAlreadyUpgraded() && !m_data->m_startsActive)
	{
		return;
	}
	bool angle = false;
	if (!m_data->m_dieMux.isDieApplicable(*obj, event, &angle))
	{
		return;
	}
	if (angle)
	{
		reportAngle(*obj, "FireWeaponWhenDeadBehavior");
	}
	static const int kBeingCanceled = ObjectTemplateInfoBuilder::objectStatusIndex("BUILD_BEING_CANCELED"); // RW status 0x56
	if (kBeingCanceled >= 0 && obj->testStatus((unsigned)kBeingCanceled))
	{
		return;
	}
	if (!m_data->m_activeDuringConstruction && obj->testStatus(OBJECT_STATUS_UNDER_CONSTRUCTION))
	{
		return;
	}
	// slot 11: the activation and conflicting masks; any conflicting bit on the object or the controlling player's completed upgrades stops it (RW 0x8097D6)
	const UpgradeMaskType &conflicting = m_data->m_conflictingMask;
	if (obj->getUpgradeMask().testForAny(conflicting))
	{
		return;
	}
	const Player *p = obj->getControllingPlayer();
	if (p && p->getCompletedUpgradeMask().testForAny(conflicting))
	{
		return;
	}
	fire();
}

void FireWeaponWhenDeadBehavior::fire()
{
	if (m_data->m_deathWeapon.empty())
	{
		return;
	}
	Object *obj = getObject();
	GameLogic &logic = obj->logic();
	const WeaponTemplate *wt = TheWeaponStore ? TheWeaponStore->findWeaponTemplate(m_data->m_deathWeapon) : nullptr;
	if (!wt)
	{
		return; // RW: an unknown DeathWeapon parsed to NULL (RW 0x8860EE)
	}
	const Coord3D at = transformPoint(*obj, m_data->m_weaponOffset); // RW 0x70BFD1
	ObjectWeapons::createAndFireTempWeapon(wt, obj, at); // RW 0x88611D: TheWeaponStore->createAndFireTempWeapon (RW 0x6CF530; lane DECOMP-1)
	++m_shots;
	(void)logic;
}

// RW 0x885E25
UpdateSleepTime FireWeaponWhenDeadBehavior::update()
{
	if (m_delay > 0)
	{
		--m_delay;
		if (m_delay == 0)
		{
			const DieModuleInterface::Event stored = m_stored;
			onDie(stored);
		}
	}
	return UPDATE_SLEEP_NONE; // RW returns 1
}

void FireWeaponWhenDeadBehavior::crc(StateHasher &hasher) const
{
	UpdateModule::crc(hasher);
	crcMux(hasher);
	hasher.addU32(m_delay);
	hasher.addU32(m_stored.sourceId);
	hasher.addI32(m_stored.deathType);
	hasher.addI32(m_stored.damageType);
	hasher.addFloat(m_stored.damageAmount);
	hasher.addU32(m_shots);
}

// ---- UpgradeDie ---------------------------------------------------------------------------------------------------------------------
// RW 0x889EB1
void UpgradeDie::onDie(const DieModuleInterface::Event &event)
{
	Object *obj = getObject();
	bool angle = false;
	if (!m_data->m_dieMux.isDieApplicable(*obj, event, &angle))
	{
		return;
	}
	if (angle)
	{
		reportAngle(*obj, "UpgradeDie");
	}
	const UpgradeTemplate *u = TheUpgradeCenter ? TheUpgradeCenter->findUpgrade(m_data->m_upgradeToRemove) : nullptr; // RW 0x66F5E5
	if (!u)
	{
		return;
	}
	if (u->getUpgradeType() == UPGRADE_TYPE_PLAYER)
	{
		if (Player *p = obj->getControllingPlayer())
		{
			p->removeUpgrade(u, false); // RW 0x6AE60C
		}
		return;
	}
	Object *producer = obj->logic().findObjectByID(obj->getProducerID()); // RW 0x889EFA: Object + 0x78
	if (producer && producer->hasUpgrade(u))
	{
		producer->removeUpgrade(u); // RW 0x691438
	}
}

// ---- HeroDie ------------------------------------------------------------------------------------------------------------------------
// RW 0x8C64FE (callback RW 0x8C642D)
void HeroDie::onDie(const DieModuleInterface::Event &)
{
	Object *obj = getObject();
	Player *p = obj->getControllingPlayer();
	if (!p || !m_data->m_specialPowerTemplate)
	{
		return; // RW 0x8C6435: a null template matches nothing
	}
	GameLogic &logic = obj->logic();
	// RW 0x8C6514: Player::iterateObjects (RW 0x6ABABD) with the callback RW 0x8C642D, which only sets the matching module's ready frame to the current frame (slot
	// 0x20): the walk's order cannot be observed, so the logic's list of the same objects (those the player controls) gives the same result (lane MODULES-2)
	const unsigned now = logic.getFrame();
	for (Object *other = logic.getFirstObject(); other; other = other->getNextObject())
	{
		if (other->getControllingPlayer() != p)
		{
			continue;
		}
		if (SpecialPowerModuleInterface *sp = SpecialPowerModules::findModule(*other, m_data->m_specialPowerTemplate)) // RW 0x68C26D
		{
			sp->setReadyFrame(now); // slot 0x20
		}
	}
}
