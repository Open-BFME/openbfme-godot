// OpenBFME retail tests of the Palantir's centre and side state (lane HUD-2): the Evenstar / Ring of the PlayerMagic button (SetPlayerButtonsState), the
// command interface with the selection's portrait (PalantirCommandUI) and the RenderGlobe clip of the globe. They SKIP when ROTWK_INSTALL / BFME2_INSTALL are
// unset. GPL-3.0.

#include "HudTestUtil.h"

#include "Common/PlayerTemplate.h"
#include "Common/ModelState.h"
#include "GameClient/GUI/ShellServices.h"
#include "GameClient/InGameHud.h"
#include "GameClient/InGameHeroSelect.h"
#include "GameClient/PalantirCommandUI.h"
#include "GameLogic/Object/ExperienceTracker.h"
#include "Libraries/Source/Apt/AptCharacterInst.h"
#include "Libraries/Source/Apt/AptRenderList.h"
#include "Libraries/Source/Apt/AptButtonInst.h"
#include "Libraries/Source/Apt/AptInput.h"
#include "GameClient/AptCanvas.h"

#include <algorithm>
#include <map>
#include <set>
#include <variant>

using namespace hudtest;

namespace
{
AptButtonInst *firstButton(AptCharacterInst *instance)
{
	if (!instance)
	{
		return nullptr;
	}
	if (AptButtonInst *button = instance->asButton())
	{
		return button;
	}
	if (AptSpriteInst *sprite = instance->asSprite())
	{
		for (AptCharacterInst *child : sprite->children())
		{
			if (AptButtonInst *button = firstButton(child))
			{
				return button;
			}
		}
	}
	return nullptr;
}

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
// The six outer frames stay visible while empty; their contents are removed.
void checkEmptyPanel(PalantirRig &h)
{
	AptPalantir *p = h.hud->palantir();
	REQUIRE(p);
	REQUIRE(h.clip("CommandButtons"));
	CHECK(h.clip("CommandButtons")->visible);
	CHECK(h.label("CommandButtons") == "_show");
	CHECK(h.hud->controlBar().palantirButtons().empty());
	std::map<std::string, std::string> framesByIndex;
	for (const AptPalantir::ButtonFrame &reported : p->buttonFrames())
	{
		std::string path = reported.path;
		if (path.rfind("_level", 0) == 0)
		{
			path = path.substr(path.find('.') + 1);
		}
		if (path.rfind("CommandButtons.", 0) == 0 && reported.index.size() == 1 && reported.index >= "0" && reported.index <= "5")
		{
			framesByIndex[reported.index] = path;
		}
	}
	REQUIRE(framesByIndex.size() == AptPalantir::kArcPositions);
	std::set<std::string> uniqueFrames;
	for (const auto &entry : framesByIndex)
	{
		uniqueFrames.insert(entry.second);
	}
	REQUIRE(uniqueFrames.size() == AptPalantir::kArcPositions);
	AptRenderList render;
	h.hud->apt().buildRenderList(render);
	for (int i = 0; i < AptPalantir::kArcPositions; ++i)
	{
		const std::string frame = framesByIndex.at(std::to_string(i));
		AptSpriteInst *outer = h.clip(frame);
		REQUIRE_MESSAGE(outer != nullptr, frame);
		CHECK(outer->visible);
		CHECK(h.clip(frame + ".content") == nullptr);
		const std::string content = "_level" + std::to_string(p->level()) + "." + frame + ".content";
		CHECK(p->imageFor(content + "_Image") == nullptr);
		CHECK(p->timerFor(content + "_Timer") == -1.0f);
		const std::string *count = h.hud->windows().aptText("APT:" + content + "_ProductionCount");
		CHECK((count == nullptr || *count == " "));
		for (const AptRenderCommand &command : render.commands)
		{
			CHECK(command.path.find(frame + ".content") == std::string::npos);
		}
	}
}

// the arc position's clip as the movie reported it (AptPalantir::buttonFrames: index "0" .. "5", the path without its _level prefix)
std::string arcFrame(PalantirRig &h, int position)
{
	for (const AptPalantir::ButtonFrame &reported : h.hud->palantir()->buttonFrames())
	{
		std::string path = reported.path;
		if (path.rfind("_level", 0) == 0)
		{
			path = path.substr(path.find('.') + 1);
		}
		if (path.rfind("CommandButtons.", 0) == 0 && reported.index == std::to_string(position))
		{
			return path;
		}
	}
	FAIL("no reported arc frame for position " << position);
	return std::string();
}

void checkPassiveContent(PalantirRig &h, const std::string &frame)
{
	AptSpriteInst *content = h.clip(frame + ".content");
	REQUIRE(content);
	const int passive = content->labelFrame("_static");
	const int next = content->labelFrame("_notReady");
	// tolerance: missing labels return -1; a valid timeline frame is nonnegative.
	REQUIRE(passive >= 0);
	// tolerance: the next named label bounds the passive state's timeline.
	REQUIRE(next > passive);
	// tolerance: allow animation within the passive state's timeline, not another state's frames.
	CHECK(content->frame >= passive);
	// tolerance: the next named label begins the following state.
	CHECK(content->frame < next);
	CHECK(content->visible);
}
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
	checkEmptyPanel(h);
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

TEST_CASE("hud2 palantir: widescreen panels preserve proportions and edge anchors through resizing")
{
	if (!haveWorld("hud2 widescreen"))
	{
		return;
	}
	PalantirRig hud(shared());
	hud.frames(30);
	Object *builder = hud.rig.make("MenPorter", 2800, 1400);
	REQUIRE(builder != nullptr);
	hud.hud->input().ui().selectObject(builder->getID());
	hud.frames(40);
	REQUIRE(hud.hud->palantir()->sideBarShown());
	AptSpriteInst *frame = hud.clip("PalantirFrame");
	AptSpriteInst *side = hud.clip("SideCommandBar");
	REQUIRE(frame != nullptr);
	REQUIRE(side != nullptr);
	const AptMatrix originalFrame = frame->matrix;
	const AptMatrix originalSide = side->matrix;
	struct Viewport
	{
		int width, height;
		float correction;
	};
	for (const Viewport &viewport : { Viewport{ 1024, 768, 1.0f }, Viewport{ 1280, 720, 0.75f }, Viewport{ 2560, 1080, 0.5625f },
		Viewport{ 1280, 960, 1.0f }, Viewport{ 1280, 720, 0.75f } })
	{
		INFO(viewport.width, "x", viewport.height);
		hud.hud->setWindowSize(viewport.width, viewport.height);
		CHECK(frame->matrix.a == originalFrame.a);
		CHECK(frame->matrix.tx == originalFrame.tx);
		CHECK(side->matrix.a == originalSide.a);
		CHECK(side->matrix.tx == originalSide.tx);
		CHECK(frame->viewportTransform.a == viewport.correction);
		CHECK(frame->viewportTransform.tx == 0.0f);
		CHECK(side->viewportTransform.a == viewport.correction);
		CHECK(side->viewportTransform.tx == 1024.0f * (1.0f - viewport.correction));
		AptStageMapping mapping;
		mapping.windowW = (float)viewport.width;
		mapping.windowH = (float)viewport.height;
		CHECK(frame->viewportTransform.a * mapping.scaleX() == mapping.scaleY());
		float stageX, stageY, windowX, windowY;
		frame->viewportTransform.apply(0, 768, stageX, stageY);
		mapping.stageToWindow(stageX, stageY, windowX, windowY);
		CHECK(windowX == 0.0f);
		CHECK(windowY == (float)viewport.height);
		side->viewportTransform.apply(1024, 384, stageX, stageY);
		mapping.stageToWindow(stageX, stageY, windowX, windowY);
		CHECK(windowX == (float)viewport.width);
		CHECK(windowY == (float)viewport.height * 0.5f);
		for (const char *path : { "PalantirButtons.Buttons.Options", "SideCommandBar" })
		{
			INFO(path);
			AptButtonInst *button = firstButton(hud.hud->apt().resolvePath(hud.hud->apt().level(hud.hud->palantir()->level()), path));
			REQUIRE(button != nullptr);
			float left, top, right, bottom;
			REQUIRE(button->contentBounds(left, top, right, bottom));
			bool hit = false;
			for (int row = 1; row < 16 && !hit; ++row)
			{
				for (int column = 1; column < 16 && !hit; ++column)
				{
					button->globalMatrix().apply(left + (right - left) * (float)column / 16.0f, top + (bottom - top) * (float)row / 16.0f, stageX, stageY);
					if (hud.hud->apt().input().hitTestButtons(stageX, stageY) == button)
					{
						mapping.stageToWindow(stageX, stageY, windowX, windowY);
						hit = hud.hud->isOverGui((int)windowX, (int)windowY);
					}
				}
			}
			CHECK(hit);
		}
	}
}

TEST_CASE("hud2 palantir: widescreen radar pings retain their supplied stage position")
{
	if (!haveWorld("hud2 widescreen radar pings"))
	{
		return;
	}
	PalantirRig hud(shared());
	hud.frames(30);
	AptSpriteInst *radarPings = hud.clip("RadarPings");
	AptSpriteInst *pings = hud.clip("RadarPings.PingStage");
	REQUIRE(radarPings != nullptr);
	REQUIRE(pings != nullptr);
	const size_t before = pings->children().size();
	REQUIRE(hud.hud->palantir()->createRadarPing(123, "PingAttack"));
	hud.frames(1);
	REQUIRE(pings->children().size() == before + 1);
	AptCharacterInst *ping = pings->children().back();
	for (int width : { 1024, 2048, 4096, 1024 })
	{
		hud.hud->setWindowSize(width, 768);
		const float stageX = 128.0f * radarPings->viewportTransform.a;
		REQUIRE(hud.hud->palantir()->moveRadarPing(123, stageX, 640));
		float actualX, actualY;
		ping->globalMatrix().apply(0, 0, actualX, actualY);
		CHECK(actualX == stageX);
		CHECK(actualY == 640.0f);
	}
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

	// Deselecting a unit must retain the six empty frames and their parent panel.
	h.hud->input().ui().deselectAll(false);
	h.frames(20);
	checkEmptyPanel(h);

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
	checkEmptyPanel(h);
	// HUD-1's command buttons retry CreateContent / SetState until their clips' frames define them; the calls of this lane never fail
	for (const std::string &e : p->callErrors())
	{
		CHECK_MESSAGE((e.find("CommandInterface") == std::string::npos && e.find("SetPlayer") == std::string::npos), e);
	}
}

TEST_CASE("hud2 palantir: passive contents are removed on context changes, deselection and selected-unit destruction")
{
	if (!haveWorld("hud2 palantir passive context"))
	{
		return;
	}
	PalantirRig h(shared());
	h.frames(30);
	AptPalantir *p = h.hud->palantir();
	REQUIRE(p);
	checkEmptyPanel(h);
	CHECK_FALSE(p->rankInterfaceShown());

	const Coord3D c = h.rig.freeSpot(2800, 1400, 250.0f);
	Object *boromir = h.rig.make("GondorBoromir", c.x, c.y);
	REQUIRE(boromir);
	ExperienceTracker *xp = boromir->getExperienceTracker();
	REQUIRE(xp);
	REQUIRE(xp->isTrainable());
	xp->gainLevels(20, false);
	h.hud->input().ui().selectObject(boromir->getID());
	h.frames(10);
	REQUIRE(p->rankInterfaceShown());

	std::vector<int> passiveSlots;
	for (const ControlBarButton &button : h.hud->controlBar().palantirButtons())
	{
		if (button.button && button.button->hasOption(COMMAND_OPTION_NONPRESSABLE))
		{
			passiveSlots.push_back(button.position);
		}
	}
	REQUIRE_FALSE(passiveSlots.empty());
	for (int slot : passiveSlots)
	{
		// tolerance: the passive button's position must fit the six-frame arc table before deriving its clip path.
		REQUIRE((slot >= 0 && slot < AptPalantir::kArcPositions));
		const std::string frame = arcFrame(h, slot);
		checkPassiveContent(h, frame);
		const std::string content = "_level" + std::to_string(p->movieLevel()) + "." + frame + ".content";
		REQUIRE(p->imageFor(content + "_Image") != nullptr);
	}

	auto checkOldPassiveSlotsAreEmpty = [&] {
		AptRenderList render;
		h.hud->apt().buildRenderList(render);
		for (int slot : passiveSlots)
		{
			const bool occupied = std::any_of(h.hud->controlBar().palantirButtons().begin(), h.hud->controlBar().palantirButtons().end(),
				[slot](const ControlBarButton &button) { return button.position == slot; });
			if (occupied)
			{
				continue;
			}
			const std::string frame = arcFrame(h, slot);
			const std::string content = "_level" + std::to_string(p->movieLevel()) + "." + frame + ".content";
			CHECK(h.clip(frame + ".content") == nullptr);
			CHECK(p->imageFor(content + "_Image") == nullptr);
			CHECK(p->timerFor(content + "_Timer") == -1.0f);
			for (const AptRenderCommand &command : render.commands)
			{
				CHECK(command.path.find(frame + ".content") == std::string::npos);
			}
		}
	};

	Object *secondHero = h.rig.make("GondorAragornMP", c.x + 250.0f, c.y);
	REQUIRE(secondHero);
	ExperienceTracker *secondXp = secondHero->getExperienceTracker();
	REQUIRE(secondXp);
	REQUIRE(secondXp->isTrainable());
	secondXp->gainLevels(20, false);
	h.hud->input().ui().deselectAll(false);
	h.hud->input().ui().selectObject(secondHero->getID());
	h.frames(10);
	const PalantirCommandUI::RankInfo expectedRank = PalantirCommandUI::rankInfo(h.hud->input().context(), *secondHero);
	REQUIRE(expectedRank.type == 0);
	CHECK(expectedRank.rank == secondXp->getRank());
	CHECK(p->rankInterfaceShown());
	CHECK(p->rankProgressShown() == expectedRank.progress);
	checkOldPassiveSlotsAreEmpty();

	h.hud->input().ui().deselectAll(false);
	h.hud->input().ui().selectObject(boromir->getID());
	h.frames(10);
	REQUIRE(p->rankInterfaceShown());
	for (int slot : passiveSlots)
	{
		const auto button = std::find_if(h.hud->controlBar().palantirButtons().begin(), h.hud->controlBar().palantirButtons().end(),
			[slot](const ControlBarButton &candidate) { return candidate.position == slot; });
		REQUIRE(button != h.hud->controlBar().palantirButtons().end());
		REQUIRE(button->button);
		CHECK(button->button->hasOption(COMMAND_OPTION_NONPRESSABLE));
		const std::string frame = arcFrame(h, slot);
		checkPassiveContent(h, frame);
	}
	h.hud->input().ui().deselectAll(false);
	h.frames(10);
	checkEmptyPanel(h);
	CHECK_FALSE(p->rankInterfaceShown());

	Object *fighter = h.rig.make("GondorFighter", c.x + 250.0f, c.y);
	REQUIRE(fighter);
	h.hud->input().ui().deselectAll(false);
	h.hud->input().ui().selectObject(fighter->getID());
	h.frames(10);
	// (the rank interface's own rules are test_hud2_palantir's rank cases; this case checks only that no passive content is left)
	for (const ControlBarButton &button : h.hud->controlBar().palantirButtons())
	{
		CHECK_FALSE((button.button && button.button->hasOption(COMMAND_OPTION_NONPRESSABLE)));
	}
	checkOldPassiveSlotsAreEmpty();

	h.hud->input().ui().deselectAll(false);
	h.frames(10);
	checkEmptyPanel(h);
	CHECK_FALSE(p->rankInterfaceShown());

	h.hud->input().ui().selectObject(boromir->getID());
	h.frames(5);
	REQUIRE(boromir->getID() == h.hud->controlBar().sourceObject());
	REQUIRE(p->rankInterfaceShown());
	for (int slot : passiveSlots)
	{
		const auto button = std::find_if(h.hud->controlBar().palantirButtons().begin(), h.hud->controlBar().palantirButtons().end(),
			[slot](const ControlBarButton &candidate) { return candidate.position == slot; });
		REQUIRE(button != h.hud->controlBar().palantirButtons().end());
		REQUIRE(button->button);
		CHECK(button->button->hasOption(COMMAND_OPTION_NONPRESSABLE));
		const std::string frame = arcFrame(h, slot);
		checkPassiveContent(h, frame);
		const std::string content = "_level" + std::to_string(p->movieLevel()) + "." + frame + ".content";
		CHECK(p->imageFor(content + "_Image") != nullptr);
	}
	const ObjectID boromirId = boromir->getID();
	h.rig.logic().destroyObject(boromir);
	h.frames(5);
	CHECK(h.rig.logic().findObjectByID(boromirId) == nullptr);
	CHECK_FALSE(p->rankInterfaceShown());
	checkEmptyPanel(h);
}

TEST_CASE("stop S-760: the Palantir portrait reports the branches it does not port")
{
	const std::vector<std::string> stops = PalantirCommandUI::acceptanceStops();
	REQUIRE(stops.size() == 1);
	CHECK(stops[0].rfind("[S-760]", 0) == 0);
}

TEST_CASE("hud2 palantir: custom heroes use their model-state portraits rather than the generic Wanderer")
{
	if (!haveWorld("hud2 custom hero portrait"))
	{
		return;
	}
	PalantirRig h(shared());
	Object *maiden = h.rig.make("CreateAHero", 2800, 1400);
	Object *captain = h.rig.make("CreateAHero", 3000, 1400);
	REQUIRE(maiden);
	REQUIRE(captain);
	// Independent retail CreateAHeroModels.inc expectations: CREATE_A_HERO_02
	// names CPShieldMaiden; CREATE_A_HERO_00 names CPCaptainofGondor.
	maiden->setModelConditionState(ModelCondition::indexOf("CREATE_A_HERO_02"), true);
	captain->setModelConditionState(ModelCondition::indexOf("CREATE_A_HERO_00"), true);
	CHECK(PalantirCommandUI::objectPortrait(*maiden) == "CPShieldMaiden");
	CHECK(PalantirCommandUI::objectPortrait(*captain) == "CPCaptainofGondor");
	h.hud->input().ui().selectObject(maiden->getID());
	h.frames(10);
	CHECK(h.hud->palantir()->portraitShown() == "CPShieldMaiden");
	// Both objects have the same template, but different portraits: do not
	// skip the second object using the ordinary template comparison cache.
	h.hud->input().ui().selectObject(captain->getID());
	h.frames(10);
	CHECK(h.hud->palantir()->portraitShown() == h.rig.local->getPlayerTemplate()->m_multiSelectionPortrait);
}

TEST_CASE("hud2 custom hero buttons: model-state icons distinguish Shield Maiden and Captain")
{
	if (!haveWorld("hud2 custom hero button")) return;
	PalantirRig h(shared());
	Object *maiden = h.rig.make("CreateAHero", 2800, 1400);
	Object *captain = h.rig.make("CreateAHero", 3000, 1400);
	REQUIRE(maiden);
	REQUIRE(captain);
	maiden->setModelConditionState(ModelCondition::indexOf("CREATE_A_HERO_02"), true);
	captain->setModelConditionState(ModelCondition::indexOf("CREATE_A_HERO_00"), true);
	CHECK(InGameHeroSelect::buttonImage(*maiden) == "HICAHShieldMaiden");
	CHECK(InGameHeroSelect::buttonImage(*captain) == "HICAHCaptainGondor");
	maiden->setModelConditionState(ModelCondition::indexOf("MOUNTED"), true);
	CHECK(InGameHeroSelect::buttonImage(*maiden) == "HICAHShieldMaiden");
}
