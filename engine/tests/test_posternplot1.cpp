// OpenBFME unit tests, lane POSTERNPLOT-1: the FoundationAIUpdate interface's slot 2 (RW 0x8582DE, tier A = BFME2 0x85527A, decomp FoundationAIUpdate.cpp
// rva0045527A) called by DynamicPortalBehaviour's upgrade and removal (RW 0x8643CA / 0x863D56). GPL-3.0.
//
// On the bytes: an unchanged id returns (RW 0x8582E5); the id goes to + 0x28; with an id a foundation that is no WALL_UPGRADE (template + 0x11A & 0x40) gets
// status 3 (UNSELECTABLE) and its drawable setSelectable(false) + fadeOut(10); a WALL_UPGRADE one, the local player's, posts MSG_REMOVE_FROM_SELECTED_GROUP
// (0x3ED) and is deselected; with 0 a foundation that is no WALL_UPGRADE loses status 3 and its drawable setSelectable(true) + fadeIn(30). Retail's two
// objects with both modules (GondorCastleUpgrade, RohanCastleUpgrade) are WALL_UPGRADE.

#include "doctest.h"

#include "MoveTestUtil.h"
#include "StartTestUtil.h"

#include "Common/Upgrade.h"
#include "GameClient/ClientEvents.h"
#include "GameClient/GUI/Skirmish/IniSkirmishSetupSource.h"
#include "GameClient/LiveGame.h"
#include "GameLogic/Construction.h"
#include "GameLogic/Module/LogicModules.h"
#include "GameLogic/Module/CastleModules.h"
#include "GameLogic/Module/PortalModules.h"
#include "GameLogic/NewGame/NewGame.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/PlayerCommands.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"

#include <memory>
#include <string>
#include <vector>

namespace
{
std::string plot(const char *name, const char *kinds, bool ai, bool member)
{
	std::string s = std::string("Object ") + name + "\n  KindOf = " + kinds + "\n";
	s += "  Behavior = DynamicPortalBehaviour ModuleTag_Portal\n    TriggeredBy = Upgrade_Barracks2\n  End\n";
	s += "  Behavior = FoundationAIUpdate ModuleTag_FA\n  End\n";
	if (member)
	{
		s += "  Behavior = CastleMemberBehavior ModuleTag_CM\n  End\n";
	}
	if (ai)
	{
		s += "  Behavior = AIUpdateInterface ModuleTag_AI\n  End\n";
	}
	return s + "End\n";
}

struct Hooks : ObjectClientHooks
{
	std::vector<std::pair<char, UnsignedInt>> calls; // 'o' fadeOut, 'i' fadeIn, 'd' deselectLocal
	void objectCreated(Object &obj) override { obj.friend_bindToClient(this); } // as ClientEventRecorder::objectCreated
	void objectDestroyed(Object &) override {}
	void fadeIn(Object &, UnsignedInt frames) override { calls.emplace_back('i', frames); }
	void fadeOut(Object &, UnsignedInt frames) override { calls.emplace_back('o', frames); }
	void deselectLocal(Object &) override { calls.emplace_back('d', 0u); }
};

// MoveWorld: the AI modules (AIUpdateInterface) come from the AI world
struct PlotWorld : movetest::MoveWorld
{
	PlotWorld() {}
	void load(const std::string &text)
	{
		const std::string err = w.load(text, INI_LOAD_OVERWRITE, "plots.ini");
		REQUIRE_MESSAGE(err.empty(), err);
	}
	Player *alice() { return players.findPlayerWithName("Alice"); }
};

struct PlotFx : PlotWorld
{
	Hooks fake;
	ObjectClientHooks *before = nullptr;
	PlotFx()
	{
		load(plot("PPlot", "STRUCTURE SELECTABLE IMMOBILE", true, true) + plot("PWallPlot", "STRUCTURE SELECTABLE IMMOBILE WALL_UPGRADE", true, true) +
			 plot("PNoAI", "STRUCTURE SELECTABLE IMMOBILE", false, true) + plot("PNoMember", "STRUCTURE SELECTABLE IMMOBILE", true, false));
		before = logic->clientHooks();
		logic->setClientHooks(&fake); // the objects made from here on are bound to these hooks (RW 0x628882)
		buildMap();
	}
	~PlotFx()
	{
		logic->reset(); // the objects bound to `fake` go before it does
		logic->setClientHooks(before);
	}
	static ObjectID occupant(Object &o)
	{
		const CastleMemberBehavior *m = dynamic_cast<const CastleMemberBehavior *>(o.findModule("CastleMemberBehavior"));
		REQUIRE(m);
		return m->occupantId();
	}
	static bool unselectable(const Object &o)
	{
		const int bit = ObjectTemplateInfoBuilder::objectStatusIndex("UNSELECTABLE");
		REQUIRE(bit >= 0); // tolerance: an index of the status table (-1: missing)
		return o.testStatus((unsigned)bit);
	}
	void remove(Object &o) { o.removeUpgrade(TheUpgradeCenter->findUpgrade("Upgrade_Barracks2")); }
};

const char *const kStop = "[S-4855] DynamicPortalBehaviour (RW 0x864357 / 0x863D3A)";

bool stopMet(GameLogic &logic)
{
	for (const auto &s : logic.notedStopHits())
	{
		if (s.first.rfind(kStop, 0) == 0)
		{
			return true;
		}
	}
	return false;
}
} // namespace

TEST_CASE("posternplot1: the upgrade makes a plot that is no WALL_UPGRADE its own occupant, UNSELECTABLE, faded out over 10; the removal restores it, fading in over 30")
{
	PlotFx f;
	Object *o = f.make("PPlot", f.teamOf("Alice"));
	REQUIRE(o);
	REQUIRE(o->getAIUpdateInterface());
	REQUIRE(DynamicPortalBehaviour::of(*o));
	REQUIRE(o->clientHooks() == &f.fake);
	f.frames(1);
	CHECK(PlotFx::occupant(*o) == INVALID_ID);
	CHECK_FALSE(PlotFx::unselectable(*o));
	CHECK(PlayerCommands::isSelectable(*o));
	o->giveUpgrade("Upgrade_Barracks2");
	CHECK(PlotFx::occupant(*o) == o->getID()); // RW 0x8643DE: the object's own id (+ 0x74)
	CHECK(PlotFx::unselectable(*o));            // RW 0x858318
	CHECK_FALSE(PlayerCommands::isSelectable(*o));
	REQUIRE(f.fake.calls.size() == 1);
	CHECK(f.fake.calls[0] == std::make_pair('o', 10u)); // RW 0x85832E
	f.remove(*o);
	CHECK(PlotFx::occupant(*o) == INVALID_ID);
	CHECK_FALSE(PlotFx::unselectable(*o)); // RW 0x85839F
	CHECK(PlayerCommands::isSelectable(*o));
	REQUIRE(f.fake.calls.size() == 2);
	CHECK(f.fake.calls[1] == std::make_pair('i', 30u)); // RW 0x8583C9
	CHECK_FALSE(stopMet(*f.logic));
}

TEST_CASE("posternplot1: a WALL_UPGRADE plot (retail's GondorCastleUpgrade kind) takes the occupant but stays selectable; the client is told to deselect it")
{
	PlotFx f;
	Object *o = f.make("PWallPlot", f.teamOf("Alice"));
	REQUIRE(o);
	f.frames(1);
	o->giveUpgrade("Upgrade_Barracks2");
	CHECK(PlotFx::occupant(*o) == o->getID());
	CHECK_FALSE(PlotFx::unselectable(*o));
	CHECK(PlayerCommands::isSelectable(*o));
	REQUIRE(f.fake.calls.size() == 1);
	CHECK(f.fake.calls[0].first == 'd'); // RW 0x85834C .. 0x858389
	f.remove(*o);
	CHECK(PlotFx::occupant(*o) == INVALID_ID);
	CHECK_FALSE(PlotFx::unselectable(*o));
	CHECK(f.fake.calls.size() == 1); // RW 0x858393: a WALL_UPGRADE foundation does nothing more with 0
	CHECK_FALSE(stopMet(*f.logic));
}

TEST_CASE("posternplot1: the slot does nothing for an unchanged id; without an AI the portal does not call it; without a CastleMemberBehavior the stop is reported")
{
	PlotFx f;
	Object *o = f.make("PPlot", f.teamOf("Alice"));
	Object *noAi = f.make("PNoAI", f.teamOf("Alice"));
	Object *noMember = f.make("PNoMember", f.teamOf("Alice"));
	REQUIRE(o);
	REQUIRE(noAi);
	REQUIRE(noMember);
	f.frames(1);
	CHECK(Construction::setFoundationOccupant(*o, o->getID()));
	CHECK(Construction::setFoundationOccupant(*o, o->getID())); // RW 0x8582E5: the same id again
	CHECK(f.fake.calls.size() == 1);
	CHECK(PlotFx::unselectable(*o));
	CHECK(Construction::setFoundationOccupant(*o, INVALID_ID));
	CHECK(Construction::setFoundationOccupant(*o, INVALID_ID));
	CHECK(f.fake.calls.size() == 2);
	CHECK_FALSE(PlotFx::unselectable(*o));

	noAi->giveUpgrade("Upgrade_Barracks2"); // RW 0x8643CA: no AI (+ 0x260), no call
	CHECK(PlotFx::occupant(*noAi) == INVALID_ID);
	CHECK_FALSE(PlotFx::unselectable(*noAi));
	CHECK_FALSE(stopMet(*f.logic));

	noMember->giveUpgrade("Upgrade_Barracks2");
	CHECK_FALSE(PlotFx::unselectable(*noMember)); // the port keeps no occupant there (S-952): nothing changes
	CHECK(stopMet(*f.logic));
	CHECK(f.fake.calls.size() == 2);
}

TEST_CASE("posternplot1: the client event DESELECT_LOCAL carries the object's owner at the call")
{
	PlotWorld w;
	w.load(plot("PWallPlot", "STRUCTURE SELECTABLE IMMOBILE WALL_UPGRADE", true, true));
	ClientEventRecorder rec(*w.logic);
	Object *o = w.make("PWallPlot", w.teamOf("Alice"));
	REQUIRE(o);
	rec.deselectLocal(*o);
	const std::vector<ClientEvent> events = rec.take(false);
	REQUIRE_FALSE(events.empty());
	const ClientEvent *last = &events.back();
	CHECK(last->kind == ClientEvent::DESELECT_LOCAL);
	CHECK(last->object == o->getID());
	CHECK(last->ownerIndex == w.alice()->getPlayerIndex());
}

namespace
{
using namespace starttest;

int templateIndex(Shared &s, const std::string &name)
{
	for (int i = 0; i < s.world->playerTemplates().getPlayerTemplateCount(); ++i)
	{
		const PlayerTemplate *pt = s.world->playerTemplates().getNthPlayerTemplate(i);
		if (pt && pt->getName() == name)
		{
			return i;
		}
	}
	return -1;
}
} // namespace

TEST_CASE("posternplot1 retail: MAP WOR Minas Tirith: Upgrade_PosternGate on a MinisWallAUpgrade (GondorCastleUpgrade, WALL_UPGRADE) makes it its own occupant; it stays selectable")
{
	OPENBFME_REQUIRE_START(s);
	NewGameMessage m;
	m.game.mapName = "maps/map wor minas tirith/map wor minas tirith.map";
	m.game.seed = 11;
	m.game.startingCash = 1000;
	const char *factions[2] = { "FactionMen", "FactionMordor" };
	for (int i = 0; i < 2; ++i)
	{
		SkirmishGameSlot &sl = m.game.slots[i];
		sl.state = i == 0 ? SLOT_PLAYER : SLOT_EASY_AI;
		sl.name = i == 0 ? u"Host" : u"Easy";
		sl.accepted = true;
		sl.playerTemplate = templateIndex(*s, factions[i]);
		REQUIRE(sl.playerTemplate >= 0); // tolerance: the index of a found template (-1: not found)
		sl.startPos = i;
		sl.color = i;
	}
	std::string error;
	static std::vector<MapCacheEntry> cache;
	if (cache.empty())
	{
		REQUIRE_MESSAGE(IniSkirmishSetupSource::loadMapCache(*s->mount->fs, cache, &error), error);
	}
	NewGameStart start{ RandomAlgorithm::ZH_CarryChain };
	REQUIRE_MESSAGE(NewGame::prepareNewGame(m, s->world->playerTemplates(), s->settings, cache, RandomAlgorithm::ZH_CarryChain, start, &error), error);
	ArchiveW3DFileSource source(*s->mount->fs);
	WW3DAssetManager assets(source);
	LiveGame game(*s->world, *s->mount->fs, assets, s->options);
	LiveGame::Options o;
	o.start = &start;
	REQUIRE_MESSAGE(game.load(o, &error), error);
	for (int i = 0; i < 2; ++i)
	{
		game.advance(0.2);
	}
	GameLogic &logic = game.logic();
	Object *wall = nullptr;
	int count = 0;
	for (Object *obj = logic.getFirstObject(); obj; obj = obj->getNextObject())
	{
		if (obj->getTemplate()->getName() == "MinisWallAUpgrade")
		{
			wall = wall ? wall : obj;
			++count;
		}
	}
	MESSAGE("MinisWallAUpgrade objects: " << count);
	REQUIRE(wall);
	REQUIRE(wall->isKindOfName("WALL_UPGRADE"));
	REQUIRE(wall->getAIUpdateInterface());
	REQUIRE(DynamicPortalBehaviour::of(*wall));
	const CastleMemberBehavior *member = dynamic_cast<const CastleMemberBehavior *>(wall->findModule("CastleMemberBehavior"));
	REQUIRE(member);
	const int unselectable = ObjectTemplateInfoBuilder::objectStatusIndex("UNSELECTABLE");
	REQUIRE(unselectable >= 0); // tolerance: an index of the status table (-1: missing)
	const ObjectID before = member->occupantId();
	CHECK(before == INVALID_ID);
	REQUIRE(PlayerCommands::isSelectable(*wall)); // KindOf SELECTABLE, alive, no UNSELECTABLE
	const UpgradeTemplate *u = TheUpgradeCenter ? TheUpgradeCenter->findUpgrade("Upgrade_PosternGate") : nullptr;
	REQUIRE(u);
	wall->giveUpgrade(u);
	REQUIRE(DynamicPortalBehaviour::of(*wall)->created());
	CHECK(member->occupantId() == wall->getID());
	CHECK_FALSE(wall->testStatus((unsigned)unselectable));
	CHECK(PlayerCommands::isSelectable(*wall)); // RW 0x85830B: a WALL_UPGRADE foundation stays selectable
	int s4855 = 0;
	for (const auto &st : logic.notedStopHits())
	{
		s4855 += st.first.rfind("[S-4855]", 0) == 0 ? 1 : 0;
	}
	CHECK(s4855 == 0);
	for (int i = 0; i < 10; ++i)
	{
		game.advance(0.2);
	}
	CHECK(member->occupantId() == wall->getID()); // nothing clears it while the gate stands (the plot's update sleeps: no path here wakes it)
}
