// OpenBFME retail tests (lane QA-1): the HUD picks what is drawn (GameClient/DrawablePick). Found by the scripted player of the QA runs: a click on the Men
// fortress selected its shell (MenFortress, Model = None: no command set, so no Porter, heroes or upgrades could be reached), and a click on a barracks
// selected the arrow tower in front of it (the geometry cylinders of S-284: GondorBarracks is a cylinder of radius 8 and height 10 under a building about
// 60 across). SKIP when ROTWK_INSTALL / BFME2_INSTALL are unset. GPL-3.0.

#include "CreepTestUtil.h"
#include "HudTestUtil.h"

#include "GameClient/DrawablePick.h"
#include "GameClient/DrawableManager.h"
#include "GameClient/GUI/ShellServices.h"
#include "GameLogic/Module/ProductionUpdate.h"
#include "GameClient/InGameHud.h"
#include "GameClient/GUI/AptScreens/AptPalantir.h"
#include "GameClient/LogicSnapshot.h"
#include "GameClient/MessageStream/MetaEvent.h"

#include <cmath>

#include <algorithm>
#include <set>
#include "GameLogic/Module/ActiveBody.h"
#include "GameLogic/Module/CastleModules.h"
#include "GameLogic/Module/DozerAIUpdate.h"

using namespace hudtest;

namespace
{
struct PickRig
{
	Rig rig;
	RecordingShellServices services;
	std::unique_ptr<InGameHud> hud;
	explicit PickRig(SharedWorld &s) : rig(s)
	{
		InGameHud::Config cfg{ *rig.game, *s.world, *s.mount->fs, services, rig.view, s.mouse, s.meta, nullptr };
		hud = std::make_unique<InGameHud>(cfg);
		std::string error;
		REQUIRE_MESSAGE(hud->boot(&error), error);
		hud->setWindowSize(1024, 768);
		rig.view.setScreen(1024, 768);
	}
	~PickRig() { hud.reset(); }
	void frames(int n)
	{
		for (int i = 0; i < n; ++i)
		{
			hud->update(0.033);
			rig.game->advance(0.033);
		}
	}
	void click(const Coord3D &world, int timeMs)
	{
		const ICoord2D px = rig.screenOf(world);
		hud->mouseMove(px.x, px.y);
		hud->mouseButton(HudInput::Button::Left, true, px.x, px.y, 0, timeMs);
		hud->mouseButton(HudInput::Button::Left, false, px.x, px.y, 0, timeMs + 40);
		frames(10);
	}
	void deselect(int timeMs)
	{
		const ICoord2D px = rig.screenOf(Coord3D{ rig.view.position().x, rig.view.position().y, 0.0f });
		hud->mouseButton(HudInput::Button::Right, true, px.x, px.y, 0, timeMs);
		hud->mouseButton(HudInput::Button::Right, false, px.x, px.y, 0, timeMs + 40);
		frames(4);
	}
};
} // namespace

TEST_CASE("qa1 pick: a click on a barracks' body above its GEOMETRY cylinder selects it (the ray meets the drawn model, ZH W3DView::pickDrawable)")
{
	if (!haveWorld("qa1 pick"))
	{
		return;
	}
	SharedWorld &s = shared();
	PickRig h(s);
	const Coord3D c = h.rig.freeSpot(2800, 1400, 200.0f);
	Object *b = h.rig.make("GondorBarracks", c.x, c.y);
	h.rig.lookAt({ c.x, c.y, 0 });
	h.frames(20);
	const Drawable *d = h.rig.game->drawables().findByObject(b->getID());
	REQUIRE(d != nullptr);
	// the geometry is a cylinder of radius 8 and height 10: 30 above the centre and 20 to the side is the building, not its geometry
	for (const Coord3D &p : { Coord3D{ c.x, c.y, 30.0f }, Coord3D{ c.x + 20.0f, c.y, 15.0f }, Coord3D{ c.x - 20.0f, c.y, 15.0f } })
	{
		h.deselect(500);
		h.click(p, 1000);
		CHECK_MESSAGE(h.hud->input().ui().selected() == std::vector<ObjectID>{ b->getID() }, "a click on the barracks model at (", p.x - c.x, ", ", p.z, ")");
	}
	CHECK(DrawablePick::cachedModels() >= 1);
	// the selected barracks filled the Palantir's buttons: their CreateContent wrote "<clip>_ContentName" into the provider RW 0x9D26D2 registers for the call,
	// not into an unregistered extern
	h.frames(30);
	REQUIRE_FALSE(h.hud->controlBar().palantirButtons().empty());
	size_t contentErrors = 0;
	for (const std::string &e : h.hud->windows().errors())
	{
		contentErrors += e.find("_ContentName") != std::string::npos ? 1 : 0;
	}
	CHECK(contentErrors == 0);
	// the stop the pick reports (S-1200): what is not read from the target
	const std::vector<std::string> stops = h.hud->stops();
	CHECK(std::find(stops.begin(), stops.end(), std::string(DrawablePick::stopLine())) != stops.end());
	CHECK(std::string(DrawablePick::stopLine()).rfind("[S-1200] picking (InGameHud): the ray meets the triangles of the drawn W3D models", 0) == 0);
	// far off the model nothing is picked: the click on open ground drops nothing into the selection
	h.deselect(3000);
	h.click(Coord3D{ c.x + 150.0f, c.y + 150.0f, 0.0f }, 4000);
	CHECK(h.hud->input().ui().selected().empty());
}

TEST_CASE("qa1 pick: a click on the Men fortress selects its citadel, not the castle shell that draws nothing; the bar shows MenFortressCommandSet")
{
	if (!haveWorld("qa1 pick fortress"))
	{
		return;
	}
	SharedWorld &s = shared();
	PickRig h(s);
	const Coord3D c = h.rig.freeSpot(2600, 2600, 300.0f);
	Object *shell = h.rig.make("MenFortress", c.x, c.y);
	CastleBehavior *castle = dynamic_cast<CastleBehavior *>(shell->findModule("CastleBehavior"));
	REQUIRE(castle != nullptr);
	REQUIRE_MESSAGE(castle->unpack(), castle->lastError()); // what the starting base does with the player's fortress (BUILD-1 / CASTLE-1)
	h.frames(30);
	Object *keep = h.rig.logic().findObjectByID(castle->keepId());
	REQUIRE(keep != nullptr);
	CHECK(keep->getTemplate()->getName() == "MenFortressCitadel");
	// both are SELECTABLE and stand on one spot; only the keep draws a model
	CHECK(shell->isKindOfName("SELECTABLE"));
	h.rig.lookAt({ c.x, c.y, 0 });
	h.frames(10);
	const Drawable *sd = h.rig.game->drawables().findByObject(shell->getID());
	REQUIRE(sd != nullptr);
	float t = 0.0f;
	const Coord3D origin{ c.x, c.y - 420.0f, 520.0f };
	const Coord3D dir{ 0.0f, 420.0f, -500.0f };
	CHECK(DrawablePick::rayTest(*sd, origin, dir, &t) == DrawablePick::Result::NotDrawn);
	h.click(Coord3D{ c.x, c.y, 20.0f }, 1000);
	REQUIRE(h.hud->input().ui().selected() == std::vector<ObjectID>{ keep->getID() });
	CHECK(h.hud->controlBar().commandSetName() == "MenFortressCommandSet");
	bool porter = false;
	for (const ControlBarButton &cb : h.hud->controlBar().palantirButtons())
	{
		porter = porter || (cb.button && cb.button->m_name == "Command_ConstructMenPorter");
	}
	CHECK(porter);
}

TEST_CASE("qa1 pick: a click on a soldier of a horde selects the horde (the members are drawn; the horde draws nothing itself)")
{
	if (!haveWorld("qa1 pick horde"))
	{
		return;
	}
	SharedWorld &s = shared();
	PickRig h(s);
	const Coord3D c = h.rig.freeSpot(2400, 1800, 200.0f);
	Object *horde = h.rig.make("GondorFighterHorde", c.x, c.y);
	h.rig.lookAt({ c.x, c.y, 0 });
	h.frames(60);
	const ContainModuleInterface *contain = horde->getContain();
	REQUIRE(contain != nullptr);
	REQUIRE(contain->getContainedItemsList() != nullptr);
	REQUIRE_FALSE(contain->getContainedItemsList()->empty());
	const Object *member = contain->getContainedItemsList()->front();
	const Drawable *md = h.rig.game->drawables().findByObject(member->getID());
	REQUIRE(md != nullptr);
	const Coord3D at = *md->getPosition();
	{
		// the ray meets the soldier's drawn model
		Coord3D o, d;
		REQUIRE(h.rig.view.screenToRay(h.rig.screenOf(Coord3D{ at.x, at.y, at.z + 8.0f }), o, d));
		float t = 0.0f;
		CHECK(DrawablePick::rayTest(*md, o, d, &t) == DrawablePick::Result::Hit);
	}
	h.click(Coord3D{ at.x, at.y, at.z + 8.0f }, 1000);
	CHECK(h.hud->input().ui().selected() == std::vector<ObjectID>{ horde->getID() });
}

TEST_CASE("qa1 pick: an enemy building in the shroud is not picked: the click moves the army there instead of an attack the logic refuses (S-565)")
{
	if (!haveWorld("qa1 pick shroud"))
	{
		return;
	}
	SharedWorld &s = shared();
	PickRig h(s);
	Player *enemy = h.rig.game->players().findPlayerWithName("Player_2");
	REQUIRE(enemy != nullptr);
	creeptest::removeCreeps(h.rig.game->logic()); // lane MOD-4: the creeps near the map's middle would attack the horde
	const Coord3D c = h.rig.freeSpot(2400, 1800, 200.0f);
	Object *horde = h.rig.make("GondorFighterHorde", c.x, c.y);
	const Coord3D far = h.rig.freeSpot(c.x + 1500.0f, c.y + 1500.0f, 200.0f);
	Object *tower = h.rig.make("GondorBarracks", far.x, far.y, enemy);
	h.rig.game->shroud().setDisplayed(true); // the game shows the shroud (a skirmish map with explored cells: GameWorld::start_new_game)
	h.rig.lookAt({ c.x, c.y, 0 });
	h.frames(30);
	const std::shared_ptr<const LogicSnapshot> snap = h.rig.game->presentedSnapshot();
	REQUIRE(snap.get() != nullptr);
	const ObjectSnapshot *rec = snap->find(tower->getID());
	REQUIRE(rec != nullptr);
	REQUIRE_MESSAGE(rec->shroudedForLocal, "the enemy building far from every unit of the local player is SHROUDED for it");
	// select the horde by a click on one of its soldiers
	const Object *member = horde->getContain()->getContainedItemsList()->front();
	h.click(*h.rig.game->drawables().findByObject(member->getID())->getPosition(), 1000);
	REQUIRE(h.hud->input().ui().selected() == std::vector<ObjectID>{ horde->getID() });
	// the camera goes to the building; a click on where it stands
	h.rig.lookAt({ far.x, far.y, 0 });
	h.frames(4);
	const size_t before = h.hud->input().messageLog().size();
	h.click(Coord3D{ far.x, far.y, 20.0f }, 3000);
	const std::vector<std::string> &log = h.hud->input().messageLog();
	REQUIRE(log.size() > before);
	bool attack = false, move = false;
	for (size_t i = before; i < log.size(); ++i)
	{
		attack = attack || log[i].rfind("MSG_DO_ATTACK_OBJECT", 0) == 0;
		move = move || log[i].rfind("MSG_DO_MOVETO", 0) == 0 || log[i].rfind("MSG_DO_ATTACKMOVETO", 0) == 0;
	}
	CHECK_FALSE(attack);
	CHECK(move);
}

TEST_CASE("qa1 context: a click with the Porter on our abandoned site resumes it (MSG_RESUME_CONSTRUCTION) and it completes; on a damaged building it repairs (MSG_DO_REPAIR) (S-1201)")
{
	if (!haveWorld("qa1 context"))
	{
		return;
	}
	SharedWorld &s = shared();
	PickRig h(s);
	h.rig.local->getMoney()->deposit(50000, false);
	const Coord3D c = h.rig.freeSpot(2800, 1400, 260.0f);
	Object *porter = h.rig.make("MenPorter", c.x - 120.0f, c.y);
	DozerAIUpdate *dozer = dynamic_cast<DozerAIUpdate *>(porter->getAIUpdateInterface());
	REQUIRE(dozer != nullptr);
	const ThingTemplate *tt = h.rig.logic().things().findTemplate("GondorBarracks")->getFinalOverride();
	Coord3D site{ c.x, c.y, h.rig.logic().getGroundHeight(c.x, c.y) };
	Object *barracks = dozer->construct(*tt, site, 0.0f, *h.rig.local);
	REQUIRE(barracks != nullptr);
	for (int i = 0; i < 300 && barracks->getConstructionPercent() < 10.0f; ++i)
	{
		h.frames(1);
	}
	REQUIRE(barracks->getConstructionPercent() >= 10.0f);
	// the builder is given another building to put up (what the scripted player did): the first site stops rising
	const ThingTemplate *farm = h.rig.logic().things().findTemplate("GondorFarm")->getFinalOverride();
	const Coord3D other = h.rig.freeSpot(c.x - 400.0f, c.y - 300.0f, 120.0f);
	REQUIRE(dozer->construct(*farm, Coord3D{ other.x, other.y, h.rig.logic().getGroundHeight(other.x, other.y) }, 0.0f, *h.rig.local) != nullptr);
	h.frames(60);
	const float stalled = barracks->getConstructionPercent();
	h.frames(60);
	REQUIRE(barracks->getConstructionPercent() == stalled);
	REQUIRE(barracks->isUnderConstruction());
	// the player selects the Porter and clicks the site
	h.rig.lookAt({ porter->getPosition()->x, porter->getPosition()->y, 0 });
	h.frames(4);
	h.click(*h.rig.game->drawables().findByObject(porter->getID())->getPosition(), 1000);
	REQUIRE(h.hud->input().ui().selected() == std::vector<ObjectID>{ porter->getID() });
	h.rig.lookAt({ c.x, c.y, 0 });
	h.frames(4);
	size_t before = h.hud->input().messageLog().size();
	h.click(Coord3D{ c.x, c.y, 15.0f }, 3000);
	bool resume = false;
	for (size_t i = before; i < h.hud->input().messageLog().size(); ++i)
	{
		resume = resume || h.hud->input().messageLog()[i].rfind("MSG_RESUME_CONSTRUCTION", 0) == 0;
	}
	REQUIRE(resume);
	for (int i = 0; i < 6000 && barracks->isUnderConstruction(); ++i)
	{
		h.frames(1);
	}
	CHECK_FALSE(barracks->isUnderConstruction());
	// damaged: the same click repairs
	ActiveBody *body = dynamic_cast<ActiveBody *>(barracks->getBodyModule());
	REQUIRE(body != nullptr);
	body->internalChangeHealth(-0.5f * body->getMaxHealth());
	const float hurt = body->getHealth();
	REQUIRE(hurt < body->getMaxHealth());
	h.frames(2);
	before = h.hud->input().messageLog().size();
	h.click(Coord3D{ c.x, c.y, 15.0f }, 9000);
	bool repair = false;
	for (size_t i = before; i < h.hud->input().messageLog().size(); ++i)
	{
		repair = repair || h.hud->input().messageLog()[i].rfind("MSG_DO_REPAIR", 0) == 0;
	}
	CHECK(repair);
	h.frames(300);
	CHECK(body->getHealth() > hurt);
	// the stop the HUD reports
	bool reported = false, ctrlDrag = false;
	for (const std::string &l : h.hud->stops())
	{
		reported = reported || l.rfind("[S-1201] context commands (lane QA-1)", 0) == 0;
		ctrlDrag = ctrlDrag || l.rfind("[S-3301] drag selection (lane PLAY-3)", 0) == 0; // lane PLAY-3: the Ctrl drag selection is not ported
	}
	CHECK(reported);
	CHECK(ctrlDrag);
}

TEST_CASE("qa1 hotkeys: E selects the units of the selection's kind, H looks at the home base (S-1202)")
{
	if (!haveWorld("qa1 hotkeys"))
	{
		return;
	}
	SharedWorld &s = shared();
	PickRig h(s);
	const Coord3D c = h.rig.freeSpot(2400, 1800, 260.0f);
	Object *a = h.rig.make("GondorFighterHorde", c.x - 150.0f, c.y);
	Object *b = h.rig.make("GondorFighterHorde", c.x + 150.0f, c.y);
	Object *other = h.rig.make("GondorArcherHorde", c.x, c.y + 200.0f);
	h.rig.lookAt({ c.x, c.y, 0 });
	h.frames(40);
	const Object *member = a->getContain()->getContainedItemsList()->front();
	const Coord3D feet = *h.rig.game->drawables().findByObject(member->getID())->getPosition();
	h.click(Coord3D{ feet.x, feet.y, feet.z + 8.0f }, 1000);
	REQUIRE_MESSAGE(h.hud->input().ui().selected() == std::vector<ObjectID>{ a->getID() }, "selected ", h.hud->input().ui().selected().size());
	auto press = [&](int keyCode, int mods) {
		const int down = KEY_STATE_DOWN | mods, up = KEY_STATE_UP | mods;
		h.hud->key(keyCode, down);
		h.frames(2);
		h.hud->key(keyCode, up);
		h.frames(4);
	};
	const MetaMapRec *matching = s.meta.find(CMSG_META_SELECT_MATCHING_UNITS);
	REQUIRE(matching != nullptr);
	press(matching->key, 0);
	const std::vector<ObjectID> sel = h.hud->input().ui().selected();
	CHECK(std::find(sel.begin(), sel.end(), a->getID()) != sel.end());
	CHECK(std::find(sel.begin(), sel.end(), b->getID()) != sel.end());
	CHECK(std::find(sel.begin(), sel.end(), other->getID()) == sel.end());
	// H: a building of ours far away is the home base (no command centre here: the costliest structure)
	const Coord3D far = h.rig.freeSpot(c.x + 1200.0f, c.y + 900.0f, 200.0f);
	Object *barracks = h.rig.make("GondorBarracks", far.x, far.y);
	h.frames(4);
	const MetaMapRec *home = s.meta.find(CMSG_META_VIEW_HOME_BASE);
	REQUIRE(home != nullptr);
	REQUIRE((home->modState & (MOD_CTRL | MOD_SHIFT)) == 0);
	press(home->key, 0);
	const Coord3D at = h.rig.view.position();
	CHECK(std::fabs(at.x - barracks->getPosition()->x) < 1.0f);
	CHECK(std::fabs(at.y - barracks->getPosition()->y) < 1.0f);
	CHECK(h.hud->input().commandTranslator().unportedMeta().count("VIEW_HOME_BASE") == 0);
	CHECK(h.hud->input().commandTranslator().unportedMeta().count("SELECT_MATCHING_UNITS") == 0);
}

TEST_CASE("qa1 palantir: a horde's arc shows its windows 0 .. 5, Attack-Move and Stop are off the bar as in retail (lane HUD-4, RW 0x92FF5C)")
{
	if (!haveWorld("qa1 palantir overflow"))
	{
		return;
	}
	SharedWorld &s = shared();
	PickRig h(s);
	const Coord3D c = h.rig.freeSpot(2400, 1800, 200.0f);
	Object *horde = h.rig.make("GondorFighterHorde", c.x, c.y);
	h.rig.lookAt({ c.x, c.y, 0 });
	h.frames(40);
	const Object *member = horde->getContain()->getContainedItemsList()->front();
	const Coord3D feet = *h.rig.game->drawables().findByObject(member->getID())->getPosition();
	h.click(Coord3D{ feet.x, feet.y, feet.z + 8.0f }, 1000);
	REQUIRE(h.hud->input().ui().selected() == std::vector<ObjectID>{ horde->getID() });
	h.frames(30);
	const ControlBar &bar = h.hud->controlBar();
	REQUIRE(!bar.palantirButtons().empty());
	for (const ControlBarButton &b : bar.palantirButtons())
	{
		CHECK(b.position >= 0);
		CHECK(b.position < (int)AptPalantir::kArcPositions);
	}
	std::set<std::string> off;
	for (const ControlBarButton &b : bar.offBarButtons())
	{
		off.insert(b.button->m_name);
	}
	CHECK(off.count("Command_AttackMove") == 1);
	CHECK(off.count("Command_Stop") == 1);
	REQUIRE(h.hud->palantir() != nullptr);
	for (const std::string &e : h.hud->palantir()->callErrors())
	{
		CHECK_MESSAGE(e.rfind("arc overflow: ", 0) != 0, e);
	}
}

TEST_CASE("qa1 pick: the triangle cache drops a prototype with its asset manager (address reuse must not show stale triangles, review r1)")
{
	if (!haveWorld("qa1 pick cache"))
	{
		return;
	}
	SharedWorld &s = shared();
	const size_t before = DrawablePick::cachedModels();
	size_t during = 0;
	{
		PickRig h(s);
		const Coord3D c = h.rig.freeSpot(2800, 1400, 200.0f);
		Object *b = h.rig.make("GondorBarracks", c.x, c.y);
		h.rig.lookAt({ c.x, c.y, 0 });
		h.frames(20);
		h.click(Coord3D{ c.x, c.y, 30.0f }, 1000);
		REQUIRE(h.hud->input().ui().selected() == std::vector<ObjectID>{ b->getID() });
		during = DrawablePick::cachedModels();
		CHECK(during > before);
	} // the rig's asset manager (and every prototype it made) is gone
	CHECK(DrawablePick::cachedModels() == before);
	// a second manager gets fresh triangles: the same click works again
	PickRig h2(s);
	const Coord3D c2 = h2.rig.freeSpot(2800, 1400, 200.0f);
	Object *b2 = h2.rig.make("GondorBarracks", c2.x, c2.y);
	h2.rig.lookAt({ c2.x, c2.y, 0 });
	h2.frames(20);
	h2.click(Coord3D{ c2.x, c2.y, 30.0f }, 1000);
	CHECK(h2.hud->input().ui().selected() == std::vector<ObjectID>{ b2->getID() });
	CHECK(DrawablePick::cachedModels() == during);
}
