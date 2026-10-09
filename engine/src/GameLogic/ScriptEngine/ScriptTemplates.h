// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0 (ScriptEngine::init, ConditionTemplate / ActionTemplate).
//
// The script condition and action template registries of RotWK (lane SCRIPT-1): the binary's FULL registries (PLAN rule 6), generated from the
// image by tools/script/extract_script_templates.py into ScriptTemplateTables.inc.
//
// TARGET FACTS (RotWK game.dat, caveat S-001):
//   * ScriptEngine's constructor (RW 0x607D6C) makes 600 action Templates at this + 0x20 and 203 condition Templates at this + 0x12C20 (0x80 bytes each,
//     constructor RW 0x7B44F7: + 0x00 = 1, the parameter types zeroed); ScriptEngine::init (RW 0x605755) fills them with RW 0x7D5270 (actions) and
//     RW 0x7D01C0 (conditions). Slots the init never touches keep an empty internal name.
//   * Template + 0x00 is a game-mode mask: ScriptEngine evaluates a condition (RW 0x6092A9) and runs an action (RW 0x60C1C9) only when
//     `mask & mode` is not zero, where mode = 2 in a War of the Ring game (GameLogic + 0x110 == 8, or == 9 with + 0x114 == 3: RW 0x5DC46C) and 1
//     otherwise (RW 0x602FDD).
//   * The dispatch switches: ScriptConditions::evaluateCondition (RW 0x7EB7CD) has cases for the ordinals the table marks `retailCase`; every other
//     ordinal >= 5 evaluates false. ScriptActions::executeAction (RW 0x7CAFA5) likewise (an ordinal without a case does nothing).
//   * By name: the parsers re-match a stored ordinal by the template's NameKey (RW 0x7B776D: ordinals 0 .. 202; RW 0x7B68F9: 0 .. 599), so the FIRST
//     template with a name wins (MAP_REVEAL_IN_TRIGGER is registered at 552 and 553).

#pragma once

#include <string>
#include <vector>

struct ScriptTemplate
{
	int index = -1;
	std::string name;        ///< the internal name ("" for a slot the binary never fills)
	int modeMask = 1;        ///< Template + 0x00
	bool retailCase = false; ///< the evaluator / executor switch has a case for this ordinal
	std::vector<int> parameterTypes;
};

namespace ScriptTemplates
{
// every slot of the registry (index = ordinal): 203 conditions, 600 actions
const std::vector<ScriptTemplate> &conditions();
const std::vector<ScriptTemplate> &actions();
// the ordinal of the first template with this internal name, -1 when none (the parsers' by-name search)
int findCondition(const std::string &name);
int findAction(const std::string &name);
// the template of an ordinal (null outside the registry)
const ScriptTemplate *condition(int ordinal);
const ScriptTemplate *action(int ordinal);
} // namespace ScriptTemplates
