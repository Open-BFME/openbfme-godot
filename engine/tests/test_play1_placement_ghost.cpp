// OpenBFME unit tests. GPL-3.0.
// Lane PLAY-1: the building placement ghost's look (GameClient/PlacementGhost.h): the BUILD_PLACEMENT_CURSOR model state of a structure's draw module and
// the sub objects its draw script hides (RotWK RW 0x69C5E6 / 0x4B4379 / 0x4B4443). SKIPs without ROTWK_INSTALL / BFME2_INSTALL.

#include "doctest.h"
#include "HudTestUtil.h"

#include "Common/Thing/ThingTemplate.h"
#include "GameClient/PlacementGhost.h"
#include "GameLogic/Map/CastleTemplates.h"

#include <string>

using namespace hudtest;

namespace
{
bool has(const std::vector<std::string> &v, const std::string &x)
{
	for (const std::string &e : v)
	{
		if (e == x)
		{
			return true;
		}
	}
	return false;
}
} // namespace

TEST_CASE("play1 placement ghost: a structure's BUILD_PLACEMENT_CURSOR model and the sub objects its draw script hides")
{
	if (!haveWorld("play1 placement ghost"))
	{
		return;
	}
	SharedWorld &s = shared();
	struct Case
	{
		const char *name, *model, *hidden;
	};
	for (const Case &c : { Case{ "GondorBarracks", "GBBarracks_SKN", "V1FLAG" }, Case{ "GondorFarm", "GBFarm_SKN", "V2" }, Case{ "MordorOrcPit", "MBOrcpit_SKN", "N_Fire" },
						   Case{ "AngmarBarracks", "KBHall", "V1S" }, Case{ "ElvenBarracks", "NBElvnBarx_SKN", "N_Glow" } })
	{
		CAPTURE(std::string(c.name));
		const ThingTemplate *tt = s.world->things().findTemplate(c.name);
		REQUIRE(tt);
		const PlacementGhost::Look l = PlacementGhost::lookOf(*tt);
		CHECK(l.model == c.model);
		CHECK(has(l.hidden, "N_Window")); // the build-up and upgrade sub objects are hidden on the ghost
		CHECK(has(l.hidden, c.hidden));
		CHECK(l.unread.empty());
	}
}

// lane PLAY-3 (the owner: no placement preview with a builder): a fortress's own BUILD_PLACEMENT_CURSOR model is None; RotWK's placement update (RW 0x6A2AE5)
// draws the castle's layout for the local faction instead, one ghost per entry (PlacementGhost::castleLookOf). Every faction's fortress has a layout whose
// pieces have models; a structure that is no castle has none
TEST_CASE("play3 placement ghost: a fortress's ghost is its castle layout for the faction, each piece with its BUILD_PLACEMENT_CURSOR model")
{
	if (!haveWorld("play3 castle ghost"))
	{
		return;
	}
	SharedWorld &s = shared();
	CastleTemplateStore store;
	store.setLoader(CastleTemplateStore::fileSystemLoader(*s.mount->fs));
	auto find = [&s](const std::string &n) { return s.world->things().findTemplate(n); };
	struct Case
	{
		const char *fortress, *side;
	};
	for (const Case &c : { Case{ "MenFortress", "Men" }, Case{ "ElvenFortress", "Elves" }, Case{ "DwarvenFortress", "Dwarves" }, Case{ "IsengardFortress", "Isengard" },
						   Case{ "MordorFortress", "Mordor" }, Case{ "WildFortress", "Wild" }, Case{ "AngmarFortress", "Angmar" } })
	{
		CAPTURE(std::string(c.fortress));
		const ThingTemplate *tt = s.world->things().findTemplate(c.fortress);
		REQUIRE(tt);
		const PlacementGhost::CastleLook look = PlacementGhost::castleLookOf(*tt, c.side, store, find);
		CHECK(look.castle);
		CHECK(look.error.empty());
		CHECK_FALSE(look.base.empty());
		REQUIRE_FALSE(look.pieces.empty());
		int drawn = 0;
		for (const PlacementGhost::Piece &p : look.pieces)
		{
			drawn += p.look.model.empty() ? 0 : 1;
		}
		CHECK(drawn > 0);
		MESSAGE(std::string(c.fortress) << ": layout " << look.base << ", " << look.pieces.size() << " pieces, " << drawn << " with a model");
	}
	// MenFortress: its own look draws nothing (Model = None), the layout (Fortress_Men) does
	const PlacementGhost::Look own = PlacementGhost::lookOf(*s.world->things().findTemplate("MenFortress"));
	CHECK(own.model.empty());
	const PlacementGhost::CastleLook men = PlacementGhost::castleLookOf(*s.world->things().findTemplate("MenFortress"), "Men", store, find);
	CHECK(men.base == "Fortress_Men");
	// a side the castle names no base for: no pieces, the reason reported
	const PlacementGhost::CastleLook none = PlacementGhost::castleLookOf(*s.world->things().findTemplate("MenFortress"), "NoSuchSide", store, find);
	CHECK(none.castle);
	CHECK(none.pieces.empty());
	CHECK(none.error.find("names no base") != std::string::npos);
	// a structure that is no castle
	CHECK_FALSE(PlacementGhost::castleLookOf(*s.world->things().findTemplate("GondorBarracks"), "Men", store, find).castle);
}
