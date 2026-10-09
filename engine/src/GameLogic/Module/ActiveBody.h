// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// ActiveBody (ZH Include/GameLogic/Module/ActiveBody.h, Source/GameLogic/Object/Body/ActiveBody.cpp), lane LOGIC-1: the health state of an
// object. Damage handling (attemptDamage, healing, the damage states' side effects) belongs to the combat lane; this port has the data, the
// constructor and the damage state calculation.
//
// TARGET FACTS (RotWK game.dat, caveat S-001):
//   * ActiveBodyModuleData: constructor RW 0x8C373F, field table RW 0xC71D68 (21 rows, below). Defaults: MaxHealth 0, InitialHealth -1.0
//     (RW 0xBD19DC), MaxHealthDamaged 0, MaxHealthReallyDamaged 0, DodgePercent 0, the three transition / recovery times 0,
//     UseDefaultDamageSettings true, GrabDamage 200.0 and CheerRadius 200.0 (RW 0xBE4170), flags false. The createData proc is RW 0x651186,
//     the create proc RW 0x65114B (an object of 0x100 bytes built by RW 0x8C3841).
//   * ActiveBody::ActiveBody (RW 0x8C3841): the initial health (+0x1C) and the max health (+0x20) are copied from the data; the damaged
//     fraction (+0x24) is MaxHealthDamaged * (1.0 / maxHealth) and the really damaged fraction (+0x28) MaxHealthReallyDamaged * (1.0 /
//     maxHealth) (SSE: the reciprocal first, then the products); the current health (+0x18) is the max health when InitialHealth == -1.0,
//     else the initial health, and the previous health (+0x2C) the same; with UseDefaultDamageSettings a fraction of 0 takes the
//     GlobalData value (RW 0xDE4364 + 0xB4 / + 0xB8 = UnitDamagedThreshold / UnitReallyDamagedThreshold, gamedata.ini); then the damage
//     state is set from the health (RW 0x8C2A5D).
//   * the damage state (RW 0x8C1B79, a function of the current health): 0 health = RUBBLE (3); reallyDamagedFraction * maxHealth >= health =
//     REALLYDAMAGED (2); damagedFraction * maxHealth >= health = DAMAGED (1); else PRISTINE (0).
//   * which GlobalData fields +0xB4 / +0xB8 are is INFERENCE (the INI names UnitDamagedThreshold / UnitReallyDamagedThreshold; the offsets
//     are not tied to the names): stop S-149.
// Not ported: the transition times and RecoveryTime consumers, DodgePercent, the Grab* fields (rock throwing), the attribute modifiers, the
// burning death behaviour, CheerRadius, the damage creation list: the fields are parsed and kept (the FX and OCL names unresolved: their
// stores are not loaded, stop S-149); nothing acts on them yet.

#pragma once

#include "Common/INI.h"
#include "Common/INIDataTypes.h"
#include "GameLogic/Module/BehaviorModule.h"

#include <string>
#include <vector>

class ArmorTemplate;

class ActiveBodyModuleData : public ModuleData
{
public:
	ActiveBodyModuleData() = default;

	// RW 0xC71D68, in the binary's order
	float m_maxHealth = 0.0f;
	float m_initialHealth = -1.0f;
	float m_maxHealthDamaged = 0.0f;
	float m_maxHealthReallyDamaged = 0.0f;
	unsigned m_recoveryTime = 0;
	float m_dodgePercent = 0.0f;
	unsigned m_enteringDamagedTransitionTime = 0;
	unsigned m_enteringReallyDamagedTransitionTime = 0;
	std::string m_grabObject;
	std::string m_grabFX;
	float m_grabDamage = 200.0f;
	Coord2D m_grabOffset;
	bool m_useDefaultDamageSettings = true;
	std::vector<std::vector<std::string>> m_damageCreationList; ///< one entry per `DamageCreationList = OCL DamageType [...]` line
	std::string m_healingBuffFx;
	std::string m_damagedAttributeModifier;
	std::string m_reallyDamagedAttributeModifier;
	float m_cheerRadius = 200.0f;
	bool m_removeUpgradesOnDeath = false;
	bool m_burningDeathBehavior = false;
	std::string m_burningDeathFX;

	static void buildFieldParse(MultiIniFieldParse &p);
};

class ActiveBody : public BehaviorModule, public BodyModuleInterface
{
public:
	ActiveBody(Thing *thing, const ActiveBodyModuleData *data);

	BodyModuleInterface *getBody() override { return this; }
	float getHealth() const override { return m_currentHealth; }
	float getMaxHealth() const override { return m_maxHealth; }
	float getInitialHealth() const override { return m_initialHealth; }
	float getPreviousHealth() const { return m_previousHealth; }
	// lane HERO-1 (Sol review): the reference the revive scales (RW 0x8C47DE reads body interface + 0x1C = module + 0x2C; RW 0x8C1CD5 setMaxHealth stores newMax
	// there). Distinct from the previous health internalChangeHealth keeps (RW 0x8C31A5 writes interface + 0x0C); it starts at the actual initial health
	float getReviveReference() const { return m_reviveReference; }
	BodyDamageType getDamageState() const override { return calcDamageState(); }
	void applyInitialDamageState() override { updateDamageState(true); }
	void setInitialHealth(int initialPercent) override;
	const ActiveBodyModuleData *data() const { return m_data; }

	// ---- lane COMBAT-1: the damage pipeline (RW 0x8C3FA3; scratch/weapon1/damage.md 2.7) ----
	void attemptDamage(DamageInfo &info) override;
	void setIndestructible(bool on) override { m_indestructible = on; } // lane SCRIPT-2 (RW 0x8C3326)
	bool isIndestructible() const override { return m_indestructible; }
	void attemptHealing(DamageInfo &info) override;
	float estimateDamage(const DamageInfoInput &input) const override;
	float getDodgePercent() const override { return m_data->m_dodgePercent; }
	// lane XP-1: RW 0x8C1CD5
	void setMaxHealth(float newMax, int changeType) override;
	// RW 0x8C31A5 internalChangeHealth (Immortal overrides it, RW 0x8C4AB4)
	virtual void internalChangeHealth(float delta);
	// lane BUILD-2: RW body vslot 0xAC (RW 0x5015C6): stores the current health, nothing else (GettingBuiltBehavior::startConstruction puts the health back with it)
	void friend_setHealthRaw(float health) { m_currentHealth = health; }
	// the armour of the object's current armor set: the ArmorTemplate named by the best ArmorTemplateSet (RW 0x5D893C looks the name up per hit); null when the template has
	// no ArmorSet; an armour name the store lacks is reported once (a logic error, PLAN rule 10) and counts as no armour
	const ArmorTemplate *currentArmor() const;
	// ZH BodyModuleInterface::setDamageStateIgnoringBodyScalar: the body scalar (RW this + 4, multiplied in by body slot 0x6C): 1.0 by default
	float bodyDamageScalar() const { return m_damageScalar; }
	void setBodyDamageScalar(float s) { m_damageScalar = s; }
	// the last frame the body took a damaging hit and from whom (RW + 0xAC, the last damage record); 0xFFFFFFFF / INVALID_ID when never
	UnsignedInt lastDamageFrame() const { return m_lastDamageFrame; }
	ObjectID lastDamager() const { return m_lastDamager; }
	// lane COMBAT-2: the damage state the body last applied (RW + 0x30, written by setCorrectDamageState RW 0x8C2A5D)
	BodyDamageType appliedDamageState() const { return m_curDamageState; }
	void crc(StateHasher &hasher) const override;

protected:
	// the DamageInfo attemptDamage is processing (RW passes it down to internalChangeHealth, RW 0x8C553F / 0x8C5828 read and change it); null outside a hit
	DamageInfo *currentDamageInfo() const { return m_currentInfo; }
	// RW 0x8C2A5D setCorrectDamageState (called by internalChangeHealth RW 0x8C31A5 through vslot 0x54): recompute the damage state from the health; when it changed apply what the
	// state means to the object: the model conditions DAMAGED / REALLYDAMAGED / RUBBLE and, for a STRUCTURE entering or leaving RUBBLE, the rubble effects (see ActiveBody.cpp)
	// `initial`: the first application after the object was built (no position yet: the pathfinder footprint is registered later by whoever places the object, and sees the state)
	void updateDamageState(bool initial = false);
	float m_currentHealth = 0.0f;  // RW +0x18
	float m_previousHealth = 0.0f; // the previous health of internalChangeHealth (damage accounting)
	float m_reviveReference = 0.0f; // lane HERO-1: RW module + 0x2C (see getReviveReference)

private:
	class DamageHost;
	BodyDamageType calcDamageState() const;
	void doDamageFX(const DamageInfo &info); // RW 0x8C2F02 (lane FX-2)

	const ActiveBodyModuleData *m_data;
	float m_initialHealth = 0.0f;  // RW +0x1C
	float m_maxHealth = 0.0f;      // RW +0x20
	float m_damagedFraction = 0.0f;        // RW +0x24
	float m_reallyDamagedFraction = 0.0f;  // RW +0x28
	float m_damageScalar = 1.0f;   // RW this + 4 (the body's damage scalar)
	bool m_indestructible = false; // RW + 0xC7 (lane SCRIPT-2: the map's objectIndestructible, UNIT_AFFECT_OBJECT_PANEL_FLAGS)
	BodyDamageType m_curDamageState = BODY_PRISTINE; // RW + 0x30
	DamageInfo *m_currentInfo = nullptr;
	UnsignedInt m_lastDamageFrame = 0xFFFFFFFFu; // RW + 0xAC (RW 0x8C3841: -1 = never; BUILD-2)
	ObjectID m_lastDamager = INVALID_ID;
	mutable bool m_armorReported = false;
	// lane FX-2: the damage FX throttle of RW 0x8C2F02 (+0x3C the last DamageFX type played, +0x38 the frame until which that type stays throttled). Only the
	// client output depends on them; not hashed (the event queue boundary of FXEvents.h)
	int m_lastDamageFXType = 0;           // RW + 0x3C
	UnsignedInt m_damageFXThrottleUntil = 0; // RW + 0x38
};

// RW vtable 0xC71FE0: health never drops below 1 (internalChangeHealth delta = max(delta, 1 - health), RW 0x8C4AB4)
class ImmortalBody : public ActiveBody
{
public:
	using ActiveBody::ActiveBody;
	void internalChangeHealth(float delta) override;
};

// RW vtable 0xC71ED0, attemptDamage RW 0x8C49C7: a hit never takes more than health - 1 unless UNRESISTABLE
class HighlanderBody : public ActiveBody
{
public:
	using ActiveBody::ActiveBody;
	void attemptDamage(DamageInfo &info) override;
};
