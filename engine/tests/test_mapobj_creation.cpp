// OpenBFME unit tests: the map object creation rules (lane MAPOBJ-1). A synthetic map and synthetic object templates exercise every
// rule of the object loop (GameClient/MapObjectDrawables.h): the expected values are worked out by hand from the rule text (each
// comment names the binary address the rule comes from), never read back from the code under test.

#include "doctest.h"

#include "GameClient/MapCreationHooks.h"
#include "GameClient/MapHordeSpawn.h"
#include "GameClient/MapObjectDrawables.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DDrawModules.h"
#include "GameLogic/Map/TerrainLogic.h"
#include "LuaTestUtil.h"
#include "ObjectTestUtil.h"

#include <cmath>
#include <fstream>
#include <sstream>

namespace
{
const double kPi = 3.14159265358979323846;

struct MapWorld : objtest::World
{
	MapWorld()
	{
		W3DDrawModules::registerTypedDrawModuleData(modules);
		MapHordeSpawn::bindHordeContainData(modules);
	}
};

// flat terrain of w x h cells, border 0, every height sample `raw` (raw 256 = 10 world units)
WorldHeightMap flatMap(int w, int h, int raw)
{
	WorldHeightMap m;
	m.m_width = w;
	m.m_height = h;
	m.m_borderSize = 0;
	m.m_dataSize = w * h;
	m.m_data.assign((size_t)w * h, (std::uint16_t)raw);
	return m;
}

MapObject object(const char *name, float x, float y, float z = 0.0f, float angle = 0.0f, int flags = 0)
{
	MapObject o;
	o.m_objectName = name;
	o.m_location = { x, y, z };
	o.m_angle = angle;
	o.m_flags = flags;
	return o;
}

struct Scene
{
	LoadedMap map;
	MapObjectOptions options;
	std::unique_ptr<TerrainLogic> terrain;
	MapObjectDrawables out;

	explicit Scene(int raw = 256, int cells = 100)
	{
		map.heightMap = flatMap(cells, cells, raw);
		map.chunks.hasObjectsList = true;
		options.useTrees = true;
	}
	void run(MapWorld &w)
	{
		terrain.reset(new TerrainLogic());
		terrain->init(map.heightMap, map.chunks, nullptr);
		MapObjectCreation::build(map, "test", w.things, *terrain, options, out);
	}
	const MapObjectRecord &rec(size_t i) const { return out.records.at(i); }
	size_t count(MapObjectFate f) const { return out.report.byFate[f]; }
};

const char *kObjects =
	"Object Tree1\n  Draw = W3DTreeDraw ModuleTag_01\n    ModelName = PTTree01\n    TextureName = tree.tga\n  End\n  KindOf = SHRUBBERY TREE\nEnd\n"
	"Object Shrub1\n  Draw = W3DTreeDraw ModuleTag_01\n    ModelName = PTShrub01\n  End\n  KindOf = SHRUBBERY SHRUB OPTIMIZED_PROP\nEnd\n"
	"Object Prop1\n  Draw = W3DPropDraw ModuleTag_01\n    ModelName = WPRock01\n  End\n  KindOf = OPTIMIZED_PROP\nEnd\n"
	"Object Fluff1\n  Draw = W3DPropDraw ModuleTag_01\n    ModelName = WPFluff\n  End\n  KindOf = CLEARED_BY_BUILD\nEnd\n"
	"Object Fence1\n  Draw = W3DPropDraw ModuleTag_01\n    ModelName = WPFence\n  End\n  FenceWidth = 5.0\n  KindOf = CLEARED_BY_BUILD\nEnd\n"
	"Object Sound1\n  Draw = W3DDefaultDraw ModuleTag_01\n  End\n  KindOf = OPTIMIZED_SOUND\nEnd\n"
	"Object Building1\n  Draw = W3DScriptedModelDraw ModuleTag_01\n"
	"    DefaultModelConditionState\n      Model = BldDay\n    End\n"
	"    ModelConditionState = NIGHT\n      Model = BldNight\n    End\n"
	"    ModelConditionState = SNOW\n      Model = BldSnow\n    End\n"
	"  End\n  Scale = 2.0\n  KindOf = STRUCTURE\nEnd\n"
	"Object Anchored\n  Draw = W3DScriptedModelDraw ModuleTag_01\n    DefaultModelConditionState\n      Model = Anch\n    End\n  End\n"
	"  GeometryRotationAnchorOffset = X:100 Y:0\n  KindOf = STRUCTURE\nEnd\n"
	"Object AnchoredXY\n  Draw = W3DScriptedModelDraw ModuleTag_01\n    DefaultModelConditionState\n      Model = Anch2\n    End\n  End\n"
	"  GeometryRotationAnchorOffset = X:30 Y:20\n  KindOf = STRUCTURE\nEnd\n"
	"Object Invisible\n  Draw = W3DScriptedModelDraw ModuleTag_01\n    DefaultModelConditionState\n      Model = None\n    End\n  End\n  KindOf = INFANTRY\nEnd\n"
	"Object Effect1\n  Draw = W3DLightDraw ModuleTag_01\n  End\n  KindOf = STRUCTURE\nEnd\n"
	"Object NoDraw\n  KindOf = STRUCTURE\nEnd\n"
	"Object ShrubOnlyStruct\n  Draw = W3DScriptedModelDraw ModuleTag_01\n    DefaultModelConditionState\n      Model = ShrubStruct\n    End\n  End\n  KindOf = SHRUBBERY STRUCTURE\nEnd\n"
	"Object FloorBuilding\n  Draw = W3DFloorDraw ModuleTag_Floor\n    ModelName = Bib\n    StartHidden = Yes\n  End\n"
	"  Draw = W3DScriptedModelDraw ModuleTag_Draw\n    DefaultModelConditionState\n      Model = FloorBldg\n    End\n  End\n  KindOf = STRUCTURE\nEnd\n";

size_t indexOfFate(const Scene &s, MapObjectFate f)
{
	for (size_t i = 0; i < s.out.records.size(); ++i)
	{
		if (s.out.records[i].fate == f)
		{
			return i;
		}
	}
	return (size_t)-1;
}
} // namespace

TEST_CASE("mapobj creation: the KindOf bit numbers the loop depends on are the binary's name table indices (RW 0xDA0E68)")
{
	CHECK(MapObjectCreation::kindOfIndex("SHRUBBERY") == 6);
	CHECK(MapObjectCreation::kindOfIndex("CAN_CAST_REFLECTIONS") == 5);
	CHECK(MapObjectCreation::kindOfIndex("CLEARED_BY_BUILD") == 51);
	CHECK(MapObjectCreation::kindOfIndex("WALK_ON_TOP_OF_WALL") == 60);
	CHECK(MapObjectCreation::kindOfIndex("TREE") == 94);
	CHECK(MapObjectCreation::kindOfIndex("SHRUB") == 95);
	CHECK(MapObjectCreation::kindOfIndex("OPTIMIZED_PROP") == 100);
	CHECK(MapObjectCreation::kindOfIndex("OPTIMIZED_SOUND") == 192);
	CHECK(MapObjectCreation::kindOfIndex("shrubbery") == 6); // case-insensitive like the binary's scan
	CHECK(MapObjectCreation::kindOfIndex("OPTIMIZED_TREE") == -1); // ZH's bit does not exist in RotWK
}

TEST_CASE("mapobj creation: the reader drops objects outside z [-1000, 12799.8046875] (RW 0x70F403); version <= 2 forces z to 0")
{
	MapWorld w;
	REQUIRE(w.load(kObjects).empty());
	Scene s;
	s.map.chunks.objects = {
		object("Building1", 50, 50, -1000.0f),            // 0: the lower bound itself stays
		object("Building1", 50, 50, -1000.01f),           // 1: below: dropped
		object("Building1", 50, 50, 12799.8046875f),      // 2: the upper bound itself stays
		object("Building1", 50, 50, 12799.81f),           // 3: above: dropped
		object("Building1", 50, 50, 5.0f),                // 4
	};
	s.run(w);
	REQUIRE(s.out.records.size() == 5);
	CHECK(s.rec(0).fate == MAPOBJ_OBJECT);
	CHECK(s.rec(1).fate == MAPOBJ_CULLED_Z);
	CHECK(s.rec(2).fate == MAPOBJ_OBJECT);
	CHECK(s.rec(3).fate == MAPOBJ_CULLED_Z);
	CHECK(s.rec(4).fate == MAPOBJ_OBJECT);
	CHECK(s.count(MAPOBJ_CULLED_Z) == 2);
	// z is a height ABOVE the ground: ground 10 (raw 256) + z
	const MapObjectDrawable &d4 = s.out.drawables.back();
	CHECK(d4.position.z == doctest::Approx(15.0f));

	Scene old;
	old.map.versions["/ObjectsList"] = { 2 };
	old.map.chunks.objects = { object("Building1", 50, 50, 5000.0f), object("Building1", 50, 50, -5000.0f) };
	old.run(w);
	CHECK(old.count(MAPOBJ_CULLED_Z) == 0); // z forced to 0 before the range test
	CHECK(old.out.drawables[0].position.z == doctest::Approx(10.0f));
}

TEST_CASE("mapobj creation: road and bridge point flags are skipped, other flags are not (RW 0x62DE22: flags & 0x36)")
{
	MapWorld w;
	REQUIRE(w.load(kObjects).empty());
	Scene s;
	const int flags[] = { FLAG_ROAD_POINT1, FLAG_ROAD_POINT2, FLAG_BRIDGE_POINT1, FLAG_BRIDGE_POINT2, FLAG_ROAD_CORNER_ANGLED, FLAG_ROAD_CORNER_TIGHT,
		FLAG_ROAD_JOIN, FLAG_DONT_RENDER, FLAG_DRAWS_IN_MIRROR };
	for (int f : flags)
	{
		s.map.chunks.objects.push_back(object("Building1", 50, 50, 0, 0, f));
	}
	s.run(w);
	REQUIRE(s.out.records.size() == 9);
	CHECK(s.rec(0).fate == MAPOBJ_ROAD_BRIDGE_POINT);
	CHECK(s.rec(1).fate == MAPOBJ_ROAD_BRIDGE_POINT);
	CHECK(s.rec(2).fate == MAPOBJ_ROAD_BRIDGE_POINT);
	CHECK(s.rec(3).fate == MAPOBJ_ROAD_BRIDGE_POINT);
	for (size_t i = 4; i < 9; ++i)
	{
		CHECK_MESSAGE(s.rec(i).fate == MAPOBJ_OBJECT, "flag " << flags[i]); // 0x8, 0x40, 0x80, 0x100 (DONT_RENDER: nothing tests it), 1
	}
	CHECK(s.out.drawables[8 - 4].drawsInMirror); // FLAG_DRAWS_IN_MIRROR
	CHECK_FALSE(s.out.drawables[0].drawsInMirror);
}

TEST_CASE("mapobj creation: objects without a template are classified, never dropped silently; a name differing in case is an error")
{
	MapWorld w;
	REQUIRE(w.load(kObjects).empty());
	Scene s;
	MapObject wp = object("*Waypoints/Waypoint", 10, 10);
	wp.m_properties.setInt("waypointID", 5);
	MapObject gen = object("*GenericAIObjects/GenericAIObject", 10, 10);
	gen.m_properties.setInt("GenericAIObjectID", 1);
	MapObject scorch = object("Scorch", 10, 10);
	scorch.m_properties.setInt("scorchType", 2);
	MapObject light = object("SomeLight", 10, 10);
	light.m_properties.setReal("lightHeightAboveTerrain", 12.0f);
	s.map.chunks.objects = { wp, gen, scorch, light, object("", 1, 1), object("*Lonely", 1, 1), object("Nope", 1, 1), object("building1", 1, 1) };
	s.run(w);
	REQUIRE(s.out.records.size() == 8);
	CHECK(s.rec(0).fate == MAPOBJ_WAYPOINT);
	CHECK(s.rec(1).fate == MAPOBJ_GENERIC_AI);
	CHECK(s.rec(2).fate == MAPOBJ_SCORCH);
	CHECK(s.rec(3).fate == MAPOBJ_LIGHT);
	CHECK(s.rec(4).fate == MAPOBJ_NO_TEMPLATE_EMPTY);
	CHECK(s.rec(5).fate == MAPOBJ_NO_TEMPLATE_STAR);
	CHECK(s.rec(6).fate == MAPOBJ_UNRESOLVED);
	CHECK(s.rec(7).fate == MAPOBJ_UNRESOLVED); // the lookup is case sensitive (RW 0x6D12E5): "building1" is not "Building1"
	CHECK(s.rec(7).detail.find("differs only in case from 'Building1'") != std::string::npos);
	CHECK(s.out.report.unresolved.size() == 2);
	CHECK(s.out.report.caseMismatch.size() == 1);
	CHECK(s.out.report.errors.size() == 2); // both unresolved names reach the error list
	CHECK(s.out.drawables.empty());
}

TEST_CASE("mapobj creation: the client-only path (trees, shrubs, props, fluff, unnamed sounds) and the full object path (RW 0x62DFxx-0x62E4xx)")
{
	MapWorld w;
	REQUIRE(w.load(kObjects).empty());
	Scene s;
	MapObject namedSound = object("Sound1", 1, 1);
	namedSound.m_properties.setAsciiString("objectName", "MySound");
	MapObject emptyNamedSound = object("Sound1", 1, 1);
	emptyNamedSound.m_properties.setAsciiString("objectName", "");
	s.map.chunks.objects = { object("Tree1", 20, 20), object("Shrub1", 30, 30), object("Prop1", 40, 40), object("Fluff1", 50, 50), object("Fence1", 60, 60),
		object("Sound1", 70, 70), namedSound, emptyNamedSound, object("Building1", 80, 80), object("ShrubOnlyStruct", 85, 85) };
	s.run(w);
	REQUIRE(s.out.records.size() == 10);
	CHECK(s.rec(0).fate == MAPOBJ_CLIENT_TREE);   // KindOf TREE (bit 94)
	CHECK(s.rec(1).fate == MAPOBJ_CLIENT_SHRUB);  // SHRUB (bit 95) wins over OPTIMIZED_PROP: tree, then shrub, then sound, then prop
	CHECK(s.rec(2).fate == MAPOBJ_CLIENT_PROP);   // OPTIMIZED_PROP (bit 100)
	CHECK(s.rec(3).fate == MAPOBJ_CLIENT_PROP);   // CLEARED_BY_BUILD with FenceWidth 0: fluff
	CHECK(s.rec(4).fate == MAPOBJ_OBJECT);        // CLEARED_BY_BUILD with FenceWidth 5: a fence is a real object
	CHECK(s.rec(5).fate == MAPOBJ_CLIENT_SOUND);  // OPTIMIZED_SOUND without objectName
	CHECK(s.rec(6).fate == MAPOBJ_OBJECT);        // OPTIMIZED_SOUND with a non-empty objectName is a full object
	CHECK(s.rec(7).fate == MAPOBJ_CLIENT_SOUND);  // an empty objectName counts as none
	CHECK(s.rec(8).fate == MAPOBJ_OBJECT);
	CHECK(s.rec(9).fate == MAPOBJ_OBJECT);        // SHRUBBERY alone does not make a client-only object (TREE / SHRUB / props do)
	// the client-only path reads the template's first draw module only; a tree draws its ModelName
	REQUIRE(s.out.drawables.size() == 10);
	CHECK(s.out.drawables[0].draws.size() == 1);
	CHECK(s.out.drawables[0].draws[0].model == "PTTree01");
	CHECK(s.out.drawables[1].draws[0].model == "PTShrub01");
	CHECK(s.out.drawables[2].draws[0].model == "WPRock01");
	// sounds draw nothing; the draw module W3DDefaultDraw draws nothing in a release build anyway
	CHECK(s.out.drawables[5].draws[0].model.empty());
	// shrubbery switched off: SHRUBBERY objects are skipped (the full-object ShrubOnlyStruct too)
	Scene off;
	off.options.useTrees = false;
	off.map.chunks.objects = s.map.chunks.objects;
	off.run(w);
	CHECK(off.rec(0).fate == MAPOBJ_SHRUBBERY_OFF);
	CHECK(off.rec(1).fate == MAPOBJ_SHRUBBERY_OFF);
	CHECK(off.rec(2).fate == MAPOBJ_CLIENT_PROP);
	CHECK(off.rec(9).fate == MAPOBJ_SHRUBBERY_OFF);
	CHECK(off.rec(8).fate == MAPOBJ_OBJECT);
}

TEST_CASE("mapobj creation: position, angle, scale and the model of the state")
{
	MapWorld w;
	REQUIRE(w.load(kObjects).empty());
	Scene s(512); // ground 20
	MapObject a = object("Building1", 100, 200, 3.0f, (float)(3.0 * kPi)); // 3 PI normalises to PI (the range is (-PI, PI])
	MapObject b = object("Building1", 100, 200, 0.0f, (float)(-kPi));      // -PI is excluded: +PI
	MapObject c = object("Building1", 100, 200, 0.0f, 1.0f);
	MapObject scaled = object("Tree1", 100, 200);
	scaled.m_properties.setReal("objectPrototypeScale", 1.5f);
	MapObject scaledObj = object("Building1", 100, 200);
	scaledObj.m_properties.setReal("objectPrototypeScale", 0.5f);
	s.map.chunks.objects = { a, b, c, scaled, scaledObj };
	s.run(w);
	REQUIRE(s.out.drawables.size() == 5);
	const MapObjectDrawable &da = s.out.drawables[0];
	CHECK(da.position.x == 100.0f);
	CHECK(da.position.y == 200.0f);
	CHECK(da.position.z == doctest::Approx(23.0f)); // z + ground
	CHECK(da.angle == doctest::Approx((float)kPi).epsilon(1e-5));
	CHECK(s.out.drawables[1].angle == doctest::Approx((float)kPi).epsilon(1e-5));
	CHECK(s.out.drawables[2].angle == doctest::Approx(1.0f));
	// rotation about Z by 1 rad: columns X = (cos, sin, 0), Y = (-sin, cos, 0), Z = (0, 0, 1) in row-major storage
	const MapObjectDrawable &dc = s.out.drawables[2];
	CHECK(dc.basis[0] == doctest::Approx(std::cos(1.0)));
	CHECK(dc.basis[1] == doctest::Approx(-std::sin(1.0)));
	CHECK(dc.basis[3] == doctest::Approx(std::sin(1.0)));
	CHECK(dc.basis[4] == doctest::Approx(std::cos(1.0)));
	CHECK(dc.basis[8] == 1.0f);
	// scale: the template Scale (Building1 2.0, default 1.0 for the tree) times objectPrototypeScale when present (RW 0x62E36F, 0x695E5A)
	CHECK(da.scale == 2.0f);
	CHECK(s.out.drawables[3].scale == doctest::Approx(1.5f));
	CHECK(s.out.drawables[4].scale == doctest::Approx(1.0f)); // 2.0 * 0.5
	CHECK(s.out.report.withPrototypeScale == 2);
	CHECK(da.draws[0].model == "BldDay");
}

TEST_CASE("mapobj creation: GeometryRotationAnchorOffset moves the location as RW 0xAD1A60 does (x += ax cos + ay sin, y += ay cos + ax sin)")
{
	MapWorld w;
	REQUIRE(w.load(kObjects).empty());
	Scene s;
	const float angle = (float)(kPi / 2.0);
	s.map.chunks.objects = { object("Anchored", 50, 50, 0, 0.0f), object("Anchored", 50, 50, 0, angle), object("AnchoredXY", 10, 10, 0, angle) };
	s.run(w);
	REQUIRE(s.out.drawables.size() == 3);
	// ax = 100, ay = 0: the offset rotated by the angle: angle 0 -> (+100, 0); angle PI/2 -> (0, +100)
	CHECK(s.out.drawables[0].position.x == doctest::Approx(150.0f));
	CHECK(s.out.drawables[0].position.y == doctest::Approx(50.0f));
	CHECK(s.out.drawables[1].position.x == doctest::Approx(50.0f).epsilon(1e-4));
	CHECK(s.out.drawables[1].position.y == doctest::Approx(150.0f));
	// ay != 0: the binary adds ay * sin to x and ay * cos to y (NOT a rotation: ay = 20 at PI/2 gives x += 30*0 + 20*1, y += 20*0 + 30*1)
	CHECK(s.out.drawables[2].position.x == doctest::Approx(10.0f + 20.0f).epsilon(1e-4));
	CHECK(s.out.drawables[2].position.y == doctest::Approx(10.0f + 30.0f).epsilon(1e-4));
	CHECK(s.out.report.movedByAnchor == 3);
}

TEST_CASE("mapobj creation: alignToTerrain builds the basis from the terrain normal (RW 0x67D208); client-only and full objects alike")
{
	MapWorld w;
	REQUIRE(w.load(kObjects).empty());
	Scene s;
	// a slope rising 1 unit of raw height per cell column: the plane z = x * (10/256) / 10 per world unit -> dz/dx = 1/256 * ... use a steep one
	for (int y = 0; y < 100; ++y)
	{
		for (int x = 0; x < 100; ++x)
		{
			s.map.heightMap.m_data[(size_t)(x + y * 100)] = (std::uint16_t)(x * 1280); // 1280 raw = 50 world units per cell (10 units): slope 5
		}
	}
	MapObject o = object("Building1", 45, 45, 0, 0.0f);
	o.m_properties.setBool("alignToTerrain", true);
	MapObject t = object("Tree1", 45, 45, 0, 0.0f);
	t.m_properties.setBool("alignToTerrain", true);
	MapObject flat = object("Building1", 45, 45, 0, 0.0f);
	flat.m_properties.setBool("alignToTerrain", false);
	s.map.chunks.objects = { o, t, flat };
	s.run(w);
	REQUIRE(s.out.drawables.size() == 3);
	CHECK(s.out.report.alignedToTerrain == 2);
	for (size_t i = 0; i < 2; ++i)
	{
		const MapObjectDrawable &d = s.out.drawables[i];
		REQUIRE(d.alignToTerrain);
		const double nl = std::sqrt((double)d.normal.x * d.normal.x + (double)d.normal.y * d.normal.y + (double)d.normal.z * d.normal.z);
		CHECK(nl == doctest::Approx(1.0).epsilon(1e-4));
		CHECK(d.normal.x < 0.0f); // the ground rises towards +x: the normal leans towards -x
		// the Z column is the normal; the X column (angle 0) lies in the tangent plane: X . N == 0 with X.x > 0
		const double X[3] = { d.basis[0], d.basis[3], d.basis[6] }, Y[3] = { d.basis[1], d.basis[4], d.basis[7] }, N[3] = { d.basis[2], d.basis[5], d.basis[8] };
		CHECK(N[0] == doctest::Approx(d.normal.x).epsilon(1e-5));
		CHECK(N[2] == doctest::Approx(d.normal.z).epsilon(1e-5));
		CHECK(X[0] * N[0] + X[1] * N[1] + X[2] * N[2] == doctest::Approx(0.0).epsilon(1e-5));
		CHECK(Y[0] * N[0] + Y[1] * N[1] + Y[2] * N[2] == doctest::Approx(0.0).epsilon(1e-5));
		CHECK(X[0] > 0.0);
		// right handed: X x Y = N
		CHECK(X[1] * Y[2] - X[2] * Y[1] == doctest::Approx(N[0]).epsilon(1e-4));
		CHECK(X[2] * Y[0] - X[0] * Y[2] == doctest::Approx(N[1]).epsilon(1e-4));
		CHECK(X[0] * Y[1] - X[1] * Y[0] == doctest::Approx(N[2]).epsilon(1e-4));
	}
	CHECK_FALSE(s.out.drawables[2].alignToTerrain);
	CHECK(s.out.drawables[2].basis[8] == 1.0f);
}

TEST_CASE("mapobj creation: initial model condition flags (NIGHT / SNOW), the model of the state, and what client-only objects ignore")
{
	MapWorld w;
	REQUIRE(w.load(kObjects).empty());
	const int night = ModelCondition::indexOf("NIGHT"), snow = ModelCondition::indexOf("SNOW");
	REQUIRE(night == 7);
	REQUIRE(snow == 8);
	auto sceneWith = [&](bool nightMap, bool snowMap, bool followTod, bool followWeather) {
		std::unique_ptr<Scene> s(new Scene());
		s->map.chunks.hasGlobalLighting = true;
		s->map.chunks.lighting.timeOfDay = nightMap ? 4 : 2;
		s->map.chunks.hasWorldInfo = true;
		s->map.chunks.worldInfo.setInt("weather", snowMap ? 1 : 0);
		s->options.forceModelsToFollowTimeOfDay = followTod;
		s->options.forceModelsToFollowWeather = followWeather;
		MapObject plain = object("Building1", 10, 10);
		MapObject keyNight = object("Building1", 10, 10);
		keyNight.m_properties.setInt("objectTime", 2);
		MapObject keyDay = object("Building1", 10, 10);
		keyDay.m_properties.setInt("objectTime", 1);
		MapObject keySnow = object("Building1", 10, 10);
		keySnow.m_properties.setInt("objectWeather", 2);
		MapObject keyNoSnow = object("Building1", 10, 10);
		keyNoSnow.m_properties.setInt("objectWeather", 1);
		MapObject keyOther = object("Building1", 10, 10);
		keyOther.m_properties.setInt("objectTime", 3);
		MapObject tree = object("Tree1", 10, 10);
		tree.m_properties.setInt("objectTime", 2);
		s->map.chunks.objects = { plain, keyNight, keyDay, keySnow, keyNoSnow, keyOther, tree };
		s->run(w);
		return s;
	};
	{
		// GameData follows time of day and weather (RotWK GameData.ini: both Yes, RW 0xBFFAC0 / 0xBFFAD0), night + snow map
		std::unique_ptr<Scene> s = sceneWith(true, true, true, true);
		REQUIRE(s->out.drawables.size() == 7);
		const auto &d = s->out.drawables;
		CHECK(d[0].flags.test(night));
		CHECK(d[0].flags.test(snow));
		CHECK(d[1].flags.test(night));            // objectTime 2 sets
		CHECK_FALSE(d[2].flags.test(night));      // objectTime 1 clears
		CHECK(d[2].flags.test(snow));
		CHECK(d[3].flags.test(snow));
		CHECK_FALSE(d[4].flags.test(snow));       // objectWeather 1 clears
		CHECK(d[4].flags.test(night));
		CHECK(d[5].flags.test(night));            // objectTime 3 changes nothing
		// the model follows the state: NIGHT is defined before SNOW, so the first state whose conditions are all set wins (RW 0x4B4379)
		CHECK(d[0].draws[0].model == "BldNight");
		CHECK(d[2].draws[0].model == "BldSnow");  // night cleared: SNOW matches
		CHECK(d[4].draws[0].model == "BldNight"); // snow cleared, night stays
		// the client-only tree: SNOW / NIGHT from the map (ZH addProp), the object keys are not read on this path
		CHECK(d[6].flags.test(night));
		CHECK(d[6].flags.test(snow));
		CHECK(s->out.report.nightFlagged >= 5);
	}
	{
		// the switches off: only the object keys act
		std::unique_ptr<Scene> s = sceneWith(true, true, false, false);
		const auto &d = s->out.drawables;
		CHECK_FALSE(d[0].flags.any());
		CHECK(d[0].draws[0].model == "BldDay");
		CHECK(d[1].flags.test(night));
		CHECK(d[3].flags.test(snow));
	}
	{
		// a day, snow-free map: nothing is set
		std::unique_ptr<Scene> s = sceneWith(false, false, true, true);
		CHECK_FALSE(s->out.drawables[0].flags.any());
		CHECK_FALSE(s->out.drawables[6].flags.any());
	}
}

TEST_CASE("mapobj creation: draw modules that draw nothing are reported with their reason")
{
	MapWorld w;
	REQUIRE(w.load(kObjects).empty());
	Scene s;
	s.map.chunks.objects = { object("Invisible", 10, 10), object("Effect1", 10, 10), object("NoDraw", 10, 10), object("FloorBuilding", 10, 10) };
	s.run(w);
	REQUIRE(s.out.drawables.size() == 4);
	CHECK(s.out.drawables[0].draws[0].notDrawnReason == "the state's model is empty or NONE");
	CHECK(s.out.drawables[1].draws[0].notDrawnReason.find("effect draw") != std::string::npos);
	CHECK(s.out.drawables[2].draws.empty());
	// floors that StartHidden are not drawn (S-115), the model draw next to them is
	REQUIRE(s.out.drawables[3].draws.size() == 2);
	CHECK(s.out.drawables[3].draws[0].notDrawnReason.find("StartHidden") != std::string::npos);
	CHECK(s.out.drawables[3].draws[1].model == "FloorBldg");
	CHECK(s.out.report.notDrawnByReason["template has no draw module"] == 1);
	CHECK(s.out.report.drawables == 1);
}

TEST_CASE("mapobj creation: owners resolve through the SidesList and Teams chunks; a missing owner is reported")
{
	MapWorld w;
	REQUIRE(w.load(kObjects).empty());
	Scene s;
	SidesInfo civ, p1;
	civ.dict.setAsciiString("playerName", "");
	p1.dict.setAsciiString("playerName", "Player_1");
	p1.dict.setInt("playerColor", 0x123456);
	p1.dict.setBool("playerIsHuman", true);
	s.map.sides.sides = { civ, p1 };
	Dict team;
	team.setAsciiString("teamName", "teamPlayer_1");
	team.setAsciiString("teamOwner", "Player_1");
	s.map.sides.teams = { team };
	MapObject a = object("Building1", 10, 10), b = object("Building1", 10, 10), c = object("Building1", 10, 10), d = object("Building1", 10, 10);
	a.m_properties.setAsciiString("originalOwner", "Player_1/teamPlayer_1");
	b.m_properties.setAsciiString("originalOwner", "/teamPlyrCivilian");
	c.m_properties.setAsciiString("originalOwner", "teamPlayer_1");
	d.m_properties.setAsciiString("originalOwner", "Nobody/teamNobody");
	s.map.chunks.objects = { a, b, c, d };
	s.run(w);
	REQUIRE(s.out.drawables.size() == 4);
	REQUIRE(s.out.sides.size() == 2);
	CHECK(s.out.sides[1].name == "Player_1");
	CHECK(s.out.sides[1].hasColor);
	CHECK(s.out.sides[1].colorRGB == 0x123456);
	CHECK(s.out.sides[1].isHuman);
	CHECK(s.out.drawables[0].sideIndex == 1);
	CHECK(s.out.drawables[1].sideIndex == 0); // "/team": the neutral side
	CHECK(s.out.drawables[2].sideIndex == 1); // a plain team name goes through Teams
	CHECK(s.out.drawables[3].sideIndex == -1);
	CHECK(s.out.report.unownedSides == 1);
	CHECK(s.out.report.ownerProblems.size() == 1);
}

TEST_CASE("mapobj creation: map.ini overrides (load type 2) decide the model; KindOf macros expand at parse time, not at map time")
{
	MapWorld w;
	REQUIRE(w.load(
		"#define TREEKINDS SHRUBBERY TREE\n"
		"Object MacroTree\n  Draw = W3DTreeDraw ModuleTag_01\n    ModelName = MacroTreeMdl\n  End\n  KindOf = TREEKINDS\nEnd\n"
		"Object LateMacro\n  Draw = W3DScriptedModelDraw ModuleTag_01\n    DefaultModelConditionState\n      Model = Late\n    End\n  End\n  KindOf = LATEKIND STRUCTURE\nEnd\n",
		INI_LOAD_OVERWRITE, "objects.ini").empty());
	// a LATER file defines LATEKIND (retail: experiencelevels.ini defines ARAGORN after the object files)
	REQUIRE(w.load("#define LATEKIND TREE\n", INI_LOAD_OVERWRITE, "later.ini").empty());
	Scene s;
	s.map.chunks.objects = { object("MacroTree", 10, 10), object("LateMacro", 10, 10) };
	s.run(w);
	CHECK(s.rec(0).fate == MAPOBJ_CLIENT_TREE); // TREEKINDS was defined before the object: its words are KindOf names
	// LATEKIND was defined after the object: at parse time the token named no macro, so retail's KindOf parse saw an unknown name
	// (the INI error is the object parser's business); here the template keeps no TREE bit and the report says why
	CHECK(s.rec(1).fate == MAPOBJ_OBJECT);
	CHECK_FALSE(s.out.report.errors.empty());

	// a map.ini override replaces a template's draw module data for this map only
	MapWorld w2;
	REQUIRE(w2.load("Object Castle\n  Draw = W3DScriptedModelDraw ModuleTag_01\n    DefaultModelConditionState\n      Model = CastleOld\n    End\n  End\n  KindOf = STRUCTURE\nEnd\n").empty());
	REQUIRE(w2.load("Object Castle\n  ReplaceModule ModuleTag_01\n    Draw = W3DScriptedModelDraw ModuleTag_New\n      DefaultModelConditionState\n        Model = CastleNew\n      End\n    End\n  End\nEnd\n",
		INI_LOAD_CREATE_OVERRIDES, "map.ini").empty());
	Scene s2;
	s2.map.chunks.objects = { object("Castle", 10, 10) };
	s2.run(w2);
	REQUIRE(s2.out.drawables.size() == 1);
	CHECK(s2.out.drawables[0].draws[0].model == "CastleNew");
	w2.things.reset(); // the previous map's overrides go
	Scene s3;
	s3.map.chunks.objects = { object("Castle", 10, 10) };
	s3.run(w2);
	CHECK(s3.out.drawables[0].draws[0].model == "CastleOld");
}

TEST_CASE("mapobj creation: the GameData switches are read by name from the first GameData block; a missing key is an error")
{
	MapObjectOptions o;
	std::string err;
	CHECK(MapObjectGameData::scanText("GameData\n  Foo = 1\n  ForceModelsToFollowTimeOfDay \t= Yes ; comment\n  ForceModelsToFollowWeather = No\nEnd\n", o, &err));
	CHECK(o.forceModelsToFollowTimeOfDay);
	CHECK_FALSE(o.forceModelsToFollowWeather);
	MapObjectOptions p;
	CHECK_FALSE(MapObjectGameData::scanText("GameData\n  ForceModelsToFollowWeather = Yes\nEnd\n", p, &err));
	CHECK(err.find("ForceModelsToFollowTimeOfDay") != std::string::npos);
	CHECK_FALSE(MapObjectGameData::scanText("Weather\n  ForceModelsToFollowTimeOfDay = Yes\n  ForceModelsToFollowWeather = Yes\nEnd\n", p, &err)); // not in a GameData block
}

TEST_CASE("mapobj creation: horde members stand on the formation slots, rotated by the horde's angle (HordeContainCore, RW 0x875847)")
{
	MapWorld w;
	REQUIRE(w.load(
		"Object Soldier\n  Draw = W3DScriptedModelDraw ModuleTag_01\n    DefaultModelConditionState\n      Model = SoldierMdl\n    End\n  End\n  KindOf = INFANTRY\nEnd\n"
		"Object SoldierHorde\n"
		"  Draw = W3DScriptedModelDraw ModuleTag_01\n    DefaultModelConditionState\n      Model = None\n    End\n  End\n"
		"  Behavior = HordeContain ModuleTag_Horde\n"
		"    InitialPayload = Soldier 3\n"
		"    Slots = 6\n"
		"    RankInfo = RankNumber:1 UnitType:Soldier Position:X:50 Y:0 Position:X:50 Y:20 Position:X:50 Y:-20\n"
		"    RankInfo = RankNumber:2 UnitType:Soldier Position:X:30 Y:0 Position:X:30 Y:20 Position:X:30 Y:-20\n"
		"  End\n"
		"  KindOf = INFANTRY HORDE\nEnd\n"
		"Object OverfullHorde\n"
		"  Behavior = HordeContain ModuleTag_Horde\n"
		"    InitialPayload = Soldier 4\n"
		"    Slots = 3\n"
		"    RankInfo = RankNumber:1 UnitType:Soldier Position:X:10 Y:0 Position:X:10 Y:5\n"
		"  End\n"
		"  KindOf = INFANTRY HORDE\nEnd\n").empty());
	Scene s;
	// the horde stands at (200, 300) turned by PI/2: the slot offset (ox, oy) maps to (-oy, ox)
	s.map.chunks.objects = { object("SoldierHorde", 200, 300, 0, (float)(kPi / 2.0)), object("OverfullHorde", 500, 500, 0, 0.0f) };
	s.run(w);
	// 1 horde parent (draws nothing) + 3 members; the second horde: 2 slots for 4 payload members: 2 placed, 2 unplaced
	REQUIRE(s.out.drawables.size() == 2 + 3 + 2);
	CHECK(s.out.report.hordes == 2);
	CHECK(s.out.report.hordeMembers == 5);
	CHECK(s.out.report.hordePayload == 7);
	CHECK(s.out.report.hordeUnplaced == 2);
	const MapObjectDrawable &parent = s.out.drawables[0];
	CHECK_FALSE(parent.hordeMember);
	CHECK(parent.draws[0].model.empty());
	// members in payload order take the first free slots: rank 1 slots (50,0), (50,20), (50,-20)
	const float expected[3][2] = { { 200.0f - 0.0f, 300.0f + 50.0f }, { 200.0f - 20.0f, 300.0f + 50.0f }, { 200.0f + 20.0f, 300.0f + 50.0f } };
	for (size_t i = 0; i < 3; ++i)
	{
		const MapObjectDrawable &m = s.out.drawables[1 + i];
		CHECK(m.hordeMember);
		CHECK(m.objectIndex == 0);
		CHECK(m.draws[0].model == "SoldierMdl");
		CHECK(m.position.x == doctest::Approx(expected[i][0]).epsilon(1e-4));
		CHECK(m.position.y == doctest::Approx(expected[i][1]).epsilon(1e-4));
		CHECK(m.position.z == doctest::Approx(10.0f)); // on the ground (raw 256 = 10)
		CHECK(m.angle == doctest::Approx((float)(kPi / 2.0)));
	}
	// the unit-type matcher: the same template, an EquivalentTo entry, a reskin
	CHECK(MapHordeSpawn::unitTypeMatches(w.things, "Soldier", "Soldier"));
	CHECK_FALSE(MapHordeSpawn::unitTypeMatches(w.things, "Soldier", "SoldierHorde"));
}

TEST_CASE("mapobj creation: every stop line is registered in docs/STOPS.md and carried in every report")
{
	MapWorld w;
	REQUIRE(w.load(kObjects).empty());
	Scene s;
	s.map.chunks.objects = { object("Building1", 10, 10) };
	s.run(w);
	std::ifstream in(std::string(OPENBFME_DOCS_DIR) + "/STOPS.md");
	REQUIRE_MESSAGE(static_cast<bool>(in), "cannot read docs/STOPS.md (OPENBFME_DOCS_DIR = " << OPENBFME_DOCS_DIR << ")");
	std::stringstream ss;
	ss << in.rdbuf();
	const std::string doc = ss.str();
	std::set<std::string> ids;
	for (const std::string &line : s.out.report.stops)
	{
		REQUIRE(line.size() > 7);
		REQUIRE(line.compare(0, 3, "[S-") == 0);
		const std::string id = line.substr(1, 5);
		ids.insert(id);
		CHECK_MESSAGE(doc.find("| " + id + " |") != std::string::npos, "docs/STOPS.md has no row for " << id);
	}
	// the lane's range: S-110 .. S-119
	for (int n = 110; n <= 119; ++n)
	{
		CHECK_MESSAGE(ids.count("S-" + std::to_string(n)) == 1, "report lacks S-" << n);
	}
	CHECK(MapObjectCreation::stopLines().size() == 10);
}

TEST_CASE("mapobj creation: side colours come from playerColor, else from the PreferredColor of a faction PlayerTemplate (S-119)")
{
	MapObjectOptions o;
	std::string err;
	REQUIRE(MapObjectGameData::scanPlayerTemplates(
		"PlayerTemplate FactionCivilian\n  Side = Civilian\n  PlayableSide = No\n  PreferredColor = R:64 G:64 B:64\nEnd\n"
		"PlayerTemplate FactionMen ; the good men\n  PlayableSide      = No\n  StartingBuilding = MenFortress\n  PreferredColor\t  = R:43 G:150 B:179\nEnd\n"
		"PlayerTemplate FactionNoColor\n  StartingBuilding = X\nEnd\n", o, &err));
	CHECK(o.factionColors.size() == 1); // civilians have no StartingBuilding, and a template without a colour has none
	REQUIRE(o.factionColors.count("FactionMen") == 1);
	CHECK(o.factionColors["FactionMen"] == ((43u << 16) | (150u << 8) | 179u));
	CHECK_FALSE(MapObjectGameData::scanPlayerTemplates("Nothing here\n", o, &err));

	MapWorld w;
	REQUIRE(w.load(kObjects).empty());
	Scene s;
	s.options = o;
	SidesInfo mapColor, factionOnly, civilian, none;
	mapColor.dict.setAsciiString("playerName", "P1");
	mapColor.dict.setAsciiString("playerFaction", "FactionMen");
	mapColor.dict.setInt("playerColor", 0xFFFF0000); // the alpha byte is not part of the colour
	factionOnly.dict.setAsciiString("playerName", "P2");
	factionOnly.dict.setAsciiString("playerFaction", "FactionMen");
	civilian.dict.setAsciiString("playerName", "Civ");
	civilian.dict.setAsciiString("playerFaction", "FactionCivilian");
	none.dict.setAsciiString("playerName", "P4");
	s.map.sides.sides = { mapColor, factionOnly, civilian, none };
	s.map.chunks.objects = { object("Building1", 1, 1) };
	s.run(w);
	REQUIRE(s.out.sides.size() == 4);
	CHECK(s.out.sides[0].colorSource == MapSidePlayer::COLOR_MAP);
	CHECK(s.out.sides[0].colorRGB == 0xFF0000u);
	CHECK(s.out.sides[1].colorSource == MapSidePlayer::COLOR_FACTION);
	CHECK(s.out.sides[1].colorRGB == o.factionColors["FactionMen"]);
	CHECK_FALSE(s.out.sides[2].hasColor);
	CHECK_FALSE(s.out.sides[3].hasColor);
	CHECK(s.out.report.sidesWithMapColor == 1);
	CHECK(s.out.report.sidesWithFactionColor == 1);
	CHECK(s.out.report.sidesWithoutColor == 2);
}

// ---- S-110: the OnCreated Lua handlers (scriptevents.xml, scripts.lua) --------------------------------------------------------

namespace
{
const char *kEventsXml =
	"<?xml version=\"1.0\"?>\n<SageLuaScriptSection>\n<Events>\n"
	"  <InternalEvent Name=\"OnCreated\" /> <!-- <EventList Name=\"Commented\"> is inside a comment -->\n"
	"</Events>\n"
	"<EventLists>\n"
	"  <EventList Name=\"Base\">\n    <EventHandler EventName=\"OnDamaged\" ScriptFunctionName=\"OnDamagedBase\" />\n  </EventList>\n"
	"  <EventList Name=\"ArcherFunctions\" Inherit=\"Base\">\n    <EventHandler EventName=\"OnCreated\" ScriptFunctionName=\"OnArcherCreated\" DebugSingleStep=\"false\"/>\n  </EventList>\n"
	"  <EventList Name=\"RangerFunctions\" Inherit=\"ArcherFunctions\">\n    <EventHandler EventName=\"OnCreated\"\t\tScriptFunctionName=\"OnGondorArcherCreated\" />\n  </EventList>\n"
	"  <EventList Name=\"GhostFunctions\" Inherit=\"Base\">\n    <EventHandler EventName=\"OnCreated\" ScriptFunctionName=\"OnGhostCreated\" />\n  </EventList>\n"
	"  <EventList Name=\"QuietFunctions\" Inherit=\"Base\" />\n"
	"</EventLists>\n</SageLuaScriptSection>\n";
const char *kScriptsLua =
	"function OnArcherCreated(self)\n"
	"\tObjectHideSubObjectPermanently( self, \"Glow\", true )\n"
	"end\n\n"
	"function OnGondorArcherCreated(self)\n"
	"\t-- ObjectHideSubObjectPermanently( self, \"arrow\", true )\t-- commented out\n"
	"\tObjectHideSubObjectPermanently( self, \"FireArowTip\", true ) -- hidden because the fire arrow upgrade turns it on\n"
	"\tObjectHideSubObjectPermanently( self, \"GLOW\", true )\n"
	"\tObjectHideSubObjectPermanently( self, \"Torch\", false )\n"
	"\tif ObjectTestModelCondition(self, \"NIGHT\") then\n\t\tObjectGrantUpgrade( self, \"Upgrade_X\" )\n\tend\n"
	"end\n\n"
	"function OnDamagedBase(self)\nend\n";

const char *kHookObjects =
	"Object Ranger\n  Draw = W3DPropDraw ModuleTag_01\n    ModelName = RangerModel\n  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n    AutoAcquireEnemiesWhenIdle = Yes\n    AILuaEventsList = RangerFunctions ; the archers\n  End\n  KindOf = INFANTRY\nEnd\n"
	"Object Ghost\n  Draw = W3DPropDraw ModuleTag_01\n    ModelName = GhostModel\n  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n    AILuaEventsList = GhostFunctions\n  End\n  KindOf = INFANTRY\nEnd\n"
	"Object Quiet\n  Draw = W3DPropDraw ModuleTag_01\n    ModelName = QuietModel\n  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n    AILuaEventsList = QuietFunctions\n  End\n  KindOf = INFANTRY\nEnd\n"
	"Object Unknown\n  Draw = W3DPropDraw ModuleTag_01\n    ModelName = UnknownModel\n  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n    AILuaEventsList = NoSuchFunctions\n  End\n  KindOf = INFANTRY\nEnd\n"
	"Object Plain\n  Draw = W3DPropDraw ModuleTag_01\n    ModelName = PlainModel\n  End\n  KindOf = INFANTRY\nEnd\n"
	"Object RangerTree\n  Draw = W3DTreeDraw ModuleTag_01\n    ModelName = PTTree01\n  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n    AILuaEventsList = RangerFunctions\n  End\n  KindOf = SHRUBBERY TREE\nEnd\n";
} // namespace

TEST_CASE("mapobj creation hooks: scriptevents.xml lists and scripts.lua functions are scanned by name, comments excluded")
{
	CreationScriptData d;
	std::string err;
	REQUIRE_MESSAGE(MapCreationHooks::scan(kEventsXml, kScriptsLua, d, &err), err);
	CHECK(d.loaded);
	CHECK(d.eventLists.size() == 5); // the list inside the XML comment is not one
	CHECK(d.eventLists.count("Commented") == 0);
	CHECK(d.eventLists.at("RangerFunctions").inherit == "ArcherFunctions");
	CHECK(d.eventLists.at("RangerFunctions").onCreated == std::vector<std::string>({ "OnGondorArcherCreated" }));
	CHECK(d.eventLists.at("Base").onCreated.empty()); // OnDamaged is not OnCreated
	CHECK(d.eventLists.at("QuietFunctions").inherit == "Base");
	REQUIRE(d.luaFunctions.count("OnGondorArcherCreated") == 1);
	const CreationScriptData::LuaFunction &f = d.luaFunctions.at("OnGondorArcherCreated");
	CHECK(f.permanentHides == std::vector<std::string>({ "FireArowTip", "GLOW" })); // the commented arrow is not a hide
	CHECK(f.permanentShows == std::vector<std::string>({ "Torch" }));
	CHECK(f.calls == std::vector<std::string>({ "ObjectHideSubObjectPermanently", "ObjectHideSubObjectPermanently", "ObjectHideSubObjectPermanently", "ObjectTestModelCondition", "ObjectGrantUpgrade" }));

	// malformed data is an error, never a silent default
	CreationScriptData bad;
	CHECK_FALSE(MapCreationHooks::scan(kEventsXml, "function A(self)\n\tObjectHideSubObjectPermanently(self, \"X\", true)\n", bad, &err));
	CHECK(err.find("no closing end") != std::string::npos);
	CHECK_FALSE(MapCreationHooks::scan(kEventsXml, "function A(self)\n\tObjectHideSubObjectPermanently(self, Name, true)\nend\n", bad, &err));
	CHECK(err.find("ObjectHideSubObjectPermanently call is not") != std::string::npos);
	CHECK_FALSE(MapCreationHooks::scan("<EventList Name=\"A\"><EventList Name=\"A\">", kScriptsLua, bad, &err));
	CHECK(err.find("defined twice") != std::string::npos);
	CHECK_FALSE(MapCreationHooks::scan("<x/>", kScriptsLua, bad, &err));
	CHECK_FALSE(MapCreationHooks::scan(kEventsXml, "-- nothing\n", bad, &err));
}

TEST_CASE("mapobj creation hooks: full objects count the OnCreated handlers their template has, client-only objects none (S-110)")
{
	MapWorld w;
	REQUIRE_MESSAGE(w.load(kHookObjects).empty(), "template load");
	Scene s;
	std::string err;
	REQUIRE_MESSAGE(MapCreationHooks::scan(kEventsXml, kScriptsLua, s.options.creationScripts, &err), err);
	s.map.chunks.objects = { object("Ranger", 10, 10), object("Ranger", 20, 10), object("Ghost", 30, 10), object("Quiet", 40, 10), object("Plain", 50, 10), object("RangerTree", 60, 10), object("Unknown", 70, 10) };
	s.run(w);
	const MapObjectReport &r = s.out.report;
	CHECK(r.creationHooksScanned);
	// Ranger x2 (RangerFunctions: OnGondorArcherCreated, then the inherited ArcherFunctions' OnArcherCreated), Ghost (its own handler is not defined in scripts.lua: a problem)
	CHECK(r.creationHookObjects == 3);
	CHECK(r.creationHooksByFunction == std::map<std::string, size_t>({ { "OnArcherCreated", 2 }, { "OnGhostCreated", 1 }, { "OnGondorArcherCreated", 2 } }));
	CHECK(r.creationHooksByTemplate == std::map<std::string, size_t>({ { "Ghost: GhostFunctions -> OnGhostCreated", 1 },
		{ "Ranger: RangerFunctions -> OnGondorArcherCreated, OnArcherCreated", 2 } }));
	// permanently hidden sub objects, lower case, each counted once per object: GLOW is named by both handlers of a Ranger
	CHECK(r.creationHookHides == std::map<std::string, size_t>({ { "firearowtip", 2 }, { "glow", 2 } }));
	CHECK(r.creationHookShows == std::map<std::string, size_t>({ { "torch", 2 } }));
	// the tree is a client-only object: no OnCreated is sent; the unknown list and the undefined handler are reported (once per template)
	bool undefinedHandler = false, unknownList = false;
	for (const std::string &e : r.errors)
	{
		undefinedHandler = undefinedHandler || e.find("OnCreated handler OnGhostCreated of GhostFunctions is not defined in scripts.lua") != std::string::npos;
		unknownList = unknownList || e.find("NoSuchFunctions") != std::string::npos;
	}
	CHECK(undefinedHandler);
	CHECK(unknownList);   // a list scriptevents.xml does not define is reported, not skipped

	Scene unscanned;
	unscanned.map.chunks.objects = { object("Ranger", 10, 10) };
	unscanned.run(w);
	CHECK_FALSE(unscanned.out.report.creationHooksScanned); // the report says the scripts were not read, instead of claiming there are no hooks
	CHECK(unscanned.out.report.creationHookObjects == 0);

	// the stop line says where the hooks run and the inheritance rule
	bool s110 = false;
	for (const std::string &line : r.stops)
	{
		s110 = s110 || (line.rfind("[S-110]", 0) == 0 && line.find("OnCreated") != std::string::npos && line.find("MapObjectRuntime::build") != std::string::npos &&
			line.find("RW 0x733FAF") != std::string::npos);
	}
	CHECK(s110);
}

// ---- S-115: static draw fields the runtime stores and does not apply ---------------------------------------------------------

#include "GameClient/MapObjectRuntime.h"

namespace
{
struct NoFiles : W3DFileSource
{
	bool Exists(const std::string &) const override { return false; }
	bool Read(const std::string &, std::vector<std::uint8_t> &, std::string *error) override
	{
		if (error) *error = "no files";
		return false;
	}
};
} // namespace

TEST_CASE("mapobj runtime: floor WeatherTexture / ForceToBack / StaticModelLODMode and prop DistanceFog = No are counted as not applied (S-115)")
{
	MapWorld w;
	REQUIRE(w.load(
		"Object PlainProp\n  Draw = W3DPropDraw ModuleTag_01\n    ModelName = P1\n  End\n  KindOf = OPTIMIZED_PROP\nEnd\n"
		"Object FoggyProp\n  Draw = W3DPropDraw ModuleTag_01\n    ModelName = P2\n    DistanceFog = No\n  End\n  KindOf = OPTIMIZED_PROP\nEnd\n"
		"Object WeatherFloor\n  Draw = W3DFloorDraw ModuleTag_01\n    ModelName = F1\n    WeatherTexture = SNOWY a_snow.tga\n    WeatherTexture = NORMAL a.tga\n    ForceToBack = Yes\n  End\n  KindOf = STRUCTURE\nEnd\n"
		"Object BackFloor\n  Draw = W3DFloorDraw ModuleTag_01\n    ModelName = F2\n    ForceToBack = Yes\n    DistanceFog = No\n    StaticModelLODMode = Yes\n  End\n  KindOf = STRUCTURE\nEnd\n"
		"Object PlainFloor\n  Draw = W3DFloorDraw ModuleTag_01\n    ModelName = F3\n  End\n  KindOf = STRUCTURE\nEnd\n").empty());
	Scene s;
	s.map.chunks.objects = { object("PlainProp", 10, 10), object("FoggyProp", 20, 10), object("FoggyProp", 30, 10), object("WeatherFloor", 40, 10),
		object("BackFloor", 50, 10), object("PlainFloor", 60, 10) };
	s.run(w);
	NoFiles files;
	WW3DAssetManager assets(files);
	MapObjectRuntime runtime(assets);
	runtime.build(s.out, false);
	const MapRuntimeReport &r = runtime.report();
	CHECK(r.propDraws == 3);
	CHECK(r.floorDraws == 3);
	CHECK(r.ignoredDrawFields == std::map<std::string, size_t>({
		{ "W3DFloorDraw DistanceFog = No", 1 }, { "W3DFloorDraw ForceToBack", 2 }, { "W3DFloorDraw StaticModelLODMode", 1 },
		{ "W3DFloorDraw WeatherTexture", 1 }, { "W3DPropDraw DistanceFog = No", 2 } }));
	CHECK(r.scriptsRun == 0);
	for (const std::string &st : r.stops)
	{
		CHECK(st.rfind("[S-091]", 0) != 0); // S-091 is no longer reported by the runtime: the scripts run on real Lua
	}
	bool s115 = false;
	for (const std::string &line : s.out.report.stops)
	{
		s115 = s115 || (line.rfind("[S-115]", 0) == 0 && line.find("WeatherTexture") != std::string::npos && line.find("ForceToBack") != std::string::npos && line.find("DistanceFog") != std::string::npos);
	}
	CHECK(s115);
}

// ---- the hooks run (S-110) ----------------------------------------------------------------------------------------------------

namespace
{
// retail shape: Events / EventList directly under the root
const char *kRunXml =
	"<?xml version=\"1.0\"?>\n<SageLuaScriptSection>\n<Events>\n  <InternalEvent Name=\"OnCreated\" />\n</Events>\n"
	"<EventList Name=\"ArcherFunctions\">\n  <EventHandler EventName=\"OnCreated\" ScriptFunctionName=\"OnArcherCreated\"/>\n</EventList>\n"
	"<EventList Name=\"RangerFunctions\" Inherit=\"ArcherFunctions\">\n  <EventHandler EventName=\"OnCreated\" ScriptFunctionName=\"OnGondorArcherCreated\"/>\n</EventList>\n"
	"</SageLuaScriptSection>\n";
const char *kRunLua =
	"function OnArcherCreated(self)\n\terror('the inherited handler ran')\nend\n"
	"function OnGondorArcherCreated(self)\n"
	"\tObjectHideSubObjectPermanently( self, \"FireArowTip\", true )\n"
	"\tObjectHideSubObjectPermanently( self, \"ModuleTag_01\", true )\n"
	"\tObjectGrantUpgrade( self, \"Upgrade_X\" )\n"
	"end\n";
const char *kRunObjects =
	"Object Ranger\n  Draw = W3DPropDraw ModuleTag_01\n    ModelName = RangerModel\n  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n    AILuaEventsList = RangerFunctions\n  End\n  KindOf = INFANTRY\nEnd\n"
	"Object RangerTree\n  Draw = W3DTreeDraw ModuleTag_01\n    ModelName = PTTree01\n  End\n"
	"  Behavior = AIUpdateInterface ModuleTag_AI\n    AILuaEventsList = RangerFunctions\n  End\n  KindOf = SHRUBBERY TREE\nEnd\n";
} // namespace

TEST_CASE("mapobj runtime: the OnCreated hooks run on the real Lua engine in a retail order: a child's handler replaces the inherited one, a hide reaches the drawable through the host, an upgrade grant is the reported stop S-124")
{
	MapWorld w;
	REQUIRE_MESSAGE(w.load(kRunObjects).empty(), "template load");
	Scene s;
	std::string err;
	REQUIRE_MESSAGE(MapCreationHooks::scan(kRunXml, kRunLua, s.options.creationScripts, &err), err);
	s.map.chunks.objects = { object("Ranger", 10, 10), object("Ranger", 20, 10), object("RangerTree", 30, 10) };
	s.run(w);
	NoFiles files;
	WW3DAssetManager assets(files);
	MapObjectRuntime runtime(assets);
	runtime.setCreationScripts(&s.options.creationScripts);
	runtime.build(s.out);
	const MapRuntimeReport &r = runtime.report();
	CHECK(r.creationScriptsGiven);
	CHECK(r.creationHookObjects == 2); // the tree is client-only: no OnCreated
	CHECK(r.creationHandlers == std::map<std::string, size_t>({ { "OnGondorArcherCreated", 2 } }));
	for (const std::string &e : r.errors)
	{
		CHECK(e.find("OnCreated handler failed") == std::string::npos); // the inherited OnArcherCreated (which errors) did not run
	}
	bool grant = false, sources = false;
	for (const std::string &st : r.stops)
	{
		grant = grant || (st.rfind("[S-124]", 0) == 0 && st.find("findUpgrade") != std::string::npos);
		sources = sources || st.rfind("[S-129]", 0) == 0;
	}
	CHECK(grant);
	CHECK(sources);
	// the handler's ObjectHideSubObjectPermanently(self, "ModuleTag_01", true) matches the Ranger's draw module tag: the MODULE takes it (RW 0x6789B4: no
	// fall through to a sub object of that name) and its model is not instanced; the report counts the requests
	CHECK(r.moduleRequests == 2);
	CHECK(r.modulesHidden == 2);
	CHECK(r.moduleRequestsWithoutModel == 0);
	CHECK(r.hideMisses.count("moduletag_01") == 0);
	size_t hiddenModels = 0;
	for (MapPlacedModel &pm : runtime.models())
	{
		hiddenModels += pm.moduleHidden ? 1 : 0;
	}
	CHECK(hiddenModels == 2);
	for (const std::string &st : r.stops)
	{
		CHECK(st.find("matched draw module shows no model") == std::string::npos);
	}
	// without the scripts nothing runs and the report says so
	MapObjectRuntime plain(assets);
	plain.build(s.out);
	CHECK(plain.report().creationHookObjects == 0);
	bool said = false;
	for (const std::string &st : plain.report().stops)
	{
		said = said || (st.rfind("[S-110]", 0) == 0 && st.find("not run") != std::string::npos);
	}
	CHECK(said);
}

// ---- fix 5: the real engine decides what runs, not the report-only inventory scanner -------------------------------------------------

namespace
{
const char *kRawXml =
	"<?xml version=\"1.0\"?>\n<SageLuaScriptSection>\n<Events>\n  <InternalEvent Name=\"OnCreated\" />\n</Events>\n"
	"<EventList Name=\"L1\">\n  <EventHandler EventName=\"OnCreated\" ScriptFunctionName=\"OnOne\"/>\n</EventList>\n"
	"<EventList Name=\"L2\">\n  <EventHandler EventName=\"OnCreated\" ScriptFunctionName=\"OnRedef\"/>\n</EventList>\n"
	"<EventList Name=\"L3\">\n  <EventHandler EventName=\"OnCreated\" ScriptFunctionName=\"OnDyn\"/>\n</EventList>\n"
	"<EventList Name=\"L4\">\n  <EventHandler EventName=\"OnCreated\" ScriptFunctionName=\"OnComputed\"/>\n</EventList>\n"
	"<EventList Name=\"L5\">\n  <EventHandler EventName=\"OnCreated\" ScriptFunctionName=\"OnNowhere\"/>\n</EventList>\n"
	"</SageLuaScriptSection>\n";
const char *kRawLua =
	"function OnOne(self) HitOne = (HitOne or 0) + 1 end\n" // a valid one-line function: the scanner finds no closing `end` line
	"function OnRedef(self)\n\tHitRedef = 'first'\nend\n"
	"function OnRedef(self)\n\tHitRedef = 'second'\nend\n"
	"setglobal('OnDyn', OnOne)\n"                           // a handler assigned through a global expression
	"function OnComputed(self)\n\tTip = 'Fire'..'Tip'\n\tObjectHideSubObjectPermanently(self, Tip, true)\n\tHitComputed = (HitComputed or 0) + 1\nend\n";
const char *kRawObjects =
	"Object One\n  Draw = W3DPropDraw ModuleTag_01\n    ModelName = M1\n  End\n  Behavior = AIUpdateInterface ModuleTag_AI\n    AILuaEventsList = L1\n  End\n  KindOf = INFANTRY\nEnd\n"
	"Object Redef\n  Draw = W3DPropDraw ModuleTag_01\n    ModelName = M1\n  End\n  Behavior = AIUpdateInterface ModuleTag_AI\n    AILuaEventsList = L2\n  End\n  KindOf = INFANTRY\nEnd\n"
	"Object Dyn\n  Draw = W3DPropDraw ModuleTag_01\n    ModelName = M1\n  End\n  Behavior = AIUpdateInterface ModuleTag_AI\n    AILuaEventsList = L3\n  End\n  KindOf = INFANTRY\nEnd\n"
	"Object Computed\n  Draw = W3DPropDraw ModuleTag_01\n    ModelName = M1\n  End\n  Behavior = AIUpdateInterface ModuleTag_AI\n    AILuaEventsList = L4\n  End\n  KindOf = INFANTRY\nEnd\n"
	"Object Nowhere\n  Draw = W3DPropDraw ModuleTag_01\n    ModelName = M1\n  End\n  Behavior = AIUpdateInterface ModuleTag_AI\n    AILuaEventsList = L5\n  End\n  KindOf = INFANTRY\nEnd\n"
	"Object PlainObj\n  Draw = W3DPropDraw ModuleTag_01\n    ModelName = M1\n  End\n  KindOf = INFANTRY\nEnd\n";

std::string luaGlobalString(LuaScriptEngine &e, const char *name)
{
	lua_State *L = e.logic()->state();
	lua_getglobal(L, name);
	std::string out = lua_isnil(L, -1) ? std::string("<nil>") : std::string(lua_tostring(L, -1));
	lua_settop(L, -2);
	return out;
}
} // namespace

TEST_CASE("mapobj runtime: scripts the report-only scanner cannot read still run on the real engine (one-line function, redefinition, global assignment, computed hide argument); a missing handler global is retail's swallowed failure")
{
	MapWorld w;
	REQUIRE_MESSAGE(w.load(kRawObjects).empty(), "template load");
	Scene s;
	std::string err;
	CHECK_FALSE(MapCreationHooks::scan(kRawXml, kRawLua, s.options.creationScripts, &err)); // the scanner gives up on the one-line function ...
	CHECK(s.options.creationScripts.rawLoaded);                                           // ... but the raw files are kept
	CHECK_FALSE(s.options.creationScripts.loaded);
	CHECK_FALSE(s.options.creationScripts.inventoryError.empty());
	s.map.chunks.objects = { object("One", 10, 10), object("Redef", 20, 10), object("Dyn", 30, 10), object("Computed", 40, 10), object("Nowhere", 50, 10),
		object("PlainObj", 60, 10), object("One", 70, 10) };
	s.run(w);
	NoFiles files;
	WW3DAssetManager assets(files);
	MapObjectRuntime runtime(assets);
	runtime.setCreationScripts(&s.options.creationScripts);
	runtime.build(s.out);
	const MapRuntimeReport &r = runtime.report();
	CHECK(r.creationScriptsGiven);
	CHECK(r.creationListObjects == 6);  // every object with an AILuaEventsList the registry resolves
	CHECK(r.creationHookObjects == 6);  // each of those lists has an OnCreated handler (the registry resolves handler names, not the scanner)
	REQUIRE(runtime.scriptEngine());
	LuaScriptEngine &e = *runtime.scriptEngine();
	CHECK(luaGlobalString(e, "HitOne") == "3");           // OnOne for two `One` objects and for `Dyn` (OnDyn is the same function)
	CHECK(luaGlobalString(e, "HitRedef") == "second");    // the later definition replaces the earlier one
	CHECK(luaGlobalString(e, "HitComputed") == "1");
	CHECK(luaGlobalString(e, "Tip") == "FireTip");
	// the missing handler global: retail's dispatch builds " is not defined." and drops it (RW 0x736019): no call, no alert, no error, and the
	// other objects' handlers are unaffected
	CHECK(e.logic()->alerts().empty());
	CHECK(e.compatibilityFaults() == 0);
	for (const std::string &x : r.errors)
	{
		CHECK(x.find("OnCreated handler failed") == std::string::npos);
	}
	// the scanner failure is reported, not a gate
	bool inventory = false;
	for (const std::string &st : r.stops)
	{
		inventory = inventory || (st.rfind("[S-110]", 0) == 0 && st.find("inventory scan") != std::string::npos);
	}
	CHECK(inventory);
}

// ---- fix 1: the creation draw GetGameLogicRandomValue(1, 999) of RW 0x628892 ----------------------------------------------------------

TEST_CASE("mapobj runtime: every full object draws GetGameLogicRandomValue(1, 999, GameLogic.cpp, 0x19A7) before its drawable and OnCreated, with or without a hook; client-only objects draw none")
{
	const char *objects =
		"Object Hooked\n  Draw = W3DPropDraw ModuleTag_01\n    ModelName = M1\n  End\n  Behavior = AIUpdateInterface ModuleTag_AI\n    AILuaEventsList = LR\n  End\n  KindOf = INFANTRY\nEnd\n"
		"Object PlainObj\n  Draw = W3DPropDraw ModuleTag_01\n    ModelName = M1\n  End\n  KindOf = INFANTRY\nEnd\n"
		"Object RTree\n  Draw = W3DTreeDraw ModuleTag_01\n    ModelName = PTTree01\n  End\n  KindOf = SHRUBBERY TREE\nEnd\n";
	const char *xml =
		"<?xml version=\"1.0\"?>\n<SageLuaScriptSection>\n<Events>\n  <InternalEvent Name=\"OnCreated\" />\n</Events>\n"
		"<EventList Name=\"LR\">\n  <EventHandler EventName=\"OnCreated\" ScriptFunctionName=\"OnRand\"/>\n</EventList>\n</SageLuaScriptSection>\n";
	const char *lua = "function OnRand(self)\n\tPicked = GetRandomNumber()\nend\n";
	MapWorld w;
	REQUIRE_MESSAGE(w.load(objects).empty(), "template load");
	Scene s;
	std::string err;
	REQUIRE_MESSAGE(MapCreationHooks::scan(xml, lua, s.options.creationScripts, &err), err);
	s.map.chunks.objects = { object("PlainObj", 10, 10), object("Hooked", 20, 10), object("RTree", 30, 10), object("PlainObj", 40, 10), object("Hooked", 50, 10) };
	s.run(w);
	NoFiles files;
	WW3DAssetManager assets(files);
	GameLogicRandom rng(RandomAlgorithm::ZH_CarryChain);
	rng.seedRandom(7);
	rng.enableCallLog(true);
	MapObjectRuntime runtime(assets);
	runtime.setLogicRandom(&rng);
	runtime.setCreationScripts(&s.options.creationScripts);
	runtime.build(s.out);
	// the reference sequence (the tree draws none): draw, draw + OnCreated's GetRandomNumber, draw, draw + OnCreated's GetRandomNumber
	GameLogicRandom ref(RandomAlgorithm::ZH_CarryChain);
	ref.seedRandom(7);
	const int k = 0x19A7;
	const int d1 = ref.getValue(1, 999, "GameLogic.cpp", k);
	const int d2 = ref.getValue(1, 999, "GameLogic.cpp", k);
	const float r1 = ref.getValueReal(0.0f, 1.0f, "LuaScriptEngine.cpp", 0x74F);
	const int d3 = ref.getValue(1, 999, "GameLogic.cpp", k);
	const int d4 = ref.getValue(1, 999, "GameLogic.cpp", k);
	const float r2 = ref.getValueReal(0.0f, 1.0f, "LuaScriptEngine.cpp", 0x74F);
	const std::vector<GameLogicRandom::Call> &log = rng.callLog();
	REQUIRE(log.size() == 6);
	const bool realFlags[6] = { false, false, true, false, false, true };
	for (size_t i = 0; i < 6; ++i)
	{
		CHECK(log[i].real == realFlags[i]);
		if (!realFlags[i])
		{
			CHECK(log[i].lo == 1);
			CHECK(log[i].hi == 999);
			CHECK(log[i].line == k);
			CHECK(log[i].file == "GameLogic.cpp");
		}
	}
	CHECK(log[0].result == d1); // PlainObj
	CHECK(log[1].result == d2); // Hooked: its creation draw comes BEFORE the OnCreated handler's own draw
	CHECK(log[2].rresult == r1);
	CHECK(log[3].result == d3); // the second PlainObj (the tree between them drew none)
	CHECK(log[4].result == d4);
	CHECK(log[5].rresult == r2);
	CHECK(runtime.report().creationDraws == 4);
	bool s080 = false;
	for (const std::string &st : runtime.report().stops)
	{
		s080 = s080 || st.rfind("S-080:", 0) == 0;
	}
	CHECK(s080);
	// no scripts at all: the draws still happen
	GameLogicRandom rng2(RandomAlgorithm::ZH_CarryChain);
	rng2.seedRandom(7);
	rng2.enableCallLog(true);
	MapObjectRuntime plain(assets);
	plain.setLogicRandom(&rng2);
	plain.build(s.out);
	CHECK(rng2.callLog().size() == 4); // the same draws and no handler
	CHECK(plain.report().creationDraws == 4);
}

TEST_CASE("mapobj runtime: a hide of a matched draw module that shows no model here is reported S-124 (module-first: it still does not fall through to a sub object)")
{
	const char *objects =
		"Object Hider\n  Draw = W3DPropDraw ModuleTag_Empty\n  End\n  Draw = W3DPropDraw ModuleTag_Shown\n    ModelName = M1\n  End\n"
		"  Behavior = AIUpdateInterface ModuleTag_AI\n    AILuaEventsList = LH\n  End\n  KindOf = INFANTRY\nEnd\n";
	const char *xml =
		"<?xml version=\"1.0\"?>\n<SageLuaScriptSection>\n<Events>\n  <InternalEvent Name=\"OnCreated\" />\n</Events>\n"
		"<EventList Name=\"LH\">\n  <EventHandler EventName=\"OnCreated\" ScriptFunctionName=\"OnHide\"/>\n</EventList>\n</SageLuaScriptSection>\n";
	const char *lua = "function OnHide(self)\n\tObjectHideSubObjectPermanently(self, \"ModuleTag_Empty\", true)\nend\n";
	MapWorld w;
	REQUIRE_MESSAGE(w.load(objects).empty(), "template load");
	Scene s;
	std::string err;
	REQUIRE_MESSAGE(MapCreationHooks::scan(xml, lua, s.options.creationScripts, &err), err);
	s.map.chunks.objects = { object("Hider", 10, 10) };
	s.run(w);
	NoFiles files;
	WW3DAssetManager assets(files);
	MapObjectRuntime runtime(assets);
	runtime.setCreationScripts(&s.options.creationScripts);
	runtime.build(s.out);
	const MapRuntimeReport &r = runtime.report();
	CHECK(r.moduleRequests == 1);
	CHECK(r.moduleRequestsWithoutModel == 1);
	CHECK(r.modulesHidden == 0);
	CHECK(r.hideMisses.empty());
	bool reported = false;
	for (const std::string &st : r.stops)
	{
		reported = reported || (st.rfind("[S-124]", 0) == 0 && st.find("Drawable::showModule (RW 0x6789B4)") != std::string::npos);
	}
	CHECK(reported);
}

TEST_CASE("mapobj runtime: a duplicate EventList name in scriptevents.xml is the reported stop S-127 and reaches the map runtime report")
{
	const char *objects =
		"Object Dup\n  Draw = W3DPropDraw ModuleTag_01\n    ModelName = M1\n  End\n  Behavior = AIUpdateInterface ModuleTag_AI\n    AILuaEventsList = LD\n  End\n  KindOf = INFANTRY\nEnd\n";
	const char *xml =
		"<?xml version=\"1.0\"?>\n<SageLuaScriptSection>\n<Events>\n  <InternalEvent Name=\"OnCreated\" />\n</Events>\n"
		"<EventList Name=\"LD\">\n  <EventHandler EventName=\"OnCreated\" ScriptFunctionName=\"OnFirst\"/>\n</EventList>\n"
		"<EventList Name=\"LD\">\n  <EventHandler EventName=\"OnCreated\" ScriptFunctionName=\"OnSecond\"/>\n</EventList>\n</SageLuaScriptSection>\n";
	const char *lua = "function OnFirst(self)\n\tWhich = 'first'\nend\nfunction OnSecond(self)\n\tWhich = 'second'\nend\n";
	MapWorld w;
	REQUIRE_MESSAGE(w.load(objects).empty(), "template load");
	Scene s;
	std::string err;
	CHECK_FALSE(MapCreationHooks::scan(xml, lua, s.options.creationScripts, &err)); // the inventory scanner refuses the duplicate; the raw files still load
	REQUIRE(s.options.creationScripts.rawLoaded);
	s.map.chunks.objects = { object("Dup", 10, 10) };
	s.run(w);
	NoFiles files;
	WW3DAssetManager assets(files);
	MapObjectRuntime runtime(assets);
	runtime.setCreationScripts(&s.options.creationScripts);
	runtime.build(s.out);
	REQUIRE(runtime.scriptEngine());
	// the sink entry itself (stop, text naming the list) ...
	bool sink = false;
	for (const LuaReportSink::Entry &e : runtime.scriptEngine()->reports().entries())
	{
		sink = sink || (e.stop == "S-127" && e.text.find("LD") != std::string::npos);
	}
	CHECK(sink);
	// ... and the production report line
	bool propagated = false;
	for (const std::string &st : runtime.report().stops)
	{
		propagated = propagated || (st.rfind("[S-127]", 0) == 0 && st.find("LD") != std::string::npos);
	}
	CHECK(propagated);
}

TEST_CASE("mapobj runtime: a bridge / wall pass object after a normal object keeps drawable-list order (retail creates it first): the call-log sequence and the S-110 report are pinned")
{
	const char *objects =
		"Object Normal\n  Draw = W3DPropDraw ModuleTag_01\n    ModelName = M1\n  End\n  Behavior = AIUpdateInterface ModuleTag_AI\n    AILuaEventsList = LB\n  End\n  KindOf = INFANTRY\nEnd\n"
		"Object Span\n  Draw = W3DPropDraw ModuleTag_01\n    ModelName = M1\n  End\n  Behavior = AIUpdateInterface ModuleTag_AI\n    AILuaEventsList = LB\n  End\n  IsBridge = Yes\n  KindOf = BRIDGE\nEnd\n"
		"Object Wall\n  Draw = W3DPropDraw ModuleTag_01\n    ModelName = M1\n  End\n  KindOf = WALK_ON_TOP_OF_WALL\nEnd\n";
	const char *xml =
		"<?xml version=\"1.0\"?>\n<SageLuaScriptSection>\n<Events>\n  <InternalEvent Name=\"OnCreated\" />\n</Events>\n"
		"<EventList Name=\"LB\">\n  <EventHandler EventName=\"OnCreated\" ScriptFunctionName=\"OnRoll\"/>\n</EventList>\n</SageLuaScriptSection>\n";
	const char *lua = "function OnRoll(self)\n\tPicked = GetRandomNumber()\nend\n";
	MapWorld w;
	REQUIRE_MESSAGE(w.load(objects).empty(), "template load");
	Scene s;
	std::string err;
	REQUIRE_MESSAGE(MapCreationHooks::scan(xml, lua, s.options.creationScripts, &err), err);
	// list order: Normal, Span (bridge), Wall (walk on top of wall)
	s.map.chunks.objects = { object("Normal", 10, 10), object("Span", 20, 10), object("Wall", 30, 10) };
	s.run(w);
	NoFiles files;
	WW3DAssetManager assets(files);
	GameLogicRandom rng(RandomAlgorithm::ZH_CarryChain);
	rng.seedRandom(3);
	rng.enableCallLog(true);
	MapObjectRuntime runtime(assets);
	runtime.setLogicRandom(&rng);
	runtime.setCreationScripts(&s.options.creationScripts);
	runtime.build(s.out);
	// the CURRENT sequence: drawable-list order. creation draw + handler draw for Normal, then for Span, then Wall's creation draw
	const std::vector<GameLogicRandom::Call> &log = rng.callLog();
	REQUIRE(log.size() == 5);
	const bool real[5] = { false, true, false, true, false };
	for (size_t i = 0; i < 5; ++i)
	{
		CHECK(log[i].real == real[i]);
		if (!real[i])
		{
			CHECK(log[i].line == 0x19A7);
			CHECK(log[i].lo == 1);
			CHECK(log[i].hi == 999);
		}
	}
	const MapRuntimeReport &r = runtime.report();
	CHECK(r.creationDraws == 3);
	CHECK(r.creationBridgePassObjects == 2);
	CHECK(r.creationOrderDeviations == 2); // Span and Wall both come after the normal object
	bool reported = false;
	for (const std::string &st : r.stops)
	{
		reported = reported || st == "[S-110] 2 bridge / wall pass object(s) (IsBridge or KindOf WALK_ON_TOP_OF_WALL) follow a normal object in the drawable list: their creation draw "
			"GetGameLogicRandomValue(1, 999) (RW 0x628892) and OnCreated run in drawable-list order, retail creates them in the earlier bridge / wall pass, so every later logic draw of "
			"this map can differ from retail";
	}
	CHECK(reported);
	// bridge first (the retail order of the two kinds): no deviation, no report
	Scene s2;
	s2.map.chunks.objects = { object("Span", 20, 10), object("Normal", 10, 10) };
	s2.options.creationScripts = s.options.creationScripts;
	s2.run(w);
	MapObjectRuntime ordered(assets);
	ordered.setCreationScripts(&s2.options.creationScripts);
	ordered.build(s2.out);
	CHECK(ordered.report().creationOrderDeviations == 0);
	for (const std::string &st : ordered.report().stops)
	{
		CHECK(st.find("bridge / wall pass object(s)") == std::string::npos);
	}
}
