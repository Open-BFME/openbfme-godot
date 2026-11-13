// OpenBFME. GPL-3.0.
// BFME1 1.03's AIPlayer/AISkirmishPlayer variant, driven by the mounted
// PlayerAITypes libraries and their TeamPrototype production scripts.
#pragma once

#include "GameLogic/ObjectTypes.h"
#include "Common/Team.h"
#include <map>
#include <set>
#include <string>
#include <vector>

class GameLogic;
class Object;
class Player;
class ScriptEngine;
class StateHasher;
class Xfer;
class AIStateMachine;
class ThingTemplate;
struct ScriptActionRec;
struct ScriptCondition;
struct Coord3D;

class Bfme1SkirmishAI
{
public:
	explicit Bfme1SkirmishAI(GameLogic &logic) : m_logic(logic) {}
	int newGame();
	void update(Player *owner = nullptr);
	void reset();
	bool executeAction(ScriptEngine &engine, const ScriptActionRec &action);
	bool evaluateCondition(ScriptEngine &engine, const ScriptCondition &condition, bool &result);
	bool teamReference(const ScriptEngine &engine, const std::string &name, Team *&result) const;
	bool teamReady(const Team &team) const;
	Team *visibleTeam(const TeamPrototype &proto) const;
	void retireEmptyTeams(TeamPrototype &proto);
	void unitProduced(Object &factory, Object &unit, unsigned productionID);
	void crc(StateHasher &h) const;
	void xfer(Xfer &x);
	std::vector<std::string> report() const;
	static void defineTransportStates(AIStateMachine &machine);
	static bool enterRampage(Object &object);
	static bool rampageBlocksScriptCommand(const Object &object);

private:
	struct Member
	{
		std::string type;
		int remaining = 0;
		bool required = false;
	};
	struct BuildingTeam
	{
		TeamID team = 0;
		unsigned started = 0;
		bool reinforcement = false;
		bool priority = false;
		ObjectID reinforcementID = INVALID_ID;
		std::vector<Member> members;
	};
	struct UnitOrder
	{
		ObjectID factory = INVALID_ID;
		unsigned productionID = 0;
		TeamID team = 0;
		std::string type;
		int member = 0;
	};
	struct PlayerState
	{
		int player = -1;
		int timer = 2;
		int delay = 0;
		bool ready = false;
	};
	GameLogic &m_logic;
	std::vector<PlayerState> m_players;
	std::vector<BuildingTeam> m_building;
	std::vector<BuildingTeam> m_ready;
	std::map<TeamID, bool> m_teamRecruitable;
	std::map<ObjectID, bool> m_unitRecruitable;
	std::map<int, bool> m_autoBuild;
	std::map<std::pair<std::string, std::string>, TeamID> m_teamReferences;
	std::vector<UnitOrder> m_orders;
	std::set<TeamID> m_retired;
	std::map<int, unsigned> m_conditionFrames;
	std::map<std::string, int> m_powerReadyFrames;
	struct ThreatCache { unsigned next = 0; float value = 0.0f; };
	std::map<std::pair<int, unsigned>, ThreatCache> m_threat;
	float threat(Object &source, const Coord3D &position, float radius, std::pair<int, unsigned> key);
	bool eligible(TeamPrototype &proto);
	bool viable(TeamPrototype &proto, bool requireIdle, bool allowShortMoney);
	void buildTeam(TeamPrototype &proto, bool priority = false);
	void queueUnits(Player &owner);
	Object *factory(Player &owner, const std::string &type, bool idle, int *buildIndex = nullptr) const;
	Object *recruit(Team &team, const std::string &type, const Coord3D &center, float radius, bool defaultOutsideRadius) const;
	Object *combineHordes(Object &receiver, Object &other) const;
	void recruitCombinedFromTeam(Team &destination, Team &source, const std::string &types, int count);
	void loadReinforcementTransports(Team &team, Object *carrier, const Coord3D &destination, const Dict &data);
	bool reinforce(Player &owner, int priority);
	void recruitTeam(TeamPrototype &proto, float radius, const Coord3D *position);
	void joinTeam(Object &unit);
};
