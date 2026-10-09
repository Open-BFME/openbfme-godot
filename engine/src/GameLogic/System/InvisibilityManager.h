// OpenBFME. GPL-3.0.
//
// InvisibilityManager (lane STEALTH-1): RotWK's invisibility system, TheGameLogic + 0x178 (the binary names the class "InvisibilityManager", RW 0xBFDA6C). Zero Hour
// has no counterpart: RotWK's stealth for most units is not the ZH StealthUpdate but InvisibilityUpdate / InvisibilitySpecialPower modules that hand an
// InvisibilityNugget (STEALTH or CAMOUFLAGE, the forbidden conditions, the detection range, ...) to this manager, which decides every second whether the object
// is invisible. The object's invisibility lives in two model condition bits (INVISIBLE_STEALTH 0x222, INVISIBLE_CAMOUFLAGE 0x223, RW 0x691AA3) and the reveal in
// two statuses (INVISIBLE_DETECTED 0x60, INVISIBLE_DETECTED_BY_FRIEND 0x5F).
//
// TARGET FACTS (RotWK game.dat, caveat S-001; read with Ghidra / capstone):
//   * InvisibilityNugget (parser RW 0x81A667: a nested block through initFromINI, table RW 0xC507B8, 0xC8 bytes, constructor RW 0x6540C0): ForbiddenConditions +0
//     (parseBitString32 RW 0x42E840 over the names at RW 0xDA748C), ForbiddenWeaponConditions +4 (RW 0x6C9951, a weapon set condition mask), DetectionRange +0x14
//     (parseReal, default 0), InvisibilityType +0x18 (parseIndexList RW 0x42E956 over RW 0xDA7484, default 2 = none), IgnoreTreeCheckUpgrades +0x1C (an upgrade
//     mask, RW 0x66F603), Options +0xAC (parseBitString32 over RW 0xDA74B4), BecomeStealthedFX +0xB0 / ExitStealthFX +0xB4 (FXList, RW 0x73A302),
//     HintDetectableConditions +0xB8 (an object status mask, RW 0x7B1E5C). The three name lists are ONE run of pointers without terminators between them
//     (STEALTH CAMOUFLAGE | AWAY_FROM_TREES MOVING FIRING_PRIMARY .. FIRING_QUINARY FIRING_ANY TAKING_DAMAGE USING_ABILITY | ALLOW_NEAR_TREES DETECTED_BY_FRIENDLIES
//     DISCONTINUE_WHEN_REVEALED UNTOGGLE_HIDDEN_WHEN_LEAVING_STEALTH | NONE HOLD KILL SPAWN, NULL at RW 0xDA74D4): each parser accepts every later name of the run
//     (e.g. InvisibilityType = AWAY_FROM_TREES is 2), which this port reproduces.
//   * applyNugget (RW 0x81C217, obj, frames, nugget): only a horde object or an object outside a horde (template KindOf HORDE, RW +0x115 bit 5, or RW 0x693A1A(0)
//     null) and a type below 2; the object's entry (a map keyed by object id, RW + 4) is made when missing; an applied copy with a non-zero start frame equal to
//     the nugget (RW 0x81A7A3: every field compared, the hint mask compared with ITSELF, a retail slip) gets `frames` more lifetime; else a new copy (RW 0x81A8F5:
//     start = now, frames, inactive) is appended, and the object is evaluated at once (RW 0x81BCBD).
//   * markDetected (RW 0x81C32C, target, detector, frames, mode): the same horde guard; the entry's not-before frame (+4) becomes max(+4, now + frames)
//     (RW 0x81A751); the detector seen as ALLIES by the target's player marks "by a friend" (RW 0x6ADBEB == 2); then the object is evaluated (RW 0x81BB8A / 0x81B376)
//     with that detector, and a reveal runs the client notification (RW 0x81ACD7).
//   * update (RW 0x81BE85, GameLogic::update phase 1 at RW 0x62E8DA, just before the command list): once every LOGICFRAMES_PER_SECOND frames (+0x14 = now + 5);
//     every entry in id order: a missing object, or an evaluation that answers false, removes the entry.
//   * evaluate (RW 0x81BCBD): type = RW 0x81BB8A; RW 0x81B376 applies it (a reveal also sends the client notification RW 0x81ACD7 and pushes the not-before frame
//     by GameData ReinvisibityDelay, RW 0x81A751 with GlobalData + 0x11CC); a detected object whose detection expired (+8 <= now) loses both detected statuses
//     (RW 0x81B054); the client opacity (RW 0x81AA85) follows; the entry stays while it has applied copies or a pending not-before / detection frame.
//   * RW 0x81BB8A: copies whose lifetime ended and that are inactive are erased (RW 0x81AB8A); then in list order each copy's condition (RW 0x81B948) is computed
//     while start + frames >= now; the first active STEALTH copy decides (STEALTH, its BecomeStealthedFX when it was not active); an active CAMOUFLAGE copy makes
//     the answer CAMOUFLAGE; a copy that stops being active applies DISCONTINUE_WHEN_REVEALED (its lifetime ends), UNTOGGLE_HIDDEN_WHEN_LEAVING_STEALTH, records
//     the detector (DETECTED_BY_FRIENDLIES: "by a friend"), its ExitStealthFX and its hint mask.
//   * RW 0x81B948 (the condition): not dead, not detected (RW 0x693BF2 on the horde root: status 0x60 or 0x5F), no model condition 0x220, now >= the not-before
//     frame, no disguise (the object's StealthUpdate, RW 0x68FBD3, + 0x3C); then MOVING (speed > 0, RW 0x68B34C), the FIRING bits (RW 0x81A926), ForbiddenWeaponConditions
//     against the weapon set flags (RW 0x75CDC4), USING_ABILITY (status 0x18), TAKING_DAMAGE (only while invisible: the body's last damage frame >= the frame the
//     object became invisible, a non-zero amount, a type other than 7); ALLOW_NEAR_TREES turns a failed test into a pass near trees, AWAY_FROM_TREES fails away
//     from them (RW 0x81ABDF); a STEALTH copy fails while the script disabled stealth (Object + 0x457 bit 8, RW 0x7BC7EC); a CAMOUFLAGE copy fails when a
//     camouflage detector is in range (RW 0x81B64F).
//   * RW 0x81A926 (firing): a HORDE asks its members (contain iterate, RW 0x81A635), anything else itself, RW 0x68C89B: the current weapon exists and either its
//     status is not READY_TO_FIRE or the firing tracker's last shot is after frame 2 and less than 2 * LOGICFRAMES_PER_SECOND frames ago. FIRING_ANY (bit 7)
//     passes then; FIRING_PRIMARY .. QUINARY (bits 2 .. 6) need that weapon slot's last fire frame >= now - 1.
//   * RW 0x81ABDF (near trees): an IgnoreTreeCheckUpgrades bit on the object, or a live object of KindOf TREE within 50.0 (RW 0xBD88C4; partition distance type 0),
//     or a terrain tree (TheTerrainLogic RW 0x67F52B -> 0x67E078: the tree list at + 0x578, records not toppled (+0x18) whose 3D squared distance is below 50^2).
//   * RW 0x81B64F (camouflage detectors): within (the candidate's template CamouflageDetectionMultiplier, ThingTemplate + 0x10) * DetectionRange, 3D squared
//     distance below the square, every object other than the target that is alive, has an AI, passes GameData CamouflageDetectorObjectFilter (GlobalData + 0xEB4,
//     with the target's controlling player) and that the target is ENEMIES to (ALLIES with DETECTED_BY_FRIENDLIES) from the candidate's side (RW 0x660B85).
//   * RW 0x81B376 (apply a type): the current type (RW 0x68FC06: the two model conditions) changes through RW 0x81AB0A (the object and, for a HORDE, every
//     member); leaving STEALTH with the untoggle flag asks the object's ToggleHiddenSpecialAbilityUpdate; becoming visible plays the ExitStealthFX (from STEALTH)
//     and reveals (RW 0x81AF68: enemy players' idle AIs that see the object wake, the detection frame +8 = max(+8, now + frames), status 0x5F or 0x60);
//     becoming invisible from visible plays the BecomeStealthedFX (to STEALTH) and stamps +0xC = now.
//   * RW 0x694C0D (stealthed and undetected, used by the target acquisition, the attack states, the detector filter, the fire FX ...): no draw module whose
//     slot 0xF0 answers false (RW drawable + 0x14C), not detected (RW 0x693BF2), not DETECTED (status 0x11), invisible (RW 0x68FC2F) or STEALTHED (0xF), not
//     firing when the viewer is a computer player (player + 0x5C == 1), and not a DISGUISER (template + 0x113 bit 0) whose StealthUpdate disguises it as
//     a player allied to the viewer (+ 0x38 / + 0x3C, RW 0x6ACEAF).
// WHAT IS INFERENCE / NOT PORTED (stop S-1040, reported):
//   * the camouflage detector and TREE queries run on ThePartitionManager (lane MODULES-2 / STEALTH-2, with the largest multiplier of RW 0x81A6A1 as the range);
//     the same-map filter (RW 0xC0F374) always passes (no off-map objects are ported);
//   * the terrain tree list holds the map's KindOf TREE map objects that the map loop sends to the tree buffer (their positions; the toppled flag never sets: tree
//     toppling is not ported); the TREE object query uses the 2D centre distance (partition distance type 0 as ZH FROM_CENTER_2D);
//   * the client notification (RW 0x81ACD7: radar event, EVA, "MESSAGE:StealthDiscovered"), the opacity pulse (RW 0x81AA85) and the reveal sounds are the
//     client's; the FX play through the logic's FX events;
//   * the reveal's waking of idle enemy AIs uses the AI's VisionRange (2D centre distance) for RW 0x68FA3D's vision test;
//   * TAKING_DAMAGE reads the body's last damaging hit frame (the engine body does not keep the last hit's amount and type);
//   * the script's stealth switch (Object + 0x457 bit 8) is not ported: never set (StealthUpdate's disguise, RW 0x81B995 / 0x694C86: lane STEALTH-2);
//   * RW 0x694C0D's draw module slot 0xF0 is read as the model draw data's AffectedByStealth (RW +0x15E; the slot's body is not identified).

#pragma once

#include "Common/INI.h"
#include "Common/INIDataTypes.h"
#include "Common/Upgrade.h"
#include "GameLogic/BitFlags.h"
#include "GameLogic/ObjectTypes.h"

#include <list>
#include <map>
#include <string>
#include <vector>

class GameLogic;
class Object;
class Player;
class StateHasher;

// RW 0xC507B8 (0xC8 bytes, constructor RW 0x6540C0)
struct InvisibilityNugget
{
	enum Type
	{
		STEALTH = 0,
		CAMOUFLAGE = 1,
		NONE = 2
	};
	enum Forbidden : unsigned
	{
		AWAY_FROM_TREES = 1u << 0,
		MOVING = 1u << 1,
		FIRING_PRIMARY = 1u << 2,
		FIRING_SECONDARY = 1u << 3,
		FIRING_TERTIARY = 1u << 4,
		FIRING_QUATERNARY = 1u << 5,
		FIRING_QUINARY = 1u << 6,
		FIRING_ANY = 1u << 7,
		TAKING_DAMAGE = 1u << 8,
		USING_ABILITY = 1u << 9
	};
	enum Option : unsigned
	{
		ALLOW_NEAR_TREES = 1u << 0,
		DETECTED_BY_FRIENDLIES = 1u << 1,
		DISCONTINUE_WHEN_REVEALED = 1u << 2,
		UNTOGGLE_HIDDEN_WHEN_LEAVING_STEALTH = 1u << 3
	};
	unsigned forbiddenConditions = 0;                 ///< +0x00
	WeaponConditionFlags forbiddenWeaponConditions{}; ///< +0x04
	float detectionRange = 0.0f;                      ///< +0x14
	int invisibilityType = NONE;                      ///< +0x18
	UpgradeMaskType ignoreTreeCheckUpgrades;          ///< +0x1C
	unsigned options = 0;                             ///< +0xAC
	std::string becomeStealthedFX;                    ///< +0xB0 (empty: none)
	std::string exitStealthFX;                        ///< +0xB4
	ObjectStatusMaskType hintDetectableConditions{};  ///< +0xB8

	// RW 0x81A7A3 (the hint mask is compared with itself: never a difference)
	bool sameAs(const InvisibilityNugget &o) const;
	void crc(StateHasher &hasher) const;
	// the field parse of the nested block (RW 0x81A667): `store` points at an InvisibilityNugget
	static void parse(INI *ini, void *instance, void *store, const void *userData);
	// the binary's name runs (tests)
	static const char *const *typeNames();      ///< RW 0xDA7484
	static const char *const *forbiddenNames(); ///< RW 0xDA748C
	static const char *const *optionNames();    ///< RW 0xDA74B4
};

class InvisibilityManager
{
public:
	explicit InvisibilityManager(GameLogic &logic);

	// RW 0x81BE40 (a new game): no entries, the next update now; the terrain trees are kept (the map loop sets them)
	void reset();
	// RW 0x81BE85
	void update();
	// RW 0x69A696 .. 0x69A6BF (the Object constructor, just before its world entry RW 0x69A6C6; GameLogic::friend_objectEnteredWorld calls it): a template that
	// CamouflageDetectorObjectFilter allows (RW 0x763543 with no players) raises the largest CamouflageDetectionMultiplier (RW 0x81A6A1)
	void noteCreated(const Object &obj);
	// RW 0x81C217: `nugget` is template data (it outlives the game)
	void applyNugget(Object *obj, unsigned frames, const InvisibilityNugget &nugget);
	// RW 0x81C32C; true when the object became visible
	bool markDetected(Object *target, Object *detector, unsigned frames, int mode);

	// RW 0x81A6E7: every player ENEMIES to the object's player wakes its idle AIs that see the object (RW 0x81A6B5); also StealthUpdate's reveal
	void wakeEnemiesThatSee(Object *obj);
	// RW 0x81ABDF's tree part at a position (a TREE object, then the terrain trees); StealthUpdate's AWAY_FROM_TREES asks it too (RW 0x7764D4 ..)
	bool treesNear(const Coord3D &pos, const Object *self) const;

	// the terrain tree list (RW TheTerrainLogic + 0x578, filled by the map's object loop, RW 0x683D89)
	void clearTrees() { m_trees.clear(); }
	void addTree(const Coord3D &pos) { m_trees.push_back(pos); }
	size_t treeCount() const { return m_trees.size(); }
	const std::vector<Coord3D> &trees() const { return m_trees; }

	// ---- the object queries (static: they read the object only) ----
	static int invisibilityType(const Object &obj);       ///< RW 0x68FC06
	static bool isInvisible(const Object &obj);           ///< RW 0x68FC2F
	static bool isDetected(const Object &obj);            ///< RW 0x693BF2 (the horde root's statuses)
	static bool isFiring(const Object &obj);              ///< RW 0x68C89B
	// RW 0x694C0D: the object is stealthed and undetected for `viewer` (null: any viewer)
	static bool isStealthedAndUndetected(const Object &obj, const Player *viewer);

	// the client's look of the object for the local player (RW 0x81AA03, the drawable state RW 0x6760F9 reads): 0 drawn normally, 1 invisible and seen by a friend
	// (the opacity pulse), 3 detected and seen by an enemy, 4 detected and seen by a friend, 5 invisible and seen by an enemy (hidden). `local` null: an observer
	// (RW 0x6AAC52 false: everything is seen as a friend)
	static int clientLook(const Object &obj, const Player *local);
	// the friend's opacity pulse of the object: GameData InvisibilityOpacityMin / Max / CycleFrames for an invisible object, its StealthUpdate's FriendlyOpacityMin /
	// Max / PulseFrequency for a STEALTHED one (both RW 0x6760F9 inputs)
	void clientOpacityRange(const Object &obj, float *lo, float *hi, unsigned *cycleFrames) const;

	// ---- reports / tests ----
	void crc(StateHasher &hasher) const;
	std::vector<std::string> report() const;
	static std::vector<std::string> stopLines();
	size_t entryCount() const { return m_entries.size(); }
	size_t appliedCount(ObjectID id) const;
	unsigned reveals() const { return m_reveals; }
	unsigned becameInvisible() const { return m_becameInvisible; }

private:
	struct Applied // RW list node data: the nugget copy, + 0xC8 start, + 0xCC frames, + 0xD0 active
	{
		const InvisibilityNugget *nugget = nullptr;
		unsigned startFrame = 0;
		unsigned frames = 0;
		bool active = false;
	};
	struct Entry // RW 0x81B4C7
	{
		std::list<Applied> applied;
		unsigned notBefore = 0;       ///< + 4 (RW 0x81A751)
		unsigned detectedUntil = 0;   ///< + 8
		unsigned invisibleSince = 0;  ///< + 0xC
	};
	struct Outcome // RW 0x81A8C4
	{
		Object *detector = nullptr;   ///< + 0
		bool byFriend = false;        ///< + 4
		const std::string *becomeFX = nullptr; ///< + 8
		const std::string *exitFX = nullptr;   ///< + 0xC
		ObjectStatusMaskType hint{};  ///< + 0x10
		bool untoggleHidden = false;  ///< + 0x20
	};

	static bool tracked(const Object &obj); ///< the horde guard of RW 0x81C217 / 0x81C32C / 0x81AA85
	bool evaluate(Object *obj, Entry &e);                                    ///< RW 0x81BCBD
	int computeType(Object *obj, Entry &e, Outcome &out);                     ///< RW 0x81BB8A
	bool allowed(Object *obj, const InvisibilityNugget &n, const Entry &e, ObjectID *found); ///< RW 0x81B948
	bool firingForbidden(const Object &obj, unsigned forbidden) const;       ///< RW 0x81A926
	bool nearTrees(const Object &obj, const InvisibilityNugget &n) const;    ///< RW 0x81ABDF
	bool camouflageDetected(const Object &obj, const InvisibilityNugget &n, ObjectID *found) const; ///< RW 0x81B64F
	bool applyType(Object *obj, int type, Entry &e, const Outcome &out, unsigned delay); ///< RW 0x81B376
	void reveal(Object *obj, Entry &e, unsigned delay, bool byFriend);       ///< RW 0x81AF68
	void clearDetected(Object *obj);                                         ///< RW 0x81B054
	void setType(Object *obj, int type);                                     ///< RW 0x81AB0A
	void playFX(const std::string *fx, const Object &obj, const char *site);
	void note(const std::string &what);

	GameLogic &m_logic;
	std::map<ObjectID, Entry> m_entries; ///< RW + 4
	unsigned m_nextUpdate = 0;           ///< RW + 0x14
	float m_maxMultiplier = 1.0f;        ///< RW + 0x10: the largest CamouflageDetectionMultiplier of a created template the detector filter allows (RW 0x81A6A1)
	std::vector<Coord3D> m_trees;
	unsigned m_reveals = 0, m_becameInvisible = 0; ///< counters (tests, report)
};
