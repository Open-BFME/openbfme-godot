// OpenBFME unit tests. GPL-3.0.
// Lane AI-1, step 2: the skirmish AI base builder (GameLogic/SkirmishAI/AIBaseBuilder.h): the AIBase layouts of Bases.big, the choice of a layout, the
// angle that faces the map centre, the game phase, the request queue and a real skirmish where the computer builds its base through BUILD-1's dozers.
// Expected values follow the RW functions named in each test (hand-derived arithmetic), never this engine's own output.

#include "doctest.h"

#include "StartTestUtil.h"

#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/StateHash.h"
#include "GameClient/GUI/Skirmish/IniSkirmishSetupSource.h"
#include "GameClient/LiveGame.h"
#include "GameLogic/Module/DozerAIUpdate.h"
#include "GameLogic/NewGame/NewGame.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/SkirmishAI/AIBaseBuilder.h"
#include "GameLogic/SkirmishAI/SkirmishAIData.h"
#include "GameLogic/SkirmishAI/SkirmishAIManager.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"

#include <cmath>
#include <cstdio>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace
{
int templateIndex(const PlayerTemplateStore &store, const std::string &name)
{
	for (int i = 0; i < store.getPlayerTemplateCount(); ++i)
	{
		if (store.getNthPlayerTemplate(i)->getName() == name)
		{
			return i;
		}
	}
	return -1;
}

NewGameMessage aiMessage(const starttest::Shared &s, const std::string &aiFaction, std::uint32_t seed = 1234)
{
	NewGameMessage m;
	m.game.mapName = "maps/map mp evendim/map mp evendim.map";
	m.game.seed = seed;
	m.game.startingCash = 1500;
	SkirmishGameSlot &h = m.game.slots[0];
	h.state = SLOT_PLAYER;
	h.name = u"Human";
	h.playerTemplate = templateIndex(s.world->playerTemplates(), "FactionMen");
	h.startPos = 0;
	h.color = 0;
	h.teamNumber = 0;
	SkirmishGameSlot &c = m.game.slots[1];
	c.state = SLOT_MED_AI;
	c.name = u"Computer";
	c.playerTemplate = templateIndex(s.world->playerTemplates(), aiFaction);
	c.startPos = 1;
	c.color = 1;
	c.teamNumber = 1;
	return m;
}

struct BaseRun
{
	std::vector<std::uint32_t> hashes;
	std::string chosen;
	int phase = -1;
	unsigned long long executed = 0, failed = 0, money = 0, dozer = 0;
	std::vector<std::string> built; // template names of the AI's structures made by its dozers
	int completed = 0;
	std::vector<std::string> errors;
	unsigned long long unitRequests = 0, unitsQueued = 0;
	std::map<std::string, int> army; // the AI player's living units by template (not structures)
	size_t factories = 0;
};

void runBaseGame(starttest::Shared &s, const NewGameMessage &message, int frames, BaseRun &out, bool hashEveryFrame,
	const std::function<void(GameLogic &)> &perFrame = nullptr)
{
	static std::vector<MapCacheEntry> cache;
	std::string error;
	if (cache.empty())
	{
		REQUIRE_MESSAGE(IniSkirmishSetupSource::loadMapCache(*s.mount->fs, cache, &error), error);
	}
	NewGameStart start(RandomAlgorithm::ZH_CarryChain);
	REQUIRE_MESSAGE(NewGame::prepareNewGame(message, s.world->playerTemplates(), s.settings, cache, RandomAlgorithm::ZH_CarryChain, start, &error), error);
	ArchiveW3DFileSource source(*s.mount->fs);
	WW3DAssetManager assets(source);
	LiveGame game(*s.world, *s.mount->fs, assets, s.options);
	LiveGame::Options o;
	o.start = &start;
	REQUIRE_MESSAGE(game.load(o, &error), error);
	for (int i = 0; i < frames; ++i)
	{
		game.advance(0.2);
		if (perFrame)
		{
			perFrame(game.logic());
		}
		if (hashEveryFrame)
		{
			out.hashes.push_back(game.logic().computeStateHash());
		}
	}
	const SkirmishAIManager &m = game.logic().skirmishAI();
	REQUIRE(m.groups().size() == 1);
	const AISkirmishPlayer &ai = *m.groups()[0]->players.begin()->second;
	if (!ai.build.bases.empty())
	{
		out.chosen = ai.build.bases[0].templateMap;
		for (const AIBaseSlot &slot : ai.build.bases[0].slots)
		{
			for (const auto &e : slot.entries)
			{
				if (e->built != INVALID_ID)
				{
					out.built.push_back(e->templateName);
				}
				if (e->state == 2)
				{
					++out.completed;
				}
			}
		}
	}
	out.phase = ai.build.phase;
	out.executed = ai.build.executed;
	out.failed = ai.build.failed;
	out.money = ai.build.waitedForMoney;
	out.dozer = ai.build.waitedForDozer;
	out.errors = m.report();
	out.unitRequests = ai.build.units.made;
	out.unitsQueued = ai.build.units.queued;
	out.factories = ai.build.units.factories.size();
	const Player *p = game.logic().players().getNthPlayer(ai.playerIndex);
	for (Object *o = game.logic().getFirstObject(); o; o = o->getNextObject())
	{
		if (o->getControllingPlayer() == p && !o->isEffectivelyDead() && !o->isKindOfName("STRUCTURE") && o->isKindOfName("HORDE"))
		{
			++out.army[o->getTemplate()->getName()];
		}
	}
}
} // namespace

TEST_CASE("skirmish ai base: every AIBase reads its Bases.big layout (RW 0x82FA98) with priorities and slots")
{
	OPENBFME_REQUIRE_START(s);
	const SkirmishAIStore &ai = s->world->skirmishAI();
	size_t loaded = 0, entries = 0;
	std::set<int> slots;
	std::vector<std::string> emptyNames;
	for (const auto &kv : ai.bases())
	{
		loaded += kv.second.layoutLoaded ? 1 : 0;
		if (kv.second.structures.empty())
		{
			emptyNames.push_back(kv.second.map);
		}
		entries += kv.second.structures.size();
		for (const AIBaseStructure &st : kv.second.structures)
		{
			slots.insert(st.slot);
		}
	}
	MESSAGE("AIBase layouts: " << ai.bases().size() << " bases, " << loaded << " loaded, " << entries << " structures, slots " << slots.size());
	CHECK(loaded == ai.bases().size());
	CHECK(ai.bases().size() == 104);
	// one retail base gets no entries: its .bse's CastleTemplates chunk is named exactly like its AIBase Map (RW 0x82F3D5 routes by the case-sensitive name
	// key) but the chunk holds zero entries (review of 591ade1 corrected the earlier "chunk name differs" note)
	CHECK((emptyNames == std::vector<std::string>{ "AI BASE - DWARVES - Carn Dum" }));
	// the slots are the game phases' build groups (1 based); the slot numbers retail uses (an entry of slot n > 3 waits for a phase that never comes)
	std::string sl;
	for (int v : slots)
	{
		sl += std::to_string(v) + " ";
	}
	MESSAGE("slot numbers: " << sl);
	CHECK((slots == std::set<int>{ 1, 2, 3, 40 }));
}

TEST_CASE("skirmish ai base: the base faces the map centre (RW 0x90D3C7)")
{
	// extent 1000 x 1000, centre (500, 500); hand values: the angle of (centre - pos) minus PI/2, the sign from (x * 0 - y)
	CHECK(AIBaseBuilder::baseAngle(Coord3D{ 100.0f, 100.0f, 0.0f }, 1000.0f, 1000.0f) == doctest::Approx(-0.7853982f).epsilon(1e-5));
	CHECK(AIBaseBuilder::baseAngle(Coord3D{ 900.0f, 100.0f, 0.0f }, 1000.0f, 1000.0f) == doctest::Approx(0.7853982f).epsilon(1e-5));
	CHECK(AIBaseBuilder::baseAngle(Coord3D{ 100.0f, 900.0f, 0.0f }, 1000.0f, 1000.0f) == doctest::Approx(-2.3561945f).epsilon(1e-5));
	// at the centre: no normalisation, acos(0) = PI/2, minus PI/2 = 0
	CHECK(AIBaseBuilder::baseAngle(Coord3D{ 500.0f, 500.0f, 0.0f }, 1000.0f, 1000.0f) == doctest::Approx(0.0f));
}

TEST_CASE("skirmish ai base: the layout filter of RW 0x9BCA8E (map name, <ANY>, PlayerPositions)")
{
	AIBaseTemplate any, onMap, onMapPos2, other;
	any.gameMapToUseOn = "<ANY>";
	onMap.gameMapToUseOn = "map wor erebor.map";
	onMapPos2.gameMapToUseOn = "map wor erebor.map";
	onMapPos2.playerPositions = { 2, 4 };
	other.gameMapToUseOn = "map wor rivendell.map";
	const std::vector<const AIBaseTemplate *> side = { &any, &onMap, &onMapPos2, &other };
	// start position 1 (waypoint 2): every erebor base and <ANY>
	CHECK((AIBaseBuilder::candidates(side, "map wor erebor.map", 1, true) == std::vector<const AIBaseTemplate *>{ &any, &onMap, &onMapPos2 }));
	// start position 0 (waypoint 1): not listed in PlayerPositions
	CHECK((AIBaseBuilder::candidates(side, "map wor erebor.map", 0, true) == std::vector<const AIBaseTemplate *>{ &any, &onMap }));
	// "any" disabled for the slot (AnyTypeTemplateDisabledSlots)
	CHECK((AIBaseBuilder::candidates(side, "map wor erebor.map", 0, false) == std::vector<const AIBaseTemplate *>{ &onMap }));
	// the compare is exact (RW 0x4065AA)
	CHECK((AIBaseBuilder::candidates(side, "MAP WOR EREBOR.map", 0, false).empty()));
}

TEST_CASE("skirmish ai base: a Medium computer on Evendim chooses a layout and builds it through its dozers (Mordor, Men)")
{
	OPENBFME_REQUIRE_START(s);
	struct Expect
	{
		const char *faction;
	};
	for (const char *faction : { "FactionMordor", "FactionMen" })
	{
		INFO(std::string(faction));
		BaseRun run;
		runBaseGame(*s, aiMessage(*s, faction), 900, run, false);
		std::string list;
		for (const std::string &b : run.built)
		{
			list += b + " ";
		}
		MESSAGE(faction << ": base '" << run.chosen << "' phase " << run.phase << " executed " << run.executed << " failed " << run.failed << " waited money " << run.money
						<< " dozer " << run.dozer << " completed " << run.completed << " built: " << list);
		CHECK(!run.chosen.empty());
		CHECK(run.executed >= 2);
		CHECK(run.built.size() <= run.executed); // executions include the economy builder's farms (step 5)
		CHECK(run.completed >= 1);
		CHECK(run.phase == 0); // 900 frames = 180 s < PhaseDuration_Rush (270 s Men, 300 s Wild...)
	}
}

TEST_CASE("skirmish ai base: two runs of the same AI skirmish agree on every frame hash while it builds")
{
	OPENBFME_REQUIRE_START(s);
	BaseRun a, b;
	runBaseGame(*s, aiMessage(*s, "FactionMordor"), 300, a, true);
	runBaseGame(*s, aiMessage(*s, "FactionMordor"), 300, b, true);
	CHECK(a.hashes == b.hashes);
	CHECK(a.executed >= 1);
	CHECK(a.chosen == b.chosen);
}

TEST_CASE("skirmish ai units: each of the 7 factions as the Medium computer on Evendim trains hordes by frame 1500 (5 minutes)")
{
	OPENBFME_REQUIRE_START(s);
	for (const char *faction : { "FactionMen", "FactionElves", "FactionDwarves", "FactionIsengard", "FactionMordor", "FactionWild", "FactionAngmar" })
	{
		INFO(std::string(faction));
		BaseRun run;
		runBaseGame(*s, aiMessage(*s, faction), 1500, run, false);
		std::string a;
		int hordes = 0;
		for (const auto &kv : run.army)
		{
			a += kv.first + "x" + std::to_string(kv.second) + " ";
			hordes += kv.second;
		}
		std::string list;
		for (const std::string &b : run.built)
		{
			list += b + " ";
		}
		MESSAGE(faction << ": base '" << run.chosen << "' built: " << list << "| factories " << run.factories << " unit requests " << run.unitRequests << " queued " << run.unitsQueued
						<< " army: " << a);
		CHECK(run.factories >= 1);
		CHECK(run.unitsQueued >= 2);
		CHECK(hordes >= 1);
		// every horde trained is a member of the faction's ArmyDefinition
		const PlayerTemplate *pt = s->world->playerTemplates().findPlayerTemplate(faction);
		REQUIRE(pt);
		const ArmyDefinition *army = s->world->skirmishAI().findArmy(pt->m_side);
		REQUIRE(army);
		for (const auto &kv : run.army)
		{
			bool member = false;
			for (const ArmyMemberDefinition &m : army->members)
			{
				member = member || m.unit == kv.first;
			}
			CHECK_MESSAGE(member, kv.first);
		}
	}
}

TEST_CASE("skirmish ai units: two runs of the same AI skirmish agree on every frame hash while it builds and trains (Angmar, 1500 frames)")
{
	OPENBFME_REQUIRE_START(s);
	BaseRun a, b;
	runBaseGame(*s, aiMessage(*s, "FactionAngmar"), 1500, a, true);
	runBaseGame(*s, aiMessage(*s, "FactionAngmar"), 1500, b, true);
	CHECK(a.hashes == b.hashes);
	CHECK(a.unitsQueued >= 2);
	CHECK(a.army == b.army);
	// another lobby seed changes the world (the layout / unit draws come from the logic random)
	BaseRun c;
	runBaseGame(*s, aiMessage(*s, "FactionAngmar", 999), 300, c, true);
	CHECK(c.hashes.back() != a.hashes[299]);
}

TEST_CASE("skirmish ai base: a dozer travelling to an unfinished foundation is not given another one (taskTarget, check code 8)")
{
	// Review regression (591ade1): the free list took "not working" for free, so a dozer still walking to its first foundation was handed the next
	// request and the first foundation was abandoned (Men, Mordor, Wild, Angmar at frames 11-12 with 1500 starting cash). A dozer with a task is busy.
	OPENBFME_REQUIRE_START(s);
	for (const char *faction : { "FactionMen", "FactionMordor", "FactionWild", "FactionAngmar" })
	{
		INFO(std::string(faction));
		std::map<ObjectID, ObjectID> task;
		int abandoned = 0, travelled = 0;
		BaseRun run;
		runBaseGame(*s, aiMessage(*s, faction), 120, run, false, [&](GameLogic &logic) {
			for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
			{
				const DozerAIUpdate *d = dynamic_cast<const DozerAIUpdate *>(o->getAIUpdateInterface());
				if (!d || !o->getControllingPlayer() || !logic.skirmishAI().findAI(o->getControllingPlayer()->getPlayerIndex()))
				{
					continue;
				}
				const ObjectID was = task.count(o->getID()) ? task[o->getID()] : INVALID_ID;
				const ObjectID now = d->taskTarget();
				if (now != INVALID_ID && !d->working())
				{
					++travelled;
				}
				if (was != INVALID_ID && now != was)
				{
					const Object *old = logic.findObjectByID(was);
					if (old && !old->isEffectivelyDead() && old->isUnderConstruction())
					{
						++abandoned;
						MESSAGE("abandoned: frame " << logic.getFrame() << " dozer " << o->getID() << " foundation " << was << " -> " << now);
					}
				}
				task[o->getID()] = now;
			}
		});
		CHECK(travelled > 0); // the case of the regression happened: a dozer walked to a site
		CHECK(abandoned == 0);
		CHECK(run.executed >= 1);
	}
}

TEST_CASE("skirmish ai base: the request hash covers the complete request state and the queue order (field mutation, queue reorder)")
{
	AIBuildState st;
	AIBaseInstance base;
	base.slots.resize(1);
	for (int i = 0; i < 2; ++i)
	{
		auto r = std::make_shared<AIBuildRequest>();
		r->serial = st.nextSerial++;
		r->templateName = "SameTemplate"; // two requests of the same template at different sites
		r->offset = Coord3D{ 10.0f * (float)i, 0.0f, 0.0f };
		base.slots[0].entries.push_back(r);
		st.pending.push_back(r);
	}
	st.bases.push_back(base);
	auto unit = std::make_shared<AIBuildRequest>();
	unit->serial = st.nextSerial++;
	unit->unit = true;
	st.units.requests.push_back(unit);
	auto hash = [&] {
		StateHasher h;
		AIBaseBuilder::crc(st, h);
		return h.value();
	};
	auto moved = [&](auto mutate) {
		const std::uint32_t h0 = hash();
		mutate();
		return hash() != h0;
	};
	AIBuildRequest &r = *st.pending[0];
	CHECK(moved([&] { r.angle = 0.25f; }));
	CHECK(moved([&] { r.basePosition.x = 1.0f; }));
	CHECK(moved([&] { r.basePosition.y = 2.0f; }));
	CHECK(moved([&] { r.basePosition.z = 3.0f; }));
	CHECK(moved([&] { r.offset.z = 4.0f; }));
	CHECK(moved([&] { r.name = "N"; }));
	CHECK(moved([&] { r.lastCheck = 8; }));
	CHECK(moved([&] { r.productionID = 3; }));
	CHECK(moved([&] { r.commandPoints = 5; }));
	CHECK(moved([&] { std::swap(st.pending[0], st.pending[1]); }));
	CHECK(moved([&] { st.inProgress.push_back(st.pending[0]); }));
	CHECK(moved([&] { std::swap(st.inProgress[0], st.pending[1]); }));
	CHECK(moved([&] { unit->angle = 1.0f; }));
	CHECK(moved([&] { unit->basePosition.x = 7.0f; }));
	CHECK(moved([&] { st.units.registeredStructures.push_back(5); }));
	CHECK(moved([&] { st.waitedForMoney += 1; }));
	CHECK(moved([&] { st.waitedForDozer += 1; }));
	CHECK(moved([&] { st.bases[0].index = 2; }));
	// step 5: the economy builder and the farm fields of a request
	CHECK(moved([&] { st.economy.built = 1; }));
	CHECK(moved([&] { st.economy.inFlight = 1; }));
	CHECK(moved([&] { st.economy.lastFrame = 40; }));
	CHECK(moved([&] { st.economy.requests.push_back(st.pending[0]); }));
	CHECK(moved([&] { st.economy.disabled = true; }));
	CHECK(moved([&] { r.farm = true; }));
	CHECK(moved([&] { r.inUse = true; }));
	CHECK(moved([&] { r.failures = 1; }));
	CHECK(moved([&] { r.owner = 2; }));
	CHECK(moved([&] { r.siteIndex = 3; }));
}
