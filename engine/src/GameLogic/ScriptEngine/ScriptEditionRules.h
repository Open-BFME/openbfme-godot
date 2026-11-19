// OpenBFME. GPL-3.0.
#pragma once

#include <optional>
#include <string>

class ScriptEngine;
struct ScriptActionRec;
struct ScriptCondition;

namespace ScriptEditionRules
{
// BFME1 data adds these script cases. An unhandled case continues through the shared dispatcher.
bool execute(ScriptEngine &engine, const ScriptActionRec &action, const std::string &name);
std::optional<bool> evaluate(ScriptEngine &engine, const ScriptCondition &condition, const std::string &name);
}
