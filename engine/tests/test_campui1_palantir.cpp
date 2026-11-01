// OpenBFME retail tests, lane CAMPUI-1: the Palantir's objectives flag (RW 0x6D7647 / 0x80043E) and the scripts' button flashes (RW 0x6D455F /
// 0x6D72DF) in the real Palantir.apt. They SKIP when ROTWK_INSTALL / BFME2_INSTALL are unset. GPL-3.0.

#include "HudTestUtil.h"

#include "GameClient/GUI/AptScreens/AptPalantir.h"
#include "GameClient/GUI/ShellServices.h"
#include "GameClient/InGameHud.h"
#include "GameLogic/Economy.h"
#include "GameLogic/ScriptEngine/MissionObjectiveTracker.h"

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
	}
	~HudRig() { hud.reset(); }
	void frames(int n)
	{
		for (int i = 0; i < n; ++i)
		{
			hud->update(1.0 / 30.0);
			rig.game->advance(1.0 / 30.0);
		}
	}
	bool refused(const std::string &fn) const
	{
		for (const std::string &e : hud->palantir()->callErrors())
		{
			if (e.find(fn) != std::string::npos)
			{
				return true;
			}
		}
		return false;
	}
};
} // namespace

TEST_CASE("campui1 palantir: a newly shown objective flashes the flag until the objectives screen opens; FLASH_OBJECTIVES_BUTTON flashes it for its "
		  "seconds (Palantir.apt's FlashObjectivesButton / SetObjectivesButtonFlashEffectState)")
{
	if (!haveWorld("campui1 palantir"))
	{
		return;
	}
	SharedWorld &s = shared();
	HudRig h(s);
	h.frames(30);
	REQUIRE(h.hud->palantir() != nullptr);
	CHECK_FALSE(h.hud->palantirObjectivesFlashing()); // the skirmish map has no list: nothing shown
	// a campaign-like list: two objectives (shown by default), the second hidden by a script
	MissionObjectiveTracker &t = h.rig.logic().missionObjectives();
	t.reset();
	t.installList(std::vector<MissionObjective>(2));
	t.setShown(1, false);
	h.frames(2);
	const bool flashable = h.rig.logic().economy().context().gameMode != 2; // RW 0x625456: no flash in a skirmish
	CHECK(h.hud->palantirObjectivesFlashing() == flashable);
	h.hud->setObjectivesScreenOpen(true); // the screen clears the flash
	h.frames(2);
	CHECK_FALSE(h.hud->palantirObjectivesFlashing());
	h.hud->setObjectivesScreenOpen(false);
	h.frames(2);
	CHECK_FALSE(h.hud->palantirObjectivesFlashing()); // nothing new
	t.setShown(1, true); // SHOW_MISSION_OBJECTIVE(2)
	h.frames(2);
	CHECK(h.hud->palantirObjectivesFlashing() == flashable);
	CHECK_FALSE(h.refused("FlashObjectivesButton"));
	// FLASH_OBJECTIVES_BUTTON(2): 60 client frames
	h.hud->flashPalantirButton(1, 2);
	h.frames(1);
	CHECK(h.hud->palantir()->buttonFlashing(1));
	h.frames(55);
	CHECK(h.hud->palantir()->buttonFlashing(1));
	h.frames(6);
	CHECK_FALSE(h.hud->palantir()->buttonFlashing(1));
	CHECK_FALSE(h.refused("SetObjectivesButtonFlashEffectState"));
}
