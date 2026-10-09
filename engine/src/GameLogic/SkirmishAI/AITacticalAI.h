// OpenBFME. GPL-3.0.
//
// The skirmish AI's tactical layer (lane AI-1, step 4): the player info lists the brain reads, the enemy manager, the target chooser, the tactics generator
// with the SimpleAttack tactic, and the team builder's recruiting of tactic teams. Spec: workspace/rebuild/specs/skirmish-ai.md section 10.
//
// TARGET FACTS (RotWK game.dat, caveat S-001; static disassembly, decompiles checked against it):
//   * the brain (AISkirmishPlayer + 0x164, vtable RW 0xC16778, ctor RW 0x6C6DB1): + 0xC the target chooser (only the group's first AI, RW 0x90B3CE), + 0x10 the
//     tactics generator (RW 0x90CADA), + 0x1C RW 0x90CC05. Update RW 0x6C74C2: RW 0x6C7344; chooser ? RW 0x90B8B4 (enemy manager RW 0x9B304A, then RW 0x90B5A9);
//     the generator RW 0x90CB85 (RW 0x90BA94 drop ended tactics, RW 0x90B8C7 update each, RW 0x90C5E2 targetless tactics); the best target (chooser ? RW 0x90B342
//     -> + 0x14 : the group's shared + 0x18); a target goes to RW 0x90CB9F (type 0 ENEMY_STRUCTURE / 2 OPPORTUNITY -> RW 0x90C349, 1 DEFENSIVE -> RW 0x90C436,
//     3 EXPANSION -> RW 0x90C50C); then RW 0x90CCCB.
//   * player info record (manager + 0xA3C, 0x44 bytes, ctor RW 0x8E46D9): + 0 the army list (RW 0x99E5B6; add RW 0x99E6C8: not UNATTACKABLE, not IMMOBILE /
//     STRUCTURE, not SHIP; CAN_ATTACK, HERO or SUPPORT -> + 4, else a DOZER (RW 0x88B897 false) -> + 0x58); + 8 the structures (RW 0x99E989; filter RW 0x99E8C8:
//     (not UNATTACKABLE or EXPANSION_PAD), (not NOT_AUTOACQUIRABLE or REBUILD_HOLE), STRUCTURE, not ECONOMY_STRUCTURE, not LINKED_TO_FLAG, not BASE_SITE;
//     EXPANSION_PAD -> + 0x10 else + 4); + 0xC economy structures (RW 0x99E818); + 4 ships (RW 0x99E467). Lists hold ObjectIDs in the order added; RW 0x65BD07
//     drops the ids that no longer resolve.
//   * the chooser init RW 0x90B89A: the enemy manager's init RW 0x9B2EB9 (every other player whose template is a playable side and whose relationship to the AI is
//     ENEMIES (RW 0x6ACEAF == 0), as (index, 0.0)), the targets RW 0x90B4E7 (one AITarget, 0x3C bytes ctor RW 0x6C689D, per ArmyDefinition TacticalAITargets entry,
//     max teams = the MaxTeamsPerTarget entry of the same index, 1 past its end), the heuristics RW 0x90B581 (TheAITargetHeuristicLibrary RW 0xDE8BDC, made at
//     RW 0x82EF7B: types 1, 0 (RW 0xC85684), 3, 2, 2, 0 (RW 0xC85568)).
//   * AITarget: + 4 type, + 8 the frame it was chosen, + 0xC position, + 0x18 inactive (1 at creation), + 0x19 dropped, + 0x1C teams on it, + 0x20 max teams,
//     + 0x24 its AIThreatFinder, + 0x28 threat, + 0x34 the object, + 0x38 a running id (RW 0xDE4A00). Set RW 0x6C6975 (reset RW 0x6C63F0: frame -1, teams 0,
//     flags clear, position 0; then position, object, frame); RW 0x6C6380(1) marks it inactive and dropped.
//   * enemy manager update RW 0x9B304A: drops players that are gone or dead (RW 0x6AAC4B, Player + 0x754); when the current enemy was dropped or is none:
//     every score = RW 0x9B2B04 (the 3D distance from the AI's base to the enemy's: an AI enemy's base, else its start waypoint; a zero position scores 0), an
//     ascending sort (RW 0x9B3007: insertion sort below 16 entries), then the nearest unless there are >= 2, GetGameLogicRandomValue(1, 100) <= 50 (RW line
//     0x7A) and the second is within 1000 (RW 0xC893D8) of the first: then the second.
//   * chooser update RW 0x90B5A9: for every target that needs a choice (RW 0x90B1F4: inactive or dropped; else, after SecondsTillTargetsCanExpire * 5 frames,
//     GetGameLogicRandomValueReal(0, 1) (line 0x81) >= ChanceForTargetToExpire -> needs one, else its frame restarts): with no team on it, the current enemy
//     (none: skip), the heuristics of its type, GetGameLogicRandomValue(0, n - 1) (line 0xB8), the heuristic's choice (vslot 1: target, record, enemy); with a
//     team on it, it is marked dropped (+ 0x19).
//   * ENEMY_STRUCTURE heuristics: RW 0x97D4E6 (RW 0xC85684): for each unit of the AI's army list, the nearest (2D squared) of the enemy's structures that pass
//     RW 0x97D425 is counted; the most counted structure (the first with the largest count in id order, std::map) becomes the target with radius
//     DefaultTargetThreatRadius. RW 0x97CCBE (RW 0xC85568): when none of the enemy's structures passes RW 0x97CC29, each object of the enemy's army list's
//     + 0x58 (its dozers) is set as the target in turn (the last wins).
//   * best target RW 0x90B342: the first acceptable target (RW 0x90B2A0: not inactive / dropped; DEFENSIVE always; else its threat (AIThreatFinder) > 1000
//     and GetGameLogicRandomValue(0, 450) != 0 rejects it) with no team and max > 0, else the first acceptable with fewer teams than its max.
//   * RW 0x90C349 (ENEMY_STRUCTURE / OPPORTUNITY): RW 0x993373 gates on the difficulty (EASY in the Rush phase never; else the DifficultyTuning row's
//     OffensiveTacticActivationProbability n : d, 1.0 <= n / d always, else GetGameLogicRandomValue(0, d - 1) (AIDifficulty.cpp line 0x5B) < n); the offensive
//     prototypes (generator + 4, made by RW 0x90BE31: SimpleAttack x2, FormationAttack x2, FlankAttack, PincerAttack, FeintAttack, BasePenetration,
//     SimpleSiege, SiegeGates) that apply (vslot 1) are collected; GetGameLogicRandomValue(0, n - 1) (AITacticsGenerator.cpp line 0x158) picks one; its clone
//     (vslot 0x30) starts on the target (RW 0x8F21F7) and joins the active list (+ 0x10).
//   * SimpleAttack (vtable RW 0xC89848, ctor RW 0x9B4E2B "SimpleAttack"): applies (RW 0x9B49BD -> RW 0x9EF9AF) when a unit of the AI's army list (CAN_ATTACK or
//     SUPPORT, not IMMOBILE, not IGNORES_SELECT_ALL) can path to the target, unless the AI is EASY in the Rush phase and the target object is a living
//     DOZER_FACTORY; one team (vslot 4 = 1).
//   * start RW 0x8F21F7: when the target has fewer teams than its max: target and player kept; per team a pooled AI team prototype (TheTeamFactory + 0x10, 80
//     of them, RW 0x7A6295: "<TYPE>_<tactic>_<id>_<n>", wants members (+ 0x31C)); its type = the target's; vslot 3 (RW 0x8F121F: ENEMY_STRUCTURE ->
//     minimum size = GetGameLogicRandomValue(3, 5), AITactic.cpp line 0x1C7); all made -> the target's team count + 1, else the tactic ends.
//   * team builder (AIPlayer + 0x90, update RW 0x9A3366): every team that wants members recruits (RW 0x9A32C1 -> RW 0x9A2E06: the AI's army list in order, CAN_ATTACK
//     or SUPPORT and eligible (RW 0x9A24FC), up to the maximum (+ 0x2D0); then HERO units for types 0 / 1); a team with >= minimum members (and at its maximum or
//     RW 0x9A26A1) is handed over (RW 0x6C779B): it stops wanting members.
//   * the tactic's update RW 0x8F28DD (teams without a living member are dropped; not started: a dropped target ends it; started without teams: ends) and
//     RW 0x8F11DA (when every team is ready: not started -> the ready frame, then (RW 0xDE9F08 frames later, 0 in the image) started and vslot 0x18 =
//     RW 0x9B4DDA: every team attack-moves to the target position (RW 0x8F17EE: AIGroup::groupAttackMoveToPosition(pos, 0x7FFFFFFF, CMD_FROM_PLAYER));
//     started -> vslot 0x1C = RW 0x9B4DE7: every team gone or the target inactive ends it (RW 0x8F2006(1, 0))).
//   * end RW 0x8F2006(success, ...): ended; the target's team count - 1; success marks the target inactive and dropped (RW 0x6C6380(1)).
//   * lane AI-2: the tactic's update RW 0x8F28DD, started: RW 0x8F135E (per team entry (0x14 bytes: team, last centre, idle frames): the centre (RW 0x7A0ECD, the
//     members' average) moved more than TeamIdleCheckRadius -> stored and 0; an engaged member (RW 0x7A03AB) -> 0; else + 1), RW 0x8F27AD (the tactic position
//     + 0x38 = the average of the team centres), the origin + 0x44 (the first position), vslot 0x20, RW 0x8F144C, vslot 0x2C (RW 0x8F2784: the retreat on a
//     negative threat); SimpleAttack's started update RW 0x9B4DE7 ends the tactic with success when every team is idle for TeamTimeUntilConsideredIdle seconds
//     (RW 0x8F175A / 0x8F1725) or the target is inactive.
// What is not ported (stops S-417 / S-418 / S-891, reported at runtime): the other 8 offensive tactics (the pick draws among the 2 SimpleAttack prototypes only), the
// DEFENSIVE / OPPORTUNITY / EXPANSION heuristics and tactics, the targetless tactics (RW 0x90C5E2: FarmBuilder, StructureCreep, ...), the AIThreatFinder (threat
// taken as 0: no draw at RW 0x90B2A0), RW 0x6C7344, RW 0x90CCCB, RW 0x8F144C (the teams' AIGroup step) and RW 0x8F2784's retreat (no threat finder), the other tactics'
// launch / update bodies, the end's merge of the teams, the AI team objects (members stay in the player's team), the pathfinder check of RW 0x9EF9AF (taken as reachable), the filters
// RW 0x97D425 / 0x97CC29 / 0x9A24FC / 0x88B897 in full.

#pragma once

#include "Common/INIDataTypes.h"
#include "GameLogic/ObjectTypes.h"
#include "GameLogic/SkirmishAI/AIThreatFinder.h"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

class GameLogic;
class Player;
class StateHasher;
struct AISkirmishPlayer;
struct AISkirmishGroup;

// the player info record lists the brain reads (S-418: rebuilt each frame from the living objects in id order)
struct AIPlayerInfoLists
{
	std::vector<ObjectID> army;       ///< record + 0 -> + 4
	std::vector<ObjectID> dozers;     ///< record + 0 -> + 0x58
	std::vector<ObjectID> structures; ///< record + 8 -> + 4
};

// AITarget (0x3C bytes, ctor RW 0x6C689D)
struct AITarget
{
	int type = 0;                    ///< + 0x04 (AITARGET index)
	std::uint32_t frame = 0;         ///< + 0x08 (0 at creation, the frame it was set; -1 after a reset)
	Coord3D position;                ///< + 0x0C
	bool inactive = true;            ///< + 0x18
	bool dropped = false;            ///< + 0x19
	int teams = 0;                   ///< + 0x1C
	int maxTeams = 1;                ///< + 0x20
	float radius = 0.0f;             ///< the threat finder's radius (+ 0x24 -> + 0x5B0)
	Coord3D finderPosition;          ///< lane AI-3: the threat finder's position (+ 0x24 -> + 0x5A4; RW 0x6C644C may move it to a failed tactic's position)
	float threat = 0.0f;             ///< lane AI-3: + 0x28, the enemies' threat around the finder (RW 0x6C69F8 / 0x6C644C, GameLogic/SkirmishAI/AIThreatFinder.h)
	ObjectID object = INVALID_ID;    ///< + 0x34
	std::uint32_t id = 0;            ///< + 0x38 (RW 0xDE4A00 running; per AI here)
};

// one team of a tactic (an AI team prototype of the pool, RW 0x7A6295)
struct AITacticTeam
{
	std::string name;                ///< "<TYPE>_<tactic>_<id>_<n>"
	int type = 0;                    ///< + 0x2CC
	int minimum = 0;                 ///< + 0x2D4
	int maximum = -1;                ///< + 0x2D0 (-1: the pooled prototype's own value, not identified)
	bool wantsMembers = true;        ///< + 0x31C
	bool handedOver = false;         ///< + 0x31D
	std::vector<ObjectID> members;   ///< S-418: the recruited units (RW: the team's members)
	Coord3D lastPosition;            ///< tactic team entry + 0x04: the team centre when it last moved (RW 0x8F135E)
	int index = 0;                   ///< the pooled prototype's + 0x2DC: the team's index in its tactic (RW 0x8F128B / 0x8F12C3 look teams up by it)
	int idleFrames = 0;              ///< entry + 0x10: frames without moving or fighting (RW 0x8F135E; done at TeamTimeUntilConsideredIdle seconds, RW 0x8F1725)
};

// an active tactic (RW AITactic, the SimpleAttack clone)
struct AITactic
{
	std::string kind;                ///< the prototype's name ("SimpleAttack")
	int kindIndex = 0;               ///< which offensive prototype (AITacticalAI.cpp OffensiveKind)
	std::uint32_t id = 0;            ///< + 0x30
	int targetIndex = -1;            ///< + 0x20 (index into the owning brain's targets)
	int targetOwner = -1;            ///< the player index whose brain holds the target (the group's first AI)
	std::vector<AITacticTeam> teams; ///< + 0x14
	bool started = false;            ///< + 0x10
	bool ended = false;              ///< + 0x28
	bool ready = false;              ///< + 0x51
	std::uint32_t readyFrame = 0;    ///< + 0x54
	bool success = false;            ///< the end's argument (report)
	Coord3D position;                ///< + 0x38: the centre of the teams' centres (RW 0x8F27AD)
	Coord3D origin;                  ///< + 0x44: the first such centre after the start (RW 0x8F2955), where a retreat goes (RW 0x8F243A)
	bool retreated = false;          ///< + 0x50
	bool targetless = false;         ///< lane AI-2 r3: a clone of a targetless prototype (generator list + 0x58)
	ObjectID squadTarget = INVALID_ID; ///< FarmKillSquad + 0x58: the object the squad attacks
	// lane AI-2 r5: the offensive bodies' own state (the tactic's + 0x58 object / fields)
	int step = 0;                    ///< FormationAttack state (+ 0x58 -> + 0); FlankAttack waypoint index; PincerAttack team 0's waypoint index
	int step2 = 0;                   ///< PincerAttack team 1's waypoint index
	std::vector<Coord3D> waypoints;  ///< FlankAttack / PincerAttack waypoints (+ 0x58 -> vector)
	Coord3D gatherPos;               ///< FormationAttack + 0x58 -> + 4: the team's centre at the launch
	Coord3D aimPos;                  ///< FormationAttack + 0x58 -> + 0x10: the target position at the launch
	bool mainLaunched = false;       ///< FeintAttack + 0x58: the main team was sent
	std::uint32_t launchFrame = 0;   ///< FeintAttack + 0x5C: the launch frame
};

// lane AI-3: an attacked-at record of the brain's list + 0x20 (0xAC bytes, ctor RW 0x6C6F01): where the AI's team members fight
struct AIAttackedAt
{
	std::uint32_t id = 0;                    ///< + 0x00 (the manager's running id RW 0xDE4A08, or the merged record's)
	Coord3D position;                        ///< + 0x04
	float radius = 0.0f;                     ///< + 0x14 (300, RW 0xC16764)
	std::uint32_t frame = 0;                 ///< + 0x18
	AIThreatFinder::ThreatRecord enemies;    ///< + 0x1C: the objects whose player is the AI's ENEMIES
	AIThreatFinder::ThreatRecord others;     ///< + 0x64: the other (allied) objects
};

struct AIBrainState
{
	bool chooser = false;                         ///< + 0xC exists (the group's first AI)
	bool chooserInitialised = false;
	std::vector<std::pair<int, float>> enemies;   ///< enemy manager + 4 (player index, score)
	int currentEnemy = -1;                        ///< enemy manager + 0x14
	std::vector<AITarget> targets;                ///< chooser + 8
	std::uint32_t nextTargetId = 0;
	int bestTarget = -1;                          ///< + 0x14 (index; -1 none)
	std::vector<AITactic> tactics;                ///< generator + 0x10
	std::uint32_t nextTacticId = 0;
	AIPlayerInfoLists lists;                      ///< the AI player's record lists (S-418)
	std::map<std::string, int> tacticsByKind;    ///< tactics started by prototype name (report)
	// lane AI-2 r3: the targetless prototypes (generator + 0x4C, RW 0x90C0EE) and the AI's named counters (RW 0x6C7EDD / 0x6C7EC5: a lookup of a missing name stores 0)
	bool generatorMade = false;                   ///< RW 0x90CADA ran (the prototypes exist)
	bool farmKillEarly = false;                   ///< FarmKillSquad prototype + 0x60: GetGameLogicRandomValue(1, 100) < 95 at its creation (RW 0x9BB41D)
	std::map<std::string, int> namedCounters;     ///< AISkirmishPlayer's map of named ints
	unsigned long long targetlessPicks = 0, targetlessStarted = 0, targetlessUnported = 0;
	std::vector<AIAttackedAt> attackedAt;          ///< lane AI-3: + 0x20 (RW 0x6C7344 / 0x6C720A), at most 20
	AIThreatFinder::ThreatRecord idleArmy;        ///< lane AI-3: the player info record's army list + 0x10 (RW 0x99E63B): the army in the default team
	unsigned long long gateDraws = 0, gateRejects = 0; ///< lane AI-3: RW 0x90B2A0's draws (AITargetChooser.cpp 0xF7) and rejections
	unsigned long long retreats = 0;                ///< lane AI-3: tactics ended by RW 0x8F243A
	unsigned long long merges = 0;                  ///< lane AI-3: teams joined into another tactic's team at an end (RW 0x8F1D88 / 0x7A0F74)
	unsigned long long mergeTargetlessUnported = 0; ///< lane AI-3: merge candidates whose tactic has no target (RW 0x8F14C1, not ported)
	unsigned long long targetsSet = 0, tacticsStarted = 0, attacksLaunched = 0, tacticsEnded = 0, gateRefusals = 0, unportedTypes = 0;
};

namespace AITacticalAI
{
// the record lists of `player` (S-418)
AIPlayerInfoLists listsOf(GameLogic &logic, const Player &player);
// AIPlayer + 0x90's update (RW 0x9A3366): recruiting and hand-over of the tactic teams
void updateTeamBuilder(GameLogic &logic, AISkirmishPlayer &ai);
// the brain's update (RW 0x6C74C2); `first` is the group's first AI (the target chooser's owner)
void updateBrain(GameLogic &logic, AISkirmishPlayer &ai, AISkirmishPlayer *first);
void crc(const AIBrainState &b, StateHasher &h);
// lane AI-3: RW 0x99E63B (the player info record update RW 0x8E3CF3, run for every record before the groups): the threat record of the AI's army list objects
// that are in the player's default team (not in a tactic team)
void updateIdleArmy(GameLogic &logic, AISkirmishPlayer &ai);
// RW 0x90CADA's prototype constructors (lane AI-2 r3): the one logic random draw they make (FarmKillSquad RW 0x9BB3CA); called when the AI is made (RW 0x6C6DB1)
void makeGenerator(GameLogic &logic, AISkirmishPlayer &ai);
// test hooks (lane AI-2 r7): FlankAttack's (RW 0x9B461D) and PincerAttack's (RW 0x9B4041) waypoints from `start` to the target's position, with their draws
void flankWaypointsForTest(GameLogic &logic, AITactic &tac, const AITarget &t, const Coord3D &start);
// test hook (lane AI-3): RW 0x6C720A(pos, -1) on the AI's attacked-at list
void addAttackedAtForTest(GameLogic &logic, AISkirmishPlayer &ai, const Coord3D &pos);
void pincerWaypointsForTest(GameLogic &logic, AITactic &tac, const Coord3D &start, const Coord3D &target);
} // namespace AITacticalAI
