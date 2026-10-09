// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// The special power modules (ZH Include/GameLogic/Module/SpecialPowerModule.h, PlayerHealSpecialPower is BFME's). Lane SPELL-1: the module data of
// the SpecialPowerModule base and PlayerHealSpecialPower, and PlayerHealSpecialPower's heal (its effect), the base runtime (ready frames, pause,
// shared timers) and OCLSpecialPower. Lane SPELL-2: the base do* gate and the trigger RW 0x897987 with its attribute modifiers (S-920), OCLSpecialPower's
// CreateLocation cases and UpgradeName; the other spell book classes are GameLogic/Module/SpellBookPowers.h (S-921). The initiate's update-module part
// (RW 0x897E87) stays S-529.
//
// TARGET FACTS (RotWK game.dat, caveat S-001; each read from the disassembly):
//   * SpecialPowerModuleData (constructor RW 0x896834, field table RW 0xC64DB0, 35 rows, buildFieldParse RW 0x89699B adds only it):
//     +0x08 SpecialPowerTemplate (RW 0x73B22F: TheSpecialPowerStore lookup by name), +0x0C UpdateModuleStartsAttack, +0x0D StartsPaused, +0x10
//     InitiateSound, +0x18 AttributeModifier, +0x1C AttributeModifierRange (0.0), +0x20 AttributeModifierAffectsSelf, +0x24 AttributeModifierAffects,
//     +0x28 AttributeModifierFX, +0x2C AttributeModifierWeatherBased, +0x30 WeatherDuration, +0x34 AntiCategory, +0x38 RequirementsFilterMPSkirmish,
//     +0x3C RequirementsFilterStrategic, +0x40 TargetEnemy, +0x41 TargetAllSides, +0x42 ReEnableAntiCategory, +0x44 InitiateFX, +0x48 TriggerFX,
//     +0x4C AntiFX, +0x50 SetModelCondition (-1), +0x54 SetModelConditionTime, +0x58 GiveLevels, +0x5C DisableDuringAnimDuration, +0x5D
//     IdleWhenStartingPower, +0x5E AffectGood, +0x5F AffectEvil, +0x60 AffectAllies, +0x61 AvailableAtStart, +0x64 ChangeWeather, +0x68
//     AdjustVictim, +0x6C OnTriggerRechargeSpecialPower, +0x70 BurnDecayModifier, +0x74 UseDistanceFromCommandCenter, +0x78
//     DistanceFromCommandCenter.
//   * PlayerHealSpecialPowerModuleData (constructor RW 0x8CC459, table RW 0xC74EC0 added after the base by RW 0x8CC282): +0x7C HealAmount (0.0),
//     +0x80 HealAsPercent (TRUE), +0x84 HealRadius (RW 0xBD88D8 = 100.0), +0x88 HealAffects (a KindOf mask, RW 0x6564E7, cleared), +0xA4 HealFX
//     (an FXList), +0xA8 HealOCL (an ObjectCreationList).
//   * PlayerHealSpecialPower (vtable RW 0xC74F98; its special power interface RW 0xC74F30): doSpecialPower (slot +0x28, RW 0x8CC5B7) heals at the
//     object's own position, doSpecialPowerAtObject (+0x2C, RW 0x8CC594) at the target object's position (+0x38), doSpecialPowerAtLocation (+0x30,
//     RW 0x8CC574) at the location; each calls the base first (RW 0x8980A3 / 0x8980EF / 0x89816C: the initiate path, S-529) and then:
//   * healAt(pos) (RW 0x8CC4B4): nothing when the object is disabled (any bit of its disabled mask, RW 0x9325B4 on + 0x1C8) or has no controlling
//     player (RW 0x68B678); HealOCL is created at pos (RW 0x5F00CA; lane SPELL-2: through the CreateObject port, S-530); then every object the partition manager
//     returns within HealRadius of pos (RW 0xA39340, filter RW 0xC10E20) is healed (RW 0x8CC37B):
//       skip a victim whose template is KindOf WEBBED (+0x11B bit 3 = KindOf 155), or matches none of HealAffects (RW 0x70C548); skip it unless it is
//       on the healer's team (Object + 0x74) or allied to the healer (victim relationship toward the healer == ALLIES, RW 0x68D7AB); skip a STRUCTURE
//       (+0x108 bit 7) whose construction percent (+0x288) is in [0, 99); skip a victim without a body (+0x25C); amount = HealAmount, times the body's
//       max health (vslot +0x1C, x87 fmul at the game's 24-bit precision) when HealAsPercent; amount > 0: Object::attemptHealing(amount, no source)
//       (RW 0x690532); then HealFX at the victim (client, counted).
// DONOR: BFME1 GameLogic/Object/SpecialPower/PlayerHealSpecialPower*.cpp (the same structure).

#pragma once

#include "Common/INI.h"
#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/ObjectTypes.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

class ModuleFactory;
class SpecialPowerTemplate;
struct ObjectFilter;
struct Coord3D;
class Object;
class StateHasher;

class SpecialPowerModuleData : public ModuleData
{
public:
	std::string m_specialPowerTemplateName;                    // +0x08 (resolved below; an unknown name is null, as RW's store lookup returns)
	const SpecialPowerTemplate *m_specialPowerTemplate = nullptr;
	bool m_updateModuleStartsAttack = false;                   // +0x0C
	bool m_startsPaused = false;                               // +0x0D
	std::string m_initiateSound;                               // +0x10 (name, S-520)
	std::string m_attributeModifier;                           // +0x18
	float m_attributeModifierRange = 0.0f;                     // +0x1C
	bool m_attributeModifierAffectsSelf = false;               // +0x20
	std::shared_ptr<const ObjectFilter> m_attributeModifierAffects; // +0x24
	std::string m_attributeModifierFX;                         // +0x28 (FXList name)
	bool m_attributeModifierWeatherBased = false;              // +0x2C
	unsigned m_weatherDuration = 0;                            // +0x30 frames
	unsigned m_antiCategory = 0;                               // +0x34 (a 15-category mask, RW 0x89F32D; lane SPELL-2)
	std::shared_ptr<const ObjectFilter> m_requirementsFilterMPSkirmish; // +0x38
	std::shared_ptr<const ObjectFilter> m_requirementsFilterStrategic;  // +0x3C
	bool m_targetEnemy = false;                                // +0x40
	bool m_targetAllSides = false;                             // +0x41
	bool m_reEnableAntiCategory = false;                       // +0x42
	std::string m_initiateFX, m_triggerFX, m_antiFX;           // +0x44 / +0x48 / +0x4C (FXList names)
	std::string m_setModelCondition;                           // +0x50 (the line, S-529)
	float m_setModelConditionTime = 0.0f;                      // +0x54
	int m_giveLevels = 0;                                      // +0x58
	bool m_disableDuringAnimDuration = false;                  // +0x5C
	bool m_idleWhenStartingPower = false;                      // +0x5D
	bool m_affectGood = false, m_affectEvil = false;           // +0x5E / +0x5F
	bool m_affectAllies = true;                                // +0x60 (RW 0x8968F0: 1; lane SPELL-2 corrected SPELL-1's false)
	bool m_availableAtStart = true;                            // +0x61 (RW 0x8968F4: 1; lane SPELL-2 corrected SPELL-1's false)
	int m_changeWeather = 5;                                   // +0x64 (RW 0x8966F9: index into RW 0xDB0074, 0..5; 5 = no change; lane SPELL-2)
	bool m_adjustVictim = false;                               // +0x68
	std::string m_onTriggerRechargeSpecialPower;               // +0x6C
	unsigned m_burnDecayModifier = 0;                          // +0x70
	bool m_useDistanceFromCommandCenter = false;               // +0x74
	float m_distanceFromCommandCenter = 0.0f;                  // +0x78

	static void buildFieldParse(MultiIniFieldParse &p); // RW 0x89699B
};

class PlayerHealSpecialPowerModuleData : public SpecialPowerModuleData
{
public:
	float m_healAmount = 0.0f;       // +0x7C
	bool m_healAsPercent = true;     // +0x80
	float m_healRadius = 100.0f;     // +0x84 (RW 0xBD88D8)
	std::array<std::uint32_t, 7> m_healAffects{}; // +0x88 (a KindOfMaskType: GameLogic/BitFlags.h is not included here, see WeaponStores.h)
	std::string m_healFX;            // +0xA4 (FXList name)
	std::string m_healOCL;           // +0xA8 (ObjectCreationList name)

	static void buildFieldParse(MultiIniFieldParse &p); // RW 0x8CC282
};

// ZH SpecialPowerModuleInterface: what the rest of the game asks of a special power module (RW interface vtable at module + 0x10; the slots are
// cited per method)
class SpecialPowerModuleInterface
{
public:
	virtual ~SpecialPowerModuleInterface() = default;
	virtual const SpecialPowerTemplate *getSpecialPowerTemplate() const = 0; // slot 0x18 (RW 0x8969CA)
	virtual bool isReady() const = 0;                                       // slot 0x04 (RW 0x896C72)
	virtual float getPercentReady() const = 0;                              // slot 0x08 (RW 0x896CF2)
	virtual unsigned getReadyFrame() const = 0;                             // slot 0x10 (RW 0x89724B)
	virtual void onScienceAcquired() = 0;                                   // slot 0x1C (RW 0x896B56)
	virtual void setReadyFrame(unsigned frame) = 0;                         // slot 0x20 (RW 0x99344A)
	virtual void startPowerRecharge(float percentOfCurrent) = 0;            // slot 0x3C (RW 0x896E31)
	virtual bool requirementsMet() const = 0;                               // RW 0x8969E5 (the RequirementsFilter of the game kind)
	// display reads: isReady / getPercentReady without inserting an absent shared timer (never used by the logic)
	virtual bool isReadyForDisplay() const = 0;
	virtual float getPercentReadyForDisplay() const = 0;
	virtual void doSpecialPower(unsigned options) = 0;                                  // slot 0x28
	virtual void doSpecialPowerAtObject(Object *target, unsigned options) = 0;          // slot 0x2C
	virtual void doSpecialPowerAtLocation(const Coord3D &loc, unsigned options) = 0;    // slot 0x30
	virtual void pauseCountdown(bool pause) = 0;                                        // slot 0x24 (lane HERO-2: in the interface, RW 0x8B9676 calls it so)
};

// SpecialPowerModule (RW module vtable 0xC65058, interface 0xC64FF0; constructor RW 0x8973EE). Module fields: + 0x14 the recharge length,
// + 0x18 the ready frame, + 0x1C the pause count, + 0x20 the pause frame, + 0x24 the paused percent, + 0x28 a "disabled by the update module"
// flag, + 0x2C the last target's team. The class itself is the runtime of the 'SpecialPowerModule' INI class (the attribute modifier powers: Rallying Call,
// War Chant, Blight, ... through the trigger, lane SPELL-2).
class SpecialPowerModule : public BehaviorModule, public SpecialPowerModuleInterface
{
public:
	SpecialPowerModule(Thing *thing, const SpecialPowerModuleData *data);
	SpecialPowerModuleInterface *getSpecialPower() override { return this; }
	const SpecialPowerModuleData *spData() const { return m_spData; }

	const SpecialPowerTemplate *getSpecialPowerTemplate() const override { return m_spData->m_specialPowerTemplate; }
	bool isReady() const override;
	float getPercentReady() const override;
	unsigned getReadyFrame() const override;
	void onScienceAcquired() override;
	void setReadyFrame(unsigned frame) override { m_readyFrame = frame; }
	void startPowerRecharge(float percentOfCurrent) override;
	bool requirementsMet() const override;
	bool isReadyForDisplay() const override;
	float getPercentReadyForDisplay() const override;
	void pauseCountdown(bool pause) override; // slot 0x24 (RW 0x896756; ScavengerSpecialPower overrides it, RW 0x8C88CF)
	void doSpecialPower(unsigned options) override;
	void doSpecialPowerAtObject(Object *target, unsigned options) override;
	void doSpecialPowerAtLocation(const Coord3D &loc, unsigned options) override;

	// lane HERO-1: the module's vslot 0x38 (RW 0x897D25 -> 0x897987(null)): the trigger a SpecialAbilityUpdate runs when its preparation starts (the
	// recharge, the light points and the trigger effects at the object; the subclass effect is not part of it)
	void triggerFromUpdate() { triggerSpecialPower(nullptr, nullptr); }
	// lane HERO-2: vslot 0x30 (RW 0x896F99; the + 0xC slot 0xB0, RW 0x6519C2, forwards to it): a module that is not paused (interface slot 0xC, RW
	// 0x6519A6), not disabled by its update module (+ 0x28), whose object is not SPECIAL_ABILITY_PACKING_UNPACKING_OR_USING and whose template is not
	// shared (+ 0x59) is ready now (+ 0x18 = now: SpecialPowerTimerRefreshSpecialPower)
	void refreshTimer();
	// lane HERO-2: RW 0x897368 (ToggleMounted's SynchronizeTimerOnSpecialPower, RW 0x8B134D): with the same template (+ 0x14), not shared, the source not
	// disabled by its update module and not paused, this module takes the source's pause count, recharge length, pause frame, ready frame and paused percent
	void copyTimerFrom(const SpecialPowerModule &src);
	unsigned rechargeLength() const { return m_rechargeLength; }
	unsigned triggers() const { return m_triggers; }
	unsigned applied() const { return m_applied; } // lane HERO-1: targets that got the power's attribute modifier / levels (RW 0x89763B calls)
	int pauseCount() const { return m_pauseCount; } // lane HERO-1 (tests: UnpauseSpecialPowerUpgrade)
	unsigned long long unportedEffects() const { return m_unported; }
	// lane SPELL-2: objects the trigger's attribute modifier scan reached (RW 0x8977BF calls of vslot 0x34), base do* calls that passed the gate,
	// and the client notices of RW 0x89713F (counted)
	unsigned long long victimsAffected() const { return m_victims; }
	unsigned long long initiates() const { return m_initiates; }
	unsigned long long clientNotices() const { return m_clientNotices; }
	unsigned long long viewObjectsMade() const { return m_viewObjects; } // lane SPELL-2: the reveal objects of RW 0x896FD9
	void crc(StateHasher &h) const override;

protected:
	// RW 0x8980A3 / 0x8980EF / 0x89816C (lane SPELL-2): initiate (the pause / disabled gate, skipped with option 0x40000, and the SpecialPowerUpdate
	// module's initiate) and, unless UpdateModuleStartsAttack, the trigger. `loc` null: doSpecialPower (the object's own position)
	bool baseDo(const Coord3D *loc, const Object *target, unsigned options);
	// RW 0x897987 (lane SPELL-2): the script / EVA notices (counted), the view object (counted, S-920), the recharge unless UpdateModuleStartsAttack,
	// OnTriggerRechargeSpecialPower, the light point cost (S-531), SetModelCondition (lane HERO-1), TriggerFX, then either TheGlobalWeatherSystem
	// (AttributeModifierWeatherBased: the map-wide modifier, ChangeWeather) or the attribute modifier scan around `loc` (RW 0x8977BF)
	void triggerSpecialPower(const Coord3D *loc, const Object *target);
	// RW 0x8977BF: the victim tests (IGNORED_IN_GUI, self, AffectGood / AffectEvil, AffectAllies) then vslot 0x34 for each
	void applyToVictims(const std::vector<Object *> &victims);
	// RW 0x89763B (module vslot 0x34): GiveLevels (counted), the AttributeModifier, the AntiCategory disable until `until` (RW 0x804FCC), AntiFX and
	// AttributeModifierFX. UntamedAllegianceSpecialPower overrides it (RW 0x8CBFCF)
	virtual void applyToVictim(Object &victim, unsigned until, unsigned antiMask);
	// the subclass effect after the base part
	virtual void effectAtLocation(const Coord3D &loc, const Object *target);
	unsigned long long m_unported = 0;
	unsigned long long m_victims = 0;
	unsigned long long m_initiates = 0;
	unsigned long long m_clientNotices = 0;
	unsigned long long m_viewObjects = 0;
	void createViewObject(const Coord3D *loc); // RW 0x896FD9
	// lane HERO-1: RW 0x8980A3's gate and the initiate RW 0x897E87; true when the do* goes on to the trigger (no UpdateModuleStartsAttack)
	bool initiate(Object *target, const Coord3D *loc, unsigned options);

private:
	unsigned now() const;
	bool readyImpl(bool insertSharedTimer) const;
	float percentImpl(bool insertSharedTimer) const;
	unsigned sharedReadyFrame(bool insertSharedTimer) const;
	const SpecialPowerModuleData *m_spData;
	unsigned m_rechargeLength = 0;   // + 0x14
	unsigned m_readyFrame = 0;       // + 0x18
	int m_pauseCount = 0;            // + 0x1C
	unsigned m_pauseFrame = 0;       // + 0x20
	float m_pausedPercent = 0.0f;    // + 0x24
	bool m_updateDisabled = false;   // + 0x28
	unsigned m_triggers = 0;         // casts (a test counter, hashed)
	unsigned m_applied = 0;          // lane HERO-1: RW 0x89763B calls (a test counter, hashed)
};

class PlayerHealSpecialPower : public SpecialPowerModule
{
public:
	PlayerHealSpecialPower(Thing *thing, const PlayerHealSpecialPowerModuleData *data);
	const PlayerHealSpecialPowerModuleData *data() const { return m_data; }

	// RW 0x8CC4B4 / 0x8CC37B: the effect of the power at `pos` (see the file comment); returns the number of objects healed
	int healAt(const Coord3D &pos);
	// the client / unported parts healAt met (FX shown, OCLs to create): counted, never dropped (S-529)
	unsigned long long fxRequests() const { return m_fx; }
	unsigned long long oclRequests() const { return m_ocl; }
	int lastHealed() const { return m_lastHealed; }

protected:
	void effectAtLocation(const Coord3D &loc, const Object *target) override { m_lastHealed = healAt(loc); (void)target; }

private:
	bool healObject(Object &victim);
	const PlayerHealSpecialPowerModuleData *m_data;
	unsigned long long m_fx = 0, m_ocl = 0;
	int m_lastHealed = 0;
};

// OCLSpecialPower (data constructor RW 0x8C752F, table RW 0xC73A40 after the base; module constructor RW 0x8C72E9, interface RW 0xC73890):
// + 0x7C UpgradeOCL (science, OCL) pairs, + 0x88 OCL, + 0x8C CreateLocation (index into RW 0xDB32D8, default 0 CREATE_AT_EDGE_NEAR_SOURCE),
// + 0x90 UpgradeName, + 0x9C NearestSecondaryObjectFilter. doSpecialPowerAtLocation (RW 0x8C75D8): the base part (RW 0x89816C), the OCL is the
// first UpgradeOCL whose science the player owns, else OCL (RW 0x8C7393); CREATE_AT_LOCATION (case 3, RW 0x8C776B) creates it with the module's
// object as the source at the location (RW 0x8C7690 -> OCL::create RW 0x5F00CA); then every UpgradeName the player lacks is granted (RW
// 0x8C76A5 .. 0x8C770A). Lane SPELL-2: doSpecialPower RW 0x8C73EF / AtObject RW 0x8C73D7, CREATE_AT_LOCATION, USE_OWNER_OBJECT (4), CREATE_ABOVE_LOCATION
// (5: z + 300.0f, RW 0xBD9E90) and USE_SECONDARY_OBJECT_LOCATION (8: the closest object the NearestSecondaryObjectFilter allows, RW 0xA39090) run; the map edge
// cases (0, 1, 2, 6) and the army spawn points (7) are counted (S-530).
class OCLSpecialPowerModuleData : public SpecialPowerModuleData
{
public:
	std::vector<std::pair<std::string, std::string>> m_upgradeOCL; // + 0x7C (science name, OCL name)
	std::string m_ocl;                                             // + 0x88
	int m_createLocation = 0;                                      // + 0x8C
	std::vector<std::string> m_upgradeNames;                       // + 0x90
	std::shared_ptr<const ObjectFilter> m_nearestSecondaryObjectFilter; // + 0x9C
	static void buildFieldParse(MultiIniFieldParse &p); // RW 0x8C7B0B
};

class ObjectCreationList;
class OCLSpecialPower : public SpecialPowerModule
{
public:
	OCLSpecialPower(Thing *thing, const OCLSpecialPowerModuleData *data);
	const OCLSpecialPowerModuleData *data() const { return m_data; }
	const std::vector<ObjectID> &created() const { return m_created; }
	ObjectID secondaryObject() const { return m_secondaryUsed; }
	void doSpecialPower(unsigned options) override;                          // RW 0x8C73EF
	void doSpecialPowerAtObject(Object *target, unsigned options) override;  // RW 0x8C73D7
	void doSpecialPowerAtLocation(const Coord3D &loc, unsigned options) override; // RW 0x8C75D8

private:
	const ObjectCreationList *pickOCL() const; // RW 0x8C7393
	void createOCL(const ObjectCreationList *ocl, const Coord3D &where, bool hasSecondary);
	const OCLSpecialPowerModuleData *m_data;
	std::vector<ObjectID> m_created; // the objects of the last cast (tests, the HUD)
	ObjectID m_secondaryUsed = INVALID_ID; // USE_SECONDARY_OBJECT_LOCATION: the object found by the last cast (tests)
};

class GameLogic;
class Player;

namespace SpecialPowerModules
{
// binds the typed data and the runtime classes SpecialPowerModule, PlayerHealSpecialPower and OCLSpecialPower
void registerAll(ModuleFactory &modules);
// RW 0x68C26D Object::getSpecialPowerModule: the first behavior module whose special power template is `t` (null for none)
SpecialPowerModuleInterface *findModule(const Object &obj, const SpecialPowerTemplate *t);
// RW 0x7B1D79 SpecialPowerStore::canUseSpecialPower: the module exists and its requirements are met, the player owns one of the template's
// RequiredSciences (RW 0x6AC22F, an empty list passes), the light point test (RW 0x6AA8FB: no manager in a skirmish, S-531) and none of the
// PreventActivationConditions statuses is on the object (RW 0x75CDC4)
bool canUseSpecialPower(const Object &obj, const SpecialPowerTemplate *t);
// RW 0x68E754 / 0x68E7AC / ... Object::doSpecialPower*: nothing while the object is disabled; unless forced, canUseSpecialPower; then the
// module's do* (the readiness is the ActionManager's test, RW 0x82DFB7: see SpellCommands)
bool doSpecialPowerAtLocation(Object &obj, const SpecialPowerTemplate *t, const Coord3D &loc, unsigned options, bool force);
bool doSpecialPowerAtObject(Object &obj, const SpecialPowerTemplate *t, Object *target, unsigned options, bool force);
bool doSpecialPower(Object &obj, const SpecialPowerTemplate *t, unsigned options, bool force);
// RW 0x8C76A5 .. 0x8C770A (lane SPELL-2): the PLAYER upgrades of `names` go COMPLETE to the object's controlling player (an unknown name ends the list)
void grantPlayerUpgrades(Object &obj, const std::vector<std::string> &names);
// RW 0x6B183D (the player's game start, part): the PlayerTemplate's SpellBookMp (skirmish / multiplayer) or SpellBook object is made on the
// player's default team and its id kept (Player + 0x710); returns it (null: no name, no template, no team)
Object *createSpellBook(GameLogic &logic, Player &player);
// RW 0x6AD0F8 Player::getSpellBookObject: the kept id, else the first SPELL_BOOK object the player controls (cached)
Object *getSpellBookObject(GameLogic &logic, Player &player);
// the same answer without caching the id (display reads)
Object *findSpellBookObject(const GameLogic &logic, const Player &player);
// installs every player's science-added hook (RW 0x6AE1C9 .. 0x6AE2E6: the special power modules of the player's objects whose template's
// RequiredSciences holds the science: onScienceAcquired, then setReadyFrame(now) while GameLogic + 0x114 == 3 (S-525))
void installScienceHooks(GameLogic &logic);
}
