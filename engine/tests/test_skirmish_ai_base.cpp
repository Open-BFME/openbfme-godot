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
#include "GameLogic/ObjectTemplateInfo.h"
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

// ---- lane BACKLOG6 ---------------------------------------------------------------------------------------------------------------------------------------
namespace
{
const AISkirmishPlayer &theAI(GameLogic &logic)
{
	const SkirmishAIManager &m = logic.skirmishAI();
	REQUIRE(m.groups().size() == 1);
	return *m.groups()[0]->players.begin()->second;
}

struct FortressRun
{
	std::string name;                 // the AI's StartingBuilding
	std::vector<ObjectID> others;     // every object of that template the AI gets after the kill frame within 350 of the base (RW 0x9BC882's radius), in order
	std::vector<Coord3D> elsewhere;   // lane AIEXPAND-1: the positions of the new ones farther away (SimpleExpansion's MenFortress at an expansion target)
	Coord3D basePosition{ 0.0f, 0.0f, 0.0f };
	Coord3D firstPosition{ 0.0f, 0.0f, 0.0f };
	bool firstPhantom = false, firstBuilderIsAIDozer = false;
	int firstFrame = -1, firstPhase = -1;
	unsigned long long tasksInRush = 0, tasks = 0;
	float taskPriority = -1.0f, armyPriority = -2.0f;
	bool taskReserve = false;
	int freeDozersAtKill = -1;
	int rushFrames = -1;
	bool defeated = false;
};

// an AI skirmish that runs 400 frames past the AI's Rush phase (PhaseDuration_Rush of its ArmyDefinition); 300 frames before the phase ends the AI gets 20000
// and, with `kill`, loses its fortress: every DOZER_FACTORY it has within 350 of the base position (RW 0x9BC882's own test)
FortressRun runFortressGame(starttest::Shared &s, const char *faction, bool kill)
{
	FortressRun out;
	BaseRun run;
	int killFrame = -1;
	std::vector<ObjectID> before; // the StartingBuilding objects of the AI at the kill frame
	std::vector<ObjectID> seenElsewhere;
	const int frames = 4000;      // the callback's own end is the phase's + 400
	bool done = false;
	runBaseGame(s, aiMessage(s, faction), frames, run, false, [&](GameLogic &logic) {
		if (done)
		{
			return;
		}
		const AISkirmishPlayer &ai = theAI(logic);
		Player *p = logic.players().getNthPlayer(ai.playerIndex);
		REQUIRE(p != nullptr);
		REQUIRE(p->getPlayerTemplate() != nullptr);
		REQUIRE(ai.army != nullptr);
		if (killFrame < 0)
		{
			killFrame = (int)(ai.army->phaseDurationRush * 5.0f) - 300;
			REQUIRE(killFrame > 50); // tolerance: a precondition of the scenario (the base exists before the kill), not a result
			REQUIRE(killFrame + 700 < frames); // tolerance: a precondition of the scenario (the game is long enough), not a result
		}
		out.rushFrames = killFrame + 300;
		out.name = p->getPlayerTemplate()->m_startingBuilding;
		const int frame = (int)logic.getFrame();
		if (frame < killFrame)
		{
			return;
		}
		if (frame > killFrame + 700)
		{
			done = true;
			return;
		}
		std::vector<Object *> standing;
		int factoriesAtBase = 0;
		static const int phantom = ObjectTemplateInfoBuilder::objectStatusIndex("PHANTOM_STRUCTURE");
		for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
		{
			// lane FBSIM-1: a dozer's site is RotWK's phantom (effectively dead, RW 0x88D0D8) until the dozer places it: counted from the order
			if (o->getControllingPlayer() != p || (o->isEffectivelyDead() && !o->testStatus((unsigned)phantom)))
			{
				continue;
			}
			if (o->getTemplate()->getName() == out.name)
			{
				standing.push_back(o);
			}
			if (frame == killFrame && o->isKindOfName("DOZER_FACTORY"))
			{
				REQUIRE(ai.build.bases.size() == 1);
				const float dx = o->getPosition()->x - ai.build.bases[0].position.x, dy = o->getPosition()->y - ai.build.bases[0].position.y;
				if (dx * dx + dy * dy <= 350.0f * 350.0f)
				{
					++factoriesAtBase;
					if (kill)
					{
						o->kill(DAMAGE_UNRESISTABLE, 0);
					}
				}
			}
		}
		if (frame == killFrame)
		{
			REQUIRE(factoriesAtBase == 1);
			out.basePosition = ai.build.bases[0].position;
			out.freeDozersAtKill = (int)ai.build.freeDozers.size();
			p->getMoney()->deposit(20000u, false);
			for (Object *o : standing)
			{
				before.push_back(o->getID());
			}
			return;
		}
		out.defeated = p->isDefeated();
		if (ai.build.phase == 0)
		{
			out.tasksInRush = ai.build.fortressRebuilds;
		}
		out.tasks = ai.build.fortressRebuilds;
		if (!ai.build.bases.empty() && ai.build.bases[0].fortress && out.taskPriority < 0.0f)
		{
			out.taskPriority = ai.build.bases[0].fortress->priority;
			out.taskReserve = ai.build.bases[0].fortress->reserveMoney;
			out.armyPriority = ai.army->fortressRebuildPriority;
		}
		for (Object *o : standing)
		{
			if (std::find(before.begin(), before.end(), o->getID()) != before.end() || std::find(out.others.begin(), out.others.end(), o->getID()) != out.others.end() ||
				std::find(seenElsewhere.begin(), seenElsewhere.end(), o->getID()) != seenElsewhere.end())
			{
				continue;
			}
			const float ex = o->getPosition()->x - out.basePosition.x, ey = o->getPosition()->y - out.basePosition.y;
			if (ex * ex + ey * ey > 350.0f * 350.0f)
			{
				seenElsewhere.push_back(o->getID());
				out.elsewhere.push_back(*o->getPosition());
				continue;
			}
			if (out.others.empty())
			{
				out.firstFrame = frame;
				out.firstPhase = ai.build.phase;
				out.firstPosition = *o->getPosition();
				out.firstPhantom = o->testStatus((unsigned)phantom);
				const Object *builder = logic.findObjectByID(o->getBuilderID());
				out.firstBuilderIsAIDozer = builder && builder->getControllingPlayer() == p && builder->isKindOfName("DOZER");
			}
			out.others.push_back(o->getID());
		}
	});
	MESSAGE(std::string(faction) << std::string(kill ? " (fortress killed)" : " (control)") << ": " << out.name << ", Rush ends at frame " << out.rushFrames << ", free dozers at the kill frame: " << out.freeDozersAtKill << ", tasks " << out.tasks
		<< " (in Rush " << out.tasksInRush << "), new fortress objects " << out.others.size() << ", first at frame " << out.firstFrame << " phase " << out.firstPhase
		<< " executed " << run.executed << " failed " << run.failed << "; elsewhere " << out.elsewhere.size());
	for (const Coord3D &e : out.elsewhere)
	{
		MESSAGE("  a new " << out.name << " away from the base at (" << e.x << ", " << e.y << ")");
	}
	return out;
}
} // namespace

TEST_CASE("skirmish ai base: an AI past the Rush phase rebuilds its lost fortress at the base through a dozer; not in the Rush phase and not while one stands (RW 0x9BC882)")
{
	OPENBFME_REQUIRE_START(s);
	for (const char *faction : { "FactionMen", "FactionMordor" })
	{
		INFO(std::string(faction));
		// the control: the same game and money with the fortress standing: no task, no second fortress (RW 0x9BC965: a DOZER_FACTORY within 350 of the base)
		const FortressRun control = runFortressGame(*s, faction, false);
		CHECK_FALSE(control.defeated);
		CHECK(control.tasks == 0);
		CHECK(control.others.empty());
		// lane AIEXPAND-1: with 20000 money past the Rush phase the Men computer's SimpleExpansion builds a MenFortress at its EXPANSION target, away from the base
		// (RW 0x9B51E9's "MenFortress" whatever the side: Mordor's request never passes canMakeUnit). Engine pins: Evendim, seed 1234
		// merge queue q29 (AIEXPAND-1 after nine simulation items, measured on the merged tree, a CI host): no expansion fortress of the Men stands within this
		// run (it ends 400 frames after the Rush phase) any more; the expansion itself is test_aiexpand1_expansion.cpp's
		CHECK(control.elsewhere.size() == 0u);
		// the fortress falls 300 frames before the Rush phase ends (killing its DOZER_FACTORY, the citadel, takes the whole castle)
		const FortressRun lost = runFortressGame(*s, faction, true);
		CHECK_FALSE(lost.defeated);            // the AI still has structures: it plays on
		CHECK(lost.tasksInRush == 0);          // RW 0x9BC8E3: the phase must be >= 1 (300 frames with free dozers, money and no fortress pass first)
		CHECK(lost.tasks == 1);                // one task: it is not made again while its fortress is being built
		REQUIRE(lost.others.size() == 1);      // exactly one new StartingBuilding object
		CHECK(lost.firstPhase == 1);
		CHECK(lost.firstFrame == lost.rushFrames); // the frame the phase turns: the task is made, checked and executed by the free dozer in one update
		CHECK(lost.firstPosition.x == lost.basePosition.x); // a zero offset from the base position
		CHECK(lost.firstPosition.y == lost.basePosition.y);
		CHECK(lost.firstPhantom); // lane FBSIM-1: the dozer's site is RotWK's phantom until it arrives (RW 0x88D272)
		CHECK(lost.firstBuilderIsAIDozer);
		CHECK(lost.taskPriority == lost.armyPriority); // ArmyDefinition FortressRebuildPriority
		CHECK(lost.taskReserve);
	}
}

TEST_CASE("skirmish ai base: a request left in the queue holds no producer after the processor (AIBuilder::update RW 0x8F0EB4)")
{
	// BFME2 decomp AIBuilderUpdate.cpp (byte-matched): the request that stops the loop without a successful execution, and every request passed over, gives up
	// the dozer or factory assigned this frame. Before lane BACKLOG6 a request waiting for money or a dozer kept it.
	OPENBFME_REQUIRE_START(s);
	for (const char *faction : { "FactionMen", "FactionIsengard" })
	{
		INFO(std::string(faction));
		int held = 0;
		BaseRun run;
		runBaseGame(*s, aiMessage(*s, faction), 400, run, false, [&](GameLogic &logic) {
			for (const auto &r : theAI(logic).build.pending)
			{
				if (r->producer != INVALID_ID)
				{
					++held;
					MESSAGE("frame " << logic.getFrame() << ": pending " << r->templateName << " holds producer " << r->producer << " (check " << r->lastCheck << ")");
				}
			}
		});
		CHECK(held == 0);
		CHECK(run.money > 0); // tolerance: the guard that the case happened (a request waited for money); how often is not this test's subject
	}
}
