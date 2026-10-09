// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// CommandList and GameLogicDispatch (ZH Source/Common/MessageStream/CommandXlat / GameLogic/System/GameLogicDispatch.cpp, RotWK GameLogic::
// logicMessageDispatcher RW 0x779A3D), lane PROD-1: the lockstep command path. A frame's commands are GameMessages in a CommandList; GameLogic's phase 1
// row `commandList` (RW 0x62E8D4: every pending command goes to the dispatcher, then the list resets) hands them to the dispatcher in order. The dispatcher is
// a pure function of (message, logic state): it reads no clock, no local player, no random number and touches nothing outside the logic.
//
// TARGET FACTS (RotWK game.dat, caveat S-001; details in workspace/rebuild/specs/production.md):
//   * a message of type 1000 .. 1999 (except 0x44A, 0x447) gets the issuing player's CURRENT SELECTION as an AI group (RW 0x6AAB85) with the objects the
//     player does not control removed (RW 0x76ED16; EVACUATE_CONTESTERS and EXIT use other filters); the producer of a production command is the FIRST object
//     of that group (RW 0x779A21). There is no object id in the message.
//   * MSG_QUEUE_UNIT_CREATE (RW 0x77A764): args { byte fromBuildIndex, int templateId | buildIndex, int value30, byte batch, byte secondary }; the template is found
//     by its 16-bit id (RW 0x6CFE6C) and replaced by its final override; the production interface is the secondary one when `secondary` and the object has one
//     (RW 0x68C327 with ProductionUpdate vtable slot 30), else the first; productionID = requestUniqueUnitID (slot 2); queueCreateUnit(template, buildIndex,
//     productionID, value30, batch, "", false) (slot 8). No owner, money or enabled check here: queueCreateUnit does them through canMakeUnit.
//   * MSG_CANCEL_UNIT_CREATE (RW 0x77A844): args { byte byIndex, int index | templateId, byte all }; the producer must be controlled by the issuing player; the
//     first production interface only; byIndex -> slot 12, else slot 5 cancelUnitCreateByType(template WITHOUT the final override, all): the LAST queued matching
//     entries go first, up to five when `all`. (BFME1 / ZH cancel by production id.)
//   * MSG_SET_RALLY_POINT (RW 0x77A26C): args { object id, Coord3D, byte global, object id target }; the building is found by id and NOT checked for ownership;
//     with `global` and no target the point goes to every AUTO_RALLYPOINT object (RW 0x7798D5); else RW 0x779544: a target object's position replaces the
//     point, the exit interface's setRallyPoint takes it (the pathfinder's validation RW 0x6F5BB0 / 0x6EA88A is not ported, S-202).
//   * the player's selection is changed by MSG_CREATE_SELECTED_GROUP (args: byte createNewGroup, object ids), MSG_REMOVE_FROM_SELECTED_GROUP (object ids, RW 0x77A147)
//     and MSG_DESTROY_SELECTED_GROUP; it lives in Player::selection() (hashed) and nowhere else; the other selection
//     messages (teams, area selection, formation) are not ported (S-208).
// Every message type the dispatcher does not execute is COUNTED by type and reported (stop S-208), never dropped silently.
//
// ONE dispatch path (lane MOVE-1 joined it): the types above are executed here; other lanes register a handler for the types they execute (registerHandler: one
// handler per type, a second registration or one for a type executed here is a logic error) and the dispatcher calls it in the same defined order, inside the
// same `commandList` row. The movement lane's handlers (GameLogic/AI/AICommands.h) are the first user.

#pragma once

#include "GameLogic/GameMessage.h"

#include <functional>
#include <map>
#include <string>
#include <vector>

class GameLogic;
class Player;
class Object;

// ZH CommandList: the messages of the coming logic frame, in order
class CommandList
{
public:
	void append(GameMessage message) { m_messages.push_back(std::move(message)); }
	const std::vector<GameMessage> &messages() const { return m_messages; }
	void reset() { m_messages.clear(); }

private:
	std::vector<GameMessage> m_messages;
};

class GameLogicDispatch
{
public:
	explicit GameLogicDispatch(GameLogic &logic)
		: m_logic(logic)
	{
	}
	// installs the `commandList` phase row of `logic` (the list must outlive the logic's use of it)
	void attach(CommandList &list);
	// one message (RW 0x779A3D)
	void dispatch(const GameMessage &message);

	// a handler returns false when it found the message malformed (counted by the lane, reported through errors())
	typedef std::function<bool(GameLogic &, const GameMessage &)> Handler;
	// registers the executor of `type` for `lane` (a name for reports); std::logic_error when the type is already taken (here or by another lane)
	void registerHandler(int type, const char *lane, Handler handler);
	// the lane that executes `type` ("PROD-1" for the types of this file, "" when nobody does)
	std::string handlerLane(int type) const;

	// the player's current selection, objects that are gone dropped (ids in selection order)
	std::vector<ObjectID> selection(int playerIndex) const;
	// message types seen but not executed, with counts (stop S-208)
	const std::map<int, unsigned long long> &unhandled() const { return m_unhandled; }
	std::vector<std::string> acceptanceStops() const;
	// errors the dispatcher found (a malformed message, an unknown template id): never dropped
	const std::vector<std::string> &errors() const { return m_errors; }

private:
	Object *firstSelected(int playerIndex);
	void queueUnitCreate(const GameMessage &m);
	void cancelUnitCreate(const GameMessage &m);
	void setRallyPoint(const GameMessage &m);
	void createSelectedGroup(const GameMessage &m);
	void removeFromSelectedGroup(const GameMessage &m);
	void error(const GameMessage &m, const std::string &text);
	static bool executedHere(int type);

	struct Registered
	{
		std::string lane;
		Handler handler;
	};
	GameLogic &m_logic;
	std::map<int, Registered> m_handlers;
	std::map<int, unsigned long long> m_unhandled;
	std::vector<std::string> m_errors;
};
