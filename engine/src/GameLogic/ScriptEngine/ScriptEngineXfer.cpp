// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0 (ScriptEngine::xfer, Script::xfer, ScriptGroup::xfer,
// SequentialScript::xfer in GameLogic/ScriptEngine/ScriptEngine.cpp and Scripts.cpp), as RotWK changes them.
//
// The block obfme:ScriptEngine (lane SAVE-4d, docs/SAVES.md; stop S-4256).
//
// TARGET FACTS (RotWK game.dat, caveat S-001):
//   * RotWK's ScriptEngine::xfer is RW 0x60A45B (window (1, 5), tier B "tu-only": no decomp code). Its stream holds far more than this port keeps (the
//     Living World armies and players, attack priorities, fades, music, the named object cache by name and id, ...) and is not identified field by field
//     (docs/SAVES-RETAIL.md section 2), so the block is OpenBFME's own layout: every field ScriptEngine::crc hashes plus the state a later frame reads.
//   * inside it the records that ARE 1:1 ports keep retail's layout and window: Script::xfer (RW 0x7B3063, (1, 2): the active byte + 0x40, from version 2
//     the frame to evaluate at + 0x3C), ScriptGroup::xfer (RW 0x7B2F8A, (1, 1): the active byte), SequentialScript::xfer (RW 0x60331E, (1, 1): the team's
//     id (u32, the team's + 0x34; 0 none), the object id (XferObjectID), the side (AsciiString + 0xC), the script name (+ 0x10), the action index
//     (+ 0x18), the loops left (+ 0x1C), the frames to wait (+ 0x20), the run-again byte (+ 0x24); a load finds the team by id, RW 0x7A1EEE, and throws
//     code 5 for an id it lacks). Retail's Script / ScriptGroup xfers carry no names or counts: the tree is the map's, and the engine's xfer walks it.
// OpenBFME (S-4256):
//   * the tree is the map's and its libraries' (rebuilt from the START): the block checks the side count, each side's name and every list's script and
//     group counts, then transfers the runtime flags through the retail record layouts above;
//   * a sequential script, a prototype's generic script copy (RW 0x7A1759) and a team's id are resolved again by name / id at load: a name the tree lacks
//     or a team the game lacks is an error, never a skipped record;
//   * NOT saved: the client requests and end requests (the client's, taken every frame), the statistics, the frame-local context (current player, side,
//     script, call depth, the team scopes: constant between frames), the run-time polygon triggers (AIGateUpdate and the castles add them with their
//     objects: the objects' block puts them back, SAVE-4b; this block keeps only the list's extent, version 4), and the Lua logic state, which retail
//     does not save either (no block of its lists holds it; the AI's loadPostProcess registers every object with Lua again, RW 0x667BEF): the port's Lua engine is the client's (LiveScripting), outside GameLogic and its state hash.
//
// Not simulation maths: the transfers copy values between frames.

// The load (SAVE-4a review r1 rules, SAVES.md section 8): every container's size is bounded by its entries' encoded size; everything is read into
// temporaries and checked (the tree's shape and names, the difficulty 0 .. 3, a generic copy list of 32 entries per prototype of this game, names given
// once, a sequential record of a team XOR a unit with a script of this game's tree, a science queue's position within its player's history) before the
// engine takes it all at once. The loaded state is staged: finishLoad (the block's post-process, after every block loaded) checks the sequential records' teams exist and only
// then hands it to the engine.

#include "GameLogic/ScriptEngine/ScriptEngine.h"

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/Team.h"
#include "Common/Thing/ThingFactory.h"
#include "Common/Xfer.h"
#include "GameClient/MapChunks.h"
#include "GameLogic/GameLogic.h"

#include <algorithm>
#include <memory>
#include <tuple>

namespace
{
// the tree's runtime flags in walk order (scripts of a list, then each group's flag and its subtree): what the load takes
struct TreeFlags
{
	std::vector<bool> groupActive;
	std::vector<std::pair<bool, UnsignedInt>> scripts;
};

// RW 0x7B2F8A
void xferGroupRecord(Xfer &x, bool &active)
{
	XferVersion v{ 1, 1 };
	x.xferVersion(v);
	x.xferBool(&active);
}

// RW 0x7B3063
void xferScriptRecord(Xfer &x, bool &active, UnsignedInt &frame)
{
	XferVersion v{ 1, 2 };
	x.xferVersion(v);
	x.xferBool(&active);
	if (v.current >= 2)
	{
		x.xferUnsignedInt(&frame);
	}
}

// a list's shape (checked against this game's tree, which the START made), then its scripts' and groups' records, recursively (the order RW 0x60A377 /
// 0x60BCE5 run them in); a load only collects the flags
void xferTree(Xfer &x, ScriptEngine::RGroup &g, const std::string &where, TreeFlags &out)
{
	const bool loading = x.isLoading();
	std::uint32_t scripts = loading ? 0 : (std::uint32_t)g.scripts.size(), groups = loading ? 0 : (std::uint32_t)g.groups.size();
	x.xferUnsignedInt(&scripts);
	x.xferUnsignedInt(&groups);
	if (loading && (scripts != g.scripts.size() || groups != g.groups.size()))
	{
		throw XferException("script engine: " + where + " has " + std::to_string(scripts) + " scripts and " + std::to_string(groups) +
			" groups in the snapshot, " + std::to_string(g.scripts.size()) + " and " + std::to_string(g.groups.size()) + " in this game");
	}
	// each script and group by its name first: the flags belong to the script of that name at that position (the same counts in another order would
	// hand them to other scripts)
	auto checkName = [&](const std::string &mine, size_t i, const char *what) {
		std::string name = loading ? std::string() : mine;
		x.xferAsciiString(&name);
		if (loading && name != mine)
		{
			throw XferException("script engine: " + where + ": " + what + " " + std::to_string(i) + " is '" + name + "' in the snapshot, '" + mine +
				"' in this game");
		}
	};
	for (size_t i = 0; i < g.scripts.size(); ++i)
	{
		ScriptEngine::RScript &s = *g.scripts[i];
		checkName(s.def->name, i, "script");
		bool active = loading ? false : s.active;
		UnsignedInt frame = loading ? 0 : s.frameToEvaluateAt;
		xferScriptRecord(x, active, frame);
		out.scripts.emplace_back(active, frame);
	}
	for (size_t i = 0; i < g.groups.size(); ++i)
	{
		ScriptEngine::RGroup &c = *g.groups[i];
		checkName(c.name, i, "group");
		bool active = loading ? false : c.active;
		xferGroupRecord(x, active);
		out.groupActive.push_back(active);
		xferTree(x, c, where + " / group '" + c.name + "'", out);
	}
}

// the flags collected by xferTree, given to the tree in the same walk order
void applyTree(ScriptEngine::RGroup &g, const TreeFlags &in, size_t &script, size_t &group)
{
	for (auto &s : g.scripts)
	{
		s->active = in.scripts[script].first;
		s->frameToEvaluateAt = in.scripts[script].second;
		++script;
	}
	for (auto &c : g.groups)
	{
		c->active = in.groupActive[group++];
		applyTree(*c, in, script, group);
	}
}
} // namespace

void ScriptEngine::xfer(Xfer &x)
{
	const bool loading = x.isLoading();
	if (loading)
	{
		m_pendingLoad = nullptr; // an earlier load that never finished is dropped
		m_pendingTeams.clear();
	}
	// lane SAVE-4e: version 2 adds BACKLOG4's triggered special power lists (merged after SAVE-4d); no version 1 snapshot was ever written by a
	// release (loads of snapshots begin with SAVE-4e), so version 1 is not read (a list it lacked would load empty, silently). Lane CASTLE-UNPACK:
	// version 3 adds the object references (RW + 0x191B8); a version 2 game had none (its engine kept no such map), so it loads with an empty one.
	// Lane CASTLEAREA-1: version 4 adds the run-time trigger list's extent (removed slots included: the next trigger's index follows it); a version 3
	// game never removed a run-time trigger, so its owners' saved indices give the extent. Lane MODCOMPAT-AI: version 5 adds the completed upgrades
	// (RW + 0x1A458); an older game's engine kept none, so it loads with empty lists (the lane wrote it as 4 on its branch; renumbered by merge order).
	// Lane BFME2CAMP1: version 6 adds the attack priority table, written only once a script edited it (every other snapshot is version 5's); 6's
	// fields follow 5's
	XferVersion v{ 2, (std::uint8_t)(loading || m_attackPriorities.edited() ? 6 : 5) };
	x.xferVersion(v);
	bool loaded = loading ? false : m_loaded;
	x.xferBool(&loaded);
	if (loading && loaded != m_loaded)
	{
		throw XferException(std::string("script engine: the snapshot's game ") + (loaded ? "has" : "has no") + " map scripts, this game " +
			(m_loaded ? "has" : "has none"));
	}
	bool firstUpdate = loading ? false : m_firstUpdate, bonus = loading ? false : m_objectsReceiveDifficultyBonus;
	std::int32_t difficulty = loading ? 0 : m_gameDifficulty;
	x.xferBool(&firstUpdate);
	x.xferInt(&difficulty);
	x.xferBool(&bonus);
	if (loading && (difficulty < 0 || difficulty > 3))
	{
		throw XferException("script engine: a difficulty of " + std::to_string(difficulty)); // RW 0x603878: 0 easy, 1 normal, 2 / 3 hard
	}

	// the tree's runtime flags
	std::uint32_t sides = loading ? 0 : (std::uint32_t)m_sides.size();
	x.xferUnsignedInt(&sides);
	if (loading && sides != m_sides.size())
	{
		throw XferException("script engine: the snapshot has " + std::to_string(sides) + " sides, this game " + std::to_string(m_sides.size()));
	}
	std::vector<TreeFlags> tree(m_sides.size());
	for (size_t i = 0; i < m_sides.size(); ++i)
	{
		RSide &s = m_sides[i];
		std::string name = loading ? std::string() : s.name;
		x.xferAsciiString(&name);
		if (loading && name != s.name)
		{
			throw XferException("script engine: the snapshot's side '" + name + "' is '" + s.name + "' in this game");
		}
		xferTree(x, s.root, "side '" + s.name + "'", tree[i]);
	}

	// the prototypes' generic script copies (RW 0x7A1759), by prototype id: 32 hooks each (RW 0x7A1759's loop); a copy names its script by side and name
	std::map<int, std::vector<GenericCopy>> loadedGeneric;
	auto &generic = loading ? loadedGeneric : m_genericScripts;
	std::uint32_t n = loading ? 0 : (std::uint32_t)generic.size();
	x.xferCount(&n, 4 + 4 + 32, sizeof(std::pair<const int, std::vector<GenericCopy>>) + Xfer::kNodeBytes); // the id, the count, 32 empty copies
	auto git = generic.begin();
	for (std::uint32_t i = 0; i < n; ++i)
	{
		std::int32_t id = loading ? 0 : git->first;
		std::vector<GenericCopy> copies;
		if (!loading)
		{
			copies = git->second;
			++git;
		}
		x.xferInt(&id);
		x.xferVectorSize(copies, 1); // a copy without a script: its flag
		if (loading && copies.size() != 32)
		{
			throw XferException("script engine: prototype " + std::to_string(id) + " has " + std::to_string(copies.size()) + " generic scripts (32 hooks)");
		}
		for (GenericCopy &g : copies)
		{
			bool has = loading ? false : g.def != nullptr;
			x.xferBool(&has);
			if (!has)
			{
				continue;
			}
			std::string script = loading ? std::string() : g.def->name;
			x.xferAsciiString(&g.side);
			x.xferAsciiString(&script);
			x.xferBool(&g.active);
			if (loading)
			{
				RSide *rs = findSide(g.side);
				RScript *sc = rs ? findScriptIn(rs->root, script) : nullptr;
				if (!sc)
				{
					throw XferException("script engine: a generic script copy names '" + g.side + "/" + script + "', which this game's scripts lack");
				}
				g.def = sc->def;
			}
		}
		if (!loading)
		{
			continue;
		}
		const auto &protos = m_logic.players().teams().prototypes();
		if (std::none_of(protos.begin(), protos.end(), [id](const std::unique_ptr<TeamPrototype> &p) { return p->getID() == id; }))
		{
			throw XferException("script engine: generic scripts of team prototype " + std::to_string(id) + ", which this game lacks");
		}
		if (!generic.emplace(id, std::move(copies)).second)
		{
			throw XferException("script engine: the generic scripts of prototype " + std::to_string(id) + " twice");
		}
	}

	// the counters and flags by (side, name), in key order
	std::map<std::pair<std::string, std::string>, Counter> loadedCounters;
	auto &counters = loading ? loadedCounters : m_counters;
	n = loading ? 0 : (std::uint32_t)counters.size();
	x.xferCount(&n, 1 + 1 + 4 + 1 + 1, sizeof(std::pair<const std::pair<std::string, std::string>, Counter>) + Xfer::kNodeBytes);
	auto cit = counters.begin();
	for (std::uint32_t i = 0; i < n; ++i)
	{
		std::pair<std::string, std::string> key;
		Counter c;
		if (!loading)
		{
			key = cit->first;
			c = cit->second;
			++cit;
		}
		x.xferAsciiString(&key.first);
		x.xferAsciiString(&key.second);
		x.xferInt(&c.value);
		x.xferBool(&c.countdown);
		x.xferBool(&c.milliseconds);
		if (loading && !counters.emplace(key, c).second)
		{
			throw XferException("script engine: the counter '" + key.first + "/" + key.second + "' twice");
		}
	}
	std::map<std::pair<std::string, std::string>, bool> loadedFlags;
	auto &flags = loading ? loadedFlags : m_flags;
	n = loading ? 0 : (std::uint32_t)flags.size();
	x.xferCount(&n, 1 + 1 + 1, sizeof(std::pair<const std::pair<std::string, std::string>, bool>) + Xfer::kNodeBytes);
	auto fit = flags.begin();
	for (std::uint32_t i = 0; i < n; ++i)
	{
		std::pair<std::string, std::string> key;
		bool value = false;
		if (!loading)
		{
			key = fit->first;
			value = fit->second;
			++fit;
		}
		x.xferAsciiString(&key.first);
		x.xferAsciiString(&key.second);
		x.xferBool(&value);
		if (loading && !flags.emplace(key, value).second)
		{
			throw XferException("script engine: the flag '" + key.first + "/" + key.second + "' twice");
		}
	}

	// the object type lists (creation order) and the named object cache (insertion order: ids)
	std::vector<std::pair<std::string, std::vector<std::string>>> loadedLists;
	auto &lists = loading ? loadedLists : m_objectLists;
	x.xferVectorSize(lists, 1 + 4);
	for (auto &l : lists)
	{
		x.xferAsciiString(&l.first);
		x.xferVectorSize(l.second, 1);
		for (std::string &t : l.second)
		{
			x.xferAsciiString(&t);
		}
	}
	std::vector<std::pair<std::string, ObjectID>> loadedNamed;
	auto &named = loading ? loadedNamed : m_namedObjects;
	x.xferVectorSize(named, 1 + 4);
	for (auto &e : named)
	{
		x.xferAsciiString(&e.first);
		x.xferObjectID(&e.second);
	}

	// the sequential scripts (RW + 0x10): a chain per team / unit, each record in retail's layout (RW 0x60331E)
	std::vector<std::vector<SequentialScript>> loadedSequential;
	auto &sequential = loading ? loadedSequential : m_sequential;
	x.xferVectorSize(sequential, 4);
	for (auto &chain : sequential)
	{
		x.xferVectorSize(chain, 1 + 4 + 4 + 1 + 1 + 4 + 4 + 4 + 1);
		for (SequentialScript &r : chain)
		{
			XferVersion rv{ 1, 1 };
			x.xferVersion(rv);
			x.xferUnsignedInt(&r.team);
			x.xferObjectID(&r.object);
			x.xferAsciiString(&r.side);
			x.xferAsciiString(&r.scriptName);
			x.xferInt(&r.index);
			x.xferInt(&r.loopCount);
			x.xferInt(&r.framesToWait);
			x.xferBool(&r.again);
			if (!loading)
			{
				continue;
			}
			if ((r.team != 0) == (r.object != INVALID_ID))
			{
				throw XferException("script engine: a sequential script of team " + std::to_string(r.team) + " and unit " + std::to_string(r.object) +
					" (one of the two)");
			}
			RSide *rs = findSide(r.side);
			RScript *sc = rs ? findScriptIn(rs->root, r.scriptName) : nullptr;
			if (!sc)
			{
				throw XferException("script engine: a sequential script names '" + r.side + "/" + r.scriptName + "', which this game's scripts lack");
			}
			r.script = sc->def;
		}
	}

	// the acquired-science queues (+ 0x1A3A8), each checked against the player at its index, and HAS_FINISHED_AUDIO's timers (+ 0x1A35C)
	std::array<AcquiredScienceQueue, 20> loadedQueues;
	auto &queues = loading ? loadedQueues : m_acquiredSciences;
	for (size_t i = 0; i < queues.size(); ++i)
	{
		const Player *p = m_logic.players().getNthPlayer((int)i);
		queues[i].xfer(x, p ? &p->science() : nullptr);
	}
	std::vector<AudioTimer> loadedTimers;
	auto &timers = loading ? loadedTimers : m_audioTimers;
	x.xferVectorSize(timers, 1 + 4);
	for (AudioTimer &t : timers)
	{
		x.xferAsciiString(&t.name);
		x.xferUnsignedInt(&t.endFrame);
	}
	// lane SAVE-4e (version 2): BACKLOG4's triggered special powers (RW + 0x1A368): per player list its entries (the name's CRC, the source object id,
	// 0 for none; an issued id: the source may be gone, as in retail's list)
	std::array<std::vector<TriggeredSpecialPower>, kSpecialPowerListPlayers> loadedTriggered;
	auto &triggered = loading ? loadedTriggered : m_triggeredSpecialPowers;
	for (std::vector<TriggeredSpecialPower> &l : triggered)
	{
		x.xferVectorSize(l, 4 + 4);
		for (TriggeredSpecialPower &e : l)
		{
			x.xferUnsignedInt(&e.nameCrc);
			x.xferObjectID(&e.source);
			if (loading && e.source != INVALID_ID && e.source >= m_logic.getNextObjectID())
			{
				throw XferException("script engine: a triggered special power names the object " + std::to_string(e.source) + ", which this game never made");
			}
		}
	}
	// lane CASTLE-UNPACK (version 3): the object references by (side, name), in key order: the object's id (an id of this game; the object may be gone,
	// as in retail's map)
	std::map<std::pair<std::string, std::string>, ObjectID> loadedReferences;
	auto &references = loading ? loadedReferences : m_references;
	if (v.current >= 3)
	{
		n = loading ? 0 : (std::uint32_t)references.size();
		x.xferCount(&n, 1 + 1 + 4, sizeof(std::pair<const std::pair<std::string, std::string>, ObjectID>) + Xfer::kNodeBytes);
		auto rit = references.begin();
		for (std::uint32_t i = 0; i < n; ++i)
		{
			std::pair<std::string, std::string> key;
			ObjectID id = INVALID_ID;
			if (!loading)
			{
				key = rit->first;
				id = rit->second;
				++rit;
			}
			x.xferAsciiString(&key.first);
			x.xferAsciiString(&key.second);
			x.xferObjectID(&id);
			if (!loading)
			{
				continue;
			}
			if (id != INVALID_ID && id >= m_logic.getNextObjectID())
			{
				throw XferException("script engine: the reference '" + key.first + "/" + key.second + "' names the object " + std::to_string(id) +
					", which this game never made");
			}
			if (!references.emplace(key, id).second)
			{
				throw XferException("script engine: the reference '" + key.first + "/" + key.second + "' twice");
			}
		}
	}
	// lane CASTLEAREA-1 (version 4): the run-time trigger list's extent. The objects block (loaded before this one) put each owner's trigger back at its
	// index; the extent is at least the slots it gave and is bounded like the indices (ScriptEngine::addPolygonTriggerAt)
	std::uint32_t runtimeExtent = loading ? 0 : (std::uint32_t)m_runtimeTriggers.size();
	if (v.current >= 4)
	{
		x.xferUnsignedInt(&runtimeExtent);
		if (loading && (runtimeExtent > 0x10000 || runtimeExtent < m_runtimeTriggers.size()))
		{
			throw XferException("script engine: the run-time trigger list's extent " + std::to_string(runtimeExtent) + " (the objects gave " +
				std::to_string(m_runtimeTriggers.size()) + " slots)");
		}
	}
	else if (loading)
	{
		runtimeExtent = (std::uint32_t)m_runtimeTriggers.size();
	}
	// lane MODCOMPAT-AI (version 5): the completed upgrades (RW + 0x1A458), as the triggered special powers
	std::array<std::vector<TriggeredSpecialPower>, kSpecialPowerListPlayers> loadedUpgrades;
	auto &upgrades = loading ? loadedUpgrades : m_completedUpgrades;
	if (v.current >= 5)
	{
		for (std::vector<TriggeredSpecialPower> &l : upgrades)
		{
			x.xferVectorSize(l, 4 + 4);
			for (TriggeredSpecialPower &e : l)
			{
				x.xferUnsignedInt(&e.nameCrc);
				x.xferObjectID(&e.source);
				if (loading && e.source != INVALID_ID && e.source >= m_logic.getNextObjectID())
				{
					throw XferException("script engine: a completed upgrade names the object " + std::to_string(e.source) + ", which this game never made");
				}
			}
		}
	}
	// lane BFME2CAMP1 (version 6): the edited attack priority table (AttackPriorityStore::saved: every entry but the default, in order; its targets by name)
	bool priorities = loading ? false : m_attackPriorities.edited();
	auto loadedPriorities = std::make_shared<std::vector<AttackPriorityStore::SavedEntry>>();
	if (v.current >= 6)
	{
		x.xferBool(&priorities);
	}
	if (priorities)
	{
		std::vector<AttackPriorityStore::SavedEntry> entries = loading ? std::vector<AttackPriorityStore::SavedEntry>() : m_attackPriorities.saved();
		n = (std::uint32_t)entries.size();
		x.xferCount(&n, 1 + 4 + 4, sizeof(AttackPriorityStore::SavedEntry));
		if (loading)
		{
			if (n > 0xFF)
			{
				throw XferException("script engine: " + std::to_string(n) + " attack priority sets (at most 255)");
			}
			entries.resize(n);
		}
		for (AttackPriorityStore::SavedEntry &e : entries)
		{
			x.xferAsciiString(&e.name);
			x.xferInt(&e.defaultPriority);
			std::uint32_t m = (std::uint32_t)e.priorities.size();
			x.xferCount(&m, 1 + 4, sizeof(std::pair<const std::string, int>) + Xfer::kNodeBytes);
			if (!loading)
			{
				for (auto &kv : e.priorities)
				{
					std::string name = kv.first;
					x.xferAsciiString(&name);
					x.xferInt(&kv.second);
				}
				continue;
			}
			for (std::uint32_t j = 0; j < m; ++j)
			{
				std::string name;
				std::int32_t value = 0;
				x.xferAsciiString(&name);
				x.xferInt(&value);
				if (!e.priorities.emplace(name, value).second)
				{
					throw XferException("script engine: the attack priority set '" + e.name + "' names '" + name + "' twice");
				}
				if (!m_logic.things().findTemplate(name))
				{
					throw XferException("script engine: the attack priority set '" + e.name + "' names the thing '" + name + "', which this game lacks");
				}
			}
		}
		if (loading)
		{
			*loadedPriorities = std::move(entries);
		}
	}
	if (!loading)
	{
		return;
	}
	// names given once (each list is keyed by its name in the engine)
	auto unique = [](std::vector<std::string> names, const std::string &what) {
		std::sort(names.begin(), names.end());
		auto d = std::adjacent_find(names.begin(), names.end());
		if (d != names.end())
		{
			throw XferException("script engine: the " + what + " '" + *d + "' twice");
		}
	};
	std::vector<std::string> names;
	for (const auto &l : lists)
	{
		names.push_back(l.first);
		unique(l.second, "object type of list '" + l.first + "'");
	}
	unique(names, "object list");
	names.clear();
	for (const auto &e : named)
	{
		names.push_back(e.first);
	}
	unique(names, "named object");
	names.clear();
	for (const AudioTimer &t : timers)
	{
		names.push_back(t.name);
	}
	unique(names, "audio timer");

	// every check of the stream passed: the loaded state waits for finishLoad (after every block loaded: the teams it names are checked, then the engine
	// takes it all at once; a refusal there leaves the engine as it was)
	auto staged = std::make_shared<std::tuple<std::vector<TreeFlags>, std::map<int, std::vector<GenericCopy>>,
		std::map<std::pair<std::string, std::string>, Counter>, std::map<std::pair<std::string, std::string>, bool>,
		std::vector<std::pair<std::string, std::vector<std::string>>>, std::vector<std::pair<std::string, ObjectID>>,
		std::vector<std::vector<SequentialScript>>, std::array<AcquiredScienceQueue, 20>, std::vector<AudioTimer>,
		std::array<std::vector<TriggeredSpecialPower>, kSpecialPowerListPlayers>, std::map<std::pair<std::string, std::string>, ObjectID>,
		std::array<std::vector<TriggeredSpecialPower>, kSpecialPowerListPlayers>>>(std::move(tree), std::move(loadedGeneric), std::move(loadedCounters),
		std::move(loadedFlags), std::move(loadedLists), std::move(loadedNamed), std::move(loadedSequential), loadedQueues, std::move(loadedTimers),
		std::move(loadedTriggered), std::move(loadedReferences), std::move(loadedUpgrades));
	m_pendingTeams.clear();
	for (const auto &chain : std::get<6>(*staged))
	{
		for (const SequentialScript &r : chain)
		{
			if (r.team != 0)
			{
				m_pendingTeams.push_back(r.team);
			}
		}
	}
	m_pendingLoad = [this, staged, firstUpdate, difficulty, bonus, runtimeExtent, priorities, loadedPriorities]() {
		while (m_runtimeTriggers.size() < runtimeExtent)
		{
			RuntimeTrigger empty; // lane CASTLEAREA-1: a removed slot past the last owner's trigger
			empty.area = std::make_shared<TriggerArea>();
			m_runtimeTriggers.push_back(std::move(empty));
		}
		m_firstUpdate = firstUpdate;
		m_gameDifficulty = difficulty;
		m_objectsReceiveDifficultyBonus = bonus;
		for (size_t i = 0; i < m_sides.size(); ++i)
		{
			size_t script = 0, group = 0;
			applyTree(m_sides[i].root, std::get<0>(*staged)[i], script, group);
		}
		m_genericScripts = std::move(std::get<1>(*staged));
		m_counters = std::move(std::get<2>(*staged));
		m_flags = std::move(std::get<3>(*staged));
		m_objectLists = std::move(std::get<4>(*staged));
		m_namedObjects = std::move(std::get<5>(*staged));
		m_sequential = std::move(std::get<6>(*staged));
		m_acquiredSciences = std::get<7>(*staged);
		m_audioTimers = std::move(std::get<8>(*staged));
		m_triggeredSpecialPowers = std::move(std::get<9>(*staged)); // lane SAVE-4e
		m_references = std::move(std::get<10>(*staged));             // lane CASTLE-UNPACK
		m_completedUpgrades = std::move(std::get<11>(*staged));      // lane MODCOMPAT-AI
		if (priorities) // lane BFME2CAMP1: the table checked above (every name a template of this game)
		{
			m_attackPriorities.restore(*loadedPriorities, nullptr);
		}
		else
		{
			m_attackPriorities.reset(); // an unedited table: the AIData's entries are made again on use
		}
	};
}

void ScriptEngine::finishLoad()
{
	std::function<void()> commit = std::move(m_pendingLoad);
	m_pendingLoad = nullptr;
	const std::vector<std::uint32_t> teams = std::move(m_pendingTeams);
	m_pendingTeams.clear();
	if (!commit)
	{
		throw XferException("script engine: nothing loaded to finish");
	}
	for (std::uint32_t team : teams)
	{
		if (!m_logic.players().teams().findTeamByID(team))
		{
			throw XferException("script engine: a sequential script of team " + std::to_string(team) + ", which this game lacks"); // RW 0x603382: code 5
		}
	}
	commit();
}
