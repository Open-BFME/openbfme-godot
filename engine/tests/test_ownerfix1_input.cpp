// OpenBFME retail tests: lane OWNERFIX-1 round 2, the map scripts' DISABLE_INPUT (RW 0x7BC586) through EVERY command entry path of the HUD (review: "one
// SpellBookChillWind object appeared while input was disabled": a spell aimed before the script's DISABLE_INPUT still cast, because the spell book bar's
// messages went straight to the command list and skipped the CommandTranslator's input guard, RW 0x81F8F2 .. 0x81F979).
//
// Retail facts (BFME2 decomp, tier A for RW 0x7BC586 / RW 0x69B5ED / RW 0x83AB5D / RW 0x8076CD):
//   * every logic message passes TheMessageStream and the CommandTranslator drops all but 0x3EC, 0x447, 0x448, 0x44A, 0x45F, 0x469 while an input flag is off;
//   * the disable edge (InGameUI::setInputEnabled RW 0x69B5ED): setSelecting(false), setRadiusCursorNone, the order mode flags off, MSG_CHANGE_ORDERMODE (0, 1);
//   * DISABLE_INPUT then sends MSG_DESTROY_SELECTED_GROUP, deselects, clears the control bar's help provider (RW 0x8076CD) and the LookAt flags (RW 0x83AB5D).
// Each test drives the real HUD (mouse events, key events, the movies' buttons, the control bar), counts the messages that reach the command list and checks
// the effect in the world. They SKIP when ROTWK_INSTALL / BFME2_INSTALL are unset. GPL-3.0.

#include "HudTestUtil.h"

#include "Common/Player.h"
#include "Common/PlayerScience.h"
#include "Common/Science.h"
#include "Common/Team.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameClient/ControlBar.h"
#include "GameClient/ControlBarCommands.h"
#include "GameClient/GUI/AptScreens/AptPalantir.h"
#include "GameClient/GUI/AptScreens/AptSpellStore.h"
#include "GameClient/GUI/ShellServices.h"
#include "GameClient/GameTextTableSource.h"
#include "GameClient/InGameHeroSelect.h"
#include "GameClient/InGameHud.h"
#include "GameClient/MessageStream/LookAtXlat.h"
#include "GameClient/TacticalCamera.h"
#include "GameClient/CameraSettings.h"
#include "GameClient/LiveGame.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"
#include "GameLogic/GameLogicDispatch.h"
#include "GameLogic/ScriptEngine/ScriptEngine.h"
#include "GameLogic/SimMath.h"
#include "Libraries/Source/Apt/AptButtonInst.h"
#include "Libraries/Source/Apt/AptCharacterInst.h"
#include "Libraries/Source/Apt/AptInput.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"
#include "GameLogic/GameMessage.h"
#include "GameEngineDevice/W3DDevice/GameClient/WorldHeightMap.h"
#include "GameLogic/AI/AIGuard.h"
#include "GameLogic/Map/TerrainLogic.h"
#include "GameLogic/ObjectTemplateInfo.h"
#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/System/ShroudManager.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

using namespace hudtest;

namespace
{
size_t countMessages(HudInput &in, const std::string &prefix)
{
	size_t n = 0;
	for (const std::string &l : in.messageLog())
	{
		n += l.compare(0, prefix.size(), prefix) == 0 ? 1 : 0;
	}
	return n;
}

int countTemplate(GameLogic &logic, const std::string &name)
{
	int n = 0;
	for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		if (!o->isEffectivelyDead() && o->getTemplate() && o->getTemplate()->getName() == name)
		{
			++n;
		}
	}
	return n;
}

// a retail skirmish with the in-game HUD (the real Palantir movies, the control bar, the spell book) booted on it, driven by mouse and key events
struct HudFix
{
	Rig rig;
	RecordingShellServices services;
	GameTextTableSource text;
	std::unique_ptr<InGameHud> hud;
	int timeMs = 2000;

	HudFix(SharedWorld &s, const char *faction) : rig(s, "map mp fall back 4p", faction)
	{
		std::vector<std::uint8_t> bytes;
		std::string textError;
		REQUIRE_MESSAGE(s.mount->fs->readFile("data/lotr.str", bytes, &textError), textError);
		REQUIRE_MESSAGE(text.table.parse(bytes, &textError), textError);
		InGameHud::Config cfg{ *rig.game, *s.world, *s.mount->fs, services, rig.view, s.mouse, s.meta, &text };
		hud = std::make_unique<InGameHud>(cfg);
		std::string error;
		REQUIRE_MESSAGE(hud->boot(&error), error);
		hud->setWindowSize(1024, 768);
		rig.view.setScreen(1024, 768);
	}
	~HudFix() { hud.reset(); }
	GameLogic &logic() { return rig.logic(); }
	InGameUI &ui() { return hud->input().ui(); }
	void frames(int n)
	{
		for (int i = 0; i < n; ++i)
		{
			hud->update(0.1);
			rig.game->advance(0.2);
		}
	}
	size_t count(const std::string &prefix) { return countMessages(hud->input(), prefix); }
	void press(const ICoord2D &p, HudInput::Button b = HudInput::Button::Left, bool dbl = false)
	{
		hud->mouseMove(p.x, p.y, 0);
		timeMs += 300;
		hud->mouseButton(b, true, p.x, p.y, 0, timeMs, dbl);
		timeMs += 40;
		hud->mouseButton(b, false, p.x, p.y, 0, timeMs);
		frames(1);
	}
	void click(const Coord3D &world, HudInput::Button b = HudInput::Button::Left, bool dbl = false)
	{
		rig.lookAt(world);
		frames(1);
		press(rig.screenOf(world), b, dbl);
	}
	void key(int code, char32_t ch)
	{
		hud->key(code, KEY_STATE_DOWN, ch);
		frames(1);
		hud->key(code, KEY_STATE_UP, ch);
		frames(1);
	}
	// the body of an object, where a player clicks to select it (a horde draws no model of its own: its first soldier does)
	static Coord3D body(const Object &o)
	{
		const Object *model = &o;
		if (ContainModuleInterface *c = o.getContain())
		{
			if (c->getHordeContainInterface() && c->getContainedItemsList() && !c->getContainedItemsList()->empty())
			{
				model = c->getContainedItemsList()->front();
			}
		}
		Coord3D p = *model->getPosition();
		p.z += 8.0f;
		return p;
	}
	bool select(Object *o)
	{
		ui().deselectAll();
		click(body(*o));
		frames(2);
		return ui().selected() == std::vector<ObjectID>{ o->getID() } && rig.local->selection() == std::vector<ObjectID>{ o->getID() };
	}
	Object *makeInfantry(float dx, float dy)
	{
		float mx = 0, my = 0;
		REQUIRE(logic().terrain()->getExtent(0, mx, my));
		const Coord3D base = rig.freeSpot(mx * 0.5f, my * 0.5f, 200.0f);
		return rig.make(templates(rig.sh).infantry.front(), base.x + dx, base.y + dy);
	}
	// the Angmar citadel the spell book needs (a COMMANDCENTER), the power's science, the whole map explored (a cast needs a clear spot)
	void giveSpellBook(const char *science)
	{
		const ThingTemplate *cit = rig.sh.world->things().findTemplate("AngmarFortressCitadel");
		REQUIRE(cit);
		Object *c = logic().newObject(cit, rig.local->getDefaultTeam(), ObjectStatusMaskType{});
		REQUIRE(c);
		Coord3D at{ 600.0f, 600.0f, 0.0f };
		c->setPosition(&at);
		c->friend_onBuildComplete();
		rig.local->science().grantScience(TheScienceStore->getScienceFromInternalName(science));
		rig.game->shroud().revealMapForPlayerPermanently(rig.index());
		frames(40);
	}
	const ControlBarButton *button(int command)
	{
		for (const auto *list : { &hud->controlBar().palantirButtons(), &hud->controlBar().sideButtons(), &hud->controlBar().offBarButtons() })
		{
			for (const ControlBarButton &b : *list)
			{
				if (b.button && b.button->m_command == command)
				{
					return &b;
				}
			}
		}
		return nullptr;
	}
};

constexpr int kChillWindSlot = 0; // the movie's first visible slot: the bar shows only the powers the player owns (AngmarSpellBookCommandSet slot 2 is the first owned)
} // namespace

// ---- the one door: the CommandTranslator's guard over the stream ----------------------------------------------------------------------------------------
TEST_CASE("ownerfix1 input door: with the input off the stream passes only RotWK's six logic messages (RW 0x81F8F2 .. 0x81F979); every other one is dropped, whoever sent it")
{
	if (!haveWorld("ownerfix1 input door"))
	{
		return;
	}
	SharedWorld &s = shared();
	Rig r(s);
	const int idx = r.index();
	const int passing[] = { MSG_DESTROY_SELECTED_GROUP, MSG_SET_REPLAY_CAMERA, MSG_SELF_DESTRUCT, MSG_LOGIC_CRC, MSG_CALL_IN_REINFORCEMENTS, MSG_CHANGE_ORDERMODE };
	const int dropped[] = { MSG_DO_MOVETO, MSG_DO_STOP, MSG_QUEUE_UNIT_CREATE, MSG_PURCHASE_SCIENCE, MSG_DO_SPECIAL_POWER, MSG_DO_SPECIAL_POWER_AT_LOCATION,
		MSG_CREATE_SELECTED_GROUP, MSG_DO_ATTACKMOVETO, MSG_EVACUATE, MSG_SELL };
	// control: with the input on, the same messages all arrive
	for (int t : dropped)
	{
		r.input->submit(GameMessage(t, idx));
	}
	CHECK(r.input->flush() == sizeof(dropped) / sizeof(dropped[0]));
	r.game->commands().reset(); // (malformed on purpose: the logic never runs them)
	r.input->ui().setInputEnabled(false);
	CHECK(r.input->flush() == 1u); // the disable edge's own MSG_CHANGE_ORDERMODE (0, 1) passes
	for (int t : dropped)
	{
		r.input->submit(GameMessage(t, idx));
	}
	CHECK(r.input->flush() == 0u);
	for (int t : passing)
	{
		r.input->submit(GameMessage(t, idx));
	}
	CHECK(r.input->flush() == sizeof(passing) / sizeof(passing[0]));
	r.input->ui().setInputEnabled(true);
}

// ---- the spell book bar: Sol's reproduction ------------------------------------------------------------------------------------------------------------
TEST_CASE("ownerfix1 cutscene input: a spell aimed before DISABLE_INPUT casts nothing while it is off (no SpellBookChillWind, no message), and the aim goes on after ENABLE_INPUT")
{
	if (!haveWorld("ownerfix1 pending aim"))
	{
		return;
	}
	SharedWorld &s = shared();
	HudFix f(s, "FactionAngmar");
	auto ctx = f.hud->enterContext();
	f.giveSpellBook("SCIENCE_ChillWind");
	AptPalantir *pal = f.hud->palantir();
	REQUIRE(pal);
	REQUIRE(pal->pressSpellSlot(kChillWindSlot));
	REQUIRE(f.hud->spellBar().targeting());
	Coord3D at{ 900.0f, 700.0f, 0.0f };
	at.z = f.logic().getGroundHeight(at.x, at.y);
	f.rig.lookAt(at);
	f.frames(1);
	const ICoord2D px = f.rig.screenOf(at);
	f.hud->mouseMove(px.x, px.y, 0);
	f.frames(2);
	CHECK(f.hud->radiusCursor().active()); // the aiming circle shows
	const int winds0 = countTemplate(f.logic(), "SpellBookChillWind");
	const size_t casts0 = f.count("MSG_DO_SPECIAL_POWER_AT_LOCATION");

	f.hud->scriptInput(false); // the script's DISABLE_INPUT
	CHECK_FALSE(f.hud->radiusCursor().active()); // setRadiusCursorNone in the edge's own call (RW 0x69B632), not at the next frame
	f.frames(2);
	CHECK_FALSE(f.hud->radiusCursor().active());
	f.press(px);
	f.frames(10);
	CHECK(f.count("MSG_DO_SPECIAL_POWER_AT_LOCATION") == casts0);
	CHECK(countTemplate(f.logic(), "SpellBookChillWind") == winds0);
	CHECK(f.hud->spellBar().targeting()); // the aim is pending, as retail's pending GUI command is

	f.hud->scriptInput(true); // ENABLE_INPUT
	f.hud->mouseMove(px.x, px.y, 0);
	f.frames(2);
	CHECK(f.hud->radiusCursor().active());
	f.press(px);
	f.frames(10);
	CHECK(f.count("MSG_DO_SPECIAL_POWER_AT_LOCATION") == casts0 + 1);
	CHECK(countTemplate(f.logic(), "SpellBookChillWind") == winds0 + 1);
	CHECK_FALSE(f.hud->spellBar().targeting());
}

TEST_CASE("ownerfix1 cutscene input: a spell book press starts no aim while the input is off")
{
	if (!haveWorld("ownerfix1 spell press"))
	{
		return;
	}
	SharedWorld &s = shared();
	HudFix f(s, "FactionAngmar");
	auto ctx = f.hud->enterContext();
	f.giveSpellBook("SCIENCE_ChillWind");
	AptPalantir *pal = f.hud->palantir();
	REQUIRE(pal);
	f.hud->scriptInput(false);
	pal->pressSpellSlot(kChillWindSlot); // (the movie's button handler runs; the HUD starts no aim)
	CHECK_FALSE(f.hud->spellBar().targeting());
	f.hud->scriptInput(true);
	pal->pressSpellSlot(kChillWindSlot); // the control: the same press aims
	CHECK(f.hud->spellBar().targeting());
}

TEST_CASE("ownerfix1 cutscene input: a spell store closed while the input is off sends no purchase; closed with it on, it buys")
{
	if (!haveWorld("ownerfix1 store close"))
	{
		return;
	}
	SharedWorld &s = shared();
	HudFix f(s, "FactionAngmar");
	auto ctx = f.hud->enterContext();
	f.giveSpellBook("SCIENCE_ChillWind"); // (the store sells the other tier 1 powers too: the rank 1 points buy one)
	f.rig.local->science().addSciencePurchasePoints(10);
	std::string error;
	REQUIRE_MESSAGE(f.hud->openSpellStore(&error), error);
	f.frames(40);
	AptSpellStore *st = f.hud->spellStore();
	REQUIRE(st);
	int first = -1;
	for (int i = 0; i < SpellStoreModel::MAX_BUTTONS && first < 0; ++i)
	{
		if (AptSpellStore::stateName(st->buttonState(i)) == std::string("_active"))
		{
			first = i;
		}
	}
	REQUIRE(first == 0); // Blight (AngmarSpellStoreCommandSet slot 1, 5 points): the first power the player can still buy (Chill Wind, slot 2, is owned)
	const ScienceType sci = st->model().buttons()[(size_t)first].science;
	REQUIRE(st->click(first));
	f.hud->scriptInput(false);
	const size_t buys0 = f.count("MSG_PURCHASE_SCIENCE");
	f.hud->closeSpellStore();
	f.frames(20);
	CHECK(f.count("MSG_PURCHASE_SCIENCE") == buys0);
	CHECK_FALSE(f.rig.local->science().hasScience(sci));
	f.hud->scriptInput(true);
	REQUIRE_MESSAGE(f.hud->openSpellStore(&error), error);
	f.frames(40);
	REQUIRE(f.hud->spellStore());
	REQUIRE(f.hud->spellStore()->click(first));
	f.hud->closeSpellStore();
	f.frames(20);
	CHECK(f.count("MSG_PURCHASE_SCIENCE") == buys0 + 1);
	CHECK(f.rig.local->science().hasScience(sci));
}

// ---- the control bar, hotkeys, pending GUI commands, selection --------------------------------------------------------------------------------------------
TEST_CASE("ownerfix1 cutscene input: a control bar button, a hotkey and a pending GUI command's click do nothing while the input is off; the same actions work with it on")
{
	if (!haveWorld("ownerfix1 bar hotkey pending"))
	{
		return;
	}
	SharedWorld &s = shared();
	HudFix f(s, "FactionMen");
	auto ctx = f.hud->enterContext();
	REQUIRE_FALSE(templates(s).hordes.empty());
	float mx = 0, my = 0;
	REQUIRE(f.logic().terrain()->getExtent(0, mx, my));
	const Coord3D base = f.rig.freeSpot(mx * 0.5f, my * 0.5f, 250.0f);
	Object *horde = f.rig.make(templates(s).hordes.front(), base.x, base.y);
	REQUIRE(horde);
	f.frames(10);
	REQUIRE(f.select(horde));
	f.frames(3);
	const ControlBarButton *stop = f.button(GUI_COMMAND_STOP);
	const ControlBarButton *attackMove = f.button(GUI_COMMAND_ATTACK_MOVE);
	REQUIRE(stop);
	REQUIRE(attackMove);
	const int stopSlot = stop->slot, attackSlot = attackMove->slot;
	const bool stopInPalantir = stop->inPalantir, attackInPalantir = attackMove->inPalantir;
	const ObjectID id = horde->getID();

	// the attack move button pressed with the horde selected: a pending GUI command, waiting for its ground click
	REQUIRE(f.hud->controlBar().pressButton(attackSlot, attackInPalantir));
	f.frames(2);
	REQUIRE(f.ui().getGUICommand() != nullptr);

	f.hud->scriptInput(false); // DISABLE_INPUT: the selection goes, the pending command stays (retail's edge clears modes, not the command)
	f.frames(2);
	CHECK(f.ui().getGUICommand() != nullptr);
	CHECK(f.ui().selected().empty());
	// a selection that does not come from the player's input (a test hook: the mirror and the logic's group message appended to the command list directly), so
	// the bar has a source and the checks below are not vacuous
	f.ui().selectObject(id);
	GameMessage group(MSG_CREATE_SELECTED_GROUP, f.rig.index());
	group.appendBooleanArgument(true);
	group.appendObjectIDArgument(id);
	f.rig.game->commands().append(group);
	f.frames(2);
	REQUIRE(f.rig.local->selection() == std::vector<ObjectID>{ id });
	f.frames(3);

	const size_t stops0 = f.count("MSG_DO_STOP"), moves0 = f.count("MSG_DO_ATTACKMOVETO");
	// the pending command's ground click
	const Coord3D ground{ base.x + 220.0f, base.y + 160.0f, 0.0f };
	f.click(ground);
	f.frames(3);
	CHECK(f.count("MSG_DO_ATTACKMOVETO") == moves0);
	// the bar's Stop button (a window callback: retail runs it, its message dies in the stream; pressing it also drops the pending command)
	f.hud->controlBar().pressButton(stopSlot, stopInPalantir);
	f.frames(2);
	CHECK(f.count("MSG_DO_STOP") == stops0);
	// the S hotkey
	f.key(KEY_S, U's');
	CHECK(f.count("MSG_DO_STOP") == stops0);

	// the controls: with the input on, the same three actions work (the selection the hook made is still there)
	f.hud->scriptInput(true);
	f.frames(2);
	REQUIRE(f.ui().selected() == std::vector<ObjectID>{ id });
	REQUIRE(f.hud->controlBar().pressButton(attackSlot, attackInPalantir));
	f.frames(2);
	REQUIRE(f.ui().getGUICommand() != nullptr);
	f.click(ground);
	f.frames(3);
	CHECK(f.count("MSG_DO_ATTACKMOVETO") == moves0 + 1);
	f.key(KEY_S, U's');
	CHECK(f.count("MSG_DO_STOP") == stops0 + 2); // the S key sends two: the CommandMap's STOP and the Stop button's own "&Stop" hotkey (both in the log)
	f.hud->controlBar().pressButton(stopSlot, stopInPalantir);
	f.frames(2);
	CHECK(f.count("MSG_DO_STOP") == stops0 + 3);
}

TEST_CASE("ownerfix1 cutscene input: a double click, a drag box and the hero bar select nothing while the input is off")
{
	if (!haveWorld("ownerfix1 selection paths"))
	{
		return;
	}
	SharedWorld &s = shared();
	HudFix f(s, "FactionAngmar");
	auto ctx = f.hud->enterContext();
	Object *a = f.makeInfantry(0.0f, 0.0f);
	Object *b = f.makeInfantry(40.0f, 0.0f);
	REQUIRE(a);
	REQUIRE(b);
	// the hero the hero bar lists
	const ThingTemplate *wk = s.world->things().findTemplate("AngmarWitchking");
	REQUIRE(wk);
	Object *hero = f.logic().newObject(wk, f.rig.local->getDefaultTeam(), ObjectStatusMaskType{});
	REQUIRE(hero);
	Coord3D at = *a->getPosition();
	at.x += 120.0f;
	at.z = f.logic().getGroundHeight(at.x, at.y);
	hero->setPosition(&at);
	hero->friend_onBuildComplete();
	f.frames(40);
	InGameHeroSelect *bar = f.hud->heroSelect();
	REQUIRE(bar);
	REQUIRE(bar->attached());
	f.rig.lookAt({ at.x - 60.0f, at.y, at.z }, 300.0f, 380.0f);
	f.frames(1);

	f.hud->scriptInput(false);
	f.frames(2);
	const size_t creates0 = f.count("MSG_CREATE_SELECTED_GROUP"), areas0 = f.count("MSG_AREA_SELECTION");
	const ICoord2D pa = f.rig.screenOf(HudFix::body(*a)), pb = f.rig.screenOf(HudFix::body(*b));
	f.press(pa, HudInput::Button::Left, true); // a double click
	f.hud->mouseMove(pa.x - 30, pa.y - 40, 0);
	f.hud->mouseButton(HudInput::Button::Left, true, pa.x - 30, pa.y - 40, 0, f.timeMs += 300);
	f.hud->mouseMove(pb.x + 30, pb.y + 40, 0);
	f.hud->mouseButton(HudInput::Button::Left, false, pb.x + 30, pb.y + 40, 0, f.timeMs += 300); // a drag box over both
	f.frames(2);
	bar->selectAllHeroes();
	bar->onButtonPressed("Hero1");
	f.frames(2);
	CHECK(f.ui().selected().empty());
	CHECK(f.rig.local->selection().empty());
	CHECK(f.count("MSG_CREATE_SELECTED_GROUP") == creates0);
	CHECK(f.count("MSG_AREA_SELECTION") == areas0);

	f.hud->scriptInput(true); // the control: the hero bar's select all works
	f.frames(2);
	bar->selectAllHeroes();
	f.frames(2);
	CHECK(f.ui().selected() == std::vector<ObjectID>{ hero->getID() });
	CHECK(f.rig.local->selection() == std::vector<ObjectID>{ hero->getID() });
}

// ---- the radar ------------------------------------------------------------------------------------------------------------------------------------------
TEST_CASE("ownerfix1 cutscene input: a radar click moves neither the view nor the selection while the input is off")
{
	if (!haveWorld("ownerfix1 radar"))
	{
		return;
	}
	SharedWorld &s = shared();
	HudFix f(s, "FactionMen");
	auto ctx = f.hud->enterContext();
	Object *u = f.makeInfantry(0.0f, 0.0f);
	REQUIRE(u);
	f.frames(40);
	REQUIRE(f.select(u));
	float sq[4];
	REQUIRE(f.hud->radarSquare(sq));
	const ICoord2D inRadar{ (int)((sq[0] + sq[2]) * 0.5f), (int)((sq[1] + sq[3]) * 0.5f) };
	const Coord3D home = f.rig.view.position();
	// the control: a left click looks there, a right click orders the selection there
	f.press(inRadar);
	const Coord3D moved = f.rig.view.position();
	CHECK(std::hypot(moved.x - home.x, moved.y - home.y) > 1.0f); // tolerance: the radar's world point is not the view's old target; the property is that it moved
	const size_t moves0 = f.count("MSG_DO_MOVETO");
	f.press(inRadar, HudInput::Button::Right);
	CHECK(f.count("MSG_DO_MOVETO") == moves0 + 1);

	f.hud->scriptInput(false);
	f.frames(2);
	f.rig.view.set({ home.x, home.y - 400.0f, home.z + 500.0f }, home);
	f.press(inRadar);
	f.press(inRadar, HudInput::Button::Right);
	const Coord3D after = f.rig.view.position();
	CHECK(after.x == home.x);
	CHECK(after.y == home.y);
	CHECK(f.count("MSG_DO_MOVETO") == moves0 + 1);
}

// ---- the disable edge -------------------------------------------------------------------------------------------------------------------------------------
TEST_CASE("ownerfix1 cutscene input: the disable edge ends a drag selection, clears the order modes, resets a queued order mode through the stream and drops the help provider")
{
	if (!haveWorld("ownerfix1 disable edge"))
	{
		return;
	}
	SharedWorld &s = shared();
	HudFix f(s, "FactionMen");
	auto ctx = f.hud->enterContext();
	Object *u = f.makeInfantry(0.0f, 0.0f);
	REQUIRE(u);
	f.frames(5);
	REQUIRE(f.select(u));
	// a queued order mode (Alt held in a game: MSG_CHANGE_ORDERMODE (1, 0)), as the logic keeps it
	GameMessage queued(MSG_CHANGE_ORDERMODE, f.rig.index());
	queued.appendIntegerArgument(1);
	queued.appendIntegerArgument(0);
	f.rig.game->commands().append(queued);
	f.frames(2);
	REQUIRE(f.rig.local->orderMode() == 1);
	f.ui().setForceAttackMode(true);
	f.ui().setForceMoveToMode(true);
	f.ui().setWaypointMode(true);
	f.ui().setPreferSelectionMode(true);
	f.ui().setSelecting(true);
	InGameHelpBox::Provider p;
	p.bar = 0;
	p.slot = 3;
	f.hud->helpBox().showHelp(p, 1000u, 400, []() { return std::shared_ptr<CommandButtonHelp>(); });
	REQUIRE(f.hud->helpBox().hasProvider());
	const size_t orders0 = f.count("MSG_CHANGE_ORDERMODE"), destroys0 = f.count("MSG_DESTROY_SELECTED_GROUP");
	const size_t logBefore = f.hud->input().messageLog().size();

	f.hud->scriptInput(false);
	// at once, with no frame run: the edge's effects and retail's order (RW 0x69B5ED: setSelecting, setRadiusCursorNone, the mode flags, MSG 0x469; then RW 0x7BC586:
	// the pointer, MSG 0x3EC and the deselect, the waypoint mode, the control bar's help provider RW 0x8076CD; no LookAt translator in this rig)
	CHECK_FALSE(f.ui().isSelecting());
	CHECK_FALSE(f.ui().isInForceAttackMode());
	CHECK_FALSE(f.ui().isInForceMoveToMode());
	CHECK_FALSE(f.ui().isInWaypointMode());
	CHECK_FALSE(f.ui().isInPreferSelectionMode());
	CHECK_FALSE(f.hud->helpBox().hasProvider());
	CHECK(f.hud->input().lastScriptSteps() == std::vector<std::string>{ "setSelecting", "setRadiusCursorNone", "modeFlags", "MSG_CHANGE_ORDERMODE", "mouseVisibility",
		"MSG_DESTROY_SELECTED_GROUP", "waypointMode", "controlBarHelpProvider" });
	f.frames(2);
	CHECK_FALSE(f.ui().isSelecting());
	CHECK_FALSE(f.ui().isInForceAttackMode());
	CHECK_FALSE(f.ui().isInForceMoveToMode());
	CHECK_FALSE(f.ui().isInWaypointMode());
	CHECK_FALSE(f.ui().isInPreferSelectionMode());
	CHECK_FALSE(f.hud->helpBox().hasProvider());
	// both messages came through the stream, in retail's order (0x469 from the edge, then 0x3EC), and the logic obeyed them
	CHECK(f.count("MSG_CHANGE_ORDERMODE") == orders0 + 1);
	CHECK(f.count("MSG_DESTROY_SELECTED_GROUP") == destroys0 + 1);
	const std::vector<std::string> &log = f.hud->input().messageLog();
	REQUIRE(log.size() == logBefore + 2);
	CHECK(log[logBefore].rfind("MSG_CHANGE_ORDERMODE", 0) == 0);
	CHECK(log[logBefore + 1].rfind("MSG_DESTROY_SELECTED_GROUP", 0) == 0);
	CHECK(f.rig.local->orderMode() == 0);
	CHECK(f.rig.local->selection().empty());
	CHECK(f.ui().selected().empty());
	// enabling again changes none of that back and sends nothing
	const size_t sent = f.hud->input().sentMessages();
	f.hud->scriptInput(true);
	f.frames(2);
	CHECK(f.hud->input().sentMessages() == sent);
}

// ---- the LookAt translator's flags ----------------------------------------------------------------------------------------------------------------------
TEST_CASE("ownerfix1 cutscene input: DISABLE_INPUT ends a camera scroll and a held rotation at once (RW 0x83AB5D)")
{
	if (!haveWorld("ownerfix1 lookat flags"))
	{
		return;
	}
	SharedWorld &s = shared();
	ArchiveW3DFileSource source(*s.mount->fs);
	WW3DAssetManager assets(source);
	LiveGame game(*s.world, *s.mount->fs, assets, s.options);
	LiveGame::Options o;
	o.mapName = "map mp fall back 4p";
	o.seed = 4711;
	o.slots.players.push_back({ "Player_1", "FactionMen", true, 0, 0, 0 });
	o.slots.players.push_back({ "Player_2", "FactionMordor", false, 1, 0, 1 });
	std::string err;
	REQUIRE_MESSAGE(game.load(o, &err), err);
	Player *local = game.players().findPlayerWithName("Player_1");
	REQUIRE(local);
	game.players().setLocalPlayer(local);
	CameraSettings gd;
	REQUIRE_MESSAGE(CameraSettings::load(*s.mount->fs, gd, &err), err);
	TacticalCamera cam(gd);
	cam.setViewport(1024, 768);
	const Waypoint *w = game.logic().terrain()->findWaypointByName("Player_1_Start");
	REQUIRE(w);
	const LoadedMap &lm = game.map();
	REQUIRE(lm.hasHeightMap);
	cam.startMap(game.logic(), lm.heightMap, lm.chunks.hasWorldInfo ? &lm.chunks.worldInfo : nullptr, w->location);
	HudInput input(game.logic(), &game.ai(), cam, game.commands(), s.mouse, s.meta);
	input.attachCamera(cam);
	unsigned nowMs = 1000;
	auto tick = [&](int n) {
		for (int i = 0; i < n; ++i)
		{
			input.update();
			nowMs += 33;
			input.cameraFrame(nowMs);
		}
	};
	constexpr int kKeyRight = 0xCD;
	// the arrow key scrolls the view
	input.key(kKeyRight, KEY_STATE_DOWN);
	tick(2);
	REQUIRE(input.lookAt()->isScrolling());
	REQUIRE(input.ui().isScrolling());
	// DISABLE_INPUT with the key still held: the control bar's reset (RW 0x8076CD) runs while the view still scrolls, the LookAt flags (RW 0x83AB5D) after it
	bool scrollingAtReset = false;
	input.scriptInput(false, [&]() {
		scrollingAtReset = input.lookAt()->isScrolling();
		input.noteScriptStep("controlBarHelpProvider");
	});
	CHECK(scrollingAtReset);
	CHECK_FALSE(input.lookAt()->isScrolling());
	CHECK_FALSE(input.ui().isScrolling());
	CHECK(input.lastScriptSteps() == std::vector<std::string>{ "setSelecting", "setRadiusCursorNone", "modeFlags", "MSG_CHANGE_ORDERMODE", "mouseVisibility",
		"MSG_DESTROY_SELECTED_GROUP", "waypointMode", "controlBarHelpProvider", "lookAtFlags" });
	const Coord3D stopped = cam.position();
	tick(5);
	CHECK(cam.position().x == stopped.x);
	CHECK(cam.position().y == stopped.y);
	input.key(kKeyRight, KEY_STATE_UP);
	input.scriptInput(true);
	// the numpad rotation (numpad 6) held: turned every tick until DISABLE_INPUT clears the flags
	const float a0 = cam.getAngle();
	input.key(LookAtTranslator::DIK_KP6, KEY_STATE_DOWN);
	tick(1);
	CHECK(cam.getAngle() != a0);
	input.scriptInput(false);
	tick(1);
	const float a1 = cam.getAngle();
	tick(4);
	CHECK(cam.getAngle() == a1);
}

// ---- the HUD's own wiring of that order -----------------------------------------------------------------------------------------------------------------
TEST_CASE("ownerfix1 cutscene input: InGameHud's DISABLE_INPUT drops the help provider before the LookAt flags end a scroll, in the call itself")
{
	if (!haveWorld("ownerfix1 hud lookat order"))
	{
		return;
	}
	SharedWorld &s = shared();
	ArchiveW3DFileSource source(*s.mount->fs);
	WW3DAssetManager assets(source);
	LiveGame game(*s.world, *s.mount->fs, assets, s.options);
	LiveGame::Options o;
	o.mapName = "map mp fall back 4p";
	o.seed = 4711;
	o.slots.players.push_back({ "Player_1", "FactionMen", true, 0, 0, 0 });
	o.slots.players.push_back({ "Player_2", "FactionMordor", false, 1, 0, 1 });
	std::string err;
	REQUIRE_MESSAGE(game.load(o, &err), err);
	Player *local = game.players().findPlayerWithName("Player_1");
	REQUIRE(local);
	game.players().setLocalPlayer(local);
	CameraSettings gd;
	REQUIRE_MESSAGE(CameraSettings::load(*s.mount->fs, gd, &err), err);
	TacticalCamera cam(gd);
	cam.setViewport(1024, 768);
	const Waypoint *w = game.logic().terrain()->findWaypointByName("Player_1_Start");
	REQUIRE(w);
	const LoadedMap &lm = game.map();
	REQUIRE(lm.hasHeightMap);
	cam.startMap(game.logic(), lm.heightMap, lm.chunks.hasWorldInfo ? &lm.chunks.worldInfo : nullptr, w->location);
	RecordingShellServices services;
	GameTextTableSource text;
	std::vector<std::uint8_t> bytes;
	REQUIRE_MESSAGE(s.mount->fs->readFile("data/lotr.str", bytes, &err), err);
	REQUIRE_MESSAGE(text.table.parse(bytes, &err), err);
	InGameHud::Config cfg{ game, *s.world, *s.mount->fs, services, cam, s.mouse, s.meta, &text };
	InGameHud hud(cfg);
	REQUIRE_MESSAGE(hud.boot(&err), err);
	hud.setWindowSize(1024, 768);
	hud.input().attachCamera(cam);
	auto ctx = hud.enterContext();
	auto tick = [&](int n) {
		for (int i = 0; i < n; ++i)
		{
			hud.update(0.1);
		}
	};
	constexpr int kKeyRight = 0xCD;
	hud.key(kKeyRight, KEY_STATE_DOWN);
	tick(2);
	REQUIRE(hud.input().lookAt()->isScrolling());
	InGameHelpBox::Provider p;
	p.bar = 0;
	p.slot = 3;
	hud.helpBox().showHelp(p, 1000u, 400, []() { return std::shared_ptr<CommandButtonHelp>(); });
	REQUIRE(hud.helpBox().hasProvider());
	hud.scriptInput(false);
	// at once: both reset, the help provider first
	CHECK_FALSE(hud.helpBox().hasProvider());
	CHECK_FALSE(hud.input().lookAt()->isScrolling());
	CHECK(hud.input().lastScriptSteps() == std::vector<std::string>{ "setSelecting", "setRadiusCursorNone", "modeFlags", "MSG_CHANGE_ORDERMODE", "mouseVisibility",
		"MSG_DESTROY_SELECTED_GROUP", "waypointMode", "controlBarHelpProvider", "lookAtFlags" });
}

// ---- replay / lockstep: what the HUD did is in the command stream, nothing else -------------------------------------------------------------------------------
TEST_CASE("ownerfix1 cutscene input: the commands of a selection, a queued order mode and DISABLE_INPUT, replayed from the command list into a fresh game, give the same state")
{
	if (!haveWorld("ownerfix1 replay"))
	{
		return;
	}
	SharedWorld &s = shared();
	struct Recorded
	{
		unsigned frame;
		GameMessage message;
	};
	std::vector<Recorded> recorded;
	std::uint32_t hashA = 0;
	unsigned startFrame = 0, endFrame = 0;
	Coord3D spot{};
	{
		HudFix a(s, "FactionMen");
		auto ctx = a.hud->enterContext();
		float mx = 0, my = 0;
		REQUIRE(a.logic().terrain()->getExtent(0, mx, my));
		spot = a.rig.freeSpot(mx * 0.5f, my * 0.5f, 200.0f);
		Object *u1 = a.rig.make(templates(s).infantry.front(), spot.x, spot.y);
		Object *u2 = a.rig.make(templates(s).infantry.front(), spot.x + 40.0f, spot.y);
		REQUIRE(u1);
		REQUIRE(u2);
		GameMessage queued(MSG_CHANGE_ORDERMODE, a.rig.index());
		queued.appendIntegerArgument(1);
		queued.appendIntegerArgument(0);
		a.rig.game->commands().append(queued);
		a.rig.game->advance(0.2);
		a.rig.game->advance(0.2);
		startFrame = a.logic().getFrame();
		a.hud->input().setLogicMessageObserver([&](const GameMessage &m) { recorded.push_back({ a.logic().getFrame(), m }); });
		REQUIRE(a.select(u1));
		a.hud->scriptInput(false);
		a.frames(6);
		endFrame = a.logic().getFrame();
		hashA = a.logic().computeStateHash();
		CHECK(a.rig.local->selection().empty());
		CHECK(a.rig.local->orderMode() == 0);
	}
	REQUIRE_FALSE(recorded.empty());
	bool sawOrder = false, sawDestroy = false;
	for (const Recorded &r : recorded)
	{
		sawOrder = sawOrder || r.message.getType() == MSG_CHANGE_ORDERMODE;
		sawDestroy = sawDestroy || r.message.getType() == MSG_DESTROY_SELECTED_GROUP;
	}
	CHECK(sawOrder);   // the edge's reset travelled as a command
	CHECK(sawDestroy); // and so did the deselect
	// a fresh game, no HUD: the same objects, the same two frames, then only the recorded commands at their frames
	Rig b(s);
	Object *v1 = b.make(templates(s).infantry.front(), spot.x, spot.y);
	Object *v2 = b.make(templates(s).infantry.front(), spot.x + 40.0f, spot.y);
	REQUIRE(v1);
	REQUIRE(v2);
	GameMessage queued(MSG_CHANGE_ORDERMODE, b.index());
	queued.appendIntegerArgument(1);
	queued.appendIntegerArgument(0);
	b.game->commands().append(queued);
	b.game->advance(0.2);
	b.game->advance(0.2);
	REQUIRE(b.logic().getFrame() == startFrame);
	while (b.logic().getFrame() < endFrame)
	{
		for (const Recorded &r : recorded)
		{
			if (r.frame == b.logic().getFrame())
			{
				b.game->commands().append(r.message);
			}
		}
		b.game->advance(0.2);
	}
	CHECK(b.logic().computeStateHash() == hashA);
	CHECK(b.local->selection().empty());
	CHECK(b.local->orderMode() == 0);
}


// ---- the first campaign mission, played ---------------------------------------------------------------------------------------------------------------
namespace
{
AptButtonInst *firstButtonUnder(AptCharacterInst *c)
{
	if (!c)
	{
		return nullptr;
	}
	if (AptButtonInst *b = c->asButton())
	{
		return b;
	}
	if (AptSpriteInst *sp = c->asSprite())
	{
		for (AptCharacterInst *k : sp->children())
		{
			if (AptButtonInst *b = firstButtonUnder(k))
			{
				return b;
			}
		}
	}
	return nullptr;
}

// a point the input routes to the button at `path` of `level`: the centre of its Hit records' area when that hits, else the hit point nearest it
bool hitPoint(InGameHud &hud, int level, const std::string &path, int &x, int &y)
{
	AptButtonInst *b = firstButtonUnder(hud.apt().resolvePath(hud.apt().level(level), path));
	float x0, y0, x1, y1;
	if (!b || !b->contentBounds(x0, y0, x1, y1) || !b->info())
	{
		return false;
	}
	bool any = false;
	float hx0 = 0, hy0 = 0, hx1 = 0, hy1 = 0;
	for (const AptButtonRecord &rec : b->info()->records)
	{
		if (!(rec.stateMask & 8))
		{
			continue;
		}
		const float cxs[4] = { x0, x1, x1, x0 }, cys[4] = { y0, y0, y1, y1 };
		for (int k = 0; k < 4; ++k)
		{
			const float px = rec.matrix[0] * cxs[k] + rec.matrix[2] * cys[k] + rec.translation[0];
			const float py = rec.matrix[1] * cxs[k] + rec.matrix[3] * cys[k] + rec.translation[1];
			hx0 = any ? std::min(hx0, px) : px;
			hy0 = any ? std::min(hy0, py) : py;
			hx1 = any ? std::max(hx1, px) : px;
			hy1 = any ? std::max(hy1, py) : py;
			any = true;
		}
	}
	if (any)
	{
		x0 = hx0, y0 = hy0, x1 = hx1, y1 = hy1;
	}
	float cx, cy, best = 0.0f;
	bool found = false;
	b->globalMatrix().apply((x0 + x1) / 2, (y0 + y1) / 2, cx, cy);
	for (int gy = 0; gy <= 16; ++gy)
	{
		for (int gx = 0; gx <= 16; ++gx)
		{
			float px, py;
			b->globalMatrix().apply(x0 + (x1 - x0) * (float)gx / 16.0f, y0 + (y1 - y0) * (float)gy / 16.0f, px, py);
			const float d = (px - cx) * (px - cx) + (py - cy) * (py - cy);
			if ((!found || d < best) && b->hitTest((float)(int)px, (float)(int)py) && hud.apt().input().hitTestButtons((float)(int)px, (float)(int)py) == b)
			{
				x = (int)px;
				y = (int)py;
				best = d;
				found = true;
			}
		}
	}
	return found;
}


// MAP ANG Angmar loaded as a campaign mission with the in-game HUD on it. The script requests the real game hands to the HUD (scripts/campaign_cine.gd: DISABLE_INPUT /
// ENABLE_INPUT) are handed over here the same way
struct CampaignHud
{
	SharedWorld &sh;
	ArchiveW3DFileSource source;
	WW3DAssetManager assets;
	std::unique_ptr<LiveGame> game;
	PinholeView view;
	RecordingShellServices services;
	GameTextTableSource text;
	std::unique_ptr<InGameHud> hud;
	int timeMs = 2000;
	int disables = 0, enables = 0;
	unsigned firstEnableFrame = 0;

	explicit CampaignHud(SharedWorld &s) : sh(s), source(*s.mount->fs), assets(source)
	{
		game = std::make_unique<LiveGame>(*s.world, *s.mount->fs, assets, s.options);
		LiveGame::Options o;
		o.mapName = "map ang angmar";
		o.campaign = true;
		std::string err;
		REQUIRE_MESSAGE(game->load(o, &err), err);
		view = PinholeView({ 0, -400, 500 }, { 0, 0, 0 }, 1024, 768, 0.7f);
		std::vector<std::uint8_t> bytes;
		REQUIRE_MESSAGE(s.mount->fs->readFile("data/lotr.str", bytes, &err), err);
		REQUIRE_MESSAGE(text.table.parse(bytes, &err), err);
		InGameHud::Config cfg{ *game, *s.world, *s.mount->fs, services, view, s.mouse, s.meta, &text };
		hud = std::make_unique<InGameHud>(cfg);
		REQUIRE_MESSAGE(hud->boot(&err), err);
		hud->setWindowSize(1024, 768);
		view.setScreen(1024, 768);
	}
	~CampaignHud() { hud.reset(); }
	Player *local() { return game->players().getLocalPlayer(); }
	void frames(int n)
	{
		for (int i = 0; i < n; ++i)
		{
			hud->update(0.1);
			game->advance(0.2);
			for (const ScriptClientRequest &r : game->takeScriptRequests())
			{
				if (r.action == "DISABLE_INPUT")
				{
					hud->scriptInput(false);
					++disables;
				}
				else if (r.action == "ENABLE_INPUT")
				{
					hud->scriptInput(true);
					firstEnableFrame = enables == 0 ? game->logic().getFrame() : firstEnableFrame;
					++enables;
				}
			}
		}
	}
	void clickAt(int x, int y)
	{
		hud->mouseMove(x, y);
		frames(2);
		hud->mouseButton(HudInput::Button::Left, true, x, y, 0, timeMs += 300);
		frames(2);
		hud->mouseButton(HudInput::Button::Left, false, x, y, 0, timeMs += 40);
		frames(4);
	}
};
} // namespace

// Problem 1 played as the owner plays it (the review: the earlier test called addSkillPoints). Retail facts, independent of the code under test:
//   * the first mission starts at rank 1 with 5 power points (Rank.ini: rank 1 SciencePurchasePointsGranted 5), no skill points;
//   * killing a level 1 MordorArcher awards 1 (data\ini\experiencelevels.ini EvilWeakLevel1 ExperienceAward = EXPERIENCE_AWARD_EVIL_WEAK_1, gamedata.ini: 1);
//   * the campaign's rank 2 needs 100 skill points (Rank.ini SkillPointsNeededCampaign) and grants one power point (PLAYER_PURCHASE_POINTS_GRANTED 1); rank 3
//     needs PLAYER_SKILL_POINTS_DELTA_CAMPAIGN (100, gamedata.ini) * 2 = 200 and grants one more, rank 4 needs 300;
//   * merge queue q18: the intro's fights earn 265.38 skill points on the merged tree (rank 3, 7 power points), measured; the lane's own tree (base b5c2095d) ended the
//     intro between 100 and 200 (rank 2, 6 points). The movement is already in the tree before DEVPORT-3C (measured on the OWNERFIX-1 merge alone) and is
//     not DEVPORT-3C's Horde provider (reverting it left 265.38): a simulation change of merge queues q16 / q17 moved the intro's fight, not attributed further;
//   * lane SUMMON-1: the intro's GoblinCaveTroll now starts throwing rocks as retail's does (scripts.lua OnTrollCreated grants Upgrade_SwitchToRockThrowing, its
//     DoCommandUpgrade runs Command_ToggleMountainTrollRockThrow at frame 0: measured by logging DoCommandUpgrade, and the intro reaches rank 3 again with
//     DoCommandUpgrade switched off), so the intro's fights earn 160 skill points: rank 2, 6 power points; merge queue q23 (TARGET-1, ATTACKPATH-1, FLANK-1): 214, rank 3;
//   * lane OFFMAP-1: that fight was the Black Numenorean counterattack the map places below its edge (y -78 .. -375), pulled onto the grid at frame 1 by
//     the port's idle state (ZH's cheat move, not in RotWK's RW 0x745A64). Kept off the map (OFF_MAP, as retail: the Witch-king ends the intro at level 1)
//     nobody fights in the intro: 0 skill points, rank 1, the 5 starting power points (measured), enough for Chill Wind;
//   * the store sells Chill Wind (AngmarSpellStoreCommandSet slot 2) for EVIL_RANK_1_COST 5; a cast of it makes a SpellBookChillWind (OCL_SpellBookChillWind).
TEST_CASE("ownerfix1 campaign powers played: the first Angmar mission's intro ends at rank 1 (its counterattack stays off the map), Powers opens by the Palantir's button, Chill Wind is bought by clicks, aimed and cast, and its object stands")
{
	if (!haveWorld("ownerfix1 campaign powers played"))
	{
		return;
	}
	SharedWorld &s = shared();
	CampaignHud c(s);
	auto ctx = c.hud->enterContext();
	Player *me = c.local();
	REQUIRE(me);
	PlayerScience &sc = me->science();
	CHECK(sc.getRankLevel() == 1);
	CHECK(sc.getSkillPoints() == 0.0f);
	CHECK(sc.getSciencePurchasePoints() == 5);
	// the intro: DISABLE_INPUT ... ENABLE_INPUT around the mission's own fights (the heroes against the Mordor archers); nothing of the player's input
	for (int i = 0; i < 400 && c.enables == 0; ++i)
	{
		c.frames(1);
	}
	CHECK(c.firstEnableFrame == 181u); // the intro script: DISABLE_INPUT at frame 3, ENABLE_INPUT at 181
	REQUIRE(c.enables == 1);
	CHECK(c.disables == 1);
	c.frames(20);
	CHECK(sc.getRankLevel() == 1);
	CHECK(sc.getSkillPoints() == 0.0f); // no kill in the intro
	const int points0 = sc.getSciencePurchasePoints();
	CHECK(points0 == 5); // the 5 at the start
	// three more kills by the Witch King: each exactly the data's award
	Object *wk = c.game->logic().scriptEngine().getUnitNamed("Witch King");
	REQUIRE(wk);
	int killed = 0;
	for (Object *o = c.game->logic().getFirstObject(); o && killed < 3; o = o->getNextObject())
	{
		if (o->isEffectivelyDead() || !o->getControllingPlayer() || me->getRelationship(o->getTeam()) != ENEMIES || o->getTemplate()->getName() != "MordorArcher")
		{
			continue;
		}
		const float before = sc.getSkillPoints();
		DamageInfo info;
		info.m_input.m_damageType = DAMAGE_UNRESISTABLE;
		info.m_input.m_sourceID = wk->getID();
		info.m_input.m_sourcePlayerMask = 1u << me->getPlayerIndex();
		info.m_input.m_amount = 100000.0f;
		o->getBodyModule()->attemptDamage(info);
		CHECK(sc.getSkillPoints() == before + 1.0f);
		++killed;
	}
	REQUIRE(killed == 3);
	CHECK(sc.getRankLevel() == 1); // 3 skill points: rank 2 needs 100
	CHECK(sc.getSciencePurchasePoints() == points0);
	// a campaign rank crossed (review of #73: the intro alone no longer reaches one): the Witch King kills the mission's enemy troops, each award the data's,
	// until the skill points pass the next rank's threshold (100 * the rank); the rank rises and grants its power point (PLAYER_PURCHASE_POINTS_GRANTED 1)
	static const int kInfantry = ObjectTemplateInfoBuilder::kindOfIndex("INFANTRY"), kCavalry = ObjectTemplateInfoBuilder::kindOfIndex("CAVALRY");
	const int rankBefore = sc.getRankLevel();
	const float threshold = 100.0f * (float)rankBefore;
	for (Object *o = c.game->logic().getFirstObject(); o && sc.getSkillPoints() < threshold; o = o->getNextObject())
	{
		if (o->isEffectivelyDead() || !o->getControllingPlayer() || me->getRelationship(o->getTeam()) != ENEMIES || !o->getBodyModule() ||
			!(o->isKindOf((unsigned)kInfantry) || o->isKindOf((unsigned)kCavalry)))
		{
			continue;
		}
		DamageInfo info;
		info.m_input.m_damageType = DAMAGE_UNRESISTABLE;
		info.m_input.m_sourceID = wk->getID();
		info.m_input.m_sourcePlayerMask = 1u << me->getPlayerIndex();
		info.m_input.m_amount = 100000.0f;
		o->getBodyModule()->attemptDamage(info);
	}
	REQUIRE(sc.getSkillPoints() >= threshold); // tolerance: the kill that crosses the threshold passes it by the rest of its award
	CHECK(sc.getRankLevel() == rankBefore + 1);
	const int points = sc.getSciencePurchasePoints();
	CHECK(points == points0 + 1);
	// Powers: the Palantir's button, by the mouse
	const int pal = c.hud->palantir()->level();
	int x = 0, y = 0;
	REQUIRE(hitPoint(*c.hud, pal, "PalantirButtons.Buttons.PlayerMagic", x, y));
	c.clickAt(x, y);
	c.frames(40);
	AptSpellStore *st = c.hud->spellStore();
	REQUIRE_MESSAGE(st, c.hud->spellStoreError());
	const int lvl = st->level();
	REQUIRE(c.hud->windows().aptText("APT:SpellStoreSpellPoints") != nullptr);
	CHECK(*c.hud->windows().aptText("APT:SpellStoreSpellPoints") == std::to_string(points)); // the points the screen shows are the ones the player earned (the rank crossed above: 6)
	REQUIRE(st->model().buttons().size() == 12u); // AngmarSpellStoreCommandSet: 12 powers
	CHECK(st->model().buttons()[1].scienceName == "SCIENCE_ChillWind");
	CHECK(st->model().buttons()[1].cost == 5);
	CHECK(AptSpellStore::stateName(st->buttonState(1)) == std::string("_active"));
	// the click on Chill Wind, then ACCEPT
	REQUIRE(hitPoint(*c.hud, lvl, "SpellStore.Buttons.Spell2", x, y));
	c.clickAt(x, y);
	c.frames(3);
	CHECK(AptSpellStore::stateName(st->buttonState(1)) == std::string("_already_purchased"));
	CHECK(*c.hud->windows().aptText("APT:SpellStoreSpellPoints") == std::to_string(points - 5));
	REQUIRE(hitPoint(*c.hud, lvl, "SpellStore.Buttons.ButtonsMain.Accept", x, y));
	c.clickAt(x, y);
	c.frames(40);
	CHECK(c.hud->spellStore() == nullptr);
	CHECK(sc.hasScience(TheScienceStore->getScienceFromInternalName("SCIENCE_ChillWind")));
	CHECK(sc.getSciencePurchasePoints() == points - 5);
	// the spell book shows it; the press aims; the click on the ground in front of the Witch King casts
	AptPalantir *p = c.hud->palantir();
	CHECK(std::string(AptPalantir::spellStateName(p->spellSlotState(0))) == "_up");
	const int winds0 = countTemplate(c.game->logic(), "SpellBookChillWind");
	REQUIRE(p->pressSpellSlot(0));
	REQUIRE(c.hud->spellBar().targeting());
	// the Witch King stands at the map's south edge, where the aim is red: the first spot inland of him the power may be cast on (RW 0x6A4260)
	Coord3D at = *wk->getPosition();
	bool found = false;
	for (const float dy : { 0.0f, 40.0f, 80.0f, 120.0f, 160.0f, 200.0f })
	{
		at = *wk->getPosition();
		at.x += 150.0f;
		at.y += dy;
		at.z = c.game->logic().getGroundHeight(at.x, at.y);
		if (c.hud->spellBar().targetVerdict(c.game->logic(), at) == InGameSpellBookModel::Target::VALID)
		{
			found = true;
			break;
		}
	}
	REQUIRE(found);
	c.view.set({ at.x, at.y - 420.0f, at.z + 520.0f }, at);
	c.frames(1);
	ICoord2D px{ -1, -1 };
	REQUIRE(c.view.worldToScreen(at, px));
	c.hud->mouseMove(px.x, px.y);
	c.frames(2);
	c.hud->mouseButton(HudInput::Button::Left, true, px.x, px.y, 0, c.timeMs += 300);
	c.hud->mouseButton(HudInput::Button::Left, false, px.x, px.y, 0, c.timeMs += 40);
	c.frames(10);
	CHECK_FALSE(c.hud->spellBar().targeting());
	CHECK(countTemplate(c.game->logic(), "SpellBookChillWind") == winds0 + 1);
	CHECK(std::string(AptPalantir::spellStateName(p->spellSlotState(0))) == "_notReady");
}
