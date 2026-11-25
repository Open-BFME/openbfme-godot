// OpenBFME. GPL-3.0.
//
// ProjectileStreamUpdate: tracks chained projectile IDs in a circular buffer for stream rendering (lane MODGAP-2, stop S-113).
//
// Both editions:
//   RotWK 2.01:
//     - friend_newModuleInstance: RW 0x0064F9F3
//     - friend_newModuleData:     RW 0x00654AD2
//     - ProjectileStreamUpdate:   RW 0x00890AE0, vtable RW 0x00C07A30
//     - getModuleName / NameKey:  RW 0x00740844, RW 0x0074084A
//     - ~ProjectileStreamUpdate:  RW 0x0074088F (scalar deleting dtor RW 0x007409DD)
//     - update:                   RW 0x00740AE5
//     - addProjectile:            RW 0x007408A8
//     - cullFrontOfList:          RW 0x007409F9
//     - considerDying:            RW 0x00740A31
//     - getAllPoints:             RW 0x00740A56 (DONOR BFME2 ProjectileStreamUpdate::getAllPoints)
//     - xfer:                     RW 0x00740954
//   BFME2 1.06:
//     - friend_newModuleInstance: BFME2 0x0024FC59
//     - friend_newModuleData:     BFME2 0x0024FC91
//     - ProjectileStreamUpdate:   BFME2 0x0088D370 (decomp 0x0033EE25), vtable BFME2 0x00810DA8
//     - getAllPoints:             BFME2 0x0033F090
//     - xfer:                     BFME2 0x0033EF8E

#pragma once

#include "Common/INI.h"
#include "Common/StateHash.h"
#include "GameLogic/Module/UpdateModule.h"
#include "GameLogic/SnapshotXfer.h"

#include <vector>

class ModuleFactory;

class ProjectileStreamUpdateModuleData : public ModuleData
{
public:
	ProjectileStreamUpdateModuleData() = default;
	static void buildFieldParse(MultiIniFieldParse &p);
};

class ProjectileStreamUpdate : public UpdateModule
{
public:
	OBFME_MODULE_XFER(ProjectileStreamUpdate)

	static constexpr int MAX_PROJECTILE_STREAM = 20;

	ProjectileStreamUpdate(Thing *thing, const ModuleData *moduleData);
	~ProjectileStreamUpdate() override = default; // RW 0x0074088F (~ProjectileStreamUpdate), RW 0x007409DD (scalar deleting dtor)

	UpdateSleepTime update() override;

	void addProjectile(ObjectID sourceID, ObjectID newID, ObjectID victimID, const Coord3D *victimPos);
	void cullFrontOfList();
	bool considerDying();
	void getAllPoints(Coord3D *points, int *count) const;

	void crc(StateHasher &hasher) const override;

	ObjectID owningObject() const { return m_owningObject; }
	ObjectID targetObject() const { return m_targetObject; }
	const Coord3D &targetPosition() const { return m_targetPosition; }
	int nextFreeIndex() const { return m_nextFreeIndex; }
	int firstValidIndex() const { return m_firstValidIndex; }
	ObjectID projectileID(int index) const { return (index >= 0 && index < MAX_PROJECTILE_STREAM) ? m_projectileIDs[index] : 0; }

	static void registerClass(ModuleFactory &modules);

private:
	ObjectID m_projectileIDs[MAX_PROJECTILE_STREAM]{}; ///< +0x20
	int m_nextFreeIndex = 0;                          ///< +0x70
	int m_firstValidIndex = 0;                         ///< +0x74
	ObjectID m_owningObject = 0;                       ///< +0x78
	ObjectID m_targetObject = 0;                       ///< +0x7C
	Coord3D m_targetPosition{};                        ///< +0x80
};
