// OpenBFME unit tests. GPL-3.0.
// Lane HUDREDRAW-1 (owner playtest 2026-10-20: opening the spell book drops the game to ~30 fps): the RETAIL Palantir and SpellStore.apt in a skirmish HUD.
// A buyable power sits in its `_active` state, which loops (SpellStore.apt has a separate `_active_stopped`), so the store's canvas ops change from frame to
// frame; the device used to redraw the whole list then (the Palantir too). Here the canvas is built every frame as the device builds it and planned against
// the last (AptCanvasSegments / AptCanvasPlanRedraw): with points to spend the Palantir's powers button pulses (one segment drawn again per frame, the
// Palantir's other segments kept), and with the store open only the store's three `_active` buttons are drawn again besides it; and neither the Apt notes
// nor the window manager notes grow over 300 frames, open or closed. SKIPs without ROTWK_INSTALL / BFME2_INSTALL.

#include "HudTestUtil.h"

#include "Common/PlayerScience.h"
#include "GameClient/AptCanvas.h"
#include "GameClient/GameText.h"
#include "GameClient/GameTextTableSource.h"
#include "GameClient/GUI/ShellServices.h"
#include "GameClient/InGameHud.h"
#include "GameClient/GUI/AptScreens/AptPalantir.h"
#include "GameClient/GUI/AptScreens/AptSpellStore.h"
#include "Libraries/Source/Apt/AptLoad.h"
#include "Libraries/Source/Apt/AptRenderList.h"

#include <map>
#include <set>
#include <string>

using namespace hudtest;

namespace
{

struct RedrawRig
{
	Rig rig;
	RecordingShellServices services;
	GameTextTableSource text;
	std::unique_ptr<InGameHud> hud;
	AptArchiveFileSource source;
	AptTextureStore textures;
	AptCanvasList last;
	RedrawRig(SharedWorld &s) : rig(s, "map mp fall back 4p", "FactionIsengard"), source(*s.mount->fs), textures(source)
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
	~RedrawRig() { hud.reset(); }
	AptCanvasList canvas()
	{
		AptRenderList rl;
		hud->apt().buildRenderList(rl);
		AptCanvasInputs in;
		in.mapping.windowW = 1024;
		in.mapping.windowH = 768;
		in.textures = &textures;
		in.text = &text.table;
		AptTextRecordLookup lookup = [this](const std::string &name, std::string &out) { return hud->windows().aptTextShown(name, out); };
		in.textRecords = &lookup;
		AptCanvasList out;
		BuildAptCanvas(rl, in, out);
		return out;
	}
	struct Window
	{
		int frames = 0, full = 0;                    // frames, frames the plan drew everything again
		std::map<int, int> segments, redrawn;         // per level: its segments (last frame), segments drawn again (summed)
		std::map<int, std::map<std::string, int>> paths; // per level: the first op path of each segment drawn again -> how often
	};
	// `n` HUD frames of 33 ms, each planned against the frame before as the device plans it
	Window frames(int n)
	{
		Window w;
		for (int i = 0; i < n; ++i)
		{
			hud->update(0.033);
			rig.game->advance(0.033);
			AptCanvasList now = canvas();
			const std::vector<AptCanvasSegment> a = AptCanvasSegments(now), b = AptCanvasSegments(last);
			const AptCanvasRedrawPlan plan = AptCanvasPlanRedraw(now, a, last, b);
			w.frames += 1;
			w.full += plan.full ? 1 : 0;
			w.segments.clear();
			for (size_t j = 0; j < a.size(); ++j)
			{
				w.segments[a[j].firstLevel] += 1;
				if (!plan.full && !plan.same[j])
				{
					w.redrawn[a[j].firstLevel] += 1;
					w.paths[a[j].firstLevel][now.ops[a[j].begin].path] += 1;
				}
			}
			last = std::move(now);
		}
		return w;
	}
};

} // namespace

TEST_CASE("hudredraw1 retail: 300 frames closed and open, the Palantir draws again only its pulsing powers button, the store only its _active buttons; the notes stay")
{
	if (!haveWorld("hudredraw1 store"))
	{
		return;
	}
	SharedWorld &s = shared();
	RedrawRig r(s);
	{
		auto ctx = r.hud->enterContext();
		const ThingTemplate *cit = s.world->things().findTemplate("IsengardFortressCitadel");
		REQUIRE(cit);
		Object *c = r.rig.logic().newObject(cit, r.rig.local->getDefaultTeam(), ObjectStatusMaskType{});
		REQUIRE(c);
		Coord3D at{ 600.0f, 600.0f, 0.0f };
		c->setPosition(&at);
		c->friend_onBuildComplete();
		r.rig.local->science().addSciencePurchasePoints(40); // every tier-1 power buyable: `_active`
	}
	r.frames(90); // the HUD's fade-in
	const int pal = r.hud->palantir()->level();
	const std::string button = "_level" + std::to_string(pal) + ".PalantirButtons.Buttons.PlayerMagic.ButtonClip.instance3"; // the powers button
	const std::string resources = "_level" + std::to_string(pal) + ".ResourceBar.Resources.instance3"; // the money counter (the economy's income)
	// closed: only the powers button's pulse (points to spend) is drawn again, one segment a frame, and the money counter when it counts; the rest of the
	// Palantir is kept
	const std::size_t aptNotes = r.hud->apt().notes().size(), wmNotes = r.hud->windows().notes().size();
	RedrawRig::Window closed = r.frames(300);
	CHECK(r.hud->apt().notes().size() == aptNotes);
	CHECK(r.hud->windows().notes().size() == wmNotes);
	MESSAGE("closed: Palantir level " << pal << ", " << closed.segments[pal] << " segments, " << closed.redrawn[pal] << " drawn again; levels " << closed.segments.size());
	CHECK(closed.full == 0);
	CHECK(closed.segments.size() == 1);
	CHECK(closed.segments[pal] == 52);
	CHECK(closed.redrawn[pal] == 302);
	CHECK(closed.paths[pal] == std::map<std::string, int>{ { button, 300 }, { resources, 2 } });
	// open: as the Palantir's PlayerMagic button does (play1 drives the click: test_spell_book_hud.cpp), then the fade-in
	std::string error;
	REQUIRE_MESSAGE(r.hud->openSpellStore(&error), error);
	AptSpellStore *st = r.hud->spellStore();
	REQUIRE(st);
	r.frames(120);
	const int store = st->level();
	std::map<std::string, int> activeArt; // the art of each `_active` power: its loop steps 293 of the 300 frames (the movie's 30 fps against 33 ms frames)
	for (int i = 0; i < 12; ++i)
	{
		if (std::string(AptSpellStore::stateName(st->buttonState(i))) == "_active")
		{
			activeArt["_level" + std::to_string(store) + ".SpellStore.Buttons.Spell" + std::to_string(i + 1) + ".instance3.instance1.instance1"] = 293;
		}
	}
	CHECK(activeArt.size() == 3); // the three tier-1 powers (40 points: every one buyable)
	const std::size_t aptNotesOpen = r.hud->apt().notes().size(), wmNotesOpen = r.hud->windows().notes().size();
	RedrawRig::Window open = r.frames(300);
	MESSAGE("open: store level " << store << ", " << open.segments[store] << " segments, " << open.redrawn[store] << " drawn again; Palantir "
		<< open.redrawn[pal] << " of " << open.segments[pal]);
	for (const auto &kv : open.paths)
	{
		for (const auto &path : kv.second)
		{
			MESSAGE("open: level " << kv.first << " drew again " << path.first << " in " << path.second << " frames");
		}
	}
	CHECK(open.full == 0);
	CHECK(open.segments.size() == 2);
	CHECK(open.segments[pal] == 52);
	CHECK(open.redrawn[pal] == 302);
	CHECK(open.paths[pal] == std::map<std::string, int>{ { button, 300 }, { resources, 2 } });
	CHECK(open.segments[store] == 55);
	CHECK(open.redrawn[store] == 879);
	CHECK(open.paths[store] == activeArt); // the `_active` loop of each buyable power, nothing else of the store
	MESSAGE("notes: apt " << aptNotes << " -> " << aptNotesOpen << " -> " << r.hud->apt().notes().size() << ", window manager " << wmNotes << " -> " << wmNotesOpen << " -> "
		<< r.hud->windows().notes().size());
	CHECK(r.hud->apt().notes().size() == aptNotesOpen);
	CHECK(r.hud->windows().notes().size() == wmNotesOpen);
	r.hud->closeSpellStore();
	r.frames(60);
	const std::size_t aptNotesClosed = r.hud->apt().notes().size(), wmNotesClosed = r.hud->windows().notes().size();
	r.frames(300);
	CHECK(r.hud->apt().notes().size() == aptNotesClosed);
	CHECK(r.hud->windows().notes().size() == wmNotesClosed);
}
