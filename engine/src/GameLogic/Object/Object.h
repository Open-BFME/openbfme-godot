// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// Object: a live game object (ZH Include/GameLogic/Object.h, Source/GameLogic/Object/Object.cpp), lane LOGIC-1.
//
// CREATION ORDER (spec ini-and-object-model.md 5.3), ported from the RotWK Object constructor RW 0x69990F (caveat S-001) and newObject RW
// 0x6D165E; every step below carries the address it was read at.
//   ThingFactory::newObject(tt, team, status, id)   RW 0x6D165E
//     1. BuildVariations: when tt+0x330 (the BuildVariations list) is non-empty, GameLogicRandomValue(0, n - 1) (RW 0x6D16A7, line
//        0x20B of the retail source file) picks one entry and its template replaces tt when the name resolves (0x6D1305);
//     2. Object::Object (RW 0x625841 -> 0x69990F):
//        a. the final override of the template; the status mask is stored (RW 0x6CFD47 at +0x94); the creation frame (+0x4E0);
//        b. the id: the given one, or the next from GameLogic (RW 0x625833, 0x68BC01 sets it);
//        c. the team: the given one, or the neutral player's default team (RW 0x69954A);
//        d. the HELPER modules, in this order (the ModuleTag_ strings are at RW 0xC12340 .. 0xC12290; verified from the binary, stop
//           S-141 keeps only what the helpers DO):
//             SMCHelper (0x24 bytes, RW 0x69300C)            always
//             RecoveryHelper (0x20, 0x68D04E)                always
//             RepulsorHelper (0x20, 0x68D109)                when AIData EnableRepulsors and KindOf CAN_BE_REPULSED (tt+0x10D bit 5)
//             DefectionHelper (0x30, 0x68CFAA)               unless KindOf SHRUBBERY (tt+0x108 & 0x40), ROCK (tt+0x114 & 2) or
//                                                            ROCK_VENDOR (tt+0x119 & 1)
//             GuardingHelper (0x28, 0x8E37C2)                always
//             WeaponStatusHelper (0x20, 0x68D19A)            when canPossiblyHaveAnyWeapon (RW 0x73C191)
//             FiringTrackerHelper (0x5C, 0x8E2EB2)           when canPossiblyHaveAnyWeapon
//        e. the behavior modules in template list order (ModuleFactory::newModule, RW 0x656480); after each: body (vslot 0), contain
//           (vslot 2) and AI (vslot 19) are cached (RW 0x69A3B3); a PhysicsBehavior / StealthUpdate name key test follows (RW 0x69A420);
//        f. the experience tracker (RW 0x69A606, not ported: stop S-142);
//        g. onObjectCreated on every module (RW 0x69A637 `call [eax + 0x14]`);
//        h. the radar (RW 0x6D9042, not ported), GameLogic::registerObject (RW 0x62BA8B), then partition registration (not ported);
//     3. CreateModule::onCreate in module order (RW 0x6D16E6 .. 0x6D1709);
//     4. Object::initObject (RW 0x693D0C): sendObjectCreated (RW 0x628882: the unconditional creation draw GetGameLogicRandomValue(1, 999,
//        "GameLogic.cpp", 0x19A7), the Drawable made and bound, OnCreated: GameLogic::sendObjectCreated, which is lane LUA-1's
//        LuaScriptEngine::sendObjectCreated once installed with GameLogic::setObjectCreatedProc), the special power mask, the upgrade
//        module refresh, ... (not ported: stop S-142).
//
// Not ported (stop S-142, the lanes that need them port them): the experience tracker, the radar and partition manager, weapons and
// the weapon set, the special power mask, upgrades, the AI update interface, physics, stealth, the object's geometry and bounding
// data, the position-in-map bookkeeping of the pathfinder.
//
// Ownership: an Object owns its modules (helpers first, then the behaviors, in the retail array order). The scheduler holds raw
// UpdateModule pointers; destroying an object goes through GameLogic::destroyObject and processDestroyList, which removes them first.

#pragma once

#include "Common/Thing/Thing.h"
#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/Module/DieModule.h"
#include "GameLogic/ObjectTypes.h"

#include "GameLogic/ArmorSet.h"
#include "GameLogic/BitFlags.h"
#include "Common/Player.h"
#include "Common/Upgrade.h"

#include <array>
#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <vector>

class ExperienceTracker;
class GameLogic;
class ObjectClientHooks;
class ObjectWeapons;
struct DamageInfo;
class Team;
class Player;
class StateHasher;
class ThingTemplate;

class Object : public Thing
{
public:
	// RW 0x69990F. Use ThingFactory::newObject: it makes the object and runs the rest of the creation order.
	Object(GameLogic &logic, const ThingTemplate *tt, const ObjectStatusMaskType &status, Team *team, ObjectID id);
	~Object() override;
	Object(const Object &) = delete;
	Object &operator=(const Object &) = delete;

	Object *asObject() override { return this; }

	ObjectID getID() const { return m_id; }
	GameLogic &logic() const { return m_logic; }

	// ---- team and owner ---------------------------------------------------------------------------------------------------
	Team *getTeam() const { return m_team; }
	// ZH Object::setTeam / setOrRestoreTeam (the member lists of both teams are kept). Once the object exists, a change of controlling player runs the owner
	// change of RW 0x696F0A (lane ECON-1): the command points move from the old owner to the new one (RW 0x6914B7, unless the object is TEMPORARILY_DEFECTED)
	// and every module's onCapture(oldPlayer, newPlayer) runs
	void setTeam(Team *team);
	// lane SCRIPT-3: RW 0x69A550 / 0x697D1A: the AI's attitude from the team prototype's teamAggressiveness
	void applyTeamAttitude();
	Player *getControllingPlayer() const;
	// ---- lane HERO-2: defection (Wormtongue / Saruman / Sharku / Rogash's Dominate, a TemporarilyDefectUpdate) ----------------------------------------
	// RW 0x699368 Object::defect(newOwner, permanent): see Object.cpp
	void defect(Object *newOwner, bool permanent);
	// RW 0x699513: TEMPORARILY_DEFECTED on, then the team without recording it as the original (RW 0x698E6F -> 0x697C09(team, 0))
	void setTemporaryTeam(Team *team);
	// RW 0x69AB22: back to the team setTeam last recorded (RW 0x69959D keeps its name at + 0x320; the port keeps the Team, INFERENCE: the name lookup RW
	// 0x7A7483 finds the same team)
	void restoreOriginalTeam();
	// RW 0x69ABA7: the end of a temporary defection (TemporarilyDefectUpdate, a lethal hit on a RespawnBody)
	void endDefection();
	Team *getOriginalTeam() const { return m_originalTeam; }
	// lane HERO-2: + 0x80, the object capturing this one (SpecialAbilityUpdate's capture: RW 0x851F9C sets it, RW 0x8531CC / 0x854740 clear it; Lua's
	// ObjectCapturingObjectPlayerSide reads it, RW 0x73684D)
	void setCapturerID(ObjectID id) { m_capturerID = id; }
	ObjectID getCapturerID() const { return m_capturerID; }
	// lane HERO-2, RW 0x889141: a BECOME_UNDEAD_ONCE (sub type 3) kill by this object primes a DamageFilteredCreateObjectDie at most once per 5 frames (+ 0x450)
	bool friend_allowUndeadKill(int damageSubType, unsigned now);
	// lane HERO-2: RW 0x6996DC (a capture's team change): an object that is not contained, not UNDER_CONSTRUCTION nor SOLD and of another team takes `team`
	// (setTeam RW 0x69954A); its AI goes idle; see Object.cpp
	void setCapturedTeam(Team *team);
	// lane HERO-2: + 0x46C, the leader of a rousing speech this object follows (RousingSpeechUpdate RW 0x8B0AAE / 0x8B05D1); its other readers are not located
	void setSpeechLeader(ObjectID id) { m_speechLeader = id; }
	ObjectID getSpeechLeader() const { return m_speechLeader; }
	// lane CAMP-1H: + 0x45C, the object receives the difficulty bonus. RW 0x68B907: a change is stored and, with a controlling player, handed to it
	// (Player::applyDifficultyBonusesForObject RW 0x6AC32D, which acts only when the flag turns on); set by initObject (RW 0x693D63, when the script
	// engine's + 0x1A5D5 says so) and OBJECT_ALLOW_BONUSES (RW 0x7BD730)
	void setReceivingDifficultyBonus(bool receive);
	bool isReceivingDifficultyBonus() const { return m_receivingDifficultyBonus; }

	// ---- lane ECON-1: command points, the price paid, build completion, death ------------------------------------------------------
	// RW 0x68E0C2 / 0x68E114: the object's command points join / leave its player's pool once (the `counted` byte, RW + 0x4A0); nothing counts while the object is
	// under construction (status 2) or pending construction (status 87), or when its template has neither CommandPoints nor CommandPointBonus
	void addToPlayerCommandPoints();
	void removeFromPlayerCommandPoints();
	bool isCountedInCommandPoints() const { return m_commandPointsCounted; }
	// lane BUILD-1: RW + 0x288 the construction percent: -1 (CONSTRUCTION_COMPLETE, RW 0xBD19DC) for a finished object, 0 .. 100 while the foundation rises
	// (RW 0x858924 sets 0.0 when a foundation makes its building; GettingBuiltBehavior RW 0x857E77 advances it; 0x858A7A resets it to -1 for an instant build)
	float getConstructionPercent() const { return m_constructionPercent; }
	void setConstructionPercent(float percent) { m_constructionPercent = percent; }
	// RW 0x62684D(2, on)
	bool isUnderConstruction() const { return testStatus(OBJECT_STATUS_UNDER_CONSTRUCTION); }
	// RW + 0x33C: what the owner paid for this object (a float; ProductionUpdate stores the entry's cost, RW 0x8A1B9F); RefundDie refunds a share of it
	float getBuildCostPaid() const { return m_buildCostPaid; }
	// lane UPGRADE-1: RW + 0x340, the money queued OBJECT upgrades cost (ProductionUpdate::queueUpgrade RW 0x8A10C8 adds to it)
	float getUpgradeCostPaid() const { return m_upgradeCostPaid; }
	void addUpgradeCostPaid(float cost);
	void setBuildCostPaid(float cost) { m_buildCostPaid = cost; }
	// RW 0x68D252: every create module's onBuildComplete, in module order; run after an object that is complete at birth was made and placed (the map loop,
	// production, a finished construction)
	void friend_onBuildComplete();
	// RW 0x698F06 (the part lane ECON-1 ports): every die module's onDie, then the object's command points leave the player's pool (RW 0x698FD9). The death pipeline
	// of the combat lane calls this; GameLogic::destroyObject does not (retail's does not either)
	void friend_onDie(const DieModuleInterface::Event &event);

	// ---- lane COMBAT-1: weapons, damage, death, the kill credit --------------------------------------------------------------------
	// the live weapon set (null when the template cannot have any weapon, RW 0x73C191)
	ObjectWeapons *getWeapons() const { return m_weapons.get(); }
	// lane HUD-4: the object's weapon set flags (RW Object + 0x38C, kept on EVERY object: a horde without weapons carries WEAPONSET_TOGGLE_1 for its
	// MonitorConditionUpdate). An object with a weapon set keeps them in ObjectWeapons (the set re-selects on a change); one without keeps them here
	const WeaponConditionFlags &getWeaponSetFlags() const;
	// RW 0x691059 / 0x691106 Object::setWeaponSetFlag / clearWeaponSetFlag (GameLogic/WeaponSetToggle.cpp): the flag, the weapon set update, the mapped model
	// condition and the SWAPPING_TO_WEAPONSET_n special model condition
	void setWeaponSetFlag(int bit, bool on);
	bool hasAnyWeapon() const;
	// ZH Object::getRelationship: this object's team toward the other's (NEUTRAL when either has no team)
	Relationship getRelationship(const Object &other) const;
	// RW 0x698E7D Object::attemptDamage: a hit with a delay waits in the pending list (RW obj + 0x460), a hit made while this object runs its AI waits one frame (delay = 1.0);
	// else RW 0x697E50 doAttemptDamage: the body's attemptDamage when this object is not dead, then (lane COMBAT-4) the shockwave handler RW 0x6968BC when the object
	// is still alive or the hit carries a shockwave amount
	void attemptDamage(DamageInfo &info);
	// RW 0x697E50 (the immediate half of attemptDamage, also the pending list's)
	void doAttemptDamage(DamageInfo &info);
	// lane RADAR-1: the logic frame of the last hit the drawable's damage notice (RW 0x67B4B7, from RW 0x6968BC) would report to the radar as an attack (0xFFFFFFFF:
	// none); client state the radar reads through the snapshot (Radar::noteAttacks), never hashed
	UnsignedInt radarAttackFrame() const { return m_radarAttackFrame; }
	// RW 0x690532 Object::attemptHealing: a HEALING hit from `source` (INVALID_ID: itself)
	void attemptHealing(float amount, const Object *source);
	// lane BUILD-2: RW 0x690584 Object::attemptHealingFromSoleBenefactor(amount, source, duration): only one healer at a time (RW + 0x3D4 its id, + 0x3D8 the frame its claim
	// ends): the heal applies when the claim ran out (expiry < now), the source is the current benefactor, or the source is a SWARM_DOZER (template word + 0x108 bit 15);
	// a non-swarm source then takes the claim for `duration` frames. The hit is DAMAGE_HEALING / DEATH_NONE from the source (RW body slot 1). false: refused / no source
	bool attemptHealingFromSoleBenefactor(float amount, const Object *source, UnsignedInt duration);
	ObjectID soleHealingBenefactor() const { return m_soleHealingBenefactorID; }
	UnsignedInt soleHealingBenefactorExpiry() const { return m_soleHealingBenefactorExpiry; }
	// RW 0x697EB6 Object::updatePendingDamage: the pending hits' delays count down one per frame, a hit whose delay went below 0 applies (list order)
	void updatePendingDamage();
	size_t pendingDamageCount() const;
	// ZH Object::kill: UNRESISTABLE damage that kills (the body's health goes to 0 through the normal pipeline)
	void kill(int deathType);
	// RW 0x6955BC Object::scoreTheKill(victim, scoreType): the kill credit walks up PASS_EXPERIENCE_TO_PRODUCER producers, scores each victim once, pays the bounty
	// (Economy::awardBounty), gives the victim's player its own-guys-die skill points and the killer (or its contained / container objects) the experience
	void scoreTheKill(Object &victim, int scoreType = 1);
	// ---- lane XP-1: experience and attribute modifiers -------------------------------------------------------------------------
	// RW Object + 0x26C (made by the constructor, RW 0x69A606; null only for an object whose constructor did not finish)
	ExperienceTracker *getExperienceTracker() const { return m_experienceTracker.get(); }
	// RW 0x695475: the experience this object earns for a kill (`flag`: even an ally's value; `scale` multiplies it)
	void grantExperienceForKill(Object &victim, bool flag, float scale);
	// RW 0x68F1A8: a ModifierList by name (duration < 0: the list's own); a horde passes it to its HordeContain, everything else to its AttributeModifierPoolUpdate
	bool addAttributeModifier(const std::string &listName, int duration);
	// RW 0x68F259 (lane INTEG-1): a ModifierList by name leaves the object; a horde passes it to its HordeContain (slot 0x1DC), everything else to its pool
	void removeAttributeModifier(const std::string &listName);
	// RW 0x68C818 / 0x68C82D: the attribute queries of the object's pool (false when it has none or no active list answers)
	bool attributeModifierSum(int type, const char *name, float &out) const;
	bool attributeModifierProduct(int type, const char *name, bool innate, float &out) const;
	// RW 0x693A1A: this object when it is a HORDE, else its container when that is a HORDE, else (alsoProducer) its producer when that is a HORDE
	Object *getHordeObject(bool alsoProducer) const;
	// RW + 0x49C: the "gained a veterancy level" flags the kills collect (RW 0x695582)
	std::uint8_t experienceFlags() const { return m_experienceFlags; }
	bool hasBeenScored() const { return m_scored; }
	// the death has begun (the die modules ran): the object is no longer a target, its AI is dead and it left the pathfinder (RW 0x698F06)
	bool isEffectivelyDead() const { return m_effectivelyDead; }
	// ZH Object::setEffectivelyDead (RW 0x68D950): InactiveBody's constructor marks its object dead from the start
	void friend_setEffectivelyDead(bool dead) { m_effectivelyDead = dead; }
	// ArmorSet conditions of the object (RW body + 0xF0): the armour the body uses is the best ArmorTemplateSet for them
	ArmorSetFlags armorSetFlags() const { return m_armorSetFlags; }
	void setArmorSetFlag(int bit, bool on);
	// the weapon bonus conditions (RW Object + 0x39C: veteran levels, horde bonus, ...): bit i is TheWeaponBonusConditionNames[i]
	std::uint32_t weaponBonusConditionMask() const { return m_weaponBonusMask; }
	void setWeaponBonusCondition(int bit, bool on);

	// ---- status, kind, disabled -------------------------------------------------------------------------------------------
	const ObjectStatusMaskType &getStatusBits() const { return m_status; }
	bool testStatus(unsigned bit) const { return MaskTest(m_status, bit); }
	void setStatus(unsigned bit, bool set)
	{
		const bool was = testStatus(bit);
		MaskSet(m_status, bit, set);
		if (was != set && bit == OBJECT_STATUS_UNDER_CONSTRUCTION)
		{
			onConstructionStatusChanged();
		}
	}
	// VIS-1: RW 0x68C18F (the shroud record is marked dirty, forced) when the construction status changes (the clearing range follows it, RW 0x68E4E2)
	void onConstructionStatusChanged();
	bool isDestroyed() const { return testStatus(OBJECT_STATUS_DESTROYED); }
	const KindOfMaskType &getKindOf() const { return m_kindOf; }
	bool isKindOf(unsigned kindOfBit) const { return MaskTest(m_kindOf, kindOfBit); }
	// the template's KindOf by the binary's own name table (-1 when the name is not in it); for callers that do not hold a bit number
	bool isKindOfName(const char *name) const;
	DisabledMaskType getDisabledMask() const { return m_disabled; }
	// ZH Object::setDisabled / clearDisabled (the timed form: the type is cleared by checkDisabledStatus when `untilFrame` passes)
	void setDisabled(unsigned type, UnsignedInt untilFrame);
	void clearDisabled(unsigned type);
	// lane SCRIPT-2: the script status byte (RW Object + 0x457, ZH ObjectScriptStatusBit: 1 SCRIPT_DISABLED, 2 SCRIPT_UNPOWERED, 4 SCRIPT_UNSELLABLE,
	// 8 SCRIPT_UNSTEALTHED, 0x10 SCRIPT_TARGETABLE). RW 0x69317D: a change of bit 1 / bit 2 sets or clears the disabled type 9 (DISABLED_SCRIPT_DISABLED) /
	// 10 (DISABLED_SCRIPT_UNDERPOWERED) forever
	void setScriptStatus(std::uint8_t bits, bool set);
	std::uint8_t scriptStatus() const { return m_scriptStatus; }
	// lane SCRIPT-2: RW Object + 0x454 (set by RW 0x68BE3C, the script's "Selectable" panel flag); the client's isSelectable also needs it
	void setScriptSelectable(bool on) { m_scriptSelectable = on; }
	bool isScriptSelectable() const { return m_scriptSelectable; }

	// lane SCRIPT-2: the trigger areas the object is in (RW 0x69264D, from reactToTransformChange when the position changed). Entries at + 0x3DC
	// (trigger, + 4 entered, + 5 exited, + 6 inside; the count + 0x45A, at most 7: "***WARNING: Too many nested triggers"), the last change frame
	// + 0x414, the last integer position + 0x418. `trigger` indexes the script engine's map trigger list. The queries (RW 0x68DD46 / 0x68DD73 /
	// 0x68DDB2 / 0x68BAE0) are the script conditions'
	struct TriggerEntry
	{
		int trigger = -1;
		bool entered = false;
		bool exited = false;
		bool inside = false;
	};
	static constexpr int MAX_TRIGGERS_IN = 7;
	void updateTriggerAreaFlags();
	// lane HUD-5: GeometryInfo::setActive(name, flag) on the object's own geometry (Object + 0xA8, RW 0xAD3520): every shape of the template's list whose name
	// equals `name` exactly takes `on` as its active flag; the pathfinder's view of the object (ObjectPathfindAdapter::getGeometry) uses the object's flags from
	// then on. geometryActive() is empty until the first call; geometryVersion() counts the calls that changed a flag.
	void setGeometryActive(const std::string &name, bool on);
	const std::vector<std::uint8_t> &geometryActive() const { return m_geometryActive; }
	std::uint32_t geometryVersion() const { return m_geometryVersion; }
	int triggerCount() const { return m_triggerCount; }
	const TriggerEntry &triggerEntry(int i) const { return m_triggers[i]; }
	UnsignedInt enteredOrExitedFrame() const { return m_enteredOrExitedFrame; }

	// ---- model condition (lane PROD-1) -------------------------------------------------------------------------------------
	// TARGET RW Object + 0x10C: the object's own model condition flags (0x4C bytes = BitFlags<591>); every change is pushed to the drawable
	// (RW 0x68B53C). clearAndSet is RW 0x68D607 (the clear flags first, then the set flags: ZH Object::clearAndSetModelConditionFlags).
	// The type is the 19 word array GameLogic/BitFlags.h calls ModelConditionFlags (Common/ModelState.h's BitFlags<591> has the same words but cannot be
	// included next to that header); a bit number is an index into the binary's name table (Common/ModelConditionNames.inc).
	typedef std::array<std::uint32_t, 19> ModelConditionBits;
	const ModelConditionBits &getModelConditionBits() const { return m_modelCondition; }
	bool testModelCondition(int bit) const { return bit >= 0 && bit < 19 * 32 && ((m_modelCondition[(size_t)bit >> 5] >> (bit & 31)) & 1u) != 0; }
	void clearAndSetModelConditionFlags(const ModelConditionBits &clear, const ModelConditionBits &set);
	// lane ANIM-1, RW 0x67449C(0) on the object's drawable: Drawable::replaceModelConditionFlags (RW 0x679512) only stores the flags and marks the drawable
	// dirty (+0x443); the draw modules see them when the dirty flags are flushed: once per logic frame in updateDrawable (RW 0x6759C4, on the first client
	// frame of the frame, RW 0x63252F), or at once where the logic calls this (the pre-fire of RW 0x69213E). Client-only: nothing in the logic changes.
	void flushDrawableModelConditions();
	void setModelConditionState(int bit, bool on);
	// SMCHelper (RW 0x8E2C0F, GameLogic/Module/SMCHelper.h): the bit is on for `frames` logic frames (a later expiry of the same bit wins)
	void setSpecialModelConditionState(int bit, UnsignedInt frames);
	// lane RENDER-2 (review r1): what the drawable's launch-bone query (RW 0x6756A1, DrawableLaunchBones) reads besides the template data, held here so the
	// logic owns and hashes it: the instance scale (the template's Scale, RW 0x679FD7, default 1.0; a map object's own scale) and the model condition bits
	// a map object's placement gives the drawable (MapObjectDrawable::flags). The drawable copies both (DrawableManager::applyPlacement).
	float getInstanceScale() const { return m_instanceScale; }
	void setInstanceScale(float scale) { m_instanceScale = scale; }
	const ModelConditionBits &getPlacementConditionBits() const { return m_placementConditions; }
	void setPlacementConditionBits(const ModelConditionBits &bits) { m_placementConditions = bits; }

	// ---- upgrades (lane UPGRADE-1) and the command set (lane PROD-1) ---------------------------------------------------------------------
	// RW 0x68E040 (through 0x691421): the bit of the upgrade in the object's mask (RW + 0x28C). RW first asks the module at +0x258 (slot 0x7C, then its slot
	// 0xB0); that query is not ported (stop S-483)
	bool hasUpgrade(const UpgradeTemplate *upgrade) const { return upgrade && m_upgradeMask.test((unsigned)upgrade->getMaskBit()); }
	// RW 0x69388B: giveUpgradeSelf, then a horde contain passes the object's whole mask to its members (HordeContain slot 0x174, RW 0x87566B: for every upgrade
	// of the mask and every member that is affectedByUpgrade, member->giveUpgrade). Production, Lua and the tests grant through this
	void giveUpgrade(const UpgradeTemplate *upgrade);
	// RW 0x693817: the upgrade's bits (its sub upgrades' when it has any), then updateUpgradeModules
	void giveUpgradeSelf(const UpgradeTemplate *upgrade);
	// RW 0x694914: an owner, the gate (the controlling player has a completed object its RequiredObjectFilter allows, RW 0x6ABD0B; the other two clauses of the
	// gate, RW 0x6939DF / 0x693A1A and RW 0x6ABDC2, are not ported: S-486), then some upgrade module would upgrade with (player's completed | object's | the
	// upgrade's bit), or (contain) one of the contained objects is affected
	bool affectedByUpgrade(const UpgradeTemplate *upgrade) const;
	// RW 0x691438: the upgrade's own bit clears; every upgrade module whose resetUpgrade(that bit) succeeds is removed (RW 0x8D2688)
	void removeUpgrade(const UpgradeTemplate *upgrade);
	// RW 0x6936FE: the controlling player's completed upgrades | the object's | its castle's (not ported: S-483); attemptUpgrade on every mux that did not
	// execute, then postUpgradeCheck on every mux
	void updateUpgradeModules();
	const UpgradeMaskType &getUpgradeMask() const { return m_upgradeMask; }
	// lane HERO-1: RW 0x68CC6C on Object + 0x28C: the bits of `m` join the mask (no upgrade module runs; a revived hero's upgrades, RW 0x78158C)
	void friend_orUpgradeMask(const UpgradeMaskType &m) { m_upgradeMask.orWith(m); }
	// by name through TheUpgradeCenter (std::logic_error when none is installed; see Player::resolveUpgrade for a name that is not an upgrade)
	bool hasUpgrade(const std::string &upgradeName) const { return hasUpgrade(Player::resolveUpgrade(upgradeName, false)); }
	void giveUpgrade(const std::string &upgradeName) { giveUpgrade(Player::resolveUpgrade(upgradeName, true)); }
	// RW 0x69156B: the first non-empty of the three override strings (Object + 0x438 / 0x440 / 0x43C, written by CommandSetUpgrade-like modules), else the
	// template's CommandSet (field @112)
	std::string getCommandSetName() const;
	void setCommandSetOverride(const std::string &name) { m_commandSetOverride = name; }
	const std::string &getCommandSetOverride() const { return m_commandSetOverride; }

	// ---- modules ----------------------------------------------------------------------------------------------------------
	const std::vector<std::unique_ptr<BehaviorModule>> &modules() const { return m_modules; }
	size_t helperCount() const { return m_helperCount; }
	BodyModuleInterface *getBodyModule() const { return m_body; }
	ContainModuleInterface *getContain() const { return m_contain; }
	AIUpdateInterface *getAIUpdateInterface() const { return m_ai; }
	BehaviorModule *findModule(const std::string &className) const; // the first module of this class (helpers included), or null
	// RW 0x68BB14: the first module with an exit interface (array order); lane PROD-1
	ExitInterface *getObjectExitInterface() const;
	// RW 0x68C327: the ProductionUpdate's queue interface, or null
	ProductionUpdateInterface *getProductionUpdate() const;
	BehaviorModule *findModuleByTag(NameKeyType tagKey) const;
	// lane HORDE-2 (RW 0x68C0C5, the formation swap): `replacement` takes the place of `old` in the module list and becomes the cached contain when it is one; the old module
	// is returned (the caller destroys it after the scheduler let go of it)
	std::unique_ptr<BehaviorModule> friend_replaceModule(BehaviorModule *old, std::unique_ptr<BehaviorModule> replacement);

	// ---- container (HordeContain members, passengers) ---------------------------------------------------------------------
	Object *getContainedBy() const { return m_containedBy; }
	void friend_setContainedBy(Object *container) { m_containedBy = container; }
	// lane GARRISON-1: RW + 0x474, the object is in the world (RW 0x68E31F sets it, RW 0x68C18F clears it; a contain takes a rider out of the world and puts it back)
	bool isInWorld() const { return m_inWorld; }
	void friend_setInWorld(bool on) { m_inWorld = on; }
	// lane GARRISON-1: RW + 0x284, the frame the object was put in its container (RW 0x6901AE; 0 when not contained, RW 0x69024C)
	UnsignedInt getContainedFrame() const { return m_containedFrame; }
	void friend_setContainedFrame(UnsignedInt frame) { m_containedFrame = frame; }
	// lane GARRISON-1: the drawable's hidden flag the logic sets (RW 0x6718FB Drawable::setDrawableHidden, called with the object's drawable by the contains:
	// a rider of an ENCLOSED container, RW 0x865D3D / 0x87BF36). The client reads it (LogicSnapshot) and draws nothing while it is set
	bool isDrawableHidden() const { return m_drawableHidden; }
	void setDrawableHidden(bool on) { m_drawableHidden = on; }
	ObjectID getProducerID() const { return m_producerID; }
	void setProducer(const Object *producer) { m_producerID = producer ? producer->getID() : (ObjectID)INVALID_ID; }
	// lane BUILD-2: RW + 0x7C the builder (RW 0x68B6B6 setBuilder: the object's id, 0 for none; ZH Object::setBuilder): the dozer or the spawned worker that builds
	// or repairs this structure, or the structure itself while it builds itself (GettingBuiltBehavior RW 0x8566DF)
	ObjectID getBuilderID() const { return m_builderID; }
	void setBuilder(const Object *builder) { m_builderID = builder ? builder->getID() : (ObjectID)INVALID_ID; }

	// the value of the creation draw (RW 0x628892: GetGameLogicRandomValue(1, 999)), the drawable's seed; 0 until initObject ran
	int getCreationSeed() const { return m_creationSeed; }

	// ---- the map's name for the object (the dict key objectName) -----------------------------------------------------------
	const std::string &getName() const { return m_name; }
	// lane SCRIPT-1: the script engine's named object cache learns every name (ZH ScriptEngine::addObjectToCache; RotWK names objects through it, RW 0x759467)
	void setName(const std::string &name);

	// ---- drawable ---------------------------------------------------------------------------------------------------------
	// lane SMOOTH-1: the client of this object (null when no client layer exists); the simulation reaches the drawable only through it
	ObjectClientHooks *clientHooks() const { return m_client; }
	// ZH Object::friend_bindToDrawable (called by the client hook that made the drawable): calls onDrawableBoundToObject on every module.
	// SMOOTH-1: the object keeps the client hooks, not the drawable
	void friend_bindToClient(ObjectClientHooks *client);

	// ---- the object list (GameLogic keeps it; spec 5.4: append at the tail) -----------------------------------------------
	Object *getNextObject() const { return m_next; }
	Object *getPrevObject() const { return m_prev; }
	// ZH Object::isInList(Object **pListHead)
	bool isInList(Object *const *listHead) const { return m_next != nullptr || m_prev != nullptr || *listHead == this; }

	// ---- the recorded transform (client interpolation data) ----------------------------------------------------------------
	// TARGET RW 0x6260E1 (the twin of B1 Object::bfmeRecordTransform / Drawable::bfmeRecordTransform): GameLogic phase 2 calls it for every
	// object whose recorded frame is not the current frame (RW 0x62E95F .. 0x62E974). The previous position is the recorded transform's
	// translation when a transform was recorded before (+0x1A4), else the current one; the current transform becomes the recorded one.
	void recordTransform(UnsignedInt frame);
	UnsignedInt getRecordedFrame() const { return m_recordedFrame; }
	bool hasRecordedTransform() const { return m_recordedValid; }
	const Coord3D &getRecordedPosition() const { return m_recordedPosition; }
	float getRecordedOrientation() const { return m_recordedAngle; }
	const float *getRecordedBasis() const { return m_recordedBasis; } ///< row major, as Thing::getBasis (read by the client's RenderInterpolation)
	const Coord3D &getPreviousPosition() const { return m_previousPosition; }

	// ---- scheduler support ------------------------------------------------------------------------------------------------
	UnsignedInt getCreationFrame() const { return m_creationFrame; }
	// phase 1 end-of-frame checks (RW 0x690A42 checkDisabledStatus, 0x690AB9 checkIgnoreAICommandStatus, 0x690AE5 checkNoCollisionsStatus)
	void checkDisabledStatus(UnsignedInt frame);
	void checkIgnoreAICommandStatus(UnsignedInt frame);
	void checkNoCollisionsStatus(UnsignedInt frame);
	void setIgnoreAICommandUntil(UnsignedInt frame) { m_ignoreAIExpire = frame; }
	void setNoCollisionsUntil(UnsignedInt frame) { m_noCollisionsExpire = frame; }

	// ---- creation / destruction steps (called by ThingFactory::newObject and GameLogic) -----------------------------------
	void friend_runCreateModules();   ///< step 3
	void friend_initObject();         ///< step 4
	void friend_onDestroy();          ///< ZH Object::onDestroy: leave the container, onDelete on every module (spec 5.5 step 6)
	void friend_setListLinks(Object *prev, Object *next)
	{
		m_prev = prev;
		m_next = next;
	}
	void friend_setNext(Object *next) { m_next = next; }
	void friend_setPrev(Object *prev) { m_prev = prev; }
	// the team's intrusive member list (Team keeps its head)
	Object *friend_teamNext() const { return m_teamNext; }
	Object *friend_teamPrev() const { return m_teamPrev; }
	void friend_setTeamLinks(Object *prev, Object *next)
	{
		m_teamPrev = prev;
		m_teamNext = next;
	}

	// the OpenBFME state hash of the object (id, template, team, status, disabled, transform, modules)
	void crc(StateHasher &hasher) const;

protected:
	// ZH Object::setPosition / setOrientation: a container (the horde contain) moves what it holds
	void reactToTransformChange(const Coord3D *oldPos, float oldAngle) override;

private:
	friend struct XpTestAccess; // lane XP-1 tests: isolated state hash mutations
	void buildModules();
	void joinTeam(Team *team);
	// RW 0x696F0A
	void onOwnerChanged(Player *oldOwner, Player *newOwner);

	GameLogic &m_logic;
	ObjectID m_id = INVALID_ID;
	Team *m_team = nullptr;
	ObjectStatusMaskType m_status{};
	KindOfMaskType m_kindOf{};
	DisabledMaskType m_disabled = DISABLEDMASK_NONE;
	UnsignedInt m_disabledExpire[DISABLED_TYPE_COUNT] = {};
	UnsignedInt m_ignoreAIExpire = 0;
	WeaponConditionFlags m_weaponlessSetFlags{}; ///< lane HUD-4: RW + 0x38C of an object without ObjectWeapons
	UnsignedInt m_noCollisionsExpire = 0;
	UnsignedInt m_creationFrame = 0;
	int m_creationSeed = 0;
	std::string m_name;
	std::uint8_t m_scriptStatus = 0; ///< lane SCRIPT-2: RW + 0x457 (hashed when not zero)
	bool m_scriptSelectable = true;  ///< lane SCRIPT-2: RW + 0x454 (hashed when cleared)
	TriggerEntry m_triggers[MAX_TRIGGERS_IN];  ///< lane SCRIPT-2: RW + 0x3DC
	int m_triggerCount = 0;                      ///< RW + 0x45A
	std::vector<std::uint8_t> m_geometryActive; ///< lane HUD-5: the shapes' active flags of the object's own GeometryInfo (+ 0xA8), empty: the template's
	std::uint32_t m_geometryVersion = 0;
	UnsignedInt m_enteredOrExitedFrame = 0;      ///< RW + 0x414
	std::int32_t m_triggerCellX = 0, m_triggerCellY = 0; ///< RW + 0x418 / + 0x41C (the integer position of the last check)

	std::vector<std::unique_ptr<BehaviorModule>> m_modules; ///< helpers first, then the template's behaviors
	size_t m_helperCount = 0;
	BodyModuleInterface *m_body = nullptr;
	ContainModuleInterface *m_contain = nullptr;
	AIUpdateInterface *m_ai = nullptr;

	std::unique_ptr<ObjectWeapons> m_weapons;               ///< COMBAT-1
	std::unique_ptr<ExperienceTracker> m_experienceTracker; ///< XP-1: RW + 0x26C
	bool m_scored = false;                                  ///< XP-1: RW + 0x456 (scoreTheKill ran for this victim)
	std::uint8_t m_experienceFlags = 0;                     ///< XP-1: RW + 0x49C
	struct PendingDamage;
	std::vector<PendingDamage> m_pendingDamage;             ///< RW + 0x460
	bool m_effectivelyDead = false;                         ///< RW + 0x458 bit 0
	ArmorSetFlags m_armorSetFlags = 0;
	std::uint32_t m_weaponBonusMask = 0;
	Team *m_originalTeam = nullptr;       ///< lane HERO-2: + 0x320 (the team setTeam last recorded, RW 0x69959D)
	ObjectID m_speechLeader = INVALID_ID; ///< lane HERO-2: + 0x46C
	bool m_receivingDifficultyBonus = false; ///< lane CAMP-1H: + 0x45C (hashed)
	ObjectID m_capturerID = INVALID_ID;   ///< lane HERO-2: + 0x80
	unsigned m_undeadKillFrame = 0;       ///< lane HERO-2: + 0x450
	bool m_constructed = false;           ///< the constructor has finished: setTeam may run the owner change
	bool m_commandPointsCounted = false;  ///< RW + 0x4A0
	float m_buildCostPaid = 0.0f;         ///< RW + 0x33C
	float m_upgradeCostPaid = 0.0f;       ///< RW + 0x340 (UPGRADE-1)
	float m_constructionPercent = -1.0f;  ///< RW + 0x288

	Object *m_containedBy = nullptr;
	bool m_inWorld = false;           ///< RW + 0x474 (lane GARRISON-1)
	UnsignedInt m_containedFrame = 0;  ///< RW + 0x284 (lane GARRISON-1)
	bool m_drawableHidden = false;     ///< the drawable's hidden flag the contains set (lane GARRISON-1)
	ObjectID m_producerID = INVALID_ID;
	ObjectID m_builderID = INVALID_ID;                ///< RW + 0x7C (BUILD-2)
	ObjectID m_soleHealingBenefactorID = INVALID_ID;  ///< RW + 0x3D4 (BUILD-2)
	UnsignedInt m_soleHealingBenefactorExpiry = 0;    ///< RW + 0x3D8 (BUILD-2)
	UnsignedInt m_radarAttackFrame = 0xFFFFFFFFu;     ///< lane RADAR-1 (radarAttackFrame)
	ObjectClientHooks *m_client = nullptr; ///< SMOOTH-1: set by friend_bindToClient (no Drawable pointer in the simulation)

	ModelConditionBits m_modelCondition{};                   ///< RW + 0x10C
	UpgradeMaskType m_upgradeMask;                           ///< RW + 0x28C
	float m_instanceScale = 1.0f;                            ///< RENDER-2: the drawable's instance scale, logic owned
	ModelConditionBits m_placementConditions{};              ///< RENDER-2: the map placement's model condition bits
	std::string m_commandSetOverride;

	Object *m_next = nullptr, *m_prev = nullptr;             ///< GameLogic's object list
	Object *m_teamNext = nullptr, *m_teamPrev = nullptr;     ///< the team's member list

	bool m_recordedValid = false;
	bool m_previousValid = false;
	UnsignedInt m_recordedFrame = 0;                     ///< RW + 0x188 starts at 0
	Coord3D m_recordedPosition;
	float m_recordedAngle = 0.0f;
	float m_recordedBasis[9] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
	Coord3D m_previousPosition;
};
