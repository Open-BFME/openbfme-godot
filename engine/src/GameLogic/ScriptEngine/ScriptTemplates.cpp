// OpenBFME. GPL-3.0.
// See GameLogic/ScriptEngine/ScriptTemplates.h.

#include "GameLogic/ScriptEngine/ScriptTemplates.h"

namespace
{

struct Registry
{
	std::vector<ScriptTemplate> conditions, actions;

	Registry()
	{
#include "GameLogic/ScriptEngine/ScriptTemplateTables.inc"
		conditions.resize(OPENBFME_SCRIPT_CONDITION_TEMPLATE_COUNT);
		actions.resize(OPENBFME_SCRIPT_ACTION_TEMPLATE_COUNT);
		for (size_t i = 0; i < conditions.size(); ++i)
		{
			conditions[i].index = (int)i;
		}
		for (size_t i = 0; i < actions.size(); ++i)
		{
			actions[i].index = (int)i;
		}
#define OPENBFME_SCRIPT_CONDITION_TEMPLATE(idx, nm, mode, hasCase, count, ...) set(conditions[idx], nm, mode, hasCase, count, __VA_ARGS__);
#define OPENBFME_SCRIPT_ACTION_TEMPLATE(idx, nm, mode, hasCase, count, ...) set(actions[idx], nm, mode, hasCase, count, __VA_ARGS__);
#undef OPENBFME_SCRIPT_CONDITION_TEMPLATE_COUNT
#undef OPENBFME_SCRIPT_ACTION_TEMPLATE_COUNT
#include "GameLogic/ScriptEngine/ScriptTemplateTables.inc"
#undef OPENBFME_SCRIPT_CONDITION_TEMPLATE
#undef OPENBFME_SCRIPT_ACTION_TEMPLATE
	}

	static void set(ScriptTemplate &t, const char *name, int mode, int hasCase, int count, std::vector<int> types)
	{
		t.name = name;
		t.modeMask = mode;
		t.retailCase = hasCase != 0;
		t.parameterTypes = std::move(types);
		t.parameterTypes.resize((size_t)count);
	}
};

const Registry &registry()
{
	static const Registry r;
	return r;
}

int findIn(const std::vector<ScriptTemplate> &v, const std::string &name)
{
	for (const ScriptTemplate &t : v)
	{
		if (!t.name.empty() && t.name == name)
		{
			return t.index;
		}
	}
	return -1;
}

} // namespace

const std::vector<ScriptTemplate> &ScriptTemplates::conditions() { return registry().conditions; }
const std::vector<ScriptTemplate> &ScriptTemplates::actions() { return registry().actions; }
int ScriptTemplates::findCondition(const std::string &name) { return findIn(registry().conditions, name); }
int ScriptTemplates::findAction(const std::string &name) { return findIn(registry().actions, name); }

const ScriptTemplate *ScriptTemplates::condition(int ordinal)
{
	const std::vector<ScriptTemplate> &v = registry().conditions;
	return ordinal >= 0 && ordinal < (int)v.size() ? &v[(size_t)ordinal] : nullptr;
}

const ScriptTemplate *ScriptTemplates::action(int ordinal)
{
	const std::vector<ScriptTemplate> &v = registry().actions;
	return ordinal >= 0 && ordinal < (int)v.size() ? &v[(size_t)ordinal] : nullptr;
}
