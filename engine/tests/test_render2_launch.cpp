// OpenBFME. RENDER-2 tests: projectile launch bones. The drawable side (DrawableLaunchBones: RW 0x4C34A2 / 0x4BD9A7 / 0x4BDED7) on a synthetic soldier whose bone
// positions are worked out by hand from the skeleton and the animation, and the logic side (BezierProjectileBehavior::calcLaunchTransform, RW 0x6CAB85 / 0x70BCE7)
// with a fixed provider: the launch point of an arrow, its determinism and its place in the state hash. No retail files.
#include "doctest.h"
#include "CombatTestUtil.h"
#include "W3DDrawTestUtil.h"
#include "W3dSynth.h"

#include "GameClient/DrawableLaunchBones.h"
#include "GameLogic/Combat/ObjectWeapons.h"
#include "GameLogic/Module/ProjectileModules.h"

#include <cstring>
#include <map>
#include <string>
#include <vector>

using namespace w3dsynth;

namespace
{
// Skeleton R2UNIT_SKL: ROOT, HIP (0,0,1) under ROOT, HAND (0,2,0) under HIP, ARROW01 (0,0,1) under HAND, ARROW02 (1,0,0) under HAND, FIREBONE (0,0,2) under HAND,
// RECOIL01 (0,1,0) under HAND. HLOD R2UNIT_SKN, one mesh on the hip. Animations: raw, 30 fps, a Z translation channel on HIP with one value per frame (value i at
// frame i: the hip rises by the frame number), so a bone under the hip sits 1 * frame higher than in the bind pose.
void buildSoldier(MemoryFileSource &fs, const std::map<std::string, int> &animations)
{
	fs.Add("art\\w3d\\r2\\r2unit_skl.w3d",
		hierarchyChunk("R2UNIT_SKL", { pivot("ROOTTRANSFORM", 0xFFFFFFFFu, 0, 0, 0), pivot("HIP", 0, 0, 0, 1), pivot("HAND", 1, 0, 2, 0), pivot("ARROW01", 2, 0, 0, 1),
										 pivot("ARROW02", 2, 1, 0, 0), pivot("FIREBONE", 2, 0, 0, 2), pivot("RECOIL01", 2, 0, 1, 0) }));
	std::vector<std::uint8_t> bytes;
	SynthMesh body;
	body.Vertices = { { 0, 0, 0 }, { 1, 0, 0 }, { 1, 1, 0 } };
	body.Triangles = { 0, 1, 2 };
	body.Container = "R2UNIT_SKN";
	body.Name = "BODY";
	append(bytes, meshChunk(body));
	append(bytes, hlodChunk("R2UNIT_SKN", "R2UNIT_SKL", { { "R2UNIT_SKN.BODY", 1 } }));
	fs.Add("art\\w3d\\r2\\r2unit_skn.w3d", bytes);
	for (const auto &a : animations)
	{
		std::string lower = a.first;
		for (char &c : lower)
		{
			c = (char)std::tolower((unsigned char)c);
		}
		std::vector<float> z;
		for (int i = 0; i < a.second; ++i)
		{
			z.push_back((float)i);
		}
		fs.Add("art\\w3d\\r2\\" + lower + ".w3d", rawAnimChunk(a.first.c_str(), "R2UNIT_SKL", (std::uint32_t)a.second, 30, { { ANIM_CHANNEL_Z, 1, 0, 1, z } }));
	}
}

struct Soldier
{
	MemoryFileSource fs;
	std::unique_ptr<WW3DAssetManager> assets;
	std::unique_ptr<WW3DDrawAssets> drawAssets;
	drawtest::Harness harness;
	W3DModelDrawModuleData *data = nullptr;
	std::unique_ptr<DrawableLaunchBones> bones;
	DrawableLaunchBones::ModuleKey key;

	Soldier(const std::string &iniBody, const std::map<std::string, int> &animations = { { "R2UNIT_IDLA", 6 }, { "R2UNIT_ATKA", 4 } })
	{
		buildSoldier(fs, animations);
		assets.reset(new WW3DAssetManager(fs));
		drawAssets.reset(new WW3DDrawAssets(*assets));
		data = harness.parse(iniBody);
		REQUIRE_MESSAGE(data, harness.error);
		bones.reset(new DrawableLaunchBones(*drawAssets, {}));
		key = DrawableLaunchBones::ModuleKey{ "R2Soldier", 0 };
	}
	ModelConditionFlags flags(const char *names)
	{
		const ModelConditionFlags f = harness.parseFlags(names);
		REQUIRE_MESSAGE(harness.error.empty(), harness.error);
		return f;
	}
	// RW 0x4C34A2 with no drawable (no AttachToBoneInAnotherModule), scale 1, angle 0
	bool launch(const ModelConditionFlags &f, int wslot, int barrel, float out[12], float scale = 1.0f)
	{
		return bones->getProjectileLaunchOffset(*data, key, f, scale, 0.0f, nullptr, wslot, barrel, out);
	}
};

const char *kSoldier =
	"DefaultModelConditionState\n"
	"  Model = R2UNIT_SKN\n"
	"  Skeleton = R2UNIT_SKL\n"
	"  WeaponLaunchBone = PRIMARY ARROW\n"
	"  WeaponLaunchBone = SECONDARY FIREBONE\n"
	"  WeaponRecoilBone = TERTIARY RECOIL\n"
	"End\n"
	"IdleAnimationState\n"
	"  StateName = STATE_Idle\n"
	"  Animation = IdleA\n    AnimationName = R2UNIT_IDLA\n  End\n"
	"  FrameForPristineBonePositions = 2\n"
	"End\n"
	"AnimationState = FIRING_A\n"
	"  StateName = STATE_Fire\n"
	"  Animation = AtkA\n    AnimationName = R2UNIT_ATKA\n  End\n"
	"  Animation = AtkB\n    AnimationName = R2UNIT_IDLA\n  End\n"
	"  FrameForPristineBonePositions = 9\n"
	"End\n"
	"AnimationState = MOVING\n"
	"  StateName = STATE_Move\n"
	"End\n"
	"End\n";

void checkTranslation(const float m[12], float x, float y, float z)
{
	CHECK(m[3] == x);
	CHECK(m[7] == y);
	CHECK(m[11] == z);
}
} // namespace

TEST_CASE("render2 launch: the launch bone is the animation state's first animation at FrameForPristineBonePositions, clamped to the last frame (RW 0x4BD9A7)")
{
	Soldier s(kSoldier);
	float m[12];
	// idle (no flags): R2UNIT_IDLA (6 frames) at frame 2: the hip rises by 2. ARROW01 = ROOT + HIP (0,0,1+2) + HAND (0,2,0) + ARROW01 (0,0,1) = (0, 2, 4);
	// ARROW02 = (1, 2, 3). Barrels: ARROW01, ARROW02, then ARROW03 is missing: two barrels
	REQUIRE(s.launch(ModelConditionFlags(), 0, 0, m));
	checkTranslation(m, 0.0f, 2.0f, 4.0f);
	CHECK(m[0] == 1.0f);
	CHECK(m[5] == 1.0f);
	CHECK(m[10] == 1.0f);
	REQUIRE(s.launch(ModelConditionFlags(), 0, 1, m));
	checkTranslation(m, 1.0f, 2.0f, 3.0f);
	// a barrel index out of range is barrel 0 (RW 0x4C3664)
	REQUIRE(s.launch(ModelConditionFlags(), 0, 7, m));
	checkTranslation(m, 0.0f, 2.0f, 4.0f);
	REQUIRE(s.launch(ModelConditionFlags(), 0, -1, m));
	checkTranslation(m, 0.0f, 2.0f, 4.0f);
	const DrawableLaunchBones::PoseInfo idle = s.bones->poseOf(*s.data, s.key, *s.data->findBestInfo(ModelConditionFlags()), s.data->findBestAnimationState(ModelConditionFlags()), 1.0f);
	CHECK(idle.frame == 2);
	CHECK(idle.animation.find("R2UNIT_IDLA") != std::string::npos);

	// FIRING_A: the FIRST animation (R2UNIT_ATKA, 4 frames) at min(9, 4 - 1) = 3: ARROW01 = (0, 2, 5); the second animation of the state is never the pose
	const ModelConditionFlags firing = s.flags("FIRING_A");
	REQUIRE(s.launch(firing, 0, 0, m));
	checkTranslation(m, 0.0f, 2.0f, 5.0f);

	// MOVING: a state without an animation: the bind pose (the launch path's own render object has none): ARROW01 = (0, 2, 2)
	REQUIRE(s.launch(s.flags("MOVING"), 0, 0, m));
	checkTranslation(m, 0.0f, 2.0f, 2.0f);

	// the cache holds one entry per (module, model state, animation state, scale) and answers the same bits again
	const size_t cached = s.bones->cachedStates();
	REQUIRE(s.launch(firing, 0, 0, m));
	checkTranslation(m, 0.0f, 2.0f, 5.0f);
	CHECK(s.bones->cachedStates() == cached);
	CHECK(s.bones->problems().empty());
}

TEST_CASE("render2 launch: the scale multiplies the posed bones (RW 0x4BDD1E sets the scale matrix as the transform)")
{
	Soldier s(kSoldier);
	float m[12];
	REQUIRE(s.launch(ModelConditionFlags(), 0, 0, m, 2.0f));
	checkTranslation(m, 0.0f, 4.0f, 8.0f);
	CHECK(m[0] == 2.0f);
}

TEST_CASE("render2 launch: the RotWK barrel rules (RW 0x4BDED7) - a slot needs a launch or fx bone name, the unnumbered fallback keeps only those bones")
{
	Soldier s(kSoldier);
	float m[12];
	// SECONDARY: WeaponLaunchBone FIREBONE; FIREBONE01 does not exist, the unadorned FIREBONE does: one barrel at HAND + (0,0,2) = (0, 2, 1+2+2) at frame 2
	REQUIRE(s.launch(ModelConditionFlags(), 1, 0, m));
	checkTranslation(m, 0.0f, 2.0f, 5.0f);
	// TERTIARY names only a recoil bone (RECOIL01 exists): retail looks at the slot only when it has a fx or launch bone name, so there is no barrel and no
	// launch offset (ZH would have made a barrel with the recoil bone)
	CHECK_FALSE(s.launch(ModelConditionFlags(), 2, 0, m));
	const W3DModelBones *b = s.bones->bones(*s.data, s.key, *s.data->findBestInfo(ModelConditionFlags()), s.data->findBestAnimationState(ModelConditionFlags()), 1.0f);
	REQUIRE(b != nullptr);
	CHECK(b->barrels[2].empty());
	CHECK(b->barrels[0].size() == 2);
	CHECK(b->barrels[1].size() == 1);
	// QUATERNARY names nothing: no launch offset (RW 0x6CACDA: the caller then uses the identity)
	CHECK_FALSE(s.launch(ModelConditionFlags(), 3, 0, m));
	CHECK_FALSE(s.launch(ModelConditionFlags(), 9, 0, m));
}

TEST_CASE("render2 launch: a pose animation that does not resolve and a missing model are reported, never guessed")
{
	Soldier s("DefaultModelConditionState\n  Model = R2UNIT_SKN\n  Skeleton = R2UNIT_SKL\n  WeaponLaunchBone = PRIMARY ARROW\nEnd\n"
			  "IdleAnimationState\n  Animation = IdleA\n    AnimationName = R2UNIT_NOPE\n  End\nEnd\n"
			  "ModelConditionState = USER_1\n  Model = R2UNIT_GONE\nEnd\n"
			  "End\n");
	float m[12];
	// the animation does not resolve: the bind pose (RW 0x4BDC2D logs and poses nothing), ARROW01 = (0, 2, 2)
	REQUIRE(s.launch(ModelConditionFlags(), 0, 0, m));
	checkTranslation(m, 0.0f, 2.0f, 2.0f);
	REQUIRE(s.bones->problems().size() == 1);
	CHECK(s.bones->problems()[0].find("R2UNIT_NOPE") != std::string::npos);
	// a model state whose model is not in the archives has no bones (RW 0x4BDA6C "ASSET ERROR: Model %s not found!")
	CHECK_FALSE(s.launch(s.flags("USER_1"), 0, 0, m));
	REQUIRE(s.bones->problems().size() == 2);
	CHECK(s.bones->problems()[1].find("R2UNIT_GONE") != std::string::npos);
}

// ---------------------------------------------------------------------------------------------------------------------
// The logic side: RW 0x6CAB85 with a fixed provider
// ---------------------------------------------------------------------------------------------------------------------
namespace
{
using namespace combattest;

std::string shooter(const char *name)
{
	return std::string("Object ") + name + "\n"
		"  KindOf = INFANTRY SELECTABLE CAN_ATTACK SCORE\n"
		"  VisionRange = 250\n"
		"  Geometry = CYLINDER\n"
		"  GeometryMajorRadius = 8\n"
		"  GeometryMinorRadius = 8\n"
		"  GeometryHeight = 20\n"
		"  ArmorSet\n    Conditions = None\n    Armor = PlainArmor\n  End\n"
		"  WeaponSet\n    Conditions = None\n    Weapon = PRIMARY BowWeapon\n  End\n"
		"  Body = ActiveBody ModuleTag_Body\n    MaxHealth = 60\n  End\n"
		"  Behavior = AIUpdateInterface ModuleTag_AI\n  End\n"
		"  LocomotorSet\n    Locomotor = WalkerLoco\n    Condition = SET_NORMAL\n    Speed = 55\n  End\n"
		"End\n";
}

// a provider with one bone: rotation 90 degrees about Z (x -> y), translation (1, 2, 3); it records what it was asked
class FixedBone : public ProjectileLaunchOffsets
{
public:
	bool answer = true;
	std::vector<std::pair<int, int>> asked;
	bool launchOffset(const Object &, int wslot, int barrel, float launch[12]) override
	{
		asked.emplace_back(wslot, barrel);
		if (!answer)
		{
			return false;
		}
		const float m[12] = { 0, -1, 0, 1, 1, 0, 0, 2, 0, 0, 1, 3 };
		std::memcpy(launch, m, sizeof(m));
		return true;
	}
};

struct Range
{
	CombatWorld w;
	Object *a = nullptr, *b = nullptr;
	explicit Range(ProjectileLaunchOffsets *offsets, bool angle90 = false)
		: w(shooter("Shooter").c_str())
	{
		w.combat().setAutoAcquireEnabled(false);
		w.combat().setLaunchOffsets(offsets);
		a = w.unit("Shooter", 'A', 300, 300);
		b = w.unit("Dummy", 'B', 300, 400);
		if (angle90)
		{
			// an exact quarter turn: X axis (0, 1, 0), Y axis (-1, 0, 0) (row-major, columns are the axes)
			const float basis[9] = { 0, -1, 0, 1, 0, 0, 0, 0, 1 };
			a->setTransform(a->getPosition(), basis);
		}
		w.frames(2);
	}
	Object *fire()
	{
		ObjectWeapons *ow = a->getWeapons();
		REQUIRE(ow->chooseBestWeaponForTarget(b, PREFER_MOST_DAMAGE, CMD_FROM_PLAYER));
		while (ow->currentStatus() != WEAPON_READY_TO_FIRE && w.logic->getFrame() < 60)
		{
			w.frames(1);
		}
		ow->preFireCurrentWeapon(b, nullptr);
		int waited = 0;
		while (ow->currentStatus() == WEAPON_PRE_ATTACK && waited++ < 10)
		{
			w.frames(1);
		}
		ow->fireCurrentWeapon(b, nullptr);
		for (Object *o = w.logic->getFirstObject(); o; o = o->getNextObject())
		{
			for (const std::unique_ptr<BehaviorModule> &m : o->modules())
			{
				if (dynamic_cast<BezierProjectileBehavior *>(m.get()))
				{
					return o;
				}
			}
		}
		return nullptr;
	}
};
} // namespace

TEST_CASE("render2 launch: the projectile starts at the launcher's transform composed with the drawable's launch bone (RW 0x6CAB85 / 0x70BCE7)")
{
	// launcher at (300, 300, z0) facing +X (identity basis): world = T * B, so the arrow starts at (301, 302, z0 + 3) with B's rotation
	{
		FixedBone bone;
		Range r(&bone);
		const Coord3D p0 = *r.a->getPosition();
		Object *arrow = r.fire();
		REQUIRE(arrow != nullptr);
		REQUIRE(!bone.asked.empty());
		CHECK(bone.asked[0] == std::make_pair(0, 0)); // PRIMARY, barrel 0
		const BezierProjectileBehavior *bz = nullptr;
		for (const std::unique_ptr<BehaviorModule> &m : arrow->modules())
		{
			bz = bz ? bz : dynamic_cast<BezierProjectileBehavior *>(m.get());
		}
		REQUIRE(bz != nullptr);
		CHECK(bz->flightStart().x == p0.x + 1.0f);
		CHECK(bz->flightStart().y == p0.y + 2.0f);
		CHECK(bz->flightStart().z == p0.z + 3.0f);
		CHECK(r.w.combat().counters().launchBonesFound == 1);
	}
	// the launcher turned a quarter (X axis = +Y): the bone translation (1, 2, 3) becomes (-2, 1, 3)
	{
		FixedBone bone;
		Range r(&bone, true);
		const Coord3D p0 = *r.a->getPosition();
		Object *arrow = r.fire();
		REQUIRE(arrow != nullptr);
		const BezierProjectileBehavior *bz = nullptr;
		for (const std::unique_ptr<BehaviorModule> &m : arrow->modules())
		{
			bz = bz ? bz : dynamic_cast<BezierProjectileBehavior *>(m.get());
		}
		REQUIRE(bz != nullptr);
		CHECK(bz->flightStart().x == p0.x - 2.0f);
		CHECK(bz->flightStart().y == p0.y + 1.0f);
		CHECK(bz->flightStart().z == p0.z + 3.0f);
	}
	// no bone (the provider says false) and no provider (no drawable): the identity bone, the launcher's own position (RW 0x6CACDA)
	for (int variant = 0; variant < 2; ++variant)
	{
		FixedBone bone;
		bone.answer = false;
		Range r(variant == 0 ? &bone : nullptr);
		const Coord3D p0 = *r.a->getPosition();
		Object *arrow = r.fire();
		REQUIRE(arrow != nullptr);
		const BezierProjectileBehavior *bz = nullptr;
		for (const std::unique_ptr<BehaviorModule> &m : arrow->modules())
		{
			bz = bz ? bz : dynamic_cast<BezierProjectileBehavior *>(m.get());
		}
		REQUIRE(bz != nullptr);
		CHECK(bz->flightStart().x == p0.x);
		CHECK(bz->flightStart().y == p0.y);
		CHECK(bz->flightStart().z == p0.z);
		CHECK(r.w.combat().counters().launchBonesFound == 0);
		// a GameLogic without a provider (no W3D assets) is counted: reported in the counters line and hashed (S-460)
		CHECK(r.w.combat().counters().launchesWithoutBones == (variant == 0 ? 0u : 1u));
	}
}

TEST_CASE("render2 launch: two runs with launch bones give the same state hash every frame; the bone moves the arrow, so the hash sees it")
{
	auto run = [](ProjectileLaunchOffsets *offsets) {
		Range r(offsets);
		REQUIRE(r.fire() != nullptr);
		std::vector<std::uint32_t> hashes;
		for (int i = 0; i < 12; ++i)
		{
			r.w.frames(1);
			hashes.push_back(r.w.logic->computeStateHash());
		}
		return hashes;
	};
	FixedBone b1, b2;
	const std::vector<std::uint32_t> h1 = run(&b1);
	const std::vector<std::uint32_t> h2 = run(&b2);
	CHECK(h1 == h2);
	const std::vector<std::uint32_t> none = run(nullptr);
	CHECK(none[0] != h1[0]);
}

TEST_CASE("render2 launch: S-360 reports what is ported and what is not, and the counters line carries the launch bones")
{
	FixedBone bone;
	Range r(&bone);
	REQUIRE(r.fire() != nullptr);
	std::string s360, counters;
	for (const std::string &line : r.w.logic->report().stops)
	{
		if (line.rfind("[S-360] ", 0) == 0)
		{
			s360 = line;
		}
		if (line.rfind("[S-320..S-328 counters]", 0) == 0)
		{
			counters = line;
		}
	}
	CHECK(s360.find("RW 0x6CB490 -> 0x6CAB85 is ported") != std::string::npos);
	CHECK(s360.find("garrison branch") != std::string::npos);
	CHECK(s360.find("turret composition") != std::string::npos);
	CHECK(counters.find("launch bones 1 garrison launches unported 0 garrison launches 0 launches without bones 0") != std::string::npos);
}

TEST_CASE("render2 launch: Drawable::getPristineBonePositions per module (RW 0x4C3731) and the AttachToBoneInAnotherModule turn (RW 0x4C3545..0x4C3637)")
{
	Soldier s(kSoldier);
	Matrix3D m[4];
	// start index 0: only the plain name; the soldier's public bone ARROW exists only numbered, so nothing
	CHECK(s.bones->getPristineBonePositions(*s.data, s.key, ModelConditionFlags(), 1.0f, "ARROW", 0, m, 4) == 0);
	// start index 1: ARROW01, ARROW02, then ARROW03 is missing; idle pose (frame 2): (0, 2, 4) and (1, 2, 3)
	REQUIRE(s.bones->getPristineBonePositions(*s.data, s.key, ModelConditionFlags(), 1.0f, "ARROW", 1, m, 4) == 2);
	CHECK(m[0].Row[2][3] == 4.0f);
	CHECK(m[1].Row[0][3] == 1.0f);
	// at most maxBones
	CHECK(s.bones->getPristineBonePositions(*s.data, s.key, ModelConditionFlags(), 1.0f, "ARROW", 1, m, 1) == 1);
	// the plain name of a bone the state names as a launch bone (FIREBONE)
	REQUIRE(s.bones->getPristineBonePositions(*s.data, s.key, ModelConditionFlags(), 1.0f, "FireBone", 0, m, 4) == 1);
	CHECK(m[0].Row[2][3] == 5.0f);
	// the turn: (1, 2, 3) by 0 stays; by a quarter turn (pi / 2: cos is 6e-17 in double, rounded to float it is not 0) x' = 1 c - 2 s, y' = 2 c + 1 s
	Matrix3D b;
	b.Row[0][3] = 1.0f;
	b.Row[1][3] = 2.0f;
	b.Row[2][3] = 3.0f;
	float out[3];
	DrawableLaunchBones::turnAttachOffset(b, 0.0f, out);
	CHECK(out[0] == 1.0f);
	CHECK(out[1] == 2.0f);
	CHECK(out[2] == 3.0f);
	DrawableLaunchBones::turnAttachOffset(b, 1.5707964f, out);
	CHECK(out[0] == doctest::Approx(-2.0f).epsilon(1e-6));
	CHECK(out[1] == doctest::Approx(1.0f).epsilon(1e-6));
	CHECK(out[2] == 3.0f);
}
