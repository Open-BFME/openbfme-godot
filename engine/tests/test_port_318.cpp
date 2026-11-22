// OpenBFME unit tests. GPL-3.0.
// PORT-318 (fork issue 754): the map INI block AerialPathfindNoFlyZone and the no-fly zone test of the aerial pathfinder.
//
// Sources of the expectations, all read with `rw.py dis` on RotWK 2.01 game.dat (md5 daed3668...):
//  * RW 0x60095C is the block's parser (the block table row RW 0xD9E60C, name "AerialPathfindNoFlyZone" at RW 0xBF8D54): "TriggerArea" (RW 0xBF8D48,
//    RW 0x60096B getNextSubToken RW 0x42DD73) looked up through TheTerrainLogic (RW 0xDE4690) vslot 0x9C (RW 0x6825F6, the list RW 0xDA21FC),
//    then "Height" (RW 0xBF3598) as a real (RW 0x42EAAD) read whether or not the trigger exists (RW 0x6009AC test, RW 0x6009C2 fstp st(0)), then
//    RW 0x777F76(trigger, height) on TheAerialPathfinder (RW 0xDE7958), which appends the pair to the list at + 0x10 (RW 0x777F8E add ecx, 0x10;
//    RW 0x777F5C push_back) and adds nothing for a null trigger (RW 0x777F7E).
//  * The parser reads exactly those two tokens (nothing between RW 0x60095C and its ret at RW 0x6009DC calls the line reader) and never an End, and
//    RW's top-level dispatch (RW 0x42BDAD, the block list at RW 0xDC51C0 compared at RW 0x42BDE6) knows no End block. The dispatcher hands the
//    handler the buffer of the header line itself, so the fields are written on that line: `AerialPathfindNoFlyZone TriggerArea = Z Height = 120`.
//    The tests below write it that way, and one of them pins that a field on a following line is refused (retail's token reader has no line of its
//    own to read either: getNextSubToken RW 0x42DD73 is strtok over the current buffer, RW 0x42DEED).
//  * RW 0x777E54 is the zone test: an empty list answers 0 (RW 0x777E64 loads RW 0xC1B594 = 0.0f); otherwise the maximum starts at -9999
//    (RW 0x777E6F loads RW 0xBDFFE0 = -9999.0f) and every zone whose polygon contains the truncated sample point (x: cvttss2si RW 0x777E84, y:
//    RW 0x777E89, back to floats RW 0x68628B, polygon walk RW 0x70CF29 at RW 0x6E4DC7) and whose height is not lower than the current maximum
//    (RW 0x777EBC comiss, RW 0x777EBF ja, RW 0x777EC1) replaces it.
//  * RW 0x777ED7 is the flight ground height: the terrain (TheTerrainLogic vslot 0x18) and the zone answer combined by "the higher of the two"
//    (RW 0x777F17 fld terrain, RW 0x777F1A fld zone, RW 0x777F1D fcompi, RW 0x777F21 jbe, RW 0x777F28 the zone wins), then raised to TheAI's AIData
//    MinFlightHeight (+ 0xB0) unless that is FLT_MAX (RW 0x777F2D .. 0x777F50, RW 0xBD1910).
//  * No retail map defines such a block (checked for the fork issue that asked for this port), so the block is exercised by synthetic data only. The
//    trigger list the parser searches is the map's TriggerAreas chunk, which LiveGame hands the ScriptEngine at newGame.

#include "doctest.h"
#include "IniTestUtil.h"

#include "Common/INI.h"
#include "Common/PlayerList.h"
#include "Common/PlayerTemplate.h"
#include "Common/RandomValue.h"
#include "Common/Team.h"
#include "Common/Thing/ModuleFactory.h"
#include "Common/Thing/RwFieldTables.h"
#include "Common/Thing/ThingFactory.h"
#include "GameClient/MapChunks.h"
#include "GameLogic/AI/AerialPathfinder.h"
#include "GameLogic/GameLogic.h"

#include <cfloat>
#include <string>
#include <vector>

namespace
{
// a map's polygon trigger, the shape RW 0x6825F6 finds by name and RW 0x70CF29 tests: the 100..300 square (no Z: a 2D prism)
TriggerArea squareTrigger(const std::string &name, float lo, float hi)
{
	TriggerArea area;
	area.name = name;
	area.points.push_back(Point2F{ lo, lo });
	area.points.push_back(Point2F{ hi, lo });
	area.points.push_back(Point2F{ hi, hi });
	area.points.push_back(Point2F{ lo, hi });
	return area;
}

// TheAerialPathfinder's list is process state (RW 0xDE7958): every test starts and ends from an empty list, so no case inherits a zone (and no
// pointer into a trigger list that has gone out of scope survives).
struct NoFlyScope
{
	NoFlyScope() { AerialPathfinder::resetNoFlyZones(); }
	~NoFlyScope() { AerialPathfinder::resetNoFlyZones(); }
};
} // namespace

TEST_CASE("PORT-318 AerialPathfindNoFlyZone: RW 0x60095C reads TriggerArea and Height and appends the pair to TheAerialPathfinder's list (RW 0x777F76)")
{
	NoFlyScope zones;
	const std::vector<TriggerArea> triggers = { squareTrigger("NoFlyZone1", 100.0f, 300.0f) };
	initest::Fixture fixture;
	fixture.env.blocks.registerBlock("AerialPathfindNoFlyZone",
		[&triggers](INI *ini) { AerialPathfinder::parseNoFlyZoneBlock(ini, &triggers); });

	// the block's own tokens, on the header line and without an End (see the file header: RW 0x60095C reads two tokens off the line the dispatcher
	// handed it, RW 0x42BDAD has no End row)
	const char *ini = "AerialPathfindNoFlyZone TriggerArea = NoFlyZone1 Height = 150.0\n";
	REQUIRE(initest::loadError(fixture.env, "map.ini", ini, INI_LOAD_CREATE_OVERRIDES).empty());
	REQUIRE(AerialPathfinder::noFlyZones().size() == 1);
	CHECK(AerialPathfinder::noFlyZones()[0].trigger == &triggers[0]); // RW 0x6009BB hands the trigger the lookup returned
	CHECK(AerialPathfinder::noFlyZones()[0].height == 150.0f);        // RW 0x42EAAD scanReal, exact for a binary32-representable value

	// RW 0x60095C appends what each block gives (RW 0x777F8E: the list at + 0x10, never a replace): the same zone at another height is a second pair
	const char *twice = "AerialPathfindNoFlyZone TriggerArea = NoFlyZone1 Height = 220.0\n"
						"AerialPathfindNoFlyZone TriggerArea = NoFlyZone1 Height = 90.0\n";
	REQUIRE(initest::loadError(fixture.env, "map.ini", twice, INI_LOAD_CREATE_OVERRIDES).empty());
	REQUIRE(AerialPathfinder::noFlyZones().size() == 3);
	CHECK(AerialPathfinder::noFlyZones()[1].height == 220.0f);
	CHECK(AerialPathfinder::noFlyZones()[2].height == 90.0f);
}

TEST_CASE("PORT-318 AerialPathfindNoFlyZone: a name the map's trigger list does not hold adds no pair but Height is still read (RW 0x6009AC / 0x6009C2)")
{
	NoFlyScope zones;
	const std::vector<TriggerArea> triggers = { squareTrigger("NoFlyZone1", 100.0f, 300.0f) };
	initest::Fixture fixture;
	fixture.env.blocks.registerBlock("AerialPathfindNoFlyZone",
		[&triggers](INI *ini) { AerialPathfinder::parseNoFlyZoneBlock(ini, &triggers); });

	// the missing trigger's block is followed by a block that must still be parsed: it is only reached when the first block consumed its Height
	// token (RW 0x60099E is called before RW 0x6009AC decides, and RW 0x6009C2 only drops the value)
	const char *ini = "AerialPathfindNoFlyZone TriggerArea = NoSuchZone Height = 400.0\n"
					  "AerialPathfindNoFlyZone TriggerArea = NoFlyZone1 Height = 120.0\n";
	REQUIRE(initest::loadError(fixture.env, "map.ini", ini, INI_LOAD_CREATE_OVERRIDES).empty());
	REQUIRE(AerialPathfinder::noFlyZones().size() == 1); // only the second block's pair: RW 0x777F76 is not called for a missing trigger
	CHECK(AerialPathfinder::noFlyZones()[0].trigger == &triggers[0]);
	CHECK(AerialPathfinder::noFlyZones()[0].height == 120.0f);

	// without the map's trigger list at all (a map.ini applied with no triggers) nothing is found either
	initest::Fixture bare;
	bare.env.blocks.registerBlock("AerialPathfindNoFlyZone", [](INI *ini) { AerialPathfinder::parseNoFlyZoneBlock(ini, nullptr); });
	REQUIRE(initest::loadError(bare.env, "map.ini", ini, INI_LOAD_CREATE_OVERRIDES).empty());
	CHECK(AerialPathfinder::noFlyZones().size() == 1);

	// the fields belong to the header line: a field on the next line is not read by the parser (RW 0x60095C never reads a line), so the token the
	// line reader has left is not the one the parser wants and the load fails naming the block; nothing reaches the list
	const char *wrapped = "AerialPathfindNoFlyZone\n"
						  "  TriggerArea = NoFlyZone1\n"
						  "  Height = 120.0\n";
	const std::string wrappedError = initest::loadError(fixture.env, "map.ini", wrapped, INI_LOAD_CREATE_OVERRIDES);
	CHECK(!wrappedError.empty());
	CHECK(wrappedError.find("AerialPathfindNoFlyZone") != std::string::npos);
	CHECK(AerialPathfinder::noFlyZones().size() == 1);
}

TEST_CASE("PORT-318 the no-fly zone test (RW 0x777E54): 0 without zones, else the highest zone containing the truncated sample, -9999 outside")
{
	NoFlyScope zones;
	CHECK(AerialPathfinder::noFlyZoneHeight(200.0f, 200.0f) == 0.0f); // RW 0x777E64, RW 0xC1B594

	const TriggerArea low = squareTrigger("Low", 100.0f, 300.0f);
	const TriggerArea high = squareTrigger("High", 100.0f, 300.0f);
	AerialPathfinder::addNoFlyZone(&low, 100.0f);
	AerialPathfinder::addNoFlyZone(&high, 150.0f);

	// inside both: the largest height wins (RW 0x777EBC .. 0x777EC1; the order of the pairs does not matter)
	CHECK(AerialPathfinder::noFlyZoneHeight(200.0f, 200.0f) == 150.0f);
	CHECK(AerialPathfinder::noFlyZoneHeight(299.0f, 101.0f) == 150.0f);
	// outside the polygon, with a non-empty list: -9999 (RW 0x777E6F, RW 0xBDFFE0), so the terrain alone decides in RW 0x777ED7
	CHECK(AerialPathfinder::noFlyZoneHeight(99.0f, 200.0f) == -9999.0f);
	CHECK(AerialPathfinder::noFlyZoneHeight(400.0f, 400.0f) == -9999.0f);

	// the sample is truncated to integers (RW 0x777E84 cvttss2si): 300.5 becomes 300, which the polygon holds (its right edge is inside the walk),
	// while 300.5 itself does not (RW 0x70CF29 skips an edge that starts to the right of the point); 99.5 becomes 99, outside the left edge
	CHECK(AerialPathfinder::noFlyZoneHeight(300.5f, 200.0f) == 150.0f);
	CHECK(AerialPathfinder::noFlyZoneHeight(99.5f, 200.0f) == -9999.0f);

	// the same truncation on a zone at the origin: 50.9 and 20.9 truncate into the square's right edge, which the walk counts as inside (RW 0x70CF29
	// toggles there, RW 0x777E84 is the truncation); 51.4 truncates to 51, outside the bounding box RW 0x70CB46; -0.9 truncates to 0, which the walk
	// leaves OUTSIDE (the left edge's crossing test at RW 0x70CD24 is x <= a.x both ways, so it does not toggle), so truncation alone does not put a
	// point inside a zone that starts to its right
	const TriggerArea origin = squareTrigger("Origin", 0.0f, 50.0f);
	AerialPathfinder::resetNoFlyZones();
	AerialPathfinder::addNoFlyZone(&origin, 40.0f);
	CHECK(AerialPathfinder::noFlyZoneHeight(50.9f, 20.9f) == 40.0f);
	CHECK(AerialPathfinder::noFlyZoneHeight(51.4f, 20.9f) == -9999.0f);
	CHECK(AerialPathfinder::noFlyZoneHeight(10.9f, 20.9f) == 40.0f);  // well inside, truncated or not
	CHECK(AerialPathfinder::noFlyZoneHeight(-0.9f, 20.0f) == -9999.0f); // truncated to 0: the left edge itself is not inside
}

TEST_CASE("PORT-318 the flight ground height takes the higher of the terrain and the zone (RW 0x777ED7), and MinFlightHeight only over both")
{
	NoFlyScope zones;
	// a game without a map: TheTerrainLogic answers 0 for every sample (GameLogic::getGroundHeight without a terrain), and the AIData floor is FLT_MAX
	// (RW 0x777F2D: the branch is taken only for a value other than FLT_MAX RW 0xBD1910)
	NameKeyGenerator keys;
	keys.init();
	RwGrammar grammar(RwBinaryData::embedded());
	ModuleFactory modules(keys, grammar);
	ThingFactory things(keys, modules, grammar);
	PlayerTemplateStore templates(keys);
	TeamFactory teams;
	PlayerList players(keys, templates, teams);
	GameLogic logic(things, modules, players, RandomAlgorithm::ZH_CarryChain);
	REQUIRE(logic.settings().minFlightHeight == FLT_MAX);

	CHECK(AerialPathfinder::flightGroundHeight(logic, 200.0f, 200.0f) == 0.0f); // RW 0x777E64's 0 against the 0 terrain

	const TriggerArea zone = squareTrigger("NoFlyZone1", 100.0f, 300.0f);
	AerialPathfinder::addNoFlyZone(&zone, 150.0f);
	CHECK(AerialPathfinder::flightGroundHeight(logic, 200.0f, 200.0f) == 150.0f); // RW 0x777F1D jbe fails: the zone is higher than the terrain
	CHECK(AerialPathfinder::flightGroundHeight(logic, 50.0f, 50.0f) == 0.0f);      // RW 0x777F1D jbe taken: the terrain (0) stays

	// the zone below the terrain does not lower it (RW 0x777F21 jbe: terrain >= zone keeps the terrain)
	AerialPathfinder::resetNoFlyZones();
	const TriggerArea low = squareTrigger("Low", 100.0f, 300.0f);
	AerialPathfinder::addNoFlyZone(&low, -50.0f);
	CHECK(AerialPathfinder::flightGroundHeight(logic, 200.0f, 200.0f) == 0.0f);

	// MinFlightHeight is the last word (RW 0x777F2D .. 0x777F50): it wins over a lower zone and is not held back by a higher one
	logic.settings().minFlightHeight = 500.0f;
	CHECK(AerialPathfinder::flightGroundHeight(logic, 200.0f, 200.0f) == 500.0f);
	AerialPathfinder::resetNoFlyZones();
	const TriggerArea tall = squareTrigger("Tall", 100.0f, 300.0f);
	AerialPathfinder::addNoFlyZone(&tall, 900.0f);
	CHECK(AerialPathfinder::flightGroundHeight(logic, 200.0f, 200.0f) == 900.0f);
}
