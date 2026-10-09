// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
// See GameLogic/ScriptEngine/Scripts.h for sources and the recorded acceptance stop.

#include "GameLogic/ScriptEngine/Scripts.h"

#include "Common/DataChunk.h"
#include "GameLogic/ScriptEngine/ScriptTemplates.h"
#include "GameLogic/BitFlags.h"

#include <cctype>

namespace
{

// Parameter::ParameterType values the resolution uses (ZH Scripts.h enum order, as the RotWK templates store them)
constexpr int PARAM_INT = 0, PARAM_REAL = 1, PARAM_BOOLEAN = 8, PARAM_SIDE = 11, PARAM_OBJECT_TYPE = 15, PARAM_AI_MOOD = 20, PARAM_SURFACES_ALLOWED = 37,
			  PARAM_PERCENT = 52, PARAM_OBJECT_TYPE_LIST = 61;

ScriptParameter makeParameter(int type, int intValue)
{
	// RW 0x7B3E6A Parameter(type, int): the real 0, the string empty
	ScriptParameter p;
	p.type = type;
	p.intValue = intValue;
	return p;
}

// RW 0x7B6A0F .. 0x7B7809 (the reads of both parsers): a type 15 (OBJECT_TYPE) parameter in a slot whose template type is 61
// (OBJECT_TYPE_LIST) becomes 61 (RW 0x7B331B)
void convertObjectTypeParameters(std::vector<ScriptParameter> &params, const ScriptTemplate *t)
{
	if (!t)
	{
		return;
	}
	for (size_t i = 0; i < params.size() && i < t->parameterTypes.size(); ++i)
	{
		if (params[i].type == PARAM_OBJECT_TYPE && t->parameterTypes[i] == PARAM_OBJECT_TYPE_LIST)
		{
			params[i].type = PARAM_OBJECT_TYPE_LIST;
		}
	}
}

// RW 0x7B776D: the condition's ordinal (see Scripts.h)
void resolveCondition(ScriptCondition &c)
{
	bool match = false;
	int type = c.type;
	if (c.version >= 4)
	{
		const ScriptTemplate *stored = ScriptTemplates::condition(type);
		if (stored && !stored->name.empty() && stored->name == c.internalName)
		{
			match = true;
		}
		else
		{
			const int byName = ScriptTemplates::findCondition(c.internalName);
			if (byName >= 0)
			{
				type = byName;
				match = true;
			}
		}
	}
	// the parameters are type-converted against the template of the stored (or re-matched) ordinal (RW 0x7B7811: local_c)
	convertObjectTypeParameters(c.params, ScriptTemplates::condition(match ? type : c.type));
	if (!match)
	{
		c.resolved = 0; // retail also deletes the parameters; CONDITION_FALSE never reads them, the record keeps them for reports
		return;
	}
	// the old-file heals (RW 0x7B78B7 .. 0x7B7A0A); the list RW 0xDA625C is { 7, 17, 18, 41, 40, 42, 43, -1 }
	static const int kSurfaceConditions[] = { 7, 17, 18, 41, 40, 42, 43 };
	bool surfaceType = false;
	for (int t : kSurfaceConditions)
	{
		surfaceType = surfaceType || t == type;
	}
	if (surfaceType)
	{
		if (c.version < 2)
		{
			c.params.push_back(makeParameter(PARAM_SURFACES_ALLOWED, 3));
			c.params.resize(3);
		}
		else if (c.version < 6 && !c.params.empty())
		{
			c.params.back() = makeParameter(PARAM_SURFACES_ALLOWED, 0);
		}
	}
	if (type == 85 && c.params.size() == 1)
	{
		ScriptParameter side = makeParameter(PARAM_SIDE, 0);
		side.stringValue = "<This Player>";
		c.params.insert(c.params.begin(), side);
	}
	const ScriptTemplate *t = ScriptTemplates::condition(type);
	if (!t || t->parameterTypes.size() != c.params.size())
	{
		c.resolved = 0; // retail also deletes the parameters; CONDITION_FALSE never reads them, the record keeps them for reports
		return;
	}
	c.resolved = type;
}

// RW 0x7B68F9: the action's ordinal (see Scripts.h)
void resolveAction(ScriptActionRec &a)
{
	constexpr int NO_OP = 5;
	int type = a.type;
	if (a.version >= 2)
	{
		const ScriptTemplate *stored = ScriptTemplates::action(type);
		const bool storedMatches = stored && !stored->name.empty() && stored->name == a.internalName;
		if (!storedMatches)
		{
			// ordinal 385 keeps its slot when ITS TEMPLATE's name is one of the marker spellings (RW 0x7B6992 / 0x7B69AE compare the template's
			// internal name with the strings RW 0xC35C5C, 0xC35C1C, 0xC35C3C): in this binary template 385 is BUILD_BASE_BUILDING_PER_TACTICAL_MARKER,
			// so a record stored at 385 stays there whatever name it carries (the parameter count still decides)
			const bool keep385 = type == 385 && stored &&
				(stored->name == "BUILD_BASE_BUILDING_PER_TACTICAL_MARKER" || stored->name == "BUILD_BASE_BUILDING_WITH_TACTICAL_MARKER" ||
					stored->name == "BUILD_BASE_BUILDING_PER_TACTIC_MARKER");
			if (!keep385)
			{
				const int byName = ScriptTemplates::findAction(a.internalName);
				if (byName < 0)
				{
					a.resolved = NO_OP; // (the parameters stay: NO_OP never reads them)
					return;
				}
				type = byName;
			}
		}
	}
	const ScriptTemplate *t = ScriptTemplates::action(type);
	convertObjectTypeParameters(a.params, t);
	std::vector<ScriptParameter> &p = a.params;
	const size_t n = p.size();
	auto append = [&p](std::initializer_list<int> types, int value) {
		for (int ty : types)
		{
			p.push_back(makeParameter(ty, value));
		}
	};
	// the heals, RW 0x7B6A49 .. 0x7B6FF6 (the values pushed with each RW 0x7B3E6A call)
	switch (type)
	{
	case 14: // MOVE_CAMERA_TO
		if (n == 3) append({ PARAM_REAL, PARAM_REAL }, 0);
		break;
	case 17: // MOVE_CAMERA_ALONG_SPLINE_PATH
	case 390: // MOVE_CAMERA_LOCATOR_ALONG_SPLINE_PATH
	case 185: // CAMERA_LOOK_TOWARD_OBJECT (3 -> 5, then 5 -> 6)
		if (type != 185 && n == 3)
		{
			append({ PARAM_REAL, PARAM_REAL, PARAM_REAL }, 0);
		}
		else
		{
			if (type == 185 && n == 3)
			{
				append({ PARAM_REAL, PARAM_REAL }, 0);
			}
			if (p.size() == 5)
			{
				append({ PARAM_REAL }, 0);
			}
		}
		break;
	case 18: case 19: case 120: case 121: case 340: case 346: // ROTATE / RESET / ZOOM / PITCH_CAMERA, ROTATE_CAMERA_LOCKED, ROLL_CAMERA
		if (n == 2) append({ PARAM_REAL, PARAM_REAL }, 0);
		break;
	case 26: case 27: // CAMERA_MOD_SET_FINAL_ZOOM / _PITCH
		if (n == 1) append({ PARAM_PERCENT, PARAM_PERCENT }, 0);
		break;
	case 36: // TEAM_FOLLOW_WAYPOINTS
		if (n == 3) append({ PARAM_BOOLEAN }, 0);
		break;
	case 45: case 46: // NAMED_SET_ATTITUDE / TEAM_SET_ATTITUDE: an INT mood becomes AI_MOOD with the same value
		if (n >= 2 && p[1].type == PARAM_INT)
		{
			p[1] = makeParameter(PARAM_AI_MOOD, p[1].intValue);
		}
		break;
	case 82: // MOVIE_PLAY_FULLSCREEN: (BOOLEAN, 0)
		if (n == 1) append({ PARAM_BOOLEAN }, 0);
		break;
	case 85: // SPEECH_PLAY: (BOOLEAN, 1)
		if (n == 1) append({ PARAM_BOOLEAN }, 1);
		break;
	case 93: case 148: // MAP_REVEAL_AT_WAYPOINT / MAP_SHROUD_AT_WAYPOINT
		if (n == 2) append({ PARAM_SIDE }, 0);
		break;
	case 105: case 149: case 233: case 234: // MAP_REVEAL_ALL, MAP_SHROUD_ALL, MAP_REVEAL_ALL_PERM, MAP_REVEAL_ALL_UNDO_PERM
		if (n == 0) append({ PARAM_SIDE }, 0);
		break;
	case 122: // CAMERA_FOLLOW_NAMED
		if (n == 2) append({ PARAM_REAL }, 0);
		break;
	case 210: // CAMERA_LOOK_TOWARD_WAYPOINT
		if (n == 2) append({ PARAM_REAL, PARAM_REAL, PARAM_BOOLEAN }, 0);
		else if (n == 4) append({ PARAM_BOOLEAN }, 0);
		break;
	case 254: // SKIRMISH_BUILD_BASE_DEFENSE_FRONT: one parameter -> none, the FLANK action when it was non-zero
		if (n == 1)
		{
			const bool flank = p[0].intValue != 0;
			p.clear();
			if (flank)
			{
				type = 258;
				t = ScriptTemplates::action(type);
			}
		}
		break;
	case 255: // SKIRMISH_FIRE_SPECIAL_POWER_AT_MOST_COST: (SIDE "<This Player>") in front
		if (n == 1)
		{
			ScriptParameter side = makeParameter(PARAM_SIDE, 0);
			side.stringValue = "<This Player>";
			p.insert(p.begin(), side);
		}
		break;
	case 434: case 435: // UNIT_ / TEAM_SET_MODELCONDITION_FOR_DURATION
		if (n == 3) append({ PARAM_PERCENT }, 1);
		break;
	default:
		break;
	}
	if (!t || t->parameterTypes.size() != p.size())
	{
		a.resolved = NO_OP;
		return;
	}
	a.resolved = type;
}

bool equalsNoCase(const char *a, const std::string &b)
{
	size_t i = 0;
	for (; a[i] && i < b.size(); ++i)
	{
		if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i]))
		{
			return false;
		}
	}
	return a[i] == 0 && i == b.size();
}

ScriptParameter readParameter(DataChunkInput &file)
{
	ScriptParameter p;
	p.type = file.readInt();
	if (p.type == 16) // COORD3D (spec 1.7.8)
	{
		p.x = file.readReal();
		p.y = file.readReal();
		p.z = file.readReal();
	}
	else
	{
		p.intValue = file.readInt();
		p.realValue = file.readReal();
		p.stringValue = file.readAsciiString();
	}
	if (p.type == 41) // OBJECT_STATUS: RW 0x7B66F5 -> RW 0x68CE4F, the index of the name in TheObjectStatusNames (no case), -1 when none
	{
		p.intValue = -1;
		for (int i = 0; TheObjectStatusNames[i]; ++i)
		{
			if (equalsNoCase(TheObjectStatusNames[i], p.stringValue))
			{
				p.intValue = i;
				break;
			}
		}
	}
	return p;
}

std::vector<ScriptParameter> readParams(DataChunkInput &file)
{
	std::int32_t n = file.readInt();
	if (n < 0 || n > 4096)
	{
		throw MapParseError("script parameter count " + std::to_string(n) + " out of range");
	}
	std::vector<ScriptParameter> params;
	params.reserve((size_t)n);
	for (std::int32_t i = 0; i < n; ++i)
	{
		params.push_back(readParameter(file));
	}
	return params;
}

// Appends an item to the innermost open container (a group, else the list).
std::vector<ScriptItem> &container(ScriptParseContext *ctx)
{
	if (!ctx->groupStack.empty())
	{
		return ctx->groupStack.back()->items;
	}
	if (!ctx->currentList)
	{
		throw MapParseError("script chunk outside a ScriptList");
	}
	return ctx->currentList->items;
}

bool parsePlayerScriptsList(DataChunkInput &file, DataChunkInfo *info, void *userData)
{
	ScriptParseContext *ctx = (ScriptParseContext *)userData;
	ctx->playerScripts->version = info->version;
	return file.parse(userData);
}

bool parseScriptList(DataChunkInput &file, DataChunkInfo *, void *userData)
{
	ScriptParseContext *ctx = (ScriptParseContext *)userData;
	ctx->playerScripts->lists.emplace_back();
	ctx->currentList = &ctx->playerScripts->lists.back();
	ctx->groupStack.clear();
	bool ok = file.parse(userData);
	ctx->currentList = nullptr;
	return ok;
}

// ZH ScriptGroup::ParseGroupDataChunk (Scripts.cpp:889-900) + BFME2 ScriptGroupWriteGroup.cpp:90.
bool parseScriptGroup(DataChunkInput &file, DataChunkInfo *info, void *userData)
{
	ScriptParseContext *ctx = (ScriptParseContext *)userData;
	std::unique_ptr<ScriptGroup> g = std::make_unique<ScriptGroup>();
	g->version = info->version;
	g->name = file.readAsciiString();
	g->isActive = file.readByte() != 0;
	if (info->version >= 2)
	{
		g->isSubroutine = file.readByte() != 0;
	}
	ScriptGroup *raw = g.get();
	ScriptItem item;
	item.group = std::move(g);
	container(ctx).push_back(std::move(item));
	ctx->groupStack.push_back(raw);
	bool ok = file.parse(userData);
	ctx->groupStack.pop_back();
	return ok;
}

// ZH Script::ParseScriptFromListDataChunk (:1264-1290) + BFME2 ScriptSubRecordWrite.cpp:38-45.
bool parseScript(DataChunkInput &file, DataChunkInfo *info, void *userData)
{
	ScriptParseContext *ctx = (ScriptParseContext *)userData;
	std::unique_ptr<Script> s = std::make_unique<Script>();
	s->version = info->version;
	s->name = file.readAsciiString();
	s->comment = file.readAsciiString();
	s->conditionComment = file.readAsciiString();
	s->actionComment = file.readAsciiString();
	s->isActive = file.readByte() != 0;
	s->isOneShot = file.readByte() != 0;
	s->easy = file.readByte() != 0;
	s->normal = file.readByte() != 0;
	s->hard = file.readByte() != 0;
	s->isSubroutine = file.readByte() != 0;
	if (info->version >= 2)
	{
		s->delayEvaluationSeconds = file.readInt();
	}
	if (info->version >= 3)
	{
		s->actionsFireSequentially = file.readByte() != 0;
		s->loopActions = file.readByte() != 0;
		s->loopCount = file.readInt();
		s->sequentialTargetType = file.readByte();
		s->sequentialTargetName = file.readAsciiString();
	}
	if (info->version >= 4)
	{
		s->unknownV4 = file.readAsciiString();
	}
	Script *raw = s.get();
	ScriptItem item;
	item.script = std::move(s);
	container(ctx).push_back(std::move(item));
	ctx->currentScript = raw;
	bool ok = file.parse(userData);
	ctx->currentScript = nullptr;
	return ok;
}

bool parseOrCondition(DataChunkInput &file, DataChunkInfo *, void *userData)
{
	ScriptParseContext *ctx = (ScriptParseContext *)userData;
	if (!ctx->currentScript)
	{
		throw MapParseError("OrCondition outside a Script");
	}
	ctx->currentScript->orConditions.emplace_back();
	ctx->currentOr = &ctx->currentScript->orConditions.back();
	bool ok = file.parse(userData);
	ctx->currentOr = nullptr;
	return ok;
}

// ZH Condition::ParseConditionDataChunk (:1688-1760) + BFME2 ConditionWriteDataChunk.cpp:122-160.
bool parseCondition(DataChunkInput &file, DataChunkInfo *info, void *userData)
{
	ScriptParseContext *ctx = (ScriptParseContext *)userData;
	if (!ctx->currentOr)
	{
		throw MapParseError("Condition outside an OrCondition");
	}
	ScriptCondition c;
	c.version = info->version;
	c.type = file.readInt();
	if (info->version >= 4)
	{
		c.internalName = file.readNameKey();
	}
	c.params = readParams(file);
	if (info->version >= 5)
	{
		c.flagA = file.readInt();
		c.flagB = file.readInt();
		c.enabled = c.flagA != 0;
		c.flagB4D = c.flagB != 0;
	}
	resolveCondition(c);
	ctx->currentOr->conditions.push_back(std::move(c));
	return true;
}

// ZH ScriptAction::ParseAction (:2142-2240) + BFME2 ScriptActionWriteAction.cpp:86.
bool parseAction(DataChunkInput &file, DataChunkInfo *info, void *userData, bool falseList)
{
	ScriptParseContext *ctx = (ScriptParseContext *)userData;
	if (!ctx->currentScript)
	{
		throw MapParseError("ScriptAction outside a Script");
	}
	ScriptActionRec a;
	a.version = info->version;
	a.type = file.readInt();
	if (info->version >= 2)
	{
		a.internalName = file.readNameKey();
	}
	a.params = readParams(file);
	if (info->version >= 3)
	{
		a.tail = file.readInt();
		a.enabled = a.tail != 0;
	}
	resolveAction(a);
	(falseList ? ctx->currentScript->falseActions : ctx->currentScript->actions).push_back(std::move(a));
	return true;
}

bool parseScriptAction(DataChunkInput &f, DataChunkInfo *i, void *u) { return parseAction(f, i, u, false); }
bool parseScriptActionFalse(DataChunkInput &f, DataChunkInfo *i, void *u) { return parseAction(f, i, u, true); }

size_t countIn(const std::vector<ScriptItem> &items, bool scripts)
{
	size_t n = 0;
	for (const ScriptItem &it : items)
	{
		if (it.script)
		{
			n += scripts ? 1 : 0;
		}
		else if (it.group)
		{
			n += scripts ? 0 : 1;
			n += countIn(it.group->items, scripts);
		}
	}
	return n;
}

} // namespace

size_t ScriptList::countScripts() const { return countIn(items, true); }
size_t ScriptList::countGroups() const { return countIn(items, false); }

namespace ScriptsParse
{
void registerParsers(DataChunkInput &file, ScriptParseContext *ctx)
{
	file.registerParser("PlayerScriptsList", "", parsePlayerScriptsList, ctx);
	file.registerParser("ScriptList", "PlayerScriptsList", parseScriptList, ctx);
	file.registerParser("ScriptGroup", "ScriptList", parseScriptGroup, ctx);
	file.registerParser("ScriptGroup", "ScriptGroup", parseScriptGroup, ctx);
	file.registerParser("Script", "ScriptList", parseScript, ctx);
	file.registerParser("Script", "ScriptGroup", parseScript, ctx);
	file.registerParser("OrCondition", "Script", parseOrCondition, ctx);
	file.registerParser("Condition", "OrCondition", parseCondition, ctx);
	file.registerParser("ScriptAction", "Script", parseScriptAction, ctx);
	file.registerParser("ScriptActionFalse", "Script", parseScriptActionFalse, ctx);
}
} // namespace ScriptsParse
