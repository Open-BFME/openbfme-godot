// OpenBFME unit tests. GPL-3.0.
// Lane UI-2 (owner feedback F5: "no map preview and no map name, also why are the drop down arrows on the very left"): the LAN lobby's setup page through
// the real shell over the retail archives. The map window (MpGameSetup's CurrentMap: BFME2 window\Apt\MpMapWindow.wnd, W3DDrawMapPreview RW 0x49D9EC)
// gets the map and its _art picture under the ScrollShroud (RW 0x9772E6 / 0x702689), the map's name reaches APT:CurrentMapName (RW 0x976497), and every
// colour box's drop-down button sits at the box's right edge, scaled to its height by its image (RW 0x725688). SKIP loudly without the retail installs.

#include "doctest.h"

#include "StartShellFx.h"

#include "GameClient/GUI/AptScreens/AptLanLobby.h"
#include "GameClient/GUI/Gadgets.h"
#include "GameClient/GUI/GameWindow.h"
#include "GameClient/MapCache.h"
#include "GameNetwork/LANAPI.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <functional>
#include <memory>
#include <string>

#ifndef _WIN32
#include <unistd.h>
#endif

namespace
{
std::unique_ptr<LANAPI> ui2Lan(int factions, int colors)
{
	MountedArchive a;
	a.install = "rotwk";
	a.canonicalPath = "test.big";
	a.size = 1;
	a.md5 = "0123456789abcdef0123456789abcdef";
	const ProfileIdentity profile = ProfileIdentity::compute({ a }, RandomAlgorithm::ZH_CarryChain, {});
	LANAPI::Options o;
	o.playerTemplateCount = factions;
	o.colorCount = colors;
#ifdef _WIN32
	const int pid = 4321;
#else
	const int pid = (int)getpid();
#endif
	o.lobbyPortBase = (std::uint16_t)(46000 + (pid % 1000) * 16);
	o.lobbyPorts = 8;
	o.broadcast = false;
	o.extraTargets = { 0x7F000001u };
	auto lan = std::make_unique<LANAPI>(profile, u"Host", o);
	std::string error;
	REQUIRE_MESSAGE(lan->open(&error), error);
	return lan;
}
} // namespace

TEST_CASE("ui2 retail: the LAN setup page shows the map's picture (W3DDrawMapPreview), its name, and the colour boxes' drop-down arrows on the right")
{
	OPENBFME_REQUIRE_START(s);
	std::unique_ptr<LANAPI> lan = ui2Lan(s->world->playerTemplates().getPlayerTemplateCount(), (int)s->settings.multiplayerColors.size());
	starttest::ShellFx host(*s, 11);
	host.environment.lan = lan.get();
	host.environment.services = &host.services;
	host.shell->push("LanLobby.apt");
	REQUIRE(host.shell->errors().empty());
	AptLanLobby *screen = dynamic_cast<AptLanLobby *>(host.shell->top());
	REQUIRE(screen);
	auto run = [&](const std::function<bool()> &done) {
		for (int i = 0; i < 300; ++i)
		{
			host.tick(1);
			if (done())
			{
				return true;
			}
		}
		return false;
	};
	REQUIRE(run([&] { return screen->state() == 1; }));
	CHECK(host.wm->invokeCallback("AptLanLobby::OnCreateGameBttn", ""));
	const bool ready = run([&] { return screen->state() == 4 && screen->currentMapWindow() && screen->setup() && screen->slotGadget(7, "Color"); });
	if (!ready)
	{
		for (const std::string &e : host.layer->errors())
		{
			printf("LAYERERR %s\n", e.c_str());
		}
		for (const std::string &e : host.layer->notes())
		{
			printf("LAYERNOTE %s\n", e.c_str());
		}
		printf("state %d map window %p setup %p\n", screen->state(), (void *)screen->currentMapWindow(), (const void *)screen->setup());
		for (const WindowManagerNote &n : host.wm->notes())
		{
			printf("NOTE %s | %s\n", n.kind.c_str(), n.detail.c_str());
		}
	}
	REQUIRE(ready);
	// the colour boxes: the drop-down button at the right edge, as high as the box (RW 0x725688). The host's own row can be between refreshes here
	// (MP-2's lobby rebuilds it), so every box that exists is checked and at least seven must
	int boxes = 0;
	for (int slot = 0; slot < 8; ++slot)
	{
		GameWindow *box = screen->slotGadget(slot, "Color");
		if (!box)
		{
			continue;
		}
		++boxes;
		INFO("slot " << slot);
		GameWindow *button = nullptr;
		for (GameWindow *c = box->winGetChild(); c; c = c->winGetNext())
		{
			if (BitTest(c->winGetStyle(), GWS_PUSH_BUTTON))
			{
				button = c;
			}
		}
		REQUIRE(button);
		int bw, bh, bx, by, cw, ch;
		button->winGetSize(&bw, &bh);
		button->winGetPosition(&bx, &by);
		box->winGetSize(&cw, &ch);
		CHECK(bx == cw - bw);
		CHECK(by == 0);
		CHECK(bh == ch);
		CHECK(bw > 0);
	}
	CHECK(boxes >= 7);
	host.tick(10);
	CHECK(host.layer->errors().empty());

	// the map window: its user data is the map of the game, image 0 the map's _art picture (an image made from the file), image 1 the shroud
	GameWindow *mapWindow = screen->currentMapWindow();
	REQUIRE(mapWindow);
	const MapCacheEntry *map = static_cast<const MapCacheEntry *>(mapWindow->winGetUserData());
	REQUIRE(map);
	CHECK(map->name == screen->setup()->info().mapName);
	CHECK(BitTest(mapWindow->winGetStatus(), WIN_STATUS_IMAGE));
	const std::string image0 = mapWindow->winGetEnabledImage(0);
	REQUIRE_FALSE(image0.empty());
	const Image *picture = mapWindow->manager().images()->findImageByName(image0);
	REQUIRE(picture);
	const std::string artSuffix = "_art.tga";
	REQUIRE(picture->filename.size() > artSuffix.size());
	CHECK(picture->filename.substr(picture->filename.size() - artSuffix.size()) == artSuffix);
	CHECK(picture->status == IMAGE_STATUS_RAW_TEXTURE);
	CHECK(mapWindow->winGetEnabledImage(1) == "ScrollShroud");
	// a LAN game is not a skirmish (TheGameLogic + 0x114 != 3): the eight start spots stay hidden
	REQUIRE(screen->currentMapSpots().size() == 8);
	for (GameWindow *spot : screen->currentMapSpots())
	{
		CHECK(spot->winIsHidden());
	}
	// the draw: the picture over the aspect-kept rectangle inside the window (findDrawPositions RW 0x7018E0), shroud first, then the picture
	GadgetDrawList list;
	host.layer->buildDrawList(list);
	int x, y, w, h;
	mapWindow->winGetScreenPosition(&x, &y);
	mapWindow->winGetSize(&w, &h);
	ICoord2D ul, lr;
	MapPreviewFindDrawPositions(x, y, w, h, *map, &ul, &lr);
	int shroudAt = -1, pictureAt = -1;
	for (std::size_t i = 0; i < list.commands.size(); ++i)
	{
		const GadgetDrawCommand &c = list.commands[i];
		if (c.kind != GadgetDrawCommand::Kind::Image || c.x0 != ul.x || c.y0 != ul.y || c.x1 != lr.x || c.y1 != lr.y)
		{
			continue;
		}
		if (c.image == "ScrollShroud")
		{
			shroudAt = (int)i;
		}
		else if (c.image == image0)
		{
			pictureAt = (int)i;
		}
	}
	CHECK(shroudAt >= 0);
	CHECK(pictureAt > shroudAt);
	CHECK(std::find(list.unresolvedImages.begin(), list.unresolvedImages.end(), image0) == list.unresolvedImages.end());

	// the inferences are reported (S-1480)
	bool reported = false;
	for (const WindowManagerNote &n : host.wm->notes())
	{
		reported = reported || (n.kind == "map-preview-inferences" && n.detail.rfind("[S-1480]", 0) == 0);
	}
	CHECK(reported);

	// the name of the map (RW 0x976497 -> APT:CurrentMapName) and the game type literal
	const std::string *name = host.wm->aptText("APT:CurrentMapName");
	REQUIRE(name);
	CHECK_FALSE(name->empty());
	const std::string *type = host.wm->aptText("APT:LobbyGameType");
	REQUIRE(type);
	CHECK(*type == "Free For All");

}

TEST_CASE("ui2 map preview: findDrawPositions keeps the map's aspect (RW 0x7018E0) and a start spot steps past an earlier one (RW 0x7019A0)")
{
	MapCacheEntry map;
	map.extentMin = Coord3D{ 0, 0, 0 };
	map.extentMax = Coord3D{ 2000, 1000, 0 };
	ICoord2D ul, lr;
	// a wide map in a square: bars above and below
	MapPreviewFindDrawPositions(10, 20, 100, 100, map, &ul, &lr);
	CHECK(ul.x == 10);
	CHECK(ul.y == 20 + 25);
	CHECK(lr.x == 10 + 100);
	CHECK(lr.y == 20 + 75);
	// a tall map: bars left and right
	map.extentMax = Coord3D{ 1000, 2000, 0 };
	MapPreviewFindDrawPositions(0, 0, 100, 100, map, &ul, &lr);
	CHECK(ul.x == 25);
	CHECK(ul.y == 0);
	CHECK(lr.x == 75);
	CHECK(lr.y == 100);
}

TEST_CASE("ui2 stops S-1480 / S-1482 / S-1483 / S-1484 are registered in docs/STOPS.md with the identities reported at run time")
{
	std::ifstream in(std::string(OPENBFME_DOCS_DIR) + "/STOPS.md", std::ios::binary);
	REQUIRE_MESSAGE(in, "cannot open docs/STOPS.md");
	std::stringstream buffer;
	buffer << in.rdbuf();
	const std::string doc = buffer.str();
	const std::pair<const char *, const char *> rows[] = { { "| S-1480 |", "map-preview-inferences" }, { "| S-1482 |", "unverified_device" }, { "| S-1483 |", "view3d_notes" }, { "| S-1484 |", "options-soft-particles" } };
	for (const auto &row : rows)
	{
		const std::size_t at = doc.find(row.first);
		REQUIRE_MESSAGE(at != std::string::npos, row.first);
		const std::size_t end = doc.find('\n', at);
		CHECK_MESSAGE(doc.substr(at, end - at).find(row.second) != std::string::npos, row.first << " names " << row.second);
	}
}
