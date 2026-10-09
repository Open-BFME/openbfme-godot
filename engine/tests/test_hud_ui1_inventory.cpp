// OpenBFME retail tests of lane UI-1's transport inventory on the control bar (S-1103 / S-1104): a Gondor battle tower's command set holds two
// Command_ExitGarrison (EXIT_CONTAINER) buttons; with an archer horde inside, the first shows the horde's ButtonImage and is enabled, a press is MSG_EXIT with
// the rider and the tower (RW 0x94251F / 0x942395 / 0x940FEF) and the archers come out; a siege tower's Command_EvacuateSiegeTower is enabled only while it holds
// someone (RW 0x942733 case 0x11): restricted while empty. They share the HUD tests' retail world and SKIP when ROTWK_INSTALL / BFME2_INSTALL are unset. GPL-3.0.

#include "HudTestUtil.h"

#include "GameClient/GUI/ShellServices.h"
#include "GameClient/InGameHud.h"
#include "GameLogic/AI/AICommandSink.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"

#include <set>

using namespace hudtest;

namespace
{
struct InventoryHudRig
{
	Rig rig;
	RecordingShellServices services;
	std::unique_ptr<InGameHud> hud;
	explicit InventoryHudRig(SharedWorld &s, const char *faction = "FactionMen") : rig(s, "map mp fall back 4p", faction)
	{
		InGameHud::Config cfg{ *rig.game, *s.world, *s.mount->fs, services, rig.view, s.mouse, s.meta, nullptr };
		hud = std::make_unique<InGameHud>(cfg);
		std::string error;
		REQUIRE_MESSAGE(hud->boot(&error), error);
		hud->setWindowSize(1024, 768);
		rig.view.setScreen(1024, 768);
	}
	~InventoryHudRig() { hud.reset(); }
	void frames(int n)
	{
		for (int i = 0; i < n; ++i)
		{
			hud->update(0.033);
			rig.game->advance(0.033);
		}
	}
	// the selection the control bar reads (InGameUI); the press's MSG_EXIT names its rider and container itself (RW 0x940FEF), so the logic's selection is not needed
	void select(Object *o)
	{
		rig.lookAt(*o->getPosition());
		hud->input().ui().selectObject(o->getID());
		frames(5);
	}
	std::vector<ControlBarButton> buttons(GUICommandType type)
	{
		// lane HUD-4: a button can be on both bars (InPalantir and Radial) or on neither (offBarButtons, retail's placement RW 0x92FF5C / 0x92F082): each slot once,
		// the arc's copy first
		std::vector<ControlBarButton> out;
		std::set<int> seen;
		for (const auto *v : { &hud->controlBar().palantirButtons(), &hud->controlBar().sideButtons(), &hud->controlBar().offBarButtons() })
		{
			for (const ControlBarButton &b : *v)
			{
				if (b.button && b.button->m_command == type && seen.insert(b.slot).second)
				{
					out.push_back(b);
				}
			}
		}
		std::sort(out.begin(), out.end(), [](const ControlBarButton &a, const ControlBarButton &b) { return a.slot < b.slot; });
		return out;
	}
};

std::string buttonImageOf(const Object &o)
{
	if (const FieldValue *v = o.getTemplate()->getFinalOverride()->findField("ButtonImage"))
	{
		if (const std::string *s = std::get_if<std::string>(v))
		{
			return *s;
		}
	}
	return std::string();
}
} // namespace

TEST_CASE("ui1 hud: a battle tower's EXIT_CONTAINER buttons show its garrison (the horde's image, enabled) and a press lets the horde out (MSG_EXIT)")
{
	if (!haveWorld("ui1 hud inventory"))
	{
		return;
	}
	SharedWorld &s = shared();
	InventoryHudRig h(s);
	float mx = 0, my = 0;
	REQUIRE(h.rig.logic().terrain() != nullptr);
	REQUIRE(h.rig.logic().terrain()->getExtent(0, mx, my));
	const Coord3D c0 = h.rig.freeSpot(mx * 0.5f, my * 0.5f, 300.0f);
	Object *tower = h.rig.make("GondorKeep", c0.x, c0.y);
	Object *archers = h.rig.make("GondorArcherHorde", c0.x - 160.0f, c0.y);
	REQUIRE(tower != nullptr);
	REQUIRE(archers != nullptr);
	REQUIRE(tower->getContain() != nullptr);
	CHECK(tower->getContain()->isDisplayedOnControlBar()); // slot 0xC8 RW 0x8BD372
	h.frames(3);
	h.select(tower);
	REQUIRE(h.hud->input().ui().selected() == std::vector<ObjectID>{ tower->getID() });
	// empty: GenericSentryTowerCommandSet's two Command_ExitGarrison buttons, disabled, with the button's own image
	std::vector<ControlBarButton> exits = h.buttons(GUI_COMMAND_EXIT_CONTAINER);
	REQUIRE(exits.size() == 2);
	for (const ControlBarButton &b : exits)
	{
		CHECK(b.state == ButtonState::Restricted);
		CHECK(b.rider == INVALID_ID);
		CHECK(b.image == "BCCommand_EvacuateAll");
	}
	CHECK_FALSE(h.hud->controlBar().pressButton(exits[0].slot, exits[0].inPalantir));
	// the archers go in
	AIUpdateInterface *ai = archers->getAIUpdateInterface();
	REQUIRE(ai != nullptr);
	REQUIRE(ai->aiEnter(tower, CMD_FROM_SCRIPT));
	HordeContainInterface *hc = archers->getContain()->getHordeContainInterface();
	REQUIRE(hc != nullptr);
	int frames = 0;
	while (frames < 900 && !(archers->getContainedBy() == tower && hc->allMembersEntered()))
	{
		h.frames(1);
		++frames;
	}
	REQUIRE(archers->getContainedBy() == tower);
	h.frames(5);
	exits = h.buttons(GUI_COMMAND_EXIT_CONTAINER);
	REQUIRE(exits.size() == 2);
	// RW 0x942395: the rider takes TransportSlotCount slots from the first EXIT_CONTAINER slot, the first enabled with its ButtonImage
	CHECK(exits[0].rider == archers->getID());
	CHECK(exits[0].state == ButtonState::Enabled);
	CHECK(exits[0].image == buttonImageOf(*archers));
	CHECK_FALSE(exits[0].image.empty());
	for (std::size_t i = 1; i < exits.size(); ++i)
	{
		CHECK(exits[i].state == ButtonState::Restricted);
	}
	// the press: MSG_EXIT (RW 0x940FEF) through the command list; the horde leaves the tower
	REQUIRE(h.hud->controlBar().pressButton(exits[0].slot, exits[0].inPalantir));
	frames = 0;
	while (frames < 900 && archers->getContainedBy() == tower)
	{
		h.frames(1);
		++frames;
	}
	CHECK(archers->getContainedBy() == nullptr);
	h.frames(5);
	exits = h.buttons(GUI_COMMAND_EXIT_CONTAINER);
	REQUIRE(exits.size() == 2);
	CHECK(exits[0].rider == INVALID_ID);
	CHECK(exits[0].state == ButtonState::Restricted);
}

TEST_CASE("ui1 hud: a siege tower's Command_EvacuateSiegeTower is restricted while it holds nobody (RW 0x942733 case 0x11)")
{
	if (!haveWorld("ui1 hud evacuate"))
	{
		return;
	}
	SharedWorld &s = shared();
	InventoryHudRig h(s, "FactionMordor");
	float mx = 0, my = 0;
	REQUIRE(h.rig.logic().terrain()->getExtent(0, mx, my));
	const Coord3D c0 = h.rig.freeSpot(mx * 0.5f, my * 0.5f, 300.0f);
	Object *siege = h.rig.make("MordorSiegeTower", c0.x, c0.y);
	REQUIRE(siege != nullptr);
	h.frames(3);
	h.select(siege);
	REQUIRE(h.hud->input().ui().selected() == std::vector<ObjectID>{ siege->getID() });
	// the tower's crew is a second list (contain slot 0x11C), not the riders: getContainCount (slot 0x114) is 0 (2.01's siege towers have Slots 0: infantry use the
	// docked tower's portal, S-1103), so the button is shown restricted and a press does nothing
	REQUIRE(siege->getContain() != nullptr);
	CHECK(siege->getContain()->getContainCount() == 0u);
	CHECK(siege->getContain()->isDisplayedOnControlBar());
	std::vector<ControlBarButton> evac = h.buttons(GUI_COMMAND_EVACUATE);
	REQUIRE(evac.size() == 1);
	CHECK(evac[0].state == ButtonState::Restricted);
	CHECK_FALSE(h.hud->controlBar().pressButton(evac[0].slot, evac[0].inPalantir));
}
