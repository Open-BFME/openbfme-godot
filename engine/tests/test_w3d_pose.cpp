// OpenBFME unit tests: HTreeClass::Anim_Update / Blend_Update on synthetic hierarchies and animations. GPL-3.0.
// Expected world positions are worked out by hand from the rules of ZH htree.cpp (Translate, then postMul of the
// rotation) and the BFME2 retail Blend_Update (nlerp rotation blend, (b - a) * pct + a translation blend, fade lerp).

#include "doctest.h"

#include "Common/NumericState.h"

#include <cstdint>
#include <cstring>
#include <vector>
#include "W3dTestUtil.h"
#include "W3dSynth.h"

#include "Libraries/WWVegas/WW3D2/hanim.h"
#include "Libraries/WWVegas/WW3D2/hrawanim.h"
#include "Libraries/WWVegas/WW3D2/htree.h"
#include "Libraries/WWVegas/WW3D2/w3d_file.h"
#include "Libraries/WWVegas/WWLib/chunkio.h"

#include <cmath>
#include <functional>
#include <string>

using namespace w3dtest;

namespace
{

W3dPivotStruct makePivot(const char *name, std::uint32_t parent, float x, float y, float z)
{
	W3dPivotStruct p = {};
	setName(p.Name, W3D_NAME_LEN, name);
	p.ParentIdx = parent;
	p.Translation = { x, y, z };
	p.Rotation.Q[3] = 1.0f;
	return p;
}

HTreeClass makeTree(const std::vector<W3dPivotStruct> &pivots)
{
	ChunkWriter hier;
	W3dHierarchyStruct hh = {};
	hh.Version = W3D_MAKE_VERSION(4, 1);
	setName(hh.Name, sizeof(hh.Name), "T_SKL");
	hh.NumPivots = (std::uint32_t)pivots.size();
	hier.chunk(W3D_CHUNK_HIERARCHY_HEADER, ChunkWriter::of(hh));
	hier.chunk(W3D_CHUNK_PIVOTS, ChunkWriter::ofArray(pivots));
	ChunkWriter file;
	file.wrapper(W3D_CHUNK_HIERARCHY, hier);
	ChunkLoadClass cload(file.bytes.data(), file.bytes.size());
	REQUIRE(cload.Open_Chunk());
	HTreeClass tree;
	std::string error;
	REQUIRE_MESSAGE(tree.Load_W3D(cload, &error) == HTreeClass::OK, error);
	return tree;
}

// An animation whose getters are lambdas, so each test states its channels directly.
class FakeAnim : public HAnimClass
{
public:
	int Pivots = 0;
	std::function<Vector3(int, float)> Trans = [](int, float) { return Vector3(); };
	std::function<bool(Quaternion &, int, float)> Orient = [](Quaternion &, int, float) { return false; };
	std::function<bool(int, float)> Vis = [](int, float) { return true; };
	std::function<float(int, float)> Fade = [](int, float) { return 1.0f; };

	const std::string &Get_Name() const override { static const std::string n = "FAKE.ANIM"; return n; }
	const std::string &Get_HName() const override { static const std::string n = "FAKE"; return n; }
	int Get_Num_Frames() const override { return 10; }
	float Get_Frame_Rate() const override { return 30.0f; }
	int Get_Num_Pivots() const override { return Pivots; }
	void Get_Translation(Vector3 &t, int p, float f) const override { t = Trans(p, f); }
	bool Get_Orientation(Quaternion &q, int p, float f) const override
	{
		q.Make_Identity();
		return Orient(q, p, f);
	}
	bool Get_Visibility(int p, float f) const override { return Vis(p, f); }
	float Get_Fade(int p, float f) const override { return Fade(p, f); }
	bool Is_Node_Motion_Present(int) const override { return true; }
	bool Frame_Is_Defined(int, float) const override { return true; }
};

const float kSqrtHalf = 0.70710678118654752f;

// Root, A (child of root, base (1,0,0)), B (child of A, base (0,2,0)).
HTreeClass chainTree()
{
	return makeTree({ makePivot("ROOTTRANSFORM", 0xFFFFFFFFu, 0, 0, 0), makePivot("A", 0, 1, 0, 0), makePivot("B", 1, 0, 2, 0) });
}

} // namespace

TEST_CASE("Anim_Update: translation then rotation of A moves its child B (hand-computed)")
{
	HTreeClass tree = chainTree();
	FakeAnim anim;
	anim.Pivots = 3;
	// A is lifted 3 along Z, then rotated 90 degrees about Z (q = (0, 0, sin 45, cos 45)).
	anim.Trans = [](int p, float) { return p == 1 ? Vector3(0, 0, 3) : Vector3(); };
	anim.Orient = [](Quaternion &q, int p, float) {
		if (p != 1) return false;
		q.Set(0, 0, kSqrtHalf, kSqrtHalf);
		return true;
	};
	HTreePose pose;
	tree.Anim_Pose(Matrix3D(), &anim, 0.0f, pose);
	// A world = Translate(1,0,0) * Translate(0,0,3) * Rz(90): origin (1,0,3).
	Vector3 a = pose.Transform[1].Get_Translation();
	CHECK(a.X == doctest::Approx(1.0f));
	CHECK(a.Y == doctest::Approx(0.0f));
	CHECK(a.Z == doctest::Approx(3.0f));
	// B sits (0,2,0) in A's frame; Rz(90) maps (0,2,0) to (-2,0,0): world (-1, 0, 3).
	Vector3 b = pose.Transform[2].Get_Translation();
	CHECK(b.X == doctest::Approx(-1.0f).epsilon(1e-5));
	CHECK(b.Y == doctest::Approx(0.0f).epsilon(1e-5));
	CHECK(b.Z == doctest::Approx(3.0f));
	// The tree itself was not touched by the const evaluator.
	CHECK(tree.Get_Transform(2).Get_Translation().Y == doctest::Approx(0.0f)); // never evaluated: identity
}

TEST_CASE("Anim_Update: a pivot beyond the animation's pivot count keeps the base pose")
{
	HTreeClass tree = chainTree();
	FakeAnim anim;
	anim.Pivots = 2; // knows root and A only
	anim.Trans = [](int, float) { return Vector3(10, 10, 10); };
	HTreePose pose;
	tree.Anim_Pose(Matrix3D(), &anim, 0.0f, pose);
	// A moved by (10,10,10) from base (1,0,0); B has no motion so it is A + base (0,2,0).
	Vector3 b = pose.Transform[2].Get_Translation();
	CHECK(b.X == doctest::Approx(11.0f));
	CHECK(b.Y == doctest::Approx(12.0f));
	CHECK(b.Z == doctest::Approx(10.0f));
	CHECK(pose.IsVisible[2] == 1);
	CHECK(pose.PivotFade[2] == 1.0f);
}

TEST_CASE("Anim_Update: a pivot with no rotation channel is not rotated; visibility and fade are copied per pivot")
{
	HTreeClass tree = chainTree();
	FakeAnim anim;
	anim.Pivots = 3;
	anim.Vis = [](int p, float) { return p != 2; };
	anim.Fade = [](int p, float f) { return p == 1 ? 0.25f : (p == 2 ? f : 1.0f); };
	HTreePose pose;
	tree.Anim_Pose(Matrix3D(), &anim, 0.5f, pose);
	CHECK(pose.IsVisible[0] == 1);
	CHECK(pose.IsVisible[1] == 1);
	CHECK(pose.IsVisible[2] == 0);
	CHECK(pose.PivotFade[0] == 1.0f);
	CHECK(pose.PivotFade[1] == 0.25f);
	CHECK(pose.PivotFade[2] == 0.5f);
	// No rotation anywhere: B = (1, 2, 0).
	Vector3 b = pose.Transform[2].Get_Translation();
	CHECK(b.X == doctest::Approx(1.0f));
	CHECK(b.Y == doctest::Approx(2.0f));
}

TEST_CASE("Anim_Update: ScaleFactor scales the animated translation only")
{
	HTreeClass tree = chainTree();
	tree.Set_Scale_Factor(2.0f);
	FakeAnim anim;
	anim.Pivots = 3;
	anim.Trans = [](int p, float) { return p == 1 ? Vector3(0, 0, 3) : Vector3(); };
	HTreePose pose;
	tree.Anim_Pose(Matrix3D(), &anim, 0.0f, pose);
	Vector3 a = pose.Transform[1].Get_Translation();
	CHECK(a.X == doctest::Approx(1.0f)); // base translation is not scaled
	CHECK(a.Z == doctest::Approx(6.0f));
}

TEST_CASE("Anim_Update in place writes the same values as Anim_Pose and the root transform is applied")
{
	HTreeClass tree = chainTree();
	FakeAnim anim;
	anim.Pivots = 3;
	anim.Trans = [](int p, float) { return p == 1 ? Vector3(0, 0, 3) : Vector3(); };
	anim.Fade = [](int, float) { return 0.5f; };
	Matrix3D root;
	root.Set_Translation(Vector3(100, 0, 0));
	HTreePose pose;
	tree.Anim_Pose(root, &anim, 0.0f, pose);
	tree.Anim_Update(root, &anim, 0.0f);
	for (int i = 0; i < 3; ++i)
	{
		Vector3 e = pose.Transform[i].Get_Translation();
		Vector3 g = tree.Get_Transform(i).Get_Translation();
		CHECK(g.X == e.X);
		CHECK(g.Y == e.Y);
		CHECK(g.Z == e.Z);
	}
	CHECK(tree.Get_Transform(1).Get_Translation().X == doctest::Approx(101.0f));
	CHECK(tree.Get_Pivot(1).PivotFade == 0.5f);
	CHECK(tree.Get_Pivot(0).PivotFade == 1.0f);
}

TEST_CASE("Anim_Update: an animation that cannot define the frame throws UndefinedFrameError instead of guessing")
{
	HTreeClass tree = chainTree();
	FakeAnim anim;
	anim.Pivots = 3;
	anim.Trans = [](int p, float) -> Vector3 {
		if (p == 2) throw UndefinedFrameError("frame precedes the first key");
		return Vector3();
	};
	HTreePose pose;
	CHECK_THROWS_AS(tree.Anim_Pose(Matrix3D(), &anim, -1.0f, pose), UndefinedFrameError);
}

TEST_CASE("Blend_Update: translation lerps as (b - a) * pct + a, rotations nlerp, fade lerps, visibility is an OR")
{
	HTreeClass tree = chainTree();
	FakeAnim m0, m1;
	m0.Pivots = 3;
	m1.Pivots = 3;
	m0.Trans = [](int p, float) { return p == 1 ? Vector3(0, 0, 0) : Vector3(); };
	m1.Trans = [](int p, float) { return p == 1 ? Vector3(0, 0, 10) : Vector3(); };
	// Only m1 rotates pivot 1 (90 degrees about Z); m0 has no rotation there and so contributes the identity.
	m1.Orient = [](Quaternion &q, int p, float) {
		if (p != 1) return false;
		q.Set(0, 0, kSqrtHalf, kSqrtHalf);
		return true;
	};
	m0.Fade = [](int, float) { return 1.0f; };
	m1.Fade = [](int p, float) { return p == 1 ? 0.0f : 1.0f; };
	m0.Vis = [](int p, float) { return p != 2; };
	m1.Vis = [](int p, float) { return p == 0 || p == 1; };

	const float pct = 0.25f;
	HTreePose pose;
	tree.Blend_Pose(Matrix3D(), &m0, 0.0f, &m1, 0.0f, pct, pose);

	// Translation of A: (0,0,0) + 0.25 * (0,0,10) = 2.5 along Z, added to the base (1,0,0).
	Vector3 a = pose.Transform[1].Get_Translation();
	CHECK(a.X == doctest::Approx(1.0f));
	CHECK(a.Z == doctest::Approx(2.5f));

	// Rotation of A: normalise(0.75 * (0,0,0,1) + 0.25 * (0,0,s,s)) with s = sqrt(1/2): angle = 2 * atan2(0.25 s, 0.75 + 0.25 s).
	const double s = 0.70710678118654752;
	const double angle = 2.0 * std::atan2(0.25 * s, 0.75 + 0.25 * s);
	// B is (0,2,0) in A's frame: world = A + Rz(angle) * (0,2,0) = (1 - 2 sin angle, 2 cos angle, 2.5).
	Vector3 b = pose.Transform[2].Get_Translation();
	CHECK(b.X == doctest::Approx((float)(1.0 - 2.0 * std::sin(angle))).epsilon(1e-5));
	CHECK(b.Y == doctest::Approx((float)(2.0 * std::cos(angle))).epsilon(1e-5));
	CHECK(b.Z == doctest::Approx(2.5f));

	// Fade: (0 - 1) * 0.25 + 1 = 0.75 on pivot 1, 1.0 elsewhere.
	CHECK(pose.PivotFade[1] == doctest::Approx(0.75f));
	CHECK(pose.PivotFade[2] == doctest::Approx(1.0f));
	// Visibility: pivot 2 is visible in neither animation; pivot 1 in m1 only (OR) -> visible.
	CHECK(pose.IsVisible[1] == 1);
	CHECK(pose.IsVisible[2] == 0);
}

TEST_CASE("Blend_Update at percentage 0 and 1 reproduces the single animations")
{
	HTreeClass tree = chainTree();
	FakeAnim m0, m1;
	m0.Pivots = m1.Pivots = 3;
	m0.Trans = [](int p, float) { return p == 1 ? Vector3(0, 5, 0) : Vector3(); };
	m1.Trans = [](int p, float) { return p == 1 ? Vector3(0, 0, 10) : Vector3(); };
	m0.Orient = m1.Orient = [](Quaternion &q, int p, float) {
		if (p != 1) return false;
		q.Set(0, 0, kSqrtHalf, kSqrtHalf);
		return true;
	};
	HTreePose blend, single;
	tree.Blend_Pose(Matrix3D(), &m0, 0.0f, &m1, 0.0f, 0.0f, blend);
	tree.Anim_Pose(Matrix3D(), &m0, 0.0f, single);
	for (int i = 0; i < 3; ++i)
	{
		Vector3 x = blend.Transform[i].Get_Translation();
		Vector3 y = single.Transform[i].Get_Translation();
		CHECK(x.X == doctest::Approx(y.X).epsilon(1e-5));
		CHECK(x.Y == doctest::Approx(y.Y).epsilon(1e-5));
		CHECK(x.Z == doctest::Approx(y.Z).epsilon(1e-5));
	}
	tree.Blend_Pose(Matrix3D(), &m0, 0.0f, &m1, 0.0f, 1.0f, blend);
	tree.Anim_Pose(Matrix3D(), &m1, 0.0f, single);
	for (int i = 0; i < 3; ++i)
	{
		Vector3 x = blend.Transform[i].Get_Translation();
		Vector3 y = single.Transform[i].Get_Translation();
		CHECK(x.X == doctest::Approx(y.X).epsilon(1e-5));
		CHECK(x.Y == doctest::Approx(y.Y).epsilon(1e-5));
		CHECK(x.Z == doctest::Approx(y.Z).epsilon(1e-5));
	}
}

// ---- raw animations: the BFME2 raw-only Anim_Update (FUN_00563a80) ------------------------------------------------------
namespace
{
HRawAnimClass loadRaw(const std::vector<std::uint8_t> &bytes, int numNodes)
{
	ChunkLoadClass cload(bytes.data(), bytes.size());
	REQUIRE(cload.Open_Chunk());
	HRawAnimClass anim;
	std::string error;
	REQUIRE_MESSAGE(anim.Load_W3D(cload, numNodes, &error) == HRawAnimClass::OK, error);
	return anim;
}
} // namespace

TEST_CASE("S-028: raw and blended poses report ExactOperationOrder = false; the base, motion-channel and classic arms report true")
{
	HTreeClass tree = chainTree();
	HTreePose pose;

	tree.Base_Pose(Matrix3D(), pose);
	CHECK(pose.ExactOperationOrder);
	CHECK(pose.Arm == HTREE_POSE_ARM_BASE);

	FakeAnim fake;
	fake.Pivots = 3;
	tree.Anim_Pose(Matrix3D(), &fake, 0.0f, pose); // a plain animation class takes the classic arm: transcribed exactly
	CHECK(pose.ExactOperationOrder);
	CHECK(pose.Arm == HTREE_POSE_ARM_CLASSIC);
	tree.Anim_Pose(Matrix3D(), &fake, 0.0f, pose, HTREE_POSE_ARM_MOTION_CHANNEL);
	CHECK(pose.ExactOperationOrder);
	CHECK(pose.Arm == HTREE_POSE_ARM_MOTION_CHANNEL);

	// raw animation: BFME2 FUN_00563a80, whose own summation order is not transcribed (stop S-028)
	HRawAnimClass raw = loadRaw(w3dsynth::rawAnimChunk("RAW", "T_SKL", 4, 30, { { ANIM_CHANNEL_Z, 1, 0, 1, { 0, 10, 20, 30 } } }), 3);
	tree.Anim_Pose(Matrix3D(), &raw, 1.0f, pose);
	CHECK_FALSE(pose.ExactOperationOrder);

	// blend: BFME2 FUN_005645b0, likewise
	FakeAnim m0, m1;
	m0.Pivots = m1.Pivots = 3;
	tree.Blend_Pose(Matrix3D(), &m0, 0.0f, &m1, 0.0f, 0.5f, pose);
	CHECK_FALSE(pose.ExactOperationOrder);

	// the flag is per evaluation: a base pose after a raw pose is exact again
	tree.Base_Pose(Matrix3D(), pose);
	CHECK(pose.ExactOperationOrder);
}

TEST_CASE("Raw animation: the frame is rounded to the nearest integer, not interpolated, and wraps to 0 at the end")
{
	// BFME2 FUN_00563a80: iframe = ROUND(frame); if (iframe >= NumFrames) iframe = 0. Z channel of pivot 1: 0, 10, 20, 30.
	HTreeClass tree = chainTree();
	HRawAnimClass anim = loadRaw(w3dsynth::rawAnimChunk("RAW", "T_SKL", 4, 30, { { ANIM_CHANNEL_Z, 1, 0, 1, { 0, 10, 20, 30 } } }), 3);
	struct Case { float frame; float z; };
	const Case cases[] = { { 0.0f, 0.0f }, { 1.4f, 10.0f }, { 1.6f, 20.0f }, { 2.5f, 20.0f } /* ties to even */, { 3.0f, 30.0f },
		{ 3.5f, 0.0f } /* rounds to 4 = NumFrames: back to 0 */, { 9.0f, 0.0f } };
	for (const Case &c : cases)
	{
		HTreePose pose;
		tree.Anim_Pose(Matrix3D(), &anim, c.frame, pose);
		INFO("frame " << c.frame);
		CHECK(pose.Transform[1].Get_Translation().Z == doctest::Approx(c.z));
		CHECK(pose.Transform[1].Get_Translation().X == doctest::Approx(1.0f)); // the base translation is untouched
	}
}

TEST_CASE("Raw animation: the stored quaternion is used as written; retail composes quaternion + translation, not matrices")
{
	// BFME2 keeps every pivot as a quaternion and a translation (pivot +0x30 / +0x40) and composes with q (x) v (x) conj(q)
	// (0x5628A0 Base_Update, 0x563A80 raw Anim_Update). q = (0, 0, 0.5, 0.5) is not a unit quaternion: |q|^2 = 0.5, so the child
	// offset (0,2,0) becomes 0.5 * Rz(90)(0,2,0) = (-1, 0, 0) and B lands at A + (-1, 0, 0) = (0, 0, 0). A matrix
	// built from the same quaternion would put it at (0, 1, 0). The expected value is the pseudocode of the binary evaluated
	// in float32 (see the golden table below).
	HTreeClass tree = chainTree();
	HRawAnimClass anim = loadRaw(w3dsynth::rawAnimChunk("RAWQ", "T_SKL", 2, 30, { { ANIM_CHANNEL_Q, 1, 0, 4, { 0, 0, 0.5f, 0.5f, 0, 0, 0.5f, 0.5f } } }), 3);
	HTreePose pose;
	tree.Anim_Pose(Matrix3D(), &anim, 0.0f, pose);
	Vector3 b = pose.Transform[2].Get_Translation();
	CHECK(b.X == doctest::Approx(0.0f).epsilon(1e-5));
	CHECK(b.Y == doctest::Approx(0.0f).epsilon(1e-5));
	CHECK(b.Z == doctest::Approx(0.0f).epsilon(1e-5));
	// the world quaternion of A is q itself, unnormalised
	REQUIRE(pose.World.size() == 3);
	CHECK(pose.World[1].Q[2] == doctest::Approx(0.5f));
	CHECK(pose.World[1].Q[3] == doctest::Approx(0.5f));
}

namespace
{
W3dPivotStruct makePivotQ(const char *name, std::uint32_t parent, float x, float y, float z, float qx, float qy, float qz, float qw)
{
	W3dPivotStruct p = makePivot(name, parent, x, y, z);
	p.Rotation.Q[0] = qx;
	p.Rotation.Q[1] = qy;
	p.Rotation.Q[2] = qz;
	p.Rotation.Q[3] = qw;
	return p;
}

namespace
{
float bitsToFloat(std::uint32_t b)
{
	float f;
	std::memcpy(&f, &b, 4);
	return f;
}
std::uint32_t floatBits(float f)
{
	std::uint32_t b;
	std::memcpy(&b, &f, 4);
	return b;
}
} // namespace

// Golden table: world quaternion and translation of pivots A and B, the pseudocode of game.dat evaluated per operation in float32
// (arm A = 0x562BB0 motion-channel arm lines 84-222, arm B = classic arm lines 276-397, base U = Base_Update 0x5628A0).
// Scenario: root identity; A base q (0.1,0.2,0.3,0.9) t (1,2,3); B base q (0.5,-0.5,0.5,0.5) t (0.4,-1.5,2.2);
// animation A t (0.3,0.2,-0.1) q (0.2,0.1,-0.3,0.8); B t (-0.7,0.9,0.25) q (0,0.6,0,0.8). Neither base nor animation quaternion of A is a unit.
struct GoldenQT
{
	float Q[4];
	float T[3];
};
const GoldenQT kBaseU[2] = {
	{ { 0.100000001f, 0.200000003f, 0.300000012f, 0.899999976f }, { 1.0f, 2.0f, 3.0f } },
	{ { 0.75f, -0.249999985f, 0.449999988f, 0.349999964f }, { 2.94999981f, 0.975000143f, 4.30000019f } },
};
const GoldenQT kArmA[2] = {
	{ { 0.169999987f, 0.340000004f, -0.0600000024f, 0.769999981f }, { 1.06499994f, 2.32999992f, 2.88499999f } },
	{ { 0.427999973f, 0.0360000134f, 0.44599998f, 0.59799999f }, { 1.20450008f, 0.681499958f, 3.44825006f } },
};
const GoldenQT kArmANoRot[2] = {
	{ { 0.100000001f, 0.200000003f, 0.300000012f, 0.899999976f }, { 1.06499994f, 2.32999992f, 2.88499999f } },
	{ { 0.75f, -0.249999985f, 0.449999988f, 0.349999994f }, { 2.2249999f, 0.637500048f, 3.78500009f } },
};
const GoldenQT kArmB[2] = {
	{ { 0.169999987f, 0.340000004f, -0.0600000024f, 0.769999981f }, { 1.06499994f, 2.32999992f, 2.88499999f } },
	{ { 0.427999973f, 0.0360000134f, 0.44599998f, 0.59799999f }, { 1.20450008f, 0.681499958f, 3.44825006f } },
};
const GoldenQT kArmBNoRot[2] = {
	{ { 0.100000001f, 0.200000003f, 0.300000012f, 0.899999976f }, { 1.06499994f, 2.32999992f, 2.88499999f } },
	{ { 0.75f, -0.249999985f, 0.449999988f, 0.349999964f }, { 2.2249999f, 0.637500048f, 3.78499985f } },
};

HTreeClass goldenTree()
{
	return makeTree({ makePivot("ROOTTRANSFORM", 0xFFFFFFFFu, 0, 0, 0), makePivotQ("A", 0, 1, 2, 3, 0.1f, 0.2f, 0.3f, 0.9f),
		makePivotQ("B", 1, 0.4f, -1.5f, 2.2f, 0.5f, -0.5f, 0.5f, 0.5f) });
}

FakeAnim goldenAnim(bool rotations)
{
	FakeAnim a;
	a.Pivots = 3;
	a.Trans = [](int p, float) { return p == 1 ? Vector3(0.3f, 0.2f, -0.1f) : (p == 2 ? Vector3(-0.7f, 0.9f, 0.25f) : Vector3()); };
	if (rotations)
	{
		a.Orient = [](Quaternion &q, int p, float) {
			if (p == 1) { q.Set(0.2f, 0.1f, -0.3f, 0.8f); return true; }
			if (p == 2) { q.Set(0.0f, 0.6f, 0.0f, 0.8f); return true; }
			return false;
		};
	}
	return a;
}

void checkGolden(const HTreePose &pose, const GoldenQT (&golden)[2], const char *what)
{
	REQUIRE(pose.World.size() == 3);
	for (int i = 0; i < 2; ++i)
	{
		INFO(what << " pivot " << (i + 1));
		for (int k = 0; k < 4; ++k) CHECK(pose.World[(size_t)i + 1].Q[k] == doctest::Approx(golden[i].Q[k]).epsilon(2e-6));
		for (int k = 0; k < 3; ++k) CHECK(pose.World[(size_t)i + 1].T[k] == doctest::Approx(golden[i].T[k]).epsilon(2e-6));
		// the matrix handed to the renderer carries the same translation
		Vector3 t = pose.Transform[(size_t)i + 1].Get_Translation();
		CHECK(t.X == doctest::Approx(golden[i].T[0]).epsilon(2e-6));
		CHECK(t.Y == doctest::Approx(golden[i].T[1]).epsilon(2e-6));
		CHECK(t.Z == doctest::Approx(golden[i].T[2]).epsilon(2e-6));
	}
}
} // namespace

TEST_CASE("Pose composition is quaternion + translation as in BFME2 (Base_Update 0x5628A0), non-unit quaternions included")
{
	HTreeClass tree = goldenTree();
	HTreePose pose;
	tree.Base_Pose(Matrix3D(), pose);
	checkGolden(pose, kBaseU, "base");
}

TEST_CASE("Base_Update is bit-identical to the real BFME2 1.06 function 0x5628A0 (addition grouping included)")
{
	// Expected values: tools/retail_oracle called the real Base_Update in game.dat (BFME2 1.06) on a hand-built hierarchy object (root
	// matrix, two chained pivots), then read each pivot's world quaternion (+0x30) and translation (+0x40). Rows 0 and 1 are the golden
	// scenario above; row 1 changes pivot A's translation X from 1 to -3, where adding the parent translation last (0x562A72) and adding it
	// to the first term differ by one ulp (child X 0xBF866667, not 0xBF866668). Rows 2..15 are pseudo-random rotated roots and non-unit
	// base quaternions. Each row: root matrix (3 x 4 row-major), A base q / t, B base q / t, then world (q, t) of root, A, B as bit patterns.
	struct Row { std::uint32_t root[12], aq[4], at[3], bq[4], bt[3], world[3][7]; };
	const Row rows[] = {
{ { 0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x3F800000u, 0x00000000u }, { 0x3DCCCCCDu, 0x3E4CCCCDu, 0x3E99999Au, 0x3F666666u }, { 0x3F800000u, 0x40000000u, 0x40400000u }, { 0x3F000000u, 0xBF000000u, 0x3F000000u, 0x3F000000u }, { 0x3ECCCCCDu, 0xBFC00000u, 0x400CCCCDu }, { { 0x00000000u, 0x00000000u, 0x00000000u, 0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u }, { 0x3DCCCCCDu, 0x3E4CCCCDu, 0x3E99999Au, 0x3F666666u, 0x3F800000u, 0x40000000u, 0x40400000u }, { 0x3F400000u, 0xBE7FFFFFu, 0x3EE66666u, 0x3EB33332u, 0x403CCCCCu, 0x3F79999Cu, 0x4089999Au } } },
{ { 0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u, 0x3F800000u, 0x00000000u }, { 0x3DCCCCCDu, 0x3E4CCCCDu, 0x3E99999Au, 0x3F666666u }, { 0xC0400000u, 0x40000000u, 0x40400000u }, { 0x3F000000u, 0xBF000000u, 0x3F000000u, 0x3F000000u }, { 0x3ECCCCCDu, 0xBFC00000u, 0x400CCCCDu }, { { 0x00000000u, 0x00000000u, 0x00000000u, 0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u }, { 0x3DCCCCCDu, 0x3E4CCCCDu, 0x3E99999Au, 0x3F666666u, 0xC0400000u, 0x40000000u, 0x40400000u }, { 0x3F400000u, 0xBE7FFFFFu, 0x3EE66666u, 0x3EB33332u, 0xBF866667u, 0x3F79999Cu, 0x4089999Au } } },
{ { 0x3E45856Fu, 0x3F33FE9Fu, 0x3F2F3679u, 0x4065A513u, 0xBC8A3DF7u, 0x3F3325FDu, 0xBF36D1F2u, 0xC156E5D2u, 0xBF7B27BAu, 0x3E013B21u, 0x3E165FF6u, 0xC230CCE9u }, { 0x3C73A773u, 0xBF6CCD5Eu, 0xBE07E4C6u, 0xBF5C3BE9u }, { 0xC0312087u, 0x3FA31D51u, 0x408F4451u }, { 0xBF518E10u, 0xBE1A95AFu, 0x3F275929u, 0xBF409D08u }, { 0x3F456230u, 0xBF843FBEu, 0x409866D1u }, { { 0x3E96A2F6u, 0x3F154684u, 0xBE810F85u, 0x3F36CCCCu, 0x4065A513u, 0xC156E5D2u, 0xC230CCE9u }, { 0xBF0D9407u, 0xBF903CEAu, 0xBE2289A6u, 0xBDE6E8A0u, 0x40E0775Du, 0xC17B09D6u, 0xC222AA96u }, { 0xBE811FDFu, 0x3FAD9BE8u, 0xBF4B0FA0u, 0xBEDE5295u, 0x40E1D4E5u, 0xC1693182u, 0xC242326Cu } } },
{ { 0x3EA19E03u, 0xBF70B777u, 0xBE0251E5u, 0xC218E213u, 0xBEB1D854u, 0x3C28D23Fu, 0xBF700BA4u, 0xC19936ECu, 0x3F620CE3u, 0x3EAE2DACu, 0xBEA58FF1u, 0x41FCE6AEu }, { 0xBF2377D5u, 0x3E271DFDu, 0x3E8E3F55u, 0xBE82AA38u }, { 0x3FE6E977u, 0xBF395D1Bu, 0xBFEDE441u }, { 0x3DC38FB3u, 0xBF5FDA20u, 0xBF617BF5u, 0xBF168C94u }, { 0x3F5B09D3u, 0xBEEFB22Du, 0xC000262Du }, { { 0x3F235AEFu, 0xBF0125BFu, 0x3E9798E8u, 0x3F002A8Du, 0xC218E213u, 0xC19936ECu, 0x41FCE6AEu }, { 0xBF2BC7BEu, 0xBE1FA37Eu, 0xBE1E2C52u, 0x3E8F3D1Eu, 0xC212EF91u, 0xC190587Bu, 0x42063DB0u }, { 0x3ED8DE89u, 0xBF423C26u, 0x3EE46241u, 0xBEBEE291u, 0xC212CE9Du, 0xC194E637u, 0x420AD849u } } },
{ { 0xBD85C839u, 0x3F4974E8u, 0xBF1D1264u, 0x402141F4u, 0x3ECD3449u, 0xBF0ADB42u, 0xBF3D0486u, 0x42160E14u, 0xBF69F150u, 0xBE96999Bu, 0xBE8F57A2u, 0x41B78E65u }, { 0xBED926D8u, 0x3F75D97Au, 0xBF438CE2u, 0xBE27AF39u }, { 0x3FD750F9u, 0x4029534Au, 0x3F3AF248u }, { 0x3F03A7FAu, 0xBF322F15u, 0xBCB4D41Du, 0xBF6BED07u }, { 0x40704E49u, 0xBFEE6737u, 0x3FF9FA63u }, { { 0x3F29AB38u, 0x3EE5629Fu, 0xBF137E27u, 0x3E2B9466u, 0x402141F4u, 0x42160E14u, 0x41B78E65u }, { 0x3D00ECAAu, 0x3F56929Au, 0x3F4AF8CCu, 0xBF1DDFA6u, 0x4081662Au, 0x4210DAD4u, 0x41A365AEu }, { 0x3E3FAEE0u, 0x3D859A98u, 0xBF95C3E2u, 0x3F9396FFu, 0xC0574A4Cu, 0x420A44FAu, 0x41B57EF1u } } },
{ { 0x3F5FFDDFu, 0x3EACF648u, 0x3EB197A1u, 0x4231DF58u, 0xBDE2B228u, 0x3F5643FCu, 0xBF0932AFu, 0xC025C549u, 0xBEF155F5u, 0x3EDC6DFCu, 0x3F450C51u, 0x4183525Fu }, { 0xBF60EFF0u, 0x3ECE53EDu, 0x3E96A8F2u, 0x3F7C7712u }, { 0xC098C7A5u, 0xBEC41EC1u, 0xC05472F4u }, { 0x3F24D353u, 0xBEDC92FDu, 0xBE69E62Du, 0x3EACB34Cu }, { 0xC0750F06u, 0xC08D2274u, 0x402BAB4Bu }, { { 0x3E849874u, 0x3E6083F2u, 0xBDF6230Bu, 0x3F6ED681u, 0x4231DF58u, 0xC025C549u, 0x4183525Fu }, { 0xBEE71EDCu, 0x3F1F207Eu, 0x3EE7F212u, 0x3F8C1D25u, 0x421C0A1Fu, 0xBF1A6F26u, 0x417F2167u }, { 0x3F1B0F67u, 0xBD963790u, 0xBE9B1EB1u, 0x3F83FC10u, 0x42353CAAu, 0xC051A322u, 0x41D63ADEu } } },
{ { 0x3F11B761u, 0x3F439418u, 0xBE9B9632u, 0xC227C47Au, 0x3E9AC869u, 0x3E18D15Cu, 0x3F71032Eu, 0xC0A299AEu, 0x3F43BCE6u, 0xBF20B3FBu, 0xBE158459u, 0x409E352Cu }, { 0x3F444AE3u, 0x3F2378A6u, 0x3F3A5C2Cu, 0xBEE2E597u }, { 0xC05F691Eu, 0xC04F387Du, 0xC02B8C30u }, { 0xBE2D7905u, 0xBE909E4Bu, 0x3F44B4ECu, 0x3F6A5BBFu }, { 0xC02AAA37u, 0xBE19FB4Du, 0x3F6427FBu }, { { 0xBF202D99u, 0xBEDA2213u, 0xBE3C805Cu, 0x3F2081ECu, 0xC227C47Au, 0xC0A299AEu, 0x409E352Cu }, { 0x3F10C05Fu, 0x3F674C3Eu, 0x3EEE25D2u, 0x3F1BA3FAu, 0xC2365A0Du, 0xC1124AD2u, 0x40965CDDu }, { 0x3F9EC0EBu, 0x3E118AF8u, 0x3F62E59Cu, 0x3F0CD798u, 0xC22D2C20u, 0xC1556656u, 0x40B02F30u } } },
{ { 0xBF0CB40Bu, 0x3F283430u, 0x3F041560u, 0x40D44ABBu, 0x3F498111u, 0x3F1D3D53u, 0x3D66A46Bu, 0x42353D3Au, 0xBE8F5021u, 0x3EDFC744u, 0xBF5AD1E6u, 0x4198651Au }, { 0x3CFDCFC8u, 0x3E70D478u, 0x3EB46DCBu, 0xBF645B0Bu }, { 0xBF89C148u, 0xBF814E9Eu, 0xC07DBC7Cu }, { 0x3F4C8F97u, 0x3F0F5829u, 0x3F3FC031u, 0x3F1882D3u }, { 0x3FABE401u, 0xC08C14A9u, 0xC08A72E2u }, { { 0x3ED4CACFu, 0x3F5E638Eu, 0x3E1164BFu, 0x3E6A8856u, 0x40D44ABBu, 0x42353D3Au, 0x4198651Au }, { 0xBDBA26D4u, 0xBF5CF3C4u, 0x3CCBE464u, 0xBEF16B74u, 0x409085B4u, 0x422E79CDu, 0x41B2621Bu }, { 0xBF8BB131u, 0xBF30B380u, 0x3E99D07Eu, 0x3E8341A6u, 0xBEEEF8C0u, 0x4220A359u, 0x41BA7225u } } },
{ { 0x3EAF4CB3u, 0x3E01305Cu, 0x3F6E596Du, 0xC247E81Du, 0x3F4CDFE5u, 0x3EF5C3C2u, 0xBEB7FB5Cu, 0xC20B7E79u, 0xBEFC07F3u, 0x3F5E3EB5u, 0x3D81CDDBu, 0xC21F6A0Cu }, { 0xBE8BA9D7u, 0xBF72F18Cu, 0x3F3FA87Eu, 0x3E699D00u }, { 0xC0716187u, 0x405F51D3u, 0x409DCAFAu }, { 0xBF33F131u, 0xBEFDB025u, 0xBE9C45EAu, 0xBE8B18BDu }, { 0xBEAE224Cu, 0xBE25887Au, 0xC0848454u }, { { 0x3EE4D43Fu, 0x3F04AAACu, 0x3E7B582Bu, 0x3F2FC623u, 0xC247E81Du, 0xC20B7E79u, 0xC21F6A0Cu }, { 0x3F091BE0u, 0xBF6F576Au, 0x3E9314D8u, 0x3F162B03u, 0xC238F269u, 0xC217F496u, 0xC20A9EEDu }, { 0xBE053EEFu, 0xBD99CFD6u, 0xBF96FDA7u, 0xBE224454u, 0xC22A9112u, 0xC2044B64u, 0xC2006D43u } } },
{ { 0x3F09A379u, 0x3F4ECFA3u, 0x3E77401Du, 0xC2076CB0u, 0xBDAE78D1u, 0xBE6E7144u, 0x3F780185u, 0xC23EC300u, 0x3F56BF10u, 0xBF0A9B41u, 0xBD66E60Au, 0x423464ECu }, { 0x3D677C0Du, 0xBF34F083u, 0x3DB0D592u, 0xBF72277Du }, { 0xC018E2E3u, 0xBFAA9FD0u, 0xC05517D5u }, { 0x3D6645C5u, 0x3F74FE1Du, 0x3F3A05BDu, 0x3EC8E7CFu }, { 0x402E0A4Fu, 0x3EA6DF82u, 0x4032985Au }, { { 0xBF2D01E2u, 0xBE88DFD6u, 0xBECC9CECu, 0x3F0F04A6u, 0xC2076CB0u, 0xC23EC300u, 0x423464ECu }, { 0x3EBB072Au, 0xBDD989CFu, 0x3F6B4543u, 0xBF250AFFu, 0xC21415D6u, 0xC2499BC9u, 0x42300430u }, { 0xBF597F7Eu, 0xBF5F5786u, 0x3E7DA5E6u, 0xBF56F490u, 0xC20CF4AAu, 0xC254FB96u, 0x424169C1u } } },
{ { 0x3E5370A9u, 0xBEF2EB5Fu, 0xBF5B1092u, 0x420D0D30u, 0x3F67C618u, 0x3ED93DC0u, 0xBC89862Du, 0x41F4DCE5u, 0x3EBDF9DDu, 0xBF45724Bu, 0x3F0464DEu, 0x41FEAA96u }, { 0x3EF5A146u, 0xBF0BE8CDu, 0x3D107F16u, 0xBE93E76Au }, { 0x3FF66D97u, 0x409215B7u, 0xBF0718DEu }, { 0xBF712984u, 0xBF71B23Bu, 0xBEE1E01Bu, 0xBEF69AFFu }, { 0x408BD8C7u, 0x409C2C14u, 0x409199A7u }, { { 0xBE83CA11u, 0xBED648D4u, 0x3EF104B0u, 0x3F3B9862u, 0x420D0D30u, 0x41F4DCE5u, 0x41FEAA96u }, { 0x3F2B1D4Eu, 0xBD36F86Cu, 0x3E6CE0F6u, 0xBEAAC56Du, 0x4207C8C0u, 0x42092FDCu, 0x41E607A5u }, { 0x3E6BE65Au, 0x3ED3A64Cu, 0xBF232E43u, 0x3F59A6CCu, 0x421895FDu, 0x4205A0F2u, 0x41D33EFDu } } },
{ { 0xBE25374Eu, 0xBEAF3105u, 0x3F6CF980u, 0xC1EC8056u, 0x3F6AE363u, 0x3E963CC4u, 0x3E896A2Du, 0x41468199u, 0xBEBA1763u, 0x3F648470u, 0x3E888103u, 0x42201F93u }, { 0x3F2E4D91u, 0xBD282758u, 0x3E9CA647u, 0x3F196AE8u }, { 0x40201703u, 0xBE60F1D8u, 0xC04DBF00u }, { 0xBF5497EAu, 0x3EA47091u, 0x3F51CE4Fu, 0x3F108A01u }, { 0x40390BF3u, 0xBFD660C4u, 0x404086EFu }, { { 0x3E87207Eu, 0x3F0B8620u, 0x3F085678u, 0x3F176196u, 0xC1EC8056u, 0x41468199u, 0x42201F93u }, { 0x3F3FD8A8u, 0x3F158EA8u, 0x3DF2000Eu, 0x3D0BBBE8u, 0xC20377C2u, 0x415C6450u, 0x42184623u }, { 0x3F55EB78u, 0xBEBE350Bu, 0x3F520D16u, 0x3EB6D8BDu, 0xC20444EBu, 0x4187D9F3u, 0x420DE948u } } },
{ { 0x3F683400u, 0xBCBB988Au, 0xBED7407Au, 0x41B3D6C4u, 0xBED79229u, 0xBD48DDB9u, 0xBF67DD0Fu, 0xC203FFA0u, 0x3901EA91u, 0x3F7F9FF1u, 0xBD5DAFCBu, 0xC2152F45u }, { 0xBF329C60u, 0x3F4F48C6u, 0x3F1CEDD4u, 0xBF3528A4u }, { 0x3EF923B1u, 0xC06C2B9Bu, 0xC09B7138u }, { 0x3F272C62u, 0x3F75EAA9u, 0x3EA10AF0u, 0xBE992EC6u }, { 0x4096AF53u, 0x3FBF9565u, 0x3E88184Fu }, { { 0x3F357B08u, 0xBE205055u, 0xBE17C641u, 0x3F2BEA42u, 0x41B3D6C4u, 0xC203FFA0u, 0xC2152F45u }, { 0xBF72370Du, 0x3EA592D8u, 0x3F7B37E8u, 0x3E72B242u, 0x41C86228u, 0xC1E4FDE1u, 0xC222DF2Eu }, { 0xBECE6404u, 0x3F88E042u, 0xBFAB6807u, 0xBD94326Du, 0x41B37C77u, 0xC1FC8B01u, 0xC24799C8u } } },
{ { 0x3EB1E64Bu, 0xBF2FB09Cu, 0x3F239360u, 0xC1E72A88u, 0x3ED8DCE3u, 0xBEFC745Au, 0xBF4289E9u, 0xC1C6883Bu, 0x3F562A30u, 0x3F08E0ECu, 0x3DF47352u, 0xC1A5A06Eu }, { 0xBF04D805u, 0x3E3105F8u, 0xBEF66913u, 0xBE25DCBFu }, { 0x3F555F74u, 0x40815FFEu, 0xBF4B310Eu }, { 0xBF3CE3E9u, 0x3F51EDC1u, 0xBE95B9A4u, 0xBDAB5F63u }, { 0x4085ABB6u, 0x3C8714CDu, 0x3EA2F19Du }, { { 0x3F27ED81u, 0xBDCD10FCu, 0x3F0FF68Au, 0x3EFC9DF7u, 0xC1E72A88u, 0xC1C6883Bu, 0xC1A5A06Eu }, { 0xBED29728u, 0x3E00670Du, 0xBE88C1A5u, 0x3F0C6622u, 0xC1FF1A8Eu, 0xC1CED49Du, 0xC18F8342u }, { 0xBE4063FAu, 0x3F041B6Fu, 0xBEC3FDB4u, 0xBF07C2A0u, 0xC1F15FB7u, 0xC1DB147Eu, 0xC18C5BA6u } } },
{ { 0xBECC754Fu, 0xBE387F8Du, 0x3F66201Fu, 0xC2466D50u, 0x3D3AA748u, 0x3F79B2ECu, 0x3E5CEB92u, 0x41EF561Cu, 0xBF6A698Bu, 0x3E022AE8u, 0xBEC338D6u, 0xC2030FB2u }, { 0xBD59255Au, 0x3EE69911u, 0x3DE752FCu, 0xBEB231BCu }, { 0x3F1A5BADu, 0xC020F6B1u, 0xC00EC5E8u }, { 0x3D16500Au, 0x3DE31705u, 0x3F118C29u, 0xBF49AC07u }, { 0x402E3F42u, 0x3D9DFB89u, 0x3F1E06F9u }, { { 0xBD260DBFu, 0x3F547EF2u, 0x3DD37B9Du, 0x3F0BE91Au, 0xC2466D50u, 0x41EF561Cu, 0xC2030FB2u }, { 0x3D04A542u, 0xBD32BE8Du, 0x3D531924u, 0xBF13EF6Du, 0xC24D9955u, 0x41D814A8u, 0xC203256Du }, { 0xBD9E31C1u, 0xBD3D508Au, 0xBEBA5839u, 0x3EDBF2A8u, 0xC249D9A7u, 0x41D716E0u, 0xC202D925u } } },
{ { 0xBEBBF595u, 0x3F656335u, 0x3E7FA96Cu, 0x3F0E2900u, 0x3F4B9A98u, 0x3EE21475u, 0xBED4A298u, 0x3F9BAAB6u, 0xBEF6F9D9u, 0x3D3CDBF0u, 0xBF5FEFA6u, 0x419A2F4Fu }, { 0xBDC3310Fu, 0x3D885650u, 0xBD33ED2Eu, 0x3F620C70u }, { 0x3F185AF8u, 0x408DD86Fu, 0x40599991u }, { 0x3ECBFFC6u, 0x3F40C942u, 0x3F62657Eu, 0xBEF62D70u }, { 0xC0683BE5u, 0xC0722976u, 0xBF142D7Du }, { { 0x3F042957u, 0x3F51ADC0u, 0xBDE6C973u, 0x3E64CDC4u, 0x3F0E2900u, 0x3F9BAAB6u, 0x419A2F4Fu }, { 0x3ECFE9FAu, 0x3F45827Bu, 0x3B4BE090u, 0x3E3F8408u, 0x40A50AE8u, 0x400F0619u, 0x4181BAE3u }, { 0x3F0F29A7u, 0xBF168716u, 0x3E2634A6u, 0xBF55E25Du, 0x4081D4D6u, 0xBFDBFE6Cu, 0x4188A61Eu } } },
	};
	int rowIndex = 0;
	for (const Row &r : rows)
	{
		INFO("row " << rowIndex++);
		HTreeClass tree = makeTree({ makePivot("ROOTTRANSFORM", 0xFFFFFFFFu, 0, 0, 0),
			makePivotQ("A", 0, bitsToFloat(r.at[0]), bitsToFloat(r.at[1]), bitsToFloat(r.at[2]), bitsToFloat(r.aq[0]), bitsToFloat(r.aq[1]), bitsToFloat(r.aq[2]), bitsToFloat(r.aq[3])),
			makePivotQ("B", 1, bitsToFloat(r.bt[0]), bitsToFloat(r.bt[1]), bitsToFloat(r.bt[2]), bitsToFloat(r.bq[0]), bitsToFloat(r.bq[1]), bitsToFloat(r.bq[2]), bitsToFloat(r.bq[3])) });
		Matrix3D root;
		for (int i = 0; i < 3; ++i)
			for (int j = 0; j < 4; ++j) root.Row[i][j] = bitsToFloat(r.root[i * 4 + j]);
		HTreePose pose;
		tree.Base_Pose(root, pose);
		REQUIRE(pose.World.size() == 3);
		for (int p = 0; p < 3; ++p)
		{
			INFO("pivot " << p);
			for (int k = 0; k < 4; ++k) CHECK(floatBits(pose.World[(size_t)p].Q[k]) == r.world[p][k]);
			for (int k = 0; k < 3; ++k) CHECK(floatBits(pose.World[(size_t)p].T[k]) == r.world[p][4 + k]);
		}
	}
}

TEST_CASE("Anim_Update's two generic arms (0x562BB0) are bit-identical to the machine code, base composition and animation apply")
{
	// Expected values: the retail function's own SSE instructions, executed instruction by instruction in a scalar float32 emulator
	// (the same emulator reproduces the real Base_Update bit for bit on the 16 rows of the test above, which tools/retail_oracle produced
	// by calling the real function). Segments run: arm 0 (motion channels) 0x562C90-0x562ED1 base, 0x562F6B-0x562F8F translation
	// scaling, 0x562FB2-0x5631EA translation + rotation, 0x5631F4-0x563316 translation without rotation; arm 1 (classic) 0x563441-0x563678
	// base, 0x5636A7-0x5636D0 scaling (only when the factor is not 1), 0x5636EE-0x56392D translation + rotation,
	// 0x5636EE-0x56372A + 0x563937-0x563A17 translation without rotation. The decompile's left-to-right additions were NOT trusted: the
	// compiled code adds the two product pairs together before the parent translation (PT + (a + b)).
	// Each row: arm (0 motion channel, 1 classic), scale factor, rotation mask (bit 0 pivot A, bit 1 pivot B), A base q / t, B base q / t,
	// A anim t / q, B anim t / q, then the world (q, t) of pivots A and B as bit patterns. Root is the identity.
	struct Row { int arm; std::uint32_t scale; int mask; std::uint32_t aq[4], at[3], bq[4], bt[3], at_t[3], at_q[4], bt_t[3], bt_q[4], world[2][7]; };
	const Row rows[] = {
{ 0, 0x3F800000u, 3, { 0x3DCCCCCDu, 0x3E4CCCCDu, 0x3E99999Au, 0x3F666666u }, { 0x3F800000u, 0x40000000u, 0x40400000u }, { 0x3F000000u, 0xBF000000u, 0x3F000000u, 0x3F000000u }, { 0x3ECCCCCDu, 0xBFC00000u, 0x400CCCCDu }, { 0x3E99999Au, 0x3E4CCCCDu, 0xBDCCCCCDu }, { 0x3E4CCCCDu, 0x3DCCCCCDu, 0xBE99999Au, 0x3F4CCCCDu }, { 0xBF333333u, 0x3F666666u, 0x3E800000u }, { 0x00000000u, 0x3F19999Au, 0x00000000u, 0x3F4CCCCDu }, { { 0x3E2E147Au, 0x3EAE147Bu, 0xBD75C290u, 0x3F451EB8u, 0x3F8851EBu, 0x40151EB8u, 0x4038A3D7u }, { 0x3EDB22D0u, 0x3D1374C0u, 0x3EE45A1Cu, 0x3F191687u, 0x3F9A2D0Fu, 0x3F2E76C8u, 0x405CB021u } } },
{ 0, 0x3F800000u, 0, { 0x3DCCCCCDu, 0x3E4CCCCDu, 0x3E99999Au, 0x3F666666u }, { 0x3F800000u, 0x40000000u, 0x40400000u }, { 0x3F000000u, 0xBF000000u, 0x3F000000u, 0x3F000000u }, { 0x3ECCCCCDu, 0xBFC00000u, 0x400CCCCDu }, { 0x3E99999Au, 0x3E4CCCCDu, 0xBDCCCCCDu }, { 0x3E4CCCCDu, 0x3DCCCCCDu, 0xBE99999Au, 0x3F4CCCCDu }, { 0xBF333333u, 0x3F666666u, 0x3E800000u }, { 0x00000000u, 0x3F19999Au, 0x00000000u, 0x3F4CCCCDu }, { { 0x3DCCCCCDu, 0x3E4CCCCDu, 0x3E99999Au, 0x3F666666u, 0x3F8851EBu, 0x40151EB8u, 0x4038A3D7u }, { 0x3F400000u, 0xBE7FFFFFu, 0x3EE66666u, 0x3EB33333u, 0x400E6666u, 0x3F233334u, 0x40723D71u } } },
{ 1, 0x3F800000u, 3, { 0x3DCCCCCDu, 0x3E4CCCCDu, 0x3E99999Au, 0x3F666666u }, { 0x3F800000u, 0x40000000u, 0x40400000u }, { 0x3F000000u, 0xBF000000u, 0x3F000000u, 0x3F000000u }, { 0x3ECCCCCDu, 0xBFC00000u, 0x400CCCCDu }, { 0x3E99999Au, 0x3E4CCCCDu, 0xBDCCCCCDu }, { 0x3E4CCCCDu, 0x3DCCCCCDu, 0xBE99999Au, 0x3F4CCCCDu }, { 0xBF333333u, 0x3F666666u, 0x3E800000u }, { 0x00000000u, 0x3F19999Au, 0x00000000u, 0x3F4CCCCDu }, { { 0x3E2E147Au, 0x3EAE147Bu, 0xBD75C290u, 0x3F451EB8u, 0x3F8851EBu, 0x40151EB8u, 0x4038A3D7u }, { 0x3EDB22D0u, 0x3D1374C0u, 0x3EE45A1Cu, 0x3F191687u, 0x3F9A2D0Fu, 0x3F2E76C8u, 0x405CB021u } } },
{ 1, 0x3F800000u, 0, { 0x3DCCCCCDu, 0x3E4CCCCDu, 0x3E99999Au, 0x3F666666u }, { 0x3F800000u, 0x40000000u, 0x40400000u }, { 0x3F000000u, 0xBF000000u, 0x3F000000u, 0x3F000000u }, { 0x3ECCCCCDu, 0xBFC00000u, 0x400CCCCDu }, { 0x3E99999Au, 0x3E4CCCCDu, 0xBDCCCCCDu }, { 0x3E4CCCCDu, 0x3DCCCCCDu, 0xBE99999Au, 0x3F4CCCCDu }, { 0xBF333333u, 0x3F666666u, 0x3E800000u }, { 0x00000000u, 0x3F19999Au, 0x00000000u, 0x3F4CCCCDu }, { { 0x3DCCCCCDu, 0x3E4CCCCDu, 0x3E99999Au, 0x3F666666u, 0x3F8851EBu, 0x40151EB8u, 0x4038A3D7u }, { 0x3F400000u, 0xBE7FFFFFu, 0x3EE66666u, 0x3EB33332u, 0x400E6666u, 0x3F233334u, 0x40723D70u } } },
{ 0, 0x3F800000u, 3, { 0xBE44A725u, 0xBF198FB6u, 0xBF247406u, 0xBF00CD9Cu }, { 0x40265252u, 0xC01F436Bu, 0xBF95AC6Au }, { 0x3EBCBB67u, 0x3D9E3994u, 0x3F6072EEu, 0xBCAFB816u }, { 0xBF489A79u, 0x3F90EAA4u, 0xC0355122u }, { 0x401A5E24u, 0xBF41912Au, 0xBF2BDC36u }, { 0x3EBABEE4u, 0xBF3208CFu, 0x3EA46BE9u, 0x3F332EE6u }, { 0xBF7797C5u, 0x402B9809u, 0x3E306EA0u }, { 0x3F0C87CDu, 0xBD7DA1D3u, 0xBE476DBCu, 0xBEEC229Du }, { { 0xBF7512ACu, 0xBE7867C2u, 0xBE848A1Bu, 0xBEFC6878u, 0x3F96E259u, 0xBF6282CEu, 0xC03C22D0u }, { 0x3EAAD654u, 0xBF29095Cu, 0xBE984314u, 0xBDFB6204u, 0xBF8E1AB6u, 0x4026A5FFu, 0xC00E9949u } } },
{ 0, 0x3F800000u, 3, { 0xBF7C1942u, 0xBF17AC31u, 0x3F7AA8EAu, 0x3D8C88ACu }, { 0xBEC21DBFu, 0x408013D9u, 0x3F870D3Bu }, { 0x3F741395u, 0xBE91E4FCu, 0xBF117F9Cu, 0x3F576E69u }, { 0xC0591492u, 0x403AC6ECu, 0x3F7444D4u }, { 0x40396DD5u, 0x402A7630u, 0x3F763863u }, { 0xBF6D0630u, 0xBF741185u, 0xBD573AB9u, 0x3F23AB88u }, { 0xC020D5A2u, 0xBEE51BEBu, 0xC01EF800u }, { 0xBEC2C1F9u, 0xBF7D1917u, 0xBEAE63BBu, 0x3D3FB5BAu }, { { 0x3E8B035Cu, 0xBFB385B0u, 0x3F81A039u, 0xBFB0CE7Fu, 0xBF0890F4u, 0x402641B8u, 0xC10172C3u }, { 0x404E5BFEu, 0x3E134D98u, 0x3F30739Cu, 0x3FA0E008u, 0x41EB411Fu, 0x42149848u, 0xC2071BD2u } } },
{ 0, 0x3F000000u, 3, { 0xBF19317Au, 0x3F130DF5u, 0xBF63A2A0u, 0xBF13BEC5u }, { 0xC01B26A7u, 0xBF759637u, 0x4028D429u }, { 0xBE95B338u, 0x3EC7ADCBu, 0x3F582A85u, 0xBF3CF725u }, { 0x4068EA33u, 0x3F7F2671u, 0x401E38BDu }, { 0x3F25F0EBu, 0xBF0CDA1Eu, 0xC01AF350u }, { 0xBF3671E1u, 0xBF26D7C5u, 0xBF250BA6u, 0xBF357F86u }, { 0xBFE0943Fu, 0x3EBB4967u, 0x3FCFCBABu }, { 0x3F188EF3u, 0x3F31375Au, 0xBF43FC9Fu, 0x3F250ADCu }, { { 0xBDEA12E8u, 0x3E5DFC82u, 0x3FE6A44Cu, 0xBE5D6416u, 0xC024F716u, 0x3FADB060u, 0x4030BA08u }, { 0x3EDB8B09u, 0xC0289B73u, 0x3EA0F5F0u, 0xBFB4573Cu, 0xC14BFD30u, 0xC0D105E3u, 0x416E1066u } } },
{ 0, 0x3F000000u, 3, { 0xBF20CAE8u, 0xBF5AEFC5u, 0xBED9616Du, 0x3E2F7F92u }, { 0xC090BE3Fu, 0xC0358DC7u, 0xBF8126A3u }, { 0x3F250D4Du, 0xBE4E0211u, 0xBE2FFE94u, 0xBE841EB4u }, { 0xC08258F1u, 0x4072EC7Bu, 0x40875F1Fu }, { 0x3FAA3C22u, 0xBF1BC802u, 0xBEAC1CD0u }, { 0xBEC11B71u, 0x3E9E09C0u, 0x3E9D0B5Cu, 0xBEC1E349u }, { 0x400FB452u, 0x3FA98459u, 0xBFD81CB6u }, { 0x3F1E5926u, 0x3EB27AD9u, 0xBF0024A0u, 0xBED0829Eu }, { { 0x3D2BCD64u, 0x3F3AC3BAu, 0xBE9B2D9Au, 0x3DBD423Fu, 0xC0A84AAFu, 0xC01BC685u, 0xBEEBF2F8u }, { 0x3EDA235Fu, 0xBE19F371u, 0x3EB02ADCu, 0xBB6401D0u, 0xC00AD1B8u, 0xC039068Au, 0xC03A846Fu } } },
{ 0, 0x40100000u, 3, { 0x3F00C56Du, 0xBF56F995u, 0xBDFEB4D1u, 0x3F6D7811u }, { 0xC01C0623u, 0x3D010D0Eu, 0x40824E4Au }, { 0x3F3FCAA9u, 0x3F138F7Fu, 0x3D882AFEu, 0x3F5C12C7u }, { 0xC01BA008u, 0x407435E2u, 0xBF681BE1u }, { 0x3F7305A0u, 0x3F2A8068u, 0xBFA7335Cu }, { 0x3EB68530u, 0x3E558EE6u, 0x3F797E66u, 0x3EAAC8D2u }, { 0xBEB827D7u, 0xBF3896EBu, 0x4014CAEBu }, { 0xBE9E5407u, 0x3EAB121Fu, 0x3EC70068u, 0x3F6DE647u }, { { 0xBE96871Cu, 0xBF1F07FFu, 0x3FA226FCu, 0x3EDA5667u, 0x401B5D51u, 0x3FE7493Fu, 0x41115EDAu }, { 0xBF907E96u, 0x3F41B9D6u, 0x3FCFB9BEu, 0xBE476D6Cu, 0x40E7B949u, 0x41482F52u, 0x4142AA7Bu } } },
{ 0, 0x40100000u, 3, { 0xBF1D0ACEu, 0x3EA8F8C1u, 0xBF281C56u, 0x3E8F6D0Au }, { 0x3FC92C59u, 0x401B87C4u, 0x406A6AA4u }, { 0xBF099B5Bu, 0x3F38942Au, 0xBE9E2BB7u, 0xBE4D00E7u }, { 0xC0781B80u, 0x3EA28D50u, 0xC09F961Fu }, { 0xBF918260u, 0x3F8453A9u, 0x3FBE1D4Bu }, { 0xBF2A4FD6u, 0x3F11DAB0u, 0xBDE4E5C0u, 0xBF16E35Eu }, { 0x3FBEE837u, 0x3FB77EA9u, 0xBE447524u }, { 0x3D7E34D7u, 0x3E755BAFu, 0x3E86DB1Bu, 0x3F07B389u }, { { 0x3F033093u, 0x3EAAB34Eu, 0x3E6736DBu, 0xBF55AB6Cu, 0x40A07541u, 0x402A8534u, 0x3EB2BA48u }, { 0xBE942723u, 0xBE795FCAu, 0x3F089BA0u, 0x3DB840C4u, 0xBF4EAE60u, 0xBF70F192u, 0xC0C994CEu } } },
{ 0, 0x3F800000u, 0, { 0x3F09B202u, 0xBF19744Cu, 0xBF757197u, 0xBF4A2995u }, { 0xBFF6660Cu, 0x3F4E1BADu, 0x4093906Bu }, { 0xBE2A72F2u, 0xBE85E97Fu, 0x3DEA68ABu, 0x3F658942u }, { 0x3F614F20u, 0xC00316EFu, 0x402443BCu }, { 0x403CDBF6u, 0x3FE17FE9u, 0xBF1BFC72u }, { 0xBF27CE77u, 0xBDA078CDu, 0xBF3E395Bu, 0xBF3D19A8u }, { 0xBEA433FAu, 0x40067C43u, 0x3F9B0381u }, { 0xBE279A22u, 0x3F689E24u, 0xBF246FDDu, 0xBEFFD40Au }, { { 0x3F09B202u, 0xBF19744Cu, 0xBF757197u, 0xBF4A2995u, 0xC0D8318Bu, 0x3FE0ADDEu, 0xBF9F07BCu }, { 0x3E96B4ACu, 0xBE6E7C1Eu, 0xBF98622Eu, 0xBF2A6300u, 0xC0D52F54u, 0x40D5BEB8u, 0x3FB90FA8u } } },
{ 0, 0x3F800000u, 0, { 0xBEE7DB2Du, 0x3E14D0B0u, 0x3F478F1Du, 0xBE7052F7u }, { 0x405110DCu, 0x408BD2C8u, 0xBE554370u }, { 0x3F492E0Bu, 0x3F474DE4u, 0xBD41B695u, 0xBF2939B3u }, { 0xC08814DCu, 0xC026AC5Bu, 0xBECD747Bu }, { 0xC03A14D8u, 0xBF82C139u, 0xBD106977u }, { 0xBEA830EAu, 0x3EEF564Au, 0x3E0E93DBu, 0x3D8737A1u }, { 0x3F4D114Du, 0x3EFECE17u, 0xBF04309Du }, { 0xBF2B041Du, 0x3E348446u, 0xBF67AF98u, 0xBF6D7ED7u }, { { 0xBEE7DB2Du, 0x3E14D0B0u, 0x3F478F1Du, 0xBE7052F7u, 0x40840BDEu, 0x40D22C58u, 0x3F975126u }, { 0xBEFF6905u, 0x3E9FF2F4u, 0xBF7890AAu, 0x3EDE95A9u, 0x40912B90u, 0x4114F210u, 0x400D7CF7u } } },
{ 0, 0x3F000000u, 0, { 0xBEA16496u, 0xBF0A433Au, 0x3F02D362u, 0xBF2ED6E5u }, { 0xBF1BF3D5u, 0x3FFA49ABu, 0xBF52A4D6u }, { 0x3F1C5767u, 0xBF4402A5u, 0xBF2804FEu, 0xBEE5E5B6u }, { 0xBFAC9583u, 0x3F18D5A0u, 0xC041205Fu }, { 0x3EB53FD0u, 0x3F9AFF52u, 0x3F6A6433u }, { 0x3F216237u, 0xBF564A7Bu, 0x3EB624A8u, 0x3EE62451u }, { 0x3FF9A7E8u, 0xC00652F9u, 0xC01EC7DEu }, { 0x3F04B3B8u, 0x3E962182u, 0xBF2AB824u, 0x3F3DE2F5u }, { { 0xBEA16496u, 0xBF0A433Au, 0x3F02D362u, 0xBF2ED6E5u, 0x3E593868u, 0x3FD76CAEu, 0xBF6E0D5Au }, { 0x3EF0BEA0u, 0x3F5EE256u, 0x3F4A3BB4u, 0x3ED79441u, 0xC0590178u, 0x40B02898u, 0xC00DF274u } } },
{ 0, 0x3F000000u, 0, { 0x3EDE2613u, 0x3E6B7CBDu, 0x3F056754u, 0x3E638197u }, { 0xBFCCAEB1u, 0xC08F19AEu, 0xBFC9B9AAu }, { 0x3E801047u, 0xBF1FBFBFu, 0xBF240EB2u, 0xBF471934u }, { 0xBD0D0213u, 0x40955334u, 0x4013A492u }, { 0xC03B2F84u, 0xBF3D2DB6u, 0x3F8BDEA1u }, { 0xBF4E5A6Du, 0x3F16F75Bu, 0xBF077FABu, 0xBDE5FE9Fu }, { 0x3FE0F977u, 0x400FB0FCu, 0xC0213619u }, { 0x3F2270AAu, 0x3F4038BEu, 0x3F2D59F5u, 0x3F0C4FA3u }, { { 0x3EDE2613u, 0x3E6B7CBDu, 0x3F056754u, 0x3E638197u, 0xBF94257Fu, 0xC09E3A3Cu, 0xC00D0F10u }, { 0xBDD520B2u, 0x3DBA2B14u, 0xBF603D29u, 0x3E48D8F6u, 0xBF12B124u, 0xC0F0543Eu, 0xBF8CDAFFu } } },
{ 0, 0x40100000u, 0, { 0xBE81EDE3u, 0xBEBF1507u, 0xBF68A70Au, 0xBE53E9E4u }, { 0xBFFF2F0Eu, 0xC05A59A0u, 0x408DF747u }, { 0x3F430CAAu, 0x3F1E8EC8u, 0xBED12BA8u, 0xBF40B6DCu }, { 0xBF399BEFu, 0xC088DD20u, 0xBFBB35CFu }, { 0x3F238D88u, 0xBF8C4B2Fu, 0xBF7E27B8u }, { 0xBEB132BDu, 0x3F2878C5u, 0xBF6E4D42u, 0x3F20B616u }, { 0x3F2960F7u, 0x3E936227u, 0x3F94B57Au }, { 0xBF7681BDu, 0x3F39E7C1u, 0xBED84B8Bu, 0xBE4DBBE8u }, { { 0xBE81EDE3u, 0xBEBF1507u, 0xBF68A70Au, 0xBE53E9E4u, 0xC08489D2u, 0xC0088132u, 0x3FBADF70u }, { 0x3F3FAB3Eu, 0xBF24B119u, 0x3F6556D0u, 0x3E56070Fu, 0xC02621FAu, 0xC0B4B268u, 0xBFC89B6Du } } },
{ 0, 0x40100000u, 0, { 0x3CC91196u, 0x3F4D6F8Fu, 0xBF0B59F3u, 0x3D45962Eu }, { 0x409D5338u, 0xC038373Bu, 0x3FBD3113u }, { 0xBF2C1DC3u, 0x3E099661u, 0xBE05B60Au, 0x3E06452Bu }, { 0xC096CFABu, 0xC03EB267u, 0x40802EBAu }, { 0xBFF5F42Fu, 0xBF993CEDu, 0x403BDA7Fu }, { 0xBEFB4A6Eu, 0x3E23562Au, 0xBE0E1278u, 0xBF7E59FFu }, { 0x3F5C867Cu, 0x3C9260F1u, 0x4027C95Cu }, { 0x3F740CADu, 0xBF5A19C0u, 0x3E34B0D6u, 0xBEA27D7Au }, { { 0x3CC91196u, 0x3F4D6F8Fu, 0xBF0B59F3u, 0x3D45962Eu, 0x4110E66Bu, 0xC118C56Cu, 0x3FFEC91Du }, { 0xBD79495Fu, 0x3EF636D8u, 0x3EEE28F4u, 0xBE1FD22Du, 0x4135FBC1u, 0xC13E72E7u, 0x407B3E30u } } },
{ 0, 0x3F800000u, 1, { 0xBF3C5EEEu, 0x3F55778Cu, 0xBF51693Bu, 0x3B809A65u }, { 0xC02B1F66u, 0x3F3A93DBu, 0xC03A9FFAu }, { 0x3F359FDCu, 0xBE0261EEu, 0xBF18BDE9u, 0x3F19EE3Au }, { 0xC069D8DFu, 0x40186AD8u, 0xC02102E0u }, { 0x4002E7F2u, 0xBFBAFC68u, 0x3F68A99Fu }, { 0x3EB062DCu, 0x3C137F2Fu, 0x3F3DC823u, 0x3DB108E2u }, { 0xBFCBB83Du, 0x3E1677BFu, 0x3F8FB0E1u }, { 0xBE21CC36u, 0x3D47A9BAu, 0xBF5AE39Au, 0xBEFF7D9Du }, { { 0x3F1032DAu, 0x3EABEDE2u, 0xBEB92FC8u, 0x3F5A4DADu, 0xBFBCA830u, 0xC011AD98u, 0x3F8259F0u }, { 0x3F328149u, 0x3E30F330u, 0xBF84A344u, 0xBD7573B0u, 0xC0463965u, 0x3FB8B070u, 0x410108DBu } } },
{ 0, 0x3F800000u, 1, { 0x3F08C3C7u, 0xBEA9CA57u, 0xBF1F2FC3u, 0x3F1640A1u }, { 0xC0968727u, 0xC093C361u, 0xC08C1C64u }, { 0xBF1EE9AEu, 0xBF666A02u, 0xBED41E3Eu, 0xBEED5607u }, { 0x40876690u, 0xC01CDA82u, 0x3F45C549u }, { 0xBF9D2F4Cu, 0x3FAD54AFu, 0xC01431DAu }, { 0xBE9F294Du, 0xBF372570u, 0xBEC79E6Fu, 0xBEF55F71u }, { 0xBE923EFCu, 0x402A529Fu, 0xC0358C62u }, { 0xBF096BBFu, 0x3E5DBAE5u, 0x3F2B6DF8u, 0xBE7699E0u }, { { 0xBF410972u, 0x3E0FFF9Au, 0xBED50D1Au, 0xBF184AA9u, 0xBFF5A818u, 0xC0455B25u, 0xC05A43E2u }, { 0x3E9272A3u, 0x3ED51237u, 0x3F9A4828u, 0xBE73DC4Cu, 0x408E5E30u, 0xC1060498u, 0xC060BC36u } } },
{ 0, 0x3F000000u, 1, { 0xBE908D0Cu, 0xBEBA6493u, 0xBE7F745Fu, 0x3D9CF008u }, { 0x403311FDu, 0xC0991BBBu, 0xC04F659Au }, { 0xBF0E4666u, 0x3E77419Eu, 0x3F056551u, 0xBF5BECDAu }, { 0xC09FC328u, 0xC077102Bu, 0xBF243B0Cu }, { 0x3F935C34u, 0x40358049u, 0xBF480A4Bu }, { 0xBE1C2944u, 0x3F408DA5u, 0x3F090C9Bu, 0x3F4F0178u }, { 0xBFB626CFu, 0x3FA2DFAFu, 0x3FECD9D2u }, { 0xBEE82BA0u, 0xBF035740u, 0x3F5C5313u, 0xBF0F82ACu }, { { 0xBE7D2A95u, 0xBD42C48Cu, 0xBEDB6DD5u, 0x3EDA41EDu, 0x40430B91u, 0xC098FFA4u, 0xC037FDB8u }, { 0x3D5DF328u, 0x3F02C34Du, 0x3F011214u, 0xBE89A21Eu, 0x3FBC1546u, 0xC012EB42u, 0xC0628C89u } } },
{ 0, 0x3F000000u, 1, { 0xBEB21C9Du, 0xBF4B893Du, 0x3F40AAA1u, 0x3E788545u }, { 0xC02469D2u, 0xBF13CD44u, 0xBF0D8ED2u }, { 0xBF775E75u, 0x3EF98DB5u, 0xBF58400Au, 0xBE6EBFA4u }, { 0x40182200u, 0xC00B5439u, 0x402952B0u }, { 0x401A67E5u, 0x3F9CA1C8u, 0xBEE3E245u }, { 0x3F5BE4FAu, 0xBEB76643u, 0x3F7E2484u, 0x3E56FBF4u }, { 0x3E547DCBu, 0x400EAAA0u, 0xC0175B6Cu }, { 0xBEDA968Cu, 0x3F0CDF85u, 0xBF5D1F34u, 0x3F59BE97u }, { { 0xBEC4C020u, 0x3F3CEA9Eu, 0x3F9A6DEBu, 0xBF2EA2D6u, 0xC05EBE48u, 0x3F42F4A8u, 0xBFC3590Du }, { 0xBEECE0A6u, 0xBFFF5B12u, 0x3F521B50u, 0x3EE4F5D1u, 0xC126FA50u, 0x40EDF5BCu, 0xC04BB01Au } } },
{ 0, 0x40100000u, 1, { 0xBDBD5AB7u, 0xBCA2780Du, 0x3F7E0330u, 0xBF546B1Du }, { 0xC099A6EAu, 0x3F218B9Fu, 0x3FABD462u }, { 0x3F39932Fu, 0xBF1D8157u, 0x3F4431C1u, 0x3EA17663u }, { 0x3F498F3Bu, 0xC099BB13u, 0x403F9441u }, { 0xBFD8861Eu, 0x3F80D845u, 0x403D6DE8u }, { 0xBF797CC1u, 0xBE0FF17Du, 0x3F608C4Fu, 0xBEDA3BF2u }, { 0x4001C177u, 0x3FF4F4B2u, 0x3F9278ABu }, { 0x3F7F232Cu, 0x3ED29975u, 0xBF4A66F1u, 0x3F2F0DEEu }, { { 0x3F785B35u, 0xBF42C3F0u, 0xBF941B16u, 0xBF1C0B6Du, 0xBF789258u, 0x409D2A81u, 0x41581B23u }, { 0xBFB7274Eu, 0xBFB93CADu, 0xBF609E71u, 0xBEF41FCBu, 0x41F04A71u, 0x422EA1EBu, 0x41B5674Fu } } },
{ 0, 0x40100000u, 1, { 0x3F00E64Bu, 0xBE6928A6u, 0xBF4EB35Cu, 0xBEEC908Bu }, { 0x3E8719BCu, 0xC01FE0B9u, 0x409BBDD7u }, { 0xBF2EF26Eu, 0xBEE74B5Bu, 0xBF40B22Au, 0x3EFE8704u }, { 0x40522DD3u, 0xC01F7ADEu, 0xBF8B51A4u }, { 0x401527A1u, 0x3F161E8Bu, 0xBFD8F488u }, { 0x3F7E5DCEu, 0x3F0A0B47u, 0xBEC60247u, 0xBDD57F23u }, { 0x3A2482C2u, 0x3F4E9ECDu, 0xBE79283Au }, { 0xBF33B410u, 0xBEAEFB8Eu, 0xBF3E1449u, 0x3BA34B07u }, { { 0x3C426E9Cu, 0xBF553CA8u, 0x3F42B778u, 0xBF2440DAu, 0x3D0A6A60u, 0xC073CB2Cu, 0xC030F010u }, { 0x3FB51D4Au, 0xBF229594u, 0x3E92ADA4u, 0xBDEAB90Cu, 0xC12062EDu, 0xC120A0B3u, 0xC054E8B5u } } },
{ 0, 0x3F800000u, 2, { 0x3E68C2D3u, 0x3E50A153u, 0xBF5BDDFAu, 0x3DE47F51u }, { 0x4034114Cu, 0x3F3AA77Fu, 0xBFCFAAF8u }, { 0x3F1220ACu, 0xBE4DB8D8u, 0x3F163CA4u, 0xBF7A99FFu }, { 0xC083B460u, 0xBED78EA5u, 0xC0853AA6u }, { 0xBFEA2946u, 0x40265B29u, 0x3FD47E4Bu }, { 0xBECE1CA0u, 0xBF455C05u, 0x3EECF2F4u, 0x3ED51155u }, { 0xBFFAF506u, 0x3EA8DF57u, 0x3F58CF78u }, { 0xBF526EEBu, 0xBF46E1ADu, 0xBF732093u, 0x3F2CCEDCu }, { { 0x3E68C2D3u, 0x3E50A153u, 0xBF5BDDFAu, 0x3DE47F51u, 0x408935C7u, 0xBFD5495Au, 0xBF0323E4u }, { 0x3F7C9729u, 0xBFCF89F8u, 0xBEA328C1u, 0x3DA8D320u, 0x41206AB9u, 0xBFE1AC56u, 0xC011AFFEu } } },
{ 0, 0x3F800000u, 2, { 0xBECF150Du, 0xBEA92FEFu, 0xBF6A1380u, 0x3E984A53u }, { 0xC01FEE2Bu, 0x3ED860CDu, 0xBFC0B65Bu }, { 0xBE9B89C7u, 0xBF69F9B9u, 0xBEB31C49u, 0x3F53492Au }, { 0x408DD4D2u, 0x4084755Fu, 0x4060867Bu }, { 0x3FEED26Du, 0x3F44B43Fu, 0x3F2110F6u }, { 0x3EDF9915u, 0x3F6BCAD3u, 0x3E5473B9u, 0x3DEB0A7Au }, { 0x3FAF7AB0u, 0x4004759Au, 0x3E5048DBu }, { 0xBF757B3Eu, 0x3E58DDACu, 0xBE9FB3E6u, 0xBF3E4DD5u }, { { 0xBECF150Du, 0xBEA92FEFu, 0xBF6A1380u, 0x3E984A53u, 0xC034F1AAu, 0xBE36738Eu, 0x3F6E43FAu }, { 0x3FCA7062u, 0x3ECFE92Fu, 0xBD23B0C4u, 0xBF52D0D6u, 0x3FEF2C8Eu, 0xC017F8D6u, 0x41558362u } } },
{ 0, 0x3F000000u, 2, { 0xBF688DEFu, 0x3D8AFF2Bu, 0x3F68FA7Bu, 0x3DEDD675u }, { 0x4055C3BCu, 0xC05149A6u, 0xBFDB008Bu }, { 0x3F691699u, 0x3EC14429u, 0x3EE8252Bu, 0xBED15232u }, { 0x3FD6FBD9u, 0xBFC8C044u, 0xC040DB0Cu }, { 0xC0037DCFu, 0xBFF92FEDu, 0xC01C7D86u }, { 0xBE5F379Au, 0x3F3C2BF8u, 0x3DB77BF3u, 0xBE826E64u }, { 0x3F5963AEu, 0x40222172u, 0xBFB91D8Au }, { 0x3F5643BDu, 0x3F76ABFDu, 0xBE97D143u, 0xBF5FAF6Bu }, { { 0xBF688DEFu, 0x3D8AFF2Bu, 0x3F68FA7Bu, 0x3DEDD675u, 0x40B52F46u, 0xC00B600Du, 0x3D9854D0u }, { 0x3EEF37F2u, 0xBFA9C5DBu, 0xBEB94790u, 0xBFEE41B8u, 0x41300361u, 0x4015AFD5u, 0xC091DC86u } } },
{ 0, 0x3F000000u, 2, { 0x3EB96099u, 0x3F568F22u, 0xBE877298u, 0xBEBF10AFu }, { 0xC0286370u, 0xC0707F06u, 0x3FD21229u }, { 0xBED7BF79u, 0x3F5DD19Du, 0xBE3CC60Au, 0xBF3DFC5Cu }, { 0x3FD5BF91u, 0x409A1BF6u, 0xC06D06C0u }, { 0x40348240u, 0x3F780BD5u, 0x3FCD9D4Cu }, { 0x3F641470u, 0x3DC510D7u, 0x3D8F89F3u, 0xBF37153Fu }, { 0xBF083D07u, 0x3EA0FE9Fu, 0xBFB463E6u }, { 0xBE06BB87u, 0xBE6DC76Fu, 0x3F2CDDEFu, 0xBE9E1550u }, { { 0x3EB96099u, 0x3F568F22u, 0xBE877298u, 0xBEBF10AFu, 0xC073026Du, 0xC01CF0C8u, 0x3FB3F7AFu }, { 0xBE7AAABAu, 0x3E608326u, 0xBF1D0B46u, 0xBF34A588u, 0x3F00CC6Au, 0x40718AAEu, 0x3F29E5C3u } } },
{ 0, 0x40100000u, 2, { 0x3F4AE0CAu, 0xBF0B05B0u, 0x3E9ADBF9u, 0xB9DECE3Eu }, { 0xBFFE0D86u, 0x4032E028u, 0x409C6CBFu }, { 0xBF3F0B81u, 0x3F1F5795u, 0xBF5C379Au, 0xBE946DADu }, { 0x409239BCu, 0x4094D7B8u, 0x4088A335u }, { 0xBF5E4AE1u, 0x3FB894B8u, 0x40373A91u }, { 0xBF5EE8CEu, 0x3F280B3Au, 0x3EE5B287u, 0x3F5D43CCu }, { 0xBE9669CAu, 0x3F5BFF0Fu, 0x3FC938BFu }, { 0xBBDDBF2Cu, 0x3EDF7899u, 0xBF111661u, 0xBF692153u }, { { 0x3F4AE0CAu, 0xBF0B05B0u, 0x3E9ADBF9u, 0xB9DECE3Eu, 0xC00A18B3u, 0x3F7CD448u, 0xC01E2D68u }, { 0xBECD4402u, 0xBC35A12Eu, 0xBF262FCBu, 0xBFACD855u, 0x3FCA2A40u, 0xC05448B5u, 0xBF00B610u } } },
{ 0, 0x40100000u, 2, { 0xBF0F45F9u, 0x3F787683u, 0xBE0C736Bu, 0x3F46BBB8u }, { 0x40967FBBu, 0x40855D46u, 0x3EFC7E0Bu }, { 0xBED88FF7u, 0x3E5ED295u, 0x3F0A5677u, 0x3EE3A857u }, { 0xC0958FABu, 0xC08DE87Cu, 0xC0959303u }, { 0xBFC0A3DCu, 0xBF23A7D3u, 0xBF4581D9u }, { 0x3F04FAC9u, 0x3F07501Eu, 0xBEDCF199u, 0xBE9B03E4u }, { 0x40161B34u, 0x3F92FBC7u, 0xC033BF65u }, { 0xBEAE6AE6u, 0x3F259B67u, 0x3D9490C0u, 0xBF1924B3u }, { { 0xBF0F45F9u, 0x3F787683u, 0xBE0C736Bu, 0x3F46BBB8u, 0x404EAB76u, 0x40B8DDDDu, 0x40F9E2B9u }, { 0xBEA6AFD0u, 0xBF4FEB54u, 0xBD9D24EBu, 0xBF28C4B1u, 0xC0E0438Eu, 0xC05483D0u, 0x41E55740u } } },
{ 1, 0x3F800000u, 3, { 0xBE2E47B3u, 0x3EB4E45Au, 0xBF2517A2u, 0xBEFBD75Fu }, { 0x405A2245u, 0xBF8DE069u, 0xBF601B70u }, { 0x3F7AAEFBu, 0x3F6A8266u, 0xBEC96616u, 0xBF7FF173u }, { 0xC051FA6Fu, 0x409828F6u, 0x400182A0u }, { 0xBFCDFC35u, 0x401B3D1Bu, 0x4010CB9Du }, { 0x3ED93EC5u, 0xBF76E48Au, 0x3D6A0647u, 0xBF0E233Au }, { 0xBFBB8A7Du, 0xBF23B5E0u, 0xBF17E9FAu }, { 0x3F44B8EBu, 0xBE8078C2u, 0xBF2B0CFCu, 0xBF304013u }, { { 0xBF374A13u, 0x3C6A68E0u, 0x3EB0395Fu, 0x3F390FEFu, 0x3FDC733Du, 0xC0623D10u, 0xBFAC655Au }, { 0xBFBD2263u, 0xBF453530u, 0x3C1B0770u, 0xBFCDCB07u, 0xC0366399u, 0xC0291958u, 0x3F947CF0u } } },
{ 1, 0x3F800000u, 3, { 0xBD94DA43u, 0xBEE3B5F0u, 0x3F2DC108u, 0x3E84F488u }, { 0x40142BCEu, 0xC04E7FFFu, 0x3F2AFB77u }, { 0x3EC351AAu, 0xBF674B52u, 0xBE4EF79Cu, 0x3EAC2342u }, { 0xC05DDB66u, 0x409244DDu, 0xC0385289u }, { 0xC012CC1Du, 0x3F901C88u, 0x403A6273u }, { 0x3F0BDA1Du, 0x3D82F311u, 0xBEAF9E69u, 0xBF11A568u }, { 0xC027473Fu, 0xBFD416FCu, 0x3F45A9D2u }, { 0xBF015927u, 0x3F7A1A04u, 0xBF2FC48Cu, 0xBF02D404u }, { { 0x3E95B0E4u, 0x3F1D90C2u, 0xBE729680u, 0x3E1CE371u, 0x4017F9F3u, 0xC0C1D82Cu, 0x3F16B68Fu }, { 0x3EDF6AECu, 0x3F1A21C0u, 0xBE1EA0A9u, 0xBF4696D7u, 0x40A0785Eu, 0xC037F968u, 0x400B4278u } } },
{ 1, 0x3F000000u, 3, { 0xBE3D4C61u, 0x3F685FEBu, 0xBEA2AF65u, 0xBF2182C5u }, { 0x400FD7E9u, 0xC0846E04u, 0xBE95E5E0u }, { 0xBF11C7BAu, 0x3F73AC06u, 0xBDFF26C1u, 0x3C3903E0u }, { 0x4091F2EBu, 0x3FC315DFu, 0x409D991Fu }, { 0xBEDB2A55u, 0x400EED44u, 0xBEC90B1Au }, { 0xBEE4574Bu, 0x3F6BEF1Fu, 0x3E7BDA8Au, 0xBF3696C2u }, { 0x3ED313C0u, 0x3F92C406u, 0xC00BD948u }, { 0xBD0751DCu, 0xBDE56EDBu, 0xBF222809u, 0x3EBF1BE6u }, { { 0x3F6DE74Du, 0xBF855626u, 0x3E9C9FA7u, 0xBEC820F8u, 0x3FDDBEBEu, 0xC031E7D4u, 0xBF604E5Au }, { 0x3E968884u, 0xBE9C3C85u, 0xBF610812u, 0x3F4028A1u, 0x40CE6C66u, 0xC119FE6Eu, 0xC16C3259u } } },
{ 1, 0x3F000000u, 3, { 0xBCD1E1ABu, 0x3E442C23u, 0x3D9270B7u, 0x3E6ACDEBu }, { 0x40328FFDu, 0xBE24EA6Bu, 0x4094CA17u }, { 0x3F4DFCD8u, 0xBDBA4B18u, 0xBF41E458u, 0xBCA6ABB0u }, { 0xBF985316u, 0x4078C405u, 0xBFED9BE5u }, { 0xC0290640u, 0xBFE9FD72u, 0x4011185Eu }, { 0xBF41AC35u, 0x3F51BE35u, 0xBE101496u, 0x3DA7EA84u }, { 0x3FC38B63u, 0x400D1E83u, 0xC0149DA0u }, { 0x3E35BDB4u, 0xBF1F54F0u, 0x3F68CA1Fu, 0x3F0AF4BBu }, { { 0xBE85B0B6u, 0x3E156066u, 0x3DC7C389u, 0xBE170500u, 0x403A3298u, 0xBE64B4CCu, 0x4098EFC9u }, { 0xBE1C0096u, 0xBD43055Cu, 0x3EDEA740u, 0x3DF3382Cu, 0x404320BFu, 0xBE3CF3A7u, 0x40A55CE3u } } },
{ 1, 0x40100000u, 3, { 0xBEFC1006u, 0xBE9DC6CCu, 0x3F73A268u, 0x3F6A9628u }, { 0x402AE0F2u, 0xC09386EBu, 0xC033616Du }, { 0x3E53A5CBu, 0xBF1631F9u, 0x3D0BCEBBu, 0xBF7CB22Bu }, { 0xBEC63F29u, 0xBE73029Eu, 0x405258D2u }, { 0xBF6B5C76u, 0x4019A88Du, 0xBFC4B1A9u }, { 0xBF771881u, 0xBDC52C82u, 0xBF2A4748u, 0x3F68FD0Au }, { 0x3FF75015u, 0x40276BA1u, 0xBFDC0862u }, { 0x3F3FEACCu, 0x3F540715u, 0xBEDEDC0Eu, 0x3F2140B7u }, { { 0xBF8499B3u, 0xBFCEAFA4u, 0x3BD82280u, 0x3F764F82u, 0xBDB60DC0u, 0xC1317276u, 0xC16F19DBu }, { 0xBFE52CFEu, 0x3F032C06u, 0x3FC19142u, 0xC0198CD4u, 0x42155A78u, 0xC1B9E386u, 0xC1732364u } } },
{ 1, 0x40100000u, 3, { 0xBE865AE3u, 0xBF20AC38u, 0x3EB4BB07u, 0xBEA9B253u }, { 0xBFFDF3CBu, 0x3FB90E68u, 0x3FBDDD2Du }, { 0xBF788AD5u, 0xBF7150A2u, 0x3D9338A3u, 0xBF51D3D1u }, { 0xC07AD2DBu, 0x408B0AECu, 0x4090A590u }, { 0x3FA29CA5u, 0x4022AEFDu, 0x3F91EB6Au }, { 0x3F5A4818u, 0xBF7E30F1u, 0xBE826299u, 0xBE89D8E4u }, { 0xBF772824u, 0xBFB0F596u, 0xBF8A4FF1u }, { 0x3EF98EC7u, 0x3F6612E3u, 0x3F581919u, 0xBE610D27u }, { { 0x3E98C40Bu, 0x3F3B7370u, 0x3F48FB04u, 0xBE619B1Cu, 0x3F5BDC1Eu, 0x3FF48B9Du, 0xC0171136u }, { 0xBED4DCC6u, 0x3EFDB5F2u, 0x400F6576u, 0x3F2307B2u, 0x419A2B44u, 0x410782B4u, 0xC12F8394u } } },
{ 1, 0x3F800000u, 0, { 0x3E530A22u, 0xBF09D738u, 0xBEC3DFFFu, 0x3F52B204u }, { 0x3EC5393Du, 0xC083E7C4u, 0xBE3A5EC8u }, { 0x3F13F8DCu, 0x3EB9317Eu, 0xBF46EF5Fu, 0xBE63CFF8u }, { 0xBFE54CF8u, 0xC032A4D1u, 0xC022D17Fu }, { 0x4001B1D6u, 0x3EA3C63Au, 0x3FE8B466u }, { 0xBE53C0CCu, 0x3F2F219Bu, 0xBF7C4B72u, 0xBEA2A705u }, { 0xBFED9124u, 0xBFB1BCDCu, 0x3FB49D02u }, { 0xBF24759Bu, 0x3F4181DAu, 0x3E2247F9u, 0x3F405757u }, { { 0x3E530A22u, 0xBF09D738u, 0xBEC3DFFFu, 0x3F52B204u, 0xBF4EC466u, 0xC0AEEF0Cu, 0x401B659Eu }, { 0x3F7C963Bu, 0x3EB687D6u, 0xBE2CBB84u, 0xBECF3CB7u, 0xC052331Fu, 0xC0BD3AFCu, 0xC009701Du } } },
{ 1, 0x3F800000u, 0, { 0xBF74468Au, 0xBF605BFAu, 0xBF6F224Du, 0x3F79608Cu }, { 0xBE9D2240u, 0xC08CE353u, 0xC007EE3Du }, { 0xBE959111u, 0xBEF6E8ABu, 0xBEBD6B75u, 0xBF05904Du }, { 0xC0210910u, 0xBF2F1D90u, 0xC077AD87u }, { 0x3F16EB40u, 0x3FEBF164u, 0xBFF8DB1Eu }, { 0x3E8C6275u, 0x3EBE49F7u, 0xBEBBAD7Du, 0xBE6C9C2Eu }, { 0xC00A57B3u, 0x3F323BDCu, 0x4037A3F0u }, { 0x3F0DA419u, 0xBF121677u, 0xBE13395Au, 0x3C9D1F34u }, { { 0xBF74468Au, 0xBF605BFAu, 0xBF6F224Du, 0x3F79608Cu, 0x40C3A178u, 0xC1368B2Du, 0xBF40BF50u }, { 0x3DB23FD0u, 0xBDBDC444u, 0x3EA98765u, 0xBFC710FBu, 0xBED0D830u, 0xC1A06DE1u, 0xC01D7B98u } } },
{ 1, 0x3F000000u, 0, { 0xBEE0792Cu, 0x3F6D2EA9u, 0x3F419C42u, 0xBDDBE3C6u }, { 0xC02EE683u, 0x4021B4A3u, 0x3E3E44D4u }, { 0x3EDC096Cu, 0x3F3FCD46u, 0x3F71735Fu, 0x3F11DF0Fu }, { 0xC03CA7E9u, 0x3F097C43u, 0x400B5740u }, { 0xBFF9CD20u, 0x403BD887u, 0xBFAF1082u }, { 0x3F32618Eu, 0xBF787096u, 0xBF536A73u, 0xBF77F516u }, { 0xC01E5B86u, 0xC02362AAu, 0xC021ACD0u }, { 0x3EE49DEDu, 0x3F575055u, 0x3F18294Fu, 0x3F22F66Au }, { { 0xBEE0792Cu, 0x3F6D2EA9u, 0x3F419C42u, 0xBDDBE3C6u, 0xBFF33B40u, 0x402F4C82u, 0x4049CA49u }, { 0x3C3838D0u, 0x3F97CF40u, 0xBECB3FF4u, 0xBFA3DE5Au, 0x40903A47u, 0x4096226Eu, 0x3FA76B80u } } },
{ 1, 0x3F000000u, 0, { 0x3F0EF92Fu, 0xBF26D94Au, 0xBE6AAF8Cu, 0xBF67FBD1u }, { 0xC04CFE42u, 0xC04298FAu, 0xBEBCD43Eu }, { 0x3F58C174u, 0xBE9504EEu, 0x3EC187B3u, 0x3EA3312Eu }, { 0xBFF6B543u, 0xC0925536u, 0xC0357DC0u }, { 0xBFE5FFFBu, 0x3DBB1913u, 0xBF8D2B02u }, { 0x3E9B7C06u, 0x3E65D747u, 0xBF475031u, 0x3F384D6Fu }, { 0xBF0086F4u, 0xBFDDAB15u, 0xC0330F85u }, { 0xBD1FE8A9u, 0xBF42FA6Fu, 0x3F018927u, 0xBF18B3B8u }, { { 0x3F0EF92Fu, 0xBF26D94Au, 0xBE6AAF8Cu, 0xBF67FBD1u, 0xC08B5911u, 0xC05C4F63u, 0x3F507605u }, { 0xBF66FE6Cu, 0xBEB2C2C2u, 0xBCD75790u, 0xBF5D61BEu, 0xC0995C02u, 0xC10A5E70u, 0x40AFC8AAu } } },
{ 1, 0x40100000u, 0, { 0xBF285195u, 0xBE174A6Du, 0xBF585CF0u, 0x3F51D109u }, { 0x4063B155u, 0xC0864AC8u, 0xC025090Au }, { 0x3F2A90FDu, 0x3F09274Bu, 0x3D925B22u, 0x3EB5349Bu }, { 0xBF17DB5Cu, 0x3F8A93EAu, 0x3F98DFB7u }, { 0x3FBB7495u, 0x40352EEEu, 0x3DE9B9EFu }, { 0xBF3D7C8Bu, 0x3F0A8192u, 0x3F72C093u, 0x3D98AD49u }, { 0x40269704u, 0x3FC184E9u, 0x3FC08969u }, { 0x3F15F7BFu, 0x3E5CF599u, 0xBF48569Cu, 0xBF3ACD61u }, { { 0xBF285195u, 0xBE174A6Du, 0xBF585CF0u, 0x3F51D109u, 0x4170E25Bu, 0xC12AA964u, 0xC049D2ADu }, { 0x3F417082u, 0xBE046AE0u, 0xBEFD1DA2u, 0x3F5E213Au, 0x41B6334Au, 0xC1998EFCu, 0xBF04EE34u } } },
{ 1, 0x40100000u, 0, { 0x3F3C962Cu, 0x3E91D4E3u, 0xBF284096u, 0xBF076C08u }, { 0x406FC927u, 0xC06FC559u, 0x3F5B0DB9u }, { 0x3F055ACEu, 0xBED5ED9Au, 0xBF779A34u, 0xBE6B3337u }, { 0x3FDAFB3Du, 0x40721139u, 0x3F7BDCC7u }, { 0xC0244C0Au, 0xBFE819F2u, 0x3D602F11u }, { 0xBEFDE324u, 0xBF3C1075u, 0xBF30DEC0u, 0x3F0FABF2u }, { 0xBF67F11Bu, 0x40070C3Du, 0x402F5EDBu }, { 0xBEA06339u, 0x3EB19139u, 0xBF149CE1u, 0xBEE320CDu }, { { 0x3F3C962Cu, 0x3E91D4E3u, 0xBF284096u, 0xBF076C08u, 0x403B621Eu, 0xC0F4420Eu, 0x4116D266u }, { 0xBF7EAF20u, 0x3F069718u, 0x3E53618Cu, 0xBF476664u, 0xC1475DC4u, 0xC15C924Du, 0x40F85BC4u } } },
{ 1, 0x3F800000u, 1, { 0xBF0E52C8u, 0x3C69CEB9u, 0xBF4BF877u, 0x3F1CE82Du }, { 0x3FFDBCFCu, 0xC06FE563u, 0xC00DF03Bu }, { 0xBEF8B233u, 0x3F635C46u, 0x3F4CB185u, 0xBF11E556u }, { 0x3F9CB951u, 0x4020EF1Fu, 0x3E5EF3EDu }, { 0xBFA8A594u, 0x400675D0u, 0xC03A8629u }, { 0x3F5F9066u, 0x3F01B4FCu, 0x3E797D0Bu, 0xBEE94C8Eu }, { 0xC01D6377u, 0xBF2A4CD6u, 0x3DA12E29u }, { 0x3E157537u, 0xBF22F9C2u, 0xBE830CF5u, 0xBF3016BBu }, { { 0x3F990E7Cu, 0xBE833B69u, 0x3E5F7A7Au, 0x3EC946FDu, 0x3FA6B94Bu, 0xC0B1BA02u, 0xC0DC537Eu }, { 0xBFA2B648u, 0xBF112047u, 0x3F904FDCu, 0x3ED1E054u, 0x3DFD4EA0u, 0xC15B9D16u, 0x404D9544u } } },
{ 1, 0x3F800000u, 1, { 0xBF739326u, 0x3CC17CE9u, 0xBD14B0ECu, 0x3E592DCFu }, { 0xBF2944A8u, 0x3FD31685u, 0x408DBA15u }, { 0xBE4EF508u, 0x3E2C75A3u, 0x3D2149F3u, 0xBCDFC0C0u }, { 0xC09F0E91u, 0xC0852C3Bu, 0x3F489542u }, { 0x3F06E01Bu, 0xBF49AF7Eu, 0xBF6E760Bu }, { 0xBF4B5CD0u, 0x3EB5AFAAu, 0x3E92A8D6u, 0xBF42E4D4u }, { 0xC0020F8Cu, 0xBFC495CBu, 0xBFA58CB9u }, { 0x3E008A9Cu, 0x3F18343Eu, 0x3E079D6Cu, 0x3F1444F7u }, { { 0x3F135554u, 0x3EB7A217u, 0xBE6C0368u, 0xBF6A4F85u, 0xBE590664u, 0x3FF5EF44u, 0x40B28FC1u }, { 0x3E638969u, 0xBE0F62C5u, 0x3E0F057Au, 0x3DB84B4Bu, 0xC0B821F5u, 0xC06DA27Eu, 0x410EB969u } } },
{ 1, 0x3F000000u, 1, { 0xBEC39337u, 0xBE700A3Cu, 0x3EAD22C0u, 0x3F2D7E38u }, { 0x40849439u, 0x400D0E64u, 0xC04F1036u }, { 0x3E37CFBDu, 0xBF5532C4u, 0xBF21B8D9u, 0xBE011F98u }, { 0x3F9E2449u, 0xC03A0D83u, 0xBF39B428u }, { 0xBF9AEE52u, 0xC0172A7Cu, 0x4015CF89u }, { 0x3F280F6Cu, 0x3F11DA72u, 0xBF3625AFu, 0x3F278AD2u }, { 0x4024988Cu, 0xC0257CAEu, 0xBFC25178u }, { 0xBED4A136u, 0xBF1A0103u, 0x3DF6213Au, 0x3EED291Cu }, { { 0x3E2D18BAu, 0x3E3B4CDCu, 0xBEA635ABu, 0x3F88C32Eu, 0x40623B23u, 0x3FF8278Au, 0xC002534Au }, { 0xBE5C9882u, 0xBF5D471Bu, 0xBF4EC207u, 0xBE5F0C30u, 0x3E1D0260u, 0xBFD9CAECu, 0xC0C38F81u } } },
{ 1, 0x3F000000u, 1, { 0x3E317E66u, 0x3F4B5DAAu, 0x3E552C3Eu, 0x3EEDE7A2u }, { 0xBF039900u, 0x401BB560u, 0xC06548DDu }, { 0xBD92CD1Fu, 0xBEFD4DBDu, 0xBE81410Cu, 0xBE76A109u }, { 0xC087DEFFu, 0x3E17E988u, 0xC0372BF7u }, { 0x3DF3BB24u, 0x3E518CCBu, 0xBEB1661Bu }, { 0x3F4104BAu, 0x3D80E86Eu, 0x3EB2ABA2u, 0x3F5F3F79u }, { 0xBEB33D4Du, 0x400B8BCAu, 0xBF09B255u }, { 0xBF768AA1u, 0xBF0CCD2Au, 0xBF00F006u, 0x3E97A67Bu }, { { 0x3F43FF49u, 0x3F518836u, 0xBE7A3879u, 0x3E1B8460u, 0xBF2BEB83u, 0x4020ADBDu, 0xC0602514u }, { 0xBF05D641u, 0xBD7BCB00u, 0xBE996445u, 0x3EB91C84u, 0x3F1355F0u, 0xBF8195C1u, 0x400393C5u } } },
{ 1, 0x40100000u, 1, { 0xBF283E01u, 0x3E984544u, 0xBEBF5E99u, 0x3E99CBC3u }, { 0x40823B88u, 0x3FCAB87Au, 0x3FFBED4Fu }, { 0x3E2DD082u, 0x3E1469E5u, 0xBD29FC40u, 0x3F025C64u }, { 0xBF5F3035u, 0xBF762472u, 0x4044A9E2u }, { 0x3FBD49ABu, 0xC03C1CE2u, 0xBF8DDB1Cu }, { 0xBEB74101u, 0xBEEBF6DDu, 0xBF417B67u, 0xBD60AC53u }, { 0xBE76C875u, 0x3F3ED565u, 0xBF27D245u }, { 0x3F12AE5Au, 0x3EF68632u, 0x3F5DB9F7u, 0xBF71B547u }, { { 0xBEEFDEBBu, 0xBF04863Au, 0x3E4FAB6Eu, 0xBECB5403u, 0x408F4845u, 0x3FDA298Cu, 0x40FA0B50u }, { 0xBEA0B5BFu, 0xBE9CC48Du, 0x3E0F1302u, 0xBD20CBB4u, 0x409D633Cu, 0xBEFFA5F4u, 0x40F03250u } } },
{ 1, 0x40100000u, 1, { 0x3F25C533u, 0x3F6F8398u, 0x3CD12658u, 0x3D3BED6Cu }, { 0x4082445Bu, 0xBFE9A651u, 0x404CEE69u }, { 0xBD96D2AAu, 0x3F34AD09u, 0x3EF3EB2Eu, 0xBD605003u }, { 0xC05BB922u, 0xC007C519u, 0x3FAB38D6u }, { 0xBF83AC12u, 0xBFEB9E9Au, 0xBFB1391Fu }, { 0xBD28E0CAu, 0x3E23B845u, 0x3EEBDB3Eu, 0xBF6EA3B1u }, { 0x3F1666D2u, 0xC01E7BDEu, 0x3E38CCF5u }, { 0xBF11173Cu, 0x3EDCBE8Bu, 0xBD4B665Fu, 0xBF43F44Cu }, { { 0xBE36E56Fu, 0xBF95035Eu, 0x3E0EC99Eu, 0xBE35AD41u, 0xBE832E70u, 0xC0CFCDAAu, 0x40DCF4A3u }, { 0xBF215359u, 0x3C5AE170u, 0xBE9B9F45u, 0x3F407355u, 0x4026C1CDu, 0xC13C563Fu, 0x4152BB6Du } } },
{ 1, 0x3F800000u, 2, { 0x392FC871u, 0xBDC548F8u, 0x3ED931C7u, 0x3F4C8940u }, { 0x409F9F70u, 0x40965046u, 0xC08D4A74u }, { 0x3D6835E1u, 0xBE5C4B15u, 0x3F21321Bu, 0xBEFBE4D9u }, { 0x40857A40u, 0xC068B343u, 0xC06497EAu }, { 0xBFBA4CCCu, 0xBF6A92B5u, 0x3FECBF76u }, { 0xBE70FBD2u, 0x3E080391u, 0x3F35D841u, 0xBF4EB645u }, { 0xBB0E6BD3u, 0x3F52D3C1u, 0x3FAA0427u }, { 0xBF650484u, 0xBE851F7Cu, 0x3E953AA5u, 0xBF3E7D8Bu }, { { 0x392FC871u, 0xBDC548F8u, 0x3ED931C7u, 0x3F4C8940u, 0x40957ACAu, 0x40485AEFu, 0xC0446394u }, { 0x3F19F459u, 0xBD1D7BE0u, 0xBF07FDB0u, 0x3EEC04DEu, 0x4121FB21u, 0x409D6E11u, 0xC08E11E6u } } },
{ 1, 0x3F800000u, 2, { 0xBF7A5CF9u, 0x3F1695B6u, 0xBF3C0978u, 0x3CD20D48u }, { 0xBFCA68D5u, 0xBF266442u, 0x405C5F27u }, { 0x3F0FD8A0u, 0x3EE049D9u, 0x3F306D92u, 0x3F3D77B9u }, { 0x402B5599u, 0xC08A7BB6u, 0x3F46F75Bu }, { 0xC000FB39u, 0x401A2CD7u, 0x3FC97AC4u }, { 0x3F1E7547u, 0x3DBAA00Fu, 0xBE4ADD93u, 0xBEB12950u }, { 0x4027BFA5u, 0x4014B3B2u, 0xC00715D3u }, { 0xBF581DE2u, 0x3EA03EC1u, 0xBEDAB10Au, 0x3D82E9D0u }, { { 0xBF7A5CF9u, 0x3F1695B6u, 0xBF3C0978u, 0x3CD20D48u, 0xC0063E60u, 0xC01388C8u, 0xC032CB39u }, { 0xBF16BC84u, 0x3FB2563Bu, 0x3E3023D4u, 0xBF3401B2u, 0x40301CDDu, 0xC073682Fu, 0xC0D9EA3Eu } } },
{ 1, 0x3F000000u, 2, { 0x3E6AA209u, 0xBE00FA7Du, 0xBF49EC81u, 0x3C1A0D72u }, { 0x4081191Fu, 0xBEC0908Du, 0xC05CCB1Du }, { 0xBF3A62FFu, 0x3F6E202Fu, 0x3EBF6B0Au, 0xBEB25F75u }, { 0x3DBF7483u, 0x40759A15u, 0xBF337BF9u }, { 0xBFDC4C81u, 0x3FFFB7E3u, 0x3F08E8E0u }, { 0xBC98F1EAu, 0xBF204560u, 0x3E91CB5Fu, 0x3DAD33E7u }, { 0xBFB11A54u, 0x403FC589u, 0xBDB72E4Bu }, { 0x3E847B63u, 0x3E4F3CF5u, 0xBF2D491Fu, 0xBF6BA110u }, { { 0x3E6AA209u, 0xBE00FA7Du, 0xBF49EC81u, 0x3C1A0D72u, 0x408CBC27u, 0xBF6B6EEAu, 0xC0328F2Du }, { 0xBF59BB68u, 0x3E02E958u, 0xBF46B43Cu, 0xBF062412u, 0x40901816u, 0xC0861427u, 0xBF24B1F8u } } },
{ 1, 0x3F000000u, 2, { 0x3F408CC3u, 0xBE9F5B09u, 0xBDD69F0Fu, 0xBF10E93Au }, { 0x3F65D2C0u, 0x401D05F1u, 0xBF94E8B3u }, { 0xBDDE9060u, 0xBF1AC863u, 0x3E552632u, 0xBEC3FB75u }, { 0x4038872Bu, 0x406B0B44u, 0xC09FB8FAu }, { 0x3F088DBBu, 0xBF76E170u, 0xBFBD4037u }, { 0xBF0510EEu, 0xBEC85F6Fu, 0xBF519A8Au, 0xBF3DFAD4u }, { 0xC02C306Au, 0xBF8524F5u, 0x40236415u }, { 0xBEA4905Cu, 0x3F30B50Du, 0xBF230124u, 0x3ED9D229u }, { { 0x3F408CC3u, 0xBE9F5B09u, 0xBDD69F0Fu, 0xBF10E93Au, 0x3F9F46A4u, 0x3FE1343Au, 0xBF2CE6CAu }, { 0xBB73CB60u, 0x3E3A4F2Bu, 0xBEEFA945u, 0xBF22FA52u, 0x3FB241DBu, 0xC08259F9u, 0xC05B81D6u } } },
{ 1, 0x40100000u, 2, { 0x3F682333u, 0xBF14A087u, 0x3E2C9B92u, 0x3EE76C31u }, { 0xC09EFB5Du, 0xC0965C0Bu, 0xBF309909u }, { 0xBEF70791u, 0xBE8406E6u, 0x3D951CCEu, 0x3F0E635Eu }, { 0xC02156ABu, 0x405A278Cu, 0xC010B3D9u }, { 0x3F808094u, 0x401583F2u, 0xC016E4C3u }, { 0x3F661418u, 0xBF7F185Eu, 0xBF55FF4Du, 0x3E5F2077u }, { 0x3F22A136u, 0x40202A3Cu, 0x3F7A098Fu }, { 0xBF22AEDAu, 0x3DA5BA5Fu, 0x3F6F4428u, 0x3F24AC35u }, { { 0x3F682333u, 0xBF14A087u, 0x3E2C9B92u, 0x3EE76C31u, 0xC10A5DEEu, 0xC03E3370u, 0x41161BC3u }, { 0xBF2AC43Eu, 0xBEB6FE5Au, 0xBDD9ACE0u, 0x3F6E5B6Au, 0xC1791B01u, 0x3FFFFC78u, 0x4182BD5Du } } },
{ 1, 0x40100000u, 2, { 0x3EDBD0DBu, 0xBF001820u, 0xBDEF48F2u, 0x3E25EC37u }, { 0xC06FDD55u, 0xBF79CF89u, 0xBF049C69u }, { 0xBE6FB5E4u, 0x3F0BB4DEu, 0xBEF4E115u, 0xBC982291u }, { 0x408E9A91u, 0x3F827BCCu, 0x3E4D941Cu }, { 0x3FB180E1u, 0x3F986ABAu, 0x3E9D35CBu }, { 0xBE820024u, 0x3E21B4C3u, 0x3F59A269u, 0xBE27AA7Cu }, { 0x3F7B67EBu, 0xBFB4F799u, 0x3E062477u }, { 0xBECC431Fu, 0xBEC7CD19u, 0xBF18013Du, 0xBF26CB10u }, { { 0x3EDBD0DBu, 0xBF001820u, 0xBDEF48F2u, 0x3E25EC37u, 0xC0A4A8FCu, 0xC00F4E48u, 0x3DB49C98u }, { 0xBEF227A2u, 0xBE4EE8EAu, 0xBE3B0773u, 0x3D525978u, 0xC0C35E08u, 0xC0899161u, 0xBEFB8120u } } },
	};
	int rowIndex = 0;
	for (const Row &r : rows)
	{
		INFO("row " << rowIndex++ << " arm " << r.arm << " mask " << r.mask);
		HTreeClass tree = makeTree({ makePivot("ROOTTRANSFORM", 0xFFFFFFFFu, 0, 0, 0),
			makePivotQ("A", 0, bitsToFloat(r.at[0]), bitsToFloat(r.at[1]), bitsToFloat(r.at[2]), bitsToFloat(r.aq[0]), bitsToFloat(r.aq[1]), bitsToFloat(r.aq[2]), bitsToFloat(r.aq[3])),
			makePivotQ("B", 1, bitsToFloat(r.bt[0]), bitsToFloat(r.bt[1]), bitsToFloat(r.bt[2]), bitsToFloat(r.bq[0]), bitsToFloat(r.bq[1]), bitsToFloat(r.bq[2]), bitsToFloat(r.bq[3])) });
		tree.Set_Scale_Factor(bitsToFloat(r.scale));
		FakeAnim anim;
		anim.Pivots = 3;
		const Row *rp = &r;
		anim.Trans = [rp](int p, float) {
			if (p == 1) return Vector3(bitsToFloat(rp->at_t[0]), bitsToFloat(rp->at_t[1]), bitsToFloat(rp->at_t[2]));
			if (p == 2) return Vector3(bitsToFloat(rp->bt_t[0]), bitsToFloat(rp->bt_t[1]), bitsToFloat(rp->bt_t[2]));
			return Vector3();
		};
		anim.Orient = [rp](Quaternion &q, int p, float) {
			if (p == 1 && (rp->mask & 1)) { q.Set(bitsToFloat(rp->at_q[0]), bitsToFloat(rp->at_q[1]), bitsToFloat(rp->at_q[2]), bitsToFloat(rp->at_q[3])); return true; }
			if (p == 2 && (rp->mask & 2)) { q.Set(bitsToFloat(rp->bt_q[0]), bitsToFloat(rp->bt_q[1]), bitsToFloat(rp->bt_q[2]), bitsToFloat(rp->bt_q[3])); return true; }
			return false;
		};
		HTreePose pose;
		Matrix3D root;
		tree.Anim_Pose(root, &anim, 0.0f, pose, r.arm == 0 ? HTREE_POSE_ARM_MOTION_CHANNEL : HTREE_POSE_ARM_CLASSIC);
		REQUIRE(pose.World.size() == 3);
		for (int p = 0; p < 2; ++p)
		{
			INFO("pivot " << (p + 1));
			for (int k = 0; k < 4; ++k) CHECK(floatBits(pose.World[(size_t)p + 1].Q[k]) == r.world[p][k]);
			for (int k = 0; k < 3; ++k) CHECK(floatBits(pose.World[(size_t)p + 1].T[k]) == r.world[p][4 + k]);
		}
	}
}

TEST_CASE("Anim_Update of a motion-channel animation (arm 0x562BB0 lines 84-222) matches the binary's pseudocode")
{
	HTreeClass tree = goldenTree();
	FakeAnim anim = goldenAnim(true);
	HTreePose pose;
	tree.Anim_Pose(Matrix3D(), &anim, 0.0f, pose, HTREE_POSE_ARM_MOTION_CHANNEL);
	checkGolden(pose, kArmA, "arm A");
	FakeAnim noRot = goldenAnim(false);
	tree.Anim_Pose(Matrix3D(), &noRot, 0.0f, pose, HTREE_POSE_ARM_MOTION_CHANNEL);
	checkGolden(pose, kArmANoRot, "arm A, no rotation channel");
	CHECK(pose.Arm == HTREE_POSE_ARM_MOTION_CHANNEL);
}

TEST_CASE("Anim_Update of a classic animation (arm 0x562BB0 lines 276-397) matches the binary's pseudocode")
{
	HTreeClass tree = goldenTree();
	FakeAnim anim = goldenAnim(true);
	HTreePose pose;
	tree.Anim_Pose(Matrix3D(), &anim, 0.0f, pose); // a FakeAnim is not a motion-channel animation: the classic arm
	checkGolden(pose, kArmB, "arm B");
	CHECK(pose.Arm == HTREE_POSE_ARM_CLASSIC);
	FakeAnim noRot = goldenAnim(false);
	tree.Anim_Pose(Matrix3D(), &noRot, 0.0f, pose);
	checkGolden(pose, kArmBNoRot, "arm B, no rotation channel");
}

TEST_CASE("The matrix of a pivot is built from its quaternion as HLodClass::Update_Sub_Object_Transforms does (0x59D640), unnormalised")
{
	// q = (0.1, 0.2, 0.3, 0.9), t = (1,2,3): rows evaluated from the pseudocode in float32.
	const float expected[3][4] = { { 0.74000001f, -0.5f, 0.419999987f, 1.0f },
		{ 0.580000043f, 0.800000012f, -0.0599999875f, 2.0f },
		{ -0.299999982f, 0.300000012f, 0.900000036f, 3.0f } };
	HTreeClass tree = goldenTree();
	HTreePose pose;
	tree.Base_Pose(Matrix3D(), pose);
	for (int r = 0; r < 3; ++r)
		for (int c = 0; c < 4; ++c)
		{
			INFO("row " << r << " column " << c);
			CHECK(pose.Transform[1].Row[r][c] == doctest::Approx(expected[r][c]).epsilon(2e-6));
		}
}

TEST_CASE("The root matrix becomes a quaternion + translation (FUN_00b17b40) and is composed through the chain")
{
	// root: 90 degrees about Z and translation (10, 20, 30). Child at (1,0,0) in the root frame lands at (10, 21, 30).
	HTreeClass tree = makeTree({ makePivot("ROOTTRANSFORM", 0xFFFFFFFFu, 0, 0, 0), makePivot("A", 0, 1, 0, 0) });
	Matrix3D root;
	root.Row[0][0] = 0; root.Row[0][1] = -1; root.Row[1][0] = 1; root.Row[1][1] = 0;
	root.Set_Translation(Vector3(10, 20, 30));
	HTreePose pose;
	tree.Base_Pose(root, pose);
	CHECK(pose.World[0].Q[2] == doctest::Approx(0.70710678f).epsilon(1e-6));
	CHECK(pose.World[0].Q[3] == doctest::Approx(0.70710678f).epsilon(1e-6));
	CHECK(pose.World[0].Q[0] == doctest::Approx(0.0f));
	Vector3 a = pose.Transform[1].Get_Translation();
	CHECK(a.X == doctest::Approx(10.0f).epsilon(1e-5));
	CHECK(a.Y == doctest::Approx(21.0f).epsilon(1e-5));
	CHECK(a.Z == doctest::Approx(30.0f).epsilon(1e-5));
}

TEST_CASE("BFME2_Nlerp is bit-identical to the real BFME2 1.06 function 0xB17550 (inverse square root 0x44233A included)")
{
	// Expected values: tools/retail_oracle called the real function in game.dat (BFME2 1.06) on a 32-bit helper process, x87 precision
	// control 24 bits as retail's setFPMode leaves it. Each row: a, b, t, result, as float bit patterns. 15 rows: hand-picked branches
	// (dot < 0, zero length, non-unit input, identical inputs) and six pseudo-random pairs. Nothing here was computed by the code under test.
	struct Row { std::uint32_t a[4], b[4], t, out[4]; };
	const Row rows[] = {
{ { 0x00000000u, 0x00000000u, 0x00000000u, 0x3F800000u }, { 0x00000000u, 0x00000000u, 0x3F3504F3u, 0x3F3504F3u }, 0x3F000000u, { 0x00000000u, 0x00000000u, 0x3EC3EF15u, 0x3F6C835Fu } },
{ { 0x00000000u, 0x00000000u, 0x00000000u, 0x3F800000u }, { 0x00000000u, 0x00000000u, 0x3F3504F3u, 0x3F3504F3u }, 0x3E800000u, { 0x00000000u, 0x00000000u, 0x3E3FDCBFu, 0x3F7B775Eu } },
{ { 0x3DCCCCCDu, 0x3E4CCCCDu, 0x3E99999Au, 0x3F666666u }, { 0x3F000000u, 0xBF000000u, 0x3F000000u, 0x3F000000u }, 0x3E99999Au, { 0x3E7E05ECu, 0xBC38BE84u, 0x3ECFD64Eu, 0x3F612828u } },
{ { 0x3DCCCCCDu, 0x3E4CCCCDu, 0x3E99999Au, 0x3F666666u }, { 0xBF000000u, 0x3F000000u, 0xBF000000u, 0xBF000000u }, 0x3E99999Au, { 0x3E7E05ECu, 0xBC38BE84u, 0x3ECFD64Eu, 0x3F612828u } },
{ { 0x3F000000u, 0x3F000000u, 0x3F000000u, 0x3F000000u }, { 0x3F000000u, 0x3F000000u, 0x3F000000u, 0x3F000000u }, 0x3F666666u, { 0x3EFFFFFFu, 0x3EFFFFFFu, 0x3EFFFFFFu, 0x3EFFFFFFu } },
{ { 0x3E4CCCCDu, 0xBDCCCCCDu, 0x3ECCCCCDu, 0x3F333333u }, { 0x3F19999Au, 0x3E99999Au, 0xBE4CCCCDu, 0x3F000000u }, 0x3F400000u, { 0x3F25F0AEu, 0x3E84C08Bu, 0xBD84C08Cu, 0x3F3688C0u } },
{ { 0x3F800000u, 0x00000000u, 0x00000000u, 0x00000000u }, { 0xBF19999Au, 0x3F4CCCCDu, 0x00000000u, 0x00000000u }, 0x3E800000u, { 0x3F79E765u, 0xBE5E2305u, 0x00000000u, 0x00000000u } },
{ { 0x40400000u, 0x3F800000u, 0xC0000000u, 0x3F000000u }, { 0x3E800000u, 0x3E000000u, 0x41000000u, 0x3F800000u }, 0x3F19999Au, { 0x3E3BF1B0u, 0x3D68B154u, 0xBF7A9798u, 0xBD8F320Eu } },
{ { 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u }, { 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u }, 0x3F000000u, { 0x00000000u, 0x00000000u, 0x00000000u, 0x00000000u } },
{ { 0xBF4302B8u, 0x3BA4DF5Eu, 0x3CC1B40Du, 0x3F3851FFu }, { 0xBF4B732Fu, 0xBF0DADA6u, 0x3E4EE929u, 0x3DE7AA6Fu }, 0x3F488B28u, { 0xBF554299u, 0xBEEA326Cu, 0x3E30FE22u, 0x3E8474C9u } },
{ { 0x3DC3D5F2u, 0x3EEC126Au, 0x3F094669u, 0x3F008679u }, { 0x3E3131D7u, 0xBF05275Cu, 0x3E69E3EFu, 0xBF474D8Au }, 0x3F511746u, { 0xBE0CAB7Au, 0x3F10AA11u, 0xBDC8A38Cu, 0x3F4EBE18u } },
{ { 0xBDCE36BCu, 0x3F212893u, 0x3EBD9F52u, 0x3EB7B1BCu }, { 0xBF145D6Fu, 0xBEFE7359u, 0x3F75B712u, 0x3F5BCF7Au }, 0x3F4E1D63u, { 0xBEC46567u, 0xBE602315u, 0x3F2AA4B1u, 0x3F19BBECu } },
{ { 0x3F7FD2EDu, 0x3CDE9E4Du, 0xBF58EC99u, 0xBEA36F9Bu }, { 0x3DEB5141u, 0xBED4DE01u, 0xBDAE4B38u, 0x3F40C14Eu }, 0x3F3437BEu, { 0x3E92A492u, 0x3ECD2D61u, 0xBE8243C1u, 0xBF550E9Fu } },
{ { 0x3E1E9023u, 0xBF73555Fu, 0x3ED7EC4Cu, 0xBE8BBB96u }, { 0xBE1F4C6Fu, 0xBE4F4FEEu, 0x3E2F8CD3u, 0xBB4A7EC8u }, 0x3DEE7C42u, { 0x3DF692C4u, 0xBF603187u, 0x3ECBDFDBu, 0xBE7AD39Du } },
{ { 0x3C505D0Du, 0x3F00133Du, 0x3E4D2370u, 0xBC92F3EDu }, { 0x3F5560DDu, 0xBEF7262Du, 0xBE23E8C9u, 0x3F5A3B3Au }, 0x3EF55863u, { 0xBF016DA9u, 0x3F2213E2u, 0x3E6E9DDEu, 0xBF09AEC3u } },
	};
	for (const Row &r : rows)
	{
		Quaternion a(bitsToFloat(r.a[0]), bitsToFloat(r.a[1]), bitsToFloat(r.a[2]), bitsToFloat(r.a[3]));
		Quaternion b(bitsToFloat(r.b[0]), bitsToFloat(r.b[1]), bitsToFloat(r.b[2]), bitsToFloat(r.b[3]));
		Quaternion q;
		BFME2_Nlerp(q, a, b, bitsToFloat(r.t));
		INFO("t bits " << r.t);
		CHECK(floatBits(q.X) == r.out[0]);
		CHECK(floatBits(q.Y) == r.out[1]);
		CHECK(floatBits(q.Z) == r.out[2]);
		CHECK(floatBits(q.W) == r.out[3]);
	}
}

TEST_CASE("pc24 fast path (BFME2_Mul24 / BFME2_Sub24) equals the NumericState facade bit for bit, edge cases included")
{
	// The facade (NumericState::pc24*) is the reference: it emulates the x87 PC24 operation with exact integer arithmetic. The fast path
	// takes the plain float result only in the normal range; everything else is delegated, so every input must agree.
	std::uint32_t state = 0x12345678u;
	auto next = [&]() {
		state ^= state << 13;
		state ^= state >> 17;
		state ^= state << 5;
		return state;
	};
	std::vector<float> values = { 0.0f, -0.0f, 1.0f, -1.0f, 1.5f, 3.0f, 1e-20f, -1e-20f, 1e-30f, 3e-39f, -3e-39f, 1.17549435e-38f, 1e-45f, 1e20f, 3e38f,
		3.4028235e38f, -3.4028235e38f, 0.70710678f, 0.99999994f };
	for (int i = 0; i < 4000; ++i)
	{
		const std::uint32_t b = next();
		if (((b >> 23) & 0xFF) == 0xFF) continue; // no NaN / infinity operands: the facade documents its NaN handling separately
		values.push_back(bitsToFloat(b));
		// values with exponents spread around the float range
		values.push_back(bitsToFloat((b & 0x807FFFFFu) | (((next() % 80u) + 40u) << 23)));
		values.push_back(bitsToFloat((b & 0x807FFFFFu) | ((next() % 8u) << 23))); // zero / subnormal / tiny exponents
	}
	size_t compared = 0, mismatched = 0;
	for (size_t i = 0; i < values.size(); ++i)
	{
		for (int k = 0; k < 6; ++k)
		{
			const float a = values[i];
			const float b = values[(i * 7 + (size_t)k * 13 + next() % values.size()) % values.size()];
			++compared;
			if (floatBits(BFME2_Mul24(a, b)) != floatBits(NumericState::pc24Mul(a, b))) ++mismatched;
			if (floatBits(BFME2_Sub24(a, b)) != floatBits(NumericState::pc24Sub(a, b))) ++mismatched;
		}
	}
	CHECK(compared > 70000);
	CHECK(mismatched == 0);
}

TEST_CASE("Raw animation: a fade channel reads 1.0 outside its frame range (BFME2 binary), its value inside")
{
	HTreeClass tree = chainTree();
	// fade of pivot 1 is keyed on frames 1..2 only
	HRawAnimClass anim = loadRaw(w3dsynth::rawAnimChunk("RAWF", "T_SKL", 4, 30, { { ANIM_CHANNEL_FADE, 1, 1, 1, { 0.5f, 0.25f } } }), 3);
	const float expected[4] = { 1.0f, 0.5f, 0.25f, 1.0f };
	for (int f = 0; f < 4; ++f)
	{
		HTreePose pose;
		tree.Anim_Pose(Matrix3D(), &anim, (float)f, pose);
		INFO("frame " << f);
		CHECK(pose.PivotFade[1] == doctest::Approx(expected[f]));
		CHECK(pose.PivotFade[2] == 1.0f); // pivot 2 is beyond the animation
	}
}

TEST_CASE("HTreeClass::Make_Default is the one-pivot RootTransform tree")
{
	HTreeClass tree = HTreeClass::Make_Default();
	CHECK(tree.Num_Pivots() == 1);
	CHECK(tree.Get_Pivot(0).Name == "RootTransform");
	CHECK(tree.Get_Pivot(0).ParentIdx == -1);
	HTreePose pose;
	Matrix3D root;
	root.Set_Translation(Vector3(1, 2, 3));
	tree.Base_Pose(root, pose);
	REQUIRE(pose.Num_Pivots() == 1);
	CHECK(pose.Transform[0].Get_Translation().Z == doctest::Approx(3.0f));
}
