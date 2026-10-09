// OpenBFME. GPL-3.0.
//
// Lane MODULES-2: the emotion modules. EmotionTrackerUpdate (342 retail templates: hordes, heroes, single units) chooses and runs one emotion nugget at a time
// from the requests it collects and from its own fear, hero and taunt scans; RadiateFearUpdate (28 templates: Witch-king, Black Riders, Fell Beast, attack troll,
// Rogash, barrow wights ...) pulses fear / terror / uncontrollable fear into the trackers of its victims. RotWK only; ported from the binary (caveat S-001).
//
// TARGET FACTS (RotWK game.dat):
//   * EmotionTrackerUpdate data (RW 0x8B525F, 0x40 bytes; table RW 0xC6D450): TauntAndPointDistance (+8, parseReal), TauntAndPointUpdateDelay (+0xC,
//     parseDurationUnsignedInt), TauntAndPointExcluded / AfraidOf / AlwaysAfraidOf / PointAt (+0x10 / 0x14 / 0x18 / 0x1C, ObjectFilter RW 0x76392F; default RW
//     0x763D11: NONE), HeroScanDistance (+0x20), FearScanDistance (+0x24), QuarrelProbability (+0x28, RW 0x42EEFA percent), IgnoreVeterancy (+0x2C),
//     ImmuneToFearLevel (+0x30, int, default 5), AddEmotion (RW 0x8B618C: `[OVERRIDE] <nugget>`; "Emotion not found" / "Emotion name or 'OVERRIDE <Emotion
//     name>' expected."; the data keeps a copy of the named nugget; OVERRIDE parses fields into the copy up to End and marks it (+0x18D)).
//   * the module (RW 0x8B62BE, Object + 0x254 caches it): + 0x24 requested[12], + 0x30 request end frame[12], + 0x60 request source[12], + 0x90 the object's
//     nuggets (a copy marked OVERRIDE is used as is, any other entry is the system's nugget of that name at construction), + 0x9C the running nugget, + 0xA0
//     the taunt scan countdown ((object id % TauntAndPointUpdateDelay) + 1, 1 without a delay), + 0xB0 the forced type (-1), + 0xB4 / + 0xB8 the forced
//     frames / source (script UNIT_FORCE_EMOTION), + 0xBC the next hero cheer frame, + 0xC0 the hero flag.
//   * request (RW 0x68F383: the object's top container (Object + 0x27C chain) asks its tracker; RW 0x8B4E75): requested[type] = 1, end = now + delay,
//     source = the source's id.
//   * update (RW 0x8B5738, every frame, returns 1): a contained object stops its nugget; otherwise the taunt / point scan (when the countdown runs out), the fear
//     and hero scans over TheEmotionSystem's scary / hero list, the QuarrelProbability draw (GameLogicRandomValueReal(0, 1) EmotionTrackerUpdate.cpp line
//     0x25E, EVERY frame unless status SPECIAL_ABILITY_PACKING_UNPACKING_OR_USING), the nugget choice (RW 0x8B54DD), the switch / start / update / stop of the
//     running nugget and the expiry of the requests. See EmotionModules.cpp for each step.
//   * RadiateFearUpdate (create RW 0x89F86E, data RW 0x89F7F6: InitiallyActive +8, WhichSpecialPower +0xC (-1), GenerateTerror / GenerateFear /
//     GenerateUncontrollableFear +0x10 / 0x11 / 0x12, EmotionPulseRadius +0x14, EmotionPulseInterval +0x18, VictimFilter +0x1C (default RW 0x763DAA: ALL),
//     the upgrade mux data at +0x20, table RW 0xC679F0). Constructor RW 0x89F890: wake next frame, InitiallyActive gives the mux its self upgrade (RW
//     0x855388); the upgrade wakes it next frame (RW 0x8554D6). update RW 0x89F9A8: a dead object or a mux that is not upgraded sleeps forever; else ThePartitionManager
//     within EmotionPulseRadius (type 0, unsorted; filters RW 0xC0F374 the same Object + 0x458 bit 3, RW 0xC1676C the owner's ENEMIES (4)); every hit the
//     VictimFilter allows for the owner gets TERROR (6) / FEAR (4) / UNCONTROLLABLE_FEAR (5) from the object with delay EmotionPulseInterval; sleep (object id % 5)
//     + EmotionPulseInterval.
//
//   * the taunt / point / alert scan (lane MODULES-3, RW 0x8B58B1 .. 0x8B59D1, every TauntAndPointUpdateDelay frames): unless the object is STAND_GROUND,
//     getClosestObject (RW 0xA39090: TauntAndPointDistance, centre 2D) with the filters in chain order: the tracker filter (RW 0x8B4FEA / 0x8B50F0: not while the
//     object is STEALTHED and not DETECTED; another member of the remembered target's horde passes; no STRUCTURE, HORDE, UNATTACKABLE, IMMOBILE, IGNORED_IN_GUI;
//     an ENEMY, alive and visible (RW 0x694CCC) sets "seen"; not TauntAndPointExcluded, not on a wall cell (RW 0x6EA857); a TAUNT or POINT nugget whose canApply
//     passes; the object on a wall, the candidate off the ground layer, or the ground line clear (RW 0x6EE5E3): remembered and kept), canSee within TauntAndPointDistance (RW
//     0x6612AC -> 0x68FA3D) and visibility (RW 0x660D71: visible, or a computer player and the object firing, RW 0x68C89B). A target in PointAt, AfraidOf or
//     AlwaysAfraidOf: POINT requested, the closest distance its edge distance (RW 0x66352C); another target: TAUNT; none: TAUNT and POINT off, ALERT = "seen".
// NOT PORTED / INFERENCE (stop S-1022, EmotionTrackerUpdate::stopLines; S-1021 for the nuggets): RW 0x694CCC's garrison (contain) and disguise branches are taken as
// visible; RW 0x6EA857 reads the ground cell's connect layer (the port's grid has no wall layer); the forced emotion of the scripts (+ 0xB0 .. + 0xB8) is never set; canSee's firing arc cone (template FiringArc below
// 360, RW 0x68FAC9) is reported (no retail template sets one); the relationship of the scans is Object::getRelationship (RW 0x68D7AB's mine / gate cases
// aside); without a shroud manager the threat is 0 and the vision range is the template's ShroudClearingRange (both reported); the layer (RW 0x68BBE0) is
// the pathfinder layer of the AI (ground when none).

#pragma once

#include "Common/INI.h"
#include "GameLogic/Module/UpdateModule.h"
#include "GameLogic/Module/UpgradeModule.h"
#include "GameLogic/ObjectFilter.h"
#include "GameLogic/System/EmotionSystem.h"

#include <array>
#include <memory>
#include <string>
#include <vector>

class ModuleFactory;
class StateHasher;

class EmotionTrackerUpdateModuleData : public ModuleData
{
public:
	float m_tauntAndPointDistance = 0.0f;   ///< + 8
	unsigned m_tauntAndPointUpdateDelay = 0; ///< + 0xC (frames)
	ObjectFilter m_tauntAndPointExcluded = ObjectFilter::none({}, {}); ///< + 0x10
	ObjectFilter m_afraidOf = ObjectFilter::none({}, {});              ///< + 0x14
	ObjectFilter m_alwaysAfraidOf = ObjectFilter::none({}, {});        ///< + 0x18
	ObjectFilter m_pointAt = ObjectFilter::none({}, {});               ///< + 0x1C
	float m_heroScanDistance = 0.0f;        ///< + 0x20
	float m_fearScanDistance = 0.0f;        ///< + 0x24
	float m_quarrelProbability = 0.0f;      ///< + 0x28
	bool m_ignoreVeterancy = false;         ///< + 0x2C
	int m_immuneToFearLevel = 5;            ///< + 0x30
	std::vector<std::shared_ptr<EmotionNuggetTemplate>> m_emotions; ///< + 0x34 (AddEmotion, in order)
	static void buildFieldParse(MultiIniFieldParse &p);
};

class EmotionTrackerUpdate : public UpdateModule
{
public:
	EmotionTrackerUpdate(Thing *thing, const EmotionTrackerUpdateModuleData *data); ///< RW 0x8B62BE
	UpdateSleepTime update() override;                                               ///< RW 0x8B5738
	void crc(StateHasher &h) const override;

	// RW 0x8B4E75
	void request(int type, Object *source, int delay);
	// RW 0x68F383: the request reaches the tracker of the object's top container (none: nothing)
	static void requestEmotion(Object &obj, int type, Object *source, int delay);
	// lane MODULES-3: RW 0x68F3A3 -> RW 0x8B4EA5: the top container's tracker drops the request of `type` (requested[type] = 0)
	static void clearEmotionRequest(Object &obj, int type);
	static EmotionTrackerUpdate *of(Object &obj);
	// lane SCRIPT-2: RW 0x8B4EB1 (UNIT / TEAM / PLAYER_FORCE_EMOTION through RW 0x68F3CB: the first tracker up the container chain): for seconds > 0 the
	// forced type (+ 0xB0), its frames ceil(0.005 * seconds * 1000) (+ 0xB4), the source (+ 0xB8); the running nugget stops (RW 0x8E168A)
	void force(int type, float seconds, Object *source);
	static void forceEmotion(Object &obj, int type, float seconds, Object *source);

	bool requested(int type) const { return type >= 0 && type < EMOTION_TYPE_COUNT && m_requested[(size_t)type]; }
	const EmotionNugget *current() const { return m_current; }
	int forcedType() const { return m_forcedType; }
	int forcedFrames() const { return m_forcedFrames; }
	// lane MODULES-3 r2: RW 0x8B4FA1 (from the AI machine's clearTemporaryState RW 0x751DA9): a current nugget with an AIState stops (EmotionNugget::stop RW 0x8E168A)
	void stopCurrentWithAIState();
	const std::vector<std::unique_ptr<EmotionNugget>> &nuggets() const { return m_nuggets; }
	static std::vector<std::string> stopLines();

private:
	EmotionNugget *selectNugget(int enemyThreat, unsigned friendThreat); ///< RW 0x8B54DD
	void threats(int &enemyThreat, unsigned &friendThreat) const;        ///< RW 0x8B54FF .. 0x8B553E
	void tauntScan(Object *&taunt, float &closest);                       ///< RW 0x8B58B1 .. 0x8B59D1 (lane MODULES-3)

	const EmotionTrackerUpdateModuleData *m_data;
	std::array<bool, EMOTION_TYPE_COUNT> m_requested{};         ///< + 0x24
	std::array<UnsignedInt, EMOTION_TYPE_COUNT> m_requestEnd{}; ///< + 0x30
	std::array<ObjectID, EMOTION_TYPE_COUNT> m_source{};        ///< + 0x60
	std::vector<std::unique_ptr<EmotionNugget>> m_nuggets;      ///< + 0x90
	EmotionNugget *m_current = nullptr;                         ///< + 0x9C
	int m_scanCountdown = 1;                                    ///< + 0xA0
	int m_forcedType = -1;                                      ///< + 0xB0
	int m_forcedFrames = 0;                                     ///< + 0xB4
	ObjectID m_forcedSource = INVALID_ID;                       ///< + 0xB8
	UnsignedInt m_nextHeroCheer = 0;                            ///< + 0xBC
	bool m_heroFlag = false;                                    ///< + 0xC0
};

class RadiateFearUpdateModuleData : public UpgradeModuleData
{
public:
	bool m_initiallyActive = false;       ///< + 8
	int m_whichSpecialPower = -1;         ///< + 0xC
	bool m_generateTerror = false;        ///< + 0x10
	bool m_generateFear = false;          ///< + 0x11
	bool m_generateUncontrollableFear = false; ///< + 0x12
	float m_emotionPulseRadius = 0.0f;    ///< + 0x14
	unsigned m_emotionPulseInterval = 0;  ///< + 0x18 (frames)
	ObjectFilter m_victimFilter = ObjectFilter::all({}); ///< + 0x1C
	static void buildFieldParse(MultiIniFieldParse &p);
};

class RadiateFearUpdate : public UpdateModule, public UpgradeMux
{
public:
	RadiateFearUpdate(Thing *thing, const RadiateFearUpdateModuleData *data); ///< RW 0x89F890
	UpgradeMux *getUpgrade() override { return this; }
	UpdateSleepTime update() override; ///< RW 0x89F9A8
	void crc(StateHasher &h) const override;
	unsigned pulses() const { return m_pulses; }
	unsigned long long requests() const { return m_requests; }

protected:
	void upgradeImplementation() override; ///< RW 0x8554D6: wake next frame

private:
	const RadiateFearUpdateModuleData *m_data;
	unsigned m_pulses = 0;                 ///< pulses run (hashed)
	unsigned long long m_requests = 0;     ///< emotion requests sent (hashed)
};

namespace EmotionModules
{
void registerAll(ModuleFactory &modules);
// RW 0x694CCC(player, 0) as the tracker's scans read it (the same INFERENCE, S-1022): also the visibility filter RW 0xC0F19C (allow RW 0x660D71) of the script
// engine's broadcasts (lane HERO-2, LiveScripting::objectsInRange)
bool visibleToScans(const Object &o);
// RW 0x68FA3D Object::canSee(other, range) as the tracker ports it (lane HERO-2: AutoAbilityBehavior's filter RW 0xC1D66C asks it too)
bool canSeeObject(Object &self, Object &other, float range);
}
