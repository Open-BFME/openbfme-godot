// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// The update modules of the spell book's effect objects (lane SPELL-2): RotWK's FireWeaponUpdate with its FireWeaponNugget list (ZH has a single Weapon field; BFME2 rewrote it). The
// arrow volleys, bombards, rains of fire, earthquakes ... of the spell book are objects made by their OCL that fire weapons through FireWeaponUpdate
// and go away through DeletionUpdate.
//
// TARGET FACTS (RotWK game.dat, caveat S-001), read from the disassembly:
//   * DeletionUpdate: lane MODULES-1's (GameLogic/Module/ExtraUpdateModules.h), with this lane's setLifetimeRange (RW 0x88B830).
//   * FireWeaponUpdate (create RW 0x64CBF7, data RW 0x64CC2F, table RW 0xC62A40: FireWeaponNugget (RW 0x88F327: a 0x18-byte nugget parsed with
//     RW 0xC628F0 { WeaponName + 0 (a name), FireDelay + 4 (parseDurationUnsignedInt), OneShot + 8, Offset + 0xC (Coord3D) }, appended to the list at
//     data + 8), HeroModeTrigger + 0xC, ChargingModeTrigger + 0xD, AliveOnly + 0xE; module constructor RW 0x88F731):
//       - the constructor (RW 0x88F464): for each nugget whose weapon the WeaponStore has (RW 0x6CC5DF): a runtime record { a new Weapon of slot 0
//         (RW 0x68B150) owned by the object (+ 8 = object id), loaded at once (RW 0x6CEE0F), fire frame = now + FireDelay, OneShot, Offset }; then the
//         module wakes next frame (RW 0x850C32(obj, 1));
//       - update (RW 0x88F554, interface slot 0): only when (HeroModeTrigger: model condition HERO, Object + 0x124 bit 28 = condition 220) and
//         (ChargingModeTrigger: CHARGING, + 0x11C bit 4 = 132) and (AliveOnly: not effectively dead, + 0x458 bit 0); then every record whose fire frame
//         is below the frame and whose weapon is READY_TO_FIRE (RW 0x6CDCE7) fires at the object's position, plus the Offset turned by the object's
//         orientation (CRT sin / cos, x87) when the Offset is not zero, through Weapon::forceFireWeapon (RW 0x6CF3D2); a OneShot record's fire frame
//         becomes 0xFFFFFFFF;
//       - the sleep (RW 0x88F0AA): the smallest max(fire frame, the weapon's whenWeCanFireAgain + 0x18) over the records; none (0xFFFFFFFF) sleeps
//         forever, a frame already passed wakes next frame, else the difference.
//   * FloodUpdate (lane SPELL-3; the spell book's Flood, Arwen's flood): module data table RW 0xC631D8 { FloodMember (RW 0x891696: a 0x38-byte member parsed
//     with RW 0xC63080 { MemberTemplateName + 0 (parseAsciiString), ControlPointOffsetOne .. Four + 4 / + 0x10 / + 0x1C / + 0x28 (parseCoord3D RW 0x42F247),
//     MemberSpeed + 0x34 (parseReal RW 0x42ED00) }, appended to the list at data + 8), AngleOfFlow + 0xC (parseAngleReal RW 0x42EE15), DirectionIsRelative
//     + 0x10 (parseBool) }; constructor RW 0x891713; the module keeps a list of { member object id, its path (Coord3D vector), the path index } at + 0x20 and
//     a members-made flag at + 0x24:
//       - the path (RW 0x8912C5): angle = AngleOfFlow, plus the object's orientation through normalizeAngle (RW 0x644FD0) when DirectionIsRelative; each
//         control point is (c * x - s * y) + pos.x, (x * s + c * y) + pos.y, z = 0 with s / c the MSVCR71 sin / cos of the angle (RW 0xA3CF90 / 0xA3CF84,
//         stored as floats, scalar SSE); the BezierSegment of the four (RW 0x960E8D), its approximate length at tolerance 1 (RW 0x9610EF) divided by
//         MemberSpeed on the x87, MSVCR71 ceil, _ftol2, at least 2 segments (`cmp count, 2; jg`), getSegmentPoints (RW 0x9612A3);
//       - the members (RW 0x8917FD, first update): per FloodMember in order, the template by name (RW 0x6D1305; unknown: nothing), a new object on the
//         flood's team (RW 0x6D165E), its path, then its transform: Matrix3D::buildTransformMatrix (RW 0xB26610) at the first path point along
//         (second - first) normalised (SSE length (z^2 + y^2) + x^2, Inv_Sqrt RW 0x441C56, x87 products), Object::setTransformMatrix (RW 0x68DA67);
//       - update (RW 0x8919BE, every frame: UPDATE_SLEEP_NONE): the members first when not made; then per record whose object exists: the index
//         grows; at or past the path's end the object is killed (RW 0x698EC3, UNRESISTABLE, DEATH_NORMAL); else it stands on that point at the terrain
//         height (TerrainLogic vslot 0x18) and, while a next point exists, turns by the relative angle to it (RW 0x4B3D8D, x87 fadd with the
//         orientation, RW 0x70C31E).
//     BFME2 decomp GameLogic/Object/Update/FloodUpdate.cpp (FloodUpdate::update, tier A for RW 0x8919BE; the member maker tier B same-shape for RW 0x8917FD)
//     and FloodUpdate.cpp (was reverse/attempts/0x0048dc51.cpp, matched since; the path builder above is read from the RotWK disassembly).
// INFERENCE (S-924): the weapon's firing path is WEAPON-1's privateFireWeapon at a position through the object's weapon host; ObjectWeapons made for an
// object without a WeaponSet only to host this weapon (the object's own weapon state is not changed).

#pragma once

#include "Common/INI.h"
#include "Common/INIDataTypes.h"
#include "GameLogic/Module/UpdateModule.h"
#include "GameLogic/ObjectTypes.h"

#include <memory>
#include <string>
#include <vector>

class ModuleFactory;
class ObjectWeapons;
class Weapon;
class WeaponTemplate;

struct FireWeaponNuggetData
{
	std::string weaponName;  ///< + 0
	unsigned fireDelay = 0;  ///< + 4 (frames)
	bool oneShot = false;    ///< + 8
	Coord3D offset{};        ///< + 0xC
};

class FireWeaponUpdateModuleData : public ModuleData
{
public:
	std::vector<FireWeaponNuggetData> m_nuggets; ///< + 8
	bool m_heroModeTrigger = false;     ///< + 0xC
	bool m_chargingModeTrigger = false; ///< + 0xD
	bool m_aliveOnly = false;           ///< + 0xE
	static void buildFieldParse(MultiIniFieldParse &p);
};

class FireWeaponUpdate : public UpdateModule
{
public:
	OBFME_MODULE_XFER(FireWeaponUpdate) // lane SAVE-4b
	FireWeaponUpdate(Thing *thing, const FireWeaponUpdateModuleData *data);
	~FireWeaponUpdate() override;
	UpdateSleepTime update() override; ///< RW 0x88F554
	unsigned long long shots() const { return m_shots; }
	unsigned long long unfiredUpdates() const { return m_unfired; } ///< updates of a logic without TheAI (noted as S-924)
	// lane NUGGET-1: RW 0x88F041 (DamageFieldNugget RW 0x90F72A): the module's single weapon (+ 0x20), a new Weapon of slot 0 (RW 0x68B150) owned by the object and
	// loaded at once (RW 0x6CEE0F), replacing the previous one; a null template does nothing. While it is set the update fires only it (RW 0x88F554: READY_TO_FIRE,
	// at the object's position, RW 0x6CF3D2) and not the nugget list; the sleep is still the list's (RW 0x88F0AA reads only the list)
	void setWeapon(const WeaponTemplate *tmpl);
	const Weapon *singleWeapon() const { return m_weapon.get(); }
	void crc(StateHasher &hasher) const override;

private:
	struct Record
	{
		std::unique_ptr<Weapon> weapon;
		unsigned fireFrame = 0xFFFFFFFFu;
		bool oneShot = false;
		Coord3D offset{};
	};
	UpdateSleepTime nextSleep() const; ///< RW 0x88F0AA
	ObjectWeapons &weaponHost();
	const FireWeaponUpdateModuleData *m_data;
	std::vector<Record> m_records;
	std::unique_ptr<ObjectWeapons> m_ownHost; ///< the host of an object without a WeaponSet (S-924)
	std::unique_ptr<Weapon> m_weapon;         ///< + 0x20 (lane NUGGET-1)
	unsigned long long m_shots = 0;
	unsigned long long m_unfired = 0;
};

struct FloodMemberData
{
	std::string templateName;       ///< + 0
	Coord3D controlPointOffset[4]{}; ///< + 4 / + 0x10 / + 0x1C / + 0x28
	float speed = 0.0f;              ///< + 0x34
};

class FloodUpdateModuleData : public ModuleData
{
public:
	std::vector<FloodMemberData> m_members; ///< + 8
	float m_angleOfFlow = 0.0f;             ///< + 0xC (radians)
	bool m_directionIsRelative = false;     ///< + 0x10
	static void buildFieldParse(MultiIniFieldParse &p);
};

class FloodUpdate : public UpdateModule
{
public:
	OBFME_MODULE_XFER(FloodUpdate) // lane SAVE-4b
	FloodUpdate(Thing *thing, const FloodUpdateModuleData *data);
	UpdateSleepTime update() override; ///< RW 0x8919BE
	void crc(StateHasher &hasher) const override;
	struct Record
	{
		ObjectID id = INVALID_ID;  ///< + 0
		std::vector<Coord3D> path; ///< + 4
		int index = 0;             ///< + 0x10
	};
	const std::vector<Record> &records() const { return m_records; }
	bool membersCreated() const { return m_membersCreated; }

private:
	void buildPath(const FloodMemberData &member, Record &record) const; ///< RW 0x8912C5
	void createMembers();                                               ///< RW 0x8917FD
	const FloodUpdateModuleData *m_data;
	std::vector<Record> m_records; ///< + 0x20 (a list in retail; only appended to)
	bool m_membersCreated = false; ///< + 0x24
};

namespace SpellEffectModules
{
void registerAll(ModuleFactory &modules);
std::string stopLine(); ///< [S-924]
}
