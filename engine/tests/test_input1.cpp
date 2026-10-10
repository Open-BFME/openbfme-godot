// OpenBFME tests of lane INPUT-1: RotWK's input layer instead of Zero Hour's. The CommandMap field table (RW 0xBF0E70), the control groups of RotWK's
// SelectionTranslator (RW 0x83C29E) and the logic's MSG_ADD_TO_TEAM (RW 0x6AD722), the camera bookmarks of the LookAt translator (RW 0x83AC4A) and the
// CommandTranslator's meta cases (RW 0x81F8D8). Every key goes through HudInput::key (the MetaEventTranslator and the whole stream), as the device sends
// it. The retail tests SKIP when ROTWK_INSTALL / BFME2_INSTALL are unset. GPL-3.0.

#include "doctest.h"

#include "HudTestUtil.h"

#include "Common/INI.h"
#include "Common/INIException.h"
#include "GameClient/CameraSettings.h"
#include "GameClient/HudObjects.h"
#include "GameClient/TacticalCamera.h"
#include "GameLogic/GameLogicDispatch.h"
#include "GameLogic/GameMessage.h"
#include "GameLogic/Map/TerrainLogic.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/PlayerCommands.h"

#include <algorithm>
#include <cmath>

using namespace hudtest;

namespace
{
size_t logCount(const HudInput &in, const std::string &prefix)
{
	size_t n = 0;
	for (const std::string &l : in.messageLog())
	{
		if (l.compare(0, prefix.size(), prefix) == 0)
		{
			++n;
		}
	}
	return n;
}

std::string lastLog(const HudInput &in, size_t back = 0)
{
	const std::vector<std::string> &l = in.messageLog();
	return l.size() > back ? l[l.size() - 1 - back] : std::string();
}

Coord3D mapCenter(Rig &r)
{
	float mx = 0, my = 0;
	REQUIRE(r.logic().terrain() != nullptr);
	REQUIRE(r.logic().terrain()->getExtent(0, mx, my));
	return Coord3D{ mx * 0.5f, my * 0.5f, 0.0f };
}

bool parseCommandMap(MetaMap &map, const std::string &text, std::string *error)
{
	INIEnvironment env;
	map.registerBlocks(env);
	INI ini(env);
	try
	{
		ini.loadMemory("CommandMap.ini", std::vector<std::uint8_t>(text.begin(), text.end()), INI_LOAD_OVERWRITE);
	}
	catch (const std::exception &e)
	{
		if (error)
		{
			*error = e.what();
		}
		return false;
	}
	return true;
}
} // namespace

TEST_CASE("input1 commandmap: RotWK's field table (RW 0xBF0E70): the Modifiers values, UseableIn as a bit string with PLANNING, retail's rejections")
{
	MetaMap map;
	std::string err;
	REQUIRE_MESSAGE(parseCommandMap(map, "CommandMap ADD_TO_TEAM3\n  Key = KEY_KP3\n  Transition = DOWN\n  Modifiers = SHIFT_ALT_CTRL\n  UseableIn = GAME PLANNING\n"
										  "  Category = TEAM\n  Description = GUI:x\n  DisplayName = GUI:y\nEnd\n"
										  "CommandMap SELECT_HERO\n  Key = KEY_NONE\n  Transition = DOUBLEDOWN\n  Modifiers = CTRL_ALT\n  UseableIn = SHELL\nEnd\n",
					   &err),
		err);
	const MetaMapRec *a = map.find(CMSG_META_ADD_TO_TEAM3);
	REQUIRE(a != nullptr);
	CHECK(a->key == KEY_KP3);
	CHECK(a->modState == 0x54); // RW 0xBF0E28: SHIFT_ALT_CTRL = 0x54
	CHECK(a->usableIn == (COMMANDUSABLE_GAME | COMMANDUSABLE_PLANNING));
	CHECK(a->category == 5);
	const MetaMapRec *h = map.find(CMSG_META_SELECT_HERO);
	REQUIRE(h != nullptr);
	CHECK(h->modState == 0x44);
	CHECK(h->transition == TRANSITION_DOUBLEDOWN);
	CHECK(h->usableIn == COMMANDUSABLE_SHELL);
	// the newest record is tried first (ZH / RotWK push at the head)
	CHECK(map.records().front().meta == CMSG_META_SELECT_HERO);
	CHECK(map.isBound(KEY_KP3, MOD_SHIFT | MOD_ALT | MOD_CTRL));
	CHECK_FALSE(map.isBound(KEY_KP3, MOD_NONE));
	// what retail refuses: an unknown meta name, key name, modifier, transition or UseableIn name
	for (const char *bad : { "CommandMap NOT_A_META\nEnd\n", "CommandMap STOP\n  Key = KEY_WHAT\nEnd\n", "CommandMap STOP\n  Modifiers = CONTROL\nEnd\n",
			 "CommandMap STOP\n  Transition = HOLD\nEnd\n", "CommandMap STOP\n  UseableIn = MENU\nEnd\n" })
	{
		MetaMap m;
		CHECK_MESSAGE(!parseCommandMap(m, bad, nullptr), bad);
	}
}

TEST_CASE("input1 commandmap: the install's 96 records carry RotWK's modifier values and every key the language map binds")
{
	if (!haveWorld("input1 commandmap"))
	{
		return;
	}
	const MetaMap &m = shared().meta;
	CHECK(m.records().size() == 96); // 94 of the language's CommandMap.ini + 2 of Data\INI\CommandMap.ini (the rapid iteration pair)
	const MetaMapRec *create = m.find(CMSG_META_CREATE_TEAM7);
	REQUIRE(create != nullptr);
	CHECK(create->key == KEY_7);
	CHECK(create->modState == 0x04);
	const MetaMapRec *add = m.find(CMSG_META_ADD_TEAM7);
	REQUIRE(add != nullptr);
	CHECK(add->modState == 0x10);
	const MetaMapRec *view = m.find(CMSG_META_VIEW_TEAM7);
	REQUIRE(view != nullptr);
	CHECK(view->modState == 0x40);
	const MetaMapRec *shot = m.find(CMSG_META_TAKE_SCREENSHOT);
	REQUIRE(shot != nullptr);
	CHECK(shot->usableIn == (COMMANDUSABLE_GAME | COMMANDUSABLE_SHELL));
	// Ctrl+Z is free for OpenBFME's free camera toggle; Z alone is TOGGLE_PLANNING_MODE
	CHECK_FALSE(m.isBound(KEY_Z, MOD_CTRL));
	CHECK(m.isBound(KEY_Z, MOD_NONE));
}

TEST_CASE("input1 groups: RotWK's SelectionTranslator: Ctrl+n / n / Shift+n through the keys, the double press within 5 logic frames, the reset on a selection change")
{
	if (!haveWorld("input1 groups"))
	{
		return;
	}
	SharedWorld &s = shared();
	Rig r(s);
	const Coord3D c0 = r.freeSpot(mapCenter(r).x, mapCenter(r).y, 260.0f);
	Object *a = r.make(templates(s).infantry.front(), c0.x, c0.y);
	Object *b = r.make(templates(s).infantry.front(), c0.x + 120.0f, c0.y);
	r.lookAt({ c0.x + 60.0f, c0.y, 0.0f }, 300.0f, 380.0f);
	r.frame(2);
	auto px = [&](Object *o) { return r.screenOf({ o->getPosition()->x, o->getPosition()->y, 6.0f }); };
	auto empty = [&]() { return ICoord2D{ px(a).x - 150, px(a).y + 200 }; };

	r.leftClick(px(a).x, px(a).y);
	REQUIRE(r.input->ui().selected() == std::vector<ObjectID>{ a->getID() });
	r.pressKey(KEY_1, KEY_STATE_LCONTROL); // CREATE_TEAM1: MSG_CREATE_TEAM1 with A
	CHECK(lastLog(*r.input).rfind("MSG_CREATE_TEAM1", 0) == 0);
	r.leftClick(px(b).x, px(b).y);
	r.pressKey(KEY_2, KEY_STATE_LCONTROL); // CREATE_TEAM2 with B
	r.frame(6);
	CHECK(r.local->hotkeySquad(1) == std::vector<ObjectID>{ a->getID() });
	CHECK(r.local->hotkeySquad(2) == std::vector<ObjectID>{ b->getID() });

	// 1: RW 0x83D04E: MSG_DESTROY_SELECTED_GROUP (true), then MSG_SELECT_TEAM1; the HUD selects A
	const size_t sel0 = logCount(*r.input, "MSG_SELECT_TEAM1");
	r.pressKey(KEY_1);
	CHECK(logCount(*r.input, "MSG_SELECT_TEAM1") == sel0 + 1);
	CHECK(lastLog(*r.input, 1).rfind("MSG_DESTROY_SELECTED_GROUP", 0) == 0);
	CHECK(r.input->ui().selected() == std::vector<ObjectID>{ a->getID() });
	// 1 again within 5 logic frames with group 1 still selected: the view looks at the group, nothing is sent
	r.lookAt({ c0.x + 900.0f, c0.y + 900.0f, 0.0f });
	r.frame(1);
	r.pressKey(KEY_1);
	CHECK(logCount(*r.input, "MSG_SELECT_TEAM1") == sel0 + 1);
	CHECK(std::fabs(r.view.position().x - a->getPosition()->x) < 1.0f);
	CHECK(std::fabs(r.view.position().y - a->getPosition()->y) < 1.0f);
	r.frame(1);
	CHECK(r.local->selection() == std::vector<ObjectID>{ a->getID() });

	// RW 0x83CF64: the selection changed since the last group press (a click on B): 1 within the window selects again (ZH's rule looked instead)
	r.lookAt({ c0.x + 60.0f, c0.y, 0.0f }, 300.0f, 380.0f);
	r.leftClick(px(b).x, px(b).y);
	r.pressKey(KEY_1);
	CHECK(logCount(*r.input, "MSG_SELECT_TEAM1") == sel0 + 2);
	CHECK(r.input->ui().selected() == std::vector<ObjectID>{ a->getID() });
	// past the window (RW 0xD9F608: 5 logic frames) a press selects again even with the group selected
	r.frame(5);
	r.pressKey(KEY_1);
	CHECK(logCount(*r.input, "MSG_SELECT_TEAM1") == sel0 + 3);

	// Shift+2: MSG_ADD_TEAM2, B joins the selection
	r.frame(6);
	const size_t add0 = logCount(*r.input, "MSG_ADD_TEAM2");
	r.pressKey(KEY_2, KEY_STATE_LSHIFT);
	CHECK(logCount(*r.input, "MSG_ADD_TEAM2") == add0 + 1);
	CHECK(r.input->ui().selected().size() == 2);
	r.frame(1);
	CHECK(r.local->selection().size() == 2);

	// VIEW_TEAMn (Alt+n) looks at group n for n = 1 .. 9; RW 0x83CDC0 tests 0 < n: Alt+0 looks nowhere
	r.leftClick(px(a).x, px(a).y);
	r.pressKey(KEY_0, KEY_STATE_LCONTROL); // group 0 = A
	r.frame(1);
	CHECK(r.local->hotkeySquad(0) == std::vector<ObjectID>{ a->getID() });
	r.lookAt({ c0.x + 900.0f, c0.y + 900.0f, 0.0f });
	const Coord3D away = r.view.position();
	r.pressKey(KEY_0, KEY_STATE_LALT);
	CHECK(r.view.position().x == away.x);
	r.pressKey(KEY_2, KEY_STATE_LALT);
	CHECK(std::fabs(r.view.position().x - b->getPosition()->x) < 1.0f);
	r.frame(1);
	CHECK(r.game->dispatch().unhandled().empty());
	(void)empty;
}

TEST_CASE("input1 groups: ADD_TO_TEAMn (RotWK only, no retail binding): MSG_ADD_TO_TEAMn adds the selection to the group without clearing it (RW 0x6AD722)")
{
	if (!haveWorld("input1 add to team"))
	{
		return;
	}
	SharedWorld &s = shared();
	Rig r(s);
	const Coord3D c0 = r.freeSpot(mapCenter(r).x, mapCenter(r).y, 260.0f);
	Object *a = r.make(templates(s).infantry.front(), c0.x, c0.y);
	Object *b = r.make(templates(s).infantry.front(), c0.x + 120.0f, c0.y);
	r.lookAt({ c0.x + 60.0f, c0.y, 0.0f }, 300.0f, 380.0f);
	r.frame(2);
	auto px = [&](Object *o) { return r.screenOf({ o->getPosition()->x, o->getPosition()->y, 6.0f }); };
	r.leftClick(px(a).x, px(a).y);
	r.pressKey(KEY_3, KEY_STATE_LCONTROL);
	r.leftClick(px(b).x, px(b).y);
	// a mod's CommandMap could bind it: the meta message as the MetaEventTranslator would append it
	r.input->stream().append(CMSG_META_ADD_TO_TEAM3);
	r.input->update();
	CHECK(lastLog(*r.input) == "MSG_ADD_TO_TEAM3 o" + std::to_string(b->getID()));
	r.frame(1);
	CHECK(r.local->hotkeySquad(3) == std::vector<ObjectID>{ a->getID(), b->getID() });
	CHECK(r.game->dispatch().unhandled().count(MSG_ADD_TO_TEAM3) == 0);
	// a later Ctrl+4 takes B out of group 3 (each member is in one group)
	r.pressKey(KEY_4, KEY_STATE_LCONTROL);
	r.frame(1);
	CHECK(r.local->hotkeySquad(3) == std::vector<ObjectID>{ a->getID() });
}

TEST_CASE("input1 commands: RotWK's CommandTranslator cases: the no-op metas are eaten, Del sends MSG_SELL, ` toggles the spell store, F12 asks for a screenshot")
{
	if (!haveWorld("input1 commands"))
	{
		return;
	}
	Rig r(shared());
	r.frame(1);
	const CommandTranslator &ct = r.input->commandTranslator();
	r.pressKey(KEY_S, KEY_STATE_LALT); // AUTO_SAVE: RW 0x820680, eaten
	CHECK(ct.retailNoOpMeta().count("AUTO_SAVE") == 1);
	const size_t sell0 = logCount(*r.input, "MSG_SELL");
	r.pressKey(KEY_DEL); // SELL: RW 0x820460
	CHECK(logCount(*r.input, "MSG_SELL") == sell0 + 1);
	r.pressKey(KEY_TICK); // SPELL_STORE (Transition UP): RW 0x820638
	CHECK(r.input->ui().spellStoreToggles() == 1);
	r.pressKey(KEY_F12); // TAKE_SCREENSHOT: RW 0x82019D
	CHECK(r.input->ui().screenshotRequests() == 1);
	r.pressKey(KEY_S); // STOP: MSG_DO_STOP (RW 0x81FEBF), no autosave
	CHECK(lastLog(*r.input).rfind("MSG_DO_STOP", 0) == 0);
	CHECK(ct.retailNoOpMeta().count("AUTO_SAVE") == 1);
}

namespace
{
struct BookmarkRig
{
	ArchiveW3DFileSource source;
	WW3DAssetManager assets;
	std::unique_ptr<LiveGame> game;
	CameraSettings gd;
	std::unique_ptr<TacticalCamera> cam;
	std::unique_ptr<HudInput> input;
	unsigned nowMs = 1000;
	int keyState = 0;

	explicit BookmarkRig(SharedWorld &s)
		: source(*s.mount->fs)
		, assets(source)
	{
		game = std::make_unique<LiveGame>(*s.world, *s.mount->fs, assets, s.options);
		LiveGame::Options o;
		o.mapName = "map mp fall back 4p";
		o.seed = 4711;
		o.slots.players.push_back({ "Player_1", "FactionMen", true, 0, 0, 0 });
		o.slots.players.push_back({ "Player_2", "FactionMordor", false, 1, 0, 1 });
		std::string err;
		REQUIRE_MESSAGE(game->load(o, &err), err);
		Player *local = game->players().findPlayerWithName("Player_1");
		REQUIRE(local != nullptr);
		game->players().setLocalPlayer(local);
		REQUIRE_MESSAGE(CameraSettings::load(*s.mount->fs, gd, &err), err);
		cam = std::make_unique<TacticalCamera>(gd);
		cam->setViewport(1024, 768);
		const Waypoint *w = game->logic().terrain()->findWaypointByName("Player_1_Start");
		REQUIRE(w != nullptr);
		const LoadedMap &lm = game->map();
		cam->startMap(game->logic(), lm.heightMap, lm.chunks.hasWorldInfo ? &lm.chunks.worldInfo : nullptr, w->location);
		input = std::make_unique<HudInput>(game->logic(), &game->ai(), *cam, game->commands(), s.mouse, s.meta);
		input->attachCamera(*cam);
	}
	~BookmarkRig()
	{
		input.reset();
		game->logic().reset();
	}
	void frame(int n = 1)
	{
		for (int i = 0; i < n; ++i)
		{
			input->update();
			nowMs += 33;
			input->cameraFrame(nowMs);
		}
	}
	void press(int key, int mods = 0)
	{
		if (mods)
		{
			keyState |= mods;
			input->key(KEY_LCTRL, KEY_STATE_DOWN | keyState);
		}
		input->key(key, KEY_STATE_DOWN | keyState);
		input->key(key, KEY_STATE_UP | keyState);
		if (mods)
		{
			keyState &= ~mods;
			input->key(KEY_LCTRL, KEY_STATE_UP | keyState);
		}
		frame(1);
	}
};
} // namespace

TEST_CASE("input1 camera: the bookmarks: Ctrl+F1 stores the view, F1 brings it back, an unset bookmark does nothing (RW 0x83AC4A cases 0x24 .. 0x33)")
{
	if (!haveWorld("input1 bookmarks"))
	{
		return;
	}
	BookmarkRig r(shared());
	r.frame(3);
	TacticalCamera &c = *r.cam;
	const Coord3D home = c.position();
	c.setAngle(0.5f);
	r.frame(1);
	const float angle = c.getAngle(), height = c.getHeightAboveGround();
	r.press(KEY_F1, KEY_STATE_LCONTROL); // SAVE_VIEW1
	CHECK(std::find(r.input->ui().messages().begin(), r.input->ui().messages().end(), "GUI:BookmarkXSet") != r.input->ui().messages().end());
	c.lookAt({ home.x + 400.0f, home.y + 300.0f, 0.0f });
	c.setAngle(0.0f);
	r.frame(2);
	REQUIRE(std::fabs(c.position().x - home.x) > 100.0f);
	const Coord3D moved = c.position();
	r.press(KEY_F2); // VIEW_VIEW2: never stored
	CHECK(c.position().x == moved.x);
	r.press(KEY_F1); // VIEW_VIEW1
	CHECK(std::fabs(c.position().x - home.x) < 1.0f);
	CHECK(std::fabs(c.position().y - home.y) < 1.0f);
	CHECK(std::fabs(c.getAngle() - angle) < 1e-4f);
	CHECK(std::fabs(c.getHeightAboveGround() - height) < 1e-3f);
	// numpad 5: CAMERA_RESET (RW 0x8203FA -> View::resetCamera at the view's position): the default angle and the maximum height
	r.press(KEY_KP5);
	CHECK(c.getHeightAboveGround() == c.maxHeight());
	CHECK(std::fabs(c.position().x - home.x) < 1.0f);
}

TEST_CASE("input1 hotkeys: the label's '&' character (RW 0x75A67F) and the HotKeyTranslator's key up rule (RW 0x75B068 / 0x75AEFA)")
{
	CHECK(HotKeyTranslator::hotkeyOf(u"Ambush For&mation") == U'm');
	CHECK(HotKeyTranslator::hotkeyOf(u"&Farm") == U'F');
	CHECK(HotKeyTranslator::hotkeyOf(u"Farm&") == 0);
	CHECK(HotKeyTranslator::hotkeyOf(u"Farm") == 0);
	CHECK(HotKeyTranslator::hotkeyOf(u"A&b&c") == U'b');
	CHECK(HotKeyTranslator::usCharOf(KEY_F) == U'f');
	CHECK(HotKeyTranslator::usCharOf(KEY_7) == U'7');
	CHECK(HotKeyTranslator::usCharOf(KEY_F1) == 0);
	if (!haveWorld("input1 hotkeys"))
	{
		return;
	}
	Rig r(shared());
	r.frame(1);
	HotKeyTranslator &hk = r.input->hotKeyTranslator();
	std::vector<std::pair<int, bool>> pressed;
	hk.setPress([&](int slot, bool inPalantir) {
		pressed.push_back({ slot, inPalantir });
		return true;
	});
	HotKeyTranslator::Entry farm;
	farm.key = U'F';
	farm.slot = 3;
	farm.inPalantir = true;
	farm.availability = HotKeyTranslator::Availability::Enabled;
	HotKeyTranslator::Entry well = farm;
	well.key = U'w';
	well.slot = 4;
	well.availability = HotKeyTranslator::Availability::Disabled;
	HotKeyTranslator::Entry hidden = farm;
	hidden.key = U'q';
	hidden.slot = 5;
	hidden.availability = HotKeyTranslator::Availability::Hidden;
	hk.setEntries({ farm, well, hidden });
	r.pressKey(KEY_F); // the key up: 'f' (lower first, then 'F') -> slot 3
	REQUIRE(pressed.size() == 1);
	CHECK(pressed[0] == std::make_pair(3, true));
	r.pressKey(KEY_F, KEY_STATE_LSHIFT); // Shift alone is allowed
	CHECK(pressed.size() == 2);
	r.pressKey(KEY_F, KEY_STATE_LCONTROL); // Ctrl refuses the key (and CREATE_FORMATION takes Ctrl+F)
	r.pressKey(KEY_F, KEY_STATE_LALT);
	CHECK(pressed.size() == 2);
	r.pressKey(KEY_W); // disabled: the disabled sound, nothing pressed
	r.pressKey(KEY_Q); // hidden: passed over
	CHECK(pressed.size() == 2);
	CHECK(hk.outcomes().at("disabled") == 1);
	CHECK(hk.outcomes().at("enabled") == 2);
	// the layout's character decides (an AZERTY 'a' sits on the scan code of Q): the device passes it with the key
	r.input->key(KEY_Q, KEY_STATE_DOWN, U'a');
	r.input->key(KEY_Q, KEY_STATE_UP, U'a');
	r.input->update();
	CHECK(pressed.size() == 2); // no button marks 'a' yet
	farm.key = U'A';
	hk.setEntries({ farm });
	r.input->key(KEY_Q, KEY_STATE_DOWN, U'a');
	r.input->key(KEY_Q, KEY_STATE_UP, U'a');
	r.input->update();
	CHECK(pressed.size() == 3);
}

TEST_CASE("input1 r2 modifiers: the sides are tracked apart and the device's aggregate flags repair a lost transition (review r1)")
{
	ModifierTracker m;
	m.key(KEY_LCTRL, true);
	m.key(KEY_RCTRL, true);
	m.key(KEY_LCTRL, false); // left Ctrl let go, right Ctrl still held
	CHECK((m.state() & KEY_STATE_CONTROL) == KEY_STATE_RCONTROL);
	CHECK(m.sync(true, false, false).empty()); // the pointer's next event says Ctrl: nothing to repair
	CHECK((m.state() & KEY_STATE_CONTROL) == KEY_STATE_RCONTROL);
	// the right release was lost (Alt+Tab): the next event says no Ctrl, the right side is released
	const auto up = m.sync(false, false, false);
	REQUIRE(up.size() == 1);
	CHECK(up[0].key == KEY_RCTRL);
	CHECK_FALSE(up[0].down);
	CHECK(m.state() == 0);
	// a Shift press the window never saw: the left one is pressed
	const auto down = m.sync(false, true, false);
	REQUIRE(down.size() == 1);
	CHECK(down[0].key == KEY_LSHIFT);
	CHECK(down[0].down);
	CHECK(m.state() == KEY_STATE_LSHIFT);
	CHECK(ModifierTracker::isModifierKey(KEY_RALT));
	CHECK_FALSE(ModifierTracker::isModifierKey(KEY_1));
}

TEST_CASE("input1 r2 groups: recall and the camera skip what RW 0x8DB103 getLiveObjects filters (unselectable, dead), the membership stays")
{
	if (!haveWorld("input1 groups filter"))
	{
		return;
	}
	SharedWorld &s = shared();
	Rig r(s);
	const Coord3D c0 = r.freeSpot(mapCenter(r).x, mapCenter(r).y, 260.0f);
	Object *a = r.make(templates(s).infantry.front(), c0.x, c0.y);
	Object *b = r.make(templates(s).infantry.front(), c0.x + 120.0f, c0.y);
	r.lookAt({ c0.x + 60.0f, c0.y, 0.0f }, 300.0f, 380.0f);
	r.frame(2);
	auto px = [&](Object *o) { return r.screenOf({ o->getPosition()->x, o->getPosition()->y, 6.0f }); };
	r.leftDrag(px(a).x - 40, px(a).y - 40, px(b).x + 40, px(b).y + 40);
	REQUIRE(r.input->ui().selected().size() == 2);
	r.pressKey(KEY_1, KEY_STATE_LCONTROL);
	r.frame(6);
	REQUIRE(r.local->hotkeySquad(1).size() == 2);
	// A becomes UNSELECTABLE (status 3: what a garrison's contain mask gives its riders, RW 0x6901FC)
	const int unselectable = ObjectTemplateInfoBuilder::objectStatusIndex("UNSELECTABLE");
	REQUIRE(unselectable >= 0);
	a->setStatus((unsigned)unselectable, true);
	CHECK_FALSE(PlayerCommands::isSelectable(*a));
	r.pressKey(KEY_1);
	CHECK(r.input->ui().selected() == std::vector<ObjectID>{ b->getID() });
	r.frame(1);
	CHECK(r.local->selection() == std::vector<ObjectID>{ b->getID() });
	CHECK(r.local->hotkeySquad(1).size() == 2); // A stays a member
	// B effectively dead too: no member is live, past the double press window 1 selects nothing
	b->friend_setEffectivelyDead(true);
	CHECK_FALSE(PlayerCommands::isSelectable(*b));
	r.frame(6);
	r.pressKey(KEY_1);
	CHECK(r.input->ui().selected().empty());
	r.frame(1);
	CHECK(r.local->selection().empty());
	r.lookAt({ c0.x + 900.0f, c0.y + 900.0f, 0.0f });
	const Coord3D away = r.view.position();
	r.pressKey(KEY_1, KEY_STATE_LALT); // VIEW_TEAM1: no live member, no look
	CHECK(r.view.position().x == away.x);
	// A selectable again: it comes back with the group
	a->setStatus((unsigned)unselectable, false);
	r.frame(6);
	r.pressKey(KEY_1);
	CHECK(r.input->ui().selected() == std::vector<ObjectID>{ a->getID() });
	r.frame(1);
	CHECK(r.local->selection() == std::vector<ObjectID>{ a->getID() });
	b->friend_setEffectivelyDead(false);
}

TEST_CASE("input1 r2 orders: A makes no mode (RotWK has no case), Alt sends MSG_CHANGE_ORDERMODE and the logic stores the mode, a move stays a move (r3); the German Y / Z swap")
{
	if (!haveWorld("input1 order modes"))
	{
		return;
	}
	SharedWorld &s = shared();
	Rig r(s);
	const Coord3D c0 = r.freeSpot(mapCenter(r).x, mapCenter(r).y, 260.0f);
	Object *a = r.make(templates(s).infantry.front(), c0.x, c0.y);
	r.lookAt({ c0.x, c0.y, 0.0f }, 300.0f, 380.0f);
	r.frame(2);
	const ICoord2D p = r.screenOf({ a->getPosition()->x, a->getPosition()->y, 6.0f });
	r.leftClick(p.x, p.y);
	const CommandTranslator &ct = r.input->commandTranslator();
	r.pressKey(KEY_A); // TOGGLE_ATTACKMOVE: no case in RW 0x81F8D8 (the Attack Move button's hotkey does it)
	CHECK_FALSE(r.input->ui().isInAttackMoveToMode());
	CHECK(ct.retailNoOpMeta().count("TOGGLE_ATTACKMOVE") == 1);
	// Alt down: ORDERMODE_WAYPOINT -> MSG_CHANGE_ORDERMODE (1, 0) (RW 0x820249); the logic's player is in mode 1
	r.setModifier(KEY_LALT, KEY_STATE_LALT, true);
	r.input->update();
	CHECK(lastLog(*r.input) == "MSG_CHANGE_ORDERMODE i1 i0");
	r.frame(1);
	CHECK(r.local->orderMode() == 1);
	CHECK_FALSE(r.input->ui().isInWaypointMode()); // RotWK keeps the UI's waypoint flag for BEGIN_WAYPOINTS (unbound)
	// r3: the AiOrdersManager queue that reads the mode is not ported (stop S-281): a move given in mode 1 is a move, not a waypoint
	const auto before = r.game->aiCommands().stats();
	r.leftClick(p.x + 150, p.y + 80); // the standard setup of the rig: the left click orders
	r.frame(1);
	CHECK(lastLog(*r.input, 0).rfind("MSG_DO_MOVETO", 0) == 0);
	CHECK(r.game->aiCommands().stats().waypoints == before.waypoints);
	CHECK(r.game->aiCommands().stats().moves == before.moves + 1);
	// Alt up: (0, 1) back to immediate
	r.setModifier(KEY_LALT, KEY_STATE_LALT, false);
	r.frame(1);
	CHECK(lastLog(*r.input) == "MSG_CHANGE_ORDERMODE i0 i1");
	CHECK(r.local->orderMode() == 0);
	// a stale (1, 1) changes nothing (RW 0x77BCCF: the mode it replaces must match)
	{
		ClientMessage &stale = r.input->stream().append(MSG_CHANGE_ORDERMODE);
		stale.appendInteger(1);
		stale.appendInteger(1);
	}
	r.frame(1);
	CHECK(r.local->orderMode() == 0);
	// German keyboard layout: the Y key gives the CommandMap's KEY_Z (TOGGLE_PLANNING_MODE)
	r.input->metaTranslator().setGermanKeyboard(true);
	const unsigned planning0 = ct.unportedMeta().count("TOGGLE_PLANNING_MODE") ? ct.unportedMeta().at("TOGGLE_PLANNING_MODE") : 0;
	r.pressKey(KEY_Y);
	CHECK(ct.unportedMeta().at("TOGGLE_PLANNING_MODE") == planning0 + 1);
	r.input->metaTranslator().setGermanKeyboard(false);
	r.pressKey(KEY_Y);
	CHECK(ct.unportedMeta().at("TOGGLE_PLANNING_MODE") == planning0 + 1);
	CHECK(r.game->dispatch().unhandled().count(MSG_CHANGE_ORDERMODE) == 0);
}

TEST_CASE("input1 r3 force attack: Ctrl held makes the force-attack mode (ZH's BEGIN_ / END_FORCEATTACK, S-1675); a click on the ground, an own unit and an own building "
		  "is a force attack (RW 0x81E4B5), the cursor says so")
{
	if (!haveWorld("input1 force attack"))
	{
		return;
	}
	SharedWorld &s = shared();
	Rig r(s);
	const Coord3D c0 = r.freeSpot(mapCenter(r).x, mapCenter(r).y, 420.0f);
	Object *horde = r.make("GondorFighterHorde", c0.x, c0.y);
	Object *unit = r.make("GondorBoromir", c0.x + 110.0f, c0.y + 40.0f); // an own unit of one object (a hero)
	Object *barracks = r.make("GondorBarracks", c0.x - 220.0f, c0.y);
	REQUIRE(horde != nullptr);
	REQUIRE(barracks != nullptr);
	r.lookAt({ c0.x - 40.0f, c0.y, 0.0f }, 420.0f, 520.0f);
	r.frame(3);
	auto px = [&](const Object *o, float up) { return r.screenOf({ o->getPosition()->x, o->getPosition()->y, o->getPosition()->z + up }); };
	const ICoord2D ph = px(horde, 6.0f);
	r.leftDrag(ph.x - 50, ph.y - 50, ph.x + 50, ph.y + 50);
	r.frame(2);
	REQUIRE(r.local->selection() == std::vector<ObjectID>{ horde->getID() });
	const ICoord2D pu = px(unit, 10.0f), pb = px(barracks, 30.0f);
	REQUIRE(HudObjects::pickObject(r.input->context(), pu) == unit);
	const ICoord2D pg{ ph.x, ph.y + 120 };
	REQUIRE(HudObjects::pickObject(r.input->context(), pg) == nullptr);

	r.setModifier(KEY_LCTRL, KEY_STATE_LCONTROL, true); // BEGIN_FORCEATTACK (Ctrl, KEY_NONE)
	CHECK(r.input->ui().isInForceAttackMode());
	// the cursor while Ctrl is held (CommandTranslator's mouse-over hint through evaluateForceAttack)
	r.move(pu.x, pu.y);
	CHECK(r.input->ui().cursor() == std::string(MouseCursorName::ForceAttackObj));
	r.move(pg.x, pg.y);
	CHECK(r.input->ui().cursor() == std::string(MouseCursorName::ForceAttackGround));
	// the ground: MSG_DO_FORCE_ATTACK_GROUND with the position
	r.leftClick(pg.x, pg.y);
	CHECK(lastLog(*r.input).rfind("MSG_DO_FORCE_ATTACK_GROUND L(", 0) == 0);
	// an own unit: MSG_DO_FORCE_ATTACK_OBJECT with the unit and the position, the logic takes it
	const unsigned long long forced0 = r.game->aiCommands().stats().forceAttacks;
	r.leftClick(pu.x, pu.y);
	CHECK(lastLog(*r.input).rfind("MSG_DO_FORCE_ATTACK_OBJECT o" + std::to_string(unit->getID()) + " L(", 0) == 0);
	// an own building
	r.leftClick(pb.x, pb.y);
	CHECK(lastLog(*r.input).rfind("MSG_DO_FORCE_ATTACK_OBJECT o" + std::to_string(barracks->getID()) + " L(", 0) == 0);
	r.frame(2);
	CHECK(r.game->aiCommands().stats().forceAttacks == forced0 + 2);
	CHECK(r.local->selection() == std::vector<ObjectID>{ horde->getID() }); // a force attack selects nothing
	r.setModifier(KEY_LCTRL, KEY_STATE_LCONTROL, false); // END_FORCEATTACK
	CHECK_FALSE(r.input->ui().isInForceAttackMode());
	// without Ctrl the ground is a move again
	r.move(pg.x, pg.y);
	CHECK(r.input->ui().cursor() == std::string(MouseCursorName::Move));
	CHECK(r.input->commandTranslator().retailNoOpMeta().count("BEGIN_FORCEATTACK") == 0);
}
