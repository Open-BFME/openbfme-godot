// OpenBFME unit tests. GPL-3.0.
// Lane MODCOMPAT-MOD1: AutoPickUpUpdate (RW 0x89958F / 0x899562, the WAKE_AUTO_PICKUP button RW 0x68C8DE). Binary facts read from the RotWK binary
// (RW_GAME_DAT), the retail data (ROTWK_INSTALL) and live objects in a started skirmish. The expectations are retail's rules (each check names its address).

#include "doctest.h"

#include "BuildTestUtil.h"
#include "PeImage.h"
#include "StartTestUtil.h"

#include "Common/Player.h"
#include "Common/PlayerList.h"
#include "Common/StateHash.h"
#include "Common/Thing/ThingFactory.h"
#include "Common/Thing/ThingTemplate.h"
#include "Common/XferLoad.h"
#include "Common/XferSave.h"
#include "GameClient/ControlBarCommands.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AutoPickUpUpdate.h"
#include "GameLogic/Module/GrabPassengerSpecialPower.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/ObjectFilterMatch.h"
#include "GameLogic/ObjectTemplateInfo.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace
{
float floatAt(const retailtest::PeImage &pe, std::uint32_t va)
{
	const std::uint32_t u = pe.u32At(va);
	float f;
	std::memcpy(&f, &u, 4);
	return f;
}

// the lobby's result: slot 0 a human (Men), slot 1 an easy computer (Mordor) on Evendim, start positions 0 and 4 (test_pickup_modules.cpp's game)
bool startGame(starttest::Shared &s, std::uint32_t seed, buildtest::Game &out, std::string *error)
{
	static std::vector<MapCacheEntry> cache;
	if (cache.empty() && !IniSkirmishSetupSource::loadMapCache(*s.mount->fs, cache, error))
	{
		return false;
	}
	startgolden::Spec a{ 0 }, b{ 1 };
	a.tmpl = buildtest::templateIndex(s, "FactionMen");
	b.tmpl = buildtest::templateIndex(s, "FactionMordor");
	a.color = 0;
	b.color = 1;
	a.start = 0;
	b.start = 4;
	NewGameMessage message;
	message.game = startgolden::makeInfo({ a, b });
	message.game.slots[1].state = SLOT_EASY_AI;
	message.game.mapName = "maps/map mp evendim/map mp evendim.map";
	message.game.seed = seed;
	message.game.startingCash = 5000;
	if (!NewGame::prepareNewGame(message, s.world->playerTemplates(), s.settings, cache, RandomAlgorithm::ZH_CarryChain, out.start, error))
	{
		return false;
	}
	out.source = std::make_unique<ArchiveW3DFileSource>(*s.mount->fs);
	out.assets = std::make_unique<WW3DAssetManager>(*out.source);
	out.live = std::make_unique<LiveGame>(*s.world, *s.mount->fs, *out.assets, s.options);
	LiveGame::Options o;
	o.start = &out.start;
	return out.live->load(o, error);
}

const Player *playerAtStart(GameLogic &logic, int start)
{
	for (int i = 0; i < logic.players().getPlayerCount(); ++i)
	{
		const Player *p = logic.players().getNthPlayer(i);
		if (p && p->getPlayerTemplate() && p->getPlayerTemplate()->m_playableSide && p->getMultiplayerStartIndex() == start)
		{
			return p;
		}
	}
	return nullptr;
}

Coord3D besideFortress(GameLogic &logic, const Player *owner, float dx, float dy)
{
	for (Object *x = logic.getFirstObject(); x; x = x->getNextObject())
	{
		if (x->getControllingPlayer() == owner && x->isKindOfName("COMMANDCENTER") && !x->isEffectivelyDead())
		{
			return Coord3D{ x->getPosition()->x + dx, x->getPosition()->y + dy, x->getPosition()->z };
		}
	}
	FAIL("no fortress");
	return Coord3D{};
}

std::vector<AutoPickUpUpdate *> autoPickUpsOf(Object &o)
{
	std::vector<AutoPickUpUpdate *> out;
	for (const auto &m : o.modules())
	{
		if (AutoPickUpUpdate *a = dynamic_cast<AutoPickUpUpdate *>(m.get()))
		{
			out.push_back(a);
		}
	}
	return out;
}

unsigned stopHits(GameLogic &logic, const char *prefix)
{
	unsigned n = 0;
	for (const auto &kv : logic.notedStopHits())
	{
		n += kv.first.rfind(prefix, 0) == 0 ? kv.second : 0;
	}
	return n;
}

const AutoPickUpUpdateModuleData *dataOf(starttest::Shared &s, const char *tmpl, const char *tag)
{
	const ThingTemplate *tt = s.world->things().findTemplate(tmpl);
	REQUIRE_MESSAGE(tt, tmpl);
	for (const ThingTemplate::Nugget &n : tt->behaviorModules().nuggets())
	{
		if (n.name == "AutoPickUpUpdate" && n.tag == tag)
		{
			return dynamic_cast<const AutoPickUpUpdateModuleData *>(n.data.get());
		}
	}
	return nullptr;
}
} // namespace

// ---- binary facts ------------------------------------------------------------------------------------------------------------------------------------------

TEST_CASE("autopickup1: AutoPickUpUpdate's binary facts: the data defaults (RW 0x899A96), EatObjectEntry's default (RW 0x899C7D) and the interface (RW 0xC65638)")
{
	const retailtest::PeImage *pe = retailtest::PeImage::fromEnvironment();
	if (!pe)
	{
		retailtest::printSkip("autopickup1 binary facts (set RW_GAME_DAT)");
		return;
	}
	CHECK(floatAt(*pe, 0xBD9E90) == 300.0f); // ScanDistance
	CHECK(floatAt(*pe, 0xBD1908) == 1.0f);   // TargetHealth
	// the AutoPickUpUpdateInterface (+ 0x20): RunFromButtonNumber, RunFromButton, the button wake, then the update interface's vtable (update RW 0x89958F)
	CHECK(pe->u32At(0xC65638u) == 0x899554u);
	CHECK(pe->u32At(0xC6563Cu) == 0x89955Bu);
	CHECK(pe->u32At(0xC65640u) == 0x8993FCu);
	CHECK(pe->u32At(0xC65648u) == 0x89958Fu);
	// the field table RW 0xC65B60: the parse procs in row order
	const std::uint32_t procs[] = { 0x73A429u, 0x76392Fu, 0x42ED00u, 0x899C7Du, 0x42E558u, 0x42E558u, 0x42EC5Eu, 0x42E558u };
	const std::uint32_t offsets[] = { 0x08u, 0x0Cu, 0x10u, 0x14u, 0x20u, 0x21u, 0x24u, 0x28u };
	for (int i = 0; i < 8; ++i)
	{
		CHECK(pe->u32At(0xC65B60u + 16u * i + 4u) == procs[i]);
		CHECK(pe->u32At(0xC65B60u + 16u * i + 12u) == offsets[i]);
	}
	CHECK(pe->u32At(0xC65B60u + 16u * 8u) == 0u);
}

TEST_CASE("autopickup1: the 2.01 data: the Watcher's head eats infantry, the Mountain Troll's buttons grab a tree (2) and an orc (3); the class is bound")
{
	OPENBFME_REQUIRE_START(s);
	const std::vector<std::string> unported = s->world->modules().unportedModuleClassNames();
	CHECK(std::find(unported.begin(), unported.end(), "AutoPickUpUpdate") == unported.end());
	const AutoPickUpUpdateModuleData *head = dataOf(*s, "WatcherHead", "AutoPickUpForStuffToEat");
	REQUIRE(head);
	CHECK(head->m_scanDistance == 240.0f);
	CHECK(!head->m_runFromButton);
	REQUIRE(head->m_eatObjectEntries.size() == 1);
	CHECK(head->m_eatObjectEntries[0].m_myHealth == 1.0f);
	CHECK(head->m_eatObjectEntries[0].m_targetHealth == 1.0f);
	CHECK(head->m_eatObjectEntries[0].m_filter.rule == ObjectFilter::RULE_NONE);
	CHECK(head->m_pickUpFilter.rule == ObjectFilter::RULE_NONE); // RW 0x763D11's default: allows nothing
	CHECK(!ObjectFilterMatch::isValid(&head->m_pickUpFilter));
	const AutoPickUpUpdateModuleData *trees = dataOf(*s, "MordorMountainTroll", "AutoPickUpForTrees");
	const AutoPickUpUpdateModuleData *orcs = dataOf(*s, "MordorMountainTroll", "AutoPickUpForTastyOrcs");
	REQUIRE(trees);
	REQUIRE(orcs);
	CHECK(trees->m_runFromButton);
	CHECK(trees->m_runFromButtonNumber == 2);
	CHECK(trees->m_scanDistance == 600.0f);
	CHECK(trees->m_scanDelayTime == 0u);
	CHECK(orcs->m_runFromButtonNumber == 3);
	CHECK(orcs->m_eatObjectEntries.size() == 1);
	// RW 0x76379C: NONE +TREE allows the mask {TREE}; the eat module's default filter does not
	KindOfMaskType tree{};
	MaskSet(tree, (unsigned)ObjectTemplateInfoBuilder::kindOfIndex("TREE"), true);
	CHECK(ObjectFilterMatch::allowsKindOf(trees->m_pickUpFilter, tree));
	CHECK(!ObjectFilterMatch::allowsKindOf(orcs->m_pickUpFilter, tree));
	int found = 0;
	for (const std::string &l : s->world->acceptanceStops())
	{
		found += l.rfind("[S-7160] AutoPickUpUpdate", 0) == 0 ? 1 : 0;
	}
	CHECK(found == 1);
}

// ---- the live module ------------------------------------------------------------------------------------------------------------------------------------------

TEST_CASE("autopickup1: the Watcher's head scans every ScanDelayTime, grabs a soldier through its GrabPassengerSpecialPower and eats it; the save keeps the countdown")
{
	OPENBFME_REQUIRE_START(s);
	buildtest::Game g;
	std::string err;
	REQUIRE_MESSAGE(startGame(*s, 7301, g, &err), err);
	GameLogic &logic = g.live->logic();
	const Player *human = playerAtStart(logic, 0);
	const Player *computer = playerAtStart(logic, 4);
	REQUIRE(human);
	REQUIRE(computer);
	const Coord3D at = besideFortress(logic, human, 700.0f, 0.0f);
	Object *head = g.live->createObject("WatcherHead", computer->getPlayerIndex(), at, 0.0f, &err);
	REQUIRE_MESSAGE(head, err);
	// the soldier is the head's own (the filter has no relationship; an enemy would be killed by the head's attacks first): no fight between the two
	Object *soldier = g.live->createObject("GondorFighter", computer->getPlayerIndex(), Coord3D{ at.x + 100.0f, at.y, at.z }, 0.0f, &err);
	REQUIRE_MESSAGE(soldier, err);
	const ObjectID soldierId = soldier->getID();
	const std::vector<AutoPickUpUpdate *> modules = autoPickUpsOf(*head);
	REQUIRE(modules.size() == 1);
	AutoPickUpUpdate *m = modules[0];
	GrabPassengerSpecialPower *grab = dynamic_cast<GrabPassengerSpecialPower *>(head->findModule("GrabPassengerSpecialPower"));
	REQUIRE(grab);
	const unsigned delay = m->data()->m_scanDelayTime;
	CHECK(delay == 50u); // 10000 ms (parseDurationUnsignedInt RW 0x73A429)
	CHECK(m->countdown() <= delay); // tolerance: RW 0x8994BF starts it at GameLogicRandomValue(0, ScanDelayTime)
	const unsigned before = stopHits(logic, "[S-7160]");
	int frames = 0;
	while (m->scans() == 0 && frames < (int)delay + 5)
	{
		g.live->stepFrame();
		++frames;
	}
	MESSAGE("the first scan came after " << frames << " frames (ScanDelayTime " << delay << " frames)");
	REQUIRE(m->scans() == 1);
	CHECK(m->countdown() == delay); // RW 0x899562: reloaded when it reached 0
	// RW 0x8997A9 .. 0x8998D8: the head is full health (1.0 <= MyHealth 1.0), the soldier (INFANTRY, not HERO, not HORDE) is within 240: the
	// SPECIAL_GRAB_PASSENGER power's doSpecialPowerAtObject(soldier, 0x4000) (RW 0x691526 -> GrabPassengerSpecialPower RW 0x8C94D7) starts the grab
	CHECK(grab->grabsStarted() == 1);
	CHECK(stopHits(logic, "[S-7160]") == before);
	// the grab ability holds the soldier, then eats it (test_grabpassenger.cpp measures the frames of the same sequence)
	int heldAt = -1;
	for (int i = 0; i < 60 && heldAt < 0; ++i)
	{
		g.live->stepFrame();
		if (soldier->getContainedBy() == head)
		{
			heldAt = i;
		}
	}
	MESSAGE("held after " << heldAt + 1 << " frames");
	REQUIRE(soldier->getContainedBy() == head);
	int eatenAt = -1;
	for (int i = 0; i < 80 && eatenAt < 0; ++i)
	{
		g.live->stepFrame();
		if (!logic.findObjectByID(soldierId))
		{
			eatenAt = i;
		}
	}
	MESSAGE("eaten " << eatenAt + 1 << " frames after the hold");
	REQUIRE(!logic.findObjectByID(soldierId));
	CHECK(head->getContain()->getContainCount() == 0);
	CHECK(grab->grabsStarted() == 1); // while the contain holds the soldier the scan's steps are skipped (RW 0x8997E0 .. 0x8998E0)
	// the snapshot: the countdown survives a save and load
	StateHasher h1;
	m->crc(h1);
	XferSave saved;
	m->xfer(saved);
	const unsigned savedCountdown = m->countdown();
	g.live->stepFrame();
	g.live->stepFrame();
	CHECK(m->countdown() != savedCountdown); // tolerance: two frames on, the countdown has moved (down by two, or reloaded at 0)
	{
		XferLoad load(saved.data());
		m->xfer(load);
	}
	CHECK(m->countdown() == savedCountdown);
	StateHasher h2;
	m->crc(h2);
	CHECK(h2.value() == h1.value());
	std::vector<std::uint8_t> truncated = saved.data();
	truncated.pop_back();
	XferLoad lt(truncated);
	CHECK_THROWS_AS(m->xfer(lt), XferException);
}

TEST_CASE("autopickup1: the WAKE_AUTO_PICKUP button (RW 0x68C8DE) wakes the Mountain Troll's module of its number once; the module does not scan on its own")
{
	OPENBFME_REQUIRE_START(s);
	buildtest::Game g;
	std::string err;
	REQUIRE_MESSAGE(startGame(*s, 7302, g, &err), err);
	GameLogic &logic = g.live->logic();
	const Player *computer = playerAtStart(logic, 4);
	REQUIRE(computer);
	Object *troll = g.live->createObject("MordorMountainTroll", computer->getPlayerIndex(), besideFortress(logic, computer, 500.0f, 0.0f), 0.0f, &err);
	REQUIRE_MESSAGE(troll, err);
	const std::vector<AutoPickUpUpdate *> modules = autoPickUpsOf(*troll);
	REQUIRE(modules.size() == 2);
	AutoPickUpUpdate *trees = modules[0]->getRunFromButtonNumber() == 2 ? modules[0] : modules[1];
	AutoPickUpUpdate *orcs = trees == modules[0] ? modules[1] : modules[0];
	REQUIRE(trees->getRunFromButtonNumber() == 2);
	REQUIRE(orcs->getRunFromButtonNumber() == 3);
	for (int i = 0; i < 20; ++i)
	{
		g.live->stepFrame();
	}
	CHECK(trees->scans() == 0); // RunFromButton: the gate is the button flag (RW 0x899562)
	CHECK(orcs->scans() == 0);
	// the button with OPTION_TWO (RW 0x6973A7: number 2)
	CommandButton b;
	b.m_command = GUI_COMMAND_WAKE_AUTO_PICKUP;
	b.m_options = 0x4000u;
	troll->doCommandButton(&b, CMD_FROM_PLAYER, false);
	CHECK(trees->buttonWake());
	CHECK(trees->buttonWoken());
	CHECK(!orcs->buttonWake());
	const unsigned before = stopHits(logic, "[S-7160]");
	g.live->stepFrame();
	CHECK(trees->scans() == 1);
	CHECK(!trees->buttonWake()); // RW 0x8995F3: cleared by the scan
	CHECK(trees->buttonWoken()); // + 0x29 stays
	CHECK(orcs->scans() == 0);
	// NONE +TREE: the terrain tree point (RW 0x67F52B) is reported (S-7160)
	CHECK(stopHits(logic, "[S-7160]") > before); // tolerance: once for the tree point, once more when a tree object is within 600 (the grab power)
	for (int i = 0; i < 10; ++i)
	{
		g.live->stepFrame();
	}
	CHECK(trees->scans() == 1); // once per press
}
