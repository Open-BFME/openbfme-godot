// OpenBFME. GPL-3.0.
//
// CivilianSpawnCollide (lane LOCAL-36, issue #286): destroys civilians/objects matching DeleteObjectFilter upon collision.
//
// TARGET FACTS (RotWK game.dat, caveat S-001):
//   * data (field table RW 0xC71748, after ModuleData base):
//     DeleteObjectFilter (+0x8, parseObjectFilter RW 0x76392F).
//     Ctor RW 0x8C17CC. Factory RW 0x65B091.
//   * onCollide (RW 0x8C1823; DONOR BFME2 0x4BD694 / CivilianSpawnCollide.cpp:162):
//     If colliding object matches DeleteObjectFilter for controlling player:
//     destroys other object via TheGameLogic->destroyObject(other) (RW 0x8C1844 -> RW 0x627376).
//   * wouldLikeToCollideWith (RW 0x63F3BF / vslot 0x34): returns false.
//   * xfer (RW 0x8C651D / CollideModule's): calls BehaviorModule::xferBehaviorModule.
// Local 171: RW 0x6510C2 (CivilianSpawnCollide newData), RW 0x65108A (CivilianSpawnCollide newInstance)

#pragma once

#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/Module/CollideModule.h"
#include "GameLogic/ObjectFilter.h"

class ModuleFactory;

class CivilianSpawnCollideModuleData : public ModuleData
{
public:
	ObjectFilter m_deleteObjectFilter; ///< +0x8 DeleteObjectFilter
	~CivilianSpawnCollideModuleData() override = default; // RW 0x008C1873 / RW 0x008C1857
	static void buildFieldParse(MultiIniFieldParse &p);
};

class CivilianSpawnCollide : public BehaviorModule, public CollideModuleInterface
{
public:
	OBFME_MODULE_XFER(CivilianSpawnCollide)
	CivilianSpawnCollide(Thing *thing, const CivilianSpawnCollideModuleData *data);
	~CivilianSpawnCollide() override = default; // RW 0x008C1736 / RW 0x008C1807

	CollideModuleInterface *getCollide() override { return this; }
	bool wouldLikeToCollideWith(const Object &other) const override { (void)other; return false; } // RW 0x63F3BF
	void onCollide(Object *other, const Coord3D *loc, const Coord3D *normal) override; // RW 0x8C1823

	void crc(StateHasher &hasher) const override;

	static void registerClass(ModuleFactory &modules); // getModuleName RW 0x008C174F, getModuleNameKey RW 0x008C1755

private:
	const CivilianSpawnCollideModuleData *m_data = nullptr;
};
