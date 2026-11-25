// OpenBFME. GPL-3.0.
//
// BaseUpgrade (lane LOCAL-36, fork issue 286; editions: both RotWK 2.01 and BFME2 1.06):
// The upgrade module behind a base structure spawn (e.g. MordorBase).
//
// TARGET FACTS (RotWK game.dat, caveat S-001):
//   * data (field table RW 0xC6DEB8, after UpgradeModuleData base):
//     BuildingTemplateName (+0x138, parseAsciiString RW 0x42EE5E),
//     PlacementPrefix (+0x13C, parseAsciiString RW 0x42EE5E),
//     PlacementIndex (+0x140, parseInt RW 0x42EC5E).
//     Ctor RW 0x8B7885 (calls UpgradeModuleData ctor RW 0x863A5E). Factory RW 0x65A28A.
//   * upgradeImplementation (RW 0x8B7933; DONOR BFME2 0x8B7800 / BaseUpgrade.cpp:332):
//     When triggered:
//     - Checks object and controlling player (RW 0x68B678);
//     - Looks up BuildingTemplateName via TheThingFactory->findTemplate (RW 0x6D1305);
//     - Checks placement: if PlacementIndex > 0 and !PlacementPrefix.empty(),
//       queries pristine bone positions via getPristineBonePositions / multiLogicalBones (RW 0x672A73);
//       if index in range, transforms bone position and Z-rotation to world coordinates;
//       else falls back to object's position and orientation;
//     - Spawns template via TheThingFactory->newObject (RW 0x6D165E);
//     - Sets producer and builder to self (RW 0x68B6A1 / RW 0x68B6B6);
//     - Sets position and orientation (RW 0x70C201 / RW 0x70C31E);
//     - Adds to pathfinder map via AddObjectToPathfindMap (RW 0x6E85E9);
//     - Notifies player via Player::onStructureCreated (RW 0x6AAF3B).
//   * xfer (RW 0x8B7880 / UpgradeModule's): version (1, 1), UpgradeModule::xfer(x).
// RW 0x64FB99 (BaseUpgrade newInstance), RW 0x64FBD1 (BaseUpgrade newData)

#pragma once

#include "GameLogic/Module/UpgradeModule.h"

#include <string>

class ModuleFactory;

// scalar deleting dtor RW 0x008B78B6; ModuleData empty-string virtual RW 0x004B612F
class BaseUpgradeModuleData : public UpgradeModuleData
{
public:
	std::string m_buildingTemplateName; ///< +0x138 BuildingTemplateName
	std::string m_placementPrefix;      ///< +0x13C PlacementPrefix
	int m_placementIndex = 0;           ///< +0x140 PlacementIndex
	static void buildFieldParse(MultiIniFieldParse &p);
};

// scalar deleting dtor RW 0x008B78D2
// RW 0x008B7796 (dtor -> RW 0x0086393B), RW 0x008B78EE (ModuleData dtor)
class BaseUpgrade : public UpgradeModule
{
public:
	OBFME_MODULE_XFER(BaseUpgrade) // lane SAVE-4b: UpgradeModule's
	BaseUpgrade(Thing *thing, const BaseUpgradeModuleData *data);
	static void registerClass(ModuleFactory &modules);

	void crc(StateHasher &hasher) const override;

protected:
	void upgradeImplementation() override; // RW 0x8B7933

private:
	const BaseUpgradeModuleData *m_data = nullptr;
};
