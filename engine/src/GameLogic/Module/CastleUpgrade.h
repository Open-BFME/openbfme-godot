// OpenBFME. GPL-3.0.
//
// CastleUpgrade (lane FACT-B): the upgrade module behind a fortress improvement. The citadel's improvement button researches a "...Trigger" OBJECT
// upgrade; this module, triggered by it, hands the improvement's real upgrade (Upgrade =) to the whole castle and to the walls around.
//
// TARGET FACTS (RotWK game.dat, caveat S-001):
//   * data (field table RW 0xC6F524, after the UpgradeModuleData base): Upgrade (+0x138, parseAsciiString RW 0x42EE5E, the name: TheUpgradeCenter
//     looks it up at each use, RW 0x66F5E5), WallUpgradeRadius (+0x13C, parseReal RW 0x42ED00). Create RW 0x650334 (tier A).
//   * upgradeImplementation (RW 0x8BAAC0, RotWK's own; the decomp has its radius pass only): the upgrade by name; when the object has a
//     CastleMemberBehavior whose castle (+0x18) exists, the castle's CastleBehavior grants it (RW 0x79986B, tier A, BFME2 decomp
//     Rva00397357.cpp: the keep (+0x38) gets it unless it has it already; each foundation of the two lists (+0x50 BASE_FOUNDATION / WALL_UPGRADE,
//     +0x74 BASE_DEFENSE_FOUNDATION, RW 0x7972D3 in BFME2: Rva003972D3Apply.cpp) gets it and so does the occupant of a taken plot that lacks it and
//     is affected by it; each other member (+0x5C) that lacks it and is affected gets it; "gets it" is Object::giveUpgradeSelf RW 0x693817), then
//     the castle object itself giveUpgradeSelf (RW 0x8BAB55). Last, the radius pass (RW 0x8BA9EC, tier B of BFME2 0x4B68B6): every object within
//     WallUpgradeRadius of the object (ThePartitionManager, distance type 1: FROM_CENTER_3D, RW 0x8BAA42) that is ANY of WALL_SEGMENT, WALL_HUB,
//     WALL_UPGRADE, DEFENSIVE_WALL (the KindOf mask RW 0x46E771(0, 0xBD, 0x9C, 0x96, 0x3D); filter vtable RW 0xC309B4 -> RW 0x660DC6) and has the
//     same controlling player gets Object::giveUpgrade (RW 0x69388B).
//   * INFERENCE: the occupant of a taken foundation is the CastleMemberBehavior's occupant (the object RW 0x68C3A3's interface answers, slot 0x18
//     "taken" and slot 0x18 / 0x18 "occupant": the port keeps them on CastleMemberBehavior, S-952).

#pragma once

#include "GameLogic/Module/UpgradeModule.h"

#include <string>

class ModuleFactory;

class CastleUpgradeModuleData : public UpgradeModuleData
{
public:
	std::string m_upgradeName;      ///< +0x138 Upgrade
	float m_wallUpgradeRadius = 0.0f; ///< +0x13C WallUpgradeRadius
	static void buildFieldParse(MultiIniFieldParse &p);
};

class CastleUpgrade : public UpgradeModule
{
public:
	CastleUpgrade(Thing *thing, const CastleUpgradeModuleData *data) : UpgradeModule(thing, data), m_data(data) {}
	static void registerClass(ModuleFactory &modules);

protected:
	void upgradeImplementation() override; // RW 0x8BAAC0

private:
	void giveToWallsInRange(const UpgradeTemplate *upgrade); // RW 0x8BA9EC
	const CastleUpgradeModuleData *m_data;
};
