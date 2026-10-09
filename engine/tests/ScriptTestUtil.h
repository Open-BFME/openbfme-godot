// OpenBFME unit tests: the script fixture helpers shared by the SCRIPT-1 / SCRIPT-2 tests. GPL-3.0.
#pragma once

#include "doctest.h"
#include "LogicTestUtil.h"

#include "GameClient/MapChunks.h"
#include "GameLogic/ScriptEngine/ScriptEngine.h"
#include "GameLogic/ScriptEngine/ScriptTemplates.h"
#include "GameLogic/ScriptEngine/Scripts.h"

#include <memory>
#include <string>
#include <vector>

namespace scripttest
{

inline ScriptParameter sp(int type, const std::string &s)
{
	ScriptParameter p;
	p.type = type;
	p.stringValue = s;
	return p;
}

inline ScriptParameter ip(int type, int v)
{
	ScriptParameter p;
	p.type = type;
	p.intValue = v;
	return p;
}

inline ScriptParameter rp(float v)
{
	ScriptParameter p;
	p.type = 1;
	p.realValue = v;
	return p;
}

inline ScriptCondition cond(const char *name, std::vector<ScriptParameter> params, bool enabled = true)
{
	ScriptCondition c;
	c.internalName = name;
	c.type = c.resolved = ScriptTemplates::findCondition(name);
	REQUIRE_MESSAGE(c.resolved >= 0, name);
	c.params = std::move(params);
	c.enabled = enabled;
	c.version = 5;
	return c;
}

inline ScriptActionRec act(const char *name, std::vector<ScriptParameter> params)
{
	ScriptActionRec a;
	a.internalName = name;
	a.type = a.resolved = ScriptTemplates::findAction(name);
	REQUIRE_MESSAGE(a.resolved >= 0, name);
	a.params = std::move(params);
	a.version = 3;
	return a;
}

inline std::unique_ptr<Script> script(const std::string &name, std::vector<std::vector<ScriptCondition>> ors, std::vector<ScriptActionRec> actions,
	std::vector<ScriptActionRec> falseActions = {}, bool oneShot = false, bool active = true)
{
	auto s = std::make_unique<Script>();
	s->name = name;
	s->isActive = active;
	s->isOneShot = oneShot;
	s->easy = s->normal = s->hard = true;
	for (auto &andList : ors)
	{
		OrCondition o;
		o.conditions = std::move(andList);
		s->orConditions.push_back(std::move(o));
	}
	s->actions = std::move(actions);
	s->falseActions = std::move(falseActions);
	return s;
}

inline ScriptItem item(std::unique_ptr<Script> s)
{
	ScriptItem i;
	i.script = std::move(s);
	return i;
}

inline ScriptItem group(const std::string &name, bool active, bool subroutine, std::vector<ScriptItem> items)
{
	ScriptItem i;
	i.group = std::make_unique<ScriptGroup>();
	i.group->name = name;
	i.group->isActive = active;
	i.group->isSubroutine = subroutine;
	i.group->items = std::move(items);
	return i;
}

// a game with the fixture's players (0 neutral "", 1 Alice, 2 Bob) and one script list per side
struct ScriptGame
{
	logictest::LogicWorld lw;
	std::vector<ScriptList> lists;
	ScriptGame() { lists.resize(3); }
	std::vector<TriggerArea> triggers; ///< lane SCRIPT-2: the map's trigger areas (start() hands them to the engine)
	void start()
	{
		std::vector<ScriptEngine::SideScripts> sides(3);
		const char *names[] = { "", "Alice", "Bob" };
		for (size_t i = 0; i < 3; ++i)
		{
			sides[i].sideName = names[i];
			sides[i].lists.push_back(&lists[i]);
		}
		engine().newGame(sides, triggers.empty() ? nullptr : &triggers, nullptr, false);
	}
	ScriptEngine &engine() { return lw.logic->scriptEngine(); }
	void frame() { lw.logic->runLogicFrame(); }
	std::int32_t counter(const std::string &q) const
	{
		const ScriptEngine::Counter *c = lw.logic->scriptEngine().findCounter(q);
		return c ? c->value : -999;
	}
};

} // namespace scripttest
