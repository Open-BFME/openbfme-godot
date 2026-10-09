// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// The script tree as stored in a map: parsed into plain structs, NOT executed (execution belongs to
// the scripts spec).
//
// Layouts: ZH GameEngine/Source/GameLogic/ScriptEngine/Scripts.cpp (ScriptList/ScriptGroup/Script/
// OrCondition/Condition/ScriptAction readers, :531-640, 889-900, 1264-1290, 1466-1480, 1688-1760,
// 2142-2240, 2449-2500) as changed by the BFME2 writers (target facts, Open-BFME-2
// GameLogic/ScriptEngine/: ScriptGroupWriteGroup.cpp:90, ScriptSubRecordWrite.cpp:38-45,
// ConditionWriteDataChunk.cpp:122-160, ScriptActionWriteAction.cpp:86), and spec
// maps-and-terrain.md 1.7.8, whose layouts decode all 181 retail maps with zero leftover bytes.
//
// Type resolution (lane SCRIPT-1, TARGET FACTS, RotWK game.dat, caveat S-001): the record keeps the stored ordinal and name, and
// `resolved` is the ordinal the RotWK parsers give it against the binary's template registry (ScriptTemplates.h):
//   * Condition (RW 0x7B776D): version >= 4: the stored ordinal's template when its NameKey is the stored name, else the first of
//     ordinals 0 .. 202 with that name; no match, or a version < 4 record: CONDITION_FALSE (retail deletes the parameters; the record
//     keeps them for reports, a FALSE never reads them). The parameters are read
//     anyway; a parameter of type 15 whose template slot is type 61 becomes 61 (RW 0x7B331B). v >= 5 reads + 0x4C (`enabled`, else 1)
//     and + 0x4D (else 0). Then the old-file heals: v < 2 appends (SURFACES_ALLOWED, 3) to the conditions of the list RW 0xDA625C
//     (ordinals 7, 17, 18, 41, 40, 42, 43) and sets 3 parameters; 2 <= v < 6 REPLACES the last parameter of those conditions with
//     (SURFACES_ALLOWED, 0); ordinal 85 with one parameter gets (SIDE, "<This Player>") in front. A parameter count other than the
//     template's: CONDITION_FALSE.
//   * ScriptAction (RW 0x7B68F9): version >= 2: the stored ordinal's template when its NameKey matches, else (ordinal 385 keeps its
//     slot when its TEMPLATE is named BUILD_BASE_BUILDING_PER_TACTICAL_MARKER / _WITH_TACTICAL_MARKER / PER_TACTIC_MARKER: always here) the first of 0 .. 599
//     with the name; no match: NO_OP (5). The type 15 -> 61 rule; v >= 3 reads + 0x41 (`enabled`, else 1); the
//     heals of RW 0x7B6A0F .. 0x7B6FF6 (parameters appended to old records, see Scripts.cpp); a count other than the template's: NO_OP.
// The retail corpus needs no heal (tools/script/script_census.py: every record has the template's count after the re-match).

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class DataChunkInput;
struct DataChunkInfo;

struct ScriptParameter
{
	std::int32_t type = 0; // ZH Parameter::ParameterType; 16 = COORD3D
	std::int32_t intValue = 0;
	float realValue = 0.0f;
	std::string stringValue;
	float x = 0.0f, y = 0.0f, z = 0.0f; // type 16 only
};

struct ScriptCondition
{
	std::int32_t type = 0;
	std::string internalName; // NameKey (v>=4)
	std::vector<ScriptParameter> params;
	std::int32_t flagA = 0, flagB = 0; // v>=5 (B2 m_extraFlagA/B; OpenSAGE Enabled/IsInverted)
	int version = 0;
	int resolved = 0;        ///< the RotWK parser's ordinal (see the header comment); `params` are the resolved parameters
	bool enabled = true;     ///< Condition + 0x4C: flagA != 0 for v >= 5, else 1 (RW 0x7B783F); a disabled condition is skipped (RW 0x60936C)
	bool flagB4D = false;    ///< Condition + 0x4D: flagB != 0 for v >= 5; inverts an ordinal >= 5's answer (RW 0x7ED72C, lane CAMP-1)
};

struct ScriptActionRec
{
	std::int32_t type = 0;
	std::string internalName; // NameKey (v>=2)
	std::vector<ScriptParameter> params;
	std::int32_t tail = 0; // v>=3 (B2 m_tailByte; OpenSAGE Enabled)
	int version = 0;
	int resolved = 5;        ///< the RotWK parser's ordinal (NO_OP when it does not resolve)
	bool enabled = true;     ///< ScriptAction + 0x41: tail != 0 for v >= 3, else 1; ScriptActions::executeAction skips a disabled action (RW 0x7CAFB7)
};

struct OrCondition
{
	std::vector<ScriptCondition> conditions; // AND list
};

struct ScriptGroup;

struct Script
{
	std::string name, comment, conditionComment, actionComment;
	bool isActive = false, isOneShot = false, easy = false, normal = false, hard = false, isSubroutine = false;
	std::int32_t delayEvaluationSeconds = 0; // v>=2
	bool actionsFireSequentially = false;    // v>=3
	bool loopActions = false;
	std::int32_t loopCount = 0;
	std::uint8_t sequentialTargetType = 0;
	std::string sequentialTargetName;
	std::string unknownV4; // v>=4, "ALL" throughout the corpus (meaning UNKNOWN)
	int version = 0;
	std::vector<OrCondition> orConditions;
	std::vector<ScriptActionRec> actions;      // ScriptAction
	std::vector<ScriptActionRec> falseActions; // ScriptActionFalse
};

struct ScriptItem
{
	std::unique_ptr<Script> script;      // exactly one of script / group is set
	std::unique_ptr<ScriptGroup> group;
};

struct ScriptGroup
{
	std::string name;
	bool isActive = false;
	bool isSubroutine = false; // v>=2
	int version = 0;
	std::vector<ScriptItem> items; // children in file order
};

struct ScriptList
{
	std::vector<ScriptItem> items;
	size_t countScripts() const; // recursive
	size_t countGroups() const;  // recursive
};

// .scb extras (spec 1.7.8): ScriptsPlayers v2 and ScriptTeams v1.
struct ScriptsPlayersEntry
{
	std::string name;
	bool hasDict = false;
};

// One PlayerScriptsList chunk: one ScriptList per side, in SidesList order.
struct PlayerScriptsList
{
	int version = 0;
	std::vector<ScriptList> lists;
};

// Parsers registered by the map reader. `userData` of the registered parser is the
// ScriptParseContext below.
struct ScriptParseContext
{
	PlayerScriptsList *playerScripts = nullptr;
	ScriptList *currentList = nullptr;
	std::vector<ScriptGroup *> groupStack; // innermost last
	Script *currentScript = nullptr;
	OrCondition *currentOr = nullptr;
};

namespace ScriptsParse
{
// Registers PlayerScriptsList and its children on `file`.
void registerParsers(DataChunkInput &file, ScriptParseContext *ctx);
} // namespace ScriptsParse
