// OpenBFME tests of lane BUILD-5 (the owner played preview.4): the building placement as RotWK's PlaceEventTranslator (RW 0x83E486) and placement update
// (RW 0x6A2AE5) do it, the right button's drag-or-click rule, right-drag scrolling during a placement, and the directional Scroll cursor. The retail tests
// SKIP loudly when ROTWK_INSTALL / BFME2_INSTALL are unset; the binary facts need RW_GAME_DAT. GPL-3.0.

#include "doctest.h"

#include "HudTestUtil.h"
#include "PeImage.h"

#include "GameClient/CameraSettings.h"
#include "GameClient/CursorFile.h"
#include "GameClient/InGameUI.h"
#include "GameClient/MessageStream/LookAtXlat.h"
#include "GameClient/MessageStream/PlaceEventTranslator.h"
#include "GameClient/PlacementGhost.h"
#include "GameClient/PlacementIcons.h"
#include "GameClient/Drawable.h"
#include "GameClient/DrawableManager.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DScriptedModelDraw.h"
#include "GameLogic/Map/CastleTemplates.h"
#include "GameLogic/AI/AIWorld.h"
#include "Libraries/WWVegas/WW3D2/w3dshadergen.h"
#include "Common/AsciiString.h"
#include "Common/ModelState.h"
#include "GameClient/TacticalCamera.h"
#include "GameLogic/BuildPlacement.h"
#include "GameLogic/Map/TerrainLogic.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>

using namespace hudtest;
using retailtest::PeImage;

namespace
{
// A retail game with the tactical camera and its LookAt translator in the stream (as test_camera.cpp's rig), the local player Men
struct PlaceRig
{
	SharedWorld &sh;
	ArchiveW3DFileSource source;
	WW3DAssetManager assets;
	std::unique_ptr<LiveGame> game;
	CameraSettings gd;
	std::unique_ptr<TacticalCamera> cam;
	std::unique_ptr<HudInput> input;
	Coord3D start;
	unsigned nowMs = 1000;
	int timeMs = 1000;

	DisplayedCameraView shown; // lane BUILD-5 r3: the device's view (the interpolated pose the player sees)
	explicit PlaceRig(SharedWorld &s, const char *faction = "FactionMen", const char *mapName = "map mp fall back 4p", bool displayedView = false)
		: sh(s)
		, source(*s.mount->fs)
		, assets(source)
	{
		game = std::make_unique<LiveGame>(*s.world, *s.mount->fs, assets, s.options);
		LiveGame::Options o;
		o.mapName = mapName;
		o.seed = 4711;
		o.slots.players.push_back({ "Player_1", faction, true, 0, 0, 0 });
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
		start = w->location;
		const LoadedMap &lm = game->map();
		REQUIRE(lm.hasHeightMap);
		cam->startMap(game->logic(), lm.heightMap, lm.chunks.hasWorldInfo ? &lm.chunks.worldInfo : nullptr, start);
		shown.cam = cam.get();
		shown.present(1024, 768, 1.0);
		input = std::make_unique<HudInput>(game->logic(), &game->ai(), displayedView ? static_cast<TacticalView &>(shown) : static_cast<TacticalView &>(*cam), game->commands(), s.mouse,
			s.meta);
		input->attachCamera(*cam);
		input->commandTranslator().setUseAlternateMouse(true); // RotWK's default (RW 0x642A4B)
	}
	~PlaceRig()
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
			shown.present(1024, 768, 1.0); // the displayed view at the client frame's end (a rig with displayedView projects through it)
		}
	}
	void move(int x, int y)
	{
		input->mouseMove(x, y, 0);
		input->update();
	}
	void button(HudInput::Button b, bool down, int x, int y, int heldMs = 20, int keyState = 0)
	{
		timeMs += heldMs;
		input->mouseButton(b, down, x, y, keyState, timeMs);
		input->update();
	}
	InGameUI &ui() { return input->ui(); }
	PlaceEventTranslator &place() { return input->placeTranslator(); }
	// a Porter of the local player near the start, selected by a click, and the barracks waiting for its site
	Object *startPlacing(const char *tmpl = "GondorBarracks", const char *builderName = "MenPorter")
	{
		Player *local = game->players().getLocalPlayer();
		std::string err;
		Object *porter = game->createObject(builderName, local->getPlayerIndex(), Coord3D{ start.x + 150.0f, start.y - 150.0f, 0.0f }, 0.0f, &err);
		REQUIRE_MESSAGE(porter != nullptr, err);
		cam->lookAt(*porter->getPosition());
		frame(20);
		ICoord2D pu{ -1, -1 };
		REQUIRE(cam->worldToScreen(*porter->getPosition(), pu));
		move(pu.x, pu.y);
		button(HudInput::Button::Left, true, pu.x, pu.y);
		button(HudInput::Button::Left, false, pu.x, pu.y);
		frame(2);
		REQUIRE(ui().getSelectCount() == 1);
		ui().placeBuildAvailable(tmpl, porter->getID());
		REQUIRE(ui().isPlacing());
		return porter;
	}
	size_t count(const char *prefix) const
	{
		size_t n = 0;
		for (const std::string &l : input->messageLog())
		{
			n += l.compare(0, std::strlen(prefix), prefix) == 0 ? 1 : 0;
		}
		return n;
	}
};

float f32At(const PeImage &pe, std::uint32_t va)
{
	const std::uint32_t u = pe.u32At(va);
	float f;
	std::memcpy(&f, &u, 4);
	return f;
}
} // namespace

TEST_CASE("build5 drag or click: RotWK's right click rule (dragged AND held DragToleranceMS is a drag, anything else a click) and the drag test")
{
	MouseSettings m;
	m.dragTolerance = 15;
	m.dragToleranceMS = 150;
	// RW 0x5ED4B9: beyond the tolerance on either axis, not at it
	CHECK_FALSE(m.isDragging({ 100, 100 }, { 115, 85 }));
	CHECK(m.isDragging({ 100, 100 }, { 116, 100 }));
	CHECK(m.isDragging({ 100, 100 }, { 100, 84 }));
	// RW 0x83E66C / 0x81FAE0: not dragged -> a click however long it was held
	CHECK(m.isRightClick(false, 1000, 1000));
	CHECK(m.isRightClick(false, 1000, 5000));
	// dragged but released within DragToleranceMS -> still a click ("we consider it a click, even if the mouse is moving")
	CHECK(m.isRightClick(true, 1000, 1149));
	// dragged and held DragToleranceMS or longer -> a drag (RW 0x83E698: cmp eax, [Mouse + 0x12F0]; jae: no cancel)
	CHECK_FALSE(m.isRightClick(true, 1000, 1150));
	CHECK_FALSE(m.isRightClick(true, 1000, 3000));
	// the difference is unsigned (a clock that wrapped still measures the hold)
	CHECK_FALSE(m.isRightClick(true, 0xFFFFFF00u, 0x00000100u));
}

TEST_CASE("build5 scroll cursor: the frame of the directional cursor faces the scroll amount, and its files are named as Win32Mouse loads them")
{
	// W3DMouse::setCursorDirection (RW 0x498B1A): frame 0 points right, clockwise on the screen (y down)
	CHECK(ScrollCursorDirection(10.0f, 0.0f, 8, true) == 0);
	CHECK(ScrollCursorDirection(7.0f, 7.0f, 8, true) == 1);
	CHECK(ScrollCursorDirection(0.0f, 3.0f, 8, true) == 2);
	CHECK(ScrollCursorDirection(-5.0f, 5.0f, 8, true) == 3);
	CHECK(ScrollCursorDirection(-1.0f, 0.0f, 8, true) == 4);
	CHECK(ScrollCursorDirection(-2.0f, -2.0f, 8, true) == 5);
	CHECK(ScrollCursorDirection(0.0f, -9.0f, 8, true) == 6);
	CHECK(ScrollCursorDirection(4.0f, -4.0f, 8, true) == 7);
	// just below a full turn rounds to 8, which is frame 0
	CHECK(ScrollCursorDirection(100.0f, -1.0f, 8, true) == 0);
	// one direction, not scrolling, no amount: frame 0
	CHECK(ScrollCursorDirection(0.0f, 3.0f, 1, true) == 0);
	CHECK(ScrollCursorDirection(0.0f, 3.0f, 8, false) == 0);
	CHECK(ScrollCursorDirection(0.0f, 0.0f, 8, true) == 0);
	// Win32Mouse::initCursorResources (RW 0x441506): ".cur" stripped (any case) -> .cur, else .ani; "<stem><n>" per direction
	CHECK(CursorFileName("SCCScroll.cur", 8, 0) == "SCCScroll0.cur");
	CHECK(CursorFileName("SCCScroll.cur", 8, 7) == "SCCScroll7.cur");
	CHECK(CursorFileName("SCCPointer.CUR", 1, 0) == "SCCPointer.cur");
	CHECK(CursorFileName("SCCMove", 0, 0) == "SCCMove.ani");
	CHECK(CursorFileName("SCCAttack", 1, 0) == "SCCAttack.ani");
	// the placement cursors: no Mouse.ini block for Build / InvalidBuild in retail, so InGameUI chooses names the device shows as no pointer
	InGameUI ui;
	ui.placeBuildAvailable("AnyBuilding", 7);
	CHECK(ui.cursor() == std::string(MouseCursorName::Target));
	CHECK(ui.mouseMode() == 1);
	ui.setScrolling(true);
	CHECK(ui.cursor() == std::string(MouseCursorName::Scroll));
	ui.hintCursor(MouseCursorName::Build); // RW 0x69BD53: no change while scrolling
	CHECK(ui.cursor() == std::string(MouseCursorName::Scroll));
	ui.setScrolling(false);
	CHECK(ui.cursor() == std::string(MouseCursorName::Arrow));
	ui.hintCursor(MouseCursorName::Build);
	CHECK(ui.cursor() == std::string(MouseCursorName::Build));
	ui.placeBuildAvailable(std::string(), INVALID_ID);
	CHECK(ui.cursor() == std::string(MouseCursorName::Arrow));
	CHECK(ui.mouseMode() == 0);
}

TEST_CASE("build5 retail cursors: Mouse.ini's Scroll has 8 directions whose files ship and decode; Build and InvalidBuild have no block")
{
	const char *install = std::getenv("ROTWK_INSTALL");
	if (!install || !*install || !haveWorld("build5 retail cursors"))
	{
		return;
	}
	SharedWorld &s = shared();
	std::vector<std::uint8_t> ini;
	std::string error;
	REQUIRE(s.mount->fs->readFile("data/ini/mouse.ini", ini, &error));
	std::map<std::string, MouseCursorEntry> cursors;
	REQUIRE_MESSAGE(ParseMouseCursors(std::string(ini.begin(), ini.end()), cursors, &error), error);
	REQUIRE(cursors.count("Scroll") == 1);
	CHECK(cursors["Scroll"].directions == 8);
	CHECK(cursors.count(MouseCursorName::Build) == 0);
	CHECK(cursors.count(MouseCursorName::InvalidBuild) == 0);
	REQUIRE(cursors.count(MouseCursorName::Target) == 1);
	const std::string dir = std::string(install) + "/data/cursors";
	for (int d = 0; d < 8; ++d)
	{
		const std::string path = FindFileNoCase(dir, CursorFileName(cursors["Scroll"].image, 8, d));
		REQUIRE_MESSAGE(!path.empty(), "direction " << d);
		std::ifstream f(path, std::ios::binary);
		const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
		CursorImage img;
		CHECK_MESSAGE(DecodeCursorFile(bytes, img, &error), path << ": " << error);
		CHECK(img.width > 0);
	}
	const std::string target = FindFileNoCase(dir, CursorFileName(cursors[MouseCursorName::Target].image, 1, 0));
	CHECK(!target.empty());
}

TEST_CASE("build5 binary facts: the placement translator, the placement update, the cursor table and the bib in RotWK")
{
	const PeImage *pe = PeImage::fromEnvironment();
	if (!pe)
	{
		retailtest::printSkip("build5 binary facts (RW_GAME_DAT unset)");
		return;
	}
	auto bytes = [&](std::uint32_t va, size_t n) {
		std::vector<std::uint8_t> out;
		REQUIRE(pe->read(va, n, &out));
		return out;
	};
	// RW 0x83E698: the right click's hold against Mouse + 0x12F0 (DragToleranceMS); jae skips the cancel
	CHECK(bytes(0x83E698, 6) == std::vector<std::uint8_t>{ 0x3B, 0x81, 0xF0, 0x12, 0x00, 0x00 });
	// RW 0x83E607: the anchor's 5 pixel distance
	CHECK(f32At(*pe, 0xBDAE58) == 5.0f);
	// the ghost's legality options 0x48F (RW 0x6A39B1) on odd client frames (RW 0x6A393D test al, 1) and the click's 0x49F (RW 0x83E815)
	CHECK(bytes(0x6A39B1, 5) == std::vector<std::uint8_t>{ 0x68, 0x8F, 0x04, 0x00, 0x00 });
	CHECK(bytes(0x6A393D, 2) == std::vector<std::uint8_t>{ 0xA8, 0x01 });
	CHECK(bytes(0x83E815, 5) == std::vector<std::uint8_t>{ 0x68, 0x9F, 0x04, 0x00, 0x00 });
	CHECK(PlaceEventTranslator::ghostOptions() == 0x0Fu);
	CHECK(PlaceEventTranslator::placeOptions() == 0x1Fu);
	// the colours: IllegalBuildColor (1, 0, 0), the code 9 pulse's blue 0.5 / 1.7, the opacity 0.45
	CHECK(f32At(*pe, 0xDA0B28) == 1.0f);
	CHECK(f32At(*pe, 0xDA0B2C) == 0.0f);
	CHECK(f32At(*pe, 0xDA0B30) == 0.0f);
	CHECK(f32At(*pe, 0xBD869C) == 0.5f);
	CHECK(f32At(*pe, 0xC1348C) == doctest::Approx(1.7f));
	CHECK(f32At(*pe, 0xC12408) == doctest::Approx(0.45f));
	// drawPlaceAngle (RW 0x48EAE9: mov edi, 0xBDDBB0) creates W_Arrow for the anchor and the arrow
	CHECK(bytes(0x48EAE9, 5) == std::vector<std::uint8_t>{ 0xBF, 0xB0, 0xDB, 0xBD, 0x00 });
	CHECK(pe->cstring(0xBDDBB0) == "W_Arrow");
	// the cursor name table RW 0xD9E108: None 0, Normal 1, Arrow 2, Scroll 3, Target 4 ... Build 10, InvalidBuild 11, GenericInvalid 12
	const char *names[] = { "None", "Normal", "Arrow", "Scroll", "Target", "Move", "AttackMove", "AttackObj", "ForceAttackObj", "ForceAttackGround", "Build", "InvalidBuild", "GenericInvalid" };
	for (std::uint32_t i = 0; i < 13; ++i)
	{
		CHECK_MESSAGE(pe->cstring(pe->u32At(0xD9E108 + 4 * i)) == names[i], i);
	}
	// placeBuildAvailable's mode cursor: edi = 4 (RW 0x69C698 push edi; RW 0x69C69B call setMouseCursor)
	CHECK(bytes(0x69C698, 2) == std::vector<std::uint8_t>{ 0x57, 0x8B });
	// the Win32 cursor file formats (RW 0x441506)
	CHECK(pe->cstring(0xBD84B4) == "data\\cursors\\%s%d.%s");
	CHECK(pe->cstring(0xBD84A0) == "data\\cursors\\%s.%s");
	CHECK(pe->cstring(0xBD84D0) == ".cur");
	// MiscAudio's field row 58 (table RW 0xBF5450, 16 bytes a row): BadBuildingPlacementSound at + 0xE8, the sound of a refused click (RW 0x83EACB add eax, 0xE8)
	CHECK(pe->cstring(pe->u32At(0xBF5450 + 16 * 58)) == "BadBuildingPlacementSound");
	CHECK(pe->u32At(0xBF5450 + 16 * 58 + 12) == 0xE8u);
	CHECK(bytes(0x83EACB, 5) == std::vector<std::uint8_t>{ 0x05, 0xE8, 0x00, 0x00, 0x00 });
	// the bib textures (W3DBibBuffer)
	CHECK(pe->cstring(0xBE4358) == "TBRedBib.tga");
	CHECK(pe->cstring(0xBE4368) == "TBBib.tga");
	CHECK(std::string(PlacementGhost::bibTextureFor(0)).empty());
	CHECK(std::string(PlacementGhost::bibTextureFor(9)) == "TBBib.tga");
	CHECK(std::string(PlacementGhost::bibTextureFor(3)) == "TBRedBib.tga");
}

TEST_CASE("build5 placement with a scroll: a right drag held past DragToleranceMS scrolls the view and keeps the placement; the ghost follows the pointer")
{
	if (!haveWorld("build5 placement scroll"))
	{
		return;
	}
	PlaceRig r(shared());
	r.frame(5);
	r.startPlacing();
	CHECK(r.ui().cursor() == std::string(MouseCursorName::Target)); // RW 0x69C69B
	const int tol = shared().mouse.dragTolerance, holdMs = shared().mouse.dragToleranceMS;
	REQUIRE(tol > 0);
	REQUIRE(holdMs > 0);
	r.move(500, 400);
	r.frame(2);
	REQUIRE(r.ui().placeHasGhost());
	// the builder is selected: the move hint over the ground shows Build while placing (createCommandHint RW 0x6A3E2A)
	CHECK(r.ui().cursor() == std::string(MouseCursorName::Build));
	const Coord3D ghost0 = r.ui().placeLocation();
	const Coord3D cam0 = r.cam->position();
	// the right press is not eaten: the LookAt translator arms its scroll
	r.button(HudInput::Button::Right, true, 500, 400);
	CHECK(r.input->lookAt()->isArmed());
	CHECK(r.place().rightDown());
	CHECK(r.ui().isPlacing());
	// drag beyond the tolerance: the scroll starts (type 1), the cursor is Scroll, facing right
	r.move(500 + tol + 60, 400);
	CHECK(r.place().rightDragged());
	REQUIRE(r.input->lookAt()->isScrolling());
	CHECK(r.input->lookAt()->scrollType() == LookAtTranslator::SCROLL_RMB);
	CHECK(r.ui().cursor() == std::string(MouseCursorName::Scroll));
	r.frame(6);
	CHECK(r.ui().scrollAmountX() > 0.0f);
	CHECK(r.ui().scrollAmountY() == 0.0f);
	CHECK(ScrollCursorDirection(r.ui().scrollAmountX(), r.ui().scrollAmountY(), 8, r.ui().isScrolling()) == 0);
	// the hints change nothing while scrolling
	r.move(500 + tol + 61, 400);
	CHECK(r.ui().cursor() == std::string(MouseCursorName::Scroll));
	const Coord3D cam1 = r.cam->position();
	CHECK(std::fabs(cam1.x - cam0.x) + std::fabs(cam1.y - cam0.y) > 10.0f);
	// the ghost stands under the pointer, re-projected as the view moved
	const Coord3D ghost1 = r.ui().placeLocation();
	CHECK(std::fabs(ghost1.x - ghost0.x) + std::fabs(ghost1.y - ghost0.y) > 10.0f);
	// the release, held well past DragToleranceMS: the right click MetaEvent derives is a drag; the placement stays and nothing is ordered
	r.button(HudInput::Button::Right, false, 500 + tol + 61, 400, holdMs + 200);
	CHECK_FALSE(r.input->lookAt()->isScrolling());
	CHECK(r.ui().isPlacing());
	CHECK(r.place().stats().cancelled == 0);
	CHECK(r.place().stats().rightDrags == 1);
	CHECK(r.ui().cursor() == std::string(MouseCursorName::Arrow)); // setScrolling(false) (RW 0x69B2BD)
	CHECK(r.count("MSG_DO_MOVETO") == 0);
	r.frame(2);
	CHECK(r.ui().scrollAmountX() == 0.0f); // the next tick hands a zero amount (not scrolling)
	// a pointer still: the ghost keeps following it while an arrow key scrolls the view
	const Coord3D ghost2 = r.ui().placeLocation();
	r.input->key(0xCD, 2); // right arrow (DIK 0xCD) down
	r.frame(6);
	r.input->key(0xCD, 1);
	r.frame(1);
	const Coord3D ghost3 = r.ui().placeLocation();
	CHECK(std::fabs(ghost3.x - ghost2.x) + std::fabs(ghost3.y - ghost2.y) > 10.0f);
	CHECK(r.ui().isPlacing());
	// a plain right click (no drag) cancels (RW 0x83E66C) and the click orders nothing
	r.move(520, 420);
	r.button(HudInput::Button::Right, true, 520, 420);
	r.button(HudInput::Button::Right, false, 520, 420);
	CHECK_FALSE(r.ui().isPlacing());
	CHECK(r.place().stats().cancelled == 1);
	CHECK(r.ui().cursor() == std::string(MouseCursorName::Arrow));
	CHECK(r.count("MSG_DO_MOVETO") == 0);
	// a quick right drag (released within DragToleranceMS) is still a click: it scrolls while held and cancels at the release
	r.ui().placeBuildAvailable("GondorBarracks", r.ui().firstSelected());
	r.button(HudInput::Button::Right, true, 520, 420);
	r.move(520, 420 + tol + 40);
	CHECK(r.input->lookAt()->isScrolling());
	r.button(HudInput::Button::Right, false, 520, 420 + tol + 40, holdMs / 3);
	CHECK_FALSE(r.ui().isPlacing());
	CHECK(r.place().stats().cancelled == 2);
}

TEST_CASE("build5 placement: the left press anchors, a 5 pixel drag turns the ghost, the click places at the anchor with that angle and always ends the mode")
{
	if (!haveWorld("build5 placement rotate"))
	{
		return;
	}
	PlaceRig r(shared());
	r.frame(5);
	r.startPlacing();
	// a free site east of the porter
	const ICoord2D at{ 640, 420 };
	r.move(at.x, at.y);
	r.frame(2);
	REQUIRE(r.ui().placeHasGhost());
	r.button(HudInput::Button::Left, true, at.x, at.y);
	CHECK(r.ui().isPlacementAnchored());
	const float a0 = r.ui().placeAngle();
	// within 5 pixels: no end, no turn
	r.move(at.x + 3, at.y + 3);
	r.frame(1);
	CHECK(r.ui().placementEnd().x == at.x);
	CHECK(r.ui().placeAngle() == a0);
	// 30 pixels down the screen: the ghost turns from the anchor's terrain point to the pointer's; it stays at the anchor
	r.move(at.x, at.y + 30);
	r.frame(1);
	CHECK(r.ui().placementEnd().y == at.y + 30);
	const float a1 = r.ui().placeAngle();
	CHECK(a1 != a0);
	Coord3D anchorWorld;
	REQUIRE(r.cam->screenToTerrain(at, r.game->logic(), anchorWorld));
	CHECK(r.ui().placeLocation().x == doctest::Approx(anchorWorld.x).epsilon(0.001));
	CHECK(r.ui().placeLocation().y == doctest::Approx(anchorWorld.y).epsilon(0.001));
	// the release's click places it (with shift held: RotWK has no shift to keep placing)
	const size_t before = r.count("MSG_DOZER_CONSTRUCT");
	r.button(HudInput::Button::Left, false, at.x, at.y + 30, 20, KEY_STATE_LSHIFT);
	const LegalBuildCode code = (LegalBuildCode)r.ui().placeLegalCode();
	if (r.place().stats().refused == 0)
	{
		CHECK(r.count("MSG_DOZER_CONSTRUCT") == before + 1);
		CHECK_FALSE(r.ui().isPlacing());
		CHECK(r.ui().cursor() == std::string(MouseCursorName::Arrow));
	}
	else
	{
		FAIL_CHECK("the site was refused: " << BuildPlacement::codeName(code));
	}
}

TEST_CASE("build5 placement: a refused site shows the code's label, plays BadBuildingPlacementSound, drops the anchor and keeps the mode")
{
	if (!haveWorld("build5 placement refused"))
	{
		return;
	}
	PlaceRig r(shared());
	r.frame(5);
	Object *porter = r.startPlacing();
	// on a farm beside the porter: objects in the way (the builder itself never blocks its own site)
	std::string err;
	Object *farm = r.game->createObject("GondorFarm", r.game->players().getLocalPlayer()->getPlayerIndex(),
		Coord3D{ porter->getPosition()->x + 120.0f, porter->getPosition()->y, 0.0f }, 0.0f, &err);
	REQUIRE_MESSAGE(farm != nullptr, err);
	ICoord2D pu{ -1, -1 };
	REQUIRE(r.cam->worldToScreen(*farm->getPosition(), pu));
	r.move(pu.x, pu.y);
	r.frame(2);
	r.ui().takeMessages();
	r.button(HudInput::Button::Left, true, pu.x, pu.y);
	r.button(HudInput::Button::Left, false, pu.x, pu.y);
	CHECK(r.place().stats().refused == 1);
	CHECK(r.ui().isPlacing());
	CHECK_FALSE(r.ui().isPlacementAnchored());
	const std::vector<std::string> msgs = r.ui().takeMessages();
	REQUIRE(msgs.size() == 1);
	CHECK(msgs[0] == PlaceEventTranslator::cantBuildLabel(r.ui().placeLegalCode()));
	CHECK(msgs[0] != "GUI:CantBuildThere");
	const std::vector<std::string> sounds = r.ui().takeMiscSounds();
	REQUIRE(sounds.size() == 1);
	CHECK(sounds[0] == "BadBuildingPlacementSound");
	CHECK(r.count("MSG_DOZER_CONSTRUCT") == 0);
	// the labels of RW 0x69CCEC by meaning
	CHECK(std::string(PlaceEventTranslator::cantBuildLabel(LBC_OBJECTS_IN_THE_WAY)) == "GUI:CantBuildObjectsInTheWay");
	CHECK(std::string(PlaceEventTranslator::cantBuildLabel(LBC_SHROUD)) == "GUI:CantBuildShroud");
	CHECK(std::string(PlaceEventTranslator::cantBuildLabel(LBC_OK)) == "GUI:CantBuildThere");
	// the bib of the barracks: a rectangle about the ghost
	const ThingTemplate *tt = r.game->logic().things().findTemplate("GondorBarracks");
	REQUIRE(tt != nullptr);
	const PlacementGhost::Bib bib = PlacementGhost::bibOf(*tt);
	REQUIRE_MESSAGE(bib.valid, bib.error);
	CHECK(bib.x0 < 0.0f);
	CHECK(bib.x1 > 0.0f);
	CHECK(bib.y0 < 0.0f);
	CHECK(bib.y1 == -bib.y0);
	// retail GondorBarracks: a cylinder of 8 and boxes 20 x 20 at (-22, -30) and 45 x 50 at 0: half extents 45 (x) and 50 (y), the same extra width on both
	CHECK(-bib.x0 - bib.y1 == doctest::Approx(45.0f - 50.0f));
	CHECK(bib.y1 >= 50.0f);
}

TEST_CASE("build5 r2 pointer alignment: with the device's order (camera frame, then the placement step) the ghost stands under the pointer while the view scrolls")
{
	if (!haveWorld("build5 pointer alignment"))
	{
		return;
	}
	PlaceRig r(shared());
	r.frame(5);
	r.startPlacing();
	r.input->setPlacementDeferred(true); // InGameHudNode: the placement step after the displayed camera
	// the device's render frame: the HUD's update (input), the camera's client frame, then the placement step
	auto deviceFrame = [&]() {
		r.input->update();
		r.nowMs += 33;
		r.input->cameraFrame(r.nowMs);
		r.input->placementUpdate();
	};
	auto misalignment = [&](const ICoord2D &pointer) {
		ICoord2D at{ -9999, -9999 };
		REQUIRE(r.cam->worldToScreen(r.ui().placeLocation(), at));
		return std::abs(at.x - pointer.x) + std::abs(at.y - pointer.y);
	};
	const ICoord2D p{ 520, 380 };
	r.input->mouseMove(p.x, p.y, 0);
	deviceFrame();
	deviceFrame();
	REQUIRE(r.ui().placeHasGhost());
	CHECK(misalignment(p) <= 1);
	// a right drag scrolls; the pointer holds still at the drag's end
	r.button(HudInput::Button::Right, true, p.x, p.y);
	const ICoord2D q{ p.x + 120, p.y + 40 };
	r.input->mouseMove(q.x, q.y, 0);
	int worst = 0, frames = 0;
	Coord3D cam0 = r.cam->position();
	for (int i = 0; i < 12; ++i)
	{
		deviceFrame();
		REQUIRE(r.input->lookAt()->isScrolling());
		worst = std::max(worst, misalignment(q));
		++frames;
	}
	const Coord3D cam1 = r.cam->position();
	CHECK(std::fabs(cam1.x - cam0.x) + std::fabs(cam1.y - cam0.y) > 50.0f);
	CHECK_MESSAGE(worst <= 1, "the ghost lagged " << worst << " pixels behind the pointer over " << frames << " scrolling frames");
	r.timeMs += 400;
	r.button(HudInput::Button::Right, false, q.x, q.y);
	CHECK(r.ui().isPlacing());
	// the order before (the step inside update, before the camera moved): the ghost trails by a frame's scroll
	r.input->setPlacementDeferred(false);
	r.button(HudInput::Button::Right, true, q.x, q.y);
	const ICoord2D q2{ q.x + 120, q.y };
	r.input->mouseMove(q2.x, q2.y, 0);
	int lag = 0;
	for (int i = 0; i < 6; ++i)
	{
		r.frame(1);
		lag = std::max(lag, misalignment(q2));
	}
	CHECK_MESSAGE(lag > 1, "the old order lagged " << lag << " pixels");
	r.timeMs += 400;
	r.button(HudInput::Button::Right, false, q2.x, q2.y);
}

TEST_CASE("build5 r2 ghost drawables: the builder's colour, opacity 0.45, BUILD_PLACEMENT_CURSOR and the draw script's hidden sub objects")
{
	if (!haveWorld("build5 ghost drawables"))
	{
		return;
	}
	PlaceRig r(shared());
	r.frame(5);
	Object *porter = r.startPlacing();
	r.move(520, 380);
	r.frame(2);
	REQUIRE(r.ui().placeHasGhost());
	PlacementIcons icons(r.game->drawables().services(), r.game->logic());
	icons.update(r.ui(), 33.0, PlaceEventTranslator::ghostOptions());
	REQUIRE_MESSAGE(icons.errors().empty(), icons.errors().front());
	REQUIRE(icons.icons().size() == 1);
	const PlacementIcons::Icon &ic = icons.icons()[0];
	const Drawable &d = *ic.drawable;
	// RW 0x69C731: the builder's player colour (RW 0x674504)
	const Player *owner = porter->getControllingPlayer();
	CHECK(d.getHouseColor() == (r.game->logic().settings().night ? owner->getPlayerNightColor() : owner->getPlayerColor()));
	CHECK(d.getHouseColor() != 0u);
	CHECK(d.drawOpacity() == doctest::Approx(0.45f)); // RW 0xC12408
	const int bit = ModelCondition::indexOf("BUILD_PLACEMENT_CURSOR");
	REQUIRE(bit >= 0);
	CHECK(d.getModelConditionFlags().test((size_t)bit));
	// at the ghost's site and angle
	CHECK(d.getPosition()->x == r.ui().placeLocation().x);
	CHECK(d.getPosition()->y == r.ui().placeLocation().y);
	// the draw module's BeginScript ran (the drawable Lua host): GondorBarracks' BUILD_PLACEMENT_CURSOR state hides N_Window, V1, V1FLAG, V2, V2A
	std::vector<std::string> hidden;
	for (const DrawEntry &e : d.entries())
	{
		if (e.kind == W3D_DRAWKIND_MODEL && e.draw)
		{
			W3DDrawPoseView f;
			e.draw->poseView(f);
			REQUIRE(f.model != nullptr);
			for (int h : *f.hiddenSubObjects)
			{
				std::string n = f.model->SubObjects[(size_t)h].Name;
				for (char &c : n)
				{
					c = (char)std::toupper((unsigned char)c);
				}
				hidden.push_back(n);
			}
		}
	}
	// every name the script hides that the model has as a sub object is hidden (a name the model lacks hides nothing, as in retail)
	const PlacementGhost::Look look = PlacementGhost::lookOf(*ic.tt);
	REQUIRE(!look.hidden.empty());
	std::vector<std::string> all;
	for (const DrawEntry &e : d.entries())
	{
		if (e.kind == W3D_DRAWKIND_MODEL && e.draw)
		{
			W3DDrawPoseView f;
			e.draw->poseView(f);
			for (const auto &sub : f.model->SubObjects)
			{
				std::string n = sub.Name;
				for (char &c : n)
				{
					c = (char)std::toupper((unsigned char)c);
				}
				all.push_back(n);
			}
		}
	}
	auto matches = [](const std::string &n, const std::string &h) {
		return n == h || (n.size() > h.size() && n.compare(n.size() - h.size(), h.size(), h) == 0 && n[n.size() - h.size() - 1] == '.');
	};
	size_t present = 0;
	for (std::string h : look.hidden)
	{
		for (char &c : h)
		{
			c = (char)std::toupper((unsigned char)c);
		}
		bool inModel = false, isHidden = false;
		for (const std::string &n : all)
		{
			inModel = inModel || matches(n, h);
		}
		for (const std::string &n : hidden)
		{
			isHidden = isHidden || matches(n, h);
		}
		present += inModel ? 1 : 0;
		CHECK_MESSAGE(isHidden == inModel, "the script's " << h << ": in the model " << inModel << ", hidden " << isHidden);
	}
	CHECK(present > 0);
	CHECK(hidden.size() >= present);
	// a legal site: no tint, no bib; the code is the translator's
	icons.update(r.ui(), 33.0, PlaceEventTranslator::ghostOptions());
	if (r.ui().clientFrame() & 1)
	{
		CHECK(icons.icons()[0].code == r.ui().placeLegalCode());
	}
	// the placement ends: the drawables go
	r.ui().placeBuildAvailable(std::string(), INVALID_ID);
	icons.update(r.ui(), 33.0, PlaceEventTranslator::ghostOptions());
	CHECK(icons.icons().empty());
}

TEST_CASE("build5 r2 fortress plots: a castle's ghost judges only its BASE_SITE slots (RW 0x6A3993), each at its own site with the ghost's angle")
{
	if (!haveWorld("build5 fortress plots"))
	{
		return;
	}
	struct Case
	{
		const char *faction, *builder, *fortress;
	};
	// a good and an evil fortress, each built by its faction's worker
	for (const Case &c : { Case{ "FactionMen", "MenPorter", "MenFortress" }, Case{ "FactionMordor", "MordorWorker", "MordorFortress" } })
	{
		CAPTURE(c.fortress);
		PlaceRig r(shared(), c.faction);
		r.frame(5);
		Object *builder = r.startPlacing(c.fortress, c.builder);
		CastleTemplateStore castles;
		castles.setLoader(CastleTemplateStore::fileSystemLoader(*shared().mount->fs));
		PlacementIcons icons(r.game->drawables().services(), r.game->logic());
		icons.setCastleStore(&castles);
		size_t judgedSlots = 0;
		for (int k = 0; k < 4; ++k)
		{
			const Coord3D site{ r.start.x + 80.0f * k, r.start.y - 250.0f + 60.0f * k, 0.0f };
			ICoord2D px{ 0, 0 };
			r.cam->lookAt(site);
			r.frame(2);
			REQUIRE(r.cam->worldToScreen({ site.x, site.y, r.game->logic().getGroundHeight(site.x, site.y) }, px));
			r.move(px.x, px.y);
			r.frame(1);
			if ((r.ui().clientFrame() & 1) == 0)
			{
				r.frame(1);
			}
			REQUIRE(r.ui().placeHasGhost());
			icons.update(r.ui(), 33.0, PlaceEventTranslator::ghostOptions());
			REQUIRE_MESSAGE(icons.errors().empty(), icons.errors().front());
			REQUIRE(icons.castle());
			REQUIRE(icons.icons().size() > 1);
			REQUIRE((r.ui().clientFrame() & 1) == 1u);
			bool failed = false;
			for (size_t i = 0; i < icons.icons().size(); ++i)
			{
				const PlacementIcons::Icon &ic = icons.icons()[i];
				CHECK(ic.drawable->drawOpacity() == doctest::Approx(0.45f));
				CHECK(ic.angle == r.ui().placeAngle()); // RW 0x6A36FB: the ghost's angle alone
				CHECK(ic.drawable->getHouseColor() == (i == 0 ? PlacementIcons::indicatorColor(r.game->logic(), builder->getID()) : 0u));
				if (!ic.baseSite)
				{
					CHECK(ic.code == 0);
				}
				else
				{
					++judgedSlots;
					const LegalBuildCode expect = BuildPlacement::isLocationLegalToBuild(r.game->logic(), *ic.drawable->getPosition(), *ic.tt, ic.angle,
						PlaceEventTranslator::ghostOptions(), builder, nullptr);
					CHECK(ic.code == (int)expect);
				}
				if (ic.code == 0)
				{
					CHECK(ic.tint == (failed ? PlacementIcons::TINT_ILLEGAL : PlacementIcons::TINT_NONE));
					CHECK(ic.bib == PlacementIcons::BIB_NONE);
				}
				else if (ic.code != 9)
				{
					CHECK(ic.tint == PlacementIcons::TINT_ILLEGAL);
					CHECK(ic.bib == PlacementIcons::BIB_HIGHLIGHT);
				}
				failed = failed || (ic.baseSite && ic.code != 0);
			}
		}
		// retail's fortresses are KindOf BASE_SITE themselves (mordorfortress.ini, menfortress.ini ...): slot 0 is judged at each site; the layout's
		// pieces (walls, keeps, pads) are not
		CHECK(icons.icons()[0].baseSite);
		CHECK(judgedSlots >= 4);
	}
}

TEST_CASE("build5 r2 port: a PORT straddles water and land (ratio 6 at most) and snaps to the shore facing the water (getPortTransform RW 0x794C63)")
{
	if (!haveWorld("build5 port"))
	{
		return;
	}
	PlaceRig r(shared(), "FactionMen", "map mp evendim");
	GameLogic &logic = r.game->logic();
	const ThingTemplate *ship = logic.things().findTemplate("ShipWright");
	REQUIRE(ship != nullptr);
	AIWorld *ai = logic.aiWorld();
	REQUIRE(ai != nullptr);
	const PathfindTerrain *terrain = ai->pathfinder().terrainView();
	REQUIRE(terrain != nullptr);
	// a shore: walk the map's grid for a land point next to water
	float wz = 0.0f, tz = 0.0f;
	int found = 0, legal = 0;
	for (float x = 200.0f; x < 4800.0f && found < 6; x += 40.0f)
	{
		for (float y = 200.0f; y < 4800.0f && found < 6; y += 40.0f)
		{
			if (terrain->isUnderwater(x, y, &wz, &tz) || !(terrain->isUnderwater(x + 40.0f, y, &wz, &tz) || terrain->isUnderwater(x, y + 40.0f, &wz, &tz) ||
					terrain->isUnderwater(x - 40.0f, y, &wz, &tz) || terrain->isUnderwater(x, y - 40.0f, &wz, &tz)))
			{
				continue;
			}
			const Coord3D at{ x, y, logic.getGroundHeight(x, y) };
			float a = 0.0f;
			Coord3D snapped;
			if (!BuildPlacement::getPortTransform(logic, at, *ship, 0.0f, a, snapped))
			{
				continue;
			}
			++found;
			// the snapped site is on land, and the water lies ahead: the angle minus pi / 2 points at it
			CHECK_FALSE(terrain->isUnderwater(snapped.x, snapped.y, &wz, &tz));
			const float dx = std::cos(a - 1.5707963705062866f), dy = std::sin(a - 1.5707963705062866f);
			bool water = false;
			for (float t = 5.0f; t <= 60.0f && !water; t += 5.0f)
			{
				water = terrain->isUnderwater(snapped.x + dx * t, snapped.y + dy * t, &wz, &tz);
			}
			CHECK(water);
			CHECK(snapped.z == doctest::Approx(logic.getGroundHeight(snapped.x, snapped.y)));
			const LegalBuildCode code = BuildPlacement::isLocationLegalToBuild(logic, at, *ship, 0.0f, LLF_TERRAIN_RESTRICTIONS, nullptr, nullptr);
			legal += code == LBC_OK ? 1 : 0;
		}
	}
	MESSAGE("shore sites with a port transform: " << found << ", terrain-legal ones: " << legal);
	REQUIRE(found > 0);
	// far inland a port is refused (no water among its samples); a non-port over water too
	const Coord3D inland{ r.start.x, r.start.y, logic.getGroundHeight(r.start.x, r.start.y) };
	CHECK(BuildPlacement::isLocationLegalToBuild(logic, inland, *ship, 0.0f, LLF_TERRAIN_RESTRICTIONS, nullptr, nullptr) == LBC_RESTRICTED_TERRAIN);
	float a = 0.0f;
	Coord3D snapped;
	CHECK_FALSE(BuildPlacement::getPortTransform(logic, inland, *ship, 0.0f, a, snapped));
}

TEST_CASE("build5 r3 busy frames: through the displayed (interpolated) view, also on frames the logic worker runs, the ghost stays under the live pointer")
{
	if (!haveWorld("build5 busy frames"))
	{
		return;
	}
	PlaceRig r(shared(), "FactionMen", "map mp fall back 4p", true);
	r.frame(5);
	r.startPlacing();
	r.input->setPlacementDeferred(true);
	// InGameHudNode::_process at 60 render frames a second: the HUD's update only on idle frames (its input waits otherwise), the camera's client frames at
	// 30 a second, the displayed pose at the client clock's fraction, the live pointer, the placement step (pose always, judgement when idle)
	double clientClock = 0.0;
	const double step = 1.0 / 30.0, dt = 1.0 / 60.0;
	ICoord2D live{ 520, 380 };
	int worst = 0, busyFrames = 0, checked = 0;
	auto renderFrame = [&](bool busy) {
		if (!busy)
		{
			r.input->update();
		}
		clientClock += dt;
		while (clientClock >= step)
		{
			r.nowMs += 33;
			r.input->cameraFrame(r.nowMs);
			clientClock -= step;
		}
		r.shown.present(1024, 768, clientClock / step);
		r.input->placeTranslator().setLivePointer(live);
		r.input->placementUpdate(!busy);
		if (r.ui().placeHasGhost() && !r.ui().isPlacementAnchored())
		{
			ICoord2D at{ -9999, -9999 };
			REQUIRE(r.shown.worldToScreen(r.ui().placeLocation(), at));
			worst = std::max(worst, std::abs(at.x - live.x) + std::abs(at.y - live.y));
			++checked;
		}
		busyFrames += busy ? 1 : 0;
	};
	r.input->mouseMove(live.x, live.y, 0);
	renderFrame(false);
	renderFrame(false);
	REQUIRE(r.ui().placeHasGhost());
	// a right drag starts a scroll (idle frames: the press and the move reach the translators)
	r.timeMs += 20;
	r.input->mouseButton(HudInput::Button::Right, true, live.x, live.y, 0, r.timeMs);
	live = { live.x + 140, live.y + 50 };
	r.input->mouseMove(live.x, live.y, 0);
	renderFrame(false);
	REQUIRE(r.input->lookAt()->isScrolling());
	const Coord3D cam0 = r.cam->position();
	// the scroll goes on through busy frames (3 of every 4), the pointer moving meanwhile: the queued events wait, the live pointer does not
	for (int i = 0; i < 40; ++i)
	{
		const bool busy = (i % 4) != 3;
		if (busy && (i % 2) == 0)
		{
			live = { live.x + 3, live.y - 2 }; // the OS pointer moves while the HUD's input waits
		}
		renderFrame(busy);
	}
	const Coord3D cam1 = r.cam->position();
	CHECK(std::fabs(cam1.x - cam0.x) + std::fabs(cam1.y - cam0.y) > 50.0f);
	CHECK(busyFrames >= 30);
	CHECK(checked >= 40);
	CHECK_MESSAGE(worst <= 1, "the ghost lagged " << worst << " pixels behind the live pointer over " << checked << " render frames (" << busyFrames << " busy)");
	// the legality is only asked on idle frames: a busy frame keeps the last answer
	const int codeBefore = r.ui().placeLegalCode();
	r.input->placementUpdate(false);
	CHECK(r.ui().placeLegalCode() == codeBefore);
	r.timeMs += 400;
	r.input->mouseButton(HudInput::Button::Right, false, live.x, live.y, 0, r.timeMs);
	renderFrame(false);
}

TEST_CASE("build5 r4 busy frames, a PORT: the shore-snapped ghost follows the displayed view on frames the logic worker runs too")
{
	if (!haveWorld("build5 busy frames port"))
	{
		return;
	}
	PlaceRig r(shared(), "FactionMen", "map mp evendim", true);
	GameLogic &logic = r.game->logic();
	const ThingTemplate *ship = logic.things().findTemplate("ShipWright");
	REQUIRE(ship != nullptr);
	const PathfindTerrain *terrain = logic.aiWorld()->pathfinder().terrainView();
	REQUIRE(terrain != nullptr);
	r.frame(5);
	r.startPlacing("ShipWright");
	// the shore site nearest the start that snaps
	float wz = 0.0f, tz = 0.0f, best = 1e30f;
	Coord3D shore{};
	for (float x = 200.0f; x < 4800.0f; x += 40.0f)
	{
		for (float y = 200.0f; y < 4800.0f; y += 40.0f)
		{
			const float d = (x - r.start.x) * (x - r.start.x) + (y - r.start.y) * (y - r.start.y);
			if (d >= best || terrain->isUnderwater(x, y, &wz, &tz) || !terrain->isUnderwater(x + 40.0f, y, &wz, &tz))
			{
				continue;
			}
			float a = 0.0f;
			Coord3D p;
			if (BuildPlacement::getPortTransform(logic, Coord3D{ x, y, logic.getGroundHeight(x, y) }, *ship, 0.0f, a, p))
			{
				best = d;
				shore = Coord3D{ x, y, logic.getGroundHeight(x, y) };
			}
		}
	}
	REQUIRE(best < 1e30f);
	r.cam->lookAt(shore);
	r.frame(20);
	r.input->setPlacementDeferred(true);
	double clientClock = 0.0;
	const double step = 1.0 / 30.0, dt = 1.0 / 60.0;
	ICoord2D live{ -1, -1 };
	REQUIRE(r.shown.worldToScreen(shore, live));
	double worst = 0.0;
	int busyFrames = 0, snappedBusy = 0, movedBusy = 0, checked = 0;
	Coord3D last{};
	auto renderFrame = [&](bool busy) {
		if (!busy)
		{
			r.input->update();
		}
		clientClock += dt;
		while (clientClock >= step)
		{
			r.nowMs += 33;
			r.input->cameraFrame(r.nowMs);
			clientClock -= step;
		}
		r.shown.present(1024, 768, clientClock / step);
		r.input->placeTranslator().setLivePointer(live);
		r.input->placementUpdate(!busy);
		if (!r.ui().placeHasGhost() || r.ui().isPlacementAnchored())
		{
			return;
		}
		// the expected pose: the terrain point under the live pointer through the displayed view, snapped by the logic's own getPortTransform
		Coord3D world;
		REQUIRE(r.shown.screenToTerrain(live, logic, world));
		Coord3D expect = world;
		float a = 0.0f;
		const bool snaps = BuildPlacement::getPortTransform(logic, world, *ship, r.ui().placeSlotAngle(), a, expect);
		const Coord3D at = r.ui().placeLocation();
		worst = std::max(worst, (double)std::fabs(at.x - expect.x) + std::fabs(at.y - expect.y) + std::fabs(at.z - expect.z));
		++checked;
		if (busy)
		{
			++busyFrames;
			snappedBusy += snaps ? 1 : 0;
			movedBusy += (at.x != last.x || at.y != last.y) ? 1 : 0;
		}
		last = at;
	};
	r.input->mouseMove(live.x, live.y, 0);
	renderFrame(false);
	renderFrame(false);
	REQUIRE(r.ui().placeHasGhost());
	// a short right drag: a slow scroll, so the pointer's terrain point slides along the shore
	r.timeMs += 20;
	r.input->mouseButton(HudInput::Button::Right, true, live.x, live.y, 0, r.timeMs);
	live = { live.x + 24, live.y + 8 };
	r.input->mouseMove(live.x, live.y, 0);
	renderFrame(false);
	REQUIRE(r.input->lookAt()->isScrolling());
	const Coord3D cam0 = r.cam->position();
	for (int i = 0; i < 40; ++i)
	{
		renderFrame((i % 4) != 3);
	}
	const Coord3D cam1 = r.cam->position();
	MESSAGE("camera moved " << std::fabs(cam1.x - cam0.x) + std::fabs(cam1.y - cam0.y) << ", busy frames " << busyFrames << ", snapped on " << snappedBusy
							<< ", the ghost moved on " << movedBusy << ", worst " << worst);
	CHECK(std::fabs(cam1.x - cam0.x) + std::fabs(cam1.y - cam0.y) > 5.0f);
	CHECK(busyFrames >= 30);
	CHECK(snappedBusy >= 10);    // the PORT snap ran on busy frames (no early return)
	CHECK(movedBusy >= 10);      // and the ghost moved with the displayed camera on them
	CHECK(worst <= 0.001);
	r.timeMs += 400;
	r.input->mouseButton(HudInput::Button::Right, false, live.x, live.y, 0, r.timeMs);
	renderFrame(false);
}

TEST_CASE("build5 r3 ghost errors: a ghost whose model does not load is reported (the draw module's problems are forwarded), not silently empty")
{
	if (!haveWorld("build5 ghost errors"))
	{
		return;
	}
	PlaceRig r(shared());
	r.frame(5);
	r.startPlacing();
	r.move(520, 380);
	r.frame(2);
	REQUIRE(r.ui().placeHasGhost());
	// draw services over an empty archive set: every model of the ghost is missing
	Win32BIGFileSystem empty;
	ArchiveW3DFileSource source(empty);
	WW3DAssetManager assets(source);
	DrawableManager dm(assets, r.game->logic());
	PlacementIcons icons(dm.services(), r.game->logic());
	icons.update(r.ui(), 33.0, PlaceEventTranslator::ghostOptions());
	REQUIRE(!icons.errors().empty());
	bool modelReported = false;
	for (const std::string &e : icons.errors())
	{
		MESSAGE(e);
		modelReported = modelReported || (e.find("GondorBarracks") != std::string::npos && e.find("does not load") != std::string::npos);
	}
	CHECK(modelReported);
	// the retail assets: no error
	PlacementIcons good(r.game->drawables().services(), r.game->logic());
	good.update(r.ui(), 33.0, PlaceEventTranslator::ghostOptions());
	CHECK_MESSAGE(good.errors().empty(), (good.errors().empty() ? std::string() : good.errors().front()));
	for (const std::string &n : good.notes())
	{
		MESSAGE("note: " << n); // e.g. the script hides V1FLAG, which GBBarracks_SKN lacks (a counted retail data quirk, as for the game's drawables)
	}
}

namespace
{
// the retail archives with some files hidden (the asset manager's W3D files), recording what it reads
struct HidingSource : W3DFileSource
{
	explicit HidingSource(ArchiveFileSystem &fs) : inner(fs) {}
	bool Exists(const std::string &path) const override { return !hidden(path) && inner.Exists(path); }
	bool Read(const std::string &path, std::vector<std::uint8_t> &out, std::string *error) override
	{
		reads.push_back(path);
		if (hidden(path))
		{
			if (error)
			{
				*error = path + " is hidden by the test";
			}
			return false;
		}
		return inner.Read(path, out, error);
	}
	bool hidden(const std::string &path) const
	{
		for (const std::string &h : hide)
		{
			if (AsciiStringUtil::lowered(path).find(h) != std::string::npos)
			{
				return true;
			}
		}
		return false;
	}
	ArchiveW3DFileSource inner;
	std::vector<std::string> hide, reads;
};
} // namespace

TEST_CASE("build5 r4 ghost animation errors: an animation of the ghost that does not resolve is an error (push_error, the game's report), not a note")
{
	if (!haveWorld("build5 ghost animation errors"))
	{
		return;
	}
	PlaceRig r(shared());
	r.frame(5);
	r.startPlacing();
	r.move(520, 380);
	r.frame(2);
	REQUIRE(r.ui().placeHasGhost());
	HidingSource probe(*shared().mount->fs);
	{
		WW3DAssetManager assets(probe);
		DrawableManager dm(assets, r.game->logic());
		PlacementIcons icons(dm.services(), r.game->logic());
		icons.update(r.ui(), 33.0, PlaceEventTranslator::ghostOptions());
		REQUIRE_MESSAGE(icons.errors().empty(), icons.errors().front());
		bool readsAnimation = false;
		for (const std::string &p : probe.reads)
		{
			readsAnimation = readsAnimation || AsciiStringUtil::lowered(p).find("gbbarracks_2ida") != std::string::npos;
		}
		REQUIRE(readsAnimation); // the ghost's BUILD_PLACEMENT_CURSOR state plays GBBarracks_2SKL.GBBarracks_2IDA (art\w3d\gb\gbbarracks_2ida.w3d)
	}
	// the same ghost with its animation file hidden: the draw module's "animation ... is not registered" is an error, not a counted note
	HidingSource hiding(*shared().mount->fs);
	hiding.hide = { "gbbarracks_2ida" };
	WW3DAssetManager assets(hiding);
	DrawableManager dm(assets, r.game->logic());
	PlacementIcons icons(dm.services(), r.game->logic());
	icons.update(r.ui(), 33.0, PlaceEventTranslator::ghostOptions());
	bool animationError = false;
	for (const std::string &e : icons.errors())
	{
		MESSAGE(e);
		animationError = animationError || (e.find("GondorBarracks") != std::string::npos && e.find("animation") != std::string::npos);
	}
	CHECK(animationError);
	for (const std::string &n : icons.notes())
	{
		CHECK_MESSAGE(n.find("animation") == std::string::npos, n);
	}
	// once: a second frame does not repeat it
	const size_t count = icons.errors().size();
	icons.update(r.ui(), 33.0, PlaceEventTranslator::ghostOptions());
	CHECK(icons.errors().size() == count);
}

TEST_CASE("build5 r3 alpha override: an opaque surface of a translucent drawable blends at the instance opacity (ZH's translucent pass), without the dither")
{
	W3DShaderKey k;
	k.Blend = W3D_GODOT_BLEND_OPAQUE;
	k.Texturing = true;
	const std::string plain = Generate_W3D_Shader_Code(k);
	k.AlphaOverride = true;
	const std::string ao = Generate_W3D_Shader_Code(k);
	CHECK(k.Id().find("_ao") != std::string::npos);
	W3DShaderKey k0;
	k0.Blend = W3D_GODOT_BLEND_OPAQUE;
	k0.Texturing = true;
	CHECK(k0.Id().find("_ao") == std::string::npos); // every existing id stays what it was
	// the plain opaque surface dithers its fade in the opaque pass and converts to linear once
	CHECK(plain.find("blend_mix") == std::string::npos);
	CHECK(plain.find("if (v_fade <= ign) { discard; }") != std::string::npos);
	CHECK(plain.find("w3d_to_linear(clamp(cur.rgb") != std::string::npos);
	// the translucent drawable's: SRC_ALPHA / ONE_MINUS_SRC_ALPHA with depth writes (theAlphaShader), alpha = alpha * the instance fade, no dither, gamma space
	CHECK(ao.find("blend_mix") != std::string::npos);
	CHECK(ao.find("depth_draw_always") != std::string::npos);
	CHECK(ao.find("ALPHA = a * v_fade;") != std::string::npos);
	CHECK(ao.find("if (v_fade <= ign) { discard; }") == std::string::npos);
	CHECK(ao.find("ALBEDO = w3d_to_linear") == std::string::npos);
	// an additive surface keeps its blend (its fade scales the colour, as ZH scales the diffuse)
	W3DShaderKey add;
	add.Blend = W3D_GODOT_BLEND_ADD;
	add.Texturing = true;
	add.AlphaOverride = true;
	const std::string addCode = Generate_W3D_Shader_Code(add);
	CHECK(addCode.find("blend_add") != std::string::npos);
	CHECK(addCode.find("ALPHA = v_fade;") != std::string::npos);
}

TEST_CASE("build5 resource preview: farms expose their radius and share without claiming resources")
{
	if (!haveWorld("build5 resource preview")) return;
	PlaceRig r(shared());
	r.startPlacing("GondorFarm");
	r.move(512, 384);
	PlacementIcons icons(r.game->drawables().services(), r.game->logic());
	icons.update(r.ui(), 33.0, PlaceEventTranslator::ghostOptions());
	CHECK(icons.resourceRadius() > 0);
	CHECK(icons.resourceShare() >= 0);
	CHECK(icons.resourceShare() <= 1);
	const float share = icons.resourceShare();
	icons.update(r.ui(), 33.0, PlaceEventTranslator::ghostOptions());
	CHECK(icons.resourceShare() == share);
	icons.clear();
	CHECK(icons.resourceRadius() == 0);
	CHECK(icons.resourceShare() == 0);
}

#include "GameLogic/WallSpan.h"
TEST_CASE("build5 wall preview: every ghost matches the construction planner tile")
{
	if (!haveWorld("build5 wall preview")) return;
	PlaceRig r(shared());
	std::string error;
	Object *hub = r.game->createObject("MenWallHubSmallExpansion", r.game->players().getLocalPlayer()->getPlayerIndex(), r.start, 0, &error);
	REQUIRE_MESSAGE(hub, error);
	Coord3D end = *hub->getPosition();
	end.x += 300;
	InGameUI ui;
	ui.placeBuildAvailable("MenWallHubSmall", hub->getID());
	ui.setPlaceGhost(end, 0, 0);
	WallSpan::Plan plan;
	REQUIRE(WallSpan::plan(r.game->logic(), *hub, *hub->getPosition(), end, 0, plan));
	PlacementIcons icons(r.game->drawables().services(), r.game->logic());
	icons.update(ui, 33, PlaceEventTranslator::ghostOptions());
	REQUIRE(icons.icons().size() == plan.tiles.size());
	for (size_t i = 0; i < plan.tiles.size(); ++i)
	{
		CHECK(icons.icons()[i].tt == plan.tiles[i].tmpl->getFinalOverride());
		CHECK(icons.icons()[i].drawable->getPosition()->x == plan.tiles[i].pos.x);
		CHECK(icons.icons()[i].drawable->getPosition()->y == plan.tiles[i].pos.y);
		CHECK(icons.icons()[i].code == plan.tiles[i].code);
	}
}
