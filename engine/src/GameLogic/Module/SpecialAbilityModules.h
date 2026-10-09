// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// The SpecialAbilityUpdate family (ZH Include/GameLogic/Module/SpecialAbilityUpdate.h as RotWK changes it), lane HERO-1: the update that drives
// most hero abilities. A SpecialPowerModule with UpdateModuleStartsAttack hands its do* to the object's update module whose special power
// template matches (RW 0x897E87: the initiate), and the update approaches the target, unpacks, prepares, triggers the ability's effect and packs.
//
// TARGET FACTS (RotWK game.dat, caveat S-001; each read from the disassembly):
//   * SpecialAbilityUpdateModuleData (constructor RW 0x851909, field table RW 0xC55F40, 61 rows; the offsets are the members' comments). Defaults:
//     StartAbilityRange / AbilityAbortRange 10000000.0 (RW 0xBDE694), PersistentCount -1, SkillPointsForTriggering -1 (the XP award), EffectValue 1,
//     MaxSpecialObjects 1, ApproachRequiresLOS true, the custom / grab / trigger model conditions -1.
//   * SpecialAbilityUpdate (vtable RW 0xC563D0, constructor RW 0x851D42: sleeps forever; update interface RW 0xC769EC, update RW 0x854DF7; the
//     special power update interface RW 0xC64658 at module + 0x20, its slot 0 the initiate RW 0x852E8F). Virtual slots of the main vtable:
//     0x34 finishAbility RW 0x8530BA, 0x38 approachTarget RW 0x85393B, 0x3C startPreparation RW 0x853BEC, 0x40 continuePreparation RW 0x852A3E,
//     0x44 triggerAbilityEffect RW 0x853EDF, 0x48 onEffectExpired RW 0x851E9A (none), 0x4C finishAfterPacking RW 0x8549E7, 0x50 handlePacking
//     RW 0x851E9F, 0x54 startPacking RW 0x853468, 0x58 startUnpacking RW 0x8536EA. The subclasses override them (each method below names its
//     address). Module fields: + 0x24 the trigger count, + 0x28 the pack / unpack frames left, + 0x2C an effect expiry frame (slot 0x48), + 0x30 the
//     packing state (0 none, 1 packing, 2 unpacking, 3 packed, 4 unpacked), + 0x3C the preparation frames left, + 0x40 the target, + 0x44 the target
//     location, + 0x5C the initiate's extra argument, + 0x60 the persistent triggers left, + 0x6C the command options, + 0x70 the capture FX
//     accumulator, + 0x74 active, + 0x78 the approach counter, + 0x7D no target, + 0x7E / + 0x7F the facing issued / done, + 0x80 within range
//     (sticky), + 0x82 the charge speed flag, + 0x83 the charge move, + 0x84 the custom animation frame.
//   * WeaponFireSpecialAbilityUpdate (vtable RW 0xC64818, constructor RW 0x895D98, table RW 0xC05408): the SpecialWeapon is a Weapon of its own
//     (module + 0x88: RW 0x68B150(template, slot 0), owner 0, loadAmmoNow RW 0x6CEE0F), fired at the target or the location (RW 0x8961F7).
//   * ToggleMountedSpecialAbilityUpdate (vtable RW 0xC6BDA0, table RW 0xC05A18), HeroModeSpecialAbilityUpdate (vtable RW 0xC64680, table RW
//     0xC052F0), LevelGrantSpecialPower (vtable RW 0xC735A0, table RW 0xC73550), ModelConditionSpecialAbilityUpdate (vtable RW 0xC63F38, table
//     RW 0xC05AF8: Aragorn's Elendil).
// Not ported (stops S-859 .. S-863, see stopLines): the special objects (no retail ability names SpecialObject), the capture / garrison /
// disguise / grab ability enums of the trigger switch, the face command before triggering (no AI face state: the facing check is the AI idle
// test), the line of sight test of ApproachRequiresLOS, the approach point of RW 0x851607 (the move goes to the target), sounds and voices
// (client), FleeRangeAfterCompletion, MountedTemplate (the object replacement RW 0x8B140D), the emotion system (Elendil's terror pulse is counted).

#pragma once

#include "Common/INI.h"
#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/Module/SpecialPowerModules.h"
#include "GameLogic/Module/UpdateModule.h"
#include "GameLogic/ObjectTypes.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class ModuleFactory;
class Weapon;
struct ObjectFilter;

class SpecialAbilityUpdateModuleData : public ModuleData
{
public:
	std::string m_specialPowerTemplateName;                     // + 0x38 (RW 0x73B22F)
	const SpecialPowerTemplate *m_specialPowerTemplate = nullptr;
	float m_startAbilityRange = 10000000.0f;                    // + 0x4C
	float m_abilityAbortRange = 10000000.0f;                    // + 0x50
	float m_packUnpackVariationFactor = 0.0f;                   // + 0x54
	float m_fleeRangeAfterCompletion = 0.0f;                    // + 0x58
	int m_effectValue = 1;                                      // + 0x5C
	float m_effectRange = 0.0f;                                 // + 0x60
	int m_awardXPForTriggering = 0;                             // + 0x64
	int m_skillPointsForTriggering = -1;                        // + 0x68
	int m_unpackingVariation = 0;                               // + 0x6C
	int m_persistentCount = -1;                                 // + 0x70
	unsigned m_preparationFrames = 0;                           // + 0x74
	unsigned m_persistentPrepFrames = 0;                        // + 0x78
	unsigned m_effectDuration = 0;                              // + 0x7C
	unsigned m_maxSpecialObjects = 1;                           // + 0x80
	unsigned m_packFrames = 0;                                  // + 0x84
	unsigned m_unpackFrames = 0;                                // + 0x88
	unsigned m_preTriggerUnstealthFrames = 0;                   // + 0x8C
	unsigned m_paralyzeWhenCompleted = 0;                       // + 0x90
	unsigned m_paralyzeWhenAborted = 0;                         // + 0x94
	unsigned m_attributeModifierDuration = 0;                   // + 0x98
	unsigned m_freezeAfterTriggerFrames = 0;                    // + 0x9C
	std::uint32_t m_requiredConditions = 0;                     // + 0xA0 (bit 0 MOUNTED, 1 WEAPONSET_TOGGLE_1, 2 MOVING: RW 0xDAE0E0)
	std::uint32_t m_rejectedConditions = 0;                     // + 0xA4
	bool m_skipPackingWithNoTarget = false;                     // + 0xA8
	bool m_specialObjectsPersistent = false;                    // + 0xA9
	bool m_uniqueSpecialObjectTargets = false;                  // + 0xAA
	bool m_specialObjectsPersistWhenOwnerDies = false;          // + 0xAB
	bool m_flipOwnerAfterPacking = false;                       // + 0xAC
	bool m_flipOwnerAfterUnpacking = false;                     // + 0xAD
	bool m_alwaysValidateSpecialObjects = false;                // + 0xAE
	bool m_doCaptureFX = false;                                 // + 0xAF
	bool m_loseStealthOnTrigger = false;                        // + 0xB0
	bool m_approachRequiresLOS = true;                          // + 0xB1
	bool m_chargeAttackSpeedBoost = false;                      // + 0xB2
	bool m_killAttributeModifierOnExit = false;                 // + 0xB3
	bool m_killAttributeModifierOnRejected = false;             // + 0xB4
	bool m_mustFinishAbility = false;                           // + 0xB5
	bool m_disableWhenWearingTheRing = false;                   // + 0xB6
	bool m_instant = false;                                     // + 0xB7
	bool m_needCollisionBeforeTrigger = false;                  // + 0xB8
	bool m_suppressForHordes = false;                           // + 0xC4
	bool m_approachUntilMembersInRange = false;                 // + 0xC5
	bool m_ignoreFacingCheck = false;                           // + 0xC6
	// CustomAnimAndDuration (+ 0x18, RW 0x851412: AnimState:<model condition> AnimTime:<duration> [TriggerTime:<duration>])
	int m_customAnimCondition = -1;
	unsigned m_customAnimFrames = 0;
	unsigned m_customAnimTriggerFrames = 0;
	int m_grabAnimCondition = -1;                               // + 0x24 GrabPassengerAnimAndDuration (the grab enum: S-860)
	unsigned m_grabAnimFrames = 0;
	unsigned m_grabTriggerFrames = 0;
	float m_grabPassengerHealGainPercent = 100.0f;              // + 0x30
	std::string m_specialObject, m_specialObjectAttachToBone;   // + 0x40 / + 0x44 (S-860)
	std::string m_triggerAttributeModifier;                     // + 0x48
	std::string m_disableFXParticleSystem;                      // + 0x3C
	std::string m_packSound, m_unpackSound, m_prepSoundLoop, m_triggerSound, m_activeLoopSound; // + 0x08 .. + 0x34 (client)
	std::string m_contactPointOverride;                         // + 0xBC
	std::string m_chainedButton;                                // + 0xC0
	int m_triggerModelCondition = -1;                           // + 0xC8 (RW 0x8514ED: ModelConditionState:<name>)
	float m_triggerModelConditionDuration = 0.0f;               // + 0xCC
	static void buildFieldParse(MultiIniFieldParse &p);
	void resolve() const;
};

class WeaponFireSpecialAbilityUpdateModuleData : public SpecialAbilityUpdateModuleData
{
public:
	std::string m_specialWeapon;        // + 0xD0
	int m_whichSpecialWeapon = 0;       // + 0xD4
	bool m_skipContinue = false;        // + 0xD8
	unsigned m_busyForFrames = 0;       // + 0xDC
	bool m_needLivingTargets = false;   // + 0xE0 (the binary reads it as "fire at the own position", RW 0x8962D1)
	bool m_playWeaponPreFireFX = false; // + 0xE1
	static void buildFieldParse(MultiIniFieldParse &p);
};

class ToggleMountedSpecialAbilityUpdateModuleData : public SpecialAbilityUpdateModuleData
{
public:
	float m_opacityTarget = 0.0f;                          // + 0xD0
	bool m_triggerInstantlyOnCreate = false;               // + 0xD4
	bool m_cancelDisguiseWhenDismounting = false;          // + 0xD5
	std::string m_mountedTemplate;                         // + 0xD8
	std::vector<std::string> m_synchronizeTimerOnSpecialPower; // + 0xDC
	static void buildFieldParse(MultiIniFieldParse &p);
};

class HeroModeSpecialAbilityUpdateModuleData : public SpecialAbilityUpdateModuleData
{
public:
	std::string m_heroAttributeModifier;   // + 0xD0
	unsigned m_heroEffectFrames = 0;       // + 0xD4
	bool m_useUserModelCondition = false;  // + 0xD8
	bool m_stopUnitBeforeActivating = false; // + 0xD9
	static void buildFieldParse(MultiIniFieldParse &p);
};

class LevelGrantSpecialPowerModuleData : public SpecialAbilityUpdateModuleData
{
public:
	int m_experience = 0;                                // + 0xD0
	float m_radiusEffect = 0.0f;                         // + 0xD4
	std::shared_ptr<const ObjectFilter> m_acceptanceFilter; // + 0xD8
	std::string m_levelFX;                               // + 0xDC
	static void buildFieldParse(MultiIniFieldParse &p);
};

class ModelConditionSpecialAbilityUpdateModuleData : public SpecialAbilityUpdateModuleData
{
public:
	// lane HERO-2: the defaults of the data constructor RW 0x894467: WhichSpecialPower 1, EmotionPulseRadius 50.0 (RW 0xBD88C4), ObjectFilter a valid ALL
	// filter (RW 0x763DAA with an empty KindOf mask: null here, which allows the same)
	int m_whichSpecialPower = 1;                         // + 0xD0
	bool m_generateTerror = false;                       // + 0xD4
	bool m_generateUncontrollableFear = false;           // + 0xD5
	float m_emotionPulseRadius = 50.0f;                  // + 0xD8
	std::shared_ptr<const ObjectFilter> m_objectFilter;  // + 0xDC
	static void buildFieldParse(MultiIniFieldParse &p);
};

// lane HERO-2: SpecialDisguiseUpdate (Eowyn's disguise; data table RW 0xC05C10 after the SpecialAbilityUpdate table)
class SpecialDisguiseUpdateModuleData : public SpecialAbilityUpdateModuleData
{
public:
	bool m_triggerInstantlyOnCreate = false;       // + 0xD0
	float m_opacityTarget = 0.0f;                  // + 0xD4 (client: the drawable's opacity during the pack / unpack)
	std::string m_disguiseAsTemplate;              // + 0xD8 (RW 0x6D1305 resolves it when used)
	std::string m_disguisedAsTemplateEnemy;        // + 0xDC DisguisedAsTemplate_EnemyPerspective (client: the enemy's portrait / name, RW 0x8B4440)
	std::string m_disguiseFX;                      // + 0xE0 (RW 0x73A302)
	bool m_forceMountedWhenDisguising = false;     // + 0xE4
	static void buildFieldParse(MultiIniFieldParse &p);
};

// RW 0xC64658 (the special power update interface, module + 0x20): slot 0 the initiate, slot 0x1C "does this module drive the template"
class SpecialPowerUpdateInterface
{
public:
	virtual ~SpecialPowerUpdateInterface() = default;
	virtual bool drivesSpecialPower(const SpecialPowerTemplate *t) const = 0;
	virtual void initiateIntentToDoSpecialPower(const SpecialPowerTemplate *t, Object *target, const Coord3D *loc, unsigned options, int extra) = 0;
	virtual bool isActive() const = 0;
};

class SpecialAbilityUpdate : public UpdateModule, public SpecialPowerUpdateInterface
{
public:
	SpecialAbilityUpdate(Thing *thing, const SpecialAbilityUpdateModuleData *data); // RW 0x851D42
	UpdateSleepTime update() override;                                              // RW 0x854DF7
	DisabledMaskType getDisabledTypesToProcess() const override { return (DisabledMaskType)(1u << 3); } // RW 0x855830: DISABLED_HELD

	bool drivesSpecialPower(const SpecialPowerTemplate *t) const override { return t && t == m_data->m_specialPowerTemplate; }
	// RW 0x852E8F
	void initiateIntentToDoSpecialPower(const SpecialPowerTemplate *t, Object *target, const Coord3D *loc, unsigned options, int extra) override;
	bool isActive() const override { return m_active; }
	void crc(StateHasher &h) const override;

	const SpecialAbilityUpdateModuleData *abilityData() const { return m_data; }
	int packingState() const { return m_packingState; }
	unsigned prepFrames() const { return m_prepFrames; }
	int triggerCount() const { return m_triggerCount; }
	unsigned abilitiesTriggered() const { return m_abilitiesTriggered; } // effects run (a test counter, hashed)
	unsigned long long unported() const { return m_unported; }
	unsigned captures() const { return m_captures; } // lane HERO-2: buildings captured (hashed)

	// RW 0x8530BA (slot 0x34): the ability ends (aborted: the abort paralysis; ownerDying: the special objects die with the owner)
	virtual void finishAbility(bool ownerDying, bool aborted);

protected:
	virtual void approachTarget();                 // slot 0x38, RW 0x85393B
	virtual void startPreparation();               // slot 0x3C, RW 0x853BEC
	virtual bool continuePreparation();            // slot 0x40, RW 0x852A3E
	virtual void triggerAbilityEffect();           // slot 0x44, RW 0x853EDF
	virtual void onEffectExpired() {}              // slot 0x48, RW 0x851E9A
	virtual void finishAfterPacking();             // slot 0x4C, RW 0x8549E7
	virtual bool handlePackingProcessing();        // slot 0x50, RW 0x851E9F
	virtual void startPacking(bool success);       // slot 0x54, RW 0x853468
	virtual void startUnpacking();                 // slot 0x58, RW 0x8536EA

	Object *target() const;
	SpecialPowerModule *mySpecialPowerModule() const; // RW 0x851362
	bool withinStartAbilityRange();                // RW 0x85207A
	bool withinAbortRange() const;                 // RW 0x8522E3
	bool captureTarget(SpecialPowerModule *spm);   // RW 0x8544F2 (lane HERO-2)
	bool conditionsAllow() const;                  // RW 0x851551
	void endPreparation();                         // RW 0x85260A
	void setUnpackingVariation();                  // the UnpackingVariation model condition (RW 0x853C64 .. 0x853CDE)
	void applyCustomAnim();                        // RW 0x851C48
	UpdateSleepTime calcSleep() const;             // RW 0x85266D
	bool needToUnpack() const;                     // RW 0x85133C
	bool needToPack() const;                       // RW 0x851316
	bool persistentTrigger() const;                // RW 0x852465
	void restartPersistent();                      // RW 0x8524BC
	void aiIdleFromAI();                           // RW 0x5E821A(2)
	unsigned now() const;

	const SpecialAbilityUpdateModuleData *m_data;
	int m_triggerCount = 0;          // + 0x24
	unsigned m_packFramesLeft = 0;   // + 0x28
	unsigned m_effectExpiry = 0;     // + 0x2C
	int m_packingState = 0;          // + 0x30
	unsigned m_prepFrames = 0;       // + 0x3C
	ObjectID m_targetID = INVALID_ID;// + 0x40
	Coord3D m_targetPos{};           // + 0x44
	Coord3D m_lastTargetPos{};       // + 0x50 (the approach's last target position)
	int m_extra = 0;                 // + 0x5C
	int m_persistentLeft = 0;        // + 0x60
	unsigned m_options = 0;          // + 0x6C
	float m_captureAccumulator = 0;  // + 0x70
	bool m_active = false;           // + 0x74
	unsigned m_approachCounter = 0;  // + 0x78
	bool m_keepActive = false;       // + 0x7C
	bool m_noTarget = false;         // + 0x7D
	bool m_facingIssued = false;     // + 0x7E
	bool m_facingDone = false;       // + 0x7F
	bool m_withinRange = false;      // + 0x80
	bool m_chargeSpeed = false;      // + 0x82
	bool m_chargeMove = false;       // + 0x83
	unsigned m_customAnimFrame = 0;  // + 0x84
	unsigned m_abilitiesTriggered = 0;
	unsigned m_captures = 0;
	unsigned long long m_unported = 0;
};

class WeaponFireSpecialAbilityUpdate : public SpecialAbilityUpdate
{
public:
	WeaponFireSpecialAbilityUpdate(Thing *thing, const WeaponFireSpecialAbilityUpdateModuleData *data); // RW 0x895D98
	~WeaponFireSpecialAbilityUpdate() override;
	void finishAbility(bool ownerDying, bool aborted) override; // RW 0x896033
	unsigned shotsFired() const { return m_shots; }
	Weapon *ownedWeapon() { return specialWeapon(); } // the special weapon (made on first use), for tests
	void crc(StateHasher &h) const override;

protected:
	bool continuePreparation() override; // RW 0x895E60
	void startUnpacking() override;      // RW 0x895ECE
	void triggerAbilityEffect() override; // RW 0x8961F7

private:
	int specialWeaponCondition() const;
	Weapon *specialWeapon();
	const WeaponFireSpecialAbilityUpdateModuleData *m_wf;
	std::unique_ptr<Weapon> m_weapon; // + 0x88
	bool m_weaponMade = false;
	unsigned m_shots = 0;
};

class ToggleMountedSpecialAbilityUpdate : public SpecialAbilityUpdate, public CreateModuleInterface
{
public:
	ToggleMountedSpecialAbilityUpdate(Thing *thing, const ToggleMountedSpecialAbilityUpdateModuleData *data);
	CreateModuleInterface *getCreate() override { return this; }
	void onCreate() override {}
	void onBuildComplete() override; // RW 0x8B11FE (create interface slot 1): TriggerInstantlyOnCreate triggers at once
	void finishAbility(bool ownerDying, bool aborted) override; // RW 0x8B1219
	unsigned toggles() const { return m_toggles; }
	ObjectID replacement() const { return m_replacement; }
	void crc(StateHasher &h) const override;

protected:
	bool continuePreparation() override;    // RW 0x8B42F3
	void triggerAbilityEffect() override;   // RW 0x8B1690
	void finishAfterPacking() override;     // RW 0x8B125F

private:
	const ToggleMountedSpecialAbilityUpdateModuleData *m_tm;
	void replaceWithMounted();          // RW 0x8B140D (lane HERO-2)
	bool m_replaceAfterPacking = false; // + 0x8C
	unsigned m_toggles = 0;
	ObjectID m_replacement = INVALID_ID; // the MountedTemplate object of the last replacement (tests; hashed)
};

class HeroModeSpecialAbilityUpdate : public SpecialAbilityUpdate
{
public:
	HeroModeSpecialAbilityUpdate(Thing *thing, const HeroModeSpecialAbilityUpdateModuleData *data);

protected:
	void triggerAbilityEffect() override; // RW 0x8959B5
	void onEffectExpired() override;      // RW 0x8958F8
	void startUnpacking() override;       // RW 0x8958BD

private:
	const HeroModeSpecialAbilityUpdateModuleData *m_hm;
};

class LevelGrantSpecialPower : public SpecialAbilityUpdate
{
public:
	LevelGrantSpecialPower(Thing *thing, const LevelGrantSpecialPowerModuleData *data);
	unsigned granted() const { return m_granted; }
	void crc(StateHasher &h) const override;

protected:
	void triggerAbilityEffect() override; // RW 0x8C6FA4

private:
	void grant(Object &o); // RW 0x8C6E82 -> 0x8C6D43
	const LevelGrantSpecialPowerModuleData *m_lg;
	unsigned m_granted = 0;
};

class ModelConditionSpecialAbilityUpdate : public SpecialAbilityUpdate
{
public:
	ModelConditionSpecialAbilityUpdate(Thing *thing, const ModelConditionSpecialAbilityUpdateModuleData *data);
	void finishAbility(bool ownerDying, bool aborted) override; // RW 0x8945C1
	unsigned emotionRequests() const { return m_emotions; }
	void crc(StateHasher &h) const override;

protected:
	bool continuePreparation() override;  // RW 0x8B42F3 (shared with ToggleMounted)
	void triggerAbilityEffect() override; // RW 0x89463A
	void startUnpacking() override;       // RW 0x89455B

private:
	int specialPowerCondition() const;
	const ModelConditionSpecialAbilityUpdateModuleData *m_mc;
	unsigned m_emotions = 0;
};

// lane HERO-2: SpecialDisguiseUpdate (vtable RW 0xC6CF88, constructor RW 0x8B439F: module + 0x8C the drawable opacity 1.0 .. OpacityTarget, + 0x90; the create
// interface RW 0xC6CF78 slot 1 RW 0x8B4410 is TriggerInstantlyOnCreate). The trigger (RW 0x8B4760) toggles the disguise: DISGUISED (model condition 300, Object
// + 0x130 bit 12) on with ForceMountedWhenDisguising's mount (RW 0x8B45BF), or off; the drawable of DisguiseAsTemplate replaces the object's (client). RW 0x8B4702
// ends it from SPECIAL_SMITE_CANCELDISGUISE's unpack (RW 0x8538DD), SPECIAL_ATTRIBUTEMOD_CANCELDISGUISE's trigger (RW 0x8549DD) and attribute modifier power
// (RW 0x897840), a stealth detector (RW 0x8A6644) and the mount toggles' CancelDisguiseWhenDismounting (RW 0x8B17C2 / 0x8B26BB).
class SpecialDisguiseUpdate : public SpecialAbilityUpdate, public CreateModuleInterface
{
public:
	SpecialDisguiseUpdate(Thing *thing, const SpecialDisguiseUpdateModuleData *data); // RW 0x8B439F
	CreateModuleInterface *getCreate() override { return this; }
	void onCreate() override {}
	void onBuildComplete() override;                            // RW 0x8B4410
	void finishAbility(bool ownerDying, bool aborted) override; // RW 0x8B4521
	// RW 0x8B4702: a DISGUISED object loses the disguise; `keepLook` false also gives the object its own drawable back and plays DisguiseFX at its position
	void cancelDisguise(bool keepLook);
	static SpecialDisguiseUpdate *of(Object &obj); // RW 0x68BDA5("SpecialDisguiseUpdate")
	float opacity() const { return m_opacity; }
	unsigned disguises() const { return m_disguises; }
	void crc(StateHasher &h) const override;

protected:
	bool continuePreparation() override;      // RW 0x8B42F3
	void triggerAbilityEffect() override;     // RW 0x8B4760
	bool handlePackingProcessing() override;  // RW 0x8B4455

private:
	void applyLook(bool ownLook);             // RW 0x8B45BF
	const SpecialDisguiseUpdateModuleData *m_sd;
	float m_opacity = 1.0f;                   // + 0x8C
	unsigned m_disguises = 0;
};

namespace SpecialAbilityModules
{
void registerAll(ModuleFactory &modules);
// RW 0x897E87 (the update part): the first module of the object whose special power update interface drives `t` gets the initiate; null for none
SpecialPowerUpdateInterface *findUpdate(const Object &obj, const SpecialPowerTemplate *t);
std::vector<std::string> stopLines();
// lane HERO-2: the field parsers the subclasses in HeroAbilityModules share: RW 0x73A302 (an FXList name or None) and RW 0x76392F (an ObjectFilter)
void parseFXField(INI *ini, void *instance, void *store, const void *userData);
void parseObjectFilterField(INI *ini, void *instance, void *store, const void *userData);
// lane HERO-2: the stops S-1221 .. of the abilities this lane ported (reported with the world's stops: RetailObjectWorld::acceptanceStops)
std::vector<std::string> hero2StopLines();
}
