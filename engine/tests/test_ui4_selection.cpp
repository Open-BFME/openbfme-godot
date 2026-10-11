// OpenBFME retail tests of the selection marker (lane UI-4, GameClient/SelectionDecals.h): the GameData switches (GlobalData + 0x9A5 .. + 0x9A8), the level's
// SelectionDecal (RW 0x79DACD, size RW 0x7326CC), the local player's colour, a horde's members (RW 0x86F4EE) and no marker for another player's object
// (RW 0x4B2A9B). SKIP loudly without the installs. GPL-3.0.

#include "HudTestUtil.h"

#include "GameClient/CameraSettings.h"
#include "GameClient/SelectionDecals.h"
#include "GameLogic/Object/Contain/HordeContainCore.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"
#include "GameLogic/Object/Object.h"

#include <cmath>
#include <set>
#include <vector>

using namespace hudtest;

TEST_CASE("ui4 selection: GameData's selection marker switches parse (ShowSelectedUnitMarker .. OpacityOfSimpleMergeDecals)")
{
	const char *text = R"(GameData
  DefaultCameraMinHeight = 120.0
  DefaultCameraMaxHeight = 300.0
  DefaultCameraPitchAngle = 37.5
  DefaultCameraYawAngle = 0.0
  DefaultCameraScrollSpeedScalar = 1.0
  CameraLockHeightDelta = 150.0
  CameraTerrainSampleRadiusForHeight = 1.0
  UseCameraInReplay = No
  CameraAdjustSpeed = 0.3
  ScrollAmountCutoff = 50.0
  EnforceMaxCameraHeight = No
  HorizontalScrollSpeedFactor = 0.4
  VerticalScrollSpeedFactor = 0.5
  ScreenEdgeScrollSpeedFactor = 1.0
  ScreenEdgeScrollRampTime = 0.25
  KeyboardScrollSpeedFactor = 1.0
  KeyboardCameraRotateSpeed = 0.1
  PartitionCellSize = 40.0
  ShowSelectedUnitMarker = Yes
  UseSimpleHordeDecals = No
  UseSimpleMergeDecals = Yes
  OpacityOfSimpleMergeDecals = 35%
End
)";
	CameraSettings s;
	std::string err;
	REQUIRE_MESSAGE(CameraSettings::scan(text, s, &err), err);
	CHECK(s.showSelectedUnitMarker);
	CHECK_FALSE(s.useSimpleHordeDecals);
	CHECK(s.useSimpleMergeDecals);
	CHECK(s.opacityOfSimpleMergeDecals == doctest::Approx(0.35f));
}

TEST_CASE("ui4 selection retail: a selected hero, unit and horde of the local player get their level's decal in the player's colour; another player's none")
{
	if (!haveWorld("ui4 selection"))
	{
		return;
	}
	Rig rig(shared());
	const Coord3D c = rig.freeSpot(2800, 1400, 300.0f);
	Object *hero = rig.make("GondorBoromir", c.x, c.y);
	Object *porter = rig.make("MenPorter", c.x + 150.0f, c.y);
	Object *horde = rig.make("GondorFighterHorde", c.x - 200.0f, c.y);
	Object *enemy = rig.make("GondorFaramir", c.x, c.y + 250.0f, rig.game->players().findPlayerWithName("Player_2"));
	rig.frame(30); // the horde spawns its members
	const HordeContain *hc = dynamic_cast<const HordeContain *>(horde->getContain());
	REQUIRE(hc != nullptr);
	std::set<ObjectID> members;
	for (const Object *m : *hc->getContainedItemsList())
	{
		members.insert(m->getID());
	}
	for (auto id : hc->core().registeredMembers())
	{
		members.insert((ObjectID)id);
	}
	REQUIRE(members.size() > 1);

	SelectionDecalSettings s;
	s.showSelectedUnitMarker = true;
	s.useSimpleMergeDecals = true;
	s.opacityOfSimpleMergeDecals = 0.35f;
	const std::uint32_t rgb = rig.local->getPlayerColor() & 0xFFFFFFu;
	auto build = [&](const std::vector<ObjectID> &sel, const SelectionDecalSettings &with) {
		return BuildSelectionDecals(rig.logic(), &rig.game->drawables(), rig.local, sel, with, 7);
	};

	// the hero: one decal, its level's texture at MinRadius (count 1)
	std::vector<SelectionDecal> d = build({ hero->getID() }, s);
	REQUIRE(d.size() == 1);
	CHECK(d[0].object == hero->getID());
	CHECK(d[0].texture == "decal_hero_good");
	CHECK(d[0].size == 40.0f);
	CHECK((d[0].color & 0xFFFFFFu) == rgb);
	CHECK((d[0].color >> 24) > 0);
	
	// a unit: its own level's decal
	d = build({ porter->getID() }, s);
	REQUIRE(d.size() == 1);
	CHECK(d[0].texture == "decal_G_level4");
	CHECK(d[0].size == 50.0f);

	// a horde (UseSimpleHordeDecals No): one decal of count 1 on every member, none on the horde, none twice
	d = build({ horde->getID() }, s);
	CHECK(d.size() == members.size());
	std::set<ObjectID> got;
	for (const SelectionDecal &x : d)
	{
		CHECK(members.count(x.object) == 1);
		got.insert(x.object);
	}
	CHECK(got.size() == d.size());
	// the members' decals are SHADOW_MERGE_DECAL with the cut-out decal_good_CO (GoodLevel1 .. 5), at the throb's opacity whatever the merge mode
	REQUIRE(!d.empty());
	CHECK(d[0].style == 0x1000u);
	CHECK(d[0].texture2 == "decal_good_CO");
	{
		SelectionDecalSettings full = s;
		full.useSimpleMergeDecals = false;
		const std::vector<SelectionDecal> f = build({ horde->getID() }, full);
		REQUIRE(!f.empty());
		CHECK(d[0].color == f[0].color);
	}

	// UseSimpleHordeDecals Yes: one decal on the horde, sized for its member count
	SelectionDecalSettings simple = s;
	simple.useSimpleHordeDecals = true;
	d = build({ horde->getID() }, simple);
	CHECK(d.size() <= 1);

	// another player's object and ShowSelectedUnitMarker No: no marker
	CHECK(build({ enemy->getID() }, s).empty());
	SelectionDecalSettings off = s;
	off.showSelectedUnitMarker = false;
	CHECK(build({ hero->getID(), porter->getID() }, off).empty());
	// the in-game UI hidden: the decal is kept at opacity 0
	SelectionDecalSettings hidden = s;
	hidden.drawIconUI = false;
	d = build({ hero->getID() }, hidden);
	REQUIRE(d.size() == 1);
	CHECK((d[0].color >> 24) == 0);
}

TEST_CASE("ui4 selection: the static LOD's DecalLOD sets ShowSelectedUnitMarker and UseSimpleMergeDecals (RW 0x601C62)")
{
	SelectionDecalSettings s;
	s.showSelectedUnitMarker = true;
	s.useSimpleMergeDecals = true;
	ApplyDecalLOD(s, 0); // Off
	CHECK_FALSE(s.showSelectedUnitMarker);
	CHECK(s.useSimpleMergeDecals);
	ApplyDecalLOD(s, 1); // Low
	CHECK(s.showSelectedUnitMarker);
	CHECK(s.useSimpleMergeDecals);
	ApplyDecalLOD(s, 2); // High
	CHECK(s.showSelectedUnitMarker);
	CHECK_FALSE(s.useSimpleMergeDecals);
}

namespace
{
// a white disc of radius r (in texels of a 64 x 64 texture), alpha 255 inside
std::vector<std::uint8_t> disc(float r)
{
	std::vector<std::uint8_t> px(64 * 64 * 4, 0);
	for (int y = 0; y < 64; ++y)
	{
		for (int x = 0; x < 64; ++x)
		{
			const float dx = (float)x + 0.5f - 32.0f, dy = (float)y + 0.5f - 32.0f;
			std::uint8_t *p = &px[((size_t)y * 64 + (size_t)x) * 4];
			p[0] = p[1] = p[2] = 255;
			p[3] = std::sqrt(dx * dx + dy * dy) <= r ? 255 : 0;
		}
	}
	return px;
}

SelectionDecal mergeDecal(float x, float y)
{
	SelectionDecal d;
	d.position = { x, y, 0.0f };
	d.size = 64.0f;
	d.texture = "rim";
	d.texture2 = "cut";
	d.style = 0x1000u;
	d.color = 0xCC3050E0u; // opacity 0xCC, blue
	return d;
}

const std::uint8_t *at(const MergedSelectionDecals &m, float x, float y)
{
	const int px = (int)((x - m.minX) / (m.maxX - m.minX) * (float)m.width);
	const int py = (int)((m.maxY - y) / (m.maxY - m.minY) * (float)m.height);
	return &m.rgba[((size_t)py * (size_t)m.width + (size_t)px) * 4];
}
} // namespace

TEST_CASE("ui4 selection: the merge decals compose like RotWK's two stencil passes: one outline around all the units, none inside")
{
	const std::vector<std::uint8_t> rim = disc(30.0f), cut = disc(26.0f);
	auto image = [&](const std::string &name) -> DecalImage {
		if (name == "rim")
		{
			return { 64, 64, rim.data() };
		}
		if (name == "cut")
		{
			return { 64, 64, cut.data() };
		}
		return {};
	};
	// two units 40 apart: their discs (radius 30) overlap, their cut-outs (radius 26) too
	std::vector<SelectionDecal> list{ mergeDecal(0.0f, 0.0f), mergeDecal(40.0f, 0.0f) };
	SelectionDecal hero = mergeDecal(500.0f, 500.0f);
	hero.style = 0x20u; // SHADOW_ALPHA_DECAL: not merged
	list.push_back(hero);
	SelectionDecalSettings full;
	full.showSelectedUnitMarker = true;
	full.useSimpleMergeDecals = false;
	MergedSelectionDecals m;
	std::string err;
	REQUIRE_MESSAGE(ComposeMergeDecals(list, image, full, m, &err), err);
	CHECK(m.decals == 2);
	CHECK(m.minX == doctest::Approx(-32.0f));
	CHECK(m.maxX == doctest::Approx(72.0f));
	CHECK(m.width == 104); // the textures' own density: 1 texel a unit
	CHECK(m.height == 64);
	// the band between the cut-out and the disc is drawn, opaque, in the colour
	const std::uint8_t *left = at(m, -28.0f, 0.0f);
	CHECK(left[3] == 255);
	CHECK(left[0] == 0x30);
	CHECK(left[1] == 0x50);
	CHECK(left[2] == 0xE0);
	CHECK(at(m, 68.0f, 0.0f)[3] == 255);
	CHECK(at(m, 20.0f, 20.0f)[3] == 255); // the upper edge between the two (the cut-outs end at 16.6, the discs at 22.4)
	// inside either cut-out nothing: the first disc's rim that lies inside the second unit's cut-out is gone (merged)
	CHECK(at(m, 0.0f, 0.0f)[3] == 0);
	CHECK(at(m, 13.0f, 0.0f)[3] == 0);  // unit 1's own band to the right, under unit 2's cut-out
	CHECK(at(m, 27.0f, 0.0f)[3] == 0);  // unit 2's band to the left, under unit 1's cut-out
	CHECK(at(m, -31.5f, 31.5f)[3] == 0); // outside every disc

	// UseSimpleMergeDecals: one blended pass of the discs, each pixel once, kept where the texel's alpha * opacity >= OpacityOfSimpleMergeDecals
	SelectionDecalSettings simple = full;
	simple.useSimpleMergeDecals = true;
	simple.opacityOfSimpleMergeDecals = 0.35f;
	REQUIRE(ComposeMergeDecals(list, image, simple, m, &err));
	CHECK(at(m, 0.0f, 0.0f)[3] == 0xCC);
	CHECK(at(m, 20.0f, 0.0f)[3] == 0xCC); // the overlap is not drawn twice
	simple.opacityOfSimpleMergeDecals = 0.9f; // 229 > 0xCC: the alpha test drops it all
	REQUIRE(ComposeMergeDecals(list, image, simple, m, &err));
	CHECK(at(m, 0.0f, 0.0f)[3] == 0);

	// a missing cut-out is an error, not a plain disc
	list[0].texture2 = "nope";
	CHECK_FALSE(ComposeMergeDecals(list, image, full, m, &err));
	CHECK(err.find("nope") != std::string::npos);
}
