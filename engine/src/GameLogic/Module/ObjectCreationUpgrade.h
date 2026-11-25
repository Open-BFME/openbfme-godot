// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// ObjectCreationUpgrade (lane FACT-C): an upgrade that, after a delay, makes an object at an offset from its owner and grants / removes an upgrade on
// the owner (the Angmar spike moat and the Sanctum's readiness, the expansions' wall catapults, the Mordor lava moat). ZH's class runs an OCL at once; RotWK's
// is an UpgradeMux and an UpdateModule. Open-BFME-2 matches only the constructor (ObjectCreationUpgradeCtor.cpp, tier A for RW 0x8B8221) and the removal's
// slave kill (ObjectCreationUpgrade.cpp, rva004B463C); the rest is read from the RotWK binary.
//
// TARGET FACTS:
//   * constructor RW 0x8B8221: the mux (vtable RW 0xC6E250) at + 0, the update module (RW 0xC6E21C) at + 8, the behaviour / update interfaces at + 0x14 / + 0x18;
//     + 0x28 the frame the work waits for, + 0x2C the pending flag, both 0.
//   * data (field table RW 0xC6E2F8 at + 0, then the mux table RW 0x8D26E0 at + 0x10, RW 0x8B81FF): UpgradeObject (+ 0x8, an OCL name RW 0x73A368), Delay
//     (+ 0xC, RW 0x73A429 ms -> frames), RemoveUpgrade / GrantUpgrade / ThingToSpawn (+ 0x140 / 0x144 / 0x148, names RW 0x42EE5E), Offset (+ 0x14C, a Coord3D
//     RW 0x42F247), Angle (+ 0x158, RW 0x42EE15 degrees -> radians), DestroyWhenSold (+ 0x15C, bool), DeathAnimAndDuration (+ 0x160, RW 0x851412),
//     FadeInTime (+ 0x16C, an int of ms RW 0x42ECB2), UseBuildingProduction (+ 0x170, bool).
//   * upgradeImplementation (mux slot 10, RW 0x8B8281): unless pending, the wait frame = now + Delay, pending, the update woken (RW 0x850C32).
//   * update (update interface slot 0, RW 0x8B8464): not pending or now <= the wait frame: UPDATE_SLEEP_NONE. Otherwise, with a ThingToSpawn: its
//     template (RW 0x6D1305); UseBuildingProduction: the owner's ProductionUpdate queues it (vslots 8 / 0x20; a refusal retries next frame); else the
//     object made by TheThingFactory (vslot 0x38) for the owner's player at the owner's transform applied to Offset (RW 0x8B8515 .. 0x8B85C6: x = ((m01 oy
//     + m02 oz) + m00 ox) + tx, the rows likewise), turned to the owner's angle + Angle, FadeInTime on its drawable, put at the point (RW 0x70C201), told
//     to the owner's SlaveWatcherBehavior (RW 0x887AF5, lane MODGAP-1) and its create modules run (+ 0x24C list, slot 0x68 -> slot 4 with the owner). A failed creation
//     ends there (no grant). The pending flag stays set (only processUpgradeRemoval clears it). Then GrantUpgrade given to the owner (RW 0x69388B) and RemoveUpgrade taken (RW 0x691438); UPDATE_SLEEP_FOREVER.
//   * processUpgradeRemoval (mux slot 8, RW 0x8B87C5): not executed, not pending; with DestroyWhenSold the owner's SlaveWatcherBehavior's slave is
//     killed (damage type 8) and DeathAnimAndDuration's condition set on it.
//   * wouldUpgrade (mux slot 2, RW 0x8B82AB, the class's own): the mux base's RW 0x8D26F3 asked two more things — whether the upgrade
//     already ran (0x8D273F) and slot 14 for the conflicting mask (0x8D2749) — and this class asks neither: the activation mask must be non-empty, the
//     conflicting mask must not hold any of the mask's bits (RW 0x8097D6, always "any", although the class answers slot 14 itself at RW 0x8B81E6), and
//     then the activation mask is held all (slot 13, RW 0x6AACB3) or any (RW 0x8097D6) by that mask. Answering while the upgrade has run is what
//     postUpgradeCheck needs (below), so the two slots go together.
//   * postUpgradeCheck (mux slot 6, RW 0x8B8329): every upgrade event (Object::updateUpgradeModules RW 0x6936FE calls slot 6 on every mux,
//     0x6937AB) the mask is the controlling player's completed upgrades (RW 0x68B678 -> + 0x14C) OR the object's (+ 0x28C, RW 0x68CC6C) — the castle
//     keep's mask of 0x69378C is not added. While wouldUpgrade answers yes (0x8B836D) and nothing is pending (+ 0x2C, 0x8B837A) it gives itself the upgrade
//     (RW 0x855388); otherwise the removal (slot 8, RW 0x8B87C5) runs at once, pending or not (0x8B8387). So a module whose trigger leaves loses its
//     upgrade, and the object an earlier run made dies with DestroyWhenSold; a module whose trigger stays keeps both.
//   * isPermanent (mux slot 7, RW 0x9188EB) answers false: the data's Permanent (+ 0x13E, the mux base's + 0x136) is parsed and never read, so the
//     removal of RW 0x8D2688 always runs for this class.
// NOT PORTED (counted in unported(), stop S-2007): UpgradeObject (read by no code of the update), FadeInTime (a drawable fade: client). The
// SlaveWatcherBehavior link and DestroyWhenSold's kill run since lane MODCOMPAT-MOD1 (SlaveWatcherBehavior.h).
// The rest of the class's own functions are the mux base's under the data's + 8 layout shift (the masks at + 0x10 / + 0xA0, RequiresAllTriggers
// + 0x13C, RequiresAllConflictingTriggers + 0x13D): slot 11 RW 0x8B81B5 (getUpgradeActivationMasks), slots 13 / 14 RW 0x8B81DC / 0x8B81E6, slots
// 12 / 15 RW 0x8B81F0 (the no-op RW 0x9F3A3C). Nothing to add: the port's UpgradeMux reads those fields itself. The module object's own vtable slots
// (RW 0xC6E21C) are the name RW 0x8B812F / the name key RW 0x8B8170, two destructors (RW 0x8B8168 / 0x8B8146) and two adjustor thunks (RW 0x8B8135 /
// 0x8B8142): not behaviour.

#pragma once

#include "Common/INIDataTypes.h"
#include "GameLogic/Module/UpdateModule.h"
#include "GameLogic/Module/UpgradeModule.h"

#include <string>
#include <vector>

class ModuleFactory;
class MultiIniFieldParse;
class StateHasher;

class ObjectCreationUpgradeModuleData : public UpgradeModuleData
{
public:
	std::string m_upgradeObject;  ///< + 0x8 UpgradeObject (an OCL name)
	unsigned m_delay = 0;         ///< + 0xC Delay (frames)
	std::string m_removeUpgrade;  ///< + 0x140 RemoveUpgrade
	std::string m_grantUpgrade;   ///< + 0x144 GrantUpgrade
	std::string m_thingToSpawn;   ///< + 0x148 ThingToSpawn
	Coord3D m_offset{};           ///< + 0x14C Offset
	float m_angle = 0.0f;         ///< + 0x158 Angle (radians)
	bool m_destroyWhenSold = false; ///< + 0x15C DestroyWhenSold
	int m_deathAnimCondition = -1;  ///< + 0x160 DeathAnimAndDuration AnimState
	unsigned m_deathAnimFrames = 0; ///< + 0x164 AnimTime
	unsigned m_deathTriggerFrames = 0; ///< + 0x168 TriggerTime
	unsigned m_fadeInTime = 0;      ///< + 0x16C FadeInTime (ms)
	bool m_useBuildingProduction = false; ///< + 0x170 UseBuildingProduction
	static void buildFieldParse(MultiIniFieldParse &p);
};

class ObjectCreationUpgrade : public UpdateModule, public UpgradeMux
{
public:
	OBFME_MODULE_XFER(ObjectCreationUpgrade) // lane SAVE-4b
	ObjectCreationUpgrade(Thing *thing, const ObjectCreationUpgradeModuleData *data); ///< RW 0x8B8221
	UpgradeMux *getUpgrade() override { return this; }
	// RW 0x8B82AB: its own would-upgrade (no "already ran" test, the conflicting mask always "any")
	bool wouldUpgrade(const UpgradeMaskType &mask) const override;
	// RW 0x8B8329: on every upgrade event, while the trigger is there and nothing is pending, give itself the upgrade; else the removal
	void postUpgradeCheck() override;
	// RW 0x9188EB: slot 7 answers false (the data's Permanent is parsed and never read)
	bool isPermanent() const override { return false; }
	UpdateSleepTime update() override; ///< RW 0x8B8464
	void crc(StateHasher &h) const override;
	ObjectID spawned() const { return m_spawned; }
	unsigned queued() const { return m_queued; }
	unsigned long long unported() const { return m_unported; }
	static void registerClass(ModuleFactory &modules);
	static std::vector<std::string> acceptanceStops(); // [S-2007]

protected:
	void upgradeImplementation() override; ///< RW 0x8B8281
	void processUpgradeRemoval() override; ///< RW 0x8B87C5

private:
	const ObjectCreationUpgradeModuleData *m_data;
	unsigned m_waitFrame = 0; ///< + 0x28
	bool m_pending = false;   ///< + 0x2C
	ObjectID m_spawned = INVALID_ID;   ///< the object made (a test read; hashed)
	unsigned m_queued = 0;             ///< UseBuildingProduction: units queued at the owner (a test read; hashed)
	unsigned long long m_unported = 0; ///< uses of the parts not ported (hashed)
};
