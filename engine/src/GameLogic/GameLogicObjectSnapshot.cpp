// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// The object side of GameLogic's snapshot (lane SAVE-4b, docs/SAVES.md section 8 "What SAVE-4b, 4c and 4d must cover"): the xfers of the blocks SAVE-4a
// declared for this lane. All are OpenBFME native blocks (S-4190): none of these classes is a 1:1 port of its retail counterpart.
//   obfme:Objects          every object (GameLogic::xferObjects, Object::xfer and the module xfers)
//   obfme:UpdateScheduler  the scheduler's vectors and the destroy list (GameLogic::xferScheduler)
//   obfme:TunnelTracker    every player's tunnel network (TunnelTracker::xfer, retail's layout RW 0x8FA569)
//   obfme:Combat, obfme:SellList, obfme:Experience, obfme:Invisibility, obfme:DamageOverTime   the object-side subsystems
//
// TARGET FACTS: retail's CHUNK_GameLogic (RW 0x6308F3, (1, 8)) holds the object table of contents (GameLogic::xferObjectTOC RW 0x62EF07: template names
// and ids good for this save only) and every object in list order, each in a block (SAVES-RETAIL.md section 2). RotWK's load (GameStateMap RW 0x82B8E4 step
// 6) starts the map's game first, then the blocks overwrite it. DONOR (ZH GameLogic::xfer / prepareLogicForObjectLoad): the map's objects are removed, each
// saved object is made by ThingFactory::newObject with the neutral team (its create modules and initObject run) and then loaded from its block; the scheduler
// is rebuilt after the load. OpenBFME DIFFERENCES (S-4190):
//   * the table carries each object's id, template, team and status, and EVERY object is made before ANY loads (ZH makes and loads one at a time and
//     resolves pointers in loadPostProcess): every id is looked up in the saved table, and the containment links are staged, validated from both sides
//     and installed once every object has loaded (installSnapshotLinks; review r2);
//   * the object is made with its saved id, team and status (ZH: the neutral team, then its xfer sets the team), so the team's member count and the
//     creation's status checks see the saved values (an UNDER_CONSTRUCTION structure's create modules);
//   * the creation of an object changes other state (its player's counts, the logic RNG's creation draw RW 0x628892); so after the objects load, the core
//     blocks loaded before this one (obfme:TeamFactory, obfme:Players, obfme:GameLogic, obfme:Random) are loaded again from a copy taken before the
//     objects were made. An object a creation made on its own (an OCL, a spawn) is not in the snapshot: it is removed without its death;
//   * the scheduler's vectors are state here (their order decides module order, MP-1), so obfme:UpdateScheduler saves them instead of rebuilding them.
// Rebuilt, not saved (named): the per-template caches (GameLogic::templateInfo / crushInfo, filled on demand), the target finder's cell index
// (CombatState::xfer resets it), each object's KindOf mask and module interface pointers (its template's).

#include "GameLogic/GameLogic.h"

#include "Common/GameState.h"
#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/Snapshot.h"
#include "Common/Team.h"
#include "Common/Thing/ThingFactory.h"
#include "Common/Thing/ThingTemplate.h"
#include "Common/XferLoad.h"
#include "Common/XferSave.h"
#include "GameLogic/BuildAssistantSell.h"
#include "GameLogic/Combat/CombatState.h"
#include "GameLogic/ExperienceWorld.h"
#include "GameLogic/Module/UpdateModule.h"
#include "GameLogic/Object/Contain/TunnelContainRuntime.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SnapshotXfer.h"
#include "GameLogic/System/DOTManager.h"
#include "GameLogic/System/InvisibilityManager.h"

#include <map>
#include <set>

namespace
{
// a block whose xfer is a function of its owner (the crc transfers the same fields through XferCRC)
class ObjectSideSnapshot : public Snapshot
{
public:
	explicit ObjectSideSnapshot(std::function<void(Xfer &)> fn) : m_fn(std::move(fn)) {}
	void crc(Xfer *x) override { m_fn(*x); }
	void xfer(Xfer *x) override { m_fn(*x); }
	void loadPostProcess() override {}

private:
	std::function<void(Xfer &)> m_fn;
};
} // namespace

void GameLogic::registerObjectSnapshotBlocks()
{
	GameState &gs = *m_gameState;
	auto provide = [&](const char *token, std::function<void(Xfer &)> fn) {
		gs.provideOwnedSnapshot(token, std::make_unique<ObjectSideSnapshot>(std::move(fn)), "SAVE-4b");
	};
	provide("obfme:Objects", [this](Xfer &x) { xferObjects(x); });
	provide("obfme:UpdateScheduler", [this](Xfer &x) { xferScheduler(x); });
	provide("obfme:TunnelTracker", [this](Xfer &x) {
		SnapshotXfer::version(x, 1, 1);
		std::int32_t players = x.isLoading() ? 0 : m_players.getPlayerCount();
		x.xferInt(&players);
		if (players != m_players.getPlayerCount())
		{
			throw XferException("the snapshot's tunnel networks are of " + std::to_string(players) + " players, this game has " + std::to_string(m_players.getPlayerCount()));
		}
		// a load reads every player's network aside and replaces the players' networks only when all of them loaded
		std::vector<std::unique_ptr<TunnelTracker>> loaded((size_t)players);
		for (int i = 0; i < players; ++i)
		{
			Player *p = m_players.getNthPlayer(i);
			bool has = !x.isLoading() && p->tunnelTrackerIfAny() != nullptr; // the tracker exists once a tunnel asked for it (hashed: TunnelContain::crc)
			x.xferBool(&has);
			if (!has)
			{
				continue;
			}
			if (x.isLoading())
			{
				loaded[(size_t)i] = std::make_unique<TunnelTracker>();
				loaded[(size_t)i]->xfer(x, *this);
			}
			else
			{
				p->tunnelTracker().xfer(x, *this);
			}
		}
		if (x.isLoading())
		{
			validateTunnelNetworks(loaded);
		}
		for (int i = 0; i < players && x.isLoading(); ++i)
		{
			Player *p = m_players.getNthPlayer(i);
			p->friend_dropTunnelTracker();
			if (loaded[(size_t)i])
			{
				p->tunnelTracker() = *loaded[(size_t)i];
			}
		}
	});
	provide("obfme:Combat", [this](Xfer &x) { m_combat->xfer(x); });
	provide("obfme:SellList", [this](Xfer &x) { m_sell->xfer(x); });
	provide("obfme:Experience", [this](Xfer &x) { m_experience->xfer(x); });
	provide("obfme:Invisibility", [this](Xfer &x) { m_invisibility->xfer(x); });
	provide("obfme:DamageOverTime", [this](Xfer &x) { m_dot->xfer(x); });
}

// GameLogic::reset's per-object step: the scheduler forgets the object's modules, it leaves the list and the id table, then it is deleted (its destructor
// leaves the world and its team). No death: no die module, no destroy list.
void GameLogic::deleteObjectForLoad(Object *obj)
{
	for (const std::unique_ptr<BehaviorModule> &m : obj->modules())
	{
		if (UpdateModule *u = m->asUpdateModule())
		{
			removeUpdateModule(u);
		}
	}
	unlinkObject(obj);
	friend_removeObjectFromLookup(obj);
	delete obj;
}

void GameLogic::xferObjects(Xfer &x)
{
	SnapshotXfer::version(x, 1, 1);
	struct Entry
	{
		ObjectID id = INVALID_ID;
		std::string templateName;
		TeamID team = TEAM_ID_INVALID;
		ObjectStatusMaskType status{};
	};
	std::vector<Entry> toc;
	if (!x.isLoading())
	{
		for (const Object *o = m_objectHead; o; o = o->getNextObject())
		{
			Entry e;
			e.id = o->getID();
			e.templateName = o->getTemplate()->getName();
			e.team = o->getTeam() ? o->getTeam()->getID() : TEAM_ID_INVALID;
			e.status = o->getStatusBits();
			toc.push_back(e);
		}
	}
	SnapshotXfer::each(x, toc, 29, [&](Entry &e) { // id, template name, team, the status mask
		x.xferObjectID(&e.id);
		x.xferAsciiString(&e.templateName);
		x.xferUnsignedInt(&e.team);
		SnapshotXfer::maskWords(x, e.status, SnapshotXfer::objectStatusCount(), "an object table entry's status", "the object status names"); // lane SAVE-4e
	});
	if (!x.isLoading())
	{
		for (Object *o = m_objectHead; o; o = o->getNextObject())
		{
			x.beginBlock();
			o->xfer(x);
			x.endBlock();
		}
		return;
	}

	// ---- load: check the table against this game's data before anything changes ----
	std::vector<const ThingTemplate *> templates;
	std::vector<Team *> teams;
	std::set<ObjectID> saved;
	for (const Entry &e : toc)
	{
		const ThingTemplate *tt = m_things.findTemplate(e.templateName);
		if (!tt)
		{
			throw XferException("the snapshot's object " + std::to_string(e.id) + " is of the template '" + e.templateName + "', which this game's data lacks");
		}
		Team *team = m_players.teams().findTeamByID(e.team);
		if (!team)
		{
			throw XferException("the snapshot's object " + std::to_string(e.id) + " is of team " + std::to_string(e.team) + ", which this game lacks");
		}
		if (e.id == INVALID_ID || e.id >= m_nextObjectID || !saved.insert(e.id).second)
		{
			throw XferException("the snapshot's object id " + std::to_string(e.id) + " is invalid, repeated or not below the next object id "
				+ std::to_string(m_nextObjectID));
		}
		templates.push_back(tt->getFinalOverride());
		teams.push_back(team);
	}
	// the core blocks as loaded (the objects' creation changes them; they are loaded again at the end). The teams' member ids the TeamFactory block read are
	// kept: the second load would read the live lists (the members are linked by the post-process, after this block)
	const std::vector<std::vector<ObjectID>> loadedMembers = m_players.teams().friend_loadedMembers();
	XferSave core;
	m_players.teams().xfer(core);
	m_players.xfer(core);
	xferCore(core);
	m_random.xfer(core);

	// ---- ZH prepareLogicForObjectLoad: no object of this game stays ----
	m_current = nullptr;
	while (m_objectHead)
	{
		deleteObjectForLoad(m_objectHead);
	}
	m_objectsToDestroy.clear();
	// ---- every object of the snapshot, made with its id, team and status (ThingFactory::newObject without the BuildVariations draw: the saved template is
	// the variation already) ----
	for (size_t i = 0; i < toc.size(); ++i)
	{
		Object *obj = new Object(*this, templates[i], toc[i].status, teams[i], toc[i].id);
		obj->friend_runCreateModules();
		obj->friend_initObject();
	}
	// the objects in the snapshot's order, each from its block. Every lookup is restricted to the saved table, and the containment links are staged (rule 4
	// of SAVES.md section 8's rules for xfer authors): installed only after every object loaded and both sides of every link agree
	std::vector<SnapshotLink> links;
	struct Staging
	{
		GameLogic &logic;
		~Staging()
		{
			logic.m_snapshotTable = nullptr;
			logic.m_snapshotLinks = nullptr;
		}
	} staging{ *this };
	m_snapshotTable = &saved;
	m_snapshotLinks = &links;
	for (size_t i = 0; i < toc.size(); ++i)
	{
		Object *obj = findObjectByID(toc[i].id);
		if (!obj)
		{
			throw XferException("the snapshot's object " + std::to_string(toc[i].id) + " '" + toc[i].templateName + "' did not survive its creation");
		}
		x.beginBlock();
		obj->xfer(x);
		x.endBlock();
	}
	m_snapshotTable = nullptr;
	m_snapshotLinks = nullptr;
	// the links replace every contain list the creations filled; until they are installed those lists still name the objects a creation made on its
	// own, so a refused link leaves those objects alive and listed (the refused load is put back by loading the kept snapshot, LiveGame::loadSnapshot)
	installSnapshotLinks(links);
	// an object a creation made on its own goes (it is not in the snapshot); the list then holds the snapshot's objects in its order
	for (Object *p = m_objectHead; p;)
	{
		Object *next = p->getNextObject();
		if (!saved.count(p->getID()))
		{
			deleteObjectForLoad(p);
		}
		p = next;
	}
	size_t i = 0;
	for (const Object *p = m_objectHead; p; p = p->getNextObject(), ++i)
	{
		if (i >= toc.size() || p->getID() != toc[i].id)
		{
			throw XferException("the loaded object list differs from the snapshot's at entry " + std::to_string(i));
		}
	}
	if (i != toc.size())
	{
		throw XferException("the loaded object list has " + std::to_string(i) + " objects, the snapshot " + std::to_string(toc.size()));
	}
	// the core blocks again (TeamFactory's members are linked by the post-process the load queued)
	XferLoad again(core.data());
	m_players.teams().xfer(again);
	m_players.xfer(again);
	xferCore(again);
	m_random.xfer(again);
	m_players.teams().friend_setLoadedMembers(loadedMembers);
}

// The tunnel networks a load read agree with the objects before any is installed: each tunnel id names a tunnel (an object with a TunnelContain), listed by
// one network once; each rider is in one network once, and its container is one of that network's tunnels; every object a tunnel contains is a rider of
// the network listing that tunnel
void GameLogic::validateTunnelNetworks(const std::vector<std::unique_ptr<TunnelTracker>> &networks)
{
	std::map<ObjectID, size_t> networkOfTunnel, networkOfRider;
	for (size_t n = 0; n < networks.size(); ++n)
	{
		if (!networks[n])
		{
			continue;
		}
		for (ObjectID id : networks[n]->tunnelIds())
		{
			const Object *t = findObjectByID(id);
			if (!t || !dynamic_cast<const TunnelContain *>(const_cast<Object *>(t)->getContain()))
			{
				throw XferException("player " + std::to_string(n) + "'s tunnel network lists tunnel " + std::to_string(id) + ", which is not a tunnel of the snapshot");
			}
			if (!networkOfTunnel.emplace(id, n).second)
			{
				throw XferException("tunnel " + std::to_string(id) + " is listed twice by the tunnel networks");
			}
		}
	}
	for (size_t n = 0; n < networks.size(); ++n)
	{
		if (!networks[n])
		{
			continue;
		}
		for (const Object *r : networks[n]->riders())
		{
			if (!networkOfRider.emplace(r->getID(), n).second)
			{
				throw XferException("rider " + std::to_string(r->getID()) + " is in two tunnel networks");
			}
			const Object *c = r->getContainedBy();
			const auto t = c ? networkOfTunnel.find(c->getID()) : networkOfTunnel.end();
			if (t == networkOfTunnel.end() || t->second != n)
			{
				throw XferException("player " + std::to_string(n) + "'s tunnel network lists rider " + std::to_string(r->getID()) + ", whose container "
					+ (c ? std::to_string(c->getID()) : std::string("(none)")) + " is not one of its tunnels");
			}
		}
	}
	for (const Object *o = m_objectHead; o; o = o->getNextObject())
	{
		const Object *c = o->getContainedBy();
		if (!c || !dynamic_cast<const TunnelContain *>(const_cast<Object *>(c)->getContain()))
		{
			continue;
		}
		const auto t = networkOfTunnel.find(c->getID());
		const auto r = networkOfRider.find(o->getID());
		if (t == networkOfTunnel.end() || r == networkOfRider.end() || r->second != t->second)
		{
			throw XferException("object " + std::to_string(o->getID()) + " is inside tunnel " + std::to_string(c->getID()) + ", whose network does not list it");
		}
	}
}

// Both sides of every staged link agree before any is installed: each object names one container (or none); a rider is listed by one contain only, and
// that contain's object is the rider's container; an object whose container is not a tunnel is listed by that container's contain (a tunnel network's
// riders are its player's TunnelTracker's: obfme:TunnelTracker checks those). Then every link is installed in the saved order.
void GameLogic::installSnapshotLinks(std::vector<SnapshotLink> &links)
{
	std::map<ObjectID, ObjectID> containerOf;
	std::map<ObjectID, ObjectID> listedBy;
	for (const SnapshotLink &l : links)
	{
		if (l.role == SnapshotLink::CONTAINED_BY)
		{
			if (!containerOf.emplace(l.owner, l.ids.empty() ? (ObjectID)INVALID_ID : l.ids[0]).second)
			{
				throw XferException("the snapshot names the container of object " + std::to_string(l.owner) + " twice");
			}
		}
	}
	for (const SnapshotLink &l : links)
	{
		if (l.role != SnapshotLink::RIDERS)
		{
			continue;
		}
		for (ObjectID id : l.ids)
		{
			const auto listed = listedBy.emplace(id, l.owner);
			if (!listed.second)
			{
				throw XferException("the snapshot's object " + std::to_string(id) + " is listed as a rider by objects " + std::to_string(listed.first->second)
					+ " and " + std::to_string(l.owner));
			}
			const auto c = containerOf.find(id);
			const ObjectID container = c == containerOf.end() ? (ObjectID)INVALID_ID : c->second;
			if (container != l.owner)
			{
				throw XferException("the snapshot's object " + std::to_string(l.owner) + " lists rider " + std::to_string(id) + ", whose container is "
					+ (container == INVALID_ID ? std::string("none") : std::to_string(container)));
			}
		}
	}
	for (const auto &kv : containerOf)
	{
		if (kv.second == INVALID_ID)
		{
			continue;
		}
		const Object *c = findObjectByID(kv.second);
		if (c && dynamic_cast<const TunnelContain *>(const_cast<Object *>(c)->getContain()))
		{
			continue;
		}
		const auto l = listedBy.find(kv.first);
		if (l == listedBy.end() || l->second != kv.second)
		{
			throw XferException("the snapshot's object " + std::to_string(kv.first) + " names container " + std::to_string(kv.second) + ", which does not list it");
		}
	}
	for (SnapshotLink &l : links)
	{
		std::vector<Object *> objects;
		for (ObjectID id : l.ids)
		{
			if (id != INVALID_ID)
			{
				objects.push_back(findObjectByID(id));
			}
		}
		l.install(objects);
	}
}

// the scheduler (OpenBFME native, S-4190): per phase vector, then the sleeping vector, (object id, module index) in vector order with the module's next
// call frame; then the destroy list. A load reads every entry aside and validates it first (the object exists, the index names one of its update modules,
// every update module of every object is placed exactly once, the destroy list names objects once), and only then fills the vectors in the saved order
// with each module's index (GameLogic + 0xC8 / + 0xF8: the position is state, it decides the order the modules run in).
void GameLogic::xferScheduler(Xfer &x)
{
	SnapshotXfer::version(x, 1, 1);
	struct Entry
	{
		ObjectID id = INVALID_ID;
		std::uint32_t index = 0;
		UnsignedInt next = 0;
	};
	const int kVectors = PHASE_COUNT + 1; // the phases, then the sleeping vector
	std::vector<Entry> saved[PHASE_COUNT + 1];
	if (!x.isLoading())
	{
		for (int p = 0; p < kVectors; ++p)
		{
			for (const UpdateModule *u : p < PHASE_COUNT ? m_updates[p] : m_sleeping)
			{
				const Object *obj = u->getObject();
				Entry e;
				e.id = obj->getID();
				e.next = u->friend_getNextCallFrame();
				const auto &mods = obj->modules();
				e.index = (std::uint32_t)mods.size();
				for (size_t k = 0; k < mods.size(); ++k)
				{
					e.index = mods[k].get() == u ? (std::uint32_t)k : e.index;
				}
				if (e.index == mods.size())
				{
					throw XferException("an update module of object " + std::to_string(e.id) + " is not one of its modules");
				}
				saved[p].push_back(e);
			}
		}
	}
	for (std::vector<Entry> &vec : saved)
	{
		x.xferVectorSize(vec, 12);
		for (Entry &e : vec)
		{
			x.xferObjectID(&e.id);
			x.xferUnsignedInt(&e.index);
			x.xferUnsignedInt(&e.next);
		}
	}
	std::vector<ObjectID> destroy;
	for (const Object *o : x.isLoading() ? std::vector<Object *>() : m_objectsToDestroy)
	{
		destroy.push_back(o->getID());
	}
	SnapshotXfer::ids(x, destroy);
	if (!x.isLoading())
	{
		return;
	}
	// ---- validate everything before anything changes ----
	std::vector<UpdateModule *> modules[PHASE_COUNT + 1];
	std::set<const UpdateModule *> placed;
	for (int p = 0; p < kVectors; ++p)
	{
		for (const Entry &e : saved[p])
		{
			Object *obj = SnapshotXfer::findObject(*this, e.id, "a scheduled module's object");
			UpdateModule *u = obj && e.index < obj->modules().size() ? obj->modules()[e.index]->asUpdateModule() : nullptr;
			if (!u)
			{
				throw XferException("the snapshot schedules module " + std::to_string(e.index) + " of object " + std::to_string(e.id) + ", which is not an update module");
			}
			if (!placed.insert(u).second)
			{
				throw XferException("the snapshot schedules module " + std::to_string(e.index) + " of object " + std::to_string(e.id) + " twice");
			}
			if (e.next > (UnsignedInt)UPDATE_SLEEP_FOREVER || ((p == PHASE_COUNT) != (e.next == (UnsignedInt)UPDATE_SLEEP_FOREVER)))
			{
				throw XferException("the snapshot schedules module " + std::to_string(e.index) + " of object " + std::to_string(e.id) + " for frame "
					+ std::to_string(e.next) + (p == PHASE_COUNT ? " in the sleeping vector" : " in a phase vector"));
			}
			modules[p].push_back(u);
		}
	}
	for (Object *o = m_objectHead; o; o = o->getNextObject())
	{
		for (const std::unique_ptr<BehaviorModule> &m : o->modules())
		{
			if (UpdateModule *u = m->asUpdateModule(); u && !placed.count(u))
			{
				throw XferException("the snapshot does not schedule module '" + m->getModuleTagName() + "' of object " + std::to_string(o->getID()));
			}
		}
	}
	std::vector<Object *> destroyList;
	std::set<ObjectID> listed;
	for (ObjectID id : destroy)
	{
		Object *o = SnapshotXfer::findObject(*this, id, "the destroy list");
		if (!o || !listed.insert(id).second)
		{
			throw XferException("the snapshot's destroy list names object " + std::to_string(id) + (o ? " twice" : ", no object"));
		}
		destroyList.push_back(o);
	}
	// ---- then the vectors, in the saved order ----
	m_current = nullptr;
	for (int p = 0; p < kVectors; ++p)
	{
		std::vector<UpdateModule *> &vec = p < PHASE_COUNT ? m_updates[p] : m_sleeping;
		vec.clear();
		for (size_t k = 0; k < modules[p].size(); ++k)
		{
			UpdateModule *u = modules[p][k];
			u->friend_setNextCallFrame(saved[p][k].next);
			u->friend_setIndexInLogic((int)vec.size(), p < PHASE_COUNT ? p : -1);
			vec.push_back(u);
		}
	}
	m_objectsToDestroy = destroyList;
}

std::vector<std::string> GameLogic::objectSnapshotStopLines()
{
	return {
		"[S-4190] snapshots: the objects, their modules and the object-side subsystems are OpenBFME's native blocks (obfme:Objects, obfme:UpdateScheduler, "
		"obfme:Combat, obfme:SellList, obfme:Experience, obfme:Invisibility, obfme:DamageOverTime); the classes are the port's subsets of retail's "
		"(Object::xfer RW 0x697F28 (1, 29), ActiveBody::xfer RW 0x8C3388, OpenContain::xfer RW 0x868FEF, Weapon::xfer RW 0x6CEA67, ...), so their fields "
		"follow the port's own order; retail's layout is kept by the module base chain (RW 0x7082E2, 0x8CD4AA, 0x850C60), the upgrade mux (RW 0x8D27E8, "
		"0x8D284A), TunnelTracker (RW 0x8FA569; its last two fields, the nemesis and its frame, INFERENCE, written 0: S-1107), DamageFilteredCreateObjectDie "
		"(RW 0x88927E) and DeletionUpdate (RW 0x88B863); retail import (SAVE-7) needs a translator per class",
		"[S-4191] snapshots: an object load makes every saved object again through its creation (its create modules and initObject run, ZH's load does the "
		"same), without the BuildVariations draw (the saved template is the variation), then loads its state; objects those creations made on their own are "
		"removed without their death and the core blocks are loaded again (a creation changes its player and draws the logic RNG); the saved modules must be "
		"the object's own in its order (INFERENCE: retail skips a module the data no longer has, RW 0x6987A5)",
		"[S-4192] snapshots: rebuilt, not saved: the per-template caches (GameLogic::templateInfo / crushInfo), the target finder's cell index, each object's "
		"KindOf mask and module interface pointers (its template's), LargeGroupBonusUpdate's scan scratch; the report statistics of the experience system, "
		"the sell list and the horde contains are not saved, nor GameLogic's AI command record (AICommandSink::issued: the commands run when issued) and "
		"pending producer (consumed by the creation it is set for); the client is told the loaded model conditions by SAVE-4e's load (S-5011 names what "
		"it is not told)",
		"[S-4193] snapshots: the AI update family's modules (AIUpdateInterface, HordeAIUpdate, DozerAIUpdate, GiantBirdAIUpdate) save only their versions "
		"and the update module's part in Object::xfer's module list (SAVE-4e); their AI state is obfme:AIWorld's (S-4220), where retail's "
		"AIUpdateInterface::xfer (RW 0x667416) keeps it in the module list; SAVE-4b's per-block tests can still leave them out "
		"(GameLogic::setSnapshotModuleFilter, tests only)",
	};
}
