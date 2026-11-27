// OpenBFME unit tests. GPL-3.0. The ParticleSysBone systems follow the ANIMATED bones (RW 0x4C7795 -> RW 0x4B6D5F).
//
// Every expectation is worked out by hand from the INI text and the RotWK instructions the port follows, never read back from the
// code under test:
//   * RW 0x4C7795 `cmp byte [edi + 0x108], 0` - doDrawModule calls the follow only for a module data whose
//     ParticlesAttachedToAnimatedBones is on;
//   * RW 0x4C779E .. 0x4C77A5 - the call with the argument 0 (the per-frame one), slot 27 of vtable 0xBE3B78 = RW 0x4B6D5F;
//   * RW 0x4B6DB3 findParticleSystemByID, RW 0x4B6DCC / 0x4B6DD5 (FollowBone ends only the arg-1 walk), RW 0x4B6DDF / 0x4B6E57
//     (the two branches and the "bone index 0 is skipped" case), RW 0x4B6E79 / 0x4B6E8B (stop / start from the bone's visibility),
//     RW 0x4B6E2F and 0x4B6E44 (setLocalTransform, setSkipParentXfrm(1)).
// Synthetic data only: the models are built with W3dSynth (a hierarchy named ABUNIT_SKL whose HIP pivot rises by one unit per
// animation frame), so the test runs without game files.
//
// The model:  ROOTTRANSFORM (0) -> HIP (1, base (0,0,1)) -> HAND (2, base (0,2,0)). The animation ABUNIT_IDLA moves HIP by +Z and
// hides HAND from frame 4 on (`ANIM_CHANNEL_FADE` is not needed for that: the visibility channel of a raw animation is the pivot's
// second value, see W3dSynth's ANIM_CHANNEL_Q entry count below). No visibility channel is emitted, so every pivot is visible -
// the "hidden" case is covered by the FADE channel that hides a pivot (W3dSynth's ANIM_CHANNEL_FADE, value 0 = hidden), exactly the
// channel HTreePose::IsVisible reports.

#include "doctest.h"

#include "FxTestUtil.h"
#include "W3DDrawTestUtil.h"
#include "W3dSynth.h"

#include "GameClient/ParticleBoneFollow.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DScriptedModelDraw.h"

#include <cmath>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

using namespace drawtest;
using namespace w3dsynth;

namespace
{

// The client random of the draw modules: a fixed answer for every draw, so a pose is the only thing that can move a system.
class FixedRandom : public W3DDrawRandom
{
public:
	int value(int lo, int hi) override { return lo == hi ? lo : (lo + hi) / 2; }
	float real(float lo, float hi) override { return (lo + hi) * 0.5f; }
};

// one scripted draw module on the synthetic soldier, kept alive with its assets
struct Draw
{
	MemoryFileSource fs;
	std::unique_ptr<WW3DAssetManager> assets;
	std::unique_ptr<WW3DDrawAssets> drawAssets;
	Harness harness;
	FixedRandom random;
	std::unique_ptr<W3DScriptedModelDraw> draw;
	W3DModelDrawModuleData *data = nullptr;

	Draw(const std::string &iniBody, bool hideHandFromFrame4)
	{
		// the skeleton: ROOTTRANSFORM, HIP one unit up, HAND two units further
		fs.Add("art\\w3d\\ab\\abunit_skl.w3d",
			hierarchyChunk("ABUNIT_SKL", { pivot("ROOTTRANSFORM", 0xFFFFFFFFu, 0, 0, 0), pivot("HIP", 0, 0, 0, 1), pivot("HAND", 1, 0, 2, 0) }));
		std::vector<std::uint8_t> bytes;
		SynthMesh body;
		body.Vertices = { { 0, 0, 0 }, { 1, 0, 0 }, { 1, 1, 0 }, { 0, 1, 0 } };
		body.Triangles = { 0, 1, 2, 0, 2, 3 };
		body.Container = "ABUNIT_SKN";
		body.Name = "BODY";
		append(bytes, meshChunk(body));
		append(bytes, hlodChunk("ABUNIT_SKN", "ABUNIT_SKL", { { "ABUNIT_SKN.BODY", 1 } }));
		fs.Add("art\\w3d\\ab\\abunit_skn.w3d", bytes);
		// ABUNIT_IDLA: 11 frames, 30 fps; HIP's Z is +1 at every frame (a constant translation channel); in the hidden case HAND's
		// visibility BIT channel (BIT_CHANNEL_VIS: visible 0..3, hidden from frame 4, hidden outside the range) is written too
		std::vector<SynthChannel> channels;
		channels.push_back(SynthChannel{ ANIM_CHANNEL_Z, 1, 0, 1, std::vector<float>(11, 1.0f) });
		std::vector<std::uint8_t> anim = rawAnimChunk("ABUNIT_IDLA", "ABUNIT_SKL", 11, 30, channels);
		if (hideHandFromFrame4)
		{
			// the visibility bit channel of pivot 2 (W3dBitChannelStruct: FirstFrame, LastFrame, Flags, Pivot, DefaultVal, bits);
			// frames 0..3 set, 4..10 clear
			Bytes vis;
			vis.u16(0).u16(10).u16(0).u16(2).u8(0).u8(0x0F).u8(0x00);
			ChunkWriter tail;
			tail.chunk(W3D_CHUNK_BIT_CHANNEL, vis.v);
			// splice it inside the ANIMATION wrapper: rewrite the wrapper's size
			const std::uint32_t wrapperSize = 0x80000000u | (std::uint32_t)(anim.size() - 8);
			std::memcpy(&anim[4], &wrapperSize, 4);
			anim.insert(anim.end(), tail.bytes.begin(), tail.bytes.end());
			const std::uint32_t grown = 0x80000000u | (std::uint32_t)(anim.size() - 8);
			std::memcpy(&anim[4], &grown, 4);
		}
		fs.Add("art\\w3d\\ab\\abunit_idla.w3d", anim);
		assets.reset(new WW3DAssetManager(fs));
		drawAssets.reset(new WW3DDrawAssets(*assets));
		data = harness.parse(iniBody);
		REQUIRE_MESSAGE(data, harness.error);
		W3DScriptedModelDraw::Options o;
		o.buildBones = false;
		draw.reset(new W3DScriptedModelDraw(*data, *drawAssets, random, nullptr, o));
		REQUIRE(draw->errors().empty());
	}

	// drives the draw by `ms` of render time (the animation runs at 30 fps, so 33.4 ms is one frame)
	void advanceMs(double ms) { draw->advance(ms); }

	// the pivot transforms the draw's current pose shows, evaluated by the shared hierarchy - the values the test expects a system
	// to carry (a SYSTEM's expected matrix is the drawable's world transform times one of these, scaled)
	Matrix3D pivotXf(int pivot)
	{
		W3DDrawPoseView v;
		draw->poseView(v);
		REQUIRE(v.clipKey0 != nullptr);
		HTreePose pose;
		draw->currentModel()->Tree->Anim_Pose(Matrix3D(), static_cast<const HAnimClass *>(v.clipKey0), v.frame0, pose);
		REQUIRE((size_t)pivot < pose.Transform.size()); // tolerance: a fixture precondition (the synthetic hierarchy has the pivot)
		return pose.Transform[(size_t)pivot];
	}
	bool pivotVisible(int pivot)
	{
		W3DDrawPoseView v;
		draw->poseView(v);
		REQUIRE(v.clipKey0 != nullptr);
		HTreePose pose;
		draw->currentModel()->Tree->Anim_Pose(Matrix3D(), static_cast<const HAnimClass *>(v.clipKey0), v.frame0, pose);
		REQUIRE((size_t)pivot < pose.IsVisible.size()); // tolerance: a fixture precondition (the pose has a visibility per pivot)
		return pose.IsVisible[(size_t)pivot] != 0;
	}
};

// the module body: one default state and one animation that plays for the whole test. `extra` holds module-level fields (they follow the
// state blocks, as in retail's W3DScriptedModelDraw blocks); `flags` is the line the test checks ("" when unused)
std::string moduleBody(const std::string &flags, const std::string &extra = "")
{
	return flags + extra +
		"DefaultModelConditionState\n  Model = ABUNIT_SKN\n  Skeleton = ABUNIT_SKL\nEnd\n"
		"IdleAnimationState\n"
		"  StateName = STATE_Idle\n"
		"  Animation = IDLA\n"
		"    AnimationName = ABUNIT_IDLA\n"
		"    AnimationMode = LOOP\n"
		"  End\n"
		"End\n"
		"End\n";
}

// the module-level flag the walk is gated on (RW 0x4C7795 / the data's +0x108)
const char *kFollowFlag = "ParticlesAttachedToAnimatedBones = Yes\n";
const char *kCheckDrawableFlag = "ParticleBonesCheckDrawable = Yes\n";

// a FXParticleSystem whose particle is born at the system's origin and does not move: one particle per update (BurstDelay 0, RW
// 0x5F3CCF), so every step's newest particle is the one the current transform produced
const std::string kParticleIni = fxtest::systemText("Follow", "    Lifetime = 100 100\n    BurstCount = 1 1\n    BurstDelay = 0 0\n", fxtest::kPointOrtho);

// one record for the system `id` created by the first draw entry (none: the caller fills it), with the bone the record names
ParticleBoneFollow::Record record(FXParticleSystem::ParticleSystemID id, size_t entry, const std::string &bone, int boneIndex, bool followBone,
	bool checkDrawable)
{
	ParticleBoneFollow::Record r;
	r.system = id;
	r.entry = entry;
	r.boneName = bone;
	r.boneIndex = boneIndex;
	r.followBone = followBone;
	r.checkDrawable = checkDrawable;
	return r;
}

// the pose LiveFX::followAnimatedBones hands the walk: the draw, the drawable's scale and world transform, and the module data's gate
ParticleBoneFollow::Pose poseOf(Draw &d, bool follow, float scale = 1.0f, const Matrix3D &world = Matrix3D())
{
	ParticleBoneFollow::Pose p;
	p.draw = d.draw.get();
	p.scale = scale;
	p.world = world;
	p.follow = follow;
	return p;
}

} // namespace

// -------------------------------------------------------------------------------------------------------------------------------
TEST_CASE("ParticleBoneFollow module data: ParticlesAttachedToAnimatedBones is the gate of RW 0x4C7795 and both draw states carry it")
{
	Harness off;
	W3DModelDrawModuleData *plain = off.parse("DefaultModelConditionState\n  Model = ABUNIT_SKN\nEnd\nEnd\n");
	REQUIRE(plain);
	CHECK_FALSE(ParticleBoneFollow::wantsFollow(*plain));

	Harness on;
	W3DModelDrawModuleData *flagged = on.parse(moduleBody(kFollowFlag));
	REQUIRE(flagged);
	CHECK(ParticleBoneFollow::wantsFollow(*flagged));

	Harness no;
	W3DModelDrawModuleData *explicitNo = no.parse(moduleBody("ParticlesAttachedToAnimatedBones = No\n"));
	REQUIRE(explicitNo);
	CHECK_FALSE(ParticleBoneFollow::wantsFollow(*explicitNo)); // the mod's one `No` (Ennorath 1.9.8.2: 141 object blocks on, 142 lines - ArnorTrebuchetFortress sets it twice - 1 off)

	// the second gate field of the same table row family: ParticleBonesCheckDrawable (+0x12A, RW 0x4B6DDF) is parse-only in the port
	Harness check;
	W3DModelDrawModuleData *checked = check.parse(moduleBody(kCheckDrawableFlag));
	REQUIRE(checked);
	CHECK(checked->m_particleBonesCheckDrawable);
}

// -------------------------------------------------------------------------------------------------------------------------------
TEST_CASE("ParticleBoneFollow: a ParticleSysBone system takes the ANIMATED bone's transform and keeps the parent transform skipped (RW 0x4B6E2F / 0x4B6E44)")
{
	Draw d(moduleBody(kFollowFlag), false);
	fxtest::Sim sim(kParticleIni);
	const FXParticleSystem::ParticleSystemID id = sim.spawn("Follow");
	REQUIRE(sim.mgr->findParticleSystemByID(id) != nullptr);

	// the drawable stands at (100, 200, 300); the pose is at frame 0, where HIP is (0, 0, 1) of its base plus the animation's 1
	Matrix3D world;
	world.Row[0][3] = 100.0f;
	world.Row[1][3] = 200.0f;
	world.Row[2][3] = 300.0f;
	const Matrix3D hip0 = d.pivotXf(1);
	CHECK(hip0.Get_Translation().Z == doctest::Approx(2.0f)); // HIP's base (0, 0, 1) plus the animation's Z (1) at frame 0 // tolerance: the pose walk adds the base and the animation's float32 channel; the exact sum is 2.0 only up to the add's rounding

	std::vector<ParticleBoneFollow::Record> records = { record(id, 0, "hip", 1, true, false) };
	const ParticleBoneFollow::Pose pose = poseOf(d, true, 1.0f, world);
	ParticleBoneFollow::Counters c;
	ParticleBoneFollow::followFrame(records, *sim.mgr, &pose, 1, &c, 0);
	CHECK(c.followed == 1);
	CHECK(c.missed == 0);
	FXParticleSystem::ParticleSystem *sys = sim.mgr->findParticleSystemByID(id);
	REQUIRE(sys);
	CHECK_FALSE(sys->isStopped()); // the by-index branch's RW 0x4B6E8B start

	// the effect: the system emits from the bone, so its particle is born at the drawable's world position plus HIP's pose. A system that
	// kept the creation's transform would emit at (0, 0, 0) - the creation's position for a bind-pose-less record - which is the failure
	// this test is about.
	sim.step();
	REQUIRE(!sim.particles(id).empty());
	CHECK(sim.particles(id)[0]->pos.x == doctest::Approx(100.0f).epsilon(1e-4)); // tolerance: float32 engine value vs the test's own float32 re-derivation
	CHECK(sim.particles(id)[0]->pos.y == doctest::Approx(200.0f).epsilon(1e-4)); // tolerance: float32 engine value vs the test's own float32 re-derivation
	CHECK(sim.particles(id)[0]->pos.z == doctest::Approx(300.0f + hip0.Get_Translation().Z).epsilon(1e-4)); // tolerance: float32 engine value vs the test's own float32 re-derivation

	// and the animation moves it: 100 ms = 3 frames later HIP is at the animation's own value for that frame, not the bind pose's
	d.advanceMs(100.0);
	CHECK(d.draw->frame().tracks[0].frame == doctest::Approx(3.0f).epsilon(1e-2)); // tolerance: 100 ms of render time is 3 frames at 30 fps, the draw advances on a float accumulator (3.0000002)
	const float hipZ = d.pivotXf(1).Get_Translation().Z;
	ParticleBoneFollow::followFrame(records, *sim.mgr, &pose, 1, &c, 0);
	CHECK(c.followed == 2);
	const std::size_t before = sim.particles(id).size();
	sim.step();
	REQUIRE(sim.particles(id).size() == before + 1); // BurstDelay 0: exactly one particle per update
	const FXParticleSystem::Particle *born = sim.particles(id)[before];
	CHECK(born->pos.x == doctest::Approx(100.0f).epsilon(1e-4)); // tolerance: float32 engine value vs the test's own float32 re-derivation
	CHECK(born->pos.y == doctest::Approx(200.0f).epsilon(1e-4)); // tolerance: float32 engine value vs the test's own float32 re-derivation
	CHECK(born->pos.z == doctest::Approx(300.0f + hipZ).epsilon(1e-4)); // tolerance: float32 engine value vs the test's own float32 re-derivation

	// the walk at an unchanged pose writes the same value again (idempotent for a still pose)
	const std::size_t mid = sim.particles(id).size();
	ParticleBoneFollow::followFrame(records, *sim.mgr, &pose, 1, &c, 0);
	CHECK(c.followed == 3);
	sim.step();
	REQUIRE(sim.particles(id).size() == mid + 1);
	CHECK(sim.particles(id)[mid]->pos.z == doctest::Approx(300.0f + hipZ).epsilon(1e-4)); // the particle this step made; // tolerance: float32 engine value vs the test's own float32 re-derivation
}

// -------------------------------------------------------------------------------------------------------------------------------
TEST_CASE("ParticleBoneFollow: the gate (RW 0x4C7795) leaves the system where the creation put it, and a bone the pose lacks keeps the last transform")
{
	Draw d(moduleBody(kFollowFlag), false);
	fxtest::Sim sim(kParticleIni);
	const FXParticleSystem::ParticleSystemID id = sim.spawn("Follow");
	FXParticleSystem::ParticleSystem *sys = sim.mgr->findParticleSystemByID(id);
	REQUIRE(sys);
	// the creation's local transform: the bind pose's bone position and Z rotation (RW 0x4C6514, LiveFX::createSystems)
	sys->setPosition(Coord3D{ 5.0f, 6.0f, 7.0f });
	std::vector<ParticleBoneFollow::Record> records = { record(id, 0, "hip", 1, true, false) };

	// ParticlesAttachedToAnimatedBones is OFF for this module: doDrawModule never makes the call, so nothing moves. The particle proves
	// it: it is born where the creation put the system, (5, 6, 7), not on HIP
	const ParticleBoneFollow::Pose gated = poseOf(d, false);
	ParticleBoneFollow::Counters c;
	ParticleBoneFollow::followFrame(records, *sim.mgr, &gated, 1, &c, 0);
	CHECK(c.followed == 0);
	CHECK(c.gated == 1);
	sim.step();
	REQUIRE(!sim.particles(id).empty());
	CHECK(sim.particles(id)[0]->pos.x == doctest::Approx(5.0f)); // tolerance: float32 engine value vs the test's own float32 re-derivation
	CHECK(sim.particles(id)[0]->pos.y == doctest::Approx(6.0f)); // tolerance: float32 engine value vs the test's own float32 re-derivation
	CHECK(sim.particles(id)[0]->pos.z == doctest::Approx(7.0f)); // tolerance: float32 engine value vs the test's own float32 re-derivation

	const ParticleBoneFollow::Pose pose = poseOf(d, true);
	// the by-INDEX branch (ParticleBonesCheckDrawable clear) resolves through the record's +4 alone, so a name the model lacks does not
	// matter to it: an index the model lacks is what is skipped (RW 0x4B6E9D would hand it to the render object, which answers nothing)
	records[0].boneIndex = 7;
	ParticleBoneFollow::Counters miss;
	ParticleBoneFollow::followFrame(records, *sim.mgr, &pose, 1, &miss, 0);
	CHECK(miss.followed == 0);
	CHECK(miss.missed == 1);
	sim.step();
	CHECK(sim.particles(id).back()->pos.x == doctest::Approx(5.0f)); // tolerance: float32 engine value vs the test's own float32 re-derivation
	CHECK(sim.particles(id).back()->pos.z == doctest::Approx(7.0f)); // tolerance: float32 engine value vs the test's own float32 re-derivation

	// a bone index of 0 is "no such bone" for the by-INDEX branch (RW 0x4B6E57 `test ebx, ebx; je 0x4B6ED2`): still nothing written
	records[0].boneIndex = 0;
	ParticleBoneFollow::followFrame(records, *sim.mgr, &pose, 1, &miss, 0);
	CHECK(miss.missed == 2);
	sim.step();
	CHECK(sim.particles(id).back()->pos.z == doctest::Approx(7.0f)); // tolerance: float32 engine value vs the test's own float32 re-derivation

	// the by-NAME branch (ParticleBonesCheckDrawable set, RW 0x672B5B) resolves the record's name and ignores the zero index: a name
	// the model lacks leaves the system where it was (0x4B6E1B `je 0x4B6E49` skips both calls)
	records[0].checkDrawable = true;
	records[0].boneName = "no_such_bone";
	ParticleBoneFollow::Counters byName;
	ParticleBoneFollow::followFrame(records, *sim.mgr, &pose, 1, &byName, 0);
	CHECK(byName.followed == 0);
	CHECK(byName.missed == 1); // this walk's own miss (each walk has its own counter)
	sim.step();
	CHECK(sim.particles(id).back()->pos.z == doctest::Approx(7.0f)); // tolerance: float32 engine value vs the test's own float32 re-derivation

	// and the name HIP with the same zero index follows
	records[0].boneName = "hip";
	ParticleBoneFollow::Counters hits;
	ParticleBoneFollow::followFrame(records, *sim.mgr, &pose, 1, &hits, 0);
	CHECK(hits.followed == 1);
	CHECK(hits.missed == 0); // the name resolves, so the walk writes HIP: the particle below is born on it
	CHECK(hits.started == 0); // the by-name branch has no start call (RW 0x4B6E1D .. 0x4B6E55)
	sim.step();
	CHECK(sim.particles(id).back()->pos.x == doctest::Approx(0.0f)); // tolerance: float32 engine value vs the test's own float32 re-derivation
	CHECK(sim.particles(id).back()->pos.z == doctest::Approx(d.pivotXf(1).Get_Translation().Z)); // tolerance: float32 engine value vs the test's own float32 re-derivation
}

// -------------------------------------------------------------------------------------------------------------------------------
TEST_CASE("ParticleBoneFollow: the bone's visibility stops and starts the system on the by-index branch (RW 0x4B6E79 / 0x4B6E8B)")
{
	Draw d(moduleBody(kFollowFlag), true /* HAND hidden from frame 4 on */);
	fxtest::Sim sim(kParticleIni);
	const FXParticleSystem::ParticleSystemID id = sim.spawn("Follow");
	FXParticleSystem::ParticleSystem *sys = sim.mgr->findParticleSystemByID(id);
	REQUIRE(sys);
	std::vector<ParticleBoneFollow::Record> records = { record(id, 0, "hand", 2, true, false) };
	const ParticleBoneFollow::Pose pose = poseOf(d, true);

	// frame 1: HAND is visible, the system runs and follows
	d.advanceMs(33.4);
	CHECK(d.draw->frame().tracks[0].frame == doctest::Approx(1.0f).epsilon(1e-3)); // tolerance: 33.4 ms is one frame at 30 fps, the draw advances on a float accumulator
	REQUIRE(d.pivotVisible(2));
	ParticleBoneFollow::Counters c;
	ParticleBoneFollow::followFrame(records, *sim.mgr, &pose, 1, &c, 0);
	CHECK(c.started == 1);
	CHECK(c.stopped == 0);
	CHECK_FALSE(sys->isStopped());
	sim.step();
	REQUIRE(!sim.particles(id).empty());
	CHECK(sim.particles(id)[0]->pos.y == doctest::Approx(2.0f)); // HAND's base (0, 2, 0) under the animated HIP; // tolerance: float32 engine value vs the test's own float32 re-derivation

	// frame 4: the visibility channel hides HAND, the system stops emitting (the transform is not written again)
	d.advanceMs(3 * 33.4);
	CHECK((int)std::lround(d.draw->frame().tracks[0].frame) == 4); // PristinePose reads the rounded frame, as the draw does
	REQUIRE_FALSE(d.pivotVisible(2));
	ParticleBoneFollow::followFrame(records, *sim.mgr, &pose, 1, &c, 0);
	CHECK(c.stopped == 1);
	CHECK(c.followed == 1); // unchanged: the hidden frame wrote no transform
	CHECK(sys->isStopped());
	const std::size_t emitted = sim.particles(id).size();
	sim.step();
	CHECK(sim.particles(id).size() == emitted); // RW 0x5F2EDE: a stopped system emits nothing

	// the by-name branch has no visibility test and no start call: with the flag set the walk writes the transform even while the
	// system stays stopped (RW 0x4B6E1D .. 0x4B6E55 has neither), so nothing is emitted
	records[0].checkDrawable = true;
	ParticleBoneFollow::followFrame(records, *sim.mgr, &pose, 1, &c, 0);
	CHECK(c.stopped == 1);
	CHECK(c.followed == 2);
	sim.step();
	CHECK(sim.particles(id).size() == emitted);
}

// -------------------------------------------------------------------------------------------------------------------------------
TEST_CASE("ParticleBoneFollow: FollowBone ends only the end-of-draw walk (RW 0x4B6DD5) and a dead system is skipped, not dropped (RW 0x4B6DB3)")
{
	Draw d(moduleBody(kFollowFlag), false);
	fxtest::Sim sim(kParticleIni);
	const FXParticleSystem::ParticleSystemID a = sim.spawn("Follow");
	const FXParticleSystem::ParticleSystemID b = sim.spawn("Follow");
	REQUIRE(sim.mgr->findParticleSystemByID(a) != nullptr);
	REQUIRE(sim.mgr->findParticleSystemByID(b) != nullptr);
	// `b` is not FollowBone and stands first in the list; `a` is
	std::vector<ParticleBoneFollow::Record> records = { record(b, 0, "hip", 1, false, false), record(a, 0, "hip", 1, true, false) };
	const ParticleBoneFollow::Pose pose = poseOf(d, true);

	// the per-frame call passes 0 (RW 0x4C77A3): BOTH systems follow, FollowBone or not
	ParticleBoneFollow::Counters c;
	ParticleBoneFollow::followFrame(records, *sim.mgr, &pose, 1, &c, 0);
	CHECK(c.followed == 2);
	sim.step();
	REQUIRE(!sim.particles(a).empty());
	REQUIRE(!sim.particles(b).empty());
	CHECK(sim.particles(a)[0]->pos.z == doctest::Approx(d.pivotXf(1).Get_Translation().Z)); // tolerance: float32 engine value vs the test's own float32 re-derivation
	CHECK(sim.particles(b)[0]->pos.z == doctest::Approx(d.pivotXf(1).Get_Translation().Z)); // tolerance: float32 engine value vs the test's own float32 re-derivation

	// the end-of-draw call (the argument 1 of RW 0x4C6D78) returns at the first record whose FollowBone is off: `a`, behind it, is
	// not moved - its transform is whatever the per-frame call last wrote (here the same value, so the check is the counter)
	ParticleBoneFollow::Counters end;
	ParticleBoneFollow::followFrame(records, *sim.mgr, &pose, 1, &end, 1);
	CHECK(end.followed == 0);

	// a system that is gone: skipped, the record stays (the state's clean path drops it). A destroyed system that never emitted leaves
	// the manager on its next update (RW 0x5F5123 -> deleteSystem), so the step comes before the walk
	const FXParticleSystem::ParticleSystemID gone = sim.spawn("Follow");
	sim.mgr->findParticleSystemByID(gone)->destroy();
	records.push_back(record(gone, 0, "hip", 1, false, false)); // last: the per-frame walk reaches it and finds no system
	sim.step();
	REQUIRE(sim.mgr->findParticleSystemByID(gone) == nullptr);
	ParticleBoneFollow::Counters dead;
	ParticleBoneFollow::followFrame(records, *sim.mgr, &pose, 1, &dead, 0);
	CHECK(dead.missed == 1);
	CHECK(dead.followed == 2);
	CHECK(records.size() == 3);
}

// -------------------------------------------------------------------------------------------------------------------------------
TEST_CASE("ParticleBoneFollow: the drawable's instance scale reaches the particle (RW 0x4C6514's render object scale) - a bare bone would drop it")
{
	Draw d(moduleBody(kFollowFlag), false);
	fxtest::Sim sim(kParticleIni);
	const FXParticleSystem::ParticleSystemID id = sim.spawn("Follow");
	REQUIRE(sim.mgr->findParticleSystemByID(id) != nullptr);
	std::vector<ParticleBoneFollow::Record> records = { record(id, 0, "hip", 1, true, false) };
	// a drawable drawn at 2x: HIP's (0, 0, 1) becomes (0, 0, 2)
	const ParticleBoneFollow::Pose pose = poseOf(d, true, 2.0f);
	ParticleBoneFollow::Counters c;
	ParticleBoneFollow::followFrame(records, *sim.mgr, &pose, 1, &c, 0);
	CHECK(c.followed == 1);
	sim.step();
	REQUIRE(!sim.particles(id).empty());
	CHECK(sim.particles(id)[0]->pos.z == doctest::Approx(2.0f * d.pivotXf(1).Get_Translation().Z)); // a bare bone would give 1x; // tolerance: float32 engine value vs the test's own float32 re-derivation
}

// -------------------------------------------------------------------------------------------------------------------------------
// the neighbouring behaviour the follow must not change: a system created for a module WITHOUT the gate keeps exactly what the creation
// wrote (RW 0x4C6514: position + Z rotation, no skip flag), and a system whose drawable is gone is destroyed by the attachment
TEST_CASE("ParticleBoneFollow neighbour: without the gate a ParticleSysBone system keeps the creation's transform, and an orphaned one is destroyed")
{
	fxtest::Sim sim(kParticleIni);
	const FXParticleSystem::ParticleSystemID id = sim.spawn("Follow");
	FXParticleSystem::ParticleSystem *sys = sim.mgr->findParticleSystemByID(id);
	REQUIRE(sys);
	sys->setPosition(Coord3D{ 1.0f, 2.0f, 3.0f });
	sys->rotateLocalTransformZ(0.5f);
	sys->attachToDrawable(7);
	// the drawable is not known to the environment: RW 0x5FA2CE destroys the system (0x5FA357's not-found path)
	sim.step();
	CHECK(sim.mgr->findParticleSystemByID(id) == nullptr);

	// the creation's transform stands for a system that is not attached: nothing rewrites it between steps
	const FXParticleSystem::ParticleSystemID free = sim.spawn("Follow");
	FXParticleSystem::ParticleSystem *s2 = sim.mgr->findParticleSystemByID(free);
	REQUIRE(s2);
	s2->setPosition(Coord3D{ 4.0f, 5.0f, 6.0f });
	sim.step();
	REQUIRE(!sim.particles(free).empty());
	CHECK(sim.particles(free)[0]->pos.x == doctest::Approx(4.0f)); // tolerance: float32 engine value vs the test's own float32 re-derivation
	CHECK(sim.particles(free)[0]->pos.y == doctest::Approx(5.0f)); // tolerance: float32 engine value vs the test's own float32 re-derivation
	CHECK(sim.particles(free)[0]->pos.z == doctest::Approx(6.0f)); // tolerance: float32 engine value vs the test's own float32 re-derivation
}
