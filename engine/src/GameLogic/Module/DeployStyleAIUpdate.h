// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// The Dwarven Demolisher's deploy (lane SIEGE-1, stop S-1984): DeployStyleAIUpdate (the AI that unpacks into DEPLOYED, holds the DeployedAttributeModifier
// and packs again before it moves) and ToggleDeploySpecialAbilityUpdate (the deploy button: deploys an undeployed unit, undeploys a deployed one). The only
// retail users are DwarvenDemolisher's two modules (data/ini/object/goodfaction/units/dwarven/dwarvenram.ini).
//
// TARGET FACTS (RotWK game.dat, caveat S-001; the functions were read in the disassembly, RotWK offsets):
//   DeployStyleAIUpdate (create RW 0x64CEBF size 0x59C, constructor RW 0x892013 over AIUpdateInterface, vtable RW 0xC63230; the update interface at + 0x10;
//   the AICommandInterface at + 0x20; data table RW 0xC05010 after the AI's RW 0x6636C8: UnpackTime + 0x64 and PackTime + 0x68 (parseDurationUnsignedInt
//   RW 0x73A429, frames), ResetTurretBeforePacking + 0x6C (parsed, not read by the functions below), TurretsFunctionOnlyWhenDeployed + 0x6D,
//   TurretsMustCenterBeforePacking + 0x6E, MustDeployToAttack + 0x6F (parseBool RW 0x42E558), DeployedAttributeModifier + 0x70 (RW 0x42EE5E)). Fields: + 0x4A8
//   the kept command (AICommandParmsStorage, type -1: none), + 0x56C "the last command ran", + 0x56D "the kept command waits", + 0x570 the classification at the
//   last AI-source command, + 0x574 the classification of the update (0 still, 1 moving or waiting for a path, 2 a target in range, 3 a target out of range),
//   + 0x578 the state (0 ready to move, 1 unpacking, 2 deployed, 3 packing, 4 aligning the turret), + 0x57C the frame the (un)packing ends, + 0x580 the
//   victim, + 0x584 the attack object, + 0x588 the attack position, + 0x594 an attack command, + 0x595 attack an object, + 0x596 attack a position, + 0x597 an
//   attack-move (command 0x1E), + 0x598 "the update attacked", + 0x599 / + 0x59A the deploy / undeploy requests:
//     * aiDoCommand (vslot 0x268, RW 0x89209F): the gate (vslot 0x250 = RW 0x667174); any command but idle (5) and 0x35 first idles the unit (RW 0x5E821A,
//       CMD_FROM_AI); a CMD_FROM_AI command keeps the classification (+ 0x570 = + 0x574) and runs (the base RW 0x66A7EC). Any other: the requests and targets
//       reset (RW 0x891CEB), the command kept (not idle; a call that is not a replay also clears + 0x56C), then outside the (un)packing it runs and + 0x56C is
//       set, inside it waits (+ 0x56D); command 0x1F, 0xF .. 0x12, 0x21, 0x23 set + 0x594; 0xB, 0xC, 0x39 set + 0x595 and the object; 0xE sets + 0x596 and the
//       position; 0x1E the position, + 0x597 and + 0x594;
//     * isIdle (vslot 0x1B8, RW 0x891CD3): not while a kept command waits (RW 0x891CAF), else the base's (RW 0x6643FC);
//     * the replay RW 0x891F9B: the kept command, + 0x56D cleared, through aiDoCommand with the replay flag;
//     * requestDeploy RW 0x891D3B (+ 0x59A off; + 0x599 unless unpacking or deployed), requestUndeploy RW 0x891D62 (+ 0x599 off; + 0x59A unless ready to move or
//       packing);
//     * update (RW 0x89251C .. 0x8929D3): see DeployStyleAIUpdate::update;
//     * setMyState RW 0x8921E3: see DeployStyleAIUpdate::setMyState.
//   ToggleDeploySpecialAbilityUpdate (constructor RW 0x8B1B87 over SpecialAbilityUpdate RW 0x851D42; data RW 0x8B1C23: SoundDeploy + 0xD0, SoundUndeploy + 0xD4,
//   table RW 0xC05AC8): allowsSpecialPowerUse (RW 0x8B1C5F): false while PACKING (94) or UNPACKING (96), else the base's (RW 0x851B6E); triggerAbilityEffect
//   (RW 0x8B1D10): the base's (RW 0x853EDF), then, with data and the first trigger (+ 0x24 == 1), DEPLOYED (100): undeploy (vslot 0x5C, RW 0x8B1C94: SoundUndeploy
//   for the object, requestUndeploy of the object's AI (+ 0x260) unchecked); else deploy (vslot 0x60, RW 0x8B1DB3): when allowsSpecialPowerUse and the object has an
//   AI, a unit that moves (RW 0x68BE97: its locomotor speed > 0.1, RW 0xBD83D4) or whose state is active (AI vslot 0x1BC) idles (CMD_FROM_AI), SoundDeploy,
//   requestDeploy.
// DONOR FACTS (Zero Hour DeployStyleAIUpdate.cpp): the state names (READY_TO_MOVE, DEPLOY, READY_TO_ATTACK, UNDEPLOY, ALIGNING_TURRETS); the shape of the update.
// RotWK's update differs (the classification, the attack-move deploy, the requests): the disassembly is ported, not the donor.
// INFERENCE (each marked where it is made): the port routes only some AI commands to a subclass (AIUpdateInterface::routeCommand: 0, 0x36, the move with an
// angle, 0xB, 0xC, 0xE, 0xF); the others reach the base without this class's bookkeeping (S-4945).

#pragma once

#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/SpecialAbilityModules.h"

#include <string>
#include <vector>

class ModuleFactory;

class DeployStyleAIUpdate : public AIUpdateInterface
{
public:
	OBFME_MODULE_XFER(DeployStyleAIUpdate) // the save (SAVE-4e): its version and AIUpdateInterface's (its state: xferAIStateBody)
	void xferAIStateBody(class Xfer &x) override; // the deploy state, the kept command, the attack flags (obfme:AIWorld)
	enum DeployState
	{
		READY_TO_MOVE = 0,
		DEPLOY = 1,
		READY_TO_ATTACK = 2,
		UNDEPLOY = 3,
		ALIGNING_TURRETS = 4
	};
	DeployStyleAIUpdate(Thing *thing, const ModuleData *data); // RW 0x892013
	UpdateSleepTime update() override;                         // RW 0x89251C
	bool isIdle() const override;                              // RW 0x891CD3
	void crc(StateHasher &hasher) const override;

	void requestDeploy();   // RW 0x891D3B
	void requestUndeploy(); // RW 0x891D62
	int state() const { return m_state; }
	unsigned stateFrame() const { return m_frame; }
	bool hasPendingCommand() const; // RW 0x891CAF
	unsigned replays() const { return m_replays; }

	static DeployStyleAIUpdate *of(const Object &obj);
	static void registerClass(ModuleFactory &modules);
	static const char *stopLine(); // [S-4945]

protected:
	bool routeCommand(const RoutedCommand &command) override; // RW 0x89209F for the routed commands

private:
	void setMyState(int state);     // RW 0x8921E3
	void replayPending();           // RW 0x891F9B
	void resetRequests();           // RW 0x891CEB
	void idleFromAI();              // RW 0x5E821A(2) through aiDoCommand's CMD_FROM_AI branch
	void dispatch(const RoutedCommand &c);

	// the data (raw module lines, RW table 0xC05010)
	unsigned m_unpackFrames = 0;            // + 0x64
	unsigned m_packFrames = 0;              // + 0x68
	bool m_resetTurretBeforePacking = false;// + 0x6C
	bool m_turretsOnlyWhenDeployed = false; // + 0x6D
	bool m_turretsMustCenter = false;       // + 0x6E
	bool m_mustDeployToAttack = true;       // + 0x6F
	std::string m_deployedModifier;         // + 0x70

	RoutedCommand m_kept;                   // + 0x4A8 (type -1: none)
	ObjectID m_keptTarget = INVALID_ID;
	bool m_ran = false;                     // + 0x56C
	bool m_pending = false;                 // + 0x56D
	int m_classAtCommand = 0;               // + 0x570
	int m_class = 0;                        // + 0x574
	int m_state = READY_TO_MOVE;            // + 0x578
	unsigned m_frame = 0;                   // + 0x57C
	ObjectID m_victim = INVALID_ID;         // + 0x580
	ObjectID m_attackObject = INVALID_ID;   // + 0x584
	Coord3D m_attackPos{ 0.0f, 0.0f, 0.0f };// + 0x588
	bool m_attackCommand = false;           // + 0x594
	bool m_attackObjectCommand = false;     // + 0x595
	bool m_attackPositionCommand = false;   // + 0x596
	bool m_attackMoveCommand = false;       // + 0x597
	bool m_updateAttacked = false;          // + 0x598
	bool m_deployRequest = false;           // + 0x599
	bool m_undeployRequest = false;         // + 0x59A
	bool m_replaying = false;               // aiDoCommand's second argument
	unsigned m_replays = 0;                 // observation (hashed)
};

class ToggleDeploySpecialAbilityUpdateModuleData : public SpecialAbilityUpdateModuleData
{
public:
	std::string m_soundDeploy;   // + 0xD0
	std::string m_soundUndeploy; // + 0xD4
	static void buildFieldParse(MultiIniFieldParse &p);
};

class ToggleDeploySpecialAbilityUpdate : public SpecialAbilityUpdate
{
public:
	OBFME_MODULE_XFER(ToggleDeploySpecialAbilityUpdate) // the save (SAVE-4b): SpecialAbilityUpdate's and the counts
	ToggleDeploySpecialAbilityUpdate(Thing *thing, const ToggleDeploySpecialAbilityUpdateModuleData *data); // RW 0x8B1B87
	bool allowsSpecialPowerUse() const override; // RW 0x8B1C5F
	unsigned deploys() const { return m_deploys; }
	unsigned undeploys() const { return m_undeploys; }

protected:
	void triggerAbilityEffect() override; // RW 0x8B1D10

private:
	void undeploy(); // RW 0x8B1C94
	void deploy();   // RW 0x8B1DB3
	const ToggleDeploySpecialAbilityUpdateModuleData *m_td;
	unsigned m_deploys = 0;
	unsigned m_undeploys = 0;
};

namespace DeployStyleModules
{
void registerAll(ModuleFactory &modules);
}
