// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// Team, TeamPrototype and TeamFactory (ZH Include/Common/Team.h, Source/Common/RTS/Team.cpp), lane LOGIC-1; the parts the live object
// layer and production need: teams as the owner of objects, the prototypes of a map's Teams chunk, the per-player default team
// ("team" + the player's name, ZH Player::setDefaultTeam), and relationship overrides.
//
// Determinism: members are kept in an intrusive list in insertion order (Object::friend_teamNext), prototypes in creation order, the
// relationship maps are ordered maps; nothing iterates a hash container.
//
// Not ported: team scripts / AI state (TeamPrototype's template info, attack targets, waypoints), the active/inactive flag, the
// build-list and generic-script bookkeeping: each belongs to the AI and script lanes.

#pragma once

#include "Common/Dict.h"
#include "GameLogic/ObjectTypes.h"

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

class Object;
class Player;
class PlayerList;
class StateHasher;
class TeamFactory;
class TeamPrototype;

typedef std::uint32_t TeamID;
enum
{
	TEAM_ID_INVALID = 0
};

class Team
{
public:
	Team(TeamPrototype *proto, TeamID id)
		: m_proto(proto)
		, m_id(id)
	{
	}

	TeamID getID() const { return m_id; }
	TeamPrototype *getPrototype() const { return m_proto; }
	const std::string &getName() const;
	Player *getControllingPlayer() const;

	// ZH Team::getRelationship: a team override, then an override for that team's player, then the controlling player's view
	Relationship getRelationship(const Team *that) const;
	void setTeamRelationship(const Team *that, Relationship r);
	void setPlayerRelationship(const Player *that, Relationship r);
	// lane SCRIPT-2: RW 0x7A54BC / 0x7A5512 (TEAM_REMOVE_ALL_OVERRIDE_RELATIONS): the team and the player overrides cleared
	void removeAllRelationshipOverrides()
	{
		m_teamRelations.clear();
		m_playerRelations.clear();
	}
	// lane MODULES-2: RW 0x7A1C4D / 0x7A207B (team + 0x118 / + 0x11C non-empty): the team has a team or player relationship override (the partition layer
	// of its objects is then -1, RW 0x68EE53)
	bool hasRelationshipOverrides() const { return !m_teamRelations.empty() || !m_playerRelations.empty(); }
	// lane UBER-2A: the partition layers (PartitionFilter::playerLayerMask, bit k: player k) that can hold an object getRelationship sees as ENEMIES. An
	// object on player k's layer belongs to a team of player k without overrides, so the answer depends on k alone unless this team or its player has a
	// TEAM override (then every bit is set)
	std::uint32_t enemyLayerMask() const;

	// lane SCRIPT-2: the script state (team + 0x44, TEAM_SET_STATE RW 0x7BF72E / TEAM_STATE_IS RW 0x7EA28F) and the custom states (RW 0x7A6BD3 set /
	// clear, RW 0x7A6A01 test)
	const std::string &getState() const { return m_state; }
	void setState(const std::string &s) { m_state = s; }
	void setCustomState(const std::string &s, bool on)
	{
		if (on)
		{
			m_customStates.insert(s);
		}
		else
		{
			m_customStates.erase(s);
		}
	}
	bool hasCustomState(const std::string &s) const { return m_customStates.count(s) != 0; }
	// lane SCRIPT-3: the team's common target (team + 0x114, an object id; RW 0x7A3E36 get / RW 0x79FDEA set, the rules in AIHunt.cpp)
	std::uint32_t teamTargetId() const { return m_teamTarget; }
	void setTeamTargetId(std::uint32_t id) { m_teamTarget = id; }
	// lane SCRIPT-3: the team's current waypoint of a path followed as a team (team + 0x6C; -1 none; AIWaypointPath.cpp)
	int currentWaypointId() const { return m_currentWaypoint; }
	void setCurrentWaypointId(int id) { m_currentWaypoint = id; }

	// lane CAMP-1: team + 0x128, the team has had objects (TEAM_DESTROYED RW 0x7E69C6 needs it). lane CAMP-1H: set by Team::updateState (RW 0x7A208C ->
	// 0x7A2230) when the team is active (+ 0x5D) past its creation step (+ 0x5F) and has any object (RW 0x7A11FF), see ScriptEngine::updateTeamState
	bool hadMembers() const { return m_hadMembers; }
	void setHadMembers() { m_hadMembers = true; }

	// lane CAMP-1H: the team's script state (TARGET FACTS, rotwk201_game.exe; the constructor RW 0x7A6C91 clears it, the INI-free parts of Team::updateState
	// RW 0x7A208C and Team::updateGenericScripts RW 0x7A267D read and write it, ScriptEngine::updateTeamState runs both)
	struct ScriptState
	{
		bool active = false;           ///< + 0x5D (activation, inlined at every creator: "if (!+0x5D) { +0x5E = 1; +0x5D = 1; }")
		bool created = false;          ///< + 0x5E: activated, its creation step not yet run (TEAM_CREATED RW 0x7E73E0 answers it)
		bool ready = false;            ///< + 0x5F: past the creation step (the generic scripts and the per-frame checks run)
		bool checkEnemySighted = false;///< + 0x60: the prototype has an EnemySighted or AllClear script (RW 0x7A6E0D)
		bool seeEnemy = false;         ///< + 0x61
		bool prevSeeEnemy = false;     ///< + 0x62
		bool wasIdle = false;          ///< + 0x63
		std::int32_t destroyThreshold = 0; ///< + 0x64
		std::int32_t curUnits = 0;     ///< + 0x68
		bool beingBuilt = false;       ///< + 0x113 (AIPlayer's team building RW 0x79FE29; not ported: never set)
		bool wasBuilt = false;         ///< + 0x112 (RW 0x79FE29: the ready test RW 0x7A09E4 then waits for its hordes; never set here)
		bool attemptGeneric[32];       ///< + 0x70: generic script i is still tried (the constructor sets all, RW 0x7A6E63)
		std::uint32_t genericNextFrame[32]; ///< + 0x90: the frame generic script i is evaluated again (its DelayEvaluationSeconds)
		ScriptState()
		{
			for (int i = 0; i < 32; ++i)
			{
				attemptGeneric[i] = true;
				genericNextFrame[i] = 0;
			}
		}
	};
	ScriptState &scriptState() { return m_script; }
	const ScriptState &scriptState() const { return m_script; }
	// the activation inlined at every creator of a live team (RW 0x7A6FDE, 0x62E17B, 0x6AC6F7, 0x7C8D02, 0x7C0D86, ...)
	void setActive()
	{
		if (!m_script.active)
		{
			m_script.created = true;
			m_script.active = true;
		}
	}
	bool isActive() const { return m_script.active; }

	// the members, in insertion order
	Object *getFirstMember() const { return m_head; }
	// lane AIPOW-1: retail's member list (Team + 0x38, walked by RW 0x66362D / 0x6632C9) starts at the NEWEST member (Object::prependToTeamMemberList RW
	// 0x68AD89 links each new member at the head), so retail's order is this list's reverse: walk from here with friend_teamPrev
	Object *getLastMember() const { return m_tail; }
	unsigned getMemberCount() const { return m_count; }
	void friend_addMember(Object *obj);
	void friend_removeMember(Object *obj);

	void crc(StateHasher &hasher) const;
	// lane SAVE-4a: the team's state; the members are saved as ids and linked again by TeamFactory::loadPostProcess
	void xfer(class Xfer &x);

private:
	friend class TeamFactory;
	std::vector<ObjectID> m_loadedMembers; ///< lane SAVE-4a: the member ids a load read, until the post-process links them
	TeamPrototype *m_proto;
	TeamID m_id;
	Object *m_head = nullptr, *m_tail = nullptr;
	unsigned m_count = 0;
	std::map<TeamID, Relationship> m_teamRelations;
	std::map<int, Relationship> m_playerRelations; ///< by player index
	std::string m_state;                    ///< lane SCRIPT-2: + 0x44
	std::set<std::string> m_customStates;   ///< lane SCRIPT-2
	std::uint32_t m_teamTarget = 0;         ///< lane SCRIPT-3: + 0x114
	int m_currentWaypoint = -1;             ///< lane SCRIPT-3: + 0x6C
	bool m_hadMembers = false;              ///< lane CAMP-1: + 0x128
	ScriptState m_script;                   ///< lane CAMP-1H
};

// lane CAMP-1H: the team scripts of a prototype's TeamTemplateInfo (prototype + 0x12C), read from the map's team dict by RW 0x7A2C96 with the keys of RW
// 0xDA2B14 ..: teamOnCreateScript + 0xC0, teamEventsList + 0xC4, teamOnIdleScript + 0xC8, teamInitialIdleSeconds + 0xCC (x 5 frames), teamEnemySightedScript
// + 0xD0, teamAllClearScript + 0xD4, teamOnUnitDestroyedScript + 0xD8, teamOnDestroyedScript + 0xDC, teamDestroyedThreshold + 0xE0 (real),
// teamGenericScriptHook0 .. 31 + 0x118 (the key "teamGenericScriptHook%d", RW 0xC1222C)
// lane SAVE-4a (Player.cpp): retail's TeamRelationMap::xfer (RW 0x7A56B6) then PlayerRelationMap::xfer (RW 0x6AE9B7), the two override maps
void xferRelationMaps(class Xfer &x, std::map<TeamID, Relationship> &teams, std::map<int, Relationship> &players);

struct TeamTemplateScripts
{
	std::string onCreate, eventsList, onIdle, enemySighted, allClear, onUnitDestroyed, onDestroyed;
	std::int32_t initialIdleFrames = 0;
	float destroyedThreshold = 0.0f;
	std::string generic[32];
	// lane TEAMEVENTS-1: teamProductionCondition + 0x110 (RW 0x7A38A3, key RW 0xDA2CCC) and teamExecutesActionsOnCreate + 0x114 (RW 0x7A38D9, key
	// RW 0xDA2D14, the bool read RW 0x714A66: missing = false); the prototype's + 0x23C / + 0x240 that RW 0x7A6E8E reads
	std::string productionCondition;
	bool executeActionsOnCreate = false;
};

class TeamPrototype
{
public:
	TeamPrototype(const std::string &name, Player *owner, bool singleton, int id, const Dict *dict)
		: m_name(name)
		, m_owner(owner)
		, m_singleton(singleton)
		, m_id(id)
	{
		if (dict)
		{
			m_dict = *dict;
		}
	}
	const std::string &getName() const { return m_name; }
	Player *getControllingPlayer() const { return m_owner; }
	bool getIsSingleton() const { return m_singleton; }
	int getID() const { return m_id; }
	const Dict &getDict() const { return m_dict; }
	// lane BFME2CAMP1: + 0x318, the attack priority set TEAM_APPLY_ATTACK_PRIORITY_SET gave the team (RW 0x7A1BCF; RW 0x691F0C reads it): an object that
	// joins one of the prototype's teams takes it (Object::applyTeamAttackPriority); "" none
	void setAttackPriorityName(const std::string &n) { m_attackPriorityName = n; }
	const std::string &attackPriorityName() const { return m_attackPriorityName; }
	const std::vector<Team *> &teams() const { return m_teams; }
	Team *getFirstTeam() const { return m_teams.empty() ? nullptr : m_teams.front(); }
	// lane CAMP-1: the head of retail's instance list (prototype + 0x334): a new team is PREPENDED (RW 0x79FA5C -> RW 0x79F9D0), so the head is the newest
	Team *getNewestTeam() const { return m_teams.empty() ? nullptr : m_teams.back(); }
	// lane CAMP-1H: the TeamTemplateInfo's scripts (read from the dict once)
	const TeamTemplateScripts &templateScripts() const;

private:
	friend class TeamFactory;
	std::string m_name;
	Player *m_owner;
	bool m_singleton;
	int m_id;
	Dict m_dict;
	std::string m_attackPriorityName; ///< lane BFME2CAMP1: + 0x318
	std::vector<Team *> m_teams;
	mutable std::unique_ptr<TeamTemplateScripts> m_scripts; ///< lane CAMP-1H: templateScripts()
};

class TeamFactory
{
public:
	TeamFactory() = default;
	TeamFactory(const TeamFactory &) = delete;
	TeamFactory &operator=(const TeamFactory &) = delete;

	void clear();
	// ZH TeamFactory::initTeam: a duplicate name is an error (std::runtime_error)
	TeamPrototype *initTeam(const std::string &name, Player *owner, bool singleton, const Dict *dict = nullptr);
	// a new team of a prototype (ZH createInactiveTeam / createTeam: a singleton prototype has exactly one). lane CAMP-1H: `activate` is RW 0x7A6FCB's
	// createTeam (active) against RW 0x7A6E8E's createInactiveTeam (the singleton's existing team is activated too, RW 0x7A70CE with its flag)
	// lane TEAMEVENTS-1: RW 0x7A6E8E runs the production-condition script's actions (see setCreateActionsHook) on both paths, the singleton's existing
	// team too, after the new team is linked into its prototype and before RW 0x7A6FCB's activation
	Team *createTeam(TeamPrototype *proto, bool activate = false);
	// lane TEAMEVENTS-1: RW 0x7A6E8E's action run for a prototype with teamExecutesActionsOnCreate: the script engine's lookup (RW 0x758ECB) and action
	// run (RW 0x60D053 with no team). The hook answers false while the engine has no scripts yet: the prototype is queued (in creation order) and
	// takePendingCreateActions hands the queue to the engine once its scripts are built. Retail looks the script up in TheSidesList (RW 0x604986
	// reads TheSidesList + 0x3C sides), which exists while PlayerList::newGame makes the map's teams; ours builds the script engine after the players
	// (and the objects), so a team made before that (a map singleton, RW 0x7A73DB -> 0x7A6E8E, or an object's team, RW 0x7A7147) runs its actions
	// when the engine starts. INFERENCE: no RotWK 2.01 map has a singleton or an object-owned team with the flag (the census: only AI-built teams).
	void setCreateActionsHook(std::function<bool(TeamPrototype &)> hook) { m_createActionsHook = std::move(hook); }
	std::vector<TeamPrototype *> takePendingCreateActions();

	TeamPrototype *findTeamPrototype(const std::string &name) const;
	// lane SCRIPT-2: RW 0x7A2C47, the prototype of that name owned by the player of that name (the maps reuse a team name under several owners:
	// "PlyrBNEast/Camp Guards", "PlyrBNWest/Camp Guards")
	TeamPrototype *findTeamPrototype(const std::string &ownerName, const std::string &name) const;
	// ZH TeamFactory::findTeam: the first team of the prototype with that name
	Team *findTeam(const std::string &name);
	Team *findTeamByID(TeamID id) const;
	const std::vector<std::unique_ptr<TeamPrototype>> &prototypes() const { return m_prototypes; }
	size_t teamCount() const { return m_teams.size(); } ///< lane SAVE-4e (tests)

	void crc(StateHasher &hasher) const;
	// lane SAVE-4a (Common/Xfer.h): the id counters, the prototypes (which must be this game's: the START and the map made them), every team in creation
	// order (a team the game does not have yet is created with its saved id) and its state
	void xfer(class Xfer &x);
	// after every block loaded: each team's members linked again in their saved order (`findObject` is GameLogic::findObjectByID); a member that does not
	// exist or whose object names another team is an error
	void loadPostProcess(const std::function<Object *(ObjectID)> &findObject);
	// lane SAVE-4b: the member ids each team's load read, in team order (GameLogic::xferObjects loads the teams a second time after making the objects; the
	// post-process must link the snapshot's members, not the ones that second load reads from the live lists)
	std::vector<std::vector<ObjectID>> friend_loadedMembers() const;
	// lane BFME2CAMP1: the id the next team gets (every id below it was issued; a load checks a saved team id against it)
	TeamID nextTeamID() const { return m_nextTeamID; }
	void friend_setLoadedMembers(const std::vector<std::vector<ObjectID>> &members);

private:
	std::vector<std::unique_ptr<TeamPrototype>> m_prototypes;
	std::multimap<std::string, size_t> m_byName; ///< case sensitive, like NAMEKEY; lane SCRIPT-2: one name per OWNER (RW 0x7A2C47 keys by both)
	std::deque<Team> m_teams;               ///< stable addresses, creation order
	TeamID m_nextTeamID = 1;
	int m_nextPrototypeID = 1;
	size_t m_loadedTeamCount = (size_t)-1; ///< lane SAVE-4e: the teams the snapshot being loaded holds (-1: no load)
	std::function<bool(TeamPrototype &)> m_createActionsHook; ///< lane TEAMEVENTS-1
	std::vector<TeamPrototype *> m_pendingCreateActions;      ///< lane TEAMEVENTS-1: runs the hook could not take yet
	void runCreateActions(TeamPrototype *proto);
};
