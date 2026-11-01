// OpenBFME retail tests for lane CHAT-1: the multiplayer beacon through the player's own input (Ctrl+B, a left click, Delete) on the live game:
// PLACE_BEACON (RW 0x81F8D8 case 0x90) -> Command_PlaceBeacon -> MSG_PLACE_BEACON (RW 0x83D7AD) -> the executor (RW 0x77C634: the player's MultiplayerBeacon,
// MaxBeaconsPerPlayer 3) -> the HUD's presentation (the notice, the message line label) and MSG_SELL's beacon branch (RW 0x77B2C8). SKIPs without
// ROTWK_INSTALL / BFME2_INSTALL.

#include "doctest.h"
#include "FactATestUtil.h"

#include "Common/Thing/ThingTemplate.h"
#include "GameClient/BeaconClientUpdate.h"
#include "GameLogic/BeaconCommands.h"
#include "GameLogic/Map/TerrainLogic.h"

#include <algorithm>
#include <string>
#include <vector>

using namespace facta;

namespace
{
std::vector<Object *> beaconsOf(GameLogic &logic, const Player *p)
{
	std::vector<Object *> out;
	for (Object *o = logic.getFirstObject(); o; o = o->getNextObject())
	{
		if (o->getControllingPlayer() == p && !o->isDestroyed() && o->getTemplate()->getName() == "MultiplayerBeacon")
		{
			out.push_back(o);
		}
	}
	return out;
}

// Ctrl+B as the player types it (the HUD's key path: the CommandMap's PLACE_BEACON = KEY_B, CTRL, DOWN)
void pressCtrlB(FactRig &r)
{
	r.hud->key(::KEY_LCTRL, KEY_STATE_DOWN | KEY_STATE_LCONTROL);
	r.hud->key(::KEY_B, KEY_STATE_DOWN | KEY_STATE_LCONTROL);
	r.hud->key(::KEY_B, KEY_STATE_UP | KEY_STATE_LCONTROL);
	r.hud->key(::KEY_LCTRL, KEY_STATE_UP);
	r.frames(1);
}

void leftClick(FactRig &r, const Coord3D &at)
{
	r.rig.lookAt(at);
	r.frames(1);
	const ICoord2D p = r.rig.screenOf(at);
	r.hud->mouseMove(p.x, p.y, 0);
	r.rig.timeMs += 300;
	r.hud->mouseButton(HudInput::Button::Left, true, p.x, p.y, 0, r.rig.timeMs);
	r.rig.timeMs += 40;
	r.hud->mouseButton(HudInput::Button::Left, false, p.x, p.y, 0, r.rig.timeMs);
	r.rig.timeMs += 300;
	r.frames(3);
}
} // namespace

TEST_CASE("chat1 beacons: Ctrl+B and a click place the player's beacon in a network game only; three at most; Delete removes it")
{
	if (!hudtest::haveWorld("chat1 beacons"))
	{
		return;
	}
	SharedWorld &s = hudtest::shared();
	FactRig r(s, "FactionMen");
	GameLogic &logic = r.logic();
	REQUIRE(logic.settings().maxBeaconsPerPlayer == 3); // retail multiplayer.ini
	const ThingTemplate *beacon = BeaconCommands::beaconTemplate(logic, *r.local());
	REQUIRE(beacon);
	CHECK(beacon->getName() == "MultiplayerBeacon"); // every retail faction's BeaconName

	// outside a network game (skirmish, campaign) the key does nothing (RW 0x441B7C)
	pressCtrlB(r);
	CHECK(r.hud->input().ui().getGUICommand() == nullptr);

	r.hud->input().ui().setNetworkGame(true);
	std::vector<Coord3D> spots;
	for (int i = 0; i < 4; ++i)
	{
		spots.push_back(r.spot(1500.0f + 300.0f * (float)i, 1600.0f, 60.0f));
	}
	// a player with no structure and no unit is defeated (VictoryConditions rule 3, hasSinglePlayerBeenDefeated RW 0x80953C): the beacon FAILS
	// ("GUI:BeaconPlacementFailed" on every client, RW 0x77C927)
	pressCtrlB(r);
	REQUIRE(r.hud->input().ui().getGUICommand());
	leftClick(r, spots[0]);
	CHECK(beaconsOf(logic, r.local()).empty());
	CHECK(r.hud->beaconStats().failed == 1);
	CHECK(std::count(r.hud->missingMessageLabels().begin(), r.hud->missingMessageLabels().end(), "GUI:BeaconPlacementFailed") == 1);
	// a barracks keeps the player in the game
	REQUIRE(r.make("GondorBarracks", r.spot(2600.0f, 2400.0f, 300.0f)));
	r.frames(2);
	for (int i = 0; i < 3; ++i)
	{
		pressCtrlB(r);
		const CommandButton *gc = r.hud->input().ui().getGUICommand();
		REQUIRE(gc);
		CHECK(gc->m_name == "Command_PlaceBeacon");
		leftClick(r, spots[(size_t)i]);
		CHECK(r.hud->input().ui().getGUICommand() == nullptr); // one click, one beacon (RW 0x83D7AD clears the GUI command)
		const std::vector<Object *> b = beaconsOf(logic, r.local());
		if (b.size() != (size_t)i + 1)
		{
			std::string notices;
			for (const GameLogic::BeaconNotice &n : logic.beaconNotices())
			{
				notices += std::to_string((int)n.kind) + "/" + std::to_string(n.playerIndex) + "@" + std::to_string((int)n.position.x) + "," + std::to_string((int)n.position.y) + " ";
			}
			std::string errors;
			for (const std::string &e : logic.report().errors)
			{
				errors += e + " | ";
			}
			MESSAGE("beacon debug: notices [" << notices << "] lane " << r.rig.game->dispatch().handlerLane(MSG_PLACE_BEACON) << " unhandled "
				<< (r.rig.game->dispatch().unhandled().count(MSG_PLACE_BEACON) ? r.rig.game->dispatch().unhandled().at(MSG_PLACE_BEACON) : 0ull)
				<< " terrain " << (logic.terrain() != nullptr) << " errors " << errors << " spot " << spots[(size_t)i].x << "," << spots[(size_t)i].y);
		}
		REQUIRE(b.size() == (size_t)i + 1);
	}
	// the placer's own client: the notice placed (not hidden: a player is its own ally), the message line asked for (GUI:BeaconPlaced; this rig has no game
	// text, so the label is reported missing instead of shown)
	CHECK(r.hud->beaconStats().placed == 3);
	CHECK(r.hud->beaconStats().hidden == 0);
	CHECK(r.hud->input().ui().clientHidden().empty());
	CHECK(std::count(r.hud->missingMessageLabels().begin(), r.hud->missingMessageLabels().end(), "GUI:BeaconPlaced") == 3);
	// at the limit the key does nothing (RW 0x81F8D8 compares the count first)
	pressCtrlB(r);
	CHECK(r.hud->input().ui().getGUICommand() == nullptr);
	// a fourth MSG_PLACE_BEACON straight into the stream (a hostile client could send one): the executor refuses it (TOO_MANY, the placer's message)
	r.hud->input().stream().append(MSG_PLACE_BEACON).appendLocation(spots[3]);
	r.frames(3);
	CHECK(beaconsOf(logic, r.local()).size() == 3);
	CHECK(r.hud->beaconStats().tooMany == 1);
	CHECK(std::count(r.hud->missingMessageLabels().begin(), r.hud->missingMessageLabels().end(), "GUI:TooManyBeacons") == 1);

	// the beacon's client module: a minimap pulse every 6 logic frames (RadarPulseFrequency 1000 ms -> 5 frames, strictly more than 5 apart)
	Object *first = beaconsOf(logic, r.local()).front();
	Drawable *d = r.rig.game->drawables().findByObject(first->getID());
	REQUIRE(d);
	const BeaconClientUpdate *bcu = nullptr;
	for (const std::unique_ptr<DrawableModule> &m : d->clientModules())
	{
		bcu = bcu ? bcu : dynamic_cast<const BeaconClientUpdate *>(m.get());
	}
	REQUIRE(bcu);
	CHECK(bcu->data()->m_framesBetweenRadarPulses == 5);
	CHECK(bcu->data()->m_radarPulseDuration == 3); // 500 ms: ceil(500 * 0.005f) = 3
	const unsigned long long pulses0 = r.hud->beaconStats().pulses;
	r.frames(12);
	CHECK(r.hud->beaconStats().pulses - pulses0 == 6); // three beacons, one pulse each per six frames, twelve frames

	// Delete (SELL) with the beacon selected removes it (MSG_SELL's beacon branch)
	REQUIRE(r.select(first));
	r.hud->key(::KEY_DEL, KEY_STATE_DOWN);
	r.hud->key(::KEY_DEL, KEY_STATE_UP);
	r.frames(3);
	CHECK(beaconsOf(logic, r.local()).size() == 2);
	REQUIRE(BeaconCommands::stopLines().size() == 1);
	CHECK(BeaconCommands::stopLines()[0].rfind("[S-5151] beacons", 0) == 0);
	// with a slot free again, Ctrl+B works again
	pressCtrlB(r);
	CHECK(r.hud->input().ui().getGUICommand());
}

TEST_CASE("chat1 beacons: RW 0x67DFCE moves a location outside the map onto its nearest edge, at the ground's height")
{
	if (!hudtest::haveWorld("chat1 beacon clamp"))
	{
		return;
	}
	SharedWorld &s = hudtest::shared();
	FactRig r(s, "FactionMen");
	GameLogic &logic = r.logic();
	REQUIRE(logic.terrain());
	float hiX = 0.0f, hiY = 0.0f;
	REQUIRE(logic.terrain()->getExtent(0, hiX, hiY));
	Coord3D p{ -50.0f, hiY * 0.5f, 0.0f }; // left of the map: the low x side is the nearest
	const bool dry = BeaconCommands::clampBeaconLocation(logic, p);
	CHECK(p.x == 0.0f);
	CHECK(p.y == hiY * 0.5f);
	CHECK(p.z == logic.terrain()->getGroundHeight(0.0f, hiY * 0.5f));
	(void)dry;
	Coord3D q{ hiX * 0.5f, hiY + 300.0f, 0.0f }; // beyond the high y side
	(void)BeaconCommands::clampBeaconLocation(logic, q);
	CHECK(q.x == hiX * 0.5f);
	CHECK(q.y == hiY);
	Coord3D inside{ hiX * 0.5f, hiY * 0.5f, 123.0f }; // strictly inside: unchanged (z too)
	(void)BeaconCommands::clampBeaconLocation(logic, inside);
	CHECK(inside.x == hiX * 0.5f);
	CHECK(inside.z == 123.0f);
}
