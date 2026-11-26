// OpenBFME. GPL-3.0.
// Ported from the RotWK 2.01 binary (caveat S-001). ZH has no such class (its HelicopterSlowDeathBehavior is the older relative).
//
// GiantBirdSlowDeathBehavior: the slow death of a flyer. It falls to the ground ahead of it, beside the buildings that CrashAvoidKindOfs names. RotWK 2.01 and BFME2 1.06
// have the class, but their own data only name it in a comment (gwaihir.ini); mods use it (Edain 4.8.5.4: the fell beasts, dragons and eagles).
//
// TARGET FACTS (RotWK game.dat; registration site RW 0x657F8E):
//   * data: createData RW 0x64B2BA (0x1C4 bytes), constructor RW 0x865076 (SlowDeathBehavior's RW 0x8613DA, vtable RW 0xC595C0): FXHitGround + 0x190 and OCLHitGround
//     + 0x194 none, DelayFromGroundToFinalDeath + 0x198 = 0.0, CrashAvoidKindOfs + 0x19C empty, CrashAvoidRadius + 0x1B8 = 800.0 (RW 0xBE94A8), CrashAvoidStrength
//     + 0x1BC = 0.1 (RW 0xBD83D4), NeedToMaintainFlailingHeight + 0x1C0 = No. Table RW 0xC59430 after SlowDeathBehavior's (parseFXList RW 0x73A302,
//     parseObjectCreationList RW 0x73A368, parseDurationReal RW 0x73A403, KindOf mask RW 0x6564E7, parseReal RW 0x42ED00, parseBool RW 0x42E558);
//   * module: create RW 0x64B282 (0x68 bytes), constructor RW 0x864F4F (RW 0x8604E2): + 0x50 = 0, + 0x60 = 0.0, + 0x64 = 0; vtables RW 0xC5958C (module), 0xC594C0 (update),
//     0xC594B0 (SlowDeathBehaviorInterface: only slot 0 is its own, the rest are SlowDeathBehavior's). No code of the class reads + 0x50, + 0x60 or + 0x64, nor the
//     data's FXHitGround, OCLHitGround, DelayFromGroundToFinalDeath, CrashAvoidStrength and NeedToMaintainFlailingHeight;
//   * beginSlowDeath RW 0x86511C (slot 0): SlowDeathBehavior's (RW 0x860E93), then the place of the crash, from the object's position:
//       - with an AI and the model condition MOVING (61; RW 0x46E918): the AI's vslot 0x188 (the GiantBirdAIUpdate itself, RW 0x8CEF91; null: the function ends, no fall);
//         the distance = its aerial speed (+ 0x5FC) * 5.0 (RW 0xBDAE58); above 50.0 (RW 0xBD88C4) the place moves by cos / sin (CRT) of the orientation times the
//         distance (x87 PC24, one store each) and the PhysicsBehavior may bounce (+ 0x5D = 1);
//       - with an AI: the free place RW 0x66A61A around it (see findClearCrashPlace) replaces it; none: it stays;
//       - z = TheTerrainLogic's ground height there (vslot 0x18); the place is kept (+ 0x54); the PhysicsBehavior flies to it with apex 0.0 at 30.0 (RW 0xBDAE54) per frame
//         (RW 0x792997);
//   * update RW 0x865009: while the PhysicsBehavior bounced in its last update (+ 0x5E) the HIT_GROUND phase is pending again (+ 0x40); a begun death runs
//     SlowDeathBehavior's update (RW 0x860B39); it runs every frame (1);
//   * xfer RW 0x86502B: version 1, SlowDeathBehavior's (RW 0x8603F9), + 0x50, + 0x64, + 0x54, + 0x60;
//   * BFME2 1.06: the same code instruction for instruction (0x86200E, 0x66B088 and the small ones); only the Object offsets (0x258 / 0x25C instead of 0x260 / 0x264) and
//     the AI's speed (0x538 instead of 0x5FC) differ.
//   * MOVING still stands when the die modules run: Object::onDie (RW 0x698F06) clears it after them (RW 0x69903E); RW 0x66264E (the AI marked dead) does not touch it.
// STOP S-5606: the fields no code of the class reads are kept in the data; a fall notes the stop.
// WHAT IS INFERENCE (OpenBFME): retail has no null test of the PhysicsBehavior, an object without one does not fall here; the snapshot is OpenBFME native (S-4190): version 2; version 1, the unported shell's block, still loads. sin and cos
// are the deterministic ones (S-081, S-167).

#pragma once

#include "GameLogic/BitFlags.h"
#include "GameLogic/Module/CombatModules.h"

#include <string>

class GiantBirdSlowDeathBehaviorModuleData : public SlowDeathBehaviorModuleData
{
public:
	std::string m_fxHitGround;                   ///< + 0x190 (no reader in the class)
	std::string m_oclHitGround;                  ///< + 0x194 (no reader in the class)
	float m_delayFromGroundToFinalDeath = 0.0f;  ///< + 0x198 (frames; no reader in the class)
	KindOfMaskType m_crashAvoidKindOfs{};        ///< + 0x19C
	float m_crashAvoidRadius = 800.0f;           ///< + 0x1B8
	float m_crashAvoidStrength = 0.1f;           ///< + 0x1BC (no reader in the class)
	bool m_needToMaintainFlailingHeight = false; ///< + 0x1C0 (no reader in the class)
	static void buildFieldParse(MultiIniFieldParse &p);
};

class GiantBirdSlowDeathBehavior : public SlowDeathBehavior
{
public:
	OBFME_MODULE_XFER(GiantBirdSlowDeathBehavior)
	GiantBirdSlowDeathBehavior(Thing *thing, const GiantBirdSlowDeathBehaviorModuleData *data); ///< RW 0x864F4F
	UpdateSleepTime update() override;                                                         ///< RW 0x865009
	void crc(StateHasher &hasher) const override;

	const GiantBirdSlowDeathBehaviorModuleData *birdData() const { return m_birdData; }
	const Coord3D &crashPlace() const { return m_crashPlace; }
	// RW 0x66A61A (AIUpdateInterface): the first place on rings around `pos`, at most `radius` away, where the object's circle touches no object that has all of `kinds`.
	// The centre itself is never tried. False: none, `out` stays
	static bool findClearCrashPlace(const Object &obj, float radius, const Coord3D &pos, const KindOfMaskType &kinds, Coord3D &out);

	static void registerModule(ModuleFactory &modules);

protected:
	void beginSlowDeath(const DieModuleInterface::Event &event) override; ///< RW 0x86511C

private:
	const GiantBirdSlowDeathBehaviorModuleData *m_birdData;
	Coord3D m_crashPlace{}; ///< + 0x54
};
