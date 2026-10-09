// OpenBFME. GPL-3.0.
//
// Lane CAMP-1: AttachUpdate, the object that a passing unit picks up and carries (MAP ANG Dark Eye's palantir shards PalantirShard01 .. 07; the
// crate / One Ring objects of data\ini\crate.ini). RotWK only; ported from the binary (caveat S-001).
//
// TARGET FACTS (rotwk201_game.exe):
//   * data, field table RW 0xC643B8: ObjectFilter (+8, RW 0x76392F), ParentStatus (+0xC, an object status mask, RW 0x7B1E5C), ScanRange (+0x1C, real),
//     AlwaysTeleport (+0x20), AnchorToTopOfGeometry (+0x21), ParentOwnerAttachmentEvaEvent / ParentAllyAttachmentEvaEvent / ParentEnemyAttachmentEvaEvent
//     (+0x24 / + 0x28 / + 0x2C, Eva events RW 0x5DE588), AttachFX (+0x30, an FXList RW 0x73A302), ParentOwnerDiedEvaEvent / ParentAllyDiedEvaEvent /
//     ParentEnemyDiedEvaEvent (+0x34 / + 0x38 / + 0x3C);
//   * constructor RW 0x8950A0: the parent id (+0x20) 0, the died Eva event (+0x24) -1, wake next frame (RW 0x850C32(1));
//   * update RW 0x8953BB: first the attach scan RW 0x89515C, then, with a parent id: the parent gone or effectively dead (+0x458 bit 0): the id is cleared,
//     the chosen died Eva event is played at the object (TheEva RW 0x5DD9EE) and the object kills itself (RW 0x698EC3: unresistable damage of its max
//     health - a HighlanderBody shard dies, its CreateObjectDie drops a new shard); sleep forever. Else the anchor (RW 0x895310: the parent's position;
//     a HORDE parent with a contain asks the contain slot 0x230 for its point and keeps the position when it answers false; AnchorToTopOfGeometry adds the
//     parent's geometry height, RW 0xAD1920) differs from the object's position: AlwaysTeleport -> the teleport form of setPosition (RW 0x696E63), else
//     setPosition (RW 0x70C201). Every frame (1);
//   * the scan RW 0x89515C (only without a parent): ThePartitionManager's closest object (RW 0xA39090) within ScanRange of the object's position, distance
//     type 1 (3D centres), filters RW 0xBE4CC8 (the ObjectFilter for the object's controlling player, match), RW 0xC10E20 (not effectively dead) and
//     RW 0xC1D660 (not the object itself). A hit that is a horde member (RW 0x6939DF) whose horde (+0x27C) is a HORDE: the horde is the parent. A parent
//     of KindOf CREEP (+0x11D & 0x40) needs its controlling player's RW 0x6AAC66 (the PlayerTemplate, + 0x34, is a PlayableSide, + 0x151). Then: parent id = the parent's id; a non-empty
//     ParentStatus is set on the parent (RW 0x68D440); the object gets status ATTACHED (0x62, RW 0x62684D); AttachFX at the object for the parent
//     (RW 0x4B1B5A); with a local player and a parent player: the owner / ally (relationship 2) / enemy attachment Eva event and the matching died event kept
//     for later (+0x24).
// lane CAMP-1H: the Eva events reach TheEva through AudioApi::reportEva (the attachment event at the parent, the died event at the object).
// lane CAMP-1H: a HORDE parent's anchor is its horde interface slot 0x230 (HordeContain RW 0x8713F6: the members' mean position).
// NOT PORTED (stop S-1361): the Eva event names are not checked at parse time (as S-190); slot 0x230's second id list (HordeContain + 0x54, not kept
// by the port's horde); the horde-member test's status 0x26 branch of RW 0x6939DF (a contained object's container is taken).
#pragma once

#include "Common/INI.h"
#include "GameLogic/BitFlags.h"
#include "GameLogic/Module/UpdateModule.h"
#include "GameLogic/ObjectFilter.h"

#include <string>
#include <vector>

class ModuleFactory;
class StateHasher;

class AttachUpdateModuleData : public ModuleData
{
public:
	ObjectFilter m_objectFilter;            ///< + 8
	ObjectStatusMaskType m_parentStatus{};  ///< + 0xC
	float m_scanRange = 0.0f;               ///< + 0x1C
	bool m_alwaysTeleport = false;          ///< + 0x20
	bool m_anchorToTopOfGeometry = false;   ///< + 0x21
	std::string m_ownerAttachEva, m_allyAttachEva, m_enemyAttachEva; ///< + 0x24 / + 0x28 / + 0x2C
	std::string m_attachFX;                 ///< + 0x30
	std::string m_ownerDiedEva, m_allyDiedEva, m_enemyDiedEva;       ///< + 0x34 / + 0x38 / + 0x3C
	static void buildFieldParse(MultiIniFieldParse &p);
};

class AttachUpdate : public UpdateModule
{
public:
	AttachUpdate(Thing *thing, const AttachUpdateModuleData *data); ///< RW 0x8950A0
	UpdateSleepTime update() override;                              ///< RW 0x8953BB
	void crc(StateHasher &h) const override;
	ObjectID parentID() const { return m_parentID; }
	const std::string &diedEva() const { return m_diedEva; }

	static void registerClass(ModuleFactory &modules);
	static std::vector<std::string> stopLines();

private:
	void tryAttach(); ///< RW 0x89515C
	Coord3D anchorOf(const Object &parent) const; ///< RW 0x895310

	const AttachUpdateModuleData *m_data;
	ObjectID m_parentID = 0;  ///< + 0x20
	std::string m_diedEva;    ///< + 0x24 (empty: -1)
};
