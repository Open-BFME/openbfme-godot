// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// The structure death path and the body classes that had no runtime (lane COMBAT-2): StructureBody, InactiveBody, StructureCollapseUpdate.
//
// TARGET FACTS (RotWK game.dat, caveat S-001; the registry golden engine/data/rotwk-201/module-registry.json):
//   * StructureBody: create RW 0x651268, data RW 0x656D84, tables 0xC71D68 (ActiveBody's) + 0xC84858 (EMPTY): the data is ActiveBody's. The object is 0x104 bytes: ActiveBody (RW 0x8C3841)
//     plus one dword at +0x100 (ctor RW 0x8C4AED stores 0 and its own vtables 0xC721A0 / 0xC72F08 / 0xC720F0). The body interface table 0xC720F0 is ActiveBody's table, slot for
//     slot (checked: the vtable diff against ActiveBody's primary 0xC71B78 is slots 0..4, the module slots dtor / name / crc / xfer, only): a StructureBody damages, heals and dies exactly as
//     an ActiveBody. The +0x100 dword is ZH's m_constructorObjectID (StructureBody::setConstructorObject, ZH StructureBody.cpp).
//   * InactiveBody: create RW 0x651113, data RW 0x708243, NO field table; ctor RW 0x8C1A06 (BodyModule base RW 0x8C1946, vtables 0xC71978 / 0xC718C8) ends with setEffectivelyDead(true)
//     (RW 0x68D950). attemptDamage RW 0x8C1A75: a HEALING hit goes to attemptHealing; every other hit zeroes the output (+0x70 / +0x74), noEffect = true; UNRESISTABLE (type 8) clears noEffect
//     and, once, runs the object's onDie (RW 0x698F06) and sets m_dieCalled. estimateDamage RW 0x8C18F7: the amount for UNRESISTABLE, else 0. = ZH InactiveBody.cpp.
//   * StructureCollapseUpdate: create RW 0x64EA7F, data RW 0x6568E7 (ctor RW 0x6567C9), tables 0xC69238 (MinCollapseDelay +0x38, MaxCollapseDelay +0x3C, MinBurstDelay +0x40, MaxBurstDelay +0x44,
//     CollapseDamping +0x4C, MaxShudder +0x50, BigBurstFrequency +0x48, OCL and FXList by phase, DestroyObjectWhenDone +0xF4, CollapseHeight +0xF8) + the DieMux table 0xC76BD8 at +8. Defaults
//     (RW 0x6567C9): the two collapse delays 0, MinBurstDelay 9999 (0x270F), BigBurstFrequency 0, damping 0, shudder 0, DestroyObjectWhenDone false, CollapseHeight 0, every FX / OCL
//     count 1 (RW +0xCC / +0xE0, 5 each). MaxBurstDelay (+0x44) is NOT written by the constructor (heap garbage in retail; the port starts it at 0). Phase names RW 0xDB0E7C:
//     INITIAL, DELAY, BURST, ALMOST_FINAL, FINAL (ZH has four: ALMOST_FINAL is BFME's).
//     The module (vtable 0xC690F4; update at RW 0x8A7D1D, onDie 0x8A7CCC, begin 0x8A7BF9, phases 0x8A7A62, collapse height 0x8A7A04, done 0x8A7B2A): fields +0x24 collapseFrame,
//     +0x28 burstFrame, +0x2C state (0 standing, 1 waiting, 2 collapsing, 3 done), +0x30 velocity, +0x34 currentHeight, +0x38..0x40 the position at the start (a Coord3D).
//     onDie: when the DieMux applies, the AI is marked dead, the object is deselected for everyone (client) and begin runs. begin: the position is saved, collapseFrame = now + logic
//     random(min, max), phase INITIAL fires, state = waiting, height 0; an object with the model condition DESTROYED_WHILST_BEING_CONSTRUCTED (index 0x14E) starts lower: height =
//     -(collapseHeight * (1 - constructionPercent * 0.01)) and the object moves there (RW 0x8A7C69..0x8A7CB5); the module wakes every frame. update (see the .cpp for the order of every
//     draw): waiting: the drawable shudders (client random), at collapseFrame state = collapsing, phase BURST, burstFrame = now + random(minBurst, maxBurst); collapsing: height -= velocity,
//     velocity -= gravity * (1 - damping) [RW computes it as velocity - ((1 - damping) * gravity)], the OBJECT is moved to the saved position + (random shudder drawn from the LOGIC generator,
//     RW 0x6D332C, the first draw added to y, the second to x) with z + height; at burstFrame a phase fires (BURST when random(1, bigBurstFrequency) == 1, else DELAY) and burstFrame
//     += random(minBurst, maxBurst); then the fall is over when collapseHeight + height <= 0: state = done, phase FINAL, the bone FX stop, DestroyObjectWhenDone destroys the object
//     (RW 0x62BBAB), the model conditions AWAITING_CONSTRUCTION / PARTIALLY_CONSTRUCTED / ACTIVELY_BEING_CONSTRUCTED / RUBBLE are cleared and POST_RUBBLE is set (RW 0x8A8001:
//     bit 0x8000000 of the second mask word = index 59; ZH sets POST_COLLAPSE here), the module sleeps forever; while collapseHeight * 0.7 + height <= 0 (and not done) the phase
//     ALMOST_FINAL fires EVERY frame. A phase fires one random entry of each non-empty FX list and OCL list: the index is one logic random draw (RW 0x8A78D5: random(0, size - 1),
//     repeated only for a duplicate: with the default count 1 never).
//     collapseHeight (RW 0x8A7A04): DestroyObjectWhenDone ? max(CollapseHeight, the template geometry's max height above position) : CollapseHeight.
//   * RespawnBody: create RW 0x6512F9, data RW 0x651334, tables 0xC71D68 + 0xC723FC (PermanentlyKilledByFilter +0x64, an ObjectFilter, default NONE RW 0x763D11; CanRespawn +0x68, default
//     true, RW 0x8C56E6); ctor RW 0x8C547A over the ActiveBody ctor; the body table 0xC72430 is ActiveBody's but for slot 0x21, internalChangeHealth RW 0x8C553F: a delta that would kill
//     (delta <= -health) is classed: CanRespawn false, or a killer (the info's source) that passes PermanentlyKilledByFilter, is permanent; ActiveBody's change follows; then the object's
//     RespawnUpdate (module key RW 0xC0B098) is told (RW 0x8B3349 permanently killed / 0x8B3744 start the respawn) and the death runs (Object::onDie RW 0x698F06). RespawnUpdate has no
//     runtime here: a RespawnBody then behaves as an ActiveBody (the counter `respawnWithoutUpdate`, stop S-343).
//   * DelayedDeathBody (RW create 0x651385, data 0x6513C0, tables 0xC71D68 + 0xC723FC + 0xC72628: DelayedDeathTime +0x6C, ImmortalUntilDeathTime +0x70 default true, InvulnerableFX +0x74,
//     DoHealthCheck +0x78 default true, DelayedDeathPrerequisiteUpgrade +0x7C): derived from RespawnBody (ctor RW 0x8C5763; object fields +0xF0 started, +0xF1 checked); its internalChangeHealth
//     (RW 0x8C5828 = B1's Rva00212980Owner::apply): before the first delayed death a kill hit (when `checked`) or a hit that would take the health to 0 or below (DoHealthCheck) starts it
//     when the player or the object has the prerequisite upgrade (or none is named): the object's LifetimeUpdate gets the lifetime range [DelayedDeathTime, DelayedDeathTime] (RW 0x7A7E41),
//     the FX plays, the kill flag of the hit is cleared and `started` is set; while `checked` or `started` the hit is zeroed when ImmortalUntilDeathTime or on the transition; after the
//     start a kill hit takes the whole health. slot 0x26 (RW 0x8C57A1) sets `checked`.
//   * SymbioticStructuresBody (RW create 0x6512BE, data 0x656A4D, tables 0xC71D68 + 0xC0602C: Symbiote +0x64, a name; ctor RW 0x8C4C82, object 0x10C bytes: +0x100 the symbiote's body,
//     +0x104 its object id): the body table 0xC72200 forwards getHealth, getMaxHealth, getInitialHealth, getDamageState, estimateDamage, attemptHealing and the rest to the symbiote's
//     body (RW 0x8C4EE0 resolves it from the id, an object that has none answers 0 / false) and attemptDamage on the symbiotic object itself does nothing (slot 0 is a `ret 4` stub).
//   * LifetimeUpdate: create RW 0x64E053, data RW 0x64E08B (all fields 0 by default: RW 0x7A7E0B), table 0xC31860: MinLifetime +8, MaxLifetime +0xC (durations), WaitForWakeUp +0x10, ScoreKill
//     +0x11, DeathType +0x14; ctor RW 0x7A7EE6: with WaitForWakeUp the module sleeps for ever, else the delay is random(min, max) of the LOGIC generator (RW 0x7A7D7E: at least 1 frame; a
//     HULK template takes GameLogic + 0xA0 when it is not -1: no such override here) and dieFrame = frame + delay; update RW 0x7A7F8B: while the object has the model condition
//     THROWN_PROJECTILE (index 0x9A) it asks again next frame; else with ScoreKill the last damager's player scores the kill (Object::scoreTheKill), then Object::kill with the
//     DeathType (RW 0x698EC3) and the module sleeps for ever.
// DONOR: ZH StructureBody.cpp, InactiveBody.cpp, StructureCollapseUpdate.cpp (B1's byte-exact reconstruction agrees on the layout), ZH LifetimeUpdate.cpp, B1 DelayedDeathBody.cpp.
//
// INFERENCE / NOT PORTED (stop S-340): the FX lists and OCLs a phase fires are the client's / the OCL lane's (the names are kept, the logic random draw of the index is made and the use
// counted); the drawable shudder of the waiting state and the instance matrix are the client's (RW draws the CLIENT generator there: not a logic input); the geometry height is the
// maximum over the template's shapes that are active in the template (GeometryUpgrade modules switch shapes at run time: not ported); the BoneFXUpdate stop is not
// ported; deselecting the object belongs to InGameUI (it forgets destroyed objects).

#pragma once

#include "Common/INI.h"
#include "GameLogic/Module/ActiveBody.h"
#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/Module/CombatModules.h"
#include "GameLogic/Module/DieModule.h"
#include "GameLogic/Module/MoneyEventModules.h"
#include "GameLogic/Module/UpdateModule.h"

#include <memory>
#include <string>
#include <vector>

class ModuleFactory;
struct ObjectFilter;

namespace StructureModules
{
void registerAll(ModuleFactory &modules);
}

// StructureBody: ActiveBody plus the constructor's object id (RW +0x100, ZH m_constructorObjectID)
class StructureBody : public ActiveBody
{
public:
	StructureBody(Thing *thing, const ActiveBodyModuleData *data) : ActiveBody(thing, data) {}
	// ZH StructureBody::setConstructorObject: a null object leaves the id alone
	void setConstructorObject(const Object *obj);
	ObjectID constructorObjectID() const { return m_constructorObjectID; }
	void crc(StateHasher &hasher) const override;

private:
	ObjectID m_constructorObjectID = INVALID_ID;
};

class InactiveBodyModuleData : public ModuleData
{
public:
	static void buildFieldParse(MultiIniFieldParse &p);
};

// ZH InactiveBody: no health, never damaged, dies only to UNRESISTABLE damage
class InactiveBody : public BehaviorModule, public BodyModuleInterface
{
public:
	InactiveBody(Thing *thing, const InactiveBodyModuleData *data);
	BodyModuleInterface *getBody() override { return this; }
	float getHealth() const override { return 0.0f; }
	float getMaxHealth() const override { return 0.0f; }
	float getInitialHealth() const override { return 0.0f; }
	BodyDamageType getDamageState() const override { return BODY_PRISTINE; }
	void setInitialHealth(int) override {}
	void attemptDamage(DamageInfo &info) override;
	void attemptHealing(DamageInfo &info) override;
	float estimateDamage(const DamageInfoInput &input) const override;
	bool dieCalled() const { return m_dieCalled; }
	void crc(StateHasher &hasher) const override;

private:
	bool m_dieCalled = false;
};

// RW phase name table 0xDB0E7C
enum StructureCollapsePhase
{
	SCPHASE_INITIAL = 0,
	SCPHASE_DELAY,
	SCPHASE_BURST,
	SCPHASE_ALMOST_FINAL,
	SCPHASE_FINAL,
	SCPHASE_COUNT
};

enum StructureCollapseState
{
	COLLAPSESTATE_STANDING = 0,
	COLLAPSESTATE_WAITINGFORCOLLAPSESTART,
	COLLAPSESTATE_COLLAPSING,
	COLLAPSESTATE_DONE
};

class StructureCollapseUpdateModuleData : public ModuleData
{
public:
	DieMuxData m_dieMux;                 ///< RW +8
	unsigned m_minCollapseDelay = 0;     ///< +0x38 (frames)
	unsigned m_maxCollapseDelay = 0;     ///< +0x3C
	unsigned m_minBurstDelay = 9999;     ///< +0x40
	unsigned m_maxBurstDelay = 0;        ///< +0x44 (not initialised by the retail constructor)
	int m_bigBurstFrequency = 0;         ///< +0x48
	float m_collapseDamping = 0.0f;      ///< +0x4C
	float m_maxShudder = 0.0f;           ///< +0x50
	std::vector<std::string> m_ocl[SCPHASE_COUNT]; ///< +0x54..: one entry per name token, in file order
	std::vector<std::string> m_fx[SCPHASE_COUNT];  ///< +0x90..
	int m_oclCount[SCPHASE_COUNT] = { 1, 1, 1, 1, 1 }; ///< +0xCC
	int m_fxCount[SCPHASE_COUNT] = { 1, 1, 1, 1, 1 };  ///< +0xE0
	bool m_destroyObjectWhenDone = false; ///< +0xF4
	float m_collapseHeight = 0.0f;        ///< +0xF8
	static void buildFieldParse(MultiIniFieldParse &p);
};

class StructureCollapseUpdate : public UpdateModule, public DieModuleInterface
{
public:
	StructureCollapseUpdate(Thing *thing, const StructureCollapseUpdateModuleData *data);
	DieModuleInterface *getDie() override { return this; }
	void onDie(const DieModuleInterface::Event &event) override;
	UpdateSleepTime update() override;
	void crc(StateHasher &hasher) const override;
	const StructureCollapseUpdateModuleData *data() const { return m_data; }
	StructureCollapseState state() const { return m_state; }
	unsigned collapseFrame() const { return m_collapseFrame; }
	unsigned burstFrame() const { return m_burstFrame; }
	float currentHeight() const { return m_currentHeight; }
	float velocity() const { return m_velocity; }
	const Coord3D &collapsePosition() const { return m_position; }
	// RW 0x8A7A04
	float getCollapseHeight() const;

private:
	void begin();
	void doPhase(StructureCollapsePhase phase);
	const StructureCollapseUpdateModuleData *m_data;
	unsigned m_collapseFrame = 0; // RW +0x24
	unsigned m_burstFrame = 0;    // +0x28
	StructureCollapseState m_state = COLLAPSESTATE_STANDING; // +0x2C
	float m_velocity = 0.0f;      // +0x30
	float m_currentHeight = 0.0f; // +0x34
	Coord3D m_position{};         // +0x38
};

// ---- RespawnBody / DelayedDeathBody -------------------------------------------------------------------------------------------------------------
class RespawnBodyModuleData : public ActiveBodyModuleData
{
public:
	RespawnBodyModuleData();
	std::shared_ptr<const ObjectFilter> m_permanentlyKilledByFilter; ///< +0x64
	bool m_canRespawn = true;                                        ///< +0x68
	static void buildFieldParse(MultiIniFieldParse &p);
};

class RespawnBody : public ActiveBody
{
public:
	RespawnBody(Thing *thing, const RespawnBodyModuleData *data) : ActiveBody(thing, data), m_respawn(data) {}
	void internalChangeHealth(float delta) override;
	const RespawnBodyModuleData *respawnData() const { return m_respawn; }
	// RW 0x8C553F: the delta would take the health to 0 or below (delta <= -health)
	bool wouldDie(float delta) const;
	// the killer of the hit being processed is one the body never lets respawn (CanRespawn false, or PermanentlyKilledByFilter)
	bool permanentlyKilled() const;

private:
	const RespawnBodyModuleData *m_respawn;
};

class DelayedDeathBodyModuleData : public RespawnBodyModuleData
{
public:
	unsigned m_delayedDeathTime = 0;          ///< +0x6C (frames)
	bool m_immortalUntilDeathTime = true;     ///< +0x70
	std::string m_invulnerableFX;             ///< +0x74
	bool m_doHealthCheck = true;              ///< +0x78
	std::string m_prerequisiteUpgrade;        ///< +0x7C (an upgrade name)
	static void buildFieldParse(MultiIniFieldParse &p);
};

class DelayedDeathBody : public RespawnBody
{
public:
	DelayedDeathBody(Thing *thing, const DelayedDeathBodyModuleData *data) : RespawnBody(thing, data), m_delayed(data) {}
	void internalChangeHealth(float delta) override;
	// RW slot 0x26 (RW 0x8C57A1)
	void setChecked(bool checked) { m_checked = checked; }
	bool checked() const { return m_checked; }
	bool started() const { return m_started; }
	void crc(StateHasher &hasher) const override;

private:
	const DelayedDeathBodyModuleData *m_delayed;
	bool m_started = false; // RW + 0xF0
	bool m_checked = false; // RW + 0xF1
};

// ---- SymbioticStructuresBody --------------------------------------------------------------------------------------------------------------------
class SymbioticStructuresBodyModuleData : public ActiveBodyModuleData
{
public:
	std::string m_symbiote; ///< +0x64
	static void buildFieldParse(MultiIniFieldParse &p);
};

class SymbioticStructuresBody : public ActiveBody
{
public:
	SymbioticStructuresBody(Thing *thing, const SymbioticStructuresBodyModuleData *data) : ActiveBody(thing, data), m_symbioticData(data) {}
	// RW + 0x104: the object whose body this one answers with (INVALID_ID until something links them: stop S-343)
	void setSymbiote(ObjectID id) { m_symbioteId = id; }
	ObjectID symbioteId() const { return m_symbioteId; }
	BodyModuleInterface *symbioteBody() const;
	float getHealth() const override;
	float getMaxHealth() const override;
	float getInitialHealth() const override;
	BodyDamageType getDamageState() const override;
	// the symbiotic object has no health of its own (it answers with its symbiote's): its own 0 health is not a RUBBLE state to apply
	void applyInitialDamageState() override {}
	void attemptDamage(DamageInfo &info) override;
	void attemptHealing(DamageInfo &info) override;
	float estimateDamage(const DamageInfoInput &input) const override;
	void crc(StateHasher &hasher) const override;

private:
	const SymbioticStructuresBodyModuleData *m_symbioticData;
	ObjectID m_symbioteId = INVALID_ID;
};

// ---- LifetimeUpdate -------------------------------------------------------------------------------------------------------------------------------
class LifetimeUpdateModuleData : public ModuleData
{
public:
	unsigned m_minLifetime = 0;  ///< +8 (frames)
	unsigned m_maxLifetime = 0;  ///< +0xC
	bool m_waitForWakeUp = false; ///< +0x10
	bool m_scoreKill = false;     ///< +0x11
	int m_deathType = 0;          ///< +0x14 (DeathType index)
	static void buildFieldParse(MultiIniFieldParse &p);
};

class LifetimeUpdate : public UpdateModule
{
public:
	LifetimeUpdate(Thing *thing, const LifetimeUpdateModuleData *data);
	UpdateSleepTime update() override;
	// RW 0x7A7E41: a new lifetime of random(min, max) frames from now
	void setLifetimeRange(unsigned minFrames, unsigned maxFrames);
	// RW 0x7A7E60: the data's own range (a WaitForWakeUp module starts its lifetime here)
	void wakeUp();
	unsigned dieFrame() const { return m_dieFrame; }
	bool waitsForWakeUp() const { return m_data->m_waitForWakeUp; }
	void crc(StateHasher &hasher) const override;

private:
	unsigned calcSleepDelay(unsigned minFrames, unsigned maxFrames); // RW 0x7A7D7E
	const LifetimeUpdateModuleData *m_data;
	unsigned m_dieFrame = 0;   // RW + 0x20
	unsigned m_startFrame = 0; // RW + 0x24
};
