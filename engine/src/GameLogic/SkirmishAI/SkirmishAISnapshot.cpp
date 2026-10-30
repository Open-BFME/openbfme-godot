// OpenBFME. GPL-3.0.
//
// The obfme:SkirmishAI block (lane SAVE-4c, docs/SAVES.md section 8): TheSkirmishAIManager's state. OpenBFME native layout (stop S-4220): retail saves
// CHUNK_SkirmishAISystem (lists 0 and 1, GameState::init RW 0x6DF73E) through AIPlayer::xfer RW 0x8F5756 ((1, 2)), AISkirmishPlayer::xfer RW 0x8F3F6C
// ((1, 1): AIPlayer's, then two ints and six reals) and AIBuilder::DoXfer RW 0x8F03FA ((1, 5): the dozer manager, base, economy and wall builders;
// docs/SAVES-RETAIL.md section 6). This port's skirmish AI is a partial port (S-412: the sub systems are structs of what is ported, laid out by the port), so
// the block is OpenBFME's own, in this order:
//   the started flag, the attacked-at id counter, the spell book assist timers, the data's identity (SkirmishAIStore::crc: a snapshot of another data set is
//   refused), the player info players and records, every build request ONCE (the request table: the farm site pool, the base slots, the queues, the unit
//   and economy builders share requests, so every list refers to the table by index), the farm site pool, the groups with every AI, the errors and the
//   counters of the report.
// Pointers and data references: an AI's army is found again by its side (SkirmishAIStore::findArmy, the lookup addAIPlayer made), a base's template by its
// index among its side's base templates (SkirmishAIStore::baseTemplatesOfSide, the list AIBaseBuilder::init chose from), the group's first AI by its
// player index, the store and the farm site pool are the manager's. Objects are ids (the AI resolves them when it uses them, as the live game does).

#include "GameLogic/SkirmishAI/SkirmishAIManager.h"

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/StateHash.h"
#include "GameLogic/AI/AIXfer.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SkirmishAI/SkirmishAIData.h"
#include "GameLogic/SnapshotXfer.h"

#include <map>
#include <set>

namespace
{
// the request table: every AIBuildRequest reachable from the manager, numbered in the order a save first meets it
struct RequestTable
{
	std::vector<std::shared_ptr<AIBuildRequest>> list;
	std::map<const AIBuildRequest *, std::uint32_t> index; ///< looked up only, never iterated

	void note(const std::shared_ptr<AIBuildRequest> &r)
	{
		if (!r)
		{
			throw XferException("skirmish ai: a request list holds an empty entry");
		}
		if (index.emplace(r.get(), (std::uint32_t)list.size()).second)
		{
			list.push_back(r);
		}
	}
	void noteAll(const std::vector<std::shared_ptr<AIBuildRequest>> &v)
	{
		for (const auto &r : v)
		{
			note(r);
		}
	}
};

void xferRequest(Xfer &x, AIBuildRequest &r, int version)
{
	x.xferAsciiString(&r.templateName);
	x.xferAsciiString(&r.name);
	x.xferReal(&r.priority);
	x.xferReal(&r.effectivePriority);
	x.xferObjectID(&r.producer);
	x.xferInt(&r.state);
	x.xferObjectID(&r.built);
	AIXfer::coord(x, r.offset);
	x.xferReal(&r.angle);
	AIXfer::coord(x, r.basePosition);
	x.xferReal(&r.baseAngle);
	x.xferInt(&r.slot);
	x.xferBool(&r.counts);
	x.xferUnsignedInt(&r.rebuildFrame);
	x.xferInt(&r.starts);
	x.xferInt(&r.lastCheck);
	x.xferBool(&r.unit);
	x.xferInt(&r.commandPoints);
	x.xferUnsignedInt(&r.productionID);
	x.xferUnsignedInt(&r.serial);
	x.xferBool(&r.farm);
	x.xferBool(&r.inUse);
	x.xferInt(&r.owner);
	x.xferInt(&r.failures);
	x.xferInt(&r.siteIndex);
	x.xferBool(&r.requeueOnProducerDeath);
	x.xferBool(&r.reserveMoney);
	// version 2 (lane AI5): the upgrade / expansion request
	x.xferBool(&r.upgrade);
	x.xferBool(&r.expansion);
	x.xferAsciiString(&r.buttonName);
	x.xferInt(&r.cost);
	x.xferBool(&r.upgrading);
	x.xferBool(&r.expansionDone);
	x.xferInt(&r.heuristicType);
	x.xferInt(&r.heuristicResult);
	x.xferUnsignedInt(&r.heuristicNextFrame);
	x.xferInt(&r.heuristicFortressResult);
	AIXfer::strings(x, r.heuristicUnlocks);
	if (version >= 4)
	{
		x.xferBool(&r.plain); // version 4 (lane AIEXPAND-1): SimpleExpansion's plain structure request
	}
	if (x.isLoading())
	{
		AIXfer::inRange(r.state, 0, 3, "skirmish ai: a build request's state"); // waiting, in progress, complete, failed
		AIXfer::inRange(r.heuristicType, -1, 7, "skirmish ai: an upgrade request's heuristic"); // none and the 8 of RW 0xDB4F44
		AIXfer::inRange(r.heuristicResult, 0, 3, "skirmish ai: an upgrade request's heuristic result");          // the heuristics answer 0 .. 3
		AIXfer::inRange(r.heuristicFortressResult, 0, 3, "skirmish ai: a FORTRESS heuristic's result");          // 3, or 0 once drawn (RW 0x9ED374)
		if (r.upgrade && r.expansion)
		{
			throw XferException("skirmish ai: a request that is both an upgrade and an expansion");
		}
		if (r.plain && (r.upgrade || r.expansion || r.unit || r.farm))
		{
			throw XferException("skirmish ai: a plain structure request that is also another kind");
		}
	}
}

// a list of requests by table index
// one optional request (a base's fortress task): present flag, then the table index
void xferRef(Xfer &x, std::shared_ptr<AIBuildRequest> &r, RequestTable &t)
{
	bool present = r != nullptr;
	x.xferBool(&present);
	if (!present)
	{
		r.reset();
		return;
	}
	std::uint32_t i = x.isLoading() ? 0 : t.index.at(r.get());
	x.xferUnsignedInt(&i);
	if (x.isLoading())
	{
		if (i >= t.list.size())
		{
			throw XferException("skirmish ai: a request reference " + std::to_string(i) + " beyond the " + std::to_string(t.list.size()) + " requests");
		}
		r = t.list[i];
	}
}

void xferRefs(Xfer &x, std::vector<std::shared_ptr<AIBuildRequest>> &v, RequestTable &t)
{
	x.xferVectorSize(v, 4);
	for (std::shared_ptr<AIBuildRequest> &r : v)
	{
		std::uint32_t i = x.isLoading() ? 0 : t.index.at(r.get());
		x.xferUnsignedInt(&i);
		if (x.isLoading())
		{
			if (i >= t.list.size())
			{
				throw XferException("skirmish ai: a request reference " + std::to_string(i) + " beyond the " + std::to_string(t.list.size()) + " requests");
			}
			r = t.list[i];
		}
	}
}

void xferThreat(Xfer &x, AIThreatFinder::ThreatRecord &r)
{
	for (float &f : r.v)
	{
		x.xferReal(&f);
	}
}

void xferBuild(Xfer &x, AIBuildState &s, RequestTable &t, const SkirmishAIStore &store, const std::string &side)
{
	x.xferBool(&s.baseBuilderDisabled);
	x.xferBool(&s.baseBuilderInitialised);
	AIXfer::coord(x, s.startPosition);
	x.xferVectorSize(s.bases, 30); // the template index, an empty map name, the index, the position, the angle, the fortress flag, the slot count
	const std::vector<const AIBaseTemplate *> sideBases = store.baseTemplatesOfSide(side);
	for (AIBaseInstance &b : s.bases)
	{
		std::int32_t tmpl = -1;
		if (!x.isLoading() && b.tmpl)
		{
			for (size_t i = 0; i < sideBases.size(); ++i)
			{
				tmpl = sideBases[i] == b.tmpl ? (std::int32_t)i : tmpl;
			}
			if (tmpl < 0)
			{
				throw XferException("skirmish ai: a base's template '" + b.templateMap + "' is not one of side '" + side + "'s");
			}
		}
		x.xferInt(&tmpl);
		if (x.isLoading())
		{
			if (tmpl < -1 || tmpl >= (std::int32_t)sideBases.size())
			{
				throw XferException("skirmish ai: base template " + std::to_string(tmpl) + " of side '" + side + "', which has " + std::to_string(sideBases.size()));
			}
			b.tmpl = tmpl < 0 ? nullptr : sideBases[(size_t)tmpl];
		}
		x.xferAsciiString(&b.templateMap);
		x.xferInt(&b.index);
		AIXfer::coord(x, b.position);
		x.xferReal(&b.angle);
		xferRef(x, b.fortress, t); // the fortress rebuild task (RW AIBase + 0x28, lane BACKLOG6)
		x.xferVectorSize(b.slots, 11);
		for (AIBaseSlot &slot : b.slots)
		{
			x.xferBool(&slot.done);
			x.xferBool(&slot.started);
			x.xferBool(&slot.boosted);
			x.xferInt(&slot.index);
			xferRefs(x, slot.entries, t);
		}
	}
	xferRefs(x, s.pending, t);
	xferRefs(x, s.inProgress, t);
	AIXfer::ids(x, s.freeDozers);
	AIXfer::ids(x, s.dozerManager.factories);
	x.xferBool(&s.dozerManager.lostDozer);
	AIXfer::u64(x, s.dozerManager.queued);
	AIXfer::u64(x, s.dozerManager.refused);
	AIUnitBuilderState &u = s.units;
	x.xferBool(&u.disabled);
	AIXfer::multimap(x, u.factories, 8, [&](NameKeyType &k, ObjectID &id) {
		std::int32_t key = k;
		x.xferInt(&key);
		k = key;
		x.xferObjectID(&id);
	});
	AIXfer::map(x, u.composition, 8, [&](NameKeyType &k, float &v) {
		std::int32_t key = k;
		x.xferInt(&key);
		k = key;
		x.xferReal(&v);
	});
	xferRefs(x, u.requests, t);
	x.xferBool(&u.recompute);
	AIXfer::u64(x, u.made);
	AIXfer::u64(x, u.queued);
	AIXfer::u64(x, u.executed);
	AIXfer::strings(x, u.heroNames);
	x.xferInt(&u.heroIndex);
	AIXfer::ints(x, u.removedHeroes);
	x.xferInt(&u.heroCost);
	x.xferBool(&u.heroInProgress);
	AIXfer::u64(x, u.heroesMade);
	AIEconomyState &e = s.economy;
	x.xferBool(&e.disabled);
	x.xferInt(&e.built);
	x.xferInt(&e.inFlight);
	x.xferInt(&e.lastFrame);
	xferRefs(x, e.requests, t);
	AIXfer::u64(x, e.made);
	AIXfer::u64(x, e.refusedByDifficulty);
	AIXfer::u64(x, e.noSite);
	// version 2 (lane AI5): the unit upgrader and the upgrade builder
	AIUnitUpgraderState &uu = s.unitUpgrader;
	x.xferBool(&uu.listening);
	x.xferBool(&uu.disabled);
	xferRefs(x, uu.waiting, t);
	xferRefs(x, uu.upgrading, t);
	AIXfer::u64(x, uu.registered);
	AIXfer::u64(x, uu.submitted);
	AIXfer::u64(x, uu.declined);
	AIUpgradeScienceBuilderState &sb = s.scienceBuilder;
	x.xferBool(&sb.listening);
	x.xferBool(&sb.disabled);
	AIXfer::ids(x, sb.pendingStructures);
	xferRefs(x, sb.upgrades, t);
	xferRefs(x, sb.expansions, t);
	x.xferUnsignedInt(&sb.lastExpansionFrame);
	AIXfer::u64(x, sb.registered);
	AIXfer::u64(x, sb.upgradesSubmitted);
	AIXfer::u64(x, sb.expansionsSubmitted);
	x.xferInt(&s.phase);
	if (x.isLoading())
	{
		AIXfer::inRange(s.phase, 0, 2, "skirmish ai: the game phase"); // AIBaseBuilder::updatePhase's 0, 1, 2
	}
	x.xferReal(&s.phaseFraction);
	x.xferBool(&s.ringHero);
	AIXfer::u64(x, s.executed);
	AIXfer::u64(x, s.failed);
	AIXfer::u64(x, s.waitedForMoney);
	AIXfer::u64(x, s.waitedForDozer);
	x.xferUnsignedInt(&s.nextSerial);
	AIXfer::u64(x, s.rebuilds);
	AIXfer::u64(x, s.requeuedForDozerDeath);
	AIXfer::u64(x, s.fortressRebuilds);
}

void xferBrain(Xfer &x, AIBrainState &b, RequestTable &table, int version)
{
	x.xferBool(&b.chooser);
	x.xferBool(&b.chooserInitialised);
	x.xferVectorSize(b.enemies, 8);
	for (auto &e : b.enemies)
	{
		x.xferInt(&e.first);
		x.xferReal(&e.second);
	}
	x.xferInt(&b.currentEnemy);
	x.xferVectorSize(b.targets, 50);
	for (AITarget &t : b.targets)
	{
		x.xferInt(&t.type);
		x.xferUnsignedInt(&t.frame);
		AIXfer::coord(x, t.position);
		x.xferBool(&t.inactive);
		x.xferBool(&t.dropped);
		x.xferInt(&t.teams);
		x.xferInt(&t.maxTeams);
		x.xferReal(&t.radius);
		AIXfer::coord(x, t.finderPosition);
		x.xferReal(&t.threat);
		x.xferObjectID(&t.object);
		x.xferUnsignedInt(&t.id);
		if (x.isLoading())
		{
			AIXfer::inRange(t.type, 0, 4, "skirmish ai: a target's AITARGET"); // RW 0xDA1208: 5 names
		}
	}
	x.xferUnsignedInt(&b.nextTargetId);
	x.xferInt(&b.bestTarget);
	if (x.isLoading())
	{
		AIXfer::inRange(b.bestTarget, -1, (long long)b.targets.size() - 1, "skirmish ai: the best target"); // review r2: checked after it is read
	}
	x.xferVectorSize(b.tactics, 90); // the fixed fields with an empty kind name and no teams or waypoints
	for (AITactic &tac : b.tactics)
	{
		x.xferAsciiString(&tac.kind);
		x.xferInt(&tac.kindIndex);
		x.xferUnsignedInt(&tac.id);
		x.xferInt(&tac.targetIndex);
		x.xferInt(&tac.targetOwner);
		if (x.isLoading())
		{
			// an offensive prototype 0 .. 7 or the expansion prototype 8 (version 4) by its name, or a targetless one (100 + 0 .. 10, AITacticalAI.cpp)
			static const char *const kOffensive[] = { "SimpleAttack", "FormationAttack", "FlankAttack", "PincerAttack", "FeintAttack",
				"AIBasePenetrationTroopsTactic", "SimpleSiege", "SiegeGates", "SimpleExpansion" };
			const int prototypes = version >= 4 ? 9 : 8;
			if (!(tac.kindIndex >= 0 && tac.kindIndex < prototypes && tac.kind == kOffensive[tac.kindIndex]) && !(tac.kindIndex >= 100 && tac.kindIndex < 111))
			{
				throw XferException("skirmish ai: a tactic '" + tac.kind + "' of the prototype " + std::to_string(tac.kindIndex));
			}
		}
		x.xferVectorSize(tac.teams, 50);
		for (AITacticTeam &team : tac.teams)
		{
			x.xferAsciiString(&team.name);
			x.xferInt(&team.type);
			x.xferInt(&team.minimum);
			x.xferInt(&team.maximum);
			AIXfer::ints(x, team.requiredKinds);
			AIXfer::ints(x, team.forbiddenKinds);
			x.xferInt(&team.priority);
			x.xferBool(&team.wantsMembers);
			x.xferBool(&team.handedOver);
			AIXfer::ids(x, team.members);
			AIXfer::coord(x, team.lastPosition);
			x.xferInt(&team.index);
			x.xferInt(&team.idleFrames);
			if (x.isLoading())
			{
				AIXfer::inRange(team.type, 0, 4, "skirmish ai: a tactic team's AITARGET");
			}
		}
		x.xferBool(&tac.started);
		x.xferBool(&tac.ended);
		x.xferBool(&tac.ready);
		x.xferUnsignedInt(&tac.readyFrame);
		x.xferBool(&tac.success);
		AIXfer::coord(x, tac.position);
		AIXfer::coord(x, tac.origin);
		x.xferBool(&tac.retreated);
		x.xferBool(&tac.targetless);
		x.xferObjectID(&tac.squadTarget);
		x.xferInt(&tac.step);
		x.xferInt(&tac.step2);
		AIXfer::coords(x, tac.waypoints);
		AIXfer::coord(x, tac.gatherPos);
		AIXfer::coord(x, tac.aimPos);
		x.xferBool(&tac.mainLaunched);
		x.xferUnsignedInt(&tac.launchFrame);
		if (version >= 4)
		{
			xferRef(x, tac.request, table); // version 4 (lane AIEXPAND-1): SimpleExpansion's request (+ 0x58)
			if (x.isLoading() && tac.request && tac.kindIndex != 8)
			{
				throw XferException("skirmish ai: a tactic '" + tac.kind + "' with a build request (only SimpleExpansion has one)");
			}
		}
	}
	x.xferUnsignedInt(&b.nextTacticId);
	AIXfer::map(x, b.tacticsByKind, 5, [&](std::string &k, int &v) {
		x.xferAsciiString(&k);
		x.xferInt(&v);
	});
	x.xferBool(&b.generatorMade);
	x.xferBool(&b.farmKillEarly);
	AIXfer::map(x, b.namedCounters, 5, [&](std::string &k, int &v) {
		x.xferAsciiString(&k);
		x.xferInt(&v);
	});
	for (unsigned long long *c : { &b.targetlessPicks, &b.targetlessStarted, &b.targetlessUnported })
	{
		AIXfer::u64(x, *c);
	}
	x.xferVectorSize(b.attackedAt, 168);
	for (AIAttackedAt &a : b.attackedAt)
	{
		x.xferUnsignedInt(&a.id);
		AIXfer::coord(x, a.position);
		x.xferReal(&a.radius);
		x.xferUnsignedInt(&a.frame);
		xferThreat(x, a.enemies);
		xferThreat(x, a.others);
	}
	xferThreat(x, b.idleArmy);
	AIXfer::idSet(x, b.dispatched);
	for (unsigned long long *c : { &b.idleDispatches, &b.gateDraws, &b.gateRejects, &b.retreats, &b.merges, &b.mergeTargetlessUnported, &b.targetsSet,
			 &b.tacticsStarted, &b.attacksLaunched, &b.tacticsEnded, &b.gateRefusals, &b.unportedTypes })
	{
		AIXfer::u64(x, *c);
	}
}
} // namespace

void AIStatCollector::xfer(Xfer &x)
{
	XferVersion v{ 2, 2 }; // version 2 (lane AI5): the money reserve and its target (the block's version 2 refuses older snapshots)
	x.xferVersion(v);
	for (std::vector<ObjectID> *l : { &m_army, &m_dozers, &m_ships, &m_structures, &m_pads, &m_economy, &m_heroes })
	{
		AIXfer::ids(x, *l);
	}
	AIXfer::map(x, m_counts, 5, [&](std::string &k, int &n) {
		x.xferAsciiString(&k);
		x.xferInt(&n);
	});
	x.xferUnsignedInt(&m_reserve);
	x.xferUnsignedInt(&m_reserveTarget);
}

void SkirmishAIManager::xferState(Xfer &x, bool hooksInstalled)
{
	// version 2 (lane AI5): the upgrade / expansion requests and the unit upgrader / upgrade builder states. Version 3 (community #113) adds the capture-flag
	// history, which cannot be recovered from current KindOf flags: older snapshots are refused. Version 4 (lane AIEXPAND-1): the EXPANSION heuristic's frame
	// counter, SimpleExpansion's request and plain structure requests. A version 3 snapshot comes from a build without the heuristic: it loads with 0, the
	// count at a game's start (its first expansion check then comes 301 checks after the load)
	XferVersion v{ 3, 4 };
	x.xferVersion(v);
	const bool loading = x.isLoading();
	bool started = loading ? false : m_started;
	x.xferBool(&started);
	if (loading && started != hooksInstalled)
	{
		// the hooks are installed by newGame, which the game's START ran: a snapshot whose AI ran in a game whose AI did not (or the reverse) is another game's
		throw XferException(std::string("skirmish ai: the snapshot's skirmish AI ") + (started ? "runs" : "does not run") + ", this game's " +
			(hooksInstalled ? "does" : "does not"));
	}
	m_started = started;
	x.xferUnsignedInt(&m_attackedAtIds);
	if (v.current >= 4)
	{
		x.xferUnsignedInt(&m_expansionHeuristicFrames);
	}
	x.xferVectorSize(m_assistTimers, 8);
	for (auto &t : m_assistTimers)
	{
		x.xferInt(&t.first);
		AIXfer::uns(x, t.second);
	}
	// the data the AI decides from is the game's (the START's identity): its hash must be the saved one
	bool hasStore = m_store != nullptr;
	std::uint32_t storeHash = 0;
	if (m_store)
	{
		StateHasher h;
		m_store->crc(h);
		storeHash = h.value();
	}
	const bool ownStore = hasStore;
	const std::uint32_t ownHash = storeHash;
	x.xferBool(&hasStore);
	x.xferUnsignedInt(&storeHash);
	if (loading && (hasStore != ownStore || storeHash != ownHash))
	{
		throw XferException("skirmish ai: the snapshot was made with other SkirmishAIData (or none): this game's data differs");
	}
	AIXfer::ints(x, m_infoPlayers);
	std::uint32_t n = (std::uint32_t)m_records.size();
	x.xferCount(&n, 37, sizeof(AIStatCollector) + Xfer::kNodeBytes); // the player index and an empty collector
	if (loading)
	{
		m_records.clear();
		for (std::uint32_t i = 0; i < n; ++i)
		{
			std::int32_t player = 0;
			x.xferInt(&player);
			auto r = std::make_unique<AIStatCollector>(player);
			r->xfer(x);
			if (!m_records.emplace(player, std::move(r)).second)
			{
				throw XferException("skirmish ai: two player info records of player " + std::to_string(player));
			}
		}
	}
	else
	{
		for (auto &kv : m_records)
		{
			std::int32_t player = kv.first;
			x.xferInt(&player);
			kv.second->xfer(x);
		}
	}
	// the request table
	RequestTable table;
	if (!loading)
	{
		table.noteAll(m_farmSites);
		for (const auto &g : m_groups)
		{
			for (const auto &kv : g->players)
			{
				const AIBuildState &s = kv.second->build;
				for (const AIBaseInstance &b : s.bases)
				{
					if (b.fortress)
					{
						table.note(b.fortress);
					}
					for (const AIBaseSlot &slot : b.slots)
					{
						table.noteAll(slot.entries);
					}
				}
				table.noteAll(s.pending);
				table.noteAll(s.inProgress);
				table.noteAll(s.units.requests);
				table.noteAll(s.economy.requests);
				table.noteAll(s.unitUpgrader.waiting);
				table.noteAll(s.unitUpgrader.upgrading);
				table.noteAll(s.scienceBuilder.upgrades);
				table.noteAll(s.scienceBuilder.expansions);
				for (const AITactic &tac : kv.second->brain.tactics)
				{
					if (tac.request)
					{
						table.note(tac.request); // lane AIEXPAND-1: SimpleExpansion's request (also queued while unfinished)
					}
				}
			}
		}
	}
	std::uint32_t requests = (std::uint32_t)table.list.size();
	x.xferCount(&requests, 100, sizeof(AIBuildRequest) + 32); // a request with empty names; its shared control block
	if (loading)
	{
		for (std::uint32_t i = 0; i < requests; ++i)
		{
			table.list.push_back(std::make_shared<AIBuildRequest>());
		}
	}
	for (const std::shared_ptr<AIBuildRequest> &r : table.list)
	{
		xferRequest(x, *r, v.current);
	}
	xferRefs(x, m_farmSites, table);
	// the groups
	n = (std::uint32_t)m_groups.size();
	x.xferCount(&n, 13, sizeof(AISkirmishGroup));
	if (loading)
	{
		m_groups.clear();
		for (std::uint32_t i = 0; i < n; ++i)
		{
			m_groups.push_back(std::make_unique<AISkirmishGroup>());
		}
	}
	for (std::unique_ptr<AISkirmishGroup> &g : m_groups)
	{
		x.xferBool(&g->active);
		x.xferInt(&g->lastLiveCount);
		AIXfer::ints(x, g->members);
		std::uint32_t ais = (std::uint32_t)g->players.size();
		x.xferCount(&ais, 4, sizeof(AISkirmishPlayer) + Xfer::kNodeBytes); // the player index here (the AI's fields follow below)
		std::vector<AISkirmishPlayer *> order;
		if (loading)
		{
			for (std::uint32_t i = 0; i < ais; ++i)
			{
				auto ai = std::make_unique<AISkirmishPlayer>();
				x.xferInt(&ai->playerIndex);
				AISkirmishPlayer *raw = ai.get();
				if (!g->players.emplace(raw->playerIndex, std::move(ai)).second)
				{
					throw XferException("skirmish ai: two AIs of player " + std::to_string(raw->playerIndex) + " in one group");
				}
				order.push_back(raw);
			}
		}
		else
		{
			for (auto &kv : g->players)
			{
				x.xferInt(&kv.second->playerIndex);
				order.push_back(kv.second.get());
			}
		}
		AISkirmishPlayer *first = nullptr;
		for (AISkirmishPlayer *ai : order)
		{
			x.xferAsciiString(&ai->armySide);
			bool hasArmy = ai->army != nullptr;
			x.xferBool(&hasArmy);
			if (loading)
			{
				if (!m_store)
				{
					throw XferException("skirmish ai: an AI to load and no SkirmishAIData");
				}
				ai->store = m_store;
				ai->army = m_store->findArmy(ai->armySide);
				if ((ai->army != nullptr) != hasArmy)
				{
					throw XferException("skirmish ai: side '" + ai->armySide + "' " + (hasArmy ? "has no" : "has an") + " ArmyDefinition in this game's data");
				}
				ai->farmSites = &m_farmSites;
			}
			x.xferBool(&ai->firstOfGroup);
			x.xferBool(&ai->disabled);
			x.xferReal(&ai->field170);
			x.xferUnsignedInt(&ai->creationFrame);
			x.xferInt(&ai->field178);
			x.xferAsciiString(&ai->dozerTemplate);
			x.xferUnsignedInt(&ai->shutdownFrame);
			x.xferUnsignedInt(&ai->retryTimer);
			AIXfer::inRange(ai->playerIndex, 0, m_logic.players().getPlayerCount() - 1, "skirmish ai: the player of an AI");
			const Player *player = m_logic.players().getNthPlayer(ai->playerIndex);
			if (!player)
			{
				throw XferException("skirmish ai: the AI of player " + std::to_string(ai->playerIndex) + ", which this game lacks");
			}
			if (!m_store)
			{
				throw XferException("skirmish ai: an AI to save and no SkirmishAIData");
			}
			xferBuild(x, ai->build, table, *m_store, player->getSide());
			xferBrain(x, ai->brain, table, v.current);
			first = ai->firstOfGroup ? ai : first;
		}
		if (loading)
		{
			for (AISkirmishPlayer *ai : order)
			{
				ai->groupFirst = first;
			}
		}
	}
	AIXfer::strings(x, m_errors);
	for (unsigned long long *c : { &m_counters.managerUpdates, &m_counters.gatedUpdates, &m_counters.playerInfoUpdates, &m_counters.groupUpdates,
			 &m_counters.sharedBrainUpdates, &m_counters.aiUpdates, &m_counters.aiPlayerUpdates, &m_counters.brainUpdates, &m_counters.disabledSkips,
			 &m_counters.shutdowns, &m_counters.objectAdds, &m_counters.objectRemoves })
	{
		AIXfer::u64(x, *c);
	}
	AIXfer::ids(x, m_captureFlags);
}

// rule 4 (validate before linking): every player index and every reference between the loaded AIs (the info records, the groups' members, the enemies,
// a tactic's target owner and target, a request's owner) names what the loaded state holds, before the manager takes it
void SkirmishAIManager::checkLoaded() const
{
	const int players = m_logic.players().getPlayerCount();
	AIXfer::IssuedIdScope issuedIds(m_logic.getNextObjectID());
	std::set<ObjectID> flags;
	for (ObjectID id : m_captureFlags)
	{
		if (id == INVALID_ID) { throw XferException("skirmish ai: an invalid registered capture flag"); }
		AIXfer::issued(id, "skirmish ai: a registered capture flag");
		if (!flags.insert(id).second) { throw XferException("skirmish ai: repeated registered capture flag"); }
	}
	auto player = [&](int i, const char *what) { AIXfer::inRange(i, 0, players - 1, what); };
	std::set<int> info(m_infoPlayers.begin(), m_infoPlayers.end());
	if (info.size() != m_infoPlayers.size())
	{
		throw XferException("skirmish ai: a player is twice among the player info players");
	}
	for (int i : m_infoPlayers)
	{
		player(i, "skirmish ai: a player info player");
	}
	std::set<int> records;
	for (const auto &kv : m_records)
	{
		player(kv.first, "skirmish ai: the player of a player info record");
		records.insert(kv.first);
	}
	if (records != info)
	{
		throw XferException("skirmish ai: the player info records are not the player info players'");
	}
	std::map<int, const AISkirmishPlayer *> ais; // looked up only
	for (const auto &g : m_groups)
	{
		std::set<int> members(g->members.begin(), g->members.end()), keys;
		for (const auto &kv : g->players)
		{
			keys.insert(kv.first);
			if (!ais.emplace(kv.first, kv.second.get()).second)
			{
				throw XferException("skirmish ai: player " + std::to_string(kv.first) + " has an AI in two groups");
			}
		}
		if (members.size() != g->members.size() || members != keys)
		{
			throw XferException("skirmish ai: a group's members are not its AIs' players");
		}
		// the group's first AI makes the target chooser (RW 0x6C6DB1): exactly one per group of AIs
		int firsts = 0;
		for (const auto &kv : g->players)
		{
			firsts += kv.second->firstOfGroup ? 1 : 0;
			if (kv.second->brain.chooser != kv.second->firstOfGroup)
			{
				throw XferException("skirmish ai: player " + std::to_string(kv.first) + "'s target chooser does not follow its place in the group");
			}
		}
		if (!g->players.empty() && firsts != 1)
		{
			throw XferException("skirmish ai: a group has " + std::to_string(firsts) + " first AIs");
		}
	}
	for (const auto &kv : ais)
	{
		const AIBrainState &b = kv.second->brain;
		if (b.currentEnemy != -1)
		{
			player(b.currentEnemy, "skirmish ai: the current enemy");
		}
		for (const auto &e : b.enemies)
		{
			player(e.first, "skirmish ai: an enemy");
		}
		for (const AITactic &tac : b.tactics)
		{
			if (tac.targetOwner == -1)
			{
				continue;
			}
			auto owner = ais.find(tac.targetOwner);
			if (owner == ais.end())
			{
				throw XferException("skirmish ai: a tactic's target belongs to player " + std::to_string(tac.targetOwner) + ", which has no AI");
			}
			AIXfer::inRange(tac.targetIndex, -1, (long long)owner->second->brain.targets.size() - 1, "skirmish ai: a tactic's target");
		}
	}
	// the assist timers: one per player, each a player of the game (RW 0x9E880B appends a player once)
	std::set<int> assisted;
	for (const auto &t : m_assistTimers)
	{
		player(t.first, "skirmish ai: the player of a spell book assist timer");
		if (!assisted.insert(t.first).second)
		{
			throw XferException("skirmish ai: player " + std::to_string(t.first) + " has two spell book assist timers");
		}
	}
	// objects the AI keeps up to date with the object events (RW 0x6A9D9C / 0x6A99CB): a request's producer, the free dozers, the dozer and unit
	// factories, the player info records' lists. They must be objects of the loaded game
	auto object = [&](ObjectID id, const char *what) {
		if (id != INVALID_ID && !m_logic.findObjectByID(id))
		{
			throw XferException(std::string("skirmish ai: ") + what + " " + std::to_string(id) + " is not an object of the loaded game");
		}
	};
	// a live object an AI acts through (a unit factory, a dozer factory, a free dozer, a tactic team member, an object of its info record) must be that
	// AI's player's (review r3: an issued id alone let a snapshot queue units at a rival's factory with the rival's money). The live game keeps this true
	// through a change of owner (SkirmishAIManager::objectAdded takes the object out of every other AI) and a death (the info records unregister it, the
	// dozer lists drop it; a dead factory or member stays listed as an id the game issued, S-416)
	auto owned = [&](ObjectID id, int playerIndex, const char *what) {
		const Object *o = id != INVALID_ID ? m_logic.findObjectByID(id) : nullptr;
		const int owner = o && o->getControllingPlayer() ? o->getControllingPlayer()->getPlayerIndex() : -1;
		if (o && owner != playerIndex)
		{
			throw XferException(std::string("skirmish ai: ") + what + " " + std::to_string(id) + " belongs to player " + std::to_string(owner) +
				", not to the AI's player " + std::to_string(playerIndex));
		}
	};
	std::set<const AIBuildRequest *> requests;
	auto request = [&](const std::shared_ptr<AIBuildRequest> &r) {
		if (!requests.insert(r.get()).second)
		{
			return;
		}
		// the producer may be a dozer or factory that died since (a finished request, a factory's death keeps it; looked up when used, RW 0x8F0BBC
		// clears it only for a dozer's unfinished request): an id the game has issued
		AIXfer::issued(r->producer, "skirmish ai: the producer of a build request");
		AIXfer::issued(r->built, "skirmish ai: the structure of a build request");
		if (r->owner != -1)
		{
			player(r->owner, "skirmish ai: the AI using a farm site");
		}
		if (r->farm)
		{
			AIXfer::inRange(r->siteIndex, 0, (long long)m_farmSites.size() - 1, "skirmish ai: a farm request's site");
		}
	};
	for (const auto &r : m_farmSites)
	{
		request(r);
	}
	for (const auto &kv : ais)
	{
		const AISkirmishPlayer &ai = *kv.second;
		const AIBuildState &s = ai.build;
		for (const AIBaseInstance &base : s.bases)
		{
			if (base.fortress)
			{
				request(base.fortress);
			}
			for (const AIBaseSlot &slot : base.slots)
			{
				for (const auto &r : slot.entries)
				{
					request(r);
				}
			}
		}
		for (const auto *list : { &s.pending, &s.inProgress, &s.units.requests, &s.economy.requests, &s.unitUpgrader.waiting, &s.unitUpgrader.upgrading,
				 &s.scienceBuilder.upgrades, &s.scienceBuilder.expansions })
		{
			for (const auto &r : *list)
			{
				request(r);
			}
		}
		for (const AITactic &tac : ai.brain.tactics)
		{
			if (tac.request)
			{
				request(tac.request);
			}
		}
		// a shut-down AI (disabled: its player defeated, RW 0x6C7981, or without an army) gets no more object events (RW 0x6C7779 / 0x6C778A) and never
		// runs its builders again (RW 0x6C79A1: the brain only), so its builder lists keep the ids of objects that died or changed owner since, as
		// retail's do: ids the game issued. A running AI's are kept up to date by the events: live objects of its player
		for (ObjectID id : s.scienceBuilder.pendingStructures)
		{
			AIXfer::issued(id, "skirmish ai: a structure the upgrade builder waits for"); // RW 0x99FCC4 drops a gone one
		}
		for (ObjectID id : s.freeDozers)
		{
			AIXfer::issued(id, "skirmish ai: a free dozer");
			if (!ai.disabled)
			{
				object(id, "a free dozer");
				owned(id, kv.first, "a free dozer");
			}
		}
		for (ObjectID id : s.dozerManager.factories)
		{
			AIXfer::issued(id, "skirmish ai: a dozer factory");
			if (!ai.disabled)
			{
				object(id, "a dozer factory");
				owned(id, kv.first, "a dozer factory");
			}
		}
		for (const auto &f : s.units.factories)
		{
			AIXfer::issued(f.second, "skirmish ai: a unit factory");
			if (!ai.disabled)
			{
				object(f.second, "a unit factory"); // RW 0x8F0C69 -> 0x9A051D erases a factory leaving the world or its owner
				owned(f.second, kv.first, "a unit factory");
			}
		}
		AIXfer::inRange(s.units.heroIndex, -1, (long long)s.units.heroNames.size() - 1, "skirmish ai: the next hero");
		for (int i : s.units.removedHeroes)
		{
			AIXfer::inRange(i, 0, (long long)s.units.heroNames.size() - 1, "skirmish ai: a built hero");
		}
		// a tactic's progress (AITacticalAI.cpp): FormationAttack's state 0 .. 3, FlankAttack's next waypoint 0 .. its waypoint count, PincerAttack's two
		// teams' waypoint codes ({ 0, 1, 3, 4 } and { 0, 2, 3, 4 }, RW 0x9B3EAB); the other prototypes keep both at 0
		// lane SAVE-4e (closed sets): the targets', tactics' and attack records' ids are unique and below their counters (the next id each issues)
		std::set<std::uint32_t> targetIds, tacticIds;
		for (const AITarget &tg : ai.brain.targets)
		{
			AIXfer::issued(tg.object, "skirmish ai: a target's object");
			if (tg.id >= ai.brain.nextTargetId || !targetIds.insert(tg.id).second)
			{
				throw XferException("skirmish ai: target id " + std::to_string(tg.id) + " repeated or not below the next target id " + std::to_string(ai.brain.nextTargetId));
			}
		}
		for (const AIAttackedAt &a : ai.brain.attackedAt)
		{
			if (a.id == 0 || a.id > m_attackedAtIds) // RW: r.id = ++counter (1 .. the counter)
			{
				throw XferException("skirmish ai: an attack record's id " + std::to_string(a.id) + " is not one of 1 .. " + std::to_string(m_attackedAtIds));
			}
		}
		for (ObjectID id : ai.brain.dispatched)
		{
			// the team builder's dispatch history (RW 0x9A3040's + 0x18): it gives no orders; a unit sent and then captured or dead stays named
			AIXfer::issued(id, "skirmish ai: a dispatched unit");
		}
		for (const AITactic &tac : ai.brain.tactics)
		{
			AIXfer::issued(tac.squadTarget, "skirmish ai: a squad's target");
			for (const AITacticTeam &team : tac.teams)
			{
				for (ObjectID id : team.members)
				{
					AIXfer::issued(id, "skirmish ai: a tactic team's member");
					owned(id, kv.first, "a tactic team's member"); // the AI orders them: its own while they live
				}
			}
			const std::string what = "skirmish ai: a " + tac.kind + " tactic's step";
			switch (tac.kindIndex)
			{
			case 1:
				AIXfer::inRange(tac.step, 0, 3, what.c_str());
				AIXfer::inRange(tac.step2, 0, 0, what.c_str());
				break;
			case 2:
				AIXfer::inRange(tac.step, 0, (long long)tac.waypoints.size(), what.c_str());
				AIXfer::inRange(tac.step2, 0, 0, what.c_str());
				break;
			case 3:
				if ((tac.step != 0 && tac.step != 1 && tac.step != 3 && tac.step != 4) || (tac.step2 != 0 && tac.step2 != 2 && tac.step2 != 3 && tac.step2 != 4))
				{
					throw XferException(what + "s " + std::to_string(tac.step) + " / " + std::to_string(tac.step2) + " are not PincerAttack's codes");
				}
				break;
			case 8:
				// lane AIEXPAND-1: SimpleExpansion's stages 0 .. 4 (RW 0x9B52BD); a running one has its request from stage 1 on (made with the move to 1, RW 0x9B51E9);
				// an ended one has none (its end ran the cleanup RW 0x9B5042; it is dropped at the next update, RW 0x90BA94) whatever its stage; the request is plain
				AIXfer::inRange(tac.step, 0, 4, what.c_str());
				AIXfer::inRange(tac.step2, 0, 0, what.c_str());
				if ((!tac.ended && tac.step > 0) != (tac.request != nullptr))
				{
					throw XferException("skirmish ai: a" + std::string(tac.ended ? "n ended" : "") + " SimpleExpansion at stage " + std::to_string(tac.step) +
						(tac.request ? " with a request" : " without its request"));
				}
				if (tac.request && !tac.request->plain)
				{
					throw XferException("skirmish ai: a SimpleExpansion whose request is not a plain structure request");
				}
				break;
			default:
				AIXfer::inRange(tac.step, 0, 0, what.c_str());
				AIXfer::inRange(tac.step2, 0, 0, what.c_str());
				break;
			}
			// lane SAVE-4e (closed sets): a tactic's id is unique and below the next one (tac.id = nextTacticId++); its teams' KindOf bits are KindOf names'
			if (tac.id >= ai.brain.nextTacticId || !tacticIds.insert(tac.id).second)
			{
				throw XferException("skirmish ai: tactic id " + std::to_string(tac.id) + " repeated or not below the next tactic id " + std::to_string(ai.brain.nextTacticId));
			}
			for (const AITacticTeam &team : tac.teams)
			{
				for (const std::vector<int> *bits : { &team.requiredKinds, &team.forbiddenKinds })
				{
					for (int bit : *bits)
					{
						AIXfer::inRange(bit, 0, SnapshotXfer::nameCount(TheKindOfNames) - 1, "skirmish ai: a tactic team's KindOf bit");
					}
				}
			}
		}
	}
	for (const auto &kv : m_records)
	{
		for (const std::vector<ObjectID> *list : kv.second->lists())
		{
			for (ObjectID id : *list)
			{
				object(id, "an object of a player info record");
				owned(id, kv.first, "an object of a player info record");
			}
		}
	}
}

void SkirmishAIManager::xfer(Xfer &x)
{
	if (!x.isLoading())
	{
		xferState(x, m_worldHooks != 0);
		return;
	}
	SkirmishAIManager loaded(m_logic);
	loaded.m_store = m_store;
	loaded.xferState(x, m_worldHooks != 0);
	AIXfer::blockReadToEnd(x, "skirmish ai"); // before the commit below (GameState's endBlock check comes after this xfer)
	loaded.checkLoaded();
	// valid: this manager takes the loaded state (its hooks, its store and its diagnostic sink stay)
	m_started = loaded.m_started;
	m_attackedAtIds = loaded.m_attackedAtIds;
	m_expansionHeuristicFrames = loaded.m_expansionHeuristicFrames;
	m_assistTimers = std::move(loaded.m_assistTimers);
	m_groups = std::move(loaded.m_groups);
	m_infoPlayers = std::move(loaded.m_infoPlayers);
	m_captureFlags = std::move(loaded.m_captureFlags);
	m_records = std::move(loaded.m_records);
	m_farmSites = std::move(loaded.m_farmSites);
	m_errors = std::move(loaded.m_errors);
	m_counters = loaded.m_counters;
	for (auto &g : m_groups)
	{
		for (auto &kv : g->players)
		{
			kv.second->farmSites = &m_farmSites;
		}
	}
}
