// OpenBFME. GPL-3.0.
//
// WallUpgradeUpdate (editions: both RotWK 2.01 and BFME2 1.06):
// Behavior on wall upgrade objects (e.g. towers, postern gates, catapults on walls).
// Possesses the wall structure, watches damage state, and on destruction resets the wall upgrades.
//
// TARGET FACTS:
// - Registry: RW 0x64EFD7 / BFME2 0x64F2EB ("WallUpgradeUpdate", interface mask 5 = BEHAVIOR | UPDATE | DAMAGE).
// - WallUpgradeUpdateModuleData:
//   createData RW 0x65428C / BFME2 0x6545A0.
// - WallUpgradeUpdate:
//   ctor RW 0x8AE969 / BFME2 0x4AB317,
//   getModuleName RW 0x008AE968, getModuleNameKey RW 0x008AE9CD,
//   dtor RW 0x008AEA8D / RW 0x008AE941 / BFME2 0x4AB36E,
//   xfer RW 0x008AE96E,
//   isWallUpgradeKind RW 0x8AEAA9 / BFME2 0x4AB3AE (testModelCondition RW 0x46E918 for 0x109 UPGRADE_TREBUCHET, 0x108 UPGRADE_POSTERN_GATE, 0x107 UPGRADE_GARRISON),
//   onBodyDamageStateChange RW 0x8AEAE6 / BFME2 0x4AB3EB (BODY_RUBBLE arms 40 frames countdown),
//   scanForBuildingAndPossess RW 0x8AEB10 / BFME2 0x4AB415 (STRUCTURE + WALK_ON_TOP_OF_WALL within radius 200),
//   canResetUpgrades RW 0x008AEBA6 / DONOR BFME2 0x4AB4AB (attached object not effectively dead and not model condition 0x45 ACTIVELY_BEING_CONSTRUCTED),
//   update RW 0x8AEBD3 / BFME2 0x4AB4D8:
//   * first frame runs scanForBuildingAndPossess
//   * on rubble countdown completion resets upgrades (RW 0x68E083 / BFME2 0x28DA28),
//     resets body damage state (RW 0x8C1BF9 / BFME2 slot 9), clears RUBBLE condition, clears statuses.

#pragma once

#include "Common/Module.h"
#include "GameLogic/Damage.h"
#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/Module/DamageModule.h"
#include "GameLogic/Module/UpdateModule.h"

#include <cstdint>

class ModuleFactory;
class MultiIniFieldParse;
class Object;

class WallUpgradeUpdateModuleData : public ModuleData
{
public:
	WallUpgradeUpdateModuleData() = default;
	static void buildFieldParse(MultiIniFieldParse &p);
};

class WallUpgradeUpdate : public UpdateModule, public DamageModuleInterface
{
public:
	OBFME_MODULE_XFER(WallUpgradeUpdate)

	WallUpgradeUpdate(Thing *thing, const WallUpgradeUpdateModuleData *data);

	UpdateSleepTime update() override;

	DamageModuleInterface *getDamage() override { return this; }
	void onDamage(const DamageInfo &) override {}
	void onHealing(const DamageInfo &) override {}
	void onBodyDamageStateChange(const DamageInfo *info, BodyDamageType oldState, BodyDamageType newState) override;

	bool isWallUpgradeKind() const;

	ObjectID attachedObjectID() const { return m_attachedObjectID; }
	int countdown() const { return m_countdown; }

	static void registerClass(ModuleFactory &modules);

private:
	void scanForBuildingAndPossess();
	bool canResetUpgrades() const;

	const WallUpgradeUpdateModuleData *m_data = nullptr;
	ObjectID m_attachedObjectID = 0; ///< +0x28
	int m_countdown = 0;             ///< +0x2C
	bool m_firstScan = true;         ///< +0x30
	bool m_postCountdown = false;    ///< +0x31
};
