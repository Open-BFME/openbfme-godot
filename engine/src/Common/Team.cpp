// OpenBFME. GPL-3.0.
// See Common/Team.h.

#include "Common/Team.h"

#include "Common/Player.h"
#include "Common/StateHash.h"
#include "GameLogic/Object/Object.h"

#include <stdexcept>

const std::string &Team::getName() const
{
	return m_proto->getName();
}

Player *Team::getControllingPlayer() const
{
	return m_proto->getControllingPlayer();
}

// ZH Team.cpp:1471-1499
Relationship Team::getRelationship(const Team *that) const
{
	if (!m_teamRelations.empty() && that != nullptr)
	{
		auto it = m_teamRelations.find(that->getID());
		if (it != m_teamRelations.end())
		{
			return it->second;
		}
	}
	if (!m_playerRelations.empty() && that != nullptr)
	{
		const Player *thatPlayer = that->getControllingPlayer();
		if (thatPlayer != nullptr)
		{
			auto it = m_playerRelations.find(thatPlayer->getPlayerIndex());
			if (it != m_playerRelations.end())
			{
				return it->second;
			}
		}
	}
	return getControllingPlayer()->getRelationship(that);
}

void Team::setTeamRelationship(const Team *that, Relationship r)
{
	if (that)
	{
		m_teamRelations[that->getID()] = r;
	}
}

void Team::setPlayerRelationship(const Player *that, Relationship r)
{
	if (that)
	{
		m_playerRelations[that->getPlayerIndex()] = r;
	}
}

void Team::friend_addMember(Object *obj)
{
	obj->friend_setTeamLinks(m_tail, nullptr);
	if (m_tail)
	{
		m_tail->friend_setTeamLinks(m_tail->friend_teamPrev(), obj);
	}
	else
	{
		m_head = obj;
	}
	m_tail = obj;
	++m_count;
}

void Team::friend_removeMember(Object *obj)
{
	Object *prev = obj->friend_teamPrev(), *next = obj->friend_teamNext();
	if (prev)
	{
		prev->friend_setTeamLinks(prev->friend_teamPrev(), next);
	}
	else
	{
		m_head = next;
	}
	if (next)
	{
		next->friend_setTeamLinks(prev, next->friend_teamNext());
	}
	else
	{
		m_tail = prev;
	}
	obj->friend_setTeamLinks(nullptr, nullptr);
	--m_count;
}

void Team::crc(StateHasher &h) const
{
	h.addU32(m_id);
	h.addI32(m_proto ? m_proto->getID() : 0); // the team-to-prototype association (ids are handed out in creation order)
	h.addU32(m_count);
	for (const Object *o = m_head; o; o = o->friend_teamNext())
	{
		h.addU32(o->getID());
	}
	h.addU32((std::uint32_t)m_teamRelations.size());
	for (const auto &kv : m_teamRelations)
	{
		h.addU32(kv.first);
		h.addI32((int)kv.second);
	}
	h.addU32((std::uint32_t)m_playerRelations.size());
	for (const auto &kv : m_playerRelations)
	{
		h.addI32(kv.first);
		h.addI32((int)kv.second);
	}
	if (m_hadMembers)
	{
		h.addU32(0x128u); // lane CAMP-1: + 0x128, hashed when set
	}
	if (m_script.active) // lane CAMP-1H: the script state, hashed once the team is active (an inactive team never changes it)
	{
		h.addBool(m_script.created);
		h.addBool(m_script.ready);
		h.addBool(m_script.checkEnemySighted);
		h.addBool(m_script.seeEnemy);
		h.addBool(m_script.prevSeeEnemy);
		h.addBool(m_script.wasIdle);
		h.addI32(m_script.destroyThreshold);
		h.addI32(m_script.curUnits);
		for (int i = 0; i < 32; ++i)
		{
			h.addBool(m_script.attemptGeneric[i]);
			h.addU32(m_script.genericNextFrame[i]);
		}
	}
	if (!m_state.empty() || !m_customStates.empty()) // lane SCRIPT-2: hashed once a script set them
	{
		h.addString(m_state);
		h.addU32((std::uint32_t)m_customStates.size());
		for (const std::string &c : m_customStates)
		{
			h.addString(c);
		}
	}
	if (m_teamTarget != 0) // lane SCRIPT-3: hashed once a hunt set it
	{
		h.addU32(0x114u);
		h.addU32(m_teamTarget);
	}
	if (m_currentWaypoint != -1) // lane SCRIPT-3: hashed once a team path set it
	{
		h.addU32(0x6Cu);
		h.addI32(m_currentWaypoint);
	}
}

// ---- TeamFactory ------------------------------------------------------------------------------------------------------
void TeamFactory::clear()
{
	m_teams.clear();
	m_prototypes.clear();
	m_byName.clear();
	m_nextTeamID = 1;
	m_nextPrototypeID = 1;
}

TeamPrototype *TeamFactory::initTeam(const std::string &name, Player *owner, bool singleton, const Dict *dict)
{
	if (findTeamPrototype(owner ? owner->getPlayerName() : std::string(), name))
	{
		throw std::runtime_error("TeamFactory::initTeam: team '" + name + "' already exists"); // ZH DEBUG_ASSERTCRASH "team already exists"
	}
	m_prototypes.push_back(std::make_unique<TeamPrototype>(name, owner, singleton, m_nextPrototypeID++, dict));
	TeamPrototype *tp = m_prototypes.back().get();
	m_byName.emplace(name, m_prototypes.size() - 1);
	if (singleton)
	{
		createTeam(tp); // ZH createInactiveTeam(name)
	}
	return tp;
}

Team *TeamFactory::createTeam(TeamPrototype *proto, bool activate)
{
	if (proto->getIsSingleton() && !proto->m_teams.empty())
	{
		Team *t = proto->m_teams.front(); // a singleton prototype has exactly one team
		if (activate)
		{
			t->setActive();
		}
		return t;
	}
	m_teams.emplace_back(proto, m_nextTeamID++);
	Team *t = &m_teams.back();
	proto->m_teams.push_back(t);
	// lane CAMP-1H: the constructor (RW 0x7A6C91 -> RW 0x7A6E0D): + 0x60 when the prototype has an EnemySighted (+ 0x1FC) or AllClear (+ 0x200) script
	const TeamTemplateScripts &ts = proto->templateScripts();
	t->scriptState().checkEnemySighted = !ts.enemySighted.empty() || !ts.allClear.empty();
	if (activate)
	{
		t->setActive();
	}
	return t;
}

const TeamTemplateScripts &TeamPrototype::templateScripts() const
{
	if (!m_scripts)
	{
		// RW 0x7A2C96: a missing key leaves the field empty / 0 (the AsciiString / int / real reads RW 0x714E6D / 0x714A98 / 0x714ACA)
		auto ts = std::make_unique<TeamTemplateScripts>();
		ts->onCreate = m_dict.getAsciiString("teamOnCreateScript");
		ts->eventsList = m_dict.getAsciiString("teamEventsList");
		ts->onIdle = m_dict.getAsciiString("teamOnIdleScript");
		ts->initialIdleFrames = m_dict.getInt("teamInitialIdleSeconds") * 5; // RW 0x7A373B: x [0xD9F608] (5 frames a second)
		ts->enemySighted = m_dict.getAsciiString("teamEnemySightedScript");
		ts->allClear = m_dict.getAsciiString("teamAllClearScript");
		ts->onUnitDestroyed = m_dict.getAsciiString("teamOnUnitDestroyedScript");
		ts->onDestroyed = m_dict.getAsciiString("teamOnDestroyedScript");
		ts->destroyedThreshold = m_dict.getReal("teamDestroyedThreshold");
		for (int i = 0; i < 32; ++i)
		{
			ts->generic[i] = m_dict.getAsciiString("teamGenericScriptHook" + std::to_string(i)); // "%s%d" (RW 0xC1222C)
		}
		m_scripts = std::move(ts);
	}
	return *m_scripts;
}

TeamPrototype *TeamFactory::findTeamPrototype(const std::string &name) const
{
	auto it = m_byName.find(name); // the first of that name (multimap: insertion order among equal keys)
	return it == m_byName.end() ? nullptr : m_prototypes[it->second].get();
}

TeamPrototype *TeamFactory::findTeamPrototype(const std::string &ownerName, const std::string &name) const
{
	const auto range = m_byName.equal_range(name);
	for (auto it = range.first; it != range.second; ++it)
	{
		TeamPrototype *tp = m_prototypes[it->second].get();
		const Player *owner = tp->getControllingPlayer();
		if ((owner ? owner->getPlayerName() : std::string()) == ownerName)
		{
			return tp;
		}
	}
	return nullptr;
}

// TARGET RW 0x7A7483 (TeamFactory::findTeam): the name is split at the first '/' (RW 0x72C643) into an owner part and a team part and the
// prototype is looked up by both (RW 0x7A7147): the maps write an object's originalOwner as "Owner/teamName" (e.g. "PlyrCivilian/
// teamPlyrCivilian", "/team" for the neutral player's team). A name without '/' is the plain prototype name (ZH; used for the per-player
// default team "team" + name). The lookup compares the owner part with the prototype's owner NAME; retail's exact comparison (case) is
// not recovered, a case-sensitive one is used (NAMEKEY identity, stop S-150).
// ZH TeamFactory::findTeam (Team.cpp:394-407): the first team of the prototype; a NON-singleton prototype with no team yet gets one made
// (createInactiveTeam), so an object owned by a script team ("IC - Area 1 - Mordor - A") has a real team owned by the prototype's player.
Team *TeamFactory::findTeam(const std::string &name)
{
	const size_t slash = name.find('/');
	const std::string teamName = slash == std::string::npos ? name : name.substr(slash + 1);
	TeamPrototype *tp = slash == std::string::npos ? findTeamPrototype(teamName) : findTeamPrototype(name.substr(0, slash), teamName);
	if (!tp)
	{
		return nullptr;
	}
	// lane CAMP-1: RW 0x759FDA answers the prototype's list head (+ 0x334), the newest instance (a reinforcement team made by a script, not an older one)
	Team *t = tp->getNewestTeam();
	if (!t && !tp->getIsSingleton())
	{
		t = createTeam(tp);
	}
	return t;
}

Team *TeamFactory::findTeamByID(TeamID id) const
{
	if (id == TEAM_ID_INVALID)
	{
		return nullptr;
	}
	for (const Team &t : m_teams)
	{
		if (t.getID() == id)
		{
			return const_cast<Team *>(&t);
		}
	}
	return nullptr;
}

void TeamFactory::crc(StateHasher &h) const
{
	// the id counters are state (the next team / prototype created gets them); the prototype's dict is configuration, immutable after initTeam
	h.addU32(m_nextTeamID);
	h.addI32(m_nextPrototypeID);
	h.addU32((std::uint32_t)m_prototypes.size());
	for (const auto &p : m_prototypes)
	{
		h.addI32(p->getID());
		h.addString(p->getName());
		h.addI32(p->getControllingPlayer() ? p->getControllingPlayer()->getPlayerIndex() : -1);
		h.addBool(p->getIsSingleton());
		h.addU32((std::uint32_t)p->teams().size()); // the prototype's teams, in creation order
		for (const Team *t : p->teams())
		{
			h.addU32(t->getID());
		}
	}
	h.addU32((std::uint32_t)m_teams.size());
	for (const Team &t : m_teams)
	{
		t.crc(h);
	}
}
