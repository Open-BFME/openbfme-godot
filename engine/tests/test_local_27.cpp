// OpenBFME unit tests: LOCAL-27 bridges as retail: the Bridge block (models by damage, towers, scaffold, radar colour) (issue #189; editions: both).
//
// Target facts and RotWK 2.01 / BFME2 1.06 addresses:
//   - INI parseTerrainBridgeDefinition: RW 0x005DA647 / BFME2 0x002778CB (called from BlockParse table RW 0x00D9DC34)
//   - TerrainRoadType Bridge field table: RW 0x00C19DC0 (22 entries, identical to BFME2 0x00803A28):
//       * BridgeScale (+0x1C, RW 0x0042ED00 / BFME2 0x0002F05D)
//       * ScaffoldObjectName (+0x20, RW 0x0042EE5E, getter RW 0x008DA9C6 / BFME2 0x002D9BC1)
//       * ScaffoldSupportObjectName (+0x24, RW 0x0042EE5E, getter RW 0x0096589F / BFME2 0x0056394A)
//       * RadarColor (+0x28, RW 0x0042EF99 / BFME2 0x0002F259, reader RW 0x0044F790 / BFME2 0x000DF612)
//       * TransitionEffectsHeight (+0x144, RW 0x0042ED00)
//       * NumFXPerType (+0x148, RW 0x0042EC5E)
//       * BridgeModelName (+0x34, RW 0x0042EE5E), Texture (+0x38, RW 0x0042EE5E)
//       * BridgeModelNameDamaged (+0x3C, RW 0x0042EE5E), TextureDamaged (+0x40, RW 0x0042EE5E)
//       * BridgeModelNameReallyDamaged (+0x44, RW 0x0042EE5E), TextureReallyDamaged (+0x48, RW 0x0042EE5E)
//       * BridgeModelNameBroken (+0x4C, RW 0x0042EE5E), TextureBroken (+0x50, RW 0x0042EE5E)
//       * TowerObjectNameFromLeft (+0x54, RW 0x0042EE5E)
//       * TowerObjectNameFromRight (+0x58, RW 0x0042EE5E)
//       * TowerObjectNameToLeft (+0x5C, RW 0x0042EE5E)
//       * TowerObjectNameToRight (+0x60, RW 0x0042EE5E)
//       * DamagedToSound (+0x68, RW 0x0073B217), RepairedToSound (+0xD8, RW 0x0073B217)
//   - W3DBridge::load: RW 0x004DDEBD / BFME2 0x000DE648 (switches model & texture across 4 damage states 0..3; 0x4DDF69 was int3)
//   - TerrainRoadType::getTowerObjectName: RW 0x006813E2 / BFME2 0x00280BE6 (indexes +0x54 by BridgeTowerType 0..3)
//   - Bridge::Bridge (object ctor): RW 0x0068141D / BFME2 0x003290AE (creates 4 towers at bounding rect corners; 0x681420 was +3 misaligned)
//   - BridgeBehavior::createScaffolding: RW 0x0085A7EF / BFME2 0x00457737 (creates scaffold and scaffold support objects)
//   - W3DRadarTerrainTexture::addBridge / W3DRadar::buildTerrainTexture: RW 0x0044F781 (reads RadarColor at +0x28)
//   - W3DBridgeBuffer::loadBridges: RW 0x004DEFEB / BFME2 0x000DFB3C (pairs FLAG_BRIDGE_POINT1 and FLAG_BRIDGE_POINT2)
//   - W3DBridgeBuffer::addBridge: RW 0x004DEE87 / BFME2 0x000DF612 (calls init, load, getBridgeInfo)
//   - W3DBridge::getBridgeInfo: RW 0x004DD43F / BFME2 0x000DDB94 (bridgeWidth = (m_maxY - m_minY) * m_scale)

#include "doctest.h"
#include "GameClient/TerrainRoads.h"
#include "Common/MapObject.h"
#include "GameEngineDevice/W3DDevice/GameClient/WorldHeightMap.h"

#include <cmath>

namespace
{

WorldHeightMap flatMap(int w, int h, int border, int raw)
{
	WorldHeightMap m;
	m.m_width = w;
	m.m_height = h;
	m.m_borderSize = border;
	m.m_dataSize = w * h;
	m.m_data.assign((size_t)w * h, (std::uint16_t)raw);
	return m;
}

const char *kEnnorathBridgesIni =
	"Bridge TEST_EuropeanBridgeWide\n"
	"  RoadWidth                      = 34.0\n"
	"  BridgeScale                    = 0.6\n"
	"  RadarColor                     = R:192 G:192 B:192\n"
	"  BridgeModelName                = CBWBrdgeArc\n"
	"  Texture                        = CBWBrdgeArc.tga\n"
	"  BridgeModelNameDamaged         = CBWBrdgeArc_D\n"
	"  TextureDamaged                 = CBWBrdgeArc_D.tga\n"
	"  BridgeModelNameReallyDamaged   = CBWBrdgeArc_RD\n"
	"  TextureReallyDamaged           = CBWBrdgeArc_RD.tga\n"
	"  BridgeModelNameBroken          = CBWBrdgeArc_B\n"
	"  TextureBroken                  = CBWBrdgeArc_B.tga\n"
	"  TowerObjectNameFromLeft        = EuropeanTowerBridge01\n"
	"  TowerObjectNameFromRight       = EuropeanTowerBridge02\n"
	"  TowerObjectNameToLeft          = EuropeanTowerBridge03\n"
	"  TowerObjectNameToRight         = EuropeanTowerBridge04\n"
	"  ScaffoldObjectName             = BridgeScaffold01\n"
	"  ScaffoldSupportObjectName      = BridgeScaffoldSupport01\n"
	"  TransitionEffectsHeight        = 15.0\n"
	"  NumFXPerType                   = 3\n"
	"  DamagedToSound                 = BridgeDamageSound\n"
	"  RepairedToSound                = BridgeRepairSound\n"
	"End\n"
	"\n"
	"Bridge TEST_NewBridgeWide\n"
	"  RoadWidth                      = 40.0\n"
	"  BridgeScale                    = 0.75\n"
	"  RadarColor                     = R:128 G:64 B:32\n"
	"  BridgeModelName                = CBBrdge1\n"
	"  Texture                        = OSBridge2.tga\n"
	"  ScaffoldObjectName             = BridgeScaffold01\n"
	"  ScaffoldSupportObjectName      = BridgeScaffoldSupport01\n"
	"End\n"
	"\n"
	"Road DirtRoad\n"
	"  Texture = TRDirtRoad.tga\n"
	"  RoadWidth = 50.0\n"
	"  RoadWidthInTexture = 0.95\n"
	"End\n";

} // namespace

TEST_CASE("LOCAL-27 bridge roads.ini: scanText parses TEST_EuropeanBridgeWide and TEST_NewBridgeWide")
{
	RoadTypeIndex index;
	TerrainRoads::scanText(kEnnorathBridgesIni, index);

	const RoadType *euro = index.find("TEST_EuropeanBridgeWide");
	REQUIRE(euro != nullptr);
	CHECK(euro->isBridge == true);
	CHECK(euro->bridgeScale == 0.6f);
	CHECK(euro->radarColor[0] == doctest::Approx(192.0f / 255.0f)); // tolerance: float division
	CHECK(euro->radarColor[1] == doctest::Approx(192.0f / 255.0f)); // tolerance: float division
	CHECK(euro->radarColor[2] == doctest::Approx(192.0f / 255.0f)); // tolerance: float division
	CHECK(euro->bridgeModelName == "CBWBrdgeArc");
	CHECK(euro->texture == "CBWBrdgeArc.tga");
	CHECK(euro->bridgeModelNameDamaged == "CBWBrdgeArc_D");
	CHECK(euro->textureDamaged == "CBWBrdgeArc_D.tga");
	CHECK(euro->bridgeModelNameReallyDamaged == "CBWBrdgeArc_RD");
	CHECK(euro->textureReallyDamaged == "CBWBrdgeArc_RD.tga");
	CHECK(euro->bridgeModelNameBroken == "CBWBrdgeArc_B");
	CHECK(euro->textureBroken == "CBWBrdgeArc_B.tga");
	CHECK(euro->towerObjectNameFromLeft == "EuropeanTowerBridge01");
	CHECK(euro->towerObjectNameFromRight == "EuropeanTowerBridge02");
	CHECK(euro->towerObjectNameToLeft == "EuropeanTowerBridge03");
	CHECK(euro->towerObjectNameToRight == "EuropeanTowerBridge04");
	CHECK(euro->scaffoldObjectName == "BridgeScaffold01");
	CHECK(euro->scaffoldSupportObjectName == "BridgeScaffoldSupport01");
	CHECK(euro->transitionEffectsHeight == 15.0f);
	CHECK(euro->numFXPerType == 3);
	CHECK(euro->damagedToSound == "BridgeDamageSound");
	CHECK(euro->repairedToSound == "BridgeRepairSound");

	const RoadType *newBridge = index.find("TEST_NewBridgeWide");
	REQUIRE(newBridge != nullptr);
	CHECK(newBridge->isBridge == true);
	CHECK(newBridge->bridgeScale == 0.75f);
	CHECK(newBridge->radarColor[0] == doctest::Approx(128.0f / 255.0f)); // tolerance: float division
	CHECK(newBridge->radarColor[1] == doctest::Approx(64.0f / 255.0f));  // tolerance: float division
	CHECK(newBridge->radarColor[2] == doctest::Approx(32.0f / 255.0f));  // tolerance: float division
	CHECK(newBridge->bridgeModelName == "CBBrdge1");
	CHECK(newBridge->texture == "OSBridge2.tga");

	const RoadType *road = index.find("DirtRoad");
	REQUIRE(road != nullptr);
	CHECK(road->isBridge == false);
	CHECK(road->roadWidth == 50.0f);
}

TEST_CASE("LOCAL-27 bridge damage states: modelForDamage and textureForDamage switch by BodyDamageType (RW 0x004DDF7C)")
{
	RoadTypeIndex index;
	TerrainRoads::scanText(kEnnorathBridgesIni, index);

	const RoadType *euro = index.find("TEST_EuropeanBridgeWide");
	REQUIRE(euro != nullptr);

	// State 0 (BODY_PRISTINE): pristine model & texture
	CHECK(euro->modelForDamage(0) == "CBWBrdgeArc");
	CHECK(euro->textureForDamage(0) == "CBWBrdgeArc.tga");

	// State 1 (BODY_DAMAGED): damaged model & texture
	CHECK(euro->modelForDamage(1) == "CBWBrdgeArc_D");
	CHECK(euro->textureForDamage(1) == "CBWBrdgeArc_D.tga");

	// State 2 (BODY_REALLYDAMAGED): really damaged model & texture
	CHECK(euro->modelForDamage(2) == "CBWBrdgeArc_RD");
	CHECK(euro->textureForDamage(2) == "CBWBrdgeArc_RD.tga");

	// State 3 (BODY_RUBBLE): broken model & texture
	CHECK(euro->modelForDamage(3) == "CBWBrdgeArc_B");
	CHECK(euro->textureForDamage(3) == "CBWBrdgeArc_B.tga");

	// Fallback when damaged/broken states are not defined (TEST_NewBridgeWide)
	const RoadType *newBridge = index.find("TEST_NewBridgeWide");
	REQUIRE(newBridge != nullptr);
	CHECK(newBridge->modelForDamage(0) == "CBBrdge1");
	CHECK(newBridge->modelForDamage(1) == "CBBrdge1");
	CHECK(newBridge->modelForDamage(2) == "CBBrdge1");
	CHECK(newBridge->modelForDamage(3) == "CBBrdge1");
	CHECK(newBridge->textureForDamage(0) == "OSBridge2.tga");
	CHECK(newBridge->textureForDamage(1) == "OSBridge2.tga");
	CHECK(newBridge->textureForDamage(2) == "OSBridge2.tga");
	CHECK(newBridge->textureForDamage(3) == "OSBridge2.tga");
}

TEST_CASE("LOCAL-27 bridge towers: towerObjectName queries by BridgeTowerType (RW 0x006813E2)")
{
	RoadTypeIndex index;
	TerrainRoads::scanText(kEnnorathBridgesIni, index);

	const RoadType *euro = index.find("TEST_EuropeanBridgeWide");
	REQUIRE(euro != nullptr);

	CHECK(euro->towerObjectName(BRIDGE_TOWER_FROM_LEFT) == "EuropeanTowerBridge01");
	CHECK(euro->towerObjectName(BRIDGE_TOWER_FROM_RIGHT) == "EuropeanTowerBridge02");
	CHECK(euro->towerObjectName(BRIDGE_TOWER_TO_LEFT) == "EuropeanTowerBridge03");
	CHECK(euro->towerObjectName(BRIDGE_TOWER_TO_RIGHT) == "EuropeanTowerBridge04");
}

TEST_CASE("LOCAL-27 bridge radar color: parseRGBColor normalizes 0..255 to [0, 1] (RW 0x0042EF99)")
{
	float rgb[3] = { 0 };

	CHECK(TerrainRoads::parseRGBColor("R:192 G:192 B:192", rgb));
	CHECK(rgb[0] == doctest::Approx(192.0f / 255.0f)); // tolerance: float division
	CHECK(rgb[1] == doctest::Approx(192.0f / 255.0f)); // tolerance: float division
	CHECK(rgb[2] == doctest::Approx(192.0f / 255.0f)); // tolerance: float division

	CHECK(TerrainRoads::parseRGBColor("R:255 G:0 B:128", rgb));
	CHECK(rgb[0] == 1.0f);
	CHECK(rgb[1] == 0.0f);
	CHECK(rgb[2] == doctest::Approx(128.0f / 255.0f)); // tolerance: float division

	CHECK(TerrainRoads::parseRGBColor("50 100 150", rgb));
	CHECK(rgb[0] == doctest::Approx(50.0f / 255.0f));  // tolerance: float division
	CHECK(rgb[1] == doctest::Approx(100.0f / 255.0f)); // tolerance: float division
	CHECK(rgb[2] == doctest::Approx(150.0f / 255.0f)); // tolerance: float division
}

TEST_CASE("LOCAL-27 bridge build: buildBridges creates BridgeSegment from FLAG_BRIDGE_POINT1/2 pair with height, center, angle, 4 tower corners, and scaffolding")
{
	RoadTypeIndex index;
	TerrainRoads::scanText(kEnnorathBridgesIni, index);

	WorldHeightMap heightMap;
	// Empty height map (width=0) defaults to no height offset
	std::vector<MapObject> objects;

	MapObject pt1;
	pt1.m_objectName = "TEST_EuropeanBridgeWide";
	pt1.m_location = { 100.0f, 200.0f, 10.0f };
	pt1.m_flags = FLAG_BRIDGE_POINT1;
	objects.push_back(pt1);

	MapObject pt2;
	pt2.m_objectName = "TEST_EuropeanBridgeWide";
	pt2.m_location = { 300.0f, 200.0f, 10.0f };
	pt2.m_flags = FLAG_BRIDGE_POINT2;
	objects.push_back(pt2);

	std::vector<BridgeSegment> bridges;
	RoadBuildReport report;
	TerrainRoads::buildBridges(objects, index, heightMap, bridges, report);

	CHECK(report.bridgePairs == 1);
	CHECK(report.orphanPoints == 0);
	CHECK(report.unknownBridgeTypes == 0);
	REQUIRE(bridges.size() == 1);

	const BridgeSegment &b = bridges[0];
	CHECK(b.bridgeName == "TEST_EuropeanBridgeWide");
	CHECK(b.scale == 0.6f);
	CHECK(b.center.x == 200.0f);
	CHECK(b.center.y == 200.0f);
	CHECK(b.center.z == 10.0f);
	CHECK(b.angle == 0.0f); // points along +X axis

	// Bridge points along +X, so normal is in +Y direction
	// Width = 34.0f * 0.6f = 20.4f, halfWidth = 10.2f
	const float halfWidth = 34.0f * 0.6f * 0.5f;
	CHECK(b.fromLeft.x == 100.0f);
	CHECK(b.fromLeft.y == doctest::Approx(200.0f + halfWidth));  // tolerance: float geometry calculation
	CHECK(b.fromRight.x == 100.0f);
	CHECK(b.fromRight.y == doctest::Approx(200.0f - halfWidth)); // tolerance: float geometry calculation
	CHECK(b.toLeft.x == 300.0f);
	CHECK(b.toLeft.y == doctest::Approx(200.0f + halfWidth));    // tolerance: float geometry calculation
	CHECK(b.toRight.x == 300.0f);
	CHECK(b.toRight.y == doctest::Approx(200.0f - halfWidth));   // tolerance: float geometry calculation

	// Towers at 4 corners
	CHECK(b.towerPositions[BRIDGE_TOWER_FROM_LEFT].y == doctest::Approx(200.0f + halfWidth));  // tolerance: float geometry calculation
	CHECK(b.towerPositions[BRIDGE_TOWER_FROM_RIGHT].y == doctest::Approx(200.0f - halfWidth)); // tolerance: float geometry calculation
	CHECK(b.towerTemplates[BRIDGE_TOWER_FROM_LEFT] == "EuropeanTowerBridge01");
	CHECK(b.towerTemplates[BRIDGE_TOWER_FROM_RIGHT] == "EuropeanTowerBridge02");
	CHECK(b.towerTemplates[BRIDGE_TOWER_TO_LEFT] == "EuropeanTowerBridge03");
	CHECK(b.towerTemplates[BRIDGE_TOWER_TO_RIGHT] == "EuropeanTowerBridge04");

	// Scaffolding
	CHECK(b.scaffoldTemplate == "BridgeScaffold01");
	CHECK(b.scaffoldSupportTemplate == "BridgeScaffoldSupport01");

	// Damage models & textures delegating through roadType
	CHECK(b.modelForDamage(0) == "CBWBrdgeArc");
	CHECK(b.modelForDamage(1) == "CBWBrdgeArc_D");
	CHECK(b.textureForDamage(0) == "CBWBrdgeArc.tga");
	CHECK(b.textureForDamage(1) == "CBWBrdgeArc_D.tga");

	// Radar color
	CHECK(b.radarColor[0] == doctest::Approx(192.0f / 255.0f)); // tolerance: float division
	CHECK(b.radarColor[1] == doctest::Approx(192.0f / 255.0f)); // tolerance: float division
	CHECK(b.radarColor[2] == doctest::Approx(192.0f / 255.0f)); // tolerance: float division
}

TEST_CASE("LOCAL-27 bridge orphan and unknown handling: unmatched points and missing types counted in report")
{
	RoadTypeIndex index;
	TerrainRoads::scanText(kEnnorathBridgesIni, index);

	WorldHeightMap heightMap = flatMap(10, 10, 0, 0);
	std::vector<MapObject> objects;

	// Orphan POINT1 (followed by non-POINT2 object)
	MapObject o1;
	o1.m_objectName = "TEST_EuropeanBridgeWide";
	o1.m_flags = FLAG_BRIDGE_POINT1;
	objects.push_back(o1);

	MapObject dummy1;
	dummy1.m_objectName = "Tree";
	dummy1.m_flags = 0;
	objects.push_back(dummy1);

	// Orphan POINT2 (preceded by non-POINT1 object)
	MapObject o2;
	o2.m_objectName = "TEST_EuropeanBridgeWide";
	o2.m_flags = FLAG_BRIDGE_POINT2;
	objects.push_back(o2);

	// Pair with unknown bridge type
	MapObject o3;
	o3.m_objectName = "UnknownBridge";
	o3.m_flags = FLAG_BRIDGE_POINT1;
	objects.push_back(o3);

	MapObject o4;
	o4.m_objectName = "UnknownBridge";
	o4.m_flags = FLAG_BRIDGE_POINT2;
	objects.push_back(o4);

	std::vector<BridgeSegment> bridges;
	RoadBuildReport report;
	TerrainRoads::buildBridges(objects, index, heightMap, bridges, report);

	CHECK(report.bridgePairs == 1);
	CHECK(report.orphanPoints == 2);
	CHECK(report.unknownBridgeTypes == 1);
	CHECK(bridges.empty());
}

TEST_CASE("LOCAL-27 bridge width: missing roadWidth reports invalidBridgeWidth without silent 34.0 fallback (RW 0x004DD43F, RW 0x004DDEBD)")
{
	RoadTypeIndex index;
	TerrainRoads::scanText(
		"Bridge TEST_NoWidthBridge\n"
		"  BridgeScale = 1.0\n"
		"  BridgeModelName = TestBridge\n"
		"End\n",
		index);

	WorldHeightMap heightMap = flatMap(10, 10, 0, 0);
	std::vector<MapObject> objects;
	MapObject pt1;
	pt1.m_objectName = "TEST_NoWidthBridge";
	pt1.m_location = { 0.0f, 0.0f, 0.0f };
	pt1.m_flags = FLAG_BRIDGE_POINT1;
	objects.push_back(pt1);

	MapObject pt2;
	pt2.m_objectName = "TEST_NoWidthBridge";
	pt2.m_location = { 100.0f, 0.0f, 0.0f };
	pt2.m_flags = FLAG_BRIDGE_POINT2;
	objects.push_back(pt2);

	std::vector<BridgeSegment> bridges;
	RoadBuildReport report;
	TerrainRoads::buildBridges(objects, index, heightMap, bridges, report);

	CHECK(report.bridgePairs == 1);
	CHECK(report.invalidBridgeWidth == 1);
	CHECK(bridges.empty());
}

TEST_CASE("LOCAL-27 roads parity: roads continue building straight and curved strips unchanged")
{
	RoadTypeIndex index;
	TerrainRoads::scanText(kEnnorathBridgesIni, index);

	WorldHeightMap heightMap = flatMap(20, 20, 0, 0);
	std::vector<MapObject> objects;

	MapObject r1;
	r1.m_objectName = "DirtRoad";
	r1.m_location = { 10.0f, 10.0f, 0.0f };
	r1.m_flags = FLAG_ROAD_POINT1;
	objects.push_back(r1);

	MapObject r2;
	r2.m_objectName = "DirtRoad";
	r2.m_location = { 100.0f, 10.0f, 0.0f };
	r2.m_flags = FLAG_ROAD_POINT2;
	objects.push_back(r2);

	std::vector<RoadStrip> strips;
	RoadBuildReport report;
	TerrainRoads::buildStrips(objects, index, heightMap, 0.05f, strips, report);

	CHECK(report.pairs == 1);
	CHECK(report.unknownRoadTypes == 0);
	CHECK(report.strips == 1);
	REQUIRE(!strips.empty());
	CHECK(strips[0].roadName == "DirtRoad");
	CHECK(strips[0].texture == "TRDirtRoad.tga");
	CHECK(!strips[0].position.empty());
}

TEST_CASE("LOCAL-27 pristine bridge model at load: buildBridges segment delivers pristine model and texture (RW 0x004DDEBD)")
{
	RoadTypeIndex index;
	TerrainRoads::scanText(kEnnorathBridgesIni, index);

	WorldHeightMap heightMap = flatMap(10, 10, 0, 0);
	std::vector<MapObject> objects;

	MapObject pt1;
	pt1.m_objectName = "TEST_NewBridgeWide";
	pt1.m_location = { 50.0f, 50.0f, 0.0f };
	pt1.m_flags = FLAG_BRIDGE_POINT1;
	objects.push_back(pt1);

	MapObject pt2;
	pt2.m_objectName = "TEST_NewBridgeWide";
	pt2.m_location = { 150.0f, 50.0f, 0.0f };
	pt2.m_flags = FLAG_BRIDGE_POINT2;
	objects.push_back(pt2);

	std::vector<BridgeSegment> bridges;
	RoadBuildReport report;
	TerrainRoads::buildBridges(objects, index, heightMap, bridges, report);

	REQUIRE(bridges.size() == 1);
	const BridgeSegment &seg = bridges[0];
	REQUIRE(seg.roadType != nullptr);

	// Retail W3DBridge::load (RW 0x004DDEBD / BFME2 0x000DE648): BODY_PRISTINE (0)
	CHECK(seg.roadType->modelForDamage(0) == "CBBrdge1");
	CHECK(seg.roadType->textureForDamage(0) == "OSBridge2.tga");
	CHECK(seg.roadType->bridgeScale == 0.75f);

	// RadarColor matches roads.ini TEST_NewBridgeWide (R:128 G:64 B:32)
	CHECK(seg.radarColor[0] == doctest::Approx(128.0f / 255.0f)); // tolerance: float division
	CHECK(seg.radarColor[1] == doctest::Approx(64.0f / 255.0f));  // tolerance: float division
	CHECK(seg.radarColor[2] == doctest::Approx(32.0f / 255.0f));  // tolerance: float division
}
