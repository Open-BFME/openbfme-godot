// OpenBFME. GPL-3.0.
// ScriptActions, PORT 17 (issue #73): SET_COUNTER_TO_THREAT_FINDER_THREAT (action ordinal 556, RW 0x7C68AA) - a counter takes the threat of a
// named threat area for one player and one relationship.
//
// TARGET FACTS (RotWK game.dat, caveat S-001). The parameters, from the committed template table (ScriptTemplateTables.inc: 556, four parameters
// {4 COUNTER, 67, 66, 11 PLAYER; the two middle ones are the threat area's name and "Enemies" / "Allies"}):
//   * the counter by name (RW 0x60817A, the port's ScriptEngine::counter: get or create); a counter that is not there is created empty as retail's is
//     (INFERENCE BFME2 0x003C5E10's bfmeCounter pin 0x0020874B);
//   * the player of parameter 3 by its mask (RW 0x758F7C, the port's ScriptConditions::players): retail takes the mask (INFERENCE BFME2 0x00357475)
//     then the ONE player whose bit equals it (getPlayerFromMask, RW 0x6A85B6, the port's playerOfMask): a mask of several players or of none is no
//     player and the action does nothing;
//   * parameter 2 selects the relationship: "Enemies" is 0, "Allies" is 1, anything else does nothing (INFERENCE BFME2 0x003C5E10: the two exact
//     compares, the else returns);
//   * the answer (RW 0x7EE1F4 -> RW 0x7EE166, the port's AIThreatFinder sum: retail has ONE finder class, and the map object's finder is the same
//     object the skirmish AI's AITarget holds, GameLogic/System/ThreatFinder.h);
//   * the finder by the name of parameter 1 in TheThreatFinderManager (RW 0x7EE260); found, the counter's value becomes the answer's first float
//     (RW 0x7EE1F4, truncated to an int as the store does); not found, retail appends "WARNING - Threat Finder not found during script execution: "
//     + the name + "." to its debug messages and leaves the counter alone (INFERENCE BFME2 0x003C5E10) - the port records the same text through
//     ScriptEngine::note, which is where its retail warnings go (S-1180).
// INFERENCE: retail's own warning goes to the debug log and not to the script counter; the port's note reaches the logic report, the same place
// every other script warning of this port reaches.

#include "GameLogic/ScriptEngine/ScriptActions.h"

#include "Common/Player.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/ScriptEngine/ScriptConditions.h"
#include "GameLogic/ScriptEngine/ScriptEngine.h"
#include "GameLogic/SimMath.h"
#include "GameLogic/System/ThreatFinder.h"

namespace
{
const ScriptParameter &param(const ScriptActionRec &a, size_t i)
{
	static const ScriptParameter kEmpty;
	return i < a.params.size() ? a.params[i] : kEmpty;
}
} // namespace

// RW 0x7C68AA: SET_COUNTER_TO_THREAT_FINDER_THREAT (case 556)
bool ScriptActions::executeThreatFinderAction(ScriptEngine &engine, const ScriptActionRec &a, const std::string &name)
{
	if (name != "SET_COUNTER_TO_THREAT_FINDER_THREAT")
	{
		return false;
	}
	GameLogic &logic = engine.logic();
	const std::string counterName = param(a, 0).stringValue;
	const std::string finderName = param(a, 1).stringValue;
	const std::string threatType = param(a, 2).stringValue;
	int mode = -1;
	if (threatType == "Enemies") // INFERENCE BFME2 0x003C5E10: the exact compare, 0
	{
		mode = 0;
	}
	else if (threatType == "Allies") // 1
	{
		mode = 1;
	}
	else
	{
		return true; // any other text: retail returns without touching the counter
	}
	const Player *player = ScriptConditions::playerOfMask(engine, param(a, 3).stringValue); // RW 0x6A85B6
	if (!player)
	{
		return true;
	}
	ThreatFinder *finder = logic.threatFinders().find(logic, finderName); // RW 0x7EE260
	if (!finder)
	{
		engine.note("WARNING - Threat Finder not found during script execution: " + finderName + ".");
		return true;
	}
	const AIThreatFinder::ThreatRecord answer = finder->threatForPlayer(logic, *player, mode); // RW 0x7EE1F4 / 0x7EE166
	engine.counter(counterName).value = SimMath::truncToInt32(answer.v[0]);
	return true;
}
