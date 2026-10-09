// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// FireWeaponUpdate. See GameLogic/Module/SpellEffectModules.h for the target facts. Lane SPELL-2.

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "GameLogic/Module/SpellEffectModules.h"

#include "Common/StateHash.h"
#include "Common/Thing/ModuleFactory.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/Weapon.h"
#include "GameLogic/WeaponState.h"

#include <cstddef>
#include <stdexcept>

namespace
{
const FieldParse kNuggetFields[] = { // RW 0xC628F0
	{ "WeaponName", INI::parseAsciiString, nullptr, (int)offsetof(FireWeaponNuggetData, weaponName) },
	{ "FireDelay", INI::parseDurationUnsignedInt, nullptr, (int)offsetof(FireWeaponNuggetData, fireDelay) },
	{ "OneShot", INI::parseBool, nullptr, (int)offsetof(FireWeaponNuggetData, oneShot) },
	{ "Offset", INI::parseCoord3D, nullptr, (int)offsetof(FireWeaponNuggetData, offset) },
	{ nullptr, nullptr, nullptr, 0 }
};

// RW 0x88F327: a FireWeaponNugget sub-block, appended to the list (RW 0x60011F)
void parseFireWeaponNugget(INI *ini, void *instance, void *, const void *)
{
	FireWeaponNuggetData n;
	MultiIniFieldParse multi;
	multi.add(kNuggetFields);
	ini->initFromINIMulti(&n, multi);
	static_cast<FireWeaponUpdateModuleData *>(instance)->m_nuggets.push_back(n);
}

const FieldParse kFireWeaponUpdateFields[] = { // RW 0xC62A40
	{ "FireWeaponNugget", parseFireWeaponNugget, nullptr, 0 },
	{ "HeroModeTrigger", INI::parseBool, nullptr, (int)offsetof(FireWeaponUpdateModuleData, m_heroModeTrigger) },
	{ "ChargingModeTrigger", INI::parseBool, nullptr, (int)offsetof(FireWeaponUpdateModuleData, m_chargingModeTrigger) },
	{ "AliveOnly", INI::parseBool, nullptr, (int)offsetof(FireWeaponUpdateModuleData, m_aliveOnly) },
	{ nullptr, nullptr, nullptr, 0 }
};

template <class Runtime, class Data>
void bindUpdate(ModuleFactory &modules, const char *name)
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
} // namespace

void FireWeaponUpdateModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kFireWeaponUpdateFields);
}

// ---- FireWeaponUpdate ----------------------------------------------------------------------------------------------------------------------
FireWeaponUpdate::FireWeaponUpdate(Thing *thing, const FireWeaponUpdateModuleData *data)
	: UpdateModule(thing, data)
	, m_data(data)
{
	Object *obj = getObject();
	const std::uint32_t now = obj->logic().getFrame();
	for (const FireWeaponNuggetData &n : data->m_nuggets) // RW 0x88F47E ..
	{
		const WeaponTemplate *tmpl = TheWeaponStore ? TheWeaponStore->findWeaponTemplate(n.weaponName) : nullptr; // RW 0x6CC5DF
		if (!tmpl)
		{
			continue;
		}
		Record r;
		r.oneShot = n.oneShot;
		r.fireFrame = n.fireDelay + now;
		r.offset = n.offset;
		r.weapon = std::make_unique<Weapon>(tmpl, PRIMARY_WEAPON, now); // RW 0x68B150(tmpl, 0)
		r.weapon->setOwnerID(obj->getID());
		weaponHost().loadExtraWeapon(*r.weapon); // RW 0x6CEE0F
		m_records.push_back(std::move(r));
	}
	setWakeFrame(obj, UPDATE_SLEEP(1)); // RW 0x88F769 (0x850C32(obj, 1))
}

FireWeaponUpdate::~FireWeaponUpdate() = default;

ObjectWeapons &FireWeaponUpdate::weaponHost()
{
	if (ObjectWeapons *own = getObject()->getWeapons())
	{
		return *own;
	}
	if (!m_ownHost)
	{
		m_ownHost = std::make_unique<ObjectWeapons>(*getObject()); // S-924
	}
	return *m_ownHost;
}

UpdateSleepTime FireWeaponUpdate::update()
{
	Object *obj = getObject();
	static const int kHero = CombatNames::modelCondition("HERO");         // Object + 0x124 bit 28
	static const int kCharging = CombatNames::modelCondition("CHARGING"); // Object + 0x11C bit 4
	bool gate = (!m_data->m_heroModeTrigger || obj->testModelCondition(kHero)) && (!m_data->m_chargingModeTrigger || obj->testModelCondition(kCharging)) &&
		(!m_data->m_aliveOnly || !obj->isEffectivelyDead());
	if (gate && !obj->logic().aiWorld())
	{
		// the combat geometry of a shot is the pathfinder's: a logic without TheAI (a test harness; every game has one) does not fire, and says so
		obj->logic().noteStop("[S-924] FireWeaponUpdate: the logic has no AIWorld, the weapons of the effect objects do not fire");
		++m_unfired;
		gate = false;
	}
	if (gate)
	{
		const std::uint32_t now = obj->logic().getFrame();
		for (Record &r : m_records)
		{
			if (!(r.fireFrame < now) || weaponHost().extraWeaponStatus(*r.weapon) != WEAPON_READY_TO_FIRE)
			{
				continue;
			}
			Coord3D at = *obj->getPosition();
			const float lsq = SimMath::addf32(SimMath::addf32(SimMath::mulf32(r.offset.x, r.offset.x), SimMath::mulf32(r.offset.y, r.offset.y)),
				SimMath::mulf32(r.offset.z, r.offset.z)); // Coord3D::lengthSqr (SSE)
			if (lsq > 0.0f)
			{
				// RW 0x88F5F5 ..: s = sin(angle), c = cos(angle) (CRT, stored as floats); x87: (c * x - s * y) + px, c * y + s * x + py
				const float angle = obj->getOrientation();
				const float s = SimMath::fstpDword(SimMath::sind(angle));
				const float c = SimMath::fstpDword(SimMath::cosd(angle));
				at.x = SimMath::fstpDword(SimMath::pc24AddW(SimMath::pc24SubW(SimMath::pc24MulW(c, r.offset.x), SimMath::pc24MulW(s, r.offset.y)), at.x));
				at.y = SimMath::fstpDword(SimMath::pc24AddW(SimMath::pc24AddW(SimMath::pc24MulW(c, r.offset.y), SimMath::pc24MulW(s, r.offset.x)), at.y));
			}
			weaponHost().fireExtraWeaponAt(*r.weapon, at); // RW 0x6CF3D2
			++m_shots;
			if (r.oneShot)
			{
				r.fireFrame = 0xFFFFFFFFu;
			}
		}
	}
	return nextSleep();
}

UpdateSleepTime FireWeaponUpdate::nextSleep() const
{
	std::uint32_t next = 0xFFFFFFFFu;
	for (const Record &r : m_records)
	{
		const std::uint32_t ready = r.weapon->whenWeCanFireAgain();
		const std::uint32_t at = r.fireFrame > ready ? r.fireFrame : ready; // RW 0x88F0CB
		next = at < next ? at : next;
	}
	if (next == 0xFFFFFFFFu)
	{
		return UPDATE_SLEEP_FOREVER;
	}
	const std::uint32_t now = getObject()->logic().getFrame();
	return next <= now ? UPDATE_SLEEP(1) : UPDATE_SLEEP((int)(next - now));
}

void FireWeaponUpdate::crc(StateHasher &h) const
{
	UpdateModule::crc(h);
	h.addU32((std::uint32_t)m_records.size());
	for (const Record &r : m_records)
	{
		h.addU32(r.fireFrame);
		h.addBool(r.oneShot);
		r.weapon->crc(h);
	}
	h.addU64(m_shots);
}

void SpellEffectModules::registerAll(ModuleFactory &modules)
{
	bindUpdate<FireWeaponUpdate, FireWeaponUpdateModuleData>(modules, "FireWeaponUpdate");
}

std::string SpellEffectModules::stopLine()
{
	return "[S-924] DeletionUpdate (RW 0x88B7B0, lane MODULES-1's) and FireWeaponUpdate (RW 0x88F731 / 0x88F554) run (lane SPELL-2: the spell book's effect objects fire their weapons "
		   "and go); FireWeaponUpdate's weapons fire through WEAPON-1's privateFireWeapon at a position (Weapon::forceFireWeapon RW 0x6CF3D2) with an "
		   "ObjectWeapons host made for an object without a WeaponSet (inference), and the weapons' own unported nugget kinds stay counted by the delivery";
}
