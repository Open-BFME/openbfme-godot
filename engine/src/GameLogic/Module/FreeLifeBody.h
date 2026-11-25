// OpenBFME. GPL-3.0.
//
// FreeLifeBody (lane MODGAP-1; editions: both RotWK 2.01 and BFME2 1.06):
// A lethal hit restores the object to FreeLifeHealthPercent instead of killing it.
// Ported from the RotWK binary (caveat S-001); BFME2 decompile Object/Body/FreeLifeBodyApply.cpp
// and FreeLifeBodyAttemptDamage.cpp name the pieces and agree.
//
// TARGET FACTS (RotWK game.dat):
//   create RW 0x651414, 0x114 bytes, constructor RW 0x8C5A74 over RespawnBody RW 0x8C547A: + 0x108 used = 0, + 0x10C last frame = -1, + 0x110 interval = data + 0x70,
//   + 0x100 invincible = data + 0x74, + 0x104 health fraction = data + 0x6C. Tables RW 0xC71D68 (ActiveBody), 0xC723FC (RespawnBody), 0xC727F8: FreeLifeHealthPercent
//   + 0x6C (parsePercentToReal), FreeLifeTime + 0x70 (parseDurationUnsignedInt), FreeLifeInvincible + 0x74, FreeLifePrerequisiteUpgrade + 0x84 (RW 0x73AF89),
//   FreeLifeAnimAndDuration RW 0x851412 (condition + 0x78, -1 when unset; frames + 0x7C).
//   * attemptDamage RW 0x8C5AE6 (body interface slot 0): with FreeLifeInvincible and the object in the animation's model condition the hit is ignored, else ActiveBody's;
//   * internalChangeHealth RW 0x8C5B86 (slot 0x84): the delta is lethal (-health >= delta, RW 0x8C5BA7 .. 0x8C5BB2), the fraction is above 0, the prerequisite (if any) is
//     completed for the player (RW 0x6AC2AF) or held by the object (RW 0x691421), and a former use is over FreeLifeTime frames old (frame - last > time, RW 0x8C5C07 ..
//     0x8C5C5C): the use and the frame are kept, the condition set for its frames (RW 0x68B581), the health restored by slot 0x58 (fraction * 100, 0) and the source of the
//     hit scores the kill with type 3 (RW 0x8C5CC6 -> 0x6955BC). Else RespawnBody's (RW 0x8C553F).
// The snapshot is OpenBFME native (S-4190).
// RW 0x65144F (FreeLifeBody newData)

#pragma once

#include "GameLogic/Module/StructureModules.h"

#include <string>

class ModuleFactory;

// RW 0x008C5CE5 (FreeLifeBodyModuleData::FreeLifeBodyModuleData)
class FreeLifeBodyModuleData : public RespawnBodyModuleData
{
public:
	float m_freeLifeHealthPercent = 0.0f;  ///< + 0x6C (a fraction: parsePercentToReal)
	unsigned m_freeLifeTime = 0;           ///< + 0x70 (frames)
	bool m_freeLifeInvincible = false;     ///< + 0x74
	int m_animCondition = -1;              ///< + 0x78 (-1: none)
	unsigned m_animFrames = 0;             ///< + 0x7C
	std::string m_prerequisiteUpgrade;     ///< + 0x84 (an upgrade name; empty: none)
	static void buildFieldParse(MultiIniFieldParse &p);
};

// getModuleName RW 0x008C5A29, getModuleNameKey RW 0x008C5A2F, dtor RW 0x008C5ACA, xfer RW 0x008C5B1E
// RW 0x008C5A10 (dtor -> RW 0x008C54AA)
class FreeLifeBody : public RespawnBody
{
public:
	OBFME_MODULE_XFER(FreeLifeBody) // RW 0x008C5B1E
	FreeLifeBody(Thing *thing, const FreeLifeBodyModuleData *data); // RW 0x8C5A74
	~FreeLifeBody() override = default;                            // RW 0x008C5ACA
	void attemptDamage(DamageInfo &info) override;                  // RW 0x8C5AE6
	void internalChangeHealth(float delta) override;                // RW 0x8C5B86
	void crc(StateHasher &hasher) const override;

	bool used() const { return m_used; }
	static void registerClass(ModuleFactory &modules);

private:
	const FreeLifeBodyModuleData *m_freeLife;
	bool m_used = false;           ///< + 0x108
	UnsignedInt m_lastFrame = 0;   ///< + 0x10C
};
