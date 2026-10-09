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

	// the members, in insertion order
	Object *getFirstMember() const { return m_head; }
	unsigned getMemberCount() const { return m_count; }
	void friend_addMember(Object *obj);
	void friend_removeMember(Object *obj);

	void crc(StateHasher &hasher) const;

private:
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
	const std::vector<Team *> &teams() const { return m_teams; }
	Team *getFirstTeam() const { return m_teams.empty() ? nullptr : m_teams.front(); }

private:
	friend class TeamFactory;
	std::string m_name;
	Player *m_owner;
	bool m_singleton;
	int m_id;
	Dict m_dict;
	std::vector<Team *> m_teams;
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
	// a new team of a prototype (ZH createInactiveTeam / createTeam: a singleton prototype has exactly one)
	Team *createTeam(TeamPrototype *proto);

	TeamPrototype *findTeamPrototype(const std::string &name) const;
	// lane SCRIPT-2: RW 0x7A2C47, the prototype of that name owned by the player of that name (the maps reuse a team name under several owners:
	// "PlyrBNEast/Camp Guards", "PlyrBNWest/Camp Guards")
	TeamPrototype *findTeamPrototype(const std::string &ownerName, const std::string &name) const;
	// ZH TeamFactory::findTeam: the first team of the prototype with that name
	Team *findTeam(const std::string &name);
	Team *findTeamByID(TeamID id) const;
	const std::vector<std::unique_ptr<TeamPrototype>> &prototypes() const { return m_prototypes; }

	void crc(StateHasher &hasher) const;

private:
	std::vector<std::unique_ptr<TeamPrototype>> m_prototypes;
	std::multimap<std::string, size_t> m_byName; ///< case sensitive, like NAMEKEY; lane SCRIPT-2: one name per OWNER (RW 0x7A2C47 keys by both)
	std::deque<Team> m_teams;               ///< stable addresses, creation order
	TeamID m_nextTeamID = 1;
	int m_nextPrototypeID = 1;
};
