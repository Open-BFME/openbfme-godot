// OpenBFME unit tests: lane CAMP-1, the Angmar campaign missions played end to end by their own scripts (test helpers play the player's part where a
// player would fight or walk), in campaign order (data\ini\linearcampaignexpansion1.ini ANGMAR_CAMPAIGN; MAP ANG Angmar and MAP ANG Rhudaur are
// test_script2_angmar.cpp / test_script3_mission.cpp), and the bonus mission (ANGMAR_BONUS_CAMPAIGN). Retail runs: they SKIP when ROTWK_INSTALL /
// BFME2_INSTALL are unset. GPL-3.0.

#include "doctest.h"
#include "CampaignTestUtil.h"

#include "GameLogic/Module/AttachUpdate.h"
#include "GameLogic/SimMath.h"

#include <cstdlib>
#include <map>

using campaigntest::Mission;

namespace
{
// prints the mission's unported conditions / actions and the engine's notes (diagnostics; CAMP1_SURVEY=<frames> runs the survey case)
void survey(Mission &m)
{
	const ScriptEngine::Stats &st = m.engine().stats();
	for (const auto &kv : st.unportedConditions)
	{
		MESSAGE("  unported condition " << kv.first << " x" << kv.second);
	}
	for (const auto &kv : st.unportedActions)
	{
		MESSAGE("  unported action " << kv.first << " x" << kv.second);
	}
	for (const auto &kv : st.notes)
	{
		MESSAGE("  note " << kv.first << " x" << kv.second);
	}
	for (const auto &kv : st.teamScripts) // lane CAMP-1H
	{
		MESSAGE("  team script " << kv.first << " x" << kv.second);
	}
	MESSAGE("  generic scripts fired x" << st.genericScriptsFired);
}
} // namespace

// lane CAMP-1H: Team::updateState (RW 0x7A208C) and Team::updateGenericScripts (RW 0x7A267D) run the maps' team scripts: the teams of the map objects
// are active at load, and no generic hook of the first 600 frames names a script the map does not have
TEST_CASE("camp1h team scripts: the Angmar missions' teams load with their scripts; every generic hook names a script of the map")
{
	for (const char *map : { "map ang amon sul", "map ang dark eye", "map ang barrow downs", "map ang carn dum", "map ang barrow wights", "map ang fornost",
			 "map ang bonus", "map ang rhudaur" })
	{
		OPENBFME_REQUIRE_START(s);
		Mission m;
		m.load(*s, map);
		size_t withOnCreate = 0, withGeneric = 0, active = 0;
		for (const auto &proto : m.logic().players().teams().prototypes())
		{
			const TeamTemplateScripts &ts = proto->templateScripts();
			withOnCreate += ts.onCreate.empty() ? 0 : 1;
			for (const std::string &g : ts.generic)
			{
				withGeneric += g.empty() ? 0 : 1;
			}
			for (const Team *t : proto->teams())
			{
				active += t->isActive() ? 1 : 0;
			}
		}
		m.step(600);
		const ScriptEngine::Stats &st = m.engine().stats();
		const auto onCreate = st.teamScripts.find("OnCreate");
		INFO(std::string(map));
		MESSAGE(std::string(map) << ": " << withOnCreate << " prototypes with OnCreate, " << withGeneric << " generic hooks, " << active << " teams active at load; "
								 << "OnCreate run x" << (onCreate == st.teamScripts.end() ? 0ull : onCreate->second) << ", generic fired x"
								 << st.genericScriptsFired);
		survey(m);
		for (const auto &kv : st.notes)
		{
			CHECK(kv.first.rfind("generic script not found", 0) != 0);
		}
		// (the OnCreate teams of most missions are reinforcements made later: the playthroughs pin what runs, expectTeamScripts)
		(void)onCreate;
	}
}

TEST_CASE("camp1 survey: every Angmar mission's unported script content in its first CAMP1_SURVEY frames (diagnostic; skipped without the variable)")
{
	const char *frames = std::getenv("CAMP1_SURVEY");
	if (!frames)
	{
		return;
	}
	for (const char *map : { "map ang amon sul", "map ang dark eye", "map ang barrow downs", "map ang carn dum", "map ang barrow wights", "map ang fornost",
			 "map ang bonus" })
	{
		OPENBFME_REQUIRE_START(s);
		Mission m;
		m.load(*s, map);
		m.step(std::atoi(frames));
		MESSAGE(std::string(map) << ": frame " << m.frame() << ", " << m.engine().stats().scriptsFired << " scripts fired");
		survey(m);
	}
}

namespace
{
// lane CAMP-1H: the team scripts a playthrough ran (ScriptEngine::Stats::teamScripts / genericScriptsFired)
void expectTeamScripts(Mission &m, unsigned long long onCreate, unsigned long long onDestroyed, unsigned long long generic)
{
	const ScriptEngine::Stats &st = m.engine().stats();
	auto count = [&](const char *k) {
		const auto it = st.teamScripts.find(k);
		return it == st.teamScripts.end() ? 0ull : it->second;
	};
	CHECK(count("OnCreate") == onCreate);
	CHECK(count("OnDestroyed") == onDestroyed);
	CHECK(st.genericScriptsFired == generic);
}

// waits for the mission's end request; checks it is a victory, prints it and the engine report
void expectVictory(Mission &m, int maxFrames)
{
	REQUIRE(m.until([&] { return !m.engine().endRequests().empty(); }, maxFrames));
	const ScriptEngine::EndRequest &end = m.engine().endRequests().front();
	CHECK(end.victory);
	CHECK(m.requested("VICTORY_SCREEN"));
	MESSAGE("victory (" << end.action << ") at frame " << end.frame);
	survey(m);
}
} // namespace

TEST_CASE("camp1 amon sul: MAP ANG Amon Sul plays from its intro to the victory by its own scripts")
{
	OPENBFME_REQUIRE_START(s);
	Mission m;
	m.load(*s, "map ang amon sul");
	REQUIRE(m.game->mapScriptsRunning());
	const std::string me = m.logic().players().getLocalPlayer()->getPlayerName();
	// 1. the intro cinematic (the four spell scenes); the builders walk to the player
	REQUIRE(m.until([&] { return m.requested("CAMERA_LETTERBOX_END"); }, 2000));
	MESSAGE("intro over at frame " << m.frame());
	REQUIRE(m.until([&] { return m.unit("Builder 1") && m.unit("Builder 1")->getTeam() && m.unit("Builder 1")->getTeam()->getName() == "teamPlyrAngmar"; }, 1500));
	MESSAGE("builders granted at frame " << m.frame());
	REQUIRE(m.until([&] { return m.requested("SHOW_MISSION_OBJECTIVE"); }, 200));
	// 2. the tower of Amon Sul falls -> "Fortress Destroyed" -> 6 s -> "Flag - Mission Won" -> the victory sequence
	m.killNamed("Tower of Amon Sul");
	REQUIRE(m.until([&] { return m.flag(me + "/Fortress Destroyed"); }, 50));
	REQUIRE(m.until([&] { return m.flag("/Flag - Mission Won"); }, 100));
	MESSAGE("mission won at frame " << m.frame());
	expectVictory(m, 200);
	expectTeamScripts(m, 0, 0, 0); // lane CAMP-1H: Team::updateState / updateGenericScripts (RW 0x7A208C / 0x7A267D) over the playthrough
}

namespace
{
// a mission's progress log (diagnostic): the flags that change, the counters that change, the objective / notification / caption / end requests, frame by frame
struct ProgressLog
{
	std::map<std::pair<std::string, std::string>, bool> flags;
	std::map<std::pair<std::string, std::string>, std::int32_t> counters;
	size_t requests = 0;
	void poll(Mission &m)
	{
		for (const auto &kv : m.engine().allFlags())
		{
			auto it = flags.find(kv.first);
			if (it == flags.end() ? kv.second : it->second != kv.second)
			{
				MESSAGE("f" << m.frame() << " flag " << kv.first.first << "/" << kv.first.second << " = " << kv.second);
			}
			flags[kv.first] = kv.second;
		}
		for (const auto &kv : m.engine().allCounters())
		{
			auto it = counters.find(kv.first);
			if ((it == counters.end() || it->second != kv.second.value) && !kv.second.countdown && kv.first.second.find("Random") == std::string::npos)
			{
				MESSAGE("f" << m.frame() << " counter " << kv.first.first << "/" << kv.first.second << " = " << kv.second.value);
			}
			counters[kv.first] = kv.second.value;
		}
		for (; requests < m.requests.size(); ++requests)
		{
			const ScriptClientRequest &r = m.requests[requests];
			if (r.action == "SHOW_MISSION_OBJECTIVE" || r.action == "MARK_MISSION_OBJECTIVE_COMPLETED" || r.action == "DISPLAY_NOTIFICATION_BOX" ||
				r.action == "VICTORY_SCREEN" || r.action == "DEFEAT" || r.action == "CAMERA_LETTERBOX_END" || r.action == "CAMERA_LETTERBOX_BEGIN")
			{
				std::string p;
				for (const ScriptParameter &q : r.params)
				{
					p += (q.stringValue.empty() ? std::to_string(q.intValue) : q.stringValue) + " ";
				}
				MESSAGE("f" << m.frame() << " " << r.action << " " << p);
			}
		}
	}
};
} // namespace

TEST_CASE("camp1 probe: CAMP1_PROBE=<map> CAMP1_FRAMES=<n> logs the mission's progress with no player (diagnostic; skipped without the variable)")
{
	const char *map = std::getenv("CAMP1_PROBE");
	if (!map)
	{
		return;
	}
	OPENBFME_REQUIRE_START(s);
	Mission m;
	m.load(*s, map);
	if (const char *name = std::getenv("CAMP1_SCRIPT"))
	{
		// the script's conditions with their two v5 flags (Condition + 0x4C / + 0x4D)
		ScriptEngine::RScript *rs = m.engine().findScript(name);
		REQUIRE(rs != nullptr);
		for (const OrCondition &oc : rs->def->orConditions)
		{
			for (const ScriptCondition &c : oc.conditions)
			{
				MESSAGE(c.internalName << " version " << c.version << " flagA " << c.flagA << " flagB " << c.flagB);
			}
		}
	}
	const int frames = std::getenv("CAMP1_FRAMES") ? std::atoi(std::getenv("CAMP1_FRAMES")) : 1500;
	ProgressLog log;
	for (int i = 0; i < frames; ++i)
	{
		m.step();
		log.poll(m);
	}
	survey(m);
}

namespace
{
// a hit of `amount` on `victim` from `source` (a player's unit: the last damage record names it, as a fight would)
void hit(Object &victim, const Object &source, float amount)
{
	DamageInfo info;
	info.m_input.m_damageType = DAMAGE_UNRESISTABLE;
	info.m_input.m_sourceID = source.getID();
	const Player *p = source.getControllingPlayer();
	info.m_input.m_sourcePlayerMask = p && p->getPlayerIndex() >= 0 ? (1u << p->getPlayerIndex()) : 0u;
	info.m_input.m_amount = amount;
	if (victim.getBodyModule())
	{
		victim.getBodyModule()->attemptDamage(info);
	}
}

bool envFlag(const char *name)
{
	return std::getenv(name) != nullptr;
}
} // namespace

TEST_CASE("camp1 dark eye: MAP ANG Dark Eye plays from its intro to the victory by its own scripts")
{
	OPENBFME_REQUIRE_START(s);
	Mission m;
	m.load(*s, "map ang dark eye");
	REQUIRE(m.game->mapScriptsRunning());
	ProgressLog log;
	const bool verbose = envFlag("CAMP1_VERBOSE");
	auto step = [&](int n) {
		for (int i = 0; i < n; ++i)
		{
			m.step();
			if (verbose)
			{
				log.poll(m);
			}
		}
	};
	auto until = [&](const std::function<bool()> &done, int maxFrames) {
		for (int i = 0; i < maxFrames && !done(); ++i)
		{
			step(1);
		}
		return done();
	};
	// 1. the intro: Prince Arvaleg's march, Angmar's arrival; the first objective (stop the prince)
	REQUIRE(until([&] { return m.requested("CAMERA_LETTERBOX_END"); }, 600));
	MESSAGE("intro over at frame " << m.frame());
	// 2. the player's intro team fights the prince down to a quarter of his health: the palantir cinematic, "Arveleg Dead"
	Object *prince = m.unit("Prince Arvaleg");
	Object *wolves = m.unit("Dire Wolves 01");
	REQUIRE(prince != nullptr);
	REQUIRE(wolves != nullptr);
	m.place(*wolves, *prince->getPosition());
	const float max = prince->getBodyModule()->getMaxHealth();
	hit(*prince, *wolves, SimMath::mulf32(max, 0.8f));
	REQUIRE(until([&] { return m.flag("/FLAG - Arveleg Dead"); }, 600));
	MESSAGE("the palantir breaks at frame " << m.frame());
	// the shard team arrives; its porter (SpecPorter01, PlayerPorter) builds the player's fortress, "Fort 01", which joins the player (UNIT_SET_TEAM)
	REQUIRE(until([&] { return m.unit("Fort 01") && m.unit("Fort 01")->getControllingPlayer() == m.logic().players().getLocalPlayer(); }, 4000));
	MESSAGE("the fortress joins the player at frame " << m.frame());

	// 3. the shards (PalantirShard01 .. 07, AttachUpdate): a hero of the player walks onto each shard (the AttachUpdate scan picks it up: HOLDING_THE_SHARD,
	// which starts the hermit's shards) and carries it to "AREA - Angmar Ring Return"; a shard an Arnor unit carries is taken by killing the carrier (the shard
	// dies with it and drops a new one, which "Reference Shards" names again, RW 0x7C3E5E)
	const Coord3D ring = m.centreOf("AREA - Angmar Ring Return");
	Player *local = m.logic().players().getLocalPlayer();
	auto carrierOf = [&](Object &shard) -> Object * {
		for (const std::unique_ptr<BehaviorModule> &mod : shard.modules())
		{
			if (AttachUpdate *a = dynamic_cast<AttachUpdate *>(mod.get()))
			{
				return a->parentID() ? m.logic().findObjectByID(a->parentID()) : nullptr;
			}
		}
		return nullptr;
	};
	auto porter = [&]() -> Object * {
		for (Object *o = m.logic().getFirstObject(); o; o = o->getNextObject())
		{
			if (o->getControllingPlayer() == local && o->isKindOfName("HERO") && !o->isEffectivelyDead() && !o->getContainedBy())
			{
				return o;
			}
		}
		return nullptr;
	};
	const ScriptEngine::Counter *shards = nullptr;
	int carried = 0;
	const bool all = until(
		[&] {
			Object *hero = porter();
			for (int i = 1; i <= 7 && hero; ++i)
			{
				Object *shard = m.unit("Shard 0" + std::to_string(i));
				if (!shard || shard->isEffectivelyDead())
				{
					continue;
				}
				Object *carrier = carrierOf(*shard);
				if (carrier && carrier->getControllingPlayer() != local)
				{
					MESSAGE("shard " << i << " carried by " << carrier->getTemplate()->getName() << ": killed at frame " << m.frame());
					m.kill(*carrier);
				}
				else if (!carrier)
				{
					m.place(*hero, *shard->getPosition()); // the next AttachUpdate scan picks it up
				}
				else
				{
					m.place(*carrier, ring);
					++carried;
				}
				break; // one shard at a time
			}
			shards = m.engine().findCounter("/COUNTER - Shards");
			return shards && shards->value >= 7;
		},
		12000);
	MESSAGE("shards returned: " << (shards ? shards->value : -1) << " at frame " << m.frame() << " (" << carried << " carries)");
	REQUIRE(all);
	expectVictory(m, 600);
	// lane CAMP-1H: Team::updateState / updateGenericScripts (RW 0x7A208C / 0x7A267D) over the playthrough. Lane MOVE-3: since the horde goal reservation
	// (RW 0x86EF13) and the hordes' own footprint (RW 0x6ED071) the Builder & Friends team's generic hook "Notify Player of Builder" fired once in this run;
	// MOVE-3 r3 (a horde's straight-line test with radius 1, RW 0x6EE12D, and the other review fixes) moves the team as before MOVE-3 and it no longer fires
	expectTeamScripts(m, 11, 0, 0);
}

namespace
{
// the mission driver of the CAMP-1 tests: steps with the optional progress log (CAMP1_VERBOSE)
struct Drive
{
	Mission &m;
	ProgressLog log;
	bool verbose = envFlag("CAMP1_VERBOSE");
	explicit Drive(Mission &mission) : m(mission) {}
	void step(int n = 1)
	{
		for (int i = 0; i < n; ++i)
		{
			m.step();
			if (verbose)
			{
				log.poll(m);
			}
		}
	}
	bool until(const std::function<bool()> &done, int maxFrames)
	{
		for (int i = 0; i < maxFrames && !done(); ++i)
		{
			step();
		}
		return done();
	}
	Player *local() { return m.logic().players().getLocalPlayer(); }
	// the player's army beats the units (not the structures) of the players named `prefix`* (empty: the local player's enemies) within `radius` of `centre`
	// (radius <= 0: anywhere); the number killed
	int defeatUnits(const std::string &prefix, const Coord3D &centre = Coord3D{ 0.0f, 0.0f, 0.0f }, float radius = 0.0f)
	{
		std::vector<Object *> victims;
		for (Object *o = m.logic().getFirstObject(); o; o = o->getNextObject())
		{
			const Player *p = o->getControllingPlayer();
			const bool match = prefix.empty() ? (p && p != local() && local()->getRelationship(p) == ENEMIES) : (p && p->getPlayerName().compare(0, prefix.size(), prefix) == 0);
			if (!match || o->isEffectivelyDead() || o->isKindOfName("STRUCTURE") || o->getContainedBy())
			{
				continue;
			}
			if (!(o->isKindOfName("INFANTRY") || o->isKindOfName("CAVALRY") || o->isKindOfName("HORDE") || o->isKindOfName("MONSTER") || o->isKindOfName("HERO") ||
				o->isKindOfName("SIEGE_ENGINE")))
			{
				continue;
			}
			const float dx = o->getPosition()->x - centre.x, dy = o->getPosition()->y - centre.y;
			if (radius > 0.0f && dx * dx + dy * dy > radius * radius)
			{
				continue;
			}
			victims.push_back(o);
		}
		for (Object *o : victims)
		{
			m.kill(*o);
		}
		return (int)victims.size();
	}
};
} // namespace

TEST_CASE("camp1 barrow downs: MAP ANG Barrow Downs plays from its intro to the victory by its own scripts")
{
	OPENBFME_REQUIRE_START(s);
	Mission m;
	m.load(*s, "map ang barrow downs");
	REQUIRE(m.game->mapScriptsRunning());
	Drive d(m);
	REQUIRE(d.until([&] { return m.requested("CAMERA_LETTERBOX_END"); }, 600));
	MESSAGE("intro over at frame " << m.frame());
	// 1. Hwaldar climbs the Royal Barrow (NAMED_ENTERED_AREA "AT - Royal Barrow Summit")
	Object *hwaldar = m.unit("Hwaldar");
	REQUIRE(hwaldar != nullptr);
	hwaldar->getBodyModule()->setIndestructible(true);
	m.place(*hwaldar, m.centreOf("AT - Royal Barrow Summit"));
	REQUIRE(d.until([&] { return m.flag("PlyrAngmar/Flag - Objective 1 Complete") || m.flag("/Flag - Objective 1 Complete"); }, 100));
	MESSAGE("the summit at frame " << m.frame());
	// 2 .. 4: the player holds the barrow (Hwaldar stays in "AT - King of the Royal Barrow") and beats every Arnor attack that reaches it: the first waves
	// (then NOT TEAM_HAS_UNITS(teamPlyrArnor1stArmy), an inverted condition), the vanguard, the prince's army -> "Prince Defeated" -> 8 s -> "Mission Won"
	const Coord3D hold = m.centreOf("AT - King of the Royal Barrow");
	int killed = 0;
	const bool won = d.until(
		[&] {
			if (m.frame() % 25 == 0)
			{
				m.place(*hwaldar, hold);
				killed += d.defeatUnits("PlyrArnor", hold, 900.0f); // what reaches the barrow
			}
			return !m.engine().endRequests().empty();
		},
		15000);
	MESSAGE("the Arnor units beaten: " << killed << "; end at frame " << m.frame());
	REQUIRE(won);
	expectVictory(m, 10);
	expectTeamScripts(m, 13, 0, 0); // lane CAMP-1H: Team::updateState / updateGenericScripts (RW 0x7A208C / 0x7A267D) over the playthrough
}

TEST_CASE("camp1 carn dum: MAP ANG Carn Dum plays from its intro to the victory by its own scripts")
{
	OPENBFME_REQUIRE_START(s);
	Mission m;
	m.load(*s, "map ang carn dum");
	REQUIRE(m.game->mapScriptsRunning());
	Drive d(m);
	REQUIRE(d.until([&] { return m.requested("CAMERA_LETTERBOX_END"); }, 800));
	MESSAGE("intro over at frame " << m.frame());
	// the player captures the signal tower (its owner becomes the player, as a capture does)
	Object *tower = m.unit("Signal Tower 1");
	REQUIRE(tower != nullptr);
	tower->setTeam(d.local()->getDefaultTeam());
	Object *citadel = m.unit("The Citadel");
	REQUIRE(citadel != nullptr);
	const Coord3D keep = *citadel->getPosition();
	// the elves' waves (every phase) break on the defence: what comes near the citadel is beaten; Rogash arrives once the tower is the player's, a wall piece
	// fell and the ten-minute timer ran; Glorfindel's army ("Glori's Peeps") comes and is destroyed -> "Mission Won"
	int killed = 0;
	bool glori = false;
	const bool won = d.until(
		[&] {
			if (m.frame() % 25 == 0)
			{
				killed += d.defeatUnits(std::string(), keep, 1400.0f);
			}
			// Glorfindel's army on the map: the player's army (Rogash with it) meets and destroys it
			Team *peeps = ScriptConditions::team(m.engine(), "PlyrAttack/Glori's Peeps");
			if (peeps && peeps->getFirstMember() && m.frame() % 25 == 0)
			{
				if (!glori)
				{
					glori = true;
					MESSAGE("Glorfindel's army on the map at frame " << m.frame() << " (" << peeps->getMemberCount() << " objects)");
				}
				for (Object *o : ScriptConditions::members(*peeps))
				{
					m.kill(*o);
				}
			}
			return !m.engine().endRequests().empty();
		},
		12000);
	MESSAGE("enemy units beaten near the citadel: " << killed << "; end at frame " << m.frame());
	if (!won)
	{
		survey(m);
		for (const auto &proto : m.logic().players().teams().prototypes())
		{
			if (proto->getName().find("Glori") != std::string::npos)
			{
				MESSAGE("prototype " << proto->getName() << " of " << (proto->getControllingPlayer() ? proto->getControllingPlayer()->getPlayerName() : "-") << ": "
									<< proto->teams().size() << " teams");
				for (const Team *t : proto->teams())
				{
					MESSAGE("  team " << t->getID() << ": " << t->getMemberCount() << " members, had " << t->hadMembers());
				}
			}
		}
	}
	REQUIRE(won);
	expectVictory(m, 10);
	expectTeamScripts(m, 14, 4, 0); // lane CAMP-1H: Team::updateState / updateGenericScripts (RW 0x7A208C / 0x7A267D) over the playthrough
}

TEST_CASE("camp1 barrow wights: MAP ANG Barrow Wights plays from its intro to the victory by its own scripts")
{
	OPENBFME_REQUIRE_START(s);
	Mission m;
	m.load(*s, "map ang barrow wights");
	REQUIRE(m.game->mapScriptsRunning());
	Drive d(m);
	REQUIRE(d.until([&] { return m.requested("CAMERA_LETTERBOX_END"); }, 1200));
	MESSAGE("intro over at frame " << m.frame());
	// 1. the player's sorcerer hordes (AngmarNecromancerHorde) hold the seven barrows: each barrow with one adds a soul every 4 s ("Counter - Barrow n",
	// PLAYER_HAS_COMPARISON_UNIT_TYPE_IN_TRIGGER_AREA) until 998 souls
	static const char *const kBarrows[] = { "Barrow 1 area (South East)", "Barrow 2 area (East)", "Barrow 3 area (North East)", "Barrow 4 area (North West)",
		"Barrow 5 area (center)", "Barrow 6 area (South)", "Barrow 7 peak (Royal Barrow)" };
	std::vector<ObjectID> sorcerers;
	for (Object *o = m.logic().getFirstObject(); o; o = o->getNextObject())
	{
		if (o->getControllingPlayer() == d.local() && o->getTemplate()->getName() == "AngmarNecromancerHorde")
		{
			sorcerers.push_back(o->getID());
		}
	}
	auto live = [&](size_t i) -> Object * {
		Object *o = m.logic().findObjectByID(sorcerers[i % sorcerers.size()]);
		return o && !o->isEffectivelyDead() ? o : nullptr;
	};
	MESSAGE(sorcerers.size() << " sorcerer hordes");
	REQUIRE(!sorcerers.empty());
	const ScriptEngine::Counter *souls = nullptr;
	const bool corrupted = d.until(
		[&] {
			if (m.frame() % 20 == 0)
			{
				for (size_t i = 0; i < 7; ++i)
				{
					Object *o = live(i);
					if (o)
					{
						m.place(*o, m.centreOf(kBarrows[i]));
						d.defeatUnits(std::string(), *o->getPosition(), 600.0f); // the barrow's defenders and the hunters
					}
				}
			}
			souls = m.engine().findCounter("/Counter - Souls Corrupted");
			return souls && souls->value >= 998;
		},
		12000);
	MESSAGE("souls " << (souls ? souls->value : -1) << " at frame " << m.frame());
	REQUIRE(corrupted);
	// 2. Captain Carthaen comes; the player fights him down to a third of his health -> the finale -> "Timer - Game Won"
	REQUIRE(d.until([&] { return m.unit("Captain Carthaen") != nullptr && m.engine().scriptActive("PlyrAngmar/Objective 02 Complete 2"); }, 3000));
	Object *carthaen = m.unit("Captain Carthaen");
	MESSAGE("Captain Carthaen at frame " << m.frame());
	Object *attacker = nullptr;
	for (size_t i = 0; i < sorcerers.size() && !attacker; ++i)
	{
		attacker = live(i);
	}
	REQUIRE(attacker != nullptr);
	hit(*carthaen, *attacker, SimMath::mulf32(carthaen->getBodyModule()->getMaxHealth(), 0.7f));
	const bool won = d.until([&] { return !m.engine().endRequests().empty(); }, 3000);
	if (!won)
	{
		survey(m);
	}
	REQUIRE(won);
	expectVictory(m, 10);
	expectTeamScripts(m, 25, 0, 0); // lane CAMP-1H: Team::updateState / updateGenericScripts (RW 0x7A208C / 0x7A267D) over the playthrough
}

TEST_CASE("camp1 fornost: MAP ANG Fornost plays from its intro to the victory by its own scripts")
{
	OPENBFME_REQUIRE_START(s);
	Mission m;
	m.load(*s, "map ang fornost");
	REQUIRE(m.game->mapScriptsRunning());
	Drive d(m);
	REQUIRE(d.until([&] { return m.requested("CAMERA_LETTERBOX_END"); }, 1200));
	MESSAGE("intro over at frame " << m.frame());
	// the army takes the city: the citadel ("THE BUILDING") falls -> "Victory Condition" -> "Mission Won"
	d.step(150);
	m.killNamed("THE BUILDING");
	REQUIRE(d.until([&] { return m.flag("PlyrAngmar/Mission Won") || m.flag("/Mission Won"); }, 100));
	MESSAGE("the citadel falls at frame " << m.frame());
	expectVictory(m, 300);
	expectTeamScripts(m, 0, 0, 0); // lane CAMP-1H: Team::updateState / updateGenericScripts (RW 0x7A208C / 0x7A267D) over the playthrough
}

TEST_CASE("camp1 bonus: MAP ANG Bonus (ANGMAR_BONUS_CAMPAIGN) plays from its intro to the victory by its own scripts")
{
	OPENBFME_REQUIRE_START(s);
	Mission m;
	m.load(*s, "map ang bonus");
	REQUIRE(m.game->mapScriptsRunning());
	Drive d(m);
	REQUIRE(d.until([&] { return m.requested("CAMERA_LETTERBOX_END"); }, 1200));
	MESSAGE("intro over at frame " << m.frame());
	// the good army finds the Witch King (NAMED_DISCOVERED: his dialogue lifts his indestructibility, objective 6) and defeats him -> "WK Defeated" ->
	// "Mission Win Timer Go" -> 5 s -> "Flag - Mission Won"
	Object *wk = m.unit("Witch King");
	REQUIRE(wk != nullptr);
	Object *scout = nullptr;
	for (Object *o = m.logic().getFirstObject(); o && !scout; o = o->getNextObject())
	{
		scout = o->getControllingPlayer() == d.local() && o->isKindOfName("HERO") ? o : nullptr;
	}
	REQUIRE(scout != nullptr);
	scout->getBodyModule()->setIndestructible(true);
	Coord3D near = *wk->getPosition();
	near.x += 60.0f;
	m.place(*scout, near);
	REQUIRE(d.until([&] { return m.requested("SHOW_MILITARY_CAPTION", "SCRIPT:ANGBonusMissionScene4Text_01"); }, 300)); // "Start WK Dialogue"
	REQUIRE(d.until([&] { return !wk->getBodyModule()->isIndestructible(); }, 600));
	MESSAGE("the Witch King can be fought at frame " << m.frame());
	m.kill(*wk);
	REQUIRE(d.until([&] { return m.flag("PlyrAngmarWitchKing/Flag - WK Defeated"); }, 100));
	expectVictory(m, 300);
	expectTeamScripts(m, 0, 0, 80); // lane CAMP-1H: Team::updateState / updateGenericScripts (RW 0x7A208C / 0x7A267D) over the playthrough
}

TEST_CASE("camp1 determinism: MAP ANG Dark Eye twice with the same player's part agrees on every frame's state hash (AttachUpdate, the attacked-by flags, "
		  "the team flags, the NOT conditions)")
{
	OPENBFME_REQUIRE_START(s);
	auto run = [&](std::vector<std::uint32_t> &hashes) {
		Mission m;
		m.load(*s, "map ang dark eye");
		Drive d(m);
		for (int f = 0; f < 700; ++f)
		{
			if (f == 215)
			{
				// the prince is fought down (the same hit in both runs)
				Object *prince = m.unit("Prince Arvaleg");
				Object *wolves = m.unit("Dire Wolves 01");
				REQUIRE(prince != nullptr);
				REQUIRE(wolves != nullptr);
				m.place(*wolves, *prince->getPosition());
				hit(*prince, *wolves, SimMath::mulf32(prince->getBodyModule()->getMaxHealth(), 0.8f));
			}
			if (f > 440 && f % 3 == 0)
			{
				// a hero carries the shards (AttachUpdate) as the mission test does
				Object *hero = nullptr;
				for (Object *o = m.logic().getFirstObject(); o && !hero; o = o->getNextObject())
				{
					hero = o->getControllingPlayer() == d.local() && o->isKindOfName("HERO") && !o->isEffectivelyDead() && !o->getContainedBy() ? o : nullptr;
				}
				for (int i = 1; i <= 7 && hero; ++i)
				{
					Object *shard = m.unit("Shard 0" + std::to_string(i));
					if (!shard || shard->isEffectivelyDead())
					{
						continue;
					}
					// carried (ATTACHED, status 0x62): to the ring; else the hero walks onto it
					m.place(*hero, shard->testStatus(0x62) ? m.centreOf("AREA - Angmar Ring Return") : *shard->getPosition());
					break;
				}
			}
			d.step();
			hashes.push_back(m.logic().computeStateHash());
		}
		const ScriptEngine::Counter *c = m.engine().findCounter("/COUNTER - Shards");
		MESSAGE("shards " << (c ? c->value : -1) << ", hash " << hashes.back());
	};
	std::vector<std::uint32_t> a, b;
	run(a);
	run(b);
	REQUIRE(a.size() == b.size());
	size_t firstDiff = a.size();
	for (size_t i = 0; i < a.size() && firstDiff == a.size(); ++i)
	{
		firstDiff = a[i] != b[i] ? i : firstDiff;
	}
	CHECK(firstDiff == a.size());
}

TEST_CASE("camp1 determinism: the attacked-by flags (Player + 0x360 / + 0x374) and the last damage record's player mask join the state hash")
{
	OPENBFME_REQUIRE_START(s);
	Mission m;
	m.load(*s, "map ang amon sul");
	m.step(2);
	const std::uint32_t before = m.logic().computeStateHash();
	Player *local = m.logic().players().getLocalPlayer();
	Player *other = m.logic().players().findPlayerWithName("PlyrArnor");
	REQUIRE(local != nullptr);
	REQUIRE(other != nullptr);
	CHECK_FALSE(local->getAttackedBy(other->getPlayerIndex()));
	local->setAttackedBy(other->getPlayerIndex(), m.frame());
	CHECK(local->getAttackedBy(other->getPlayerIndex()));
	const std::uint32_t flagged = m.logic().computeStateHash();
	CHECK(flagged != before);
	// a hit records the source player's mask on the body (TEAM_ATTACKED_BY_PLAYER) and marks the victim's player (RW 0x8C4341)
	Object *tower = m.unit("Tower of Amon Sul");
	Object *wk = m.unit("Witch King");
	REQUIRE(tower != nullptr);
	REQUIRE(wk != nullptr);
	hit(*tower, *wk, 1.0f);
	CHECK(m.logic().computeStateHash() != flagged);
	CHECK(other->getAttackedBy(local->getPlayerIndex()));
}

// lane CAMP-1H: getTeamNamed's create flag (RW 0x759FDA) and the team's activation / creation step (RW 0x7A208C) on MAP ANG Dark Eye's prototypes
TEST_CASE("camp1h team activation: an inactive singleton or an empty prototype answers no team without the create flag; created, then ready after "
		  "the next team update; TEAM_CREATED reads the created flag")
{
	OPENBFME_REQUIRE_START(s);
	Mission m;
	m.load(*s, "map ang dark eye");
	m.step(2);
	TeamPrototype *inactiveSingleton = nullptr, *emptyPrototype = nullptr;
	for (const auto &proto : m.logic().players().teams().prototypes())
	{
		if (!proto->getControllingPlayer())
		{
			continue;
		}
		if (proto->getIsSingleton() && proto->getNewestTeam() && !proto->getNewestTeam()->isActive() && !inactiveSingleton)
		{
			inactiveSingleton = proto.get();
		}
		if (!proto->getIsSingleton() && proto->teams().empty() && !emptyPrototype)
		{
			emptyPrototype = proto.get();
		}
	}
	REQUIRE(inactiveSingleton != nullptr);
	REQUIRE(emptyPrototype != nullptr);
	ScriptEngine &e = m.engine();
	const std::string single = inactiveSingleton->getControllingPlayer()->getPlayerName() + "/" + inactiveSingleton->getName();
	const std::string empty = emptyPrototype->getControllingPlayer()->getPlayerName() + "/" + emptyPrototype->getName();
	CHECK(ScriptConditions::team(e, single) == nullptr);
	CHECK(ScriptConditions::team(e, empty) == nullptr);
	CHECK(emptyPrototype->teams().empty()); // no implicit instance without the flag
	Team *a = ScriptConditions::team(e, single, true);
	REQUIRE(a != nullptr);
	CHECK(a->isActive());
	CHECK(a->scriptState().created);
	CHECK_FALSE(a->scriptState().ready);
	Team *b = ScriptConditions::team(e, empty, true);
	REQUIRE(b != nullptr);
	CHECK(b->isActive());
	CHECK(emptyPrototype->teams().size() == 1);
	CHECK(ScriptConditions::team(e, single) == a); // active now: found without the flag
	e.updateTeamStates();
	CHECK_FALSE(a->scriptState().created);
	CHECK(a->scriptState().ready);
	CHECK(b->scriptState().ready);
}

// lane CAMP-1H: every Angmar mission (the campaign's and the bonus one) run twice with no player input agrees on every frame's state hash: the map scripts,
// the team scripts (Team::updateState / the generic hooks), the difficulty bonus and the AI's moves are deterministic
TEST_CASE("camp1h determinism: every Angmar mission twice for 900 frames agrees on every frame's state hash")
{
	for (const char *map : { "map ang angmar", "map ang rhudaur", "map ang amon sul", "map ang dark eye", "map ang barrow downs", "map ang carn dum",
			 "map ang barrow wights", "map ang fornost", "map ang bonus" })
	{
		OPENBFME_REQUIRE_START(s);
		auto run = [&](std::vector<std::uint32_t> &hashes) {
			Mission m;
			m.load(*s, map);
			for (int f = 0; f < 900; ++f)
			{
				m.step();
				hashes.push_back(m.logic().computeStateHash());
			}
		};
		std::vector<std::uint32_t> a, b;
		run(a);
		run(b);
		size_t first = a.size();
		for (size_t i = 0; i < a.size() && i < b.size(); ++i)
		{
			if (a[i] != b[i])
			{
				first = i;
				break;
			}
		}
		INFO(std::string(map));
		CHECK(a.size() == b.size());
		CHECK(first == a.size()); // no frame differs
		MESSAGE(std::string(map) << ": 900 frames, last hash " << a.back());
	}
}

// lane CAMP-1H r2 (Sol): getTeamNamed (RW 0x759FDA) answers the contextual team when the qualified name matches it, TEAM_CREATED reads the creation flag
// (+ 0x5E), and an AttachUpdate whose carrier died is killed by the body's attemptDamage with source 0 (RW 0x698EC3), not by itself
TEST_CASE("camp1h r2 team context: a qualified name matching the running team script's team answers that team, not the newest instance")
{
	OPENBFME_REQUIRE_START(s);
	Mission m;
	m.load(*s, "map ang dark eye");
	m.step(2);
	TeamPrototype *proto = nullptr;
	for (const auto &p : m.logic().players().teams().prototypes())
	{
		if (!p->getIsSingleton() && p->getControllingPlayer() && !proto)
		{
			proto = p.get();
		}
	}
	REQUIRE(proto != nullptr);
	TeamFactory &teams = m.logic().players().teams();
	Team *older = teams.createTeam(proto, true);
	Team *newer = teams.createTeam(proto, true);
	REQUIRE(older != newer);
	const std::string name = proto->getControllingPlayer()->getPlayerName() + "/" + proto->getName();
	CHECK(ScriptConditions::team(m.engine(), name) == newer);
	{
		ScriptEngine::ThisTeamScope scope(m.engine(), older);
		CHECK(ScriptConditions::team(m.engine(), name) == older);
		CHECK(ScriptConditions::team(m.engine(), name, true) == older);
	}
	CHECK(ScriptConditions::team(m.engine(), name) == newer);
}

TEST_CASE("camp1h r2 TEAM_CREATED: true for an activated team until its creation step ran, also with no members")
{
	OPENBFME_REQUIRE_START(s);
	Mission m;
	m.load(*s, "map ang dark eye");
	m.step(2);
	TeamPrototype *proto = nullptr;
	for (const auto &p : m.logic().players().teams().prototypes())
	{
		if (!p->getIsSingleton() && p->getControllingPlayer() && p->teams().empty() && !proto)
		{
			proto = p.get();
		}
	}
	REQUIRE(proto != nullptr);
	Team *t = m.logic().players().teams().createTeam(proto, true);
	REQUIRE(t->getMemberCount() == 0);
	const std::string name = proto->getControllingPlayer()->getPlayerName() + "/" + proto->getName();
	ScriptCondition c;
	c.type = 25;
	c.resolved = 25;
	ScriptParameter p;
	p.type = 3;
	p.stringValue = name;
	c.params.push_back(p);
	CHECK(ScriptConditions::evaluate(m.engine(), c));
	m.engine().updateTeamStates(); // the creation step: + 0x5E cleared
	CHECK_FALSE(ScriptConditions::evaluate(m.engine(), c));
	CHECK_FALSE(t->scriptState().created);
}

TEST_CASE("camp1h r2 AttachUpdate: a carrier's death kills the carried object with source 0, not by itself (no attacked-by-self record)")
{
	OPENBFME_REQUIRE_START(s);
	Mission m;
	m.load(*s, "map ang dark eye");
	m.step(30);
	Player *local = m.logic().players().getLocalPlayer();
	std::string err;
	const Coord3D at = m.centreOf("AREA - Angmar Ring Return");
	Object *hero0 = m.game->createObject("AngmarHwaldar", local->getPlayerIndex(), at, 0.0f, &err); // a HERO of the player (the mission's own carry-over hero)
	REQUIRE_MESSAGE(hero0 != nullptr, err);
	REQUIRE(hero0->isKindOfName("HERO"));
	// owned by the neutral player, so the carrier's own death (the helper kills it by itself) cannot set the shard owner's attacked-by-self record
	Player *neutral = m.logic().players().getNeutralPlayer();
	REQUIRE(neutral != nullptr);
	Object *shard = m.game->createObject("PalantirShard01", neutral->getPlayerIndex(), at, 0.0f, &err);
	REQUIRE_MESSAGE(shard != nullptr, err);
	const ObjectID shardId = shard->getID();
	Object *hero = nullptr;
	for (Object *o = m.logic().getFirstObject(); o && !hero; o = o->getNextObject())
	{
		hero = o->getControllingPlayer() == local && o->isKindOfName("HERO") && !o->isEffectivelyDead() && !o->getContainedBy() ? o : nullptr;
	}
	REQUIRE(hero != nullptr);
	m.place(*hero, *shard->getPosition());
	AttachUpdate *attach = nullptr;
	for (const std::unique_ptr<BehaviorModule> &mod : shard->modules())
	{
		if (AttachUpdate *a = dynamic_cast<AttachUpdate *>(mod.get()))
		{
			attach = a;
		}
	}
	REQUIRE(attach != nullptr);
	REQUIRE(m.until([&] { return attach->parentID() == hero->getID() || attach->parentID() != 0; }, 60));
	Object *carrier = m.logic().findObjectByID(attach->parentID());
	REQUIRE(carrier != nullptr);
	const Player *shardOwner = shard->getControllingPlayer();
	REQUIRE(shardOwner != nullptr);
	m.kill(*carrier);
	REQUIRE(m.until([&] { Object *x = m.logic().findObjectByID(shardId); return x == nullptr || x->isEffectivelyDead(); }, 30));
	CHECK_FALSE(shardOwner->getAttackedBy(shardOwner->getPlayerIndex())); // the shard did not "attack" its own player
}
