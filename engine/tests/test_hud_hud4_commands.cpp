// OpenBFME retail tests for lane HUD-4: every command reachable (the control bar's retail placement, QA-1 U0) and the hotkeys (QA-1 U1). They share the HUD
// tests' retail world (the file name sorts with the test_hud_* files) and SKIP when ROTWK_INSTALL / BFME2_INSTALL are unset.

#include "doctest.h"
#include "HudTestUtil.h"

#include "GameClient/ControlBar.h"
#include "GameClient/DrawableManager.h"
#include "GameClient/GUI/AptScreens/AptPalantir.h"
#include "GameClient/GUI/ShellServices.h"
#include "GameClient/InGameHud.h"
#include "GameClient/MessageStream/MetaEvent.h"
#include "GameClient/Radar.h"
#include "GameLogic/Module/StancesBehavior.h"
#include "GameLogic/WeaponSetToggle.h"

#include <cmath>
#include <cstdio>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

using namespace hudtest;

namespace
{
struct HudRig
{
	Rig rig;
	RecordingShellServices services;
	std::unique_ptr<InGameHud> hud;
	explicit HudRig(SharedWorld &s) : rig(s)
	{
		InGameHud::Config cfg{ *rig.game, *s.world, *s.mount->fs, services, rig.view, s.mouse, s.meta, nullptr };
		hud = std::make_unique<InGameHud>(cfg);
		std::string error;
		REQUIRE_MESSAGE(hud->boot(&error), error);
		hud->setWindowSize(1024, 768);
		rig.view.setScreen(1024, 768);
	}
	~HudRig() { hud.reset(); }
	void frames(int n)
	{
		for (int i = 0; i < n; ++i)
		{
			hud->update(0.033);
			rig.game->advance(0.033);
		}
	}
	// the selection of the HUD and of the logic (the group message the selection translator would send)
	void select(Object *o)
	{
		hud->input().ui().deselectAll();
		frames(2);
		hud->input().ui().selectObject(o->getID());
		GameMessage m(MSG_CREATE_SELECTED_GROUP, rig.local->getPlayerIndex());
		m.appendBooleanArgument(true);
		m.appendObjectIDArgument(o->getID());
		rig.game->commands().append(m); // as InGameHud::send does
		frames(20);
	}
	void press(const MetaMapRec &rec)
	{
		int mods = 0;
		mods |= (rec.modState & MOD_CTRL) ? KEY_STATE_LCONTROL : 0;
		mods |= (rec.modState & MOD_SHIFT) ? KEY_STATE_LSHIFT : 0;
		mods |= (rec.modState & MOD_ALT) ? KEY_STATE_LALT : 0;
		// the modifier's own key comes first, as on a keyboard (a modifier-only CommandMap record takes the change of the modifier state)
		const int modKey = (rec.modState & MOD_CTRL) ? KEY_LCTRL : ((rec.modState & MOD_SHIFT) ? KEY_LSHIFT : ((rec.modState & MOD_ALT) ? KEY_LALT : KEY_NONE));
		if (modKey != KEY_NONE)
		{
			hud->key(modKey, KEY_STATE_DOWN | mods);
			frames(2);
		}
		hud->key(rec.key, KEY_STATE_DOWN | mods);
		frames(2);
		hud->key(rec.key, KEY_STATE_UP | mods);
		frames(2);
		if (modKey != KEY_NONE)
		{
			hud->key(modKey, KEY_STATE_UP);
		}
		frames(4);
	}
	const ControlBarButton *arcButton(int command)
	{
		for (const ControlBarButton &b : hud->controlBar().palantirButtons())
		{
			if (b.button && b.button->m_command == command)
			{
				return &b;
			}
		}
		return nullptr;
	}
};

bool flag24(const Object &o)
{
	return ((o.getWeaponSetFlags()[0] >> WeaponSetToggle::WEAPONSET_TOGGLE_1) & 1u) != 0;
}
} // namespace

// QA-1 U0: the retail placement over every command set of the retail data. Window i shows slot i (the initial range), the arc windows 0 .. 5 that are
// InPalantir, the side bar every Radial button; what neither shows is off the bar in retail too, and every such button's command is reached another way
// (Attack-Move / Stop / the stances by hotkey, the science and spell book buttons by their own interfaces, the others by the context command or not at all).
TEST_CASE("hud4 placement: every retail command set's visible buttons are placed as RW 0x92FF5C / 0x92F082 place them, counted")
{
	if (!haveWorld("hud4 placement"))
	{
		return;
	}
	const CommandStore &store = shared().world->commands();
	size_t sets = 0, arc = 0, side = 0, both = 0, off = 0, setsBeyondSix = 0, setsWithOff = 0;
	std::map<std::string, size_t> offByCommand;
	for (const std::string &name : store.setNames())
	{
		const CommandSet *set = store.findCommandSet(name);
		REQUIRE(set);
		++sets;
		int inPalantir = 0;
		bool anyOff = false;
		for (int slot = 0; slot < std::min(set->m_initialVisible, (int)CommandSet::MAX_BUTTONS); ++slot)
		{
			const CommandButton *b = set->getCommandButton(slot);
			if (!b || !b->m_showButton)
			{
				continue;
			}
			inPalantir += b->m_inPalantir ? 1 : 0;
			const ControlBar::Placement p = ControlBar::placementOf(*b, slot, 0);
			CHECK(p.window == slot);
			arc += p.arc ? 1 : 0;
			side += p.side ? 1 : 0;
			both += p.arc && p.side ? 1 : 0;
			if (!p.arc && !p.side)
			{
				++off;
				anyOff = true;
				++offByCommand[GUICommandName(b->m_command)];
			}
		}
		setsBeyondSix += inPalantir > ControlBar::kPalantirWindows ? 1 : 0;
		setsWithOff += anyOff ? 1 : 0;
	}
	std::printf("hud4 placement: %zu sets, %zu arc, %zu side (%zu both), %zu off the bar in %zu sets; %zu sets have more than six InPalantir buttons\n", sets, arc, side,
				both, off, setsWithOff, setsBeyondSix);
	for (const auto &kv : offByCommand)
	{
		std::printf("  off the bar: %s x%zu\n", kv.first.c_str(), kv.second);
	}
	// the retail 2.01 data (an independent count of the INI files gives the same numbers)
	CHECK(sets == 674);
	CHECK(arc == 1980);
	CHECK(side == 1267);
	CHECK(off == 1874);
	CHECK(setsWithOff == 319);
	// what is off the bar is the kind retail reaches without a button: the hotkeys STOP (S), TOGGLE_ATTACKMOVE (A), STANCE_* (D / F / G); the science and spell
	// book purchases (their own interfaces); the special powers past the arc (the context command: Command_CaptureBuilding at slot 12 of every horde)
	const std::set<std::string> reachable = { "SET_STANCE", "STOP", "ATTACK_MOVE", "SPECIAL_POWER", "PURCHASE_SCIENCE", "SPELL_BOOK", "EVACUATE", "FOUNDATION_CONSTRUCT",
		"OBJECT_UPGRADE", "FIRE_WEAPON", "HORDE_TOGGLE_FORMATION" };
	for (const auto &kv : offByCommand)
	{
		CHECK_MESSAGE(reachable.count(kv.first) == 1, kv.first);
	}
	CHECK(offByCommand["SET_STANCE"] == 813);
	CHECK(offByCommand["STOP"] == 293);
	CHECK(offByCommand["ATTACK_MOVE"] == 275);
}

// QA-1 U0's example: the Dwarven Forge Works (DwarvenForgeWorksCommandSet: slots 1 .. 6 build / technology / level / sell, 7 .. 9 the three technologies). The arc
// shows windows 0 .. 5, the side bar every Radial button: the technologies Forged Blades / Mithril Mail / Siege Hammer are on the side bar.
TEST_CASE("hud4 placement: the Dwarven Forge Works' Forged Blades / Mithril Mail / Siege Hammer are reachable on the side bar (RW 0x92F082)")
{
	if (!haveWorld("hud4 forge works"))
	{
		return;
	}
	SharedWorld &s = shared();
	HudRig h(s);
	h.rig.local->depositMoney(100000);
	const Coord3D c = h.rig.freeSpot(2800, 1400, 200.0f);
	Object *forge = h.rig.make("DwarvenSiegeWorks", c.x, c.y); // the Forge Works (CommandSet DwarvenForgeWorksCommandSet)
	h.rig.lookAt({ c.x, c.y, 0 });
	h.frames(20);
	h.select(forge);
	const ControlBar &bar = h.hud->controlBar();
	REQUIRE(bar.commandSetName() == "DwarvenForgeWorksCommandSet");
	std::set<std::string> sideNames;
	for (const ControlBarButton &b : bar.sideButtons())
	{
		sideNames.insert(b.button->m_name);
	}
	for (const char *tech : { "Command_PurchaseTechnologyDwarvenForgedBlades", "Command_PurchaseTechnologyDwarvenMithrilMail", "Command_PurchaseTechnologyDwarvenSiegeHammer" })
	{
		CHECK_MESSAGE(sideNames.count(tech) == 1, tech);
	}
	for (const ControlBarButton &b : bar.palantirButtons())
	{
		CHECK(b.position == b.slot); // the initial range starts at 0
		CHECK(b.slot < 6);
	}
	for (size_t i = 0; i < bar.sideButtons().size(); ++i)
	{
		CHECK(bar.sideButtons()[i].position == (int)i);
	}
	h.frames(30);
	for (const std::string &e : h.hud->palantir()->callErrors())
	{
		CHECK_MESSAGE(e.find("overflow") == std::string::npos, e);
	}
	// a technology button of the side bar presses through to the logic (the side bar's press path)
	const ControlBarButton *tech = nullptr;
	for (const ControlBarButton &b : bar.sideButtons())
	{
		tech = b.button->m_name == "Command_PurchaseTechnologyDwarvenForgedBlades" ? &b : tech;
	}
	REQUIRE(tech);
	CHECK(tech->state != ButtonState::Hidden);
}

// The weapon set toggle through the control bar: the Rohirrim's arc button (Command_ToggleRohirrimWeapon, window 1) sends MSG_WEAPONSET_TOGGLE with the horde's id
// (RW 0x9412C8); the logic toggles WEAPONSET_TOGGLE_1 (RW 0x77B529); the button then shows its second image (UCCommon_RohirrimPike) and the horde its bow
// command set.
TEST_CASE("hud4 toggle: the Rohirrim's toggle button switches the horde's weapon set through the message path (RW 0x9412C8 -> 0x77B529)")
{
	if (!haveWorld("hud4 toggle button"))
	{
		return;
	}
	SharedWorld &s = shared();
	HudRig h(s);
	const Coord3D c = h.rig.freeSpot(2400, 1800, 260.0f);
	Object *horde = h.rig.make("RohanRohirrimHorde", c.x, c.y);
	h.rig.lookAt({ c.x, c.y, 0 });
	h.frames(30);
	h.select(horde);
	const ControlBarButton *toggle = h.arcButton(GUI_COMMAND_TOGGLE_WEAPONSET);
	REQUIRE(toggle);
	CHECK(toggle->position == 1);
	CHECK(toggle->state == ButtonState::Enabled);
	CHECK(toggle->image == "UCCommon_RohirrimBow");
	CHECK_FALSE(flag24(*horde));
	const std::uint32_t before = h.rig.game->logic().computeStateHash();
	REQUIRE(h.hud->controlBar().pressButton(toggle->slot, true));
	h.frames(20);
	CHECK(flag24(*horde));
	for (const Object *m : *horde->getContain()->getContainedItemsList())
	{
		CHECK(flag24(*m));
	}
	CHECK(h.rig.game->logic().computeStateHash() != before);
	CHECK(horde->getCommandSetName() == "RohirrimHordeBowCommandSet");
	toggle = h.arcButton(GUI_COMMAND_TOGGLE_WEAPONSET);
	REQUIRE(toggle);
	CHECK(toggle->image == "UCCommon_RohirrimPike");
	REQUIRE(h.hud->controlBar().pressButton(toggle->slot, true));
	h.frames(20);
	CHECK_FALSE(flag24(*horde));
	CHECK(horde->getCommandSetName() == "RohirrimHordeCommandSet");
}

// QA-1 U1: Ctrl+H (SELECT_HERO, RW 0x81FDA2) selects the local player's next hero and looks at it; with two heroes a second press selects the other one.
TEST_CASE("hud4 hotkeys: Ctrl+H selects the next hero and looks at it (RW 0x81FDA2 / 0x81DABC)")
{
	if (!haveWorld("hud4 select hero"))
	{
		return;
	}
	SharedWorld &s = shared();
	HudRig h(s);
	const MetaMapRec *rec = s.meta.find(CMSG_META_SELECT_HERO);
	REQUIRE(rec != nullptr);
	CHECK((rec->modState & MOD_CTRL) != 0);
	CHECK(rec->key == KEY_H);
	const Coord3D c1 = h.rig.freeSpot(1800, 1400, 200.0f);
	const Coord3D c2 = h.rig.freeSpot(3200, 2600, 200.0f);
	Object *first = h.rig.make("GondorBoromir", c1.x, c1.y);
	Object *second = h.rig.make("GondorAragorn", c2.x, c2.y);
	h.rig.lookAt({ 2500, 2000, 0 });
	h.frames(10);
	h.press(*rec);
	REQUIRE(h.hud->input().ui().selected().size() == 1);
	const ObjectID a = h.hud->input().ui().selected().front();
	CHECK((a == first->getID() || a == second->getID()));
	const Object *picked = h.rig.game->logic().findObjectByID(a);
	CHECK(std::fabs(h.rig.view.position().x - picked->getPosition()->x) < 1.0f);
	h.press(*rec);
	REQUIRE(h.hud->input().ui().selected().size() == 1);
	const ObjectID b = h.hud->input().ui().selected().front();
	CHECK(b != a);
	h.press(*rec);
	CHECK(h.hud->input().ui().selected().front() == a); // the walk wraps around
	CHECK(h.hud->input().commandTranslator().unportedMeta().count("SELECT_HERO") == 0);
}

// QA-1 U1 (the stance keys RW 0x8201F5 .. 0x820247): D / F / G send MSG_CHANGE_STANCE 2 / 1 / 3 for the selection; Shift+Up / Shift+Down have no handler in RotWK
TEST_CASE("hud4 hotkeys: the stance keys change a horde's stance; Shift+Up / Shift+Down do nothing, as in RotWK (S-1673)")
{
	if (!haveWorld("hud4 stance keys"))
	{
		return;
	}
	SharedWorld &s = shared();
	HudRig h(s);
	const Coord3D c = h.rig.freeSpot(2400, 1800, 260.0f);
	Object *horde = h.rig.make("GondorFighterHorde", c.x, c.y);
	h.rig.lookAt({ c.x, c.y, 0 });
	h.frames(30);
	h.select(horde);
	StancesBehavior *st = StancesBehavior::of(*horde);
	REQUIRE(st);
	CHECK(st->getStance() == STANCE_BATTLE);
	const MetaMapRec *aggressive = s.meta.find(CMSG_META_STANCE_AGGRESSIVE);
	const MetaMapRec *hold = s.meta.find(CMSG_META_STANCE_HOLDGROUND);
	const MetaMapRec *battle = s.meta.find(CMSG_META_STANCE_BATTLE);
	REQUIRE((aggressive && hold && battle));
	CHECK(aggressive->key == KEY_D);
	CHECK(battle->key == KEY_F);
	CHECK(hold->key == KEY_G);
	h.press(*aggressive);
	h.frames(10);
	CHECK(st->getStance() == STANCE_AGGRESSIVE);
	h.press(*hold);
	h.frames(10);
	CHECK(st->getStance() == STANCE_HOLD_GROUND);
	h.press(*battle);
	h.frames(10);
	CHECK(st->getStance() == STANCE_BATTLE);
	const MetaMapRec *next = s.meta.find(CMSG_META_SELECT_NEXT_WORKER);
	const MetaMapRec *prev = s.meta.find(CMSG_META_SELECT_PREV_WORKER);
	REQUIRE((next && prev));
	CHECK(next->key == KEY_UP);
	CHECK((next->modState & MOD_SHIFT) != 0);
	const std::vector<ObjectID> sel = h.hud->input().ui().selected();
	h.press(*next);
	h.press(*prev);
	CHECK(h.hud->input().ui().selected() == sel);
	CHECK(h.hud->input().commandTranslator().retailNoOpMeta().at("SELECT_NEXT_WORKER") == 1);
	CHECK(h.hud->input().commandTranslator().retailNoOpMeta().at("SELECT_PREV_WORKER") == 1);
}

// QA-1 U1: Space (VIEW_LAST_RADAR_EVENT, RW 0x81FE7B -> TheRadar RW 0x5DCB28): no event, nothing; with events the view looks at the first one, and with the
// window 0 (S-1674) a later press starts again from the first
TEST_CASE("hud4 hotkeys: Space looks at the radar's event (RW 0x5DCB28)")
{
	if (!haveWorld("hud4 radar event"))
	{
		return;
	}
	SharedWorld &s = shared();
	HudRig h(s);
	const MetaMapRec *rec = s.meta.find(CMSG_META_VIEW_LAST_RADAR_EVENT);
	REQUIRE(rec != nullptr);
	CHECK(rec->key == KEY_SPACE);
	h.rig.lookAt({ 2000, 2000, 0 });
	h.frames(4);
	const Coord3D start = h.rig.view.position();
	h.press(*rec);
	CHECK(std::fabs(h.rig.view.position().x - start.x) < 0.01f); // no event: nothing
	h.hud->radar().addEvent(Coord3D{ 1500.0f, 1700.0f, 0.0f });
	h.hud->radar().addEvent(Coord3D{ 2600.0f, 1900.0f, 0.0f });
	h.press(*rec);
	CHECK(std::fabs(h.rig.view.position().x - 1500.0f) < 1.0f);
	CHECK(h.hud->radar().eventCursor() == 0);
	h.press(*rec);
	CHECK(h.hud->radar().eventCursor() == 0); // the window (TheRadar + 0x90) is 0 here: a later press starts from the first event
	CHECK(h.hud->input().commandTranslator().unportedMeta().count("VIEW_LAST_RADAR_EVENT") == 0);
}

TEST_CASE("hud4 radar: the event cursor walks the list while the window lasts (RW 0x5DCB7F) and wraps at its end")
{
	if (!haveWorld("hud4 radar cursor"))
	{
		return;
	}
	SharedWorld &s = shared();
	HudRig h(s);
	Radar &r = h.hud->radar();
	r.addEvent(Coord3D{ 100.0f, 100.0f, 0.0f });
	r.addEvent(Coord3D{ 200.0f, 100.0f, 0.0f });
	r.setClientFrame(1000);
	r.tryJumpToNextEvent(h.rig.view);
	CHECK(r.eventCursor() == 0);
	r.tryJumpToNextEvent(h.rig.view); // the same frame: within the window 0
	CHECK(r.eventCursor() == 1);
	r.tryJumpToNextEvent(h.rig.view); // past the end: the first again
	CHECK(r.eventCursor() == 0);
	r.setClientFrame(1001);
	r.tryJumpToNextEvent(h.rig.view);
	CHECK(r.eventCursor() == 0);
}

// QA-1 U19: the Palantir's engine calls wait for the frames the movie reports (PalantirCommandUI::OnButtonFrameLoaded RW 0x9300F8, the side bar's RW 0x92EE8B,
// the side bar movie's Loaded): selecting, switching and clearing the selection from the first frames on makes no call that fails on timing
TEST_CASE("hud4 palantir: no engine call reaches a clip before the movie reported its frame (RW 0x930088 / 0x92EE8B / 0x92FF5C)")
{
	if (!haveWorld("hud4 palantir timing"))
	{
		return;
	}
	SharedWorld &s = shared();
	HudRig h(s);
	const Coord3D c = h.rig.freeSpot(2800, 1400, 300.0f);
	Object *barracks = h.rig.make("GondorBarracks", c.x, c.y);
	Object *horde = h.rig.make("GondorFighterHorde", c.x + 250.0f, c.y);
	h.rig.lookAt({ c.x, c.y, 0 });
	// a selection before the movie's first frames ran
	h.hud->input().ui().selectObject(barracks->getID());
	h.frames(30);
	h.select(horde);
	h.frames(10);
	h.select(barracks);
	h.frames(10);
	h.hud->input().ui().deselectAll();
	h.frames(30);
	REQUIRE(h.hud->palantir() != nullptr);
	for (const std::string &e : h.hud->palantir()->callErrors())
	{
		CHECK_MESSAGE(e.find("is not a function") == std::string::npos, e);
		CHECK_MESSAGE(e.find("does not resolve") == std::string::npos, e);
	}
	CHECK(h.hud->palantir()->buttonFrames().size() >= 6); // the arc's six frames and the side bar's reported themselves
	// QA-1 U18: InitialSetup's extern.MinLOD is answered as RotWK's host answers a name it has no handler for (RW 0x623B47 / 0x623AD3), noted
	CHECK(h.hud->windows().noteCount("extern-retail-unhandled") >= 1);
}
