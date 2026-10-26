// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0 (GameLogic/ScriptEngine.h AttackPriorityInfo).
//
// AttackPriorityInfo and TheScriptEngine's table of them (lane TARGET-1): the priorities the AI's target search scores its candidates with
// (AI::findClosestEnemy, GameLogic/AI/AIClosestEnemy.h).
//
// TARGET FACTS (RotWK game.dat, caveat S-001):
//   * AttackPriorityInfo (ctor RW 0x758B1F, vtable 0xC2B744): + 4 the name, + 8 the default priority (1), + 0xC a map (const ThingTemplate * -> int) made on
//     the first setPriority (RW 0x759E24: the key is the template's final override, a later value overwrites).
//   * getPriority(hunter, target, adjust) RW 0x7597E9 (BFME2 decomp AttackPriorityInfoGetPriority.cpp, same shape): the default; a target without a template
//     answers it at once (no adjustment); else the map's value for the target's final override template when there is one; with `adjust` the combat chain
//     adjustment RW 0x758B68 (BFME2 decomp ScriptEngineSupport.cpp doModPriorityByCombatChain): the hunter's template ThreatBreakdown AIKindOf (+ 0x530) -1 ->
//     the priority unchanged; else the modifier of TheSkirmishAIData's combat chain of that AIKindOf for the target template's AIKindOf (RW 0x6A94A6: the
//     first of the 17 TargetTypes equal to it, else 0.0; the target's RAW template, no override); a modifier of -1.0 (RW 0xBD19DC) answers 0.0; when the
//     hunter's player has a skirmish AI (RW 0x6A950B) whose current enemy (RW 0x6C7807) controls the target, the modifier gains 100.0 (SSE addss); the
//     answer is x87: modifier / AIData AttackPriorityDistanceModifier + priority (fld; fdiv; fadd), stored by the caller as a float.
//   * the table: TheScriptEngine + 0x19200, 16-byte entries, entry 0 the default info (RW 0x603269: default 1, no map), count at + 0x1A200 (at most 0xFF
//     more entries). getAttackInfo(name) RW 0x605938: an entry of that name (entries 1 ..); else the AIData AttackPriority def of that name (AIData + 0xFC)
//     makes one (RW 0x6058B8: appended, named), its default the def's, its targets set back to front (each a ThingTemplate found by name, RW 0x6D1305;
//     none: skipped); no def: the default info (a debug line "Attempting to use an undefined attack priority").
//   * AIUpdateInterface + 0x70: the module data's AttackPriority (+ 0x44, the constructor RW 0x66E8FF sets "DefaultAttackPriority") through getAttackInfo when
//     the name is not empty (AI constructor RW 0x66ECFE); the object constructor (RW 0x697D58 .. 0x697D8B) replaces it with the team prototype's attack
//     priority name (RW 0x691F0C) when that is set and names a NAMED info.
//
// INFERENCE / NOT PORTED (stop S-326): the script actions that edit infos (SET_ATTACK_PRIORITY_THING / KIND_OF / SET_DEFAULT_ATTACK_PRIORITY and
// NAMED / TEAM_APPLY_ATTACK_PRIORITY_SET: S-1180) are not ported, so the table holds only infos made from AIData; nothing sets a team prototype's attack
// priority name either (ZH: only TEAM_APPLY_ATTACK_PRIORITY_SET does), so the object constructor's team override (RW 0x697D58) and the save's name
// (RW 0x667BEF) are not ported: AI + 0x70 is always the module data's info; the table is emptied with the script
// engine's reset (ZH ScriptEngine::reset does; RotWK's reset body is not read) and an entry made again holds the same values.

#pragma once

#include <deque>
#include <map>
#include <string>

class GameLogic;
class Object;
class ThingTemplate;

class AttackPriorityInfo
{
public:
	const std::string &getName() const { return m_name; }
	int getDefaultPriority() const { return m_defaultPriority; }
	// RW 0x7597E9 (the caller stores the answer as a float: fstp dword)
	float getPriority(const Object &hunter, const Object &target, bool adjust) const;
	// RW 0x759E24
	void setPriority(const ThingTemplate *tmpl, int priority);
	bool hasTable() const { return !m_priorities.empty(); }

private:
	friend class AttackPriorityStore;
	float doModPriorityByCombatChain(float priority, const Object &hunter, const Object &target) const;
	std::string m_name;
	int m_defaultPriority = 1;
	std::map<const ThingTemplate *, int> m_priorities; ///< looked up only (never iterated): pointer keys do not order anything
};

class AttackPriorityStore
{
public:
	explicit AttackPriorityStore(GameLogic &logic);
	// RW 0x603269
	const AttackPriorityInfo *getDefaultAttackInfo() const { return &m_entries.front(); }
	// RW 0x605938; never null
	const AttackPriorityInfo *getAttackInfo(const std::string &name);
	// the script engine's reset: back to the default info alone; `generation` changes so the AI modules look their info up again
	void reset();
	unsigned generation() const { return m_generation; }
	size_t size() const { return m_entries.size(); }

private:
	GameLogic &m_logic;
	std::deque<AttackPriorityInfo> m_entries; ///< entry 0 the default; a deque keeps the entries' addresses
	unsigned m_generation = 0;
	unsigned m_undefinedLookups = 0;
};
