// OpenBFME unit tests. GPL-3.0.
//
// W3DQuadrupedDraw::getFootLocations (RotWK RW 0x4C9A39, ObjectDrawInterface slot 47 / +0xBC): the four foot bone positions of the
// huge four-legged beasts (Mumakil, MordorGreatBeast).
// All assets (skeleton, mesh, animation) are synthesized in memory via W3dSynth; no game or mod files are required.

#include "doctest.h"
#include "W3DDrawTestUtil.h"
#include "W3dSynth.h"

#include "Common/Thing/ThingTemplate.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DDrawModules.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DModelDraw.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DModelDrawVariants.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DQuadrupedDraw.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DScriptedModelDraw.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"
#include "Libraries/WWVegas/WWMath/matrix3d.h"

#include <cmath>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace drawtest;
using namespace w3dsynth;

namespace
{
class FixedRandom : public W3DDrawRandom
{
public:
	int value(int lo, int hi) override { return lo; }
	float real(float lo, float hi) override { return lo; }
};

	void addQuadrupedAssets(MemoryFileSource &fs, bool includeFrontLeft = true, bool includeFrontRight = true,
		bool includeRearLeft = true, bool includeRearRight = true, bool withWalkAnim = true)
	{
		std::vector<W3dPivotStruct> pivots;
		pivots.push_back(pivot("ROOTTRANSFORM", 0xFFFFFFFFu, 0.0f, 0.0f, 0.0f));
		if (includeFrontLeft)
		{
			pivots.push_back(pivot("e_FrontLeft", 0, 24.0f, 12.0f, 0.0f));
		}
		if (includeFrontRight)
		{
			pivots.push_back(pivot("e_FrontRight", 0, 24.0f, -12.0f, 0.0f));
		}
		if (includeRearLeft)
		{
			pivots.push_back(pivot("e_BackLeft", 0, -24.0f, 12.0f, 0.0f));
		}
		if (includeRearRight)
		{
			pivots.push_back(pivot("e_BackRight", 0, -24.0f, -12.0f, 0.0f));
		}
		fs.Add("art\\w3d\\mu\\mumak_skl.w3d", hierarchyChunk("MUMAK_SKL", pivots));

		// Alternate skeleton + HLOD model for USER_1 model condition (verifies worldSpace = true defaults to pendingFlags()).
		std::vector<W3dPivotStruct> altPivots = pivots;
		if (includeFrontLeft && altPivots.size() > 1)
		{
			altPivots[1] = pivot("e_FrontLeft", 0, 30.0f, 12.0f, 2.0f);
		}
		fs.Add("art\\w3d\\mu\\mumak_alt_skl.w3d", hierarchyChunk("MUMAK_ALT_SKL", altPivots));

		SynthMesh mesh;
		mesh.Name = "BODY";
		mesh.Container = "MUMAK_SKN";
		mesh.Vertices = { { 0.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f } };
		mesh.Triangles = { 0, 1, 2 };
		std::vector<std::uint8_t> bytes;
		append(bytes, meshChunk(mesh));
		append(bytes, hlodChunk("MUMAK_SKN", "MUMAK_SKL", { { "MUMAK_SKN.BODY", 0 } }));
		fs.Add("art\\w3d\\mu\\mumak_skn.w3d", bytes);

		SynthMesh altMesh = mesh;
		altMesh.Container = "MUMAK_ALT_SKN";
		std::vector<std::uint8_t> altBytes;
		append(altBytes, meshChunk(altMesh));
		append(altBytes, hlodChunk("MUMAK_ALT_SKN", "MUMAK_ALT_SKL", { { "MUMAK_ALT_SKN.BODY", 0 } }));
		fs.Add("art\\w3d\\mu\\mumak_alt_skn.w3d", altBytes);

		if (withWalkAnim && includeFrontLeft)
		{
			// Pivot 1 (e_FrontLeft) moves forward along +X by +6.0f at frame 3 (5 frames total so 100ms does not wrap), and lifts Z by +4.0f.
			std::vector<SynthChannel> channels;
			channels.push_back(SynthChannel{ ANIM_CHANNEL_X, 1, 0, 1, { 0.0f, 2.0f, 4.0f, 6.0f, 8.0f } });
			channels.push_back(SynthChannel{ ANIM_CHANNEL_Z, 1, 0, 1, { 0.0f, 1.5f, 3.0f, 4.0f, 4.0f } });
			fs.Add("art\\w3d\\mu\\mumak_wlka.w3d", rawAnimChunk("MUMAK_WLKA", "MUMAK_SKL", 5, 30, channels));
		}
	}

	std::unique_ptr<W3DQuadrupedDrawModuleData> parseQuadrupedBody(const std::string &body)
	{
		INIEnvironment env;
		std::unique_ptr<W3DQuadrupedDrawModuleData> out(new W3DQuadrupedDrawModuleData());
		env.blocks.registerBlock("TestQuadruped", [&](INI *ini) {
			ini->initFromINIMultiProc(out.get(), W3DQuadrupedDrawModuleData::buildFieldParse);
			while (!ini->isEOF())
			{
				ini->readLine();
			}
		});
		const std::string full = "TestQuadruped\n" + body;
		INI ini(env);
		ini.loadMemory("quad.ini", std::vector<std::uint8_t>(full.begin(), full.end()), INI_LOAD_OVERWRITE);
		return out;
	}

Matrix3D makeYawTransform(const Coord3D &pos, float angle, float scale = 1.0f)
{
	Matrix3D m;
	const float c = std::cos(angle);
	const float s = std::sin(angle);
	m.Row[0][0] = c * scale;
	m.Row[0][1] = -s * scale;
	m.Row[0][2] = 0.0f;
	m.Row[0][3] = pos.x;
	m.Row[1][0] = s * scale;
	m.Row[1][1] = c * scale;
	m.Row[1][2] = 0.0f;
	m.Row[1][3] = pos.y;
	m.Row[2][0] = 0.0f;
	m.Row[2][1] = 0.0f;
	m.Row[2][2] = scale;
	m.Row[2][3] = pos.z;
	return m;
}
} // namespace

TEST_CASE("W3DQuadrupedDraw::getFootLocations matches RotWK RW 0x4C9A39 / 0x4B2F19")
{
	MemoryFileSource fs;
	addQuadrupedAssets(fs, true, true, true, true, true);
	WW3DAssetManager assets(fs);
	WW3DDrawAssets drawAssets(assets);
	FixedRandom random;
	Harness harness;

	const std::string body =
		"DefaultModelConditionState\n"
		"  Model = MUMAK_SKN\n"
		"  Skeleton = MUMAK_SKL\n"
		"End\n"
		"ModelConditionState = USER_1\n"
		"  Model = MUMAK_ALT_SKN\n"
		"  Skeleton = MUMAK_ALT_SKL\n"
		"End\n"
		"ExtraPublicBone = e_FrontLeft\n"
		"ExtraPublicBone = e_FrontRight\n"
		"ExtraPublicBone = e_BackLeft\n"
		"ExtraPublicBone = e_BackRight\n"
		"IdleAnimationState\n"
		"  StateName = Idle\n"
		"End\n"
		"AnimationState = MOVING\n"
		"  Animation = Walk\n"
		"    AnimationName = MUMAK_WLKA\n"
		"    AnimationMode = LOOP\n"
		"  End\n"
		"End\n"
		"LeftFrontFootBone = E_FRONTLEFT\n" // case-insensitive HTree lookup
		"RightFrontFootBone = e_FrontRight\n"
		"LeftRearFootBone = e_BackLeft\n"
		"RightRearFootBone = e_BackRight\n"
		"End\n";

	std::unique_ptr<W3DQuadrupedDrawModuleData> quadData = parseQuadrupedBody(body);
	REQUIRE(quadData != nullptr);

	W3DScriptedModelDraw::Options opt;
	opt.buildBones = false;

	// Base W3DScriptedModelDraw returns false at ObjectDrawInterface slot 47 (RW 0x8F8014).
	W3DScriptedModelDraw baseDraw(*quadData, drawAssets, random, nullptr, opt);
	std::vector<Coord3D> feet;
	const Matrix3D renderMtx = makeYawTransform(Coord3D{ 100.0f, 50.0f, 10.0f }, 0.0f);
	CHECK_FALSE(baseDraw.getFootLocations(feet, false, renderMtx));

	// W3DQuadrupedDraw overrides getFootLocations (RW 0x4C9A39).
	W3DQuadrupedDraw quadDraw(*quadData, drawAssets, random, nullptr, opt);
	REQUIRE(quadDraw.getFootLocations(feet, false, renderMtx));
	REQUIRE(feet.size() == 4);

	CHECK(feet[0].x == 124.0f);
	CHECK(feet[0].y == 62.0f);
	CHECK(feet[0].z == 10.0f);
	CHECK(feet[1].x == 124.0f);
	CHECK(feet[1].y == 38.0f);
	CHECK(feet[1].z == 10.0f);
	CHECK(feet[2].x == 76.0f);
	CHECK(feet[2].y == 62.0f);
	CHECK(feet[2].z == 10.0f);
	CHECK(feet[3].x == 76.0f);
	CHECK(feet[3].y == 38.0f);
	CHECK(feet[3].z == 10.0f);

	// worldSpace = true requires non-null objectTransform (RW 0x4C9AA1..0x4C9AC4) and uses pristine pose.
	std::vector<Coord3D> worldFeet;
	CHECK_FALSE(quadDraw.getFootLocations(worldFeet, true, renderMtx, nullptr));
	REQUIRE(quadDraw.getFootLocations(worldFeet, true, Matrix3D(), &renderMtx));
	REQUIRE(worldFeet.size() == 4);
	CHECK(worldFeet[0].x == 124.0f);
	CHECK(worldFeet[0].y == 62.0f);
	CHECK(worldFeet[0].z == 10.0f);

	// Animated client pose (worldSpace = false): MOVING animation advances e_FrontLeft by +6 X, +4 Z at frame 3.
	quadDraw.setModelConditionFlags(flagsOf({ "MOVING" }));
	quadDraw.advance(100.0); // 3 frames at 30 fps
	REQUIRE(quadDraw.getFootLocations(feet, false, renderMtx));
	// tolerance: frame interpolation float rounding
	CHECK(feet[0].x == doctest::Approx(130.0f));
	CHECK(feet[0].y == 62.0f);
	// tolerance: frame interpolation float rounding
	CHECK(feet[0].z == doctest::Approx(14.0f));

	// worldSpace = true with conditionFlags == nullptr uses m_pendingFlags (RW +0x17C, getModelConditionFlags).
	quadDraw.setModelConditionFlags(flagsOf({ "USER_1" }));
	REQUIRE(quadDraw.getFootLocations(worldFeet, true, Matrix3D(), &renderMtx));
	CHECK(worldFeet[0].x == 130.0f);
	CHECK(worldFeet[0].y == 62.0f);
	CHECK(worldFeet[0].z == 12.0f);
	quadDraw.setModelConditionFlags(ModelConditionFlags());

	// RW 0x4B2F19 treats RootTransform / index 0 as missing bone.
	Matrix3D dummy;
	CHECK_FALSE(quadDraw.getCurrentWorldspaceClientBonePositions("ROOTTRANSFORM", renderMtx, dummy));
}

TEST_CASE("missing foot bone rules and sibling copy match RW 0x4C9A51..0x4C9E0E")
{
	// Missing RightFrontFootBone name -> copies LeftFrontFootBone.
	{
		MemoryFileSource fs;
		addQuadrupedAssets(fs, true, true, true, true, false);
		WW3DAssetManager assets(fs);
		WW3DDrawAssets drawAssets(assets);
		FixedRandom random;
		Harness harness;

		const std::string body =
			"DefaultModelConditionState\n  Model = MUMAK_SKN\n  Skeleton = MUMAK_SKL\nEnd\n"
			"IdleAnimationState\n  StateName = Idle\nEnd\n"
			"LeftFrontFootBone = e_FrontLeft\n"
			"LeftRearFootBone = e_BackLeft\n"
			"End\n";
		std::unique_ptr<W3DQuadrupedDrawModuleData> parsed = parseQuadrupedBody(body);
		REQUIRE(parsed != nullptr);
		W3DQuadrupedDraw draw(*parsed, drawAssets, random);
		std::vector<Coord3D> feet;
		REQUIRE(draw.getFootLocations(feet, false, makeYawTransform(Coord3D{ 0.0f, 0.0f, 0.0f }, 0.0f)));
		REQUIRE(feet.size() == 4);
		CHECK(feet[0].x == 24.0f);
		CHECK(feet[0].y == 12.0f);
		CHECK(feet[1].x == feet[0].x);
		CHECK(feet[1].y == feet[0].y);
		CHECK(feet[2].x == -24.0f);
		CHECK(feet[2].y == 12.0f);
		CHECK(feet[3].x == feet[2].x);
		CHECK(feet[3].y == feet[2].y);
	}

	// Both rear bones missing from HTree -> getFootLocations returns false even when front bones resolve.
	{
		MemoryFileSource fs;
		addQuadrupedAssets(fs, true, true, false, false, false);
		WW3DAssetManager assets(fs);
		WW3DDrawAssets drawAssets(assets);
		FixedRandom random;
		Harness harness;

		const std::string body =
			"DefaultModelConditionState\n  Model = MUMAK_SKN\n  Skeleton = MUMAK_SKL\nEnd\n"
			"IdleAnimationState\n  StateName = Idle\nEnd\n"
			"LeftFrontFootBone = e_FrontLeft\n"
			"RightFrontFootBone = e_FrontRight\n"
			"LeftRearFootBone = e_BackLeft\n"
			"RightRearFootBone = e_BackRight\n"
			"End\n";
		std::unique_ptr<W3DQuadrupedDrawModuleData> parsed = parseQuadrupedBody(body);
		REQUIRE(parsed != nullptr);
		W3DQuadrupedDraw draw(*parsed, drawAssets, random);
		std::vector<Coord3D> feet;
		CHECK_FALSE(draw.getFootLocations(feet, false, Matrix3D()));
	}
}
