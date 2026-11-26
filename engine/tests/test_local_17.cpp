// OpenBFME unit tests: LOCAL-17 tree and shrub textures: W3DTreeDraw/TextureName.
//
// RotWK addresses:
//   W3DTreeBuffer::Add_Tree: RW 0x4EC93A (slot 21 in W3DTerrainVisual, RW 0x49184F -> RW 0x4678F3)
//     Type lookup loop: RW 0x4EC976 (ModelName at +0x8) and RW 0x4EC986 (TextureName at +0xC), stride 0x5C
//     Type creation: RW 0x4EC11E
//   W3DShrubBuffer::Add_Shrub: RW 0x4E8B8F (slot 26 in W3DTerrainVisual, RW 0x491986 -> RW 0x4679DB)
//     Type lookup loop: RW 0x4E8BDC (ModelName at +0x8) and RW 0x4E8BEE (TextureName at +0xC), stride 0x5C
//     Type creation: RW 0x4E80F5
//   Caller confirmation:
//     GameLogic::addTree (RW 0x683D89) and GameLogic::addShrub (RW 0x6808ED) call [edx + 0x40] on first draw module
//     returning W3DTreeDrawModuleData* passed as ebx (RW 0x4EC96A, RW 0x4E8BCE)
//   Texture loading:
//     RW 0x4EAE00..0x4EAF5F loads TextureName via WW3DAssetManager::Get_Texture (RW 0x532875), adds to atlas (RW 0x57082B).
//     Missing texture logs warning and renders with fallback UVs [0, 1] without crashing.

#include "doctest.h"

#include "ObjectTestUtil.h"

#include "Common/AsciiString.h"
#include "Common/Thing/ThingTemplate.h"
#include "GameClient/MapCreationHooks.h"
#include "GameClient/MapHordeSpawn.h"
#include "GameClient/MapObjectDrawables.h"
#include "GameClient/MapObjectRuntime.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DDrawModules.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DTreeDraw.h"
#include "GameLogic/Map/TerrainLogic.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"

#include <memory>
#include <string>
#include <vector>

namespace
{

struct MapWorld : objtest::World
{
	MapWorld()
	{
		W3DDrawModules::registerTypedDrawModuleData(modules);
		MapHordeSpawn::bindHordeContainData(modules);
	}
};

WorldHeightMap flatMap(int w, int h, int raw)
{
	WorldHeightMap m;
	m.m_width = w;
	m.m_height = h;
	m.m_borderSize = 0;
	m.m_dataSize = w * h;
	m.m_data.assign((size_t)w * h, static_cast<std::uint16_t>(raw));
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
		terrain = std::make_unique<TerrainLogic>();
		terrain->init(map.heightMap, map.chunks, nullptr);
		MapObjectCreation::build(map, "test", w.things, *terrain, options, out);
	}
};

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

TEST_CASE("LOCAL-17 tree types: distinct (ModelName, TextureName) create separate types in MapObjectRuntime (RW 0x4E8C1B, RW 0x4EC9B5)")
{
	MapWorld w;
	const std::string ini =
		"Object TreeTypeA\n"
		"  Draw = W3DTreeDraw ModuleTag_01\n"
		"    ModelName = PTTree01\n"
		"    TextureName = TreeA.tga\n"
		"  End\n"
		"  KindOf = SHRUBBERY TREE\n"
		"End\n"
		"Object TreeTypeB\n"
		"  Draw = W3DTreeDraw ModuleTag_01\n"
		"    ModelName = PTTree01\n"
		"    TextureName = TreeB.tga\n"
		"  End\n"
		"  KindOf = SHRUBBERY TREE\n"
		"End\n"
		"Object TreeTypeA_Dup\n"
		"  Draw = W3DTreeDraw ModuleTag_01\n"
		"    ModelName = PTTree01\n"
		"    TextureName = TreeA.tga\n"
		"  End\n"
		"  KindOf = SHRUBBERY TREE\n"
		"End\n"
		"Object TreeNoTex\n"
		"  Draw = W3DTreeDraw ModuleTag_01\n"
		"    ModelName = PTTree01\n"
		"  End\n"
		"  KindOf = SHRUBBERY TREE\n"
		"End\n"
		"Object ShrubTypeC\n"
		"  Draw = W3DTreeDraw ModuleTag_01\n"
		"    ModelName = PTShrub01\n"
		"    TextureName = ShrubC.tga\n"
		"  End\n"
		"  KindOf = SHRUBBERY SHRUB OPTIMIZED_PROP\n"
		"End\n";

	REQUIRE(w.load(ini).empty());

	Scene s;
	s.map.chunks.objects = {
		object("TreeTypeA", 10.0f, 10.0f),
		object("TreeTypeB", 20.0f, 10.0f),
		object("TreeTypeA_Dup", 30.0f, 10.0f),
		object("TreeNoTex", 40.0f, 10.0f),
		object("ShrubTypeC", 50.0f, 10.0f)
	};
	s.run(w);

	NoFiles files;
	WW3DAssetManager assets(files);
	MapObjectRuntime runtime(assets);
	runtime.build(s.out, false);

	const MapRuntimeReport &r = runtime.report();
	CHECK(r.treeDraws == 5);

	// In RotWK retail:
	// - TreeTypeA (pttree01, treea.tga) and TreeTypeA_Dup (pttree01, treea.tga) share one tree type entry
	// - TreeTypeB (pttree01, treeb.tga) forms a second distinct tree type entry despite sharing ModelName
	// - TreeNoTex (pttree01, "") forms a third type with empty texture name
	// - ShrubTypeC (ptshrub01, shrubc.tga) forms a fourth type
	CHECK(r.distinctTreeTypes.size() == 4u);
	CHECK(r.distinctTreeTypes.at({ "pttree01", "treea.tga" }) == 2u);
	CHECK(r.distinctTreeTypes.at({ "pttree01", "treeb.tga" }) == 1u);
	CHECK(r.distinctTreeTypes.at({ "pttree01", "" }) == 1u);
	CHECK(r.distinctTreeTypes.at({ "ptshrub01", "shrubc.tga" }) == 1u);

	// Placed models preserve textureName across instances for Godot device instantiation
	const std::vector<MapPlacedModel> &placed = runtime.models();
	REQUIRE(placed.size() == 5u);
	CHECK(placed[0].modelName == "PTTree01");
	CHECK(placed[0].textureName == "TreeA.tga");
	CHECK(placed[1].modelName == "PTTree01");
	CHECK(placed[1].textureName == "TreeB.tga");
	CHECK(placed[2].modelName == "PTTree01");
	CHECK(placed[2].textureName == "TreeA.tga");
	CHECK(placed[3].modelName == "PTTree01");
	CHECK(placed[3].textureName.empty());
	CHECK(placed[4].modelName == "PTShrub01");
	CHECK(placed[4].textureName == "ShrubC.tga");
}
