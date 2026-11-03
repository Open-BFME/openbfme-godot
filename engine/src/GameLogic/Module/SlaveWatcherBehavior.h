// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0 (the UpdateModule shape; ZH has no SlaveWatcherBehavior:
// every field and step below is RotWK's). BFME2 decomp cross-check: Code/GameEngine/Source/GameLogic/Object/Behavior/SlaveWatcherBehavior*.cpp.
//
// SlaveWatcherBehavior (lane MODCOMPAT-MOD1, census rank 6; stop S-1995 before): a structure (the citadels, the castle expansions) watches the object its
// ObjectCreationUpgrade (or its production, a queued entry's + 0x44 flag) made: while it lives the structure may share its upgrades with it; when it dies the
// structure gets GrantUpgrade back and loses RemoveUpgrade (the "buy a new wall catapult / giant / eagle" button returns); when the structure is deleted
// the slave dies with it unless LetSlaveLive. The SlaveAttackNugget of the castle weapons orders the slave to attack.
//
// TARGET FACTS (RotWK game.dat, caveat S-001; registry golden engine/data/rotwk-201/module-registry.json):
//   * registration RW 0x6587AF; create RW 0x64C30E (0xB4 bytes), constructor RW 0x887857: UpdateModule (RW 0x653114), vtables module RW 0xC60A54, + 0x0C RW 0xC67300, update + 0x10
//     RW 0xC60A48; + 0x24 the upgrade mask copy (0x24 words, RW 0x5EA666 clears it), + 0x20 the slave id 0; the update wakes next frame (RW 0x850C32(obj, 1)).
//   * data createData RW 0x64C349, constructor RW 0x887953 (0x14 bytes, vtable RW 0xC60A88): GrantUpgrade + 0x08 and RemoveUpgrade + 0x0C empty, ShareUpgrades + 0x10 and LetSlaveLive + 0x11 FALSE;
//     table RW 0xC609F8: RemoveUpgrade + 0x0C, GrantUpgrade + 0x08 (parseAsciiString RW 0x42EE5E), ShareUpgrades + 0x10, LetSlaveLive + 0x11 (parseBool
//     RW 0x42E558).
//   * setSlave RW 0x887AF5 (id): + 0x20 = id; with ShareUpgrades and the slave found: the structure's upgrade mask (+ 0x28C) is copied to + 0x24, OR-ed into
//     the slave's (RW 0x68CC6C) and the slave's upgrade modules run (RW 0x6936FE); then the update wakes next frame (RW 0x850C32(obj, 1)).
//     Callers: ObjectCreationUpgrade's update (RW 0x8B86D6, the owner's module by name key "SlaveWatcherBehavior", RW 0x68BDA5: the first one) and
//     ProductionUpdate's (RW 0x8A270E, an entry whose + 0x44 flag is set: ObjectCreationUpgrade's UseBuildingProduction queue).
//   * getSlaveID RW 0xA0605D: + 0x20.
//   * update RW 0x887A35 (the update interface): no slave: 1 (every frame). The slave by id, gone or effectively dead (+ 0x458 bit 0): RemoveUpgrade and
//     GrantUpgrade by name (TheUpgradeCenter RW 0x66F5E5; an unknown name: nothing), the structure loses RemoveUpgrade (RW 0x691438) and gets GrantUpgrade
//     (RW 0x69388B), its upgrade modules run (RW 0x6936FE), + 0x20 = 0, sleep forever (0x3FFFFFFF). Alive: with ShareUpgrades and the structure's mask
//     different from + 0x24 (RW 0x862520), the copy, the OR into the slave and its upgrade modules again; 1.
//   * destructor RW 0x8879C5: unless the data's LetSlaveLive, a live slave is killed (RW 0x698EC3(DAMAGE_FORCE 0, death 13)).
//   * xfer RW 0x8878FF: UpdateModule (RW 0x850C60), the slave id, the mask (RW 0x707CA0, or RW 0x691BD7 for a light CRC).
//   Readers of the slave: ObjectCreationUpgrade's removal (RW 0x8B87C5: DestroyWhenSold kills it, RW 0x698EC3(8, 0), and DeathAnimAndDuration's special
//   condition RW 0x68B581), SlaveAttackNugget (isApplicable RW 0x910DCF, slots 5 / 6 RW 0x910EA1 / 0x910F78: WeaponDelivery.cpp).
// WHAT IS INFERENCE (OpenBFME):
//   * the destructor's kill runs from onDelete (the structure is deleted: GameLogic::processDestroyList), as RebuildHoleBehavior's;
//   * the snapshot is OpenBFME native (S-4190): RW 0x8878FF's fields.

#pragma once

#include "Common/INI.h"
#include "Common/Upgrade.h"
#include "GameLogic/Module/UpdateModule.h"

#include <string>

class ModuleFactory;
class Object;

class SlaveWatcherBehaviorModuleData : public ModuleData
{
public:
	std::string m_grantUpgrade;  ///< + 0x08
	std::string m_removeUpgrade; ///< + 0x0C
	bool m_shareUpgrades = false; ///< + 0x10
	bool m_letSlaveLive = false;  ///< + 0x11
	static void buildFieldParse(MultiIniFieldParse &p);
};

class SlaveWatcherBehavior : public UpdateModule
{
public:
	OBFME_MODULE_XFER(SlaveWatcherBehavior) // RW 0x8878FF
	SlaveWatcherBehavior(Thing *thing, const SlaveWatcherBehaviorModuleData *data); // RW 0x887857
	UpdateSleepTime update() override;                                              // RW 0x887A35
	void onDelete() override;                                                       // RW 0x8879C5 (the destructor)
	void crc(StateHasher &hasher) const override;
	const SlaveWatcherBehaviorModuleData *data() const { return m_data; }

	void setSlave(ObjectID id);                        // RW 0x887AF5
	ObjectID getSlaveID() const { return m_slaveID; }  // RW 0xA0605D
	// tests
	const UpgradeMaskType &sharedMask() const { return m_upgradeMask; }
	unsigned releases() const { return m_releases; }

	// RW 0x68BDA5 with the name key "SlaveWatcherBehavior": the object's first such module, or null
	static SlaveWatcherBehavior *of(const Object &obj);
	static void registerModule(ModuleFactory &modules);

private:
	void shareUpgrades(Object &slave); // RW 0x887A6C .. 0x887A9B / RW 0x887B1B .. 0x887B3E
	const SlaveWatcherBehaviorModuleData *m_data;
	ObjectID m_slaveID = INVALID_ID; ///< + 0x20
	UpgradeMaskType m_upgradeMask;   ///< + 0x24
	unsigned m_releases = 0;         ///< OpenBFME counter: the slave deaths handled (tests and the hash)
};
