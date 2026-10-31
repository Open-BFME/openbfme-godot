// OpenBFME. GPL-3.0.
// See Common/Team.h.

#include "Common/Team.h"

#include <algorithm>

#include "Common/Player.h"
#include "Common/StateHash.h"
#include "Common/Xfer.h"
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

std::uint32_t Team::enemyLayerMask() const
{
	const Player *owner = getControllingPlayer();
	if (!owner || !m_teamRelations.empty() || owner->hasTeamRelationships())
	{
		return 0xFFFFFFFFu;
	}
	// getRelationship below with that team's overrides absent: this team's override for player k, else the controlling player's view of player k
	std::uint32_t mask = 0;
	for (int k = 0; k < 32; ++k)
	{
		auto it = m_playerRelations.find(k);
		const Relationship r = it != m_playerRelations.end() ? it->second : owner->relationshipToPlayerIndex(k);
		if (r == ENEMIES)
		{
			mask |= 1u << k;
		}
	}
	return mask;
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
	m_pendingCreateActions.clear();
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
		runCreateActions(proto); // lane TEAMEVENTS-1: RW 0x7A6ED8 .. 0x7A6F21
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
	runCreateActions(proto); // lane TEAMEVENTS-1: RW 0x7A6F65 .. 0x7A6FB2
	if (activate)
	{
		t->setActive();
	}
	return t;
}

// lane TEAMEVENTS-1: RW 0x7A6E8E's "if (prototype + 0x240) { script = RW 0x758ECB(owner, + 0x23C); if (script) RW 0x60D053(...) }"
void TeamFactory::runCreateActions(TeamPrototype *proto)
{
	if (!proto->templateScripts().executeActionsOnCreate)
	{
		return;
	}
	if (!m_createActionsHook || !m_createActionsHook(*proto))
	{
		m_pendingCreateActions.push_back(proto);
	}
}

std::vector<TeamPrototype *> TeamFactory::takePendingCreateActions()
{
	std::vector<TeamPrototype *> out;
	out.swap(m_pendingCreateActions);
	return out;
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
		ts->productionCondition = m_dict.getAsciiString("teamProductionCondition"); // lane TEAMEVENTS-1: RW 0x7A389E
		ts->executeActionsOnCreate = m_dict.getBool("teamExecutesActionsOnCreate"); // RW 0x7A38D0
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
		if (!p->attackPriorityName().empty()) // lane BFME2CAMP1: hashed once set
		{
			h.addString(p->attackPriorityName());
		}
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

// ---- lane SAVE-4a: the teams' snapshot --------------------------------------------------------------------------------------------------------
// OpenBFME native layout: retail's TeamFactory::xfer (RW 0x7A634B, tier B, (1, 3)) and TeamPrototype::xfer (RW 0x7A7184, tier D, (1, 2)) transfer RotWK's
// team instances with their AI and script fields; this Team is the port's subset (no recruitment, no AI attack state), so the blocks are native. The
// relation maps are retail's sub-records (xferRelationMaps). Members are ids (ZH Team::xfer saves the member ids and Team::loadPostProcess finds them).
void Team::xfer(Xfer &x)
{
	XferVersion v{ 1, 1 };
	x.xferVersion(v);
	xferRelationMaps(x, m_teamRelations, m_playerRelations);
	x.xferAsciiString(&m_state);
	std::uint32_t n = (std::uint32_t)m_customStates.size();
	x.xferCount(&n, 1, sizeof(std::string) + Xfer::kNodeBytes);
	if (!x.isLoading())
	{
		for (const std::string &c : m_customStates)
		{
			std::string s = c;
			x.xferAsciiString(&s);
		}
	}
	else
	{
		m_customStates.clear();
		for (std::uint32_t i = 0; i < n; ++i)
		{
			std::string s;
			x.xferAsciiString(&s);
			m_customStates.insert(s);
		}
	}
	x.xferUnsignedInt(&m_teamTarget);
	x.xferInt(&m_currentWaypoint);
	x.xferBool(&m_hadMembers);
	x.xferBool(&m_script.active);
	x.xferBool(&m_script.created);
	x.xferBool(&m_script.ready);
	x.xferBool(&m_script.checkEnemySighted);
	x.xferBool(&m_script.seeEnemy);
	x.xferBool(&m_script.prevSeeEnemy);
	x.xferBool(&m_script.wasIdle);
	x.xferInt(&m_script.destroyThreshold);
	x.xferInt(&m_script.curUnits);
	x.xferBool(&m_script.beingBuilt);
	x.xferBool(&m_script.wasBuilt);
	for (int i = 0; i < 32; ++i)
	{
		x.xferBool(&m_script.attemptGeneric[i]);
		x.xferUnsignedInt(&m_script.genericNextFrame[i]);
	}
	// the members, head to tail (the list order is state: AIPOW-1 walks it)
	std::vector<ObjectID> members;
	for (const Object *o = m_head; o; o = o->friend_teamNext())
	{
		members.push_back(o->getID());
	}
	x.xferVectorSize(members, 4);
	for (ObjectID &id : members)
	{
		x.xferObjectID(&id);
	}
	if (x.isLoading())
	{
		m_loadedMembers = members;
	}
}

void TeamFactory::xfer(Xfer &x)
{
	// version 2 (lane BFME2CAMP1): each prototype's attack priority set name (+ 0x318), written only when a prototype has one (every other snapshot is
	// version 1's)
	const bool names = std::any_of(m_prototypes.begin(), m_prototypes.end(), [](const std::unique_ptr<TeamPrototype> &p) { return !p->attackPriorityName().empty(); });
	XferVersion v{ 1, (std::uint8_t)(x.isLoading() || names ? 2 : 1) };
	x.xferVersion(v);
	x.xferUnsignedInt(&m_nextTeamID);
	x.xferInt(&m_nextPrototypeID);
	// the prototypes: the map and the START made them (initTeam), so they are checked, never created
	std::uint32_t count = (std::uint32_t)m_prototypes.size();
	x.xferCount(&count, 7, 0); // id, two empty names, the singleton flag; checked, nothing allocated
	if (count != m_prototypes.size())
	{
		throw XferException("the snapshot has " + std::to_string(count) + " team prototypes, this game " + std::to_string(m_prototypes.size()));
	}
	for (const std::unique_ptr<TeamPrototype> &tp : m_prototypes)
	{
		std::int32_t id = tp->getID();
		std::string name = tp->getName();
		std::string owner = tp->getControllingPlayer() ? tp->getControllingPlayer()->getPlayerName() : std::string();
		bool singleton = tp->getIsSingleton();
		x.xferInt(&id);
		x.xferAsciiString(&name);
		x.xferAsciiString(&owner);
		x.xferBool(&singleton);
		const std::string ownNow = tp->getControllingPlayer() ? tp->getControllingPlayer()->getPlayerName() : std::string();
		if (id != tp->getID() || name != tp->getName() || owner != ownNow || singleton != tp->getIsSingleton())
		{
			throw XferException("the snapshot's team prototype " + std::to_string(id) + " '" + owner + "/" + name + "' is not this game's "
				+ std::to_string(tp->getID()) + " '" + ownNow + "/" + tp->getName() + "'");
		}
		std::string priorityName = tp->attackPriorityName();
		if (v.current >= 2)
		{
			x.xferAsciiString(&priorityName);
		}
		if (x.isLoading())
		{
			tp->setAttackPriorityName(v.current >= 2 ? priorityName : std::string());
		}
	}
	// every team in creation order: (id, prototype id); a team past the game's own is created with its saved id (ZH TeamPrototype::xfer creates a team
	// it does not find)
	// lane SAVE-4e review r2: while a load that trims the later teams is pending (its post-process removes them), a save of the factory (the Objects
	// block's copy of the core blocks, reloaded mid-load) holds the snapshot's teams only, so that reload keeps them pending too
	const size_t saved = m_loadedTeamCount != (size_t)-1 && m_loadedTeamCount < m_teams.size() ? m_loadedTeamCount : m_teams.size();
	std::uint32_t teams = (std::uint32_t)saved;
	x.xferCount(&teams, 8, sizeof(Team)); // (id, prototype id); each team's own state follows after the list
	// lane SAVE-4e: a snapshot with fewer teams than this game (an older moment of the same game: a refused load's put-back, LiveGame::loadSnapshot) loads
	// too: its teams must be this game's first ones; the later ones lose their state now and are removed by the post-process (they must hold no
	// member by then: the snapshot's objects never name them)
	if (x.isLoading())
	{
		m_loadedTeamCount = (size_t)teams; // a save keeps a pending trim (the Objects block saves the core blocks mid-load)
	}
	for (std::uint32_t i = 0; i < teams; ++i)
	{
		std::uint32_t id = i < m_teams.size() ? m_teams[i].getID() : 0;
		std::int32_t proto = i < m_teams.size() ? m_teams[i].getPrototype()->getID() : 0;
		x.xferUnsignedInt(&id);
		x.xferInt(&proto);
		if (!x.isLoading())
		{
			continue;
		}
		if (i < m_teams.size())
		{
			if (id != m_teams[i].getID() || proto != m_teams[i].getPrototype()->getID())
			{
				throw XferException("the snapshot's team " + std::to_string(i) + " is " + std::to_string(id) + " of prototype " + std::to_string(proto)
					+ ", this game's is " + std::to_string(m_teams[i].getID()) + " of prototype " + std::to_string(m_teams[i].getPrototype()->getID()));
			}
			continue;
		}
		TeamPrototype *tp = nullptr;
		for (const std::unique_ptr<TeamPrototype> &p : m_prototypes)
		{
			tp = p->getID() == proto ? p.get() : tp;
		}
		if (!tp || id == TEAM_ID_INVALID || findTeamByID(id) || (tp->getIsSingleton() && !tp->m_teams.empty()))
		{
			throw XferException("the snapshot's team " + std::to_string(id) + " of prototype " + std::to_string(proto) + " cannot be created");
		}
		m_teams.emplace_back(tp, id);
		tp->m_teams.push_back(&m_teams.back());
	}
	if (x.isLoading()) // lane SAVE-4e (closed sets): the id counters are past every team and prototype (a lower one would issue an id twice)
	{
		for (size_t i = 0; i < m_teams.size() && i < teams; ++i)
		{
			if (m_teams[i].getID() >= m_nextTeamID)
			{
				throw XferException("the snapshot's next team id " + std::to_string(m_nextTeamID) + " is not past team " + std::to_string(m_teams[i].getID()));
			}
		}
		for (const std::unique_ptr<TeamPrototype> &tp : m_prototypes)
		{
			if (tp->getID() >= m_nextPrototypeID)
			{
				throw XferException("the snapshot's next team prototype id " + std::to_string(m_nextPrototypeID) + " is not past prototype "
					+ std::to_string(tp->getID()));
			}
		}
	}
	size_t stated = 0;
	for (Team &t : m_teams)
	{
		if (stated >= teams) // a load: a team past the snapshot's; a save: past the pending load's (see above)
		{
			if (x.isLoading())
			{
				t.m_loadedMembers.clear(); // a team past the snapshot's (removed by the post-process)
			}
			continue;
		}
		t.xfer(x);
		++stated;
	}
}

void TeamFactory::loadPostProcess(const std::function<Object *(ObjectID)> &findObject)
{
	// review r1: every saved link is validated before any list is touched (a member listed twice, in one team or in two, would make the intrusive
	// list loop): each id names an existing object, whose own team is the listing team, listed once in all
	std::vector<std::vector<Object *>> members(m_teams.size());
	std::map<ObjectID, TeamID> listedIn;
	size_t i = 0;
	for (Team &t : m_teams)
	{
		const std::string team = "team " + std::to_string(t.getID()) + " '" + t.getName() + "'";
		for (ObjectID id : t.m_loadedMembers)
		{
			const auto seen = listedIn.find(id);
			if (seen != listedIn.end())
			{
				throw XferException(team + ": its member object " + std::to_string(id) + " is listed twice (also in team " + std::to_string(seen->second) + ")");
			}
			listedIn[id] = t.getID();
			Object *o = findObject(id);
			if (!o)
			{
				throw XferException(team + ": its member object " + std::to_string(id) + " does not exist");
			}
			if (o->getTeam() != &t)
			{
				throw XferException(team + ": its member object " + std::to_string(id) + " belongs to another team");
			}
			members[i].push_back(o);
		}
		++i;
	}
	// then every list emptied and each team's members linked in their saved order
	for (Team &t : m_teams)
	{
		while (Object *o = t.getFirstMember())
		{
			t.friend_removeMember(o);
		}
	}
	i = 0;
	for (Team &t : m_teams)
	{
		for (Object *o : members[i])
		{
			t.friend_addMember(o);
		}
		t.m_loadedMembers.clear();
		++i;
	}
	// lane SAVE-4e: the teams past the snapshot's (see xfer) go, last first; each must be empty
	while (m_loadedTeamCount != (size_t)-1 && m_teams.size() > m_loadedTeamCount)
	{
		Team &t = m_teams.back();
		if (t.getFirstMember())
		{
			throw XferException("team " + std::to_string(t.getID()) + " '" + t.getName() + "', past the snapshot's teams, still has a member");
		}
		std::vector<Team *> &list = t.getPrototype()->m_teams;
		list.erase(std::remove(list.begin(), list.end(), &t), list.end());
		m_teams.pop_back();
	}
	m_loadedTeamCount = (size_t)-1;
}

// ---- lane SAVE-4b (see Team.h) ------------------------------------------------------------------------------------------------------------------------------
std::vector<std::vector<ObjectID>> TeamFactory::friend_loadedMembers() const
{
	std::vector<std::vector<ObjectID>> out;
	for (const Team &t : m_teams)
	{
		out.push_back(t.m_loadedMembers);
	}
	return out;
}

void TeamFactory::friend_setLoadedMembers(const std::vector<std::vector<ObjectID>> &members)
{
	if (members.size() != m_teams.size())
	{
		throw XferException("the teams' loaded members are of " + std::to_string(members.size()) + " teams, the game has " + std::to_string(m_teams.size()));
	}
	size_t i = 0;
	for (Team &t : m_teams)
	{
		t.m_loadedMembers = members[i++];
	}
}
