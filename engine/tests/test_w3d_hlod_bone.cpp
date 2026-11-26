// An HLOD's LOD model on a bone its hierarchy does not have goes on bone 0, as retail's HLodClass::Add_Lod_Model does
// (RW 0x59E9E0, the clamp at 0x59E9F2; BFME2 0x59F2B0 the same). Edain 4.8.5.4's dwarven barracks (dbkasern_skn) names bones up to
// 48 of DBKASERN_SKL's 35; the port dropped those 14 parts and the building was drawn almost empty.

#include "doctest.h"
#include "RetailTestMount.h"
#include "W3dSynth.h"

#include "Common/ArchiveFile.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DModelDrawBones.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"
#include "Libraries/WWVegas/WW3D2/htree.h"

#include <algorithm>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

using namespace w3dsynth;

namespace
{
SynthMesh quad()
{
	SynthMesh m;
	m.Vertices = { { 0, 0, 0 }, { 1, 0, 0 }, { 1, 1, 0 }, { 0, 1, 0 } };
	m.Triangles = { 0, 1, 2, 0, 2, 3 };
	return m;
}

void buildBarracks(MemoryFileSource &fs)
{
	std::vector<std::uint8_t> skn;
	for (const char *part : { "WALL", "ROOF", "FLAG" })
	{
		SynthMesh m = quad();
		m.Container = "BLD_SKN";
		m.Name = part;
		append(skn, meshChunk(m));
	}
	// WALL on bone 7 of a three-pivot hierarchy (re-exported skeleton), ROOF on HAND (2); the aggregate FLAG on bone 9
	append(skn, hlodChunk("BLD_SKN", "BLD_SKL", { { "BLD_SKN.WALL", 7 }, { "BLD_SKN.ROOF", 2 } }, { { "BLD_SKN.FLAG", 9 } }));
	fs.Add("art\\w3d\\bl\\bld_skn.w3d", skn);
	fs.Add("art\\w3d\\bl\\bld_skl.w3d",
		hierarchyChunk("BLD_SKL", { pivot("ROOTTRANSFORM", 0xFFFFFFFFu, 0, 0, 0), pivot("HIP", 0, 0, 0, 1), pivot("HAND", 1, 0, 2, 0) }));
}
} // namespace

TEST_CASE("hlod bone: a LOD model on a bone past its hierarchy is drawn on bone 0 (RW 0x59E9F2), an aggregate stays reported")
{
	MemoryFileSource fs;
	buildBarracks(fs);
	WW3DAssetManager am(fs);
	std::string error;
	const RenderObjPrototype *proto = am.Create_Render_Obj("BLD_SKN", &error);
	REQUIRE_MESSAGE(proto, error);
	REQUIRE(proto->Tree);
	CHECK(proto->Tree->Num_Pivots() == 3);
	REQUIRE(proto->SubObjects.size() == 3);

	const RenderSubObject &wall = proto->SubObjects[0];
	CHECK(wall.Type == RenderSubObject::SUB_MESH);
	CHECK(wall.BoneIndex == 0);
	CHECK(wall.BoneOutsideSkeleton == 7);
	CHECK(wall.Reason.empty());

	const RenderSubObject &roof = proto->SubObjects[1];
	CHECK(roof.Type == RenderSubObject::SUB_MESH);
	CHECK(roof.BoneIndex == 2);
	CHECK(roof.BoneOutsideSkeleton == -1);

	// aggregates are attached by Add_Sub_Object_To_Bone, not Add_Lod_Model: unchanged, unresolved and reported
	const RenderSubObject &flag = proto->SubObjects[2];
	CHECK(flag.Aggregate);
	CHECK(flag.Type == RenderSubObject::SUB_UNRESOLVED);
	CHECK(flag.BoneIndex == 9);
	CHECK(flag.BoneOutsideSkeleton == -1);
	CHECK(flag.Reason.find("outside the hierarchy") != std::string::npos);

	// WALL is on the root now, so it is not under ROOF's bone (the hide walk, RW 0x4B2964, sees the clamped bone)
	CHECK(W3DSubObjectsUnderSubObject(*proto, 1) == std::vector<int>{ 1 });
}

// OPENBFME_HLOD_MOD_W3D: a mod's W3D archive with the re-exported skeleton, for example <Edain 4.8.5.4>\__edain_w3d.big. Unset: skipped.
TEST_CASE("hlod bone data: Edain's dwarven barracks keeps all 25 parts, 14 of them on bone 0")
{
	const char *path = std::getenv("OPENBFME_HLOD_MOD_W3D");
	if (!path || !*path)
	{
		MESSAGE("skipped: OPENBFME_HLOD_MOD_W3D unset");
		return;
	}
	Win32BIGFileSystem big;
	std::string error;
	std::unique_ptr<ArchiveFile> archive = big.openArchiveFile(path, &error);
	REQUIRE_MESSAGE(archive, path << ": " << error);
	MemoryFileSource fs;
	for (const char *name : { "art\\w3d\\db\\dbkasern_skn.w3d", "art\\w3d\\db\\dbkasern_skl.w3d" })
	{
		std::vector<std::uint8_t> bytes;
		REQUIRE_MESSAGE(archive->readFile(name, bytes, &error), name << ": " << error);
		fs.Add(name, bytes);
	}
	WW3DAssetManager am(fs);
	const RenderObjPrototype *proto = am.Create_Render_Obj("dbkasern_skn", &error);
	REQUIRE_MESSAGE(proto, error);
	REQUIRE(proto->Tree);
	CHECK_FALSE(proto->HierarchyMissing);
	CHECK(proto->Tree->Num_Pivots() == 35);
	REQUIRE(proto->SubObjects.size() == 25);
	int onRoot = 0, highest = -1;
	for (const RenderSubObject &sub : proto->SubObjects)
	{
		INFO("sub object ", sub.Name, " bone ", sub.BoneIndex);
		// tolerance: the 11 parts that fit keep the file's own bones (several per bone); only the range is the claim here
		CHECK((sub.BoneIndex >= 0 && sub.BoneIndex < 35));
		CHECK(sub.Reason.find("outside the hierarchy") == std::string::npos);
		if (sub.BoneOutsideSkeleton >= 0)
		{
			++onRoot;
			CHECK(sub.BoneIndex == 0);
			CHECK(sub.Type == RenderSubObject::SUB_MESH);
			highest = std::max(highest, sub.BoneOutsideSkeleton);
		}
	}
	CHECK(onRoot == 14);
	CHECK(highest == 48);
}

// Retail has such HLODs too (the archives in RotWK's order, the top LOD): 12 in RotWK 2.01, 10 in BFME2 1.06, among them the Hobbit
// porter and the destroyed Rohan forge. Their parts past the hierarchy were dropped as well.
TEST_CASE("hlod bone retail: RotWK's porter and destroyed forge keep their parts past the hierarchy, on bone 0")
{
	retailtest::Mount *mount = retailtest::pureMount();
	if (!mount)
	{
		retailtest::printSkip("hlod bone retail");
		return;
	}
	REQUIRE_MESSAGE(mount->fs, mount->error);
	ArchiveW3DFileSource source(*mount->fs);
	WW3DAssetManager am(source);
	struct Expect
	{
		const char *name;
		int pivots, parts, onRoot, highest;
	};
	for (const Expect &e : { Expect{ "RUPORTER_SKN", 24, 16, 7, 30 }, Expect{ "KBFORGE_D3L", 27, 4, 4, 27 } })
	{
		INFO(e.name);
		std::string error;
		const RenderObjPrototype *proto = am.Create_Render_Obj(e.name, &error);
		REQUIRE_MESSAGE(proto, error);
		REQUIRE(proto->Tree);
		CHECK(proto->Tree->Num_Pivots() == e.pivots);
		REQUIRE(proto->SubObjects.size() == (size_t)e.parts);
		int onRoot = 0, highest = -1;
		for (const RenderSubObject &sub : proto->SubObjects)
		{
			if (sub.BoneOutsideSkeleton >= 0)
			{
				++onRoot;
				CHECK(sub.BoneIndex == 0);
				highest = std::max(highest, sub.BoneOutsideSkeleton);
			}
			CHECK(sub.Reason.find("outside the hierarchy") == std::string::npos);
		}
		CHECK(onRoot == e.onRoot);
		CHECK(highest == e.highest);
	}
}
