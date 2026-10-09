// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// The construction commands of the lockstep command path (lane BUILD-1): MSG_FOUNDATION_CONSTRUCT, MSG_DOZER_CONSTRUCT, MSG_DOZER_CANCEL_CONSTRUCT, MSG_RESUME_CONSTRUCTION.
// One handler per type on GameLogicDispatch (the dispatcher's registerHandler: a type executed elsewhere is a logic error). Pure functions of (message, logic state).
//
// TARGET FACTS (RotWK game.dat, S-001 caveat; the dispatcher RW 0x779A3D, its tables RW 0x77D0B7 / RW 0x77D127):
//   * the group of every message of the 1000 .. 1999 range is the issuing player's selection minus the objects the player does not control (RW 0x779AEF, 0x76ED16).
//   * MSG_FOUNDATION_CONSTRUCT 1049 (RW 0x77A91B): args { int templateId (a 16 bit id, RW 0x6CFE6C finds the template), location, real angle }. The builder is the first object of the
//     group whose template has KindOf BASE_FOUNDATION (RW 0x77A945: byte 0x115 bit 0 of the template); the player is its controlling player; the template, the builder and the player must
//     exist and Player::canBuild (RW 0x6AAA2E) must accept; then BuildAssistant slot 0x38 (RW 0x797796) with (builder, template, &location, angle, player): a plot's FoundationAIUpdate
//     constructs it (Construction::constructOnPlot). The user interface response (voice, the placement mode ends) follows (RW 0x77AA03).
//   * MSG_DOZER_CONSTRUCT 1050 (RW 0x77B011): the same args; the builder is the first object of the group (RW 0x779A21), whose template must be KindOf DOZER and whose player must be
//     0 at +0x770; Player::canBuild; BuildAssistant slot 0x38: a DOZER builder's AI constructs it (DozerAIUpdate::construct, RW vslot 0x1F8).
//   * MSG_DOZER_CANCEL_CONSTRUCT 1051 (RW 0x77B1A0): no args; the first object of the group must be controlled by the issuing player, not dead, and under construction (status 2); the price
//     paid (object + 0x33C) goes back to the player (truncated, RW 0x77B22F: ftol, RW 0x7B18B8 deposit) and the object is destroyed (RW 0x77B29C: status, model condition and kill
//     with damage type 8).
//   * MSG_RESUME_CONSTRUCTION 1067 (RW 0x77AE9F): arg { object id of the structure }; the group's dozers resume (group call RW 0x77241C). MSG_DO_REPAIR 1066 (RW 0x77AE6B): the same
//     arg shape, the group's dozers repair (RW 0x7723E6).
//   * the Dozer construct body (RW 0x88C217) calls the BuildAssistant legality at RW 0x88C2A5 with flags 0x49F and rejects a nonzero result: nothing is created, paid or changed (DozerAIUpdate::siteIsLegal,
//     asked before the builder is told to idle). The plot path (RW 0x858701) has no such call.
// INFERENCE (stop S-305): the checks the port adds are BuildAssistant::canMakeUnit (the builder's CommandSet offers the template, the player can afford it, command points, maximum
// count) in place of the unread body of RW 0x6AAA2E. MSG_DO_REPAIR's
// health effect needs the body healing of the combat lane: the repair message is counted, the dozer walks to the structure and stays (reported).

#pragma once

#include "GameLogic/GameMessage.h"
#include "GameLogic/GameLogicDispatch.h"

#include <string>
#include <vector>

class GameLogic;

class BuildCommands
{
public:
	// registers the handlers (a type another lane already registered is a logic error)
	void registerHandlers(GameLogicDispatch &dispatcher);

	struct Stats
	{
		unsigned long long foundations = 0, dozerBuilds = 0, cancels = 0, resumes = 0, repairs = 0, wallSpans = 0, repairsUnexecuted = 0, refused = 0, rejected = 0;
	};
	const Stats &stats() const { return m_stats; }
	// the reasons a command was refused (text), for tests and reports
	const std::vector<std::string> &refusals() const { return m_refusals; }
	static std::vector<std::string> acceptanceStops();

private:
	bool foundationConstruct(GameLogic &logic, const GameMessage &m);
	bool dozerConstruct(GameLogic &logic, const GameMessage &m);
	bool wallSpan(GameLogic &logic, const GameMessage &m);
	bool dozerCancel(GameLogic &logic, const GameMessage &m);
	bool resumeConstruction(GameLogic &logic, const GameMessage &m);
	bool repair(GameLogic &logic, const GameMessage &m);
	void refuse(const GameMessage &m, const std::string &why);

	Stats m_stats;
	std::vector<std::string> m_refusals;
};
