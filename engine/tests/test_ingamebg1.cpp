// OpenBFME unit tests. GPL-3.0.
// Lane INGAMEBG-1: the in-game background behind the in-game screens (the owner's report of 2026-10-29: the Pause menu and the Powers screen are
// washed out). Background.apt's ShowInGameBackground (RW 0x6230B6 mode 2) goes to the frame _show_IG_BG, whose clip InGameBackground is a View3D of
// SFE_MenuOverlay (MenuFrameAndBg.apt). The screens that show it on opening and hide it when they go are the binary's: the quit menu (ctor RW 0x92262A,
// dtor RW 0x921A96), the spell store / Powers screen (ctor RW 0x823B09 under + 0x2A3, its close button RW 0x8228F6 and its dtor RW 0x823394, each once),
// and the objectives (ctor RW 0x8E9422, dtor RW 0x8E8D84). The View3D's own scene has no fog (SceneClass ctor RW 0x5424E0: FogEnabled false), so
// the map's hardware fog is not applied to its model (W3D_NO_SCENE_FOG_BIT). SKIP loudly without the retail installs.

#include "doctest.h"

#include "StartShellFx.h"

#include "GameClient/GUI/AptScreens/AptObjectivesMenu.h"
#include "GameClient/GUI/AptScreens/AptQuitMenu.h"
#include "GameClient/GUI/AptScreens/AptSpellStore.h"
#include "GameClient/GUI/ShellEnvironment.h"

#include "GameEngineDevice/W3DDevice/GameClient/W3DHardwareFog.h"
#include "Libraries/Source/Apt/Apt.h"
#include "Libraries/Source/Apt/AptRenderList.h"
#include "Libraries/WWVegas/WW3D2/w3dshadergen.h"

#include <memory>
#include <string>

namespace
{
// the visible View3D clips of SFE_MenuOverlay on the background level (the in-game background as drawn)
int overlayDrawn(starttest::ShellFx &fx)
{
	AptRenderList rl;
	fx.wm->apt().buildRenderList(rl);
	int n = 0;
	for (const AptRenderCommand &c : rl.commands)
	{
		if (c.level == fx.wm->backgroundLevel() && c.kind == AptRenderCommand::Kind::Placeholder && c.renderObject == "SFE_MenuOverlay" && c.color.mul[3] > 0.0f)
		{
			++n;
		}
	}
	return n;
}

struct Game
{
	starttest::ShellFx fx;
	QuitMenuContext context;

	explicit Game(starttest::Shared &s) : fx(s, 7)
	{
		fx.environment.services = &fx.services;
		REQUIRE(fx.wm->loadBackground());
		fx.tick(3);
		REQUIRE(fx.wm->backgroundMode() == 0); // a running game: no background
		REQUIRE(overlayDrawn(fx) == 0);
		context.inGame = true;
		context.gameMode = 2; // a skirmish
		context.gameKind = 3;
		fx.environment.quitMenu = &context;
	}
};
} // namespace

TEST_CASE("ingamebg1 retail: the Pause menu, the Powers screen and the objectives show the in-game background (SFE_MenuOverlay) while open and hide it when they go")
{
	OPENBFME_REQUIRE_START(s);
	Game g(*s);
	SUBCASE("the Pause menu (RW 0x92262A / 0x921A96)")
	{
		auto quit = std::make_unique<AptQuitMenu>(*g.fx.wm, *g.fx.shell, g.fx.environment);
		REQUIRE(quit->level() >= 0); // tolerance: loaded into the first free slot, whichever the window manager has
		g.fx.tick(10);
		CHECK(g.fx.wm->backgroundMode() == 2);
		CHECK(overlayDrawn(g.fx) == 1);
		quit.reset(); // Resume
		CHECK(g.fx.wm->backgroundMode() == 0);
		g.fx.tick(150); // HideInGameBackground("0") plays _hide_IG_BG (its own InGameBackground clip turns the model back, ~135 frames), then _no_BG
		CHECK(overlayDrawn(g.fx) == 0);
	}
	SUBCASE("the Powers screen (RW 0x823B09; the close button RW 0x8228F6 hides it once, the dtor RW 0x823394 does not again)")
	{
		auto store = std::make_unique<AptSpellStore>(*g.fx.wm, *g.fx.shell);
		REQUIRE(store->level() >= 0); // tolerance: loaded into the first free slot, whichever the window manager has
		g.fx.tick(10);
		CHECK(g.fx.wm->backgroundMode() == 2);
		CHECK(overlayDrawn(g.fx) == 1);
		g.fx.wm->fscommand("AptSpellStore::OnBttnClose", "");
		CHECK(store->closing());
		CHECK(g.fx.wm->backgroundMode() == 0);
		// a screen opened while the store fades out shows its background; the store's dtor must not hide it again (+ 0x2A3 already set)
		g.fx.wm->setBackground(2);
		store.reset();
		CHECK(g.fx.wm->backgroundMode() == 2);
	}
	SUBCASE("the Powers screen closed without its button (Escape, the HUD): the dtor RW 0x823394 hides it")
	{
		auto store = std::make_unique<AptSpellStore>(*g.fx.wm, *g.fx.shell);
		REQUIRE(store->level() >= 0); // tolerance: loaded into the first free slot, whichever the window manager has
		g.fx.tick(10);
		CHECK(g.fx.wm->backgroundMode() == 2);
		store.reset();
		CHECK(g.fx.wm->backgroundMode() == 0);
		g.fx.tick(150);
		CHECK(overlayDrawn(g.fx) == 0);
	}
	SUBCASE("the objectives (RW 0x8E9422 / 0x8E8D84)")
	{
		auto objectives = std::make_unique<AptObjectivesMenu>(*g.fx.wm, *g.fx.shell, g.fx.services, g.fx.environment);
		REQUIRE(objectives->level() >= 0); // tolerance: loaded into the first free slot, whichever the window manager has
		g.fx.tick(10);
		CHECK(g.fx.wm->backgroundMode() == 2);
		CHECK(overlayDrawn(g.fx) == 1);
		objectives.reset();
		CHECK(g.fx.wm->backgroundMode() == 0);
		g.fx.tick(150);
		CHECK(overlayDrawn(g.fx) == 0);
	}
}

TEST_CASE("ingamebg1: an instance of a scene without fog (the View3D's, RW 0x5424E0) skips the map's hardware fog; the infantry light keeps its bit")
{
	W3DShaderKey key;
	key.Texturing = true;
	key.Tex[0] = true;
	key.Fog = W3DHardwareFog::FOG_ENABLE;
	const std::string code = Generate_W3D_Shader_Code(key);
	CHECK(code.find("v_instance.x = (px < 0.0 ? 1.0 : 0.0) + ((wi & " + std::to_string(W3D_NO_SCENE_FOG_BIT) + ") != 0 ? 2.0 : 0.0);") != std::string::npos);
	CHECK(code.find("cur.rgb = v_instance.x > 1.5 ? clamp(cur.rgb, 0.0, 1.0) : w3d_apply_fog(") != std::string::npos);
	CHECK(code.find("v_instance.x > 0.5") == std::string::npos); // every infantry-light test reads the low bit (mod 2)
	// the bit sits right above the occlusion bits; every bit set is 2^22 - 1, below 2^24 (INSTANCE_CUSTOM.w stays exact in a float)
	CHECK(W3D_NO_SCENE_FOG_BIT == (W3D_OCCLUSION_OCCLUDER_BIT << 1));
	CHECK((1023 + 1024 * 15 + 16384 + (31 << W3D_OCCLUSION_SLOT_SHIFT) + W3D_OCCLUSION_OCCLUDER_BIT + W3D_NO_SCENE_FOG_BIT) == (1 << 22) - 1);
}
