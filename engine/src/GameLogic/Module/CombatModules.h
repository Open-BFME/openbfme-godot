// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// The combat modules (lane COMBAT-1): the body variants that share ActiveBody's data table (ImmortalBody, HighlanderBody) and the die modules of the death path: SlowDeathBehavior,
// DestroyDie, KeepObjectDie and FXListDie.
//
// TARGET FACTS (RotWK game.dat, caveat S-001; the registry golden engine/data/rotwk-201/module-registry.json and field-tables.json):
//   * ActiveBody RW create 0x65114B / data 0x651186 (table 0xC71D68); HighlanderBody RW create 0x6511D7 and ImmortalBody RW create 0x651212 use THE SAME data proc and table; their runtime
//     differs by the vtable (scratch/weapon1/damage.md 2.8);
//   * SlowDeathBehavior: create 0x64AF4D, data 0x64AF85, tables 0xC58A98 (SinkRate, ProbabilityModifier, ModifierBonusPerOverkillPercent, SinkDelay, SinkDelayVariance,
//     DestructionDelay, DestructionDelayVariance, DecayBeginTime, FX, OCL, Weapon, Sound, FlingForce, FlingForceVariance, FlingPitch, FlingPitchVariance, DeathFlags, ShadowWhenDead,
//     FadeDelay, FadeTime, DoNotRandomizeMidpoint) and the DieMux table 0xC76BD8;
//   * DestroyDie: create 0x64C749, data 0x654BCC, the DieMux table only; KeepObjectDie: create 0x64C8B2, data 0x65391A, DieMux + CollapsingTime / StayOnRadar (0xC06C2C);
//     FXListDie: create 0x64C781, data 0x653817, DieMux + DeathFX / OrientToObject (0xC06B78).
// DONOR: ZH SlowDeathBehavior.cpp / B1 SlowDeathBehavior.cpp (onDie roulette, beginSlowDeath, update), ZH DestroyDie.cpp.
//
// WHAT IS INFERENCE / NOT PORTED (stop S-324): the FX / OCL / Weapon phase effects and the Sound of SlowDeathBehavior (parsed and counted, not played), DeathFlags (parsed as a token
// list), FlingForce (needs PhysicsBehavior), DecayBeginTime / FadeDelay / FadeTime / ShadowWhenDead (client side fading), the LOD death speed-up and the DISABLED_HELD of a sinking
// body; KeepObjectDie does nothing (an object with no DestroyDie / SlowDeath simply stays); FXListDie records the event without playing the FX list.

#pragma once

#include "Common/INI.h"
#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/Module/ActiveBody.h"
#include "GameLogic/Module/DieModule.h"
#include "GameLogic/Module/MoneyEventModules.h"
#include "GameLogic/Module/UpdateModule.h"

#include <string>
#include <vector>

class ModuleFactory;

namespace CombatModules
{
void registerAll(ModuleFactory &modules);
}

class DestroyDieModuleData : public ModuleData
{
public:
	DieMuxData m_dieMux;
	static void buildFieldParse(MultiIniFieldParse &p);
};

class DestroyDie : public BehaviorModule, public DieModuleInterface
{
public:
	DestroyDie(Thing *thing, const DestroyDieModuleData *data) : BehaviorModule(thing, data), m_data(data) {}
	DieModuleInterface *getDie() override { return this; }
	void onDie(const DieModuleInterface::Event &event) override;

private:
	const DestroyDieModuleData *m_data;
};

class KeepObjectDieModuleData : public ModuleData
{
public:
	DieMuxData m_dieMux;
	unsigned m_collapsingTime = 0; ///< frames (RW +0x38)
	bool m_stayOnRadar = false;    ///< RW +0x3C
	static void buildFieldParse(MultiIniFieldParse &p);
};

class KeepObjectDie : public BehaviorModule, public DieModuleInterface
{
public:
	KeepObjectDie(Thing *thing, const KeepObjectDieModuleData *data) : BehaviorModule(thing, data), m_data(data) {}
	DieModuleInterface *getDie() override { return this; }
	void onDie(const DieModuleInterface::Event &) override {}

private:
	const KeepObjectDieModuleData *m_data;
};

class FXListDieModuleData : public ModuleData
{
public:
	DieMuxData m_dieMux;
	std::string m_deathFX;        ///< RW +0x38
	bool m_orientToObject = true; ///< RW +0x3C
	static void buildFieldParse(MultiIniFieldParse &p);
};

class FXListDie : public BehaviorModule, public DieModuleInterface
{
public:
	FXListDie(Thing *thing, const FXListDieModuleData *data) : BehaviorModule(thing, data), m_data(data) {}
	DieModuleInterface *getDie() override { return this; }
	void onDie(const DieModuleInterface::Event &event) override;

private:
	const FXListDieModuleData *m_data;
};

// PorcupineFormationBodyModule (RW create 0x65156D, data 0x6515A8, tables 0xC71D68 + 0xC72E18, vtable 0xC73028): ActiveBody plus the pikemen's reflected damage
class PorcupineFormationBodyModuleData : public ActiveBodyModuleData
{
public:
	std::string m_damageWeaponTemplate;      ///< RW +0x64 DamageWeaponTemplate (the weapon fired back at an attacker in reach while the horde holds the porcupine stance)
	std::string m_crushDamageWeaponTemplate; ///< RW +0x68 CrushDamageWeaponTemplate
	int m_crusherLevelResisted = 0;          ///< RW +0x6C CrusherLevelResisted
	static void buildFieldParse(MultiIniFieldParse &p);
};

// RW vtable 0xC73028: attemptDamage RW (B1 PorcupineFormationBodyModuleAttemptDamage.cpp) asks the object's damage check whether the formation is up; if so the PorcupineDamageHelper
// fires DamageWeaponTemplate at the attacker (status bit 0 clear, not KINDOF PORCUPINE_DAMAGE_EXCLUDED, in the weapon's unmodified range), then the ActiveBody damage runs either way.
// The formation (HoldGround stance) is not ported (stop S-325), so this runs the ActiveBody damage only.
class PorcupineFormationBody : public ActiveBody
{
public:
	PorcupineFormationBody(Thing *thing, const PorcupineFormationBodyModuleData *data) : ActiveBody(thing, data), m_porcupine(data) {}
	const PorcupineFormationBodyModuleData *porcupineData() const { return m_porcupine; }

private:
	const PorcupineFormationBodyModuleData *m_porcupine;
};

// ZH SlowDeathPhaseType
enum SlowDeathPhase
{
	SDPHASE_INITIAL = 0,
	SDPHASE_MIDPOINT,
	SDPHASE_FINAL,
	SDPHASE_HIT_GROUND, // RW name table 0x012AE110; not reached by the ported death path (a flung object landing)
	SDPHASE_COUNT
};

class SlowDeathBehaviorModuleData : public ModuleData
{
public:
	DieMuxData m_dieMux;
	float m_sinkRate = 0.0f;                    ///< RW +0x38 (velocity: per frame)
	int m_probabilityModifier = 10;             ///< RW +0x3C
	float m_modifierBonusPerOverkillPercent = 0.0f; ///< RW +0x40
	unsigned m_sinkDelay = 0;                   ///< RW +0x44 (frames)
	unsigned m_sinkDelayVariance = 0;           ///< +0x48
	unsigned m_destructionDelay = 0;            ///< +0x4C
	unsigned m_destructionDelayVariance = 0;    ///< +0x50
	unsigned m_decayBeginTime = 0;              ///< +0x54
	std::vector<std::string> m_fx[SDPHASE_COUNT];     ///< FX = <phase> <names...> (one entry per name)
	std::vector<std::string> m_ocl[SDPHASE_COUNT];
	std::vector<std::string> m_weapon[SDPHASE_COUNT];
	std::vector<std::string> m_sound;                  ///< Sound = <phase> <name> (kept as written)
	std::vector<std::string> m_sounds[SDPHASE_COUNT];  ///< lane FX-2: every name of the Sound lines per phase (RW + 0xE8 + 12 * phase)
	float m_flingForce = 0.0f, m_flingForceVariance = 0.0f; ///< +0x118 / +0x11C
	float m_flingPitch = 0.0f, m_flingPitchVariance = 0.0f; ///< +0x120 / +0x124
	std::vector<std::string> m_deathFlags;             ///< DeathFlags tokens
	bool m_shadowWhenDead = false;
	unsigned m_fadeDelay = 0, m_fadeTime = 0;
	bool m_doNotRandomizeMidpoint = false;
	// lane FX-2 review: RW byte +0x18C, the mask of resolved phase entries (1 FX, 2 OCL, 4 Weapon, 8 Sound); zero: the phases do nothing (RW 0x8609BE)
	int resolvedMask() const;
	static void buildFieldParse(MultiIniFieldParse &p);
};

// ZH SlowDeathBehavior: a die module that, when it wins the roulette among the applicable slow deaths, sinks the body after SinkDelay and removes the object after DestructionDelay
class SlowDeathBehavior : public UpdateModule, public DieModuleInterface
{
public:
	SlowDeathBehavior(Thing *thing, const SlowDeathBehaviorModuleData *data);
	DieModuleInterface *getDie() override { return this; }
	void onDie(const DieModuleInterface::Event &event) override;
	UpdateSleepTime update() override;
	// RW 0x8B3313 (the update interface's slot 1): the global all-types mask RW 0xDE8B90 (ZH SlowDeathBehavior: DISABLEDMASK_ALL): a dying object that is disabled
	// (a held passenger or siege crew, lane GARRISON-2) still dies
	DisabledMaskType getDisabledTypesToProcess() const override { return (DisabledMaskType)0xFFFFFFFFu; }
	void crc(StateHasher &hasher) const override;
	const SlowDeathBehaviorModuleData *data() const { return m_data; }
	bool isSlowDeathActivated() const { return m_activated; }
	// ZH SlowDeathBehavior::getProbabilityModifier: ProbabilityModifier + overkill percent * ModifierBonusPerOverkillPercent, at least 1
	int probabilityModifier(const DieModuleInterface::Event &event) const;
	bool isDieApplicable(const DieModuleInterface::Event &event) const;
	unsigned sinkFrame() const { return m_sinkFrame; }
	unsigned destructionFrame() const { return m_destructionFrame; }

private:
	void beginSlowDeath();
	void doPhase(SlowDeathPhase phase);
	const SlowDeathBehaviorModuleData *m_data;
	bool m_activated = false;
	bool m_midpointDone = false;
	unsigned m_sinkFrame = 0, m_midpointFrame = 0, m_destructionFrame = 0;
};
