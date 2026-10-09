// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// HeroAbilityModules (lane HERO-2): the hero ability classes HERO-1 left as UnportedModule, ported from the RotWK binary (caveat S-001; ZH has none of
// them: they are BFME classes, the shapes follow SpecialAbilityUpdate's). Most are SpecialAbilityUpdate subclasses that override a few virtual slots of
// SpecialAbilityUpdate's vtable (GameLogic/Module/SpecialAbilityModules.h); each class names its vtable, constructor, field table and every slot it overrides.
//
// TARGET FACTS (each read from the disassembly):
//   * TemporarilyDefectUpdate (vtable RW 0xC761D0, update interface RW 0xC761C4, constructor RW 0x8D0B0D: sleeps forever; field table RW 0xC76214:
//     DefectDuration (+ 0x08, parseDurationUnsignedInt)). Module fields: + 0x20 the end frame, + 0x24 the start frame, + 0x28 the dominator's id,
//     + 0x2C "ended" (set by Object::endDefection RW 0x69ABEE).
//       - startDefection (RW 0x8D0BF3, from Object::defect RW 0x699368): not already running (+ 0x20 == 0), an owner object, not TEMPORARILY_DEFECTED: the
//         dominator's id is kept, the team becomes the dominator's (Object::setTemporaryTeam RW 0x699513); a dominator that is not a SPELL_BOOK (KindOf 123)
//         sets start = now, end = the given frame or now + DefectDuration and wakes the update in 2 * 5 frames (RW global 0xD9F608 = 5); a SPELL_BOOK's
//         defection sleeps forever (no end frame).
//       - update (RW 0x8D0CE0): "ended" set: the fields clear, sleep forever (the local owner's UI refresh, RW 0x8D0D29 .., is the client's); the defection
//         is over (RW 0x8D0C87: the dominator is gone or dead (+ 0x458 bit 0), a SPELL_BOOK dominator whose player RW 0x6AAC4B answers true, or now >= end):
//         the fields clear and, when the object is not contained (+ 0x27C), Object::endDefection (RW 0x69ABA7); sleep forever; else sleep 10 frames.
//   * DominateEnemySpecialPower (SpecialAbilityUpdate subclass, vtable RW 0xC762C8, trigger slot 0x44 RW 0x8D103D; field table RW 0xC76268 after the
//     SpecialAbilityUpdate table: DominateRadius + 0xD0 (real), TriggerFX + 0xD4, DominatedFX + 0xD8 (RW 0x73A302), PermanentlyConvert + 0xDC (bool),
//     AttributeModifierAffects + 0xE0 (ObjectFilter, RW 0x76392F)).
//       - the trigger runs SpecialAbilityUpdate's, then: a target object (+ 0x40) is dominated directly (RW 0x8D0F6C(target, owner, data, 1)); without one
//         ThePartitionManager is asked within DominateRadius of the target location (+ 0x44), distance type 0, near to far, through the chain RW 0xC11DC0 (the
//         owner's ENEMIES, flag 0), RW 0xC10E20 (alive), RW 0xC0F374 (Object + 0x458 bit 3: never set) and the status filter RW 0xC1D6A0 (allow RW 0x661484:
//         not HORDE_MEMBER), every hit dominated with the area flag (0); then TriggerFX at the location (RW 0x494615).
//       - RW 0x8D0F6C(victim, owner, data, direct): not the owner, not the owner's team, AttributeModifierAffects allows it for the owner's player (RW
//         0x7640C1), not UNDER_CONSTRUCTION; a victim whose producer (Object + 0x78) is a HORDE is skipped in the area and replaced by that horde when direct;
//         not AIRBORNE_TARGET (status 6); then Object::defect(owner, PermanentlyConvert) (RW 0x699368) and DominatedFX on it (its drawable's vslot 0x1EC, or
//         RW 0x4B1B5A without a drawable: an object FX event either way).
//   * ActivateModuleSpecialPower (SpecialAbilityUpdate subclass, vtable RW 0xC768B8; slot 5 onObjectCreated RW 0x8D238D, finishAbility slot 0x34 RW 0x8D2370,
//     trigger slot 0x44 RW 0x8D235D; field table RW 0xC769A0: TriggerSpecialPower (RW 0x8D239B: at most 8 entries, more are ignored; a module tag and an
//     optional TARGETPOS (0) / OBJECTPOS (1) compared with strcmp, default OBJECTPOS) into the vector at + 0xD0 of (tag key, mode) pairs).
//       - RW 0x8D222A(deactivate): for every entry, the object's module with the tag (RW 0x68F9DB): this module itself is an error message (RW 0x8D2272);
//         a module with an update interface (its + 0xC table vslot 0x24) is woken next frame, or put to sleep forever when deactivating (update interface
//         vslot 8: setWakeFrame); otherwise, when activating, its special power interface (+ 0xC vslot 0x20) gets doSpecialPowerAtLocation (vslot 0x30) at the
//         object's position (OBJECTPOS) or the ability's target location (+ 0x44) with options 0.
//       - onObjectCreated deactivates, the trigger (after SpecialAbilityUpdate's) activates, finishAbility (after SpecialAbilityUpdate's) deactivates.
//   * ArrowStormUpdate (SpecialAbilityUpdate subclass, vtable RW 0xC63E70, constructor RW 0x894063; startPreparation slot 0x3C RW 0x894396, continuePreparation
//     slot 0x40 RW 0x893CBC, trigger slot 0x44 RW 0x894100; data constructor RW 0x893CCD, field table RW 0xC05168: WeaponTemplate + 0xD0, TargetRadius + 0xD4,
//     ShotsPerTarget + 0xD8 (1), ShotsPerBurst + 0xDC (1), MaxShots + 0xE0 (1), CanShootEmptyGround + 0xE4). Module fields: + 0x88 the target list (ids),
//     + 0x8C the current target, + 0x90 its shots, + 0x94 the shots, + 0x98 done. Legolas' Arrow Wind, Gandalf's Lightning Sword.
//       - startPreparation: SpecialAbilityUpdate's, the fields cleared, then RW 0x894151: ThePartitionManager within TargetRadius of the target location,
//         distance type 0, near to far, through RW 0xC11DC0 (the owner's ENEMIES, flag 0), RW 0xC10E20 (alive), RW 0xC0F374; a hit whose KindOf has none of
//         BASE_FOUNDATION, INERT, IGNORED_IN_GUI, WALL_UPGRADE, UNATTACKABLE, MOVE_ONLY joins the list (a MONSTER also a priority list); then, when the list
//         is not empty, it is refilled by cycling through the priority list (or the list itself when there is no MONSTER) until it holds MaxShots ids.
//       - continuePreparation: done ends it, else SpecialAbilityUpdate's.
//       - trigger: SpecialAbilityUpdate's; without CanShootEmptyGround an empty list is done; else up to ShotsPerBurst shots (RW 0x893EC8), done when one says so:
//         the current target is dropped after ShotsPerTarget shots; the next live id of the list (popped from its front) is the target; with one, a temporary
//         weapon of WeaponTemplate is fired at it (TheWeaponStore->createAndFireTempWeapon RW 0x6CF590) and its shots counted; without one and with
//         CanShootEmptyGround it is fired at a point drawn in the square of TargetRadius around the location (GameLogicRandomValueReal(x - r, x + r)
//         "ArrowStormUpdate.cpp" line 0xD9, then y line 0xDA, z the ground height: RW 0x6CF530), without CanShootEmptyGround it is done; done when the shots
//         reach MaxShots.
//   * CurseSpecialPower (SpecialAbilityUpdate subclass, vtable RW 0xC76480, trigger slot 0x44 RW 0x8D134C; field table RW 0xC76440: TriggerFX + 0xD0,
//     CursedFX + 0xD4 (RW 0x73A302), CursePercentage + 0xD8 (parsePercentToReal)). The trigger runs SpecialAbilityUpdate's, then curses the target object
//     (RW 0x8D12E3) and plays TriggerFX at the location (+ 0x44); without a target object: ThePartitionManager within the special power template's
//     RadiusCursorRadius (+ 0x54) of the location, distance type 0, unsorted, through RW 0xC11DC0 (the owner's ENEMIES), the KindOf filter RW 0x445139 (HERO
//     required) and RW 0xC10E20 (alive); every hit is cursed and, when there was one, TriggerFX plays. RW 0x8D12E3: not the owner, a HERO, ENEMIES to the
//     owner (RW 0x68D7AB), not status 3 / 2 (DESTROYED / UNDER_CONSTRUCTION): every module's + 0xC slot 0xAC with CursePercentage (a SpecialPowerModule's
//     is RW 0x6519AF: its special power interface's startPowerRecharge (vslot 0x3C) with the percentage; the other classes' are no-ops) and CursedFX on it.
//   * FellBeastSwoopPower (SpecialAbilityUpdate subclass, vtable RW 0xC74C10, constructor RW 0x8CB0A8: + 0x88 false; update interface RW 0xC74C04, update RW
//     0x8CB2CE; trigger slot 0x44 RW 0x8CB28B; finishAbility slot 0x34 RW 0x8CB194). The update's first call triggers (+ 0x88 / interface + 0x78 set); the
//     trigger gives the AI command 0x3F (the level attack at the target object) or 0x40 (at the location) with source 2; later calls end the ability (RW
//     0x8530BA) and sleep forever once the AI is idle (vslot 0x1B8) or vslot 0x1D0 answers, else SPECIAL_WEAPON_ONE (+ 0x128 bit 20) follows "below the
//     template's height" (the GeometryInfo copy at + 0xA8: its + 0x14 compared with the height above the terrain) and the update runs next frame; the end
//     clears + 0x12A bit 4 and + 0x88. The AI commands 0x3F / 0x40 are GiantBirdAIUpdate's swoop (AI vslots 0x90 / 0x94): not ported (S-1223, counted).
//   * RousingSpeechUpdate (SpecialAbilityUpdate subclass, vtable RW 0xC6BB08, constructor RW 0x8B05EA; its own update interface RW 0xC6BAF8 (update RW
//     0x8B068D); startPreparation slot 0x3C RW 0x8B0485, continuePreparation slot 0x40 RW 0x8B0BAC (SpecialAbilityUpdate's), trigger slot 0x44 RW 0x8B0A0D;
//     data constructor RW 0x8B0762, field table RW 0xC6BA48: BonusRadius + 0xD0, SpeechDuration + 0xD4, UpdateInterval + 0xD8 (durations), LeaderFX + 0xDC,
//     FollowerFX + 0xE0, CreateWave + 0xE4, WaveWidth + 0xE8, ModifierName + 0xEC (RW 0x42E59E: appended), LevelUp + 0xF8, ObjectFilter + 0xFC). Module fields:
//     + 0x88 the followers (ids), + 0x8C the end frame, + 0x90 started, + 0x94 / + 0x98 the wave's inner / outer radius. Faramir's and the Captain of
//     Dale's speech.
//       - update: the previous followers lose the follower condition (+ 0x125 bit 1, model condition 201) and their leader (+ 0x46C) and the list empties (RW
//         0x8B0591); the first call starts the preparation (vslot 0x3C) and sets end = now + SpeechDuration; while now < end and inner < BonusRadius: with
//         CreateWave inner = outer and outer = min(outer + WaveWidth, BonusRadius); the trigger runs (vslot 0x44); sleep UpdateInterval. Then started = 0,
//         the leader's speech condition (+ 0x125 bit 7, model condition 207) clears, sleep forever.
//       - startPreparation: SpecialAbilityUpdate's, model condition 207, LeaderFX on the object, inner = 0, outer = CreateWave ? min(WaveWidth, BonusRadius) :
//         BonusRadius.
//       - trigger: SpecialAbilityUpdate's, then RW 0x8B07EA: ThePartitionManager within outer of the object, distance type 0, near to far, through RW 0xC11DC0
//         (the object's ALLIES, mask 4), the AllowFilter RW 0xBE4CC8 (ObjectFilter for the object's player, flag 1), RW 0xC10E20 (alive), RW 0xC0F374 and
//         RW 0xC6B9D4 (allow RW 0x8B0420: the 3D length of (candidate - centre) above inner); every hit but the object joins the followers; then every
//         ModifierName is added to every follower (RW 0x68F1A8(name, -1)), which gets model condition 201, the leader's id at + 0x46C and FollowerFX; with
//         LevelUp each follower gains a level (RW 0x79DA0A(1, 1, 0)) and so does each member of its contain (vslot 0x110 with RW 0x8B0402).
//   * TeleportSpecialAbilityUpdate (SpecialAbilityUpdate subclass, vtable RW 0xC64930; trigger slot 0x44 RW 0x8964A0, startPacking slot 0x54 RW 0x895C6B
//     (SpecialAbilityUpdate's with success), startUnpacking slot 0x58 RW 0x89653A; field table RW 0xC054B0: BusyForDuration + 0xD0, DestinationWeaponName
//     + 0xD4, SourceWeaponName + 0xD8, MaxDistance + 0xDC). Shelob's Tunnel, Karsh's Blink.
//       - startUnpacking: SourceWeaponName's temporary weapon fired at the object's position (RW 0x6CF530), SpecialAbilityUpdate's unpacking, then
//         IGNORE_AI_COMMAND until now + BusyForDuration (RW 0x6907BD).
//       - trigger: the orientation becomes its own + the angle to the target location (RW 0x4B3D8D, x87 fadd), the object is placed at the location (RW
//         0x696E63(location, 1): the teleport form) and turned (RW 0x70C31E); DestinationWeaponName's temporary weapon fired at the location; then
//         SpecialAbilityUpdate's trigger. MaxDistance is not read by the module (INFERENCE: the command's cursor, client).
//   * SpecialPowerTimerRefreshSpecialPower (a SpecialPowerModule, vtable RW 0xC0610C; the SpecialPowerModule data): its victim slot 0x34 (RW 0x8C7B26)
//     runs SpecialPowerModule's (RW 0x89763B) and then every module of the victim gets its + 0xC slot 0xB0 (RW 0x68BDF9): a SpecialPowerModule's is
//     refreshTimer (RW 0x6519C2 -> 0x896F99), the other classes' are no-ops. Elrond's Restoration.
// INFERENCE (stop S-1222, stopLines): the module tags are kept as strings and resolved with NameKeyGenerator::findKey when used (retail makes the keys while
// parsing: an unknown tag finds no module either way); RW 0x7A7483's team-name lookup of the defection end is the recorded Team.

#pragma once

#include "GameLogic/Module/DamageModule.h"
#include "GameLogic/Module/DieModule.h"
#include "GameLogic/Module/MoneyEventModules.h"
#include "GameLogic/Module/SpecialAbilityModules.h"
#include "GameLogic/Module/SpecialPowerModules.h"
#include "GameLogic/Module/UpdateModule.h"

#include <array>
#include <memory>
#include <string>
#include <utility>
#include <vector>

class ModuleFactory;
class StateHasher;
struct ObjectFilter;
class CommandButton;

// ShareExperienceBehavior (vtable RW 0xC5EE6C, constructor RW 0x88314A: sleeps forever; data RW 0x8830ED, field table RW 0xC5EF30: Radius + 0x08,
// DropOff + 0x0C, Percentage + 0x10 (reals; Percentage 1.0 by default), ObjectFilter + 0x14 (RW 0x76392F)). The object's experience listener: RW 0x88305B
// (the first module whose interface slot 0xA4 answers, the module + 0x20 interface RW 0xC5ED9C) is called by Object's kill experience (RW 0x695589) with the
// experience the object just gained (above 0). share (RW 0x8832F0, value):
//   * nothing when Percentage is not above 0 or the object is BLOODTHIRSTY (status 65, RW 0x44DDEC(0x41));
//   * ThePartitionManager within Radius of the object's position, distance type 0, ITER_FASTEST, through RW 0xC1D660 (not the object), RW 0xC10E20 (alive),
//     RW 0xC11DC0 (the object's ALLIES), RW 0xC0F374 (Object + 0x458 bit 3: never set) and the ObjectFilter for the object's controlling player (RW 0xBE4CC8);
//   * for each hit in that order: a HERO object skips a HERO candidate when TheGameLogic + 0x9F is 0 (INFERENCE: RW 0x62D299 sets the byte to 1 when a game
//     starts, so a game's heroes share with heroes); a candidate whose ExperienceTracker accepts experience (RW 0x79D322) makes value := fstp(value *
//     Percentage), then value := fstp(dropOff * value) and, above 0, ExperienceTracker::addExperiencePoints(value, true, true, true, false) (RW 0x79D833).
//     The value is NOT restored per candidate: each one shares the previous one's share (retail's register reuse, [ebp + 8]);
//   * the drop off (RW 0x88325B): DropOff == 1.0: max(0, 1 - |candidate - position| / Radius) with the length RW 0x4054F5 (x87 z*z + y*y + x*x, CRT sqrt,
//     fstp) and SSE subtract / divide; any other DropOff: 1.0.
class ShareExperienceBehaviorModuleData : public ModuleData
{
public:
	float m_radius = 0.0f;                          // + 0x08
	float m_dropOff = 0.0f;                         // + 0x0C
	float m_percentage = 1.0f;                      // + 0x10
	std::shared_ptr<const ObjectFilter> m_filter;   // + 0x14
	static void buildFieldParse(MultiIniFieldParse &p);
};

class ShareExperienceBehavior : public UpdateModule
{
public:
	ShareExperienceBehavior(Thing *thing, const ShareExperienceBehaviorModuleData *data); // RW 0x88314A
	UpdateSleepTime update() override { return UPDATE_SLEEP_FOREVER; }
	void share(float value); // RW 0x8832F0
	static ShareExperienceBehavior *of(Object &obj); // RW 0x88305B
	unsigned long long shares() const { return m_shares; }

private:
	float dropOffFactor(const Coord3D &from, const Object &candidate) const; // RW 0x88325B
	const ShareExperienceBehaviorModuleData *m_data;
	unsigned long long m_shares = 0; // diagnostics (not state)
};

// DamageFilteredCreateObjectDie (create RW 0x64C684, constructor RW 0x889222; vtables RW 0xC6134C, die RW 0xC61288 (module + 0x10), damage RW 0xC6127C
// (module + 0x14); data RW 0x8891DD: the DieMux table (extra 8) and RW 0xC613D8: CreationList + 0x38 (RW 0x73A368), DamageTypeTriggersInstantly + 0x3C and
// DamageTypeTriggersForDuration + 0x40 (parseIndexList over the damage sub types RW 0xDAF5CC NORMAL / BECOME_UNDEAD / SELF / BECOME_UNDEAD_ONCE; default 4,
// none), PostFilterTriggeredDuration + 0x44 (RW 0x73A429, frames)). Module fields: + 0x18 the frame of the last hit of the duration type, + 0x1C that hit's
// player index (-1).
//   * onDamage (RW 0x88939A): a hit whose DamageInfo sub type (D + 0x18) is DamageTypeTriggersForDuration records now and its source's controlling player's
//     index; onHealing (slot 1, RW 0x889191) clears the frame; onBodyDamageStateChange is RW 0x918BA0 (nothing);
//   * onDie (RW 0x8892B0): nothing unless the DieMux applies (RW 0x85FED5); a death by DamageTypeTriggersInstantly: the source object (found by id), when it
//     passes RW 0x889141 (a sub type 3 hit only once per 5 frames per source: the source's + 0x450 frame), primes the CreationList (RW 0x5F00CA: the source as
//     primary, the dead object's position); the dead object is then destroyed (RW 0x62BBAB) whether or not anything was made; another death within
//     PostFilterTriggeredDuration of the last duration-type hit: the recorded player (ThePlayerList RW 0x6A844E), not defeated (RW 0x6AAC4B), with a spell book
//     object (RW 0x6AD0F8) creates the list with that object as primary and the object is destroyed; after the duration the player is forgotten (-1).
class DamageFilteredCreateObjectDieModuleData : public ModuleData
{
public:
	DieMuxData m_dieMux;                 // RW 0xC76BD8
	std::string m_creationList;          // + 0x38 (empty: None)
	int m_instantType = 4;               // + 0x3C
	int m_durationType = 4;              // + 0x40
	unsigned m_postFilterDuration = 0;   // + 0x44 (frames)
	static void buildFieldParse(MultiIniFieldParse &p);
};

class DamageFilteredCreateObjectDie : public BehaviorModule, public DieModuleInterface, public DamageModuleInterface
{
public:
	DamageFilteredCreateObjectDie(Thing *thing, const DamageFilteredCreateObjectDieModuleData *data) : BehaviorModule(thing, data), m_data(data) {}
	DieModuleInterface *getDie() override { return this; }
	DamageModuleInterface *getDamage() override { return this; }
	void onDie(const DieModuleInterface::Event &event) override;            // RW 0x8892B0
	void onDamage(const DamageInfo &info) override;                         // RW 0x88939A
	void onHealing(const DamageInfo &) override { m_hitFrame = 0; }        // RW 0x889191
	void onBodyDamageStateChange(BodyDamageType, BodyDamageType) override {} // RW 0x918BA0
	void crc(StateHasher &h) const override;
	unsigned created() const { return m_created; }

private:
	void create(Object *primary);
	const DamageFilteredCreateObjectDieModuleData *m_data;
	unsigned m_hitFrame = 0;  // + 0x18
	int m_hitPlayer = -1;     // + 0x1C
	unsigned m_created = 0;   // diagnostics
};

// AutoAbilityBehavior (create RW 0x64AC9A, constructor RW 0x85D7FD; vtables RW 0xC57F8C, update RW 0xC57F80 (module + 0x10); data RW 0x85D355, field table
// RW 0xC57EC0: SpecialAbility + 0x18 (a SpecialPower name), MaxScanRange + 0x08, MinScanRange + 0x0C, WorkingRadius + 0x10, IdleTimeSeconds + 0x14 (reals),
// ForbiddenStatus + 0x1C (RW 0x7B1E5C), Query + 0x2C (RW 0x85D29C: the next free of 6 slots of (count + 0, ObjectFilter + 4); count -1 = unused; a seventh
// is INIException 1 "iniParseQuery: Too many queries"), StartsActive + 0x5C, BaseMaxRangeFromStartPos + 0x5D, AdjustAttackMeleePosition + 0x5E, AllowSelf
// + 0x5F (default Yes)). Module fields: + 0x20 the name of the auto button, + 0x24 the start position, + 0x30 "start position kept".
//   * constructor: StartsActive: the object's command set's first SPECIAL_POWER (24) button of SpecialAbility's power is set (RW 0x85D76C); else sleep forever;
//   * set (RW 0x85D76C, button; also MSG_DO_AUTO_ABILITY, RW 0x77B9BA: the object's SPECIAL_POWER button of the message's power, given to the module whose
//     SpecialAbility is empty or that power, RW 0x85D6AD): a button with AutoAbility (+ 0x10C) other than the current one becomes the auto button and the
//     update wakes (RW 0x85D48F); any other: the auto button is cleared and the update sleeps forever (the toggle);
//   * update (RW 0x85D9D1): the auto button by name; none, the object dead (+ 0x458 bit 0), any of the button's DisableOnModelCondition (+ 0x1E0), a set
//     EnableOnModelCondition (+ 0x194) none of whose bits the object has, MOUNTED_ONLY unmounted or UNMOUNTED_ONLY mounted (MOUNTED, model condition 214):
//     the button is cleared, sleep forever. With an AI and IdleTimeSeconds > 0, an AI that is not idle (slot 0x1B8) or is in a temporary state (RW
//     0x66306E): sleep cvttss2si(5 * IdleTimeSeconds). Not able to act (RW 0x85D5EA): sleep 6. Else the delay is 50 frames, or cvttss2si(5 * AutoDelay
//     (+ 0x134)) when that is above 0; a TriggerWhenReady (+ 0x12C) button runs at once (RW 0x696FD2(button, 2, 0)); otherwise the scan range is the vision
//     range (RW 0x68E43B), MaxScanRange when above 0, else PresetRange (+ 0x130) when above 0; the centre the position (the first position seen with
//     BaseMaxRangeFromStartPos); per used Query slot in order: ThePartitionManager within the range of the centre, distance type 0, near to far, chain
//     RW 0xC10E20 (alive), RW 0xC0F374, RW 0xC1D66C (Object::canSee(candidate, range), RW 0x68FA3D), not UNATTACKABLE / INSIDE_GARRISON (status 60 / 58,
//     RW 0x794311 -> 0x76A28D), not KindOf UNATTACKABLE (54, RW 0x445139), the query's ObjectFilter for the controlling player; at least `count` hits: the
//     target is the first hit (RW 0x85D4B5) that is not the object (unless AllowSelf), not above 0.8 of its health for a NeedDamagedTarget (+ 0x138) button
//     and at least MinScanRange away (Coord3D::length, when MinScanRange > 0); no slot used: RW 0x701443(object, range, 0x4A) (stop S-1227: the AI's closest
//     enemy, INFERENCE); no target: sleep 6. A target: the AI's + 0x48 := 2 (not identified, not written: S-1227), the auto sound event (RW
//     0x85D930, client unless NO_PLAY_UNIT_SPECIFIC_SOUND_FOR_AUTO_ABILITY), then NEED_TARGET_POS: RW 0x6979D9(button, target position, 2, 1); any NEED_TARGET_*
//     _OBJECT: RW 0x697890(button, target, 2, 1); else RW 0x696FD2(button, 2, 1); sleep the delay;
//   * able to act (RW 0x85D5EA): a ForbiddenStatus bit the object has: no; the button is valid (RW 0x75CC3E: its power's module fully ready, or a NEED_UPGRADE
//     upgrade the object can take and does not have), in the object's command set with its power's module (RW 0x692A75), not STUNNED (128), STUNNED_FLAILING
//     (127), STUNNED_STANDING_UP (163), INVISIBLE_STEALTH (546, RW 0x68FC06), none of AutoAbilityDisallowedOnModelCondition (+ 0x13C), not IGNORE_AI_COMMAND
//     (status 74), the AI's + 0x34 zero (INFERENCE: read as 0);
//   * the command (RW 0x696FD2 / 0x697890 / 0x6979D9 for the SPECIAL_POWER (24) and SPELL_BOOK (38) buttons): Object::doSpecialPower* of the button's power with
//     the button's options | 0x40000 (| AUTO_ABILITY_TRIGGERED 0x20000000 from this module). The other command types are counted (S-1227).
class AutoAbilityBehaviorModuleData : public ModuleData
{
public:
	struct Query
	{
		int count = -1;                               // + 0
		std::shared_ptr<const ObjectFilter> filter;   // + 4
	};
	float m_maxScanRange = 0.0f;       // + 0x08
	float m_minScanRange = 0.0f;       // + 0x0C
	float m_workingRadius = 0.0f;      // + 0x10 (not read by the update)
	float m_idleTimeSeconds = 0.0f;    // + 0x14
	std::string m_specialAbility;      // + 0x18
	ObjectStatusMaskType m_forbiddenStatus{}; // + 0x1C
	std::array<Query, 6> m_queries{};  // + 0x2C
	bool m_startsActive = false;       // + 0x5C
	bool m_baseMaxRangeFromStartPos = false; // + 0x5D
	bool m_adjustAttackMeleePosition = false; // + 0x5E
	bool m_allowSelf = true;           // + 0x5F
	static void buildFieldParse(MultiIniFieldParse &p);
};

class AutoAbilityBehavior : public UpdateModule
{
public:
	AutoAbilityBehavior(Thing *thing, const AutoAbilityBehaviorModuleData *data); // RW 0x85D7FD
	UpdateSleepTime update() override;                                           // RW 0x85D9D1
	void onObjectCreated() override;                                              // the StartsActive part of the constructor (needs the command set)
	void crc(StateHasher &h) const override;
	void setAutoButton(const CommandButton *button);                             // RW 0x85D76C
	const std::string &autoButton() const { return m_button; }
	const AutoAbilityBehaviorModuleData *data() const { return m_data; }
	// RW 0x85D6AD: the module of `obj` whose SpecialAbility is empty or `power`
	static AutoAbilityBehavior *forPower(Object &obj, const std::string &power);
	struct Stats
	{
		unsigned long long casts = 0, unportedCommands = 0, closestEnemyQueries = 0, meleeAdjust = 0;
	};
	static Stats &stats();

private:
	void clearButton(); // RW 0x85D481
	bool canAct(const CommandButton &button) const; // RW 0x85D5EA
	Object *pickTarget(const std::vector<Object *> &hits, const CommandButton &button) const; // RW 0x85D4B5
	void doCommand(const CommandButton &button, Object *target, const Coord3D *pos, bool fromAuto); // RW 0x696FD2 / 0x697890 / 0x6979D9
	const AutoAbilityBehaviorModuleData *m_data;
	std::string m_button;           // + 0x20
	Coord3D m_startPos{};           // + 0x24
	bool m_haveStartPos = false;    // + 0x30
};

// WeaponModeSpecialPowerUpdate (create RW 0x64D9C2, constructor RW 0x8982D5 over the update + special power base RW 0x991660; vtables RW 0xC65188, update
// RW 0xC6517C (+ 0x10), special power update RW 0xC65158 (+ 0x20), special power RW 0xC650F0 (+ 0x24); data RW 0x8984C3: the base table RW 0xC87588
// (SpecialPowerTemplate + 0x08 RW 0x73B22F, InitiateSound + 0x0C, StartsPaused + 0x14) and RW 0xC650A0: AttributeModifier + 0x18, Duration + 0x1C (RW 0x73A429,
// frames), LockWeaponSlot + 0x20 (RW 0x42E9B7 over RW 0xC16928; 5 = none), WeaponSetFlags + 0x24 (RW 0x6C9951)). Module fields: the base timer + 0x28 the
// ready frame, + 0x2C the pause count, + 0x30 the pause frame, + 0x34 the paused percent; + 0x38 "active".
//   * the base (RW 0x991660): a template that is not SharedSynced starts its recharge (RW 0x991500(1.0)); StartsPaused pauses (RW 0x99116C(1)); sleeps forever;
//   * the base special power: isReady (RW 0x99135D: the player's shared frame for a SharedSynced template, else not paused and the ready frame reached);
//     getPercentReady (RW 0x9913BC: ready 1; paused: the paused percent; no template 0; else 1 - fstp((unsigned)(ready - now)) / (unsigned)ReloadTime, x87);
//     getReadyFrame (RW 0x991473); pause (RW 0x99116C, as SpecialPowerModule's); startPowerRecharge (RW 0x991500: shared: the player's frame restarts (RW
//     0x6AD1B0); else reload = ftol((unsigned)ReloadTime * fstp(the player's recharge discount + 1) * the RECHARGE attribute modifier product (RW 0x68C82D,
//     1.0 here as SpecialPowerModule's, S-529)); below 1.0: nothing when the percent is 0, else ready = now + ftol((1 - max(0, percent - p)) * reload); else
//     now + reload); do* (RW 0x9911FB / 0x991231 / 0x991269): not paused and not disabled (RW 0x9325B4): the special power update's initiate (slot 0);
//   * initiate (RW 0x89841E): LockWeaponSlot none: every WeaponSetFlags bit on (RW 0x691059, when the object has a weapon set RW 0x68FDF7); else the slot is
//     locked (RW 0x69121A(slot, 2): LOCKED_PERMANENTLY); the AttributeModifier for Duration (RW 0x68F1A8); the update wakes in Duration; active; the recharge
//     starts (slot 0x3C, 1.0); the firing tracker resets (RW 0x8E302A(1): COMBAT-1's, not called, S-1228);
//   * update (RW 0x8983B5): the PERMANENT weapon lock is released (RW 0x68DF11(2)), every WeaponSetFlags bit off (RW 0x691106), the modifier removed (RW
//     0x68F259), not active, the countdown unpaused (slot 0x24(0)); sleeps forever.
class WeaponModeSpecialPowerUpdateModuleData : public ModuleData
{
public:
	const SpecialPowerTemplate *m_specialPowerTemplate = nullptr; // + 0x08
	std::string m_specialPowerTemplateName;
	std::string m_initiateSound;      // + 0x0C
	bool m_startsPaused = false;      // + 0x14
	std::string m_attributeModifier;  // + 0x18
	unsigned m_duration = 0;          // + 0x1C (frames)
	int m_lockWeaponSlot = 5;         // + 0x20
	std::array<std::uint32_t, 4> m_weaponSetFlags{}; // + 0x24
	static void buildFieldParse(MultiIniFieldParse &p);
};

class WeaponModeSpecialPowerUpdate : public UpdateModule, public SpecialPowerModuleInterface
{
public:
	WeaponModeSpecialPowerUpdate(Thing *thing, const WeaponModeSpecialPowerUpdateModuleData *data); // RW 0x8982D5
	SpecialPowerModuleInterface *getSpecialPower() override { return this; }
	UpdateSleepTime update() override; // RW 0x8983B5
	void crc(StateHasher &h) const override;
	bool active() const { return m_active; }

	const SpecialPowerTemplate *getSpecialPowerTemplate() const override { return m_data->m_specialPowerTemplate; }
	bool isReady() const override { return readyImpl(true); }                          // RW 0x99135D
	float getPercentReady() const override { return percentImpl(true); }               // RW 0x9913BC
	unsigned getReadyFrame() const override;                                           // RW 0x991473
	void onScienceAcquired() override {}                                               // slot 0x1C: RW 0x63F3BF
	void setReadyFrame(unsigned frame) override { m_readyFrame = frame; }              // RW 0x993440
	void startPowerRecharge(float percentOfCurrent) override;                          // RW 0x991500
	bool requirementsMet() const override { return true; }                             // the base has no RequirementsFilter
	bool isReadyForDisplay() const override { return readyImpl(false); }
	float getPercentReadyForDisplay() const override { return percentImpl(false); }
	void doSpecialPower(unsigned options) override;                                    // RW 0x9911FB
	void doSpecialPowerAtObject(Object *target, unsigned options) override;            // RW 0x991231
	void doSpecialPowerAtLocation(const Coord3D &loc, unsigned options) override;      // RW 0x991269
	void pauseCountdown(bool pause) override;                                          // RW 0x99116C

private:
	bool readyImpl(bool insert) const;
	float percentImpl(bool insert) const;
	unsigned now() const;
	void initiate(); // RW 0x89841E
	const WeaponModeSpecialPowerUpdateModuleData *m_data;
	unsigned m_readyFrame = 0;   // + 0x28
	int m_pauseCount = 0;        // + 0x2C
	unsigned m_pauseFrame = 0;   // + 0x30
	float m_pausedPercent = 0.0f; // + 0x34
	bool m_active = false;       // + 0x38
};

// DualWeaponBehavior (create RW 0x64AD23, constructor RW 0x85DEC9: wakes next frame; vtables RW 0xC57FD4, update RW 0xC57FC8 (+ 0x10); data RW 0x85DE99,
// field table RW 0xC58088: SwitchWeaponOnCloseRangeDistance + 0x08 (real), UseCloseRangeWhileMounted + 0x0C, MinimumSwitchTime + 0x10 (RW 0x73A429, frames),
// UseHordeRangeWeapon + 0x14, UseRealVictimRange + 0x15 (bools)). Module fields: + 0x20 a "close range" request (no writer found: 0), + 0x24 the last
// switch frame. update (RW 0x85DF88): no AI or dead: sleep forever; the weapon set flag RAMPAGE (8): 5 frames; contained by a non-HORDE: 5; UseHordeRange
// Weapon in a HORDE: the current CLOSE_RANGE state is kept (set again when set, cleared when clear); USING_SPECIAL_ABILITY (model condition 207): 5; the
// request: close; else the victim within SwitchWeaponOnCloseRangeDistance: with UseRealVictimRange the AI's goal object when its squared 3D distance (SSE,
// z, y, x) is within the distance squared, else RW 0x701443(object, distance, 0x62) (INFERENCE, S-1228: the closest visible alive enemy); a victim whose
// height difference (x87 fabs) is above 0.5 * the distance is none; UseCloseRangeWhileMounted and MOUNTED (214): close; then MinimumSwitchTime -
// now + the last switch above 0: sleep that; the last switch := now; a live victim: close, else far. Close: CLOSE_RANGE (weapon set flag 7) on and the
// model condition WEAPONSTATE_CLOSE_RANGE (160) on; far: both off (RW 0x691059 / 0x691106, Object + 0x120 bit 0, RW 0x68B53C); sleep 5.
class DualWeaponBehaviorModuleData : public ModuleData
{
public:
	float m_switchDistance = 0.0f;          // + 0x08
	bool m_useCloseRangeWhileMounted = false; // + 0x0C
	unsigned m_minimumSwitchTime = 0;       // + 0x10
	bool m_useHordeRangeWeapon = false;     // + 0x14
	bool m_useRealVictimRange = false;      // + 0x15
	static void buildFieldParse(MultiIniFieldParse &p);
};

class DualWeaponBehavior : public UpdateModule
{
public:
	DualWeaponBehavior(Thing *thing, const DualWeaponBehaviorModuleData *data); // RW 0x85DEC9
	UpdateSleepTime update() override;                                         // RW 0x85DF88
	void crc(StateHasher &h) const override;
	unsigned switches() const { return m_switches; }

private:
	UpdateSleepTime setClose(bool close);
	const DualWeaponBehaviorModuleData *m_data;
	bool m_closeRequest = false;  // + 0x20
	unsigned m_lastSwitch = 0;    // + 0x24
	unsigned m_switches = 0;      // diagnostics
};

class TemporarilyDefectUpdateModuleData : public ModuleData
{
public:
	unsigned m_defectDuration = 0; // + 0x08 (frames)
	static void buildFieldParse(MultiIniFieldParse &p);
};

class TemporarilyDefectUpdate : public UpdateModule
{
public:
	TemporarilyDefectUpdate(Thing *thing, const TemporarilyDefectUpdateModuleData *data); // RW 0x8D0B0D
	UpdateSleepTime update() override;                                                    // RW 0x8D0CE0
	void crc(StateHasher &h) const override;
	// RW 0x8D0BF3: `untilFrame` 0: now + DefectDuration
	void startDefection(Object *dominator, unsigned untilFrame);
	// RW 0x69ABEE (Object::endDefection): + 0x2C = 1
	void friend_markEnded() { m_ended = true; }
	static TemporarilyDefectUpdate *of(Object &obj); // RW 0x68BDA5("TemporarilyDefectUpdate")
	unsigned endFrame() const { return m_endFrame; }
	ObjectID dominator() const { return m_dominator; }

private:
	bool defectionOver();                    // RW 0x8D0C87
	const TemporarilyDefectUpdateModuleData *m_data;
	unsigned m_endFrame = 0;                 // + 0x20
	unsigned m_startFrame = 0;               // + 0x24
	ObjectID m_dominator = INVALID_ID;       // + 0x28
	bool m_ended = false;                    // + 0x2C
};

class DominateEnemySpecialPowerModuleData : public SpecialAbilityUpdateModuleData
{
public:
	float m_dominateRadius = 0.0f;                         // + 0xD0
	std::string m_triggerFX;                               // + 0xD4
	std::string m_dominatedFX;                             // + 0xD8
	bool m_permanentlyConvert = false;                     // + 0xDC
	std::shared_ptr<const ObjectFilter> m_affects;         // + 0xE0
	static void buildFieldParse(MultiIniFieldParse &p);
};

class DominateEnemySpecialPower : public SpecialAbilityUpdate
{
public:
	DominateEnemySpecialPower(Thing *thing, const DominateEnemySpecialPowerModuleData *data);
	unsigned dominated() const { return m_dominated; }
	void crc(StateHasher &h) const override;

protected:
	void triggerAbilityEffect() override; // RW 0x8D103D

private:
	void dominate(Object *victim, bool direct); // RW 0x8D0F6C
	const DominateEnemySpecialPowerModuleData *m_de;
	unsigned m_dominated = 0;
};

class ActivateModuleSpecialPowerModuleData : public SpecialAbilityUpdateModuleData
{
public:
	struct Entry
	{
		std::string tag;      // the key of + 0xD0's pair (INFERENCE: resolved when used)
		int mode = 1;         // 0 TARGETPOS, 1 OBJECTPOS
	};
	std::vector<Entry> m_triggers; // + 0xD0 .. + 0xD8
	static void buildFieldParse(MultiIniFieldParse &p);
};

class ActivateModuleSpecialPower : public SpecialAbilityUpdate
{
public:
	ActivateModuleSpecialPower(Thing *thing, const ActivateModuleSpecialPowerModuleData *data);
	void onObjectCreated() override;                            // RW 0x8D238D
	void finishAbility(bool ownerDying, bool aborted) override; // RW 0x8D2370
	unsigned activations() const { return m_activations; }
	void crc(StateHasher &h) const override;

protected:
	void triggerAbilityEffect() override; // RW 0x8D235D

private:
	void setModulesActive(bool deactivate); // RW 0x8D222A
	const ActivateModuleSpecialPowerModuleData *m_am;
	unsigned m_activations = 0;
};

class ArrowStormUpdateModuleData : public SpecialAbilityUpdateModuleData
{
public:
	std::string m_weaponTemplate;       // + 0xD0
	float m_targetRadius = 0.0f;        // + 0xD4
	int m_shotsPerTarget = 1;           // + 0xD8
	int m_shotsPerBurst = 1;            // + 0xDC
	int m_maxShots = 1;                 // + 0xE0
	bool m_canShootEmptyGround = false; // + 0xE4
	static void buildFieldParse(MultiIniFieldParse &p);
};

class ArrowStormUpdate : public SpecialAbilityUpdate
{
public:
	ArrowStormUpdate(Thing *thing, const ArrowStormUpdateModuleData *data); // RW 0x894063
	unsigned shots() const { return (unsigned)m_shots; }
	size_t targetsQueued() const { return m_targets.size(); }
	void crc(StateHasher &h) const override;

protected:
	void startPreparation() override;      // RW 0x894396
	bool continuePreparation() override;   // RW 0x893CBC
	void triggerAbilityEffect() override;  // RW 0x894100

private:
	void gatherTargets();                  // RW 0x894151
	bool shootOne();                       // RW 0x893EC8: true when done
	const ArrowStormUpdateModuleData *m_as;
	std::vector<ObjectID> m_targets;       // + 0x88 (a list: popped at the front, appended at the back)
	ObjectID m_current = INVALID_ID;       // + 0x8C
	int m_shotsAtCurrent = 0;              // + 0x90
	int m_shots = 0;                       // + 0x94
	bool m_done = false;                   // + 0x98
};

class CurseSpecialPowerModuleData : public SpecialAbilityUpdateModuleData
{
public:
	std::string m_triggerFX;       // + 0xD0
	std::string m_cursedFX;        // + 0xD4
	float m_cursePercentage = 0.0f; // + 0xD8
	static void buildFieldParse(MultiIniFieldParse &p);
};

class CurseSpecialPower : public SpecialAbilityUpdate
{
public:
	CurseSpecialPower(Thing *thing, const CurseSpecialPowerModuleData *data);
	unsigned cursed() const { return m_cursed; }
	void crc(StateHasher &h) const override;

protected:
	void triggerAbilityEffect() override; // RW 0x8D134C

private:
	void curse(Object &victim);           // RW 0x8D12E3
	const CurseSpecialPowerModuleData *m_cs;
	unsigned m_cursed = 0;
};

class FellBeastSwoopPower : public SpecialAbilityUpdate
{
public:
	FellBeastSwoopPower(Thing *thing, const SpecialAbilityUpdateModuleData *data); // RW 0x8CB0A8
	UpdateSleepTime update() override;                          // RW 0x8CB2CE
	void finishAbility(bool ownerDying, bool aborted) override; // RW 0x8CB194
	unsigned swoopCommands() const { return m_commands; }
	void crc(StateHasher &h) const override;

protected:
	void triggerAbilityEffect() override; // RW 0x8CB28B

private:
	bool m_started = false; // + 0x88
	unsigned m_commands = 0;
};

class RousingSpeechUpdateModuleData : public SpecialAbilityUpdateModuleData
{
public:
	float m_bonusRadius = 0.0f;                     // + 0xD0
	unsigned m_speechDuration = 0;                  // + 0xD4
	unsigned m_updateInterval = 0;                  // + 0xD8
	std::string m_leaderFX;                         // + 0xDC
	std::string m_followerFX;                       // + 0xE0
	bool m_createWave = false;                      // + 0xE4
	float m_waveWidth = 0.0f;                       // + 0xE8
	std::vector<std::string> m_modifierNames;       // + 0xEC
	bool m_levelUp = false;                         // + 0xF8
	std::shared_ptr<const ObjectFilter> m_filter;   // + 0xFC
	static void buildFieldParse(MultiIniFieldParse &p);
};

class RousingSpeechUpdate : public SpecialAbilityUpdate
{
public:
	RousingSpeechUpdate(Thing *thing, const RousingSpeechUpdateModuleData *data); // RW 0x8B05EA
	UpdateSleepTime update() override;                                            // RW 0x8B068D
	unsigned followersInspired() const { return m_inspired; }
	void crc(StateHasher &h) const override;

protected:
	void startPreparation() override;     // RW 0x8B0485
	void triggerAbilityEffect() override; // RW 0x8B0A0D

private:
	void releaseFollowers();              // RW 0x8B0591
	void gatherFollowers();               // RW 0x8B07EA
	const RousingSpeechUpdateModuleData *m_rs;
	std::vector<ObjectID> m_followers;    // + 0x88
	unsigned m_endFrame = 0;              // + 0x8C
	bool m_started = false;               // + 0x90
	float m_inner = 0.0f;                 // + 0x94
	float m_outer = 0.0f;                 // + 0x98
	unsigned m_inspired = 0;
};

class TeleportSpecialAbilityUpdateModuleData : public SpecialAbilityUpdateModuleData
{
public:
	unsigned m_busyForFrames = 0;           // + 0xD0
	std::string m_destinationWeapon;        // + 0xD4
	std::string m_sourceWeapon;             // + 0xD8
	float m_maxDistance = 0.0f;             // + 0xDC
	static void buildFieldParse(MultiIniFieldParse &p);
};

class TeleportSpecialAbilityUpdate : public SpecialAbilityUpdate
{
public:
	TeleportSpecialAbilityUpdate(Thing *thing, const TeleportSpecialAbilityUpdateModuleData *data);
	unsigned teleports() const { return m_teleports; }
	void crc(StateHasher &h) const override;

protected:
	void triggerAbilityEffect() override; // RW 0x8964A0
	void startPacking(bool success) override; // RW 0x895C6B
	void startUnpacking() override;       // RW 0x89653A

private:
	void fireTempWeapon(const std::string &name, const Coord3D &at);
	const TeleportSpecialAbilityUpdateModuleData *m_tp;
	unsigned m_teleports = 0;
};

class SpecialPowerTimerRefreshSpecialPower : public SpecialPowerModule
{
public:
	SpecialPowerTimerRefreshSpecialPower(Thing *thing, const SpecialPowerModuleData *data) : SpecialPowerModule(thing, data) {}
	unsigned refreshed() const { return m_refreshed; }
	void crc(StateHasher &h) const override;

protected:
	void applyToVictim(Object &victim, unsigned until, unsigned antiMask) override; // RW 0x8C7B26

private:
	unsigned m_refreshed = 0;
};

class GameLogicDispatch;

namespace HeroAbilityModules
{
void registerAll(ModuleFactory &modules);
// MSG_DO_AUTO_ABILITY (1112, RW 0x77B9BA) { int power id, object id }: the object's SPECIAL_POWER button of that power toggles its AutoAbilityBehavior
void registerHandlers(GameLogicDispatch &d);
std::vector<std::string> stopLines();
}
