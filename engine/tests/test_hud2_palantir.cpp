// OpenBFME retail tests of the Palantir's centre and side state (lane HUD-2): the Evenstar / Ring of the PlayerMagic button (SetPlayerButtonsState), the
// command interface with the selection's portrait (PalantirCommandUI) and the RenderGlobe clip of the globe. They SKIP when ROTWK_INSTALL / BFME2_INSTALL are
// unset. GPL-3.0.

#include "HudTestUtil.h"

#include "Common/PlayerTemplate.h"
#include "GameClient/GUI/ShellServices.h"
#include "GameClient/InGameHud.h"
#include "GameClient/PalantirCommandUI.h"
#include "Libraries/Source/Apt/AptCharacterInst.h"
#include "Libraries/Source/Apt/AptRenderList.h"

#include <variant>

using namespace hudtest;

namespace
{
struct PalantirRig
{
	Rig rig;
	RecordingShellServices services;
	std::unique_ptr<InGameHud> hud;
	// `localName`: the player the HUD belongs to (Player_1 FactionMen, Player_2 FactionMordor)
	explicit PalantirRig(SharedWorld &s, const char *localName = "Player_1") : rig(s)
	{
		Player *p = rig.game->players().findPlayerWithName(localName);
		REQUIRE(p != nullptr);
		rig.game->players().setLocalPlayer(p);
		rig.local = p;
		InGameHud::Config cfg{ *rig.game, *s.world, *s.mount->fs, services, rig.view, s.mouse, s.meta, nullptr };
		hud = std::make_unique<InGameHud>(cfg);
		std::string error;
		REQUIRE_MESSAGE(hud->boot(&error), error);
	}
	~PalantirRig() { hud.reset(); }
	void frames(int n)
	{
		for (int i = 0; i < n; ++i)
		{
			hud->update(0.033);
			rig.game->advance(0.033);
		}
	}
	AptSpriteInst *clip(const std::string &path)
	{
		AptCharacterInst *c = hud->apt().resolvePath(hud->apt().level(hud->palantir()->level()), path);
		return c ? c->asSprite() : nullptr;
	}
	// the label of the frame the clip stands on ("" when the frame carries none)
	std::string label(const std::string &path)
	{
		AptSpriteInst *s = clip(path);
		REQUIRE_MESSAGE(s != nullptr, path);
		for (const char *l : { "_blank", "_evenstar", "_ring", "_hide", "_show", "_single", "_double" })
		{
			if (s->labelFrame(l) >= 0 && s->labelFrame(l) <= s->frame)
			{
				int best = s->labelFrame(l);
				bool later = false;
				for (const char *m : { "_blank", "_evenstar", "_ring", "_hide", "_show", "_single", "_double" })
				{
					const int f = s->labelFrame(m);
					later = later || (f > best && f <= s->frame);
				}
				if (!later)
				{
					return l;
				}
			}
		}
		return std::string();
	}
	std::vector<AptRenderCommand> commands(const std::string &pathPart)
	{
		AptRenderList rl;
		hud->apt().buildRenderList(rl);
		std::vector<AptRenderCommand> out;
		for (const AptRenderCommand &c : rl.commands)
		{
			if (c.path.find(pathPart) != std::string::npos)
			{
				out.push_back(c);
			}
		}
		return out;
	}
	// whether the PlayerMagic button draws a bitmap fill of image `id`
	bool playerMagicDraws(int id)
	{
		for (const AptRenderCommand &c : commands("PalantirButtons.Buttons.PlayerMagic.ButtonClip"))
		{
			for (const AptRenderFill &f : c.fills)
			{
				if (f.imageId == id)
				{
					return true;
				}
			}
		}
		return false;
	}
	std::string templatePortrait(const Object &o)
	{
		const FieldValue *v = o.getTemplate()->findField("SelectPortrait");
		const std::string *s = v ? std::get_if<std::string>(v) : nullptr;
		return s ? *s : std::string();
	}
};
} // namespace

TEST_CASE("hud2 palantir: nothing selected; the good side's PlayerMagic button shows the Evenstar, the command interface shows an empty portrait and the globe clip")
{
	if (!haveWorld("hud2 palantir evenstar"))
	{
		return;
	}
	PalantirRig h(shared());
	h.frames(60);
	AptPalantir *p = h.hud->palantir();
	REQUIRE(p != nullptr);
	// SetPlayerButtonsState("_evenstar") (RW 0x8001E8): PlayerMagic leaves its frame 0 `_blank` (no ButtonClip) for `_evenstar`, which places the shape of the
	// Evenstar clip; EnablePlayerMagicButton("1") (HUD-3, RW 0x6D5DEB) sends it to `_up`: the Evenstar art 205 of apt_Palantir_1, not the flat `_disabled` 162;
	// the movie's preview branch (not InGame) would show `_ring`
	CHECK(h.label("PalantirButtons.Buttons.PlayerMagic") == "_evenstar");
	CHECK(h.label("PalantirButtons.Buttons.Objectives") == "_evenstar");
	CHECK(h.playerMagicDraws(205));
	CHECK_FALSE(h.playerMagicDraws(162));
	// the command interface follows the control bar's context from the first update on (PalantirCommandUI 0x93085F), the portrait clip has no image
	CHECK(p->commandInterfaceShown());
	CHECK(h.label("CommandUI") == "_show");
	CHECK(p->portraitShown().empty());
	CHECK(p->imageForClip(p->portraitKey(), "") == nullptr);
	bool portraitClip = false, globeClip = false;
	for (const AptRenderCommand &c : h.commands("_level"))
	{
		portraitClip = portraitClip || (c.kind == AptRenderCommand::Kind::Placeholder && c.symbolName == "RenderImage" && c.path == p->portraitKey());
		globeClip = globeClip || (c.kind == AptRenderCommand::Kind::Placeholder && c.symbolName == "AptPalantir::RenderGlobe" && c.path.find("GlobeSwirlRender") != std::string::npos);
	}
	CHECK(portraitClip);
	// the live HUD reports the portrait's stop
	bool s760 = false;
	for (const std::string &l : h.hud->stops())
	{
		s760 = s760 || l.rfind("[S-760]", 0) == 0;
	}
	CHECK(s760);
	// the double frame's GlobeSwirlRender reaches frame 25 (its frame 10 stops only when _global.MinLOD is true; the movie writes extern.MinLOD)
	CHECK(globeClip);
	CHECK(p->callErrors().empty());
}

TEST_CASE("hud2 palantir: an evil side's PlayerMagic button shows the Ring (the power cap state is Living World only and not sent)")
{
	if (!haveWorld("hud2 palantir ring"))
	{
		return;
	}
	PalantirRig h(shared(), "Player_2");
	REQUIRE(h.rig.local->getPlayerTemplate() != nullptr);
	REQUIRE(h.rig.local->getPlayerTemplate()->m_evil);
	h.frames(60);
	CHECK(h.label("PalantirButtons.Buttons.PlayerMagic") == "_ring");
	CHECK(h.label("PalantirButtons.Buttons.Objectives") == "_ring");
	CHECK(h.playerMagicDraws(219)); // the Ring's `_up` art
	CHECK_FALSE(h.playerMagicDraws(176));
	CHECK(h.hud->palantir()->callErrors().empty());
}

TEST_CASE("hud2 palantir: a selected unit and a selected building show their SelectPortrait in CommandUI.Portrait; a mixed selection the side's multi portrait")
{
	if (!haveWorld("hud2 palantir portrait"))
	{
		return;
	}
	PalantirRig h(shared());
	const Coord3D c = h.rig.freeSpot(2800, 1400, 200.0f);
	Object *b = h.rig.make("GondorBarracks", c.x, c.y);
	Object *u = h.rig.make("GondorFighterHorde", c.x + 250.0f, c.y);
	REQUIRE(b != nullptr);
	REQUIRE(u != nullptr);
	h.rig.lookAt({ c.x, c.y, 0 });
	h.frames(20);
	AptPalantir *p = h.hud->palantir();

	// a unit
	h.hud->input().ui().selectObject(u->getID());
	h.frames(10);
	const std::string unitPortrait = h.templatePortrait(*u);
	REQUIRE(!unitPortrait.empty());
	CHECK(p->portraitShown() == unitPortrait);
	const AptPalantir::NativeImage *img = p->imageForClip(p->portraitKey(), "");
	REQUIRE(img != nullptr);
	CHECK(img->image == unitPortrait);
	CHECK(p->commandInterfaceShown());

	// a building
	h.hud->input().ui().deselectAll(false);
	h.hud->input().ui().selectObject(b->getID());
	h.frames(10);
	const std::string buildingPortrait = h.templatePortrait(*b);
	REQUIRE(!buildingPortrait.empty());
	CHECK(buildingPortrait != unitPortrait);
	CHECK(p->portraitShown() == buildingPortrait);
	REQUIRE(p->imageForClip(p->portraitKey(), "") != nullptr);
	CHECK(p->imageForClip(p->portraitKey(), "")->image == buildingPortrait);

	// both: different portraits, the controlling player's template MultiSelectionPortrait, else "MultiPortrait" (RW 0x92FF0F .. 0x92FF47)
	h.hud->input().ui().selectObject(u->getID());
	h.frames(10);
	REQUIRE(h.hud->input().ui().getSelectCount() == 2);
	const std::string multi = h.rig.local->getPlayerTemplate()->m_multiSelectionPortrait;
	CHECK(p->portraitShown() == (multi.empty() ? std::string("MultiPortrait") : multi));

	// nothing: the portrait is cleared, the interface stays shown
	h.hud->input().ui().deselectAll(false);
	h.frames(10);
	CHECK(p->portraitShown().empty());
	CHECK(p->imageForClip(p->portraitKey(), "") == nullptr);
	CHECK(p->commandInterfaceShown());
	// HUD-1's command buttons retry CreateContent / SetState until their clips' frames define them; the calls of this lane never fail
	for (const std::string &e : p->callErrors())
	{
		CHECK_MESSAGE((e.find("CommandInterface") == std::string::npos && e.find("SetPlayer") == std::string::npos), e);
	}
}

TEST_CASE("stop S-760: the Palantir portrait reports the branches it does not port")
{
	const std::vector<std::string> stops = PalantirCommandUI::acceptanceStops();
	REQUIRE(stops.size() == 2);
	CHECK(stops[0].rfind("[S-760]", 0) == 0);
	CHECK(stops[1].rfind("[S-1955]", 0) == 0); // lane HUD-5: the rank interface's unported time bars
}
