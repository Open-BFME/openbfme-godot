// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// FireWeaponUpdate (lane SPELL-2) and FloodUpdate (lane SPELL-3). See GameLogic/Module/SpellEffectModules.h for the target facts.

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif

#include "GameLogic/Module/SpellEffectModules.h"
#include "GameLogic/SnapshotXfer.h"

#include "Common/StateHash.h"
#include "Common/Thing/ModuleFactory.h"
#include "GameLogic/Combat/BezierSegment.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/PhysicsBehavior.h"
#include "GameLogic/Module/ProjectileModules.h"
#include "Common/Thing/ThingFactory.h"
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

void parseLegacyFireWeapon(INI *ini, void *instance, void *, const void *field)
{
	auto &nuggets = static_cast<FireWeaponUpdateModuleData *>(instance)->m_nuggets;
	if (nuggets.empty()) nuggets.emplace_back();
	auto &n = nuggets.front();
	switch ((std::uintptr_t)field)
	{
		case 0: INI::parseAsciiString(ini, nullptr, &n.weaponName, nullptr); break;
		case 1: INI::parseDurationUnsignedInt(ini, nullptr, &n.fireDelay, nullptr); break;
		case 2: INI::parseBool(ini, nullptr, &n.oneShot, nullptr); break;
	}
}

const FieldParse kFireWeaponUpdateFields[] = { // RW 0xC62A40
	{ "FireWeaponNugget", parseFireWeaponNugget, nullptr, 0 },
	{ "HeroModeTrigger", INI::parseBool, nullptr, (int)offsetof(FireWeaponUpdateModuleData, m_heroModeTrigger) },
	{ "ChargingModeTrigger", INI::parseBool, nullptr, (int)offsetof(FireWeaponUpdateModuleData, m_chargingModeTrigger) },
	{ "AliveOnly", INI::parseBool, nullptr, (int)offsetof(FireWeaponUpdateModuleData, m_aliveOnly) },
	{ nullptr, nullptr, nullptr, 0 }
};
// community PR #36: BFME1's single-weapon FireWeaponUpdate rows (Weapon / FireDelay / OneShot fill the first nugget) (BFME1 data only, Common/BfmeGame.h); RotWK's table stays the binary's
const FieldParse kBfme1FireWeaponUpdate[] = {
	{ "Weapon", parseLegacyFireWeapon, (const void *)0, 0 },
	{ "FireDelay", parseLegacyFireWeapon, (const void *)1, 0 },
	{ "OneShot", parseLegacyFireWeapon, (const void *)2, 0 },
	{ nullptr, nullptr, nullptr, 0 },
};
const Bfme1FieldRowsRegistration kBfme1FireWeaponUpdateRegistration(kFireWeaponUpdateFields, kBfme1FireWeaponUpdate);


const FieldParse kFloodMemberFields[] = { // RW 0xC63080
	{ "MemberTemplateName", INI::parseAsciiString, nullptr, (int)offsetof(FloodMemberData, templateName) },
	{ "ControlPointOffsetOne", INI::parseCoord3D, nullptr, (int)offsetof(FloodMemberData, controlPointOffset) },
	{ "ControlPointOffsetTwo", INI::parseCoord3D, nullptr, (int)(offsetof(FloodMemberData, controlPointOffset) + sizeof(Coord3D)) },
	{ "ControlPointOffsetThree", INI::parseCoord3D, nullptr, (int)(offsetof(FloodMemberData, controlPointOffset) + 2 * sizeof(Coord3D)) },
	{ "ControlPointOffsetFour", INI::parseCoord3D, nullptr, (int)(offsetof(FloodMemberData, controlPointOffset) + 3 * sizeof(Coord3D)) },
	{ "MemberSpeed", INI::parseReal, nullptr, (int)offsetof(FloodMemberData, speed) },
	{ nullptr, nullptr, nullptr, 0 }
};

// RW 0x891696: a FloodMember sub-block, appended to the list (RW 0x60011F)
void parseFloodMember(INI *ini, void *instance, void *, const void *)
{
	FloodMemberData m;
	MultiIniFieldParse multi;
	multi.add(kFloodMemberFields);
	ini->initFromINIMulti(&m, multi);
	static_cast<FloodUpdateModuleData *>(instance)->m_members.push_back(m);
}

const FieldParse kFloodUpdateFields[] = { // RW 0xC631D8
	{ "FloodMember", parseFloodMember, nullptr, 0 },
	{ "AngleOfFlow", INI::parseAngleReal, nullptr, (int)offsetof(FloodUpdateModuleData, m_angleOfFlow) },
	{ "DirectionIsRelative", INI::parseBool, nullptr, (int)offsetof(FloodUpdateModuleData, m_directionIsRelative) },
	{ nullptr, nullptr, nullptr, 0 }
};

// RW 0x644FD0: into (-PI, PI] (two SSE loops)
float floodNormalizeAngle(float a)
{
	const float pi = 3.14159274f;
	const float twoPi = 6.28318548f;
	while (a > pi)
	{
		a = SimMath::subf32(a, twoPi);
	}
	while (-pi >= a)
	{
		a = SimMath::addf32(a, twoPi);
	}
	return a;
}

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
	if (gate && m_weapon)
	{
		// RW 0x88F5B6 .. 0x88F5CF: the single weapon alone, at the object's position
		if (weaponHost().extraWeaponStatus(*m_weapon) == WEAPON_READY_TO_FIRE)
		{
			weaponHost().fireExtraWeaponAt(*m_weapon, *obj->getPosition()); // RW 0x6CF3D2
			++m_shots;
		}
	}
	else if (gate)
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

void FireWeaponUpdate::setWeapon(const WeaponTemplate *tmpl)
{
	if (!tmpl)
	{
		return;
	}
	Object *obj = getObject();
	m_weapon = std::make_unique<Weapon>(tmpl, PRIMARY_WEAPON, obj->logic().getFrame()); // RW 0x68B150(tmpl, 0); the old one deleted (slot 0)
	m_weapon->setOwnerID(obj->getID());
	weaponHost().loadExtraWeapon(*m_weapon); // RW 0x6CEE0F
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
	if (m_weapon) // lane NUGGET-1: only when set, so an object without one hashes as before
	{
		h.addU32(0x4E554731u);
		m_weapon->crc(h);
	}
}

// ---- FloodUpdate (lane SPELL-3) ------------------------------------------------------------------------------------------------------------
void FloodUpdateModuleData::buildFieldParse(MultiIniFieldParse &p)
{
	p.add(kFloodUpdateFields);
}

// RW 0x891713
FloodUpdate::FloodUpdate(Thing *thing, const FloodUpdateModuleData *data)
	: UpdateModule(thing, data)
	, m_data(data)
{
}

// RW 0x8912C5
void FloodUpdate::buildPath(const FloodMemberData &member, Record &record) const
{
	const Object *obj = getObject();
	float angle = m_data->m_angleOfFlow;
	if (m_data->m_directionIsRelative)
	{
		angle = floodNormalizeAngle(SimMath::addf32(obj->getOrientation(), angle)); // RW 0x8912EE .. 0x891302 (SSE add, the result through ST0)
	}
	const Coord3D pos = *obj->getPosition();
	Coord3D cp[4];
	for (int i = 0; i < 4; ++i)
	{
		// RW 0x891340 .. 0x891548 (the same for each point): s / c the CRT sin / cos of the angle, stored as floats; scalar SSE
		const float s = SimMath::fstpDword(SimMath::sind(angle));
		const float c = SimMath::fstpDword(SimMath::cosd(angle));
		const Coord3D &o = member.controlPointOffset[i];
		cp[i].x = SimMath::addf32(SimMath::subf32(SimMath::mulf32(c, o.x), SimMath::mulf32(s, o.y)), pos.x);
		cp[i].y = SimMath::addf32(SimMath::addf32(SimMath::mulf32(o.x, s), SimMath::mulf32(c, o.y)), pos.y);
		cp[i].z = 0.0f;
	}
	const BezierSegment segment(cp); // RW 0x960E8D
	// RW 0x891558 .. 0x891583: the length (x87, RW 0x9610EF) divided by the speed on the x87, MSVCR71 ceil, _ftol2
	const double quotient = SimMath::pc24DivW(segment.getApproximateLength(1.0f), (double)member.speed);
	int count = (int)SimMath::ftol2Low32(SimMath::ceilD(quotient));
	count = count > 2 ? count : 2; // RW 0x891588 .. 0x89159B
	segment.getSegmentPoints(count, &record.path); // RW 0x9612A3
}

// RW 0x8917FD
void FloodUpdate::createMembers()
{
	Object *obj = getObject();
	GameLogic &logic = obj->logic();
	for (const FloodMemberData &member : m_data->m_members)
	{
		const ThingTemplate *tt = logic.things().findTemplate(member.templateName); // RW 0x6D1305
		if (!tt)
		{
			// RW 0x891852: retail skips the member silently; the port reports it (no silent default, review r1)
			logic.reportError("FloodUpdate of " + obj->getTemplate()->getName() + ": the FloodMember template '" + member.templateName + "' is unknown; the member is not made");
			continue;
		}
		Object *m = logic.newObject(tt, obj->getTeam(), ObjectStatusMaskType{}); // RW 0x6D165E (the flood's team, an empty status mask)
		if (!m)
		{
			continue;
		}
		Record r;
		r.id = m->getID();
		buildPath(member, r);
		const Coord3D start = r.path[0];
		const Coord3D next = r.path[1];
		// RW 0x8918A4 .. 0x891952: the direction, normalised when its length is not 0 (SSE (z^2 + y^2) + x^2, Inv_Sqrt RW 0x441C56, x87 products)
		Coord3D dir{ SimMath::subf32(next.x, start.x), SimMath::subf32(next.y, start.y), SimMath::subf32(next.z, start.z) };
		const float lenSq = SimMath::addf32(SimMath::addf32(SimMath::mulf32(dir.z, dir.z), SimMath::mulf32(dir.y, dir.y)), SimMath::mulf32(dir.x, dir.x));
		if (lenSq != 0.0f)
		{
			const float inv = ProjectileInvSqrt(lenSq);
			dir.x = SimMath::pc24Mul(dir.x, inv);
			dir.y = SimMath::pc24Mul(dir.y, inv);
			dir.z = SimMath::pc24Mul(dir.z, inv);
		}
		// Matrix3D::buildTransformMatrix (RW 0xB26610; the same build as BezierProjectileBehavior's, ProjectileModules.cpp): yaw about Z by (dir.x, dir.y) / len2,
		// then the pitch about Y; Object::setTransformMatrix (RW 0x68DA67)
		const float len2 = SimMath::fstpDword(SimMath::sqrtPC24(SimMath::pc24AddW(SimMath::pc24MulW(dir.x, dir.x), SimMath::pc24MulW(dir.y, dir.y))));
		float siny = 0.0f, cosy = 1.0f;
		if (len2 != 0.0f)
		{
			const float inv2 = SimMath::divf32(1.0f, len2);
			siny = SimMath::mulf32(inv2, dir.y);
			cosy = SimMath::mulf32(inv2, dir.x);
		}
		const float sinp = dir.z, cosp = len2;
		const float basis[9] = { SimMath::mulf32(cosy, cosp), SimMath::subf32(0.0f, siny), SimMath::subf32(0.0f, SimMath::mulf32(cosy, sinp)),
			SimMath::mulf32(siny, cosp), cosy, SimMath::subf32(0.0f, SimMath::mulf32(siny, sinp)), sinp, 0.0f, cosp };
		m->teleportTransform(&start, basis); // RW 0x89197A: RW 0x68DA67
		m_records.push_back(std::move(r)); // RW 0x891986
	}
	m_membersCreated = true;
}

// RW 0x8919BE
UpdateSleepTime FloodUpdate::update()
{
	if (!m_membersCreated)
	{
		createMembers();
	}
	GameLogic &logic = getObject()->logic();
	for (Record &r : m_records)
	{
		Object *m = logic.findObjectByID(r.id);
		if (!m)
		{
			continue;
		}
		++r.index;
		if ((unsigned)r.index >= r.path.size()) // RW 0x891A18: unsigned
		{
			m->kill(DAMAGE_UNRESISTABLE, DEATH_NORMAL); // RW 0x891A20: kill(8 = UNRESISTABLE, 0)
			continue;
		}
		Coord3D p = r.path[(size_t)r.index];
		p.z = logic.getGroundHeight(p.x, p.y);
		m->setPosition(&p); // RW 0x70C201
		if ((unsigned)(r.index + 1) < r.path.size())
		{
			const Coord3D next = r.path[(size_t)r.index + 1];
			const float orientation = m->getOrientation(); // read before the angle (RW 0x891AA6)
			m->setOrientation(SimMath::pc24Add(ObjectKnockback::relativeAngle2D(*m, next), orientation)); // RW 0x891AB5 .. 0x891AC3
		}
	}
	return UPDATE_SLEEP_NONE;
}

void FloodUpdate::crc(StateHasher &h) const
{
	UpdateModule::crc(h);
	h.addBool(m_membersCreated);
	h.addU32((std::uint32_t)m_records.size());
	for (const Record &r : m_records)
	{
		h.addU32(r.id);
		h.addI32(r.index);
		h.addU32((std::uint32_t)r.path.size());
	}
}

void SpellEffectModules::registerAll(ModuleFactory &modules)
{
	bindUpdate<FireWeaponUpdate, FireWeaponUpdateModuleData>(modules, "FireWeaponUpdate");
	bindUpdate<FloodUpdate, FloodUpdateModuleData>(modules, "FloodUpdate"); // lane SPELL-3
}

std::string SpellEffectModules::stopLine()
{
	return "[S-924] DeletionUpdate (RW 0x88B7B0, lane MODULES-1's) and FireWeaponUpdate (RW 0x88F731 / 0x88F554) run (lane SPELL-2: the spell book's effect objects fire their weapons "
		   "and go); FireWeaponUpdate's weapons fire through WEAPON-1's privateFireWeapon at a position (Weapon::forceFireWeapon RW 0x6CF3D2) with an "
		   "ObjectWeapons host made for an object without a WeaponSet (inference), and the weapons' own unported nugget kinds stay counted by the delivery";
}

// ---- lane SAVE-4b: the snapshots (OpenBFME native, S-4190) ------------------------------------------------------------------------------------------------
// FireWeaponUpdate's records are made by its constructor from the data (the same list for the object a load makes); each loads its weapon's state and its
// timer. The weapon host of an object without a WeaponSet (S-924) is saved whole.
void FireWeaponUpdate::xfer(Xfer &x)
{
	// version 2 (lane NUGGET-1): the single weapon of RW 0x88F041 after the counters
	XferVersion v{ 1, 2 };
	x.xferVersion(v);
	xferUpdateModule(x);
	std::uint32_t n = x.isLoading() ? 0 : (std::uint32_t)m_records.size();
	x.xferUnsignedInt(&n);
	if (n != m_records.size())
	{
		throw XferException("the snapshot's FireWeaponUpdate has " + std::to_string(n) + " weapons, this one " + std::to_string(m_records.size()));
	}
	for (Record &r : m_records)
	{
		x.xferUnsignedInt(&r.fireFrame);
		x.xferBool(&r.oneShot);
		SnapshotXfer::coord3D(x, r.offset);
		r.weapon->xfer(x);
	}
	bool ownHost = !x.isLoading() && m_ownHost != nullptr;
	x.xferBool(&ownHost);
	if (x.isLoading() && ownHost != (m_ownHost != nullptr))
	{
		m_ownHost.reset();
		if (ownHost)
		{
			m_ownHost = std::make_unique<ObjectWeapons>(*getObject());
		}
	}
	if (m_ownHost)
	{
		m_ownHost->xfer(x);
	}
	SnapshotXfer::u64(x, m_shots);
	SnapshotXfer::u64(x, m_unfired);
	if (v.current >= 2)
	{
		std::string name = !x.isLoading() && m_weapon ? m_weapon->getTemplate()->getName() : std::string();
		x.xferAsciiString(&name);
		if (x.isLoading())
		{
			m_weapon.reset();
			if (!name.empty())
			{
				const WeaponTemplate *tmpl = TheWeaponStore ? TheWeaponStore->findWeaponTemplate(name) : nullptr;
				if (!tmpl)
				{
					throw XferException("the snapshot's FireWeaponUpdate weapon '" + name + "' is not in the WeaponStore");
				}
				m_weapon = std::make_unique<Weapon>(tmpl, PRIMARY_WEAPON, getObject()->logic().getFrame());
				m_weapon->setOwnerID(getObject()->getID());
			}
		}
		if (m_weapon)
		{
			m_weapon->xfer(x);
		}
	}
}

// the flood's members (their ids, paths and cursors)
void FloodUpdate::xfer(Xfer &x)
{
	SnapshotXfer::version(x, 1, 1);
	xferUpdateModule(x);
	x.xferBool(&m_membersCreated);
	SnapshotXfer::each(x, m_records, 12, [&](Record &r) {
		x.xferObjectID(&r.id);
		SnapshotXfer::coords(x, r.path);
		x.xferInt(&r.index);
		SnapshotXfer::checkRange(r.index, 0, r.path.size(), "a flood member's path index");
	});
}
