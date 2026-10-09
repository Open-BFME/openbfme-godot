// File name: shares the retail world of the HUD tests (hudtest::shared(); doctest runs the test_hud_* files in name order). RENDER-2 retail tests: archers whose arrows leave
// the bow. A Gondor archer horde with drawables (DrawableManager) and the launch bone provider (DrawableLaunchBones) fires at an enemy horde; every arrow starts at its
// launcher's transform composed with the ARROW launch bone of the archer's model (GUArcher_SKN), which is checked against the bone worked out from the W3D hierarchy and
// animation files directly. Two runs give the same state hash in every frame. They run only when ROTWK_INSTALL and BFME2_INSTALL are set (otherwise SKIP).

#include "doctest.h"
#include "HudTestUtil.h"

#include "Common/PlayerList.h"
#include "Common/Team.h"
#include "GameClient/DrawableLaunchBones.h"
#include "GameClient/DrawableManager.h"
#include "GameClient/ClientEvents.h"
#include "GameClient/LogicSnapshot.h"
#include <functional>
#include "Common/NumericState.h"
#include "GameLogic/Object/PristinePose.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Combat/CombatState.h"
#include "GameLogic/Economy.h"
#include "GameLogic/EconomySettings.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/ProjectileModules.h"
#include "GameLogic/Object/Contain/HordeContainRuntime.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/RetailObjectWorld.h"
#include "Libraries/WWVegas/WW3D2/hanim.h"
#include "Libraries/WWVegas/WW3D2/htree.h"

#include "PathfindTestUtil.h"
#include "RetailTestMount.h"

#include <cfenv>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace
{
using hudtest::SharedWorld;
using hudtest::shared;

// the provider as the logic asks it, recording the launcher's transform, its animation state and the answer at the moment of the launch
struct Recorder : ProjectileLaunchOffsets
{
	struct Call
	{
		ObjectID launcher = 0;
		Coord3D pos{};
		float basis[9] = {};
		float bone[12] = {};
		bool found = false;
		std::string animState;
		const W3DModelDrawModuleData *data = nullptr;
		const ModelConditionInfo *modelState = nullptr;
		const AnimationStateInfo *state = nullptr;
	};
	DrawableLaunchBones &inner;
	std::vector<Call> calls;
	std::function<const Drawable *(const Object &)> drawableOf; ///< SMOOTH-1: the render side's drawable of an object (diagnostics of the test)
	explicit Recorder(DrawableLaunchBones &b) : inner(b) {}
	bool launchOffset(const Object &launcher, int wslot, int barrel, float launch[12]) override
	{
		Call c;
		c.launcher = launcher.getID();
		c.pos = *launcher.getPosition();
		std::memcpy(c.basis, launcher.getBasis(), sizeof(c.basis));
		c.found = inner.launchOffset(launcher, wslot, barrel, launch);
		if (c.found)
		{
			std::memcpy(c.bone, launch, sizeof(c.bone));
		}
		if (const Drawable *d = drawableOf ? drawableOf(launcher) : nullptr)
		{
			for (const DrawEntry &e : d->entries())
			{
				const W3DModelDrawModuleData *data = dynamic_cast<const W3DModelDrawModuleData *>(e.data);
				if (data && data->findBestAnimationState(d->getModelConditionFlags()))
				{
					c.data = data;
					c.modelState = data->findBestInfo(d->getModelConditionFlags());
					c.state = data->findBestAnimationState(d->getModelConditionFlags());
					c.animState = c.state->stateName;
					break;
				}
			}
		}
		calls.push_back(c);
		return c.found;
	}
};

struct LaunchArena
{
	std::unique_ptr<RetailObjectWorld::ContextScope> context;
	ArchiveW3DFileSource source;
	WW3DAssetManager assets;
	WW3DDrawAssets drawAssets;
	TeamFactory teams;
	PlayerList players;
	std::unique_ptr<DrawableLaunchBones> bones;
	std::unique_ptr<Recorder> recorder;
	std::unique_ptr<DrawableManager> drawables;
	GameLogic logic;
	std::unique_ptr<ClientEventRecorder> events;   // SMOOTH-1: the logic's client hooks; the drawables apply its events
	std::shared_ptr<const LogicSnapshot> lastSnapshot;
	AIWorldConfig cfg;
	pathtest::SyntheticTerrain terrain;
	std::unique_ptr<AIWorld> ai;

	LaunchArena(SharedWorld &s, bool withBones)
		: context(s.world->enterContext())
		, source(*s.mount->fs)
		, assets(source)
		, drawAssets(assets)
		, players(s.world->nameKeys(), s.world->playerTemplates(), teams)
		, logic(s.world->things(), s.world->modules(), players, RandomAlgorithm::ZH_CarryChain)
		, terrain(120, 120)
	{
		SkirmishSetup setup;
		setup.startingMoney = 0;
		setup.players.push_back({ "A", "FactionMen", true, 0, 0, 1 });
		setup.players.push_back({ "B", "FactionMordor", false, 1, 0, 2 });
		const std::vector<std::string> errors = players.setupSkirmish(setup);
		REQUIRE_MESSAGE(errors.empty(), (errors.empty() ? "" : errors[0]));
		std::string err;
		REQUIRE_MESSAGE(GameLogicSettingsLoader::load(*s.mount->fs, logic.settings(), &err), err);
		REQUIRE_MESSAGE(EconomySettings::load(*s.mount->fs, logic.economy().settings(), &err), err);
		logic.random().seedRandom(7);
		logic.economy().initAllCommandPoints();
		REQUIRE_MESSAGE(AIWorldConfigLoader::load(*s.mount->fs, cfg, &err), err);
		ai = std::make_unique<AIWorld>(logic, cfg, s.world->iniMacros());
		ai->attach();
		ai->newMap(terrain);
		drawables = std::make_unique<DrawableManager>(assets, logic);
		events = std::make_unique<ClientEventRecorder>(logic);
		logic.setClientHooks(events.get());
		bones = std::make_unique<DrawableLaunchBones>(drawAssets, logic.settings().standardPublicBones);
		recorder = std::make_unique<Recorder>(*bones);
		recorder->drawableOf = [this](const Object &o) { return drawableOf(o); };
		if (withBones)
		{
			logic.combat().setLaunchOffsets(recorder.get());
		}
	}
	// the drawable of an object after the events so far were applied
	const Drawable *drawableOf(const Object &o)
	{
		drawables->applyEvents(events->take());
		return drawables->findByObject(o.getID());
	}
	// one client frame: the events, the animations, the poses from the completed state's snapshot (the client never writes the logic)
	void clientFrame(double ms, double alpha)
	{
		drawables->applyEvents(events->take());
		lastSnapshot = LogicSnapshot::build(logic, lastSnapshot.get(), false, 0);
		drawables->advance(ms);
		drawables->syncTransforms(*lastSnapshot, alpha, true);
	}
	~LaunchArena()
	{
		logic.reset();
		logic.setClientHooks(nullptr);
		logic.combat().setLaunchOffsets(nullptr);
		ai.reset();
	}
	Player *player(int i) { return players.findPlayerWithName(i == 0 ? "A" : "B"); }
	Object *place(const char *templateName, int side, float x, float y)
	{
		const ThingTemplate *t = shared().world->things().findTemplate(templateName);
		REQUIRE_MESSAGE(t != nullptr, "retail template " << templateName);
		Object *o = logic.newObject(t, player(side)->getDefaultTeam(), ObjectStatusMaskType{});
		REQUIRE_MESSAGE(o != nullptr, templateName);
		Coord3D p{ x, y, 0.0f };
		o->setPosition(&p);
		o->setOrientation(side == 0 ? 0.0f : 3.14159265f);
		return o;
	}
};

BezierProjectileBehavior *bezierOf(Object *o)
{
	if (!o->isKindOf((unsigned)CombatNames::kinds().projectile))
	{
		return nullptr;
	}
	for (const std::unique_ptr<BehaviorModule> &m : o->modules())
	{
		if (BezierProjectileBehavior *b = dynamic_cast<BezierProjectileBehavior *>(m.get()))
		{
			return b;
		}
	}
	return nullptr;
}

struct Launch
{
	ObjectID arrow = 0, launcher = 0;
	Coord3D start{}, launcherPos{};
	float launcherBasis[9] = {};
	float bone[12] = {};
	bool boneFound = false;
	std::string animState;
	const W3DModelDrawModuleData *data = nullptr;
	const ModelConditionInfo *modelState = nullptr;
	const AnimationStateInfo *state = nullptr;
};

struct Run
{
	std::vector<Launch> launches;
	std::vector<std::uint32_t> hashes;
	unsigned long long launched = 0, bonesFound = 0;
	std::vector<std::string> problems;
};

// the archers fire for `frames` frames; every new arrow is recorded with its launcher's transform and the bone the provider gives for the launcher at that moment
Run run(bool withBones, int frames, bool clientFrames = false)
{
	SharedWorld &s = shared();
	Run out;
	LaunchArena a(s, withBones);
	Object *h[2] = { a.place("GondorArcherHorde", 0, 500.0f, 500.0f), a.place("MordorFighterHorde", 1, 700.0f, 500.0f) };
	a.logic.runLogicFrame();
	a.logic.runLogicFrame();
	REQUIRE(h[0]->getAIUpdateInterface()->aiAttackObject(h[1], CMD_FROM_PLAYER));
	std::map<ObjectID, bool> seen;
	for (int f = 0; f < frames; ++f)
	{
		a.logic.runLogicFrame();
		if (clientFrames)
		{
			// six client frames per logic frame: animations, render poses, the construction look (the client never writes the logic)
			for (int c = 0; c < 6; ++c)
			{
				a.clientFrame(1000.0 / 30.0, (double)c / 6.0);
			}
		}
		out.hashes.push_back(a.logic.computeStateHash());
		for (Object *o = a.logic.getFirstObject(); o; o = o->getNextObject())
		{
			BezierProjectileBehavior *b = bezierOf(o);
			if (!b || seen[o->getID()])
			{
				continue;
			}
			seen[o->getID()] = true;
			Launch l;
			l.arrow = o->getID();
			l.launcher = b->projectileGetLauncherID();
			l.start = b->flightStart();
			// this frame's call for that launcher (an archer fires at most once a frame)
			for (auto it = a.recorder->calls.rbegin(); it != a.recorder->calls.rend(); ++it)
			{
				if (it->launcher == l.launcher)
				{
					l.launcherPos = it->pos;
					std::memcpy(l.launcherBasis, it->basis, sizeof(l.launcherBasis));
					std::memcpy(l.bone, it->bone, sizeof(l.bone));
					l.boneFound = it->found;
					l.animState = it->animState;
					l.data = it->data;
					l.modelState = it->modelState;
					l.state = it->state;
					break;
				}
			}
			out.launches.push_back(l);
		}
	}
	out.launched = a.logic.combat().counters().projectilesLaunched;
	out.bonesFound = a.logic.combat().counters().launchBonesFound;
	out.problems = a.bones->problems();
	return out;
}

// The ARROW bone of GUArcher_SKL worked out from the files alone: the pivots' base transforms (W3D_CHUNK_PIVOTS: translation and quaternion, as written), the
// animation's channels at `frame` replacing them as ZH Anim_Update does (translation added to the base translation, rotation post-multiplied), multiplied along the
// parent chain in double precision. An independent evaluation (no HTreeClass pose code), compared within a float tolerance.
struct Mat
{
	double m[3][4];
	static Mat identity()
	{
		Mat r;
		for (int i = 0; i < 3; ++i)
		{
			for (int j = 0; j < 4; ++j)
			{
				r.m[i][j] = i == j ? 1.0 : 0.0;
			}
		}
		return r;
	}
	static Mat fromQT(const double q[4], const double t[3])
	{
		const double x = q[0], y = q[1], z = q[2], w = q[3];
		Mat r;
		r.m[0][0] = 1 - 2 * (y * y + z * z);
		r.m[0][1] = 2 * (x * y - z * w);
		r.m[0][2] = 2 * (z * x + y * w);
		r.m[1][0] = 2 * (x * y + z * w);
		r.m[1][1] = 1 - 2 * (z * z + x * x);
		r.m[1][2] = 2 * (y * z - x * w);
		r.m[2][0] = 2 * (z * x - y * w);
		r.m[2][1] = 2 * (y * z + x * w);
		r.m[2][2] = 1 - 2 * (y * y + x * x);
		r.m[0][3] = t[0];
		r.m[1][3] = t[1];
		r.m[2][3] = t[2];
		return r;
	}
	Mat operator*(const Mat &b) const
	{
		Mat r;
		for (int i = 0; i < 3; ++i)
		{
			for (int j = 0; j < 4; ++j)
			{
				double v = 0.0;
				for (int k = 0; k < 3; ++k)
				{
					v += m[i][k] * b.m[k][j];
				}
				r.m[i][j] = v + (j == 3 ? m[i][3] : 0.0);
			}
		}
		return r;
	}
};

Mat handBone(const HTreeClass &tree, const HAnimClass *anim, float frame, const std::string &bone)
{
	const int target = tree.Get_Bone_Index(bone);
	REQUIRE(target > 0);
	std::vector<int> chain;
	for (int i = target; i > 0; i = tree.Get_Pivot(i).ParentIdx)
	{
		chain.push_back(i);
	}
	Mat world = Mat::identity();
	for (auto it = chain.rbegin(); it != chain.rend(); ++it)
	{
		const PivotClass &p = tree.Get_Pivot(*it);
		double q[4] = { p.BaseQuat.X, p.BaseQuat.Y, p.BaseQuat.Z, p.BaseQuat.W };
		double t[3] = { p.BaseTrans.X, p.BaseTrans.Y, p.BaseTrans.Z };
		Mat local = Mat::fromQT(q, t);
		if (anim)
		{
			Vector3 tr;
			anim->Get_Translation(tr, *it, frame);
			for (int r = 0; r < 3; ++r) // ZH Matrix3D::Translate: the translation in the base transform's own axes
			{
				local.m[r][3] += local.m[r][0] * tr.X + local.m[r][1] * tr.Y + local.m[r][2] * tr.Z;
			}
			Quaternion aq;
			if (anim->Get_Orientation(aq, *it, frame))
			{
				const double qa[4] = { aq.X, aq.Y, aq.Z, aq.W };
				const double zero[3] = { 0, 0, 0 };
				local = local * Mat::fromQT(qa, zero);
			}
		}
		world = world * local;
	}
	return world;
}
} // namespace

TEST_CASE("render2 retail: Gondor archers' arrows leave the ARROW bone of the bow - the launcher's transform times the pristine bone, checked against the W3D files")
{
	if (!hudtest::haveWorld("render2 retail launch"))
	{
		return;
	}
	const Run r = run(true, 120);
	REQUIRE(r.launched >= 6);
	CHECK(r.bonesFound == r.launched); // every archer's drawable named its launch bone
	for (const std::string &p : r.problems)
	{
		std::printf("  info: launch bone problem: %s\n", p.c_str());
	}
	REQUIRE(!r.launches.empty());
	int checked = 0;
	for (const Launch &l : r.launches)
	{
		if (!l.boneFound)
		{
			continue;
		}
		// the arrow's start = T(launcher) * B (RW 0x70BCE7), the bone translation turned by the launcher's basis
		const float *t = l.launcherBasis;
		const double bx = l.bone[3], by = l.bone[7], bz = l.bone[11];
		const double ex = t[0] * bx + t[1] * by + t[2] * bz + l.launcherPos.x;
		const double ey = t[3] * bx + t[4] * by + t[5] * bz + l.launcherPos.y;
		const double ez = t[6] * bx + t[7] * by + t[8] * bz + l.launcherPos.z;
		CHECK(std::fabs(l.start.x - ex) < 1e-3);
		CHECK(std::fabs(l.start.y - ey) < 1e-3);
		CHECK(std::fabs(l.start.z - ez) < 1e-3);
		// a bow is held above the ground and near the archer
		CHECK(l.start.z - l.launcherPos.z > 5.0f);
		CHECK(l.start.z - l.launcherPos.z < 40.0f);
		++checked;
	}
	CHECK(checked >= 6);

	// the bone itself from the files: GUArcher_SKN on GUArcher_SKL; the pose of the animation state the archer was in when it fired (its first animation at
	// FrameForPristineBonePositions, clamped), evaluated independently
	SharedWorld &s = shared();
	ArchiveW3DFileSource source(*s.mount->fs);
	WW3DAssetManager assets(source);
	WW3DDrawAssets drawAssets(assets);
	std::string err;
	const RenderObjPrototype *proto = assets.Create_Render_Obj("GUArcher_SKN", &err);
	REQUIRE_MESSAGE(proto, err);
	REQUIRE(proto->Tree != nullptr);
	const Launch &first = r.launches[0];
	REQUIRE(first.boneFound);
	REQUIRE(first.data != nullptr);
	REQUIRE(first.modelState != nullptr);
	CHECK(first.modelState->modelName() == "GUArcher_SKN");
	const W3DModelDrawModuleData *data = first.data;
	const AnimationStateInfo *state = first.state;
	DrawableLaunchBones bones(drawAssets, {});
	const DrawableLaunchBones::PoseInfo pose = bones.poseOf(*data, DrawableLaunchBones::ModuleKey{ "probe", 0 }, *first.modelState, state, 1.0f);
	const HAnimClass *anim = nullptr;
	if (!pose.animation.empty())
	{
		W3DAnimationLookup lookup;
		lookup.names = { pose.animation };
		std::string resolved;
		anim = drawAssets.animation(lookup, &resolved, &err);
		REQUIRE_MESSAGE(anim != nullptr, err);
	}
	// the launch bone: ARROW01.. when numbered bones exist, else ARROW
	const std::string boneName = proto->Tree->Get_Bone_Index("ARROW01") > 0 ? "ARROW01" : "ARROW";
	const Mat hand = handBone(*proto->Tree, anim, pose.frame, boneName);
	std::printf("  info: GondorArcher fired in %s: pose %s frame %.0f, bone %s at (%.4f, %.4f, %.4f), provider (%.4f, %.4f, %.4f)\n", first.animState.c_str(),
		pose.animation.empty() ? "(bind)" : pose.animation.c_str(), (float)pose.frame, boneName.c_str(), hand.m[0][3], hand.m[1][3], hand.m[2][3], first.bone[3], first.bone[7],
		first.bone[11]);
	CHECK(std::fabs(first.bone[3] - hand.m[0][3]) < 1e-3);
	CHECK(std::fabs(first.bone[7] - hand.m[1][3]) < 1e-3);
	CHECK(std::fabs(first.bone[11] - hand.m[2][3]) < 1e-3);
	for (int i = 0; i < 3; ++i)
	{
		for (int j = 0; j < 3; ++j)
		{
			CHECK(std::fabs(first.bone[i * 4 + j] - hand.m[i][j]) < 1e-4);
		}
	}
}

TEST_CASE("render2 retail: the same volleys with launch bones twice give the same state hash in every frame; without the bones the arrows start elsewhere")
{
	if (!hudtest::haveWorld("render2 retail determinism"))
	{
		return;
	}
	const Run a = run(true, 100);
	const Run b = run(true, 100);
	REQUIRE(a.hashes.size() == b.hashes.size());
	for (size_t i = 0; i < a.hashes.size(); ++i)
	{
		REQUIRE_MESSAGE(a.hashes[i] == b.hashes[i], "frame " << i);
	}
	REQUIRE(a.launched > 0);
	CHECK(a.launched == b.launched);
	const Run c = run(false, 100);
	REQUIRE(!c.launches.empty());
	CHECK(c.bonesFound == 0);
	// without a provider an arrow starts at its launcher's own position (the identity bone)
	CHECK(c.launches[0].start.z == c.launches[0].launcherPos.z);
	CHECK(a.hashes.back() != c.hashes.back());
}

TEST_CASE("render2 retail: every model draw that names a launch bone answers RW 0x4C34A2 for its default state without a fault; the problems are listed")
{
	if (!hudtest::haveWorld("render2 retail sweep"))
	{
		return;
	}
	SharedWorld &s = shared();
	const auto context = s.world->enterContext();
	ArchiveW3DFileSource source(*s.mount->fs);
	WW3DAssetManager assets(source);
	WW3DDrawAssets drawAssets(assets);
	DrawableLaunchBones bones(drawAssets, {});
	int modules = 0, withBone = 0, answered = 0;
	for (const ThingTemplate *t : s.world->things().templates())
	{
		for (const ThingTemplate::Nugget &n : t->drawModules().nuggets())
		{
			const W3DModelDrawModuleData *data = dynamic_cast<const W3DModelDrawModuleData *>(n.data.get());
			if (!data)
			{
				continue;
			}
			++modules;
			bool names = false;
			for (const ModelConditionInfo &st : data->m_conditionStates)
			{
				for (const std::string &b : st.weaponLaunchBone)
				{
					names = names || !b.empty();
				}
			}
			if (!names)
			{
				continue;
			}
			++withBone;
			float m[12];
			bool any = false;
			for (int slot = 0; slot < W3D_WEAPONSLOT_COUNT && !any; ++slot)
			{
				any = bones.getProjectileLaunchOffset(*data, DrawableLaunchBones::ModuleKey{ t->getName(), modules }, ModelConditionFlags(), 1.0f, 0.0f, nullptr, slot, 0, m);
			}
			answered += any ? 1 : 0;
		}
	}
	std::printf("  info: %d model draws, %d name a launch bone, %d answer for their default state; %zu cached states, %zu problems\n", modules, withBone, answered,
		bones.cachedStates(), bones.problems().size());
	for (size_t i = 0; i < bones.problems().size() && i < 12; ++i)
	{
		std::printf("  info:   %s\n", bones.problems()[i].c_str());
	}
	CHECK(withBone > 100);
	CHECK(answered > withBone / 2);
}

// ---------------------------------------------------------------------------------------------------------------------
// Review r1: the launch bones are simulation input
// ---------------------------------------------------------------------------------------------------------------------
namespace
{
// one answer per (model draw that names a launch bone, animation state's condition set, slot 0..4): found flag and the 12 floats' bits
std::vector<std::uint32_t> sweepAnswers(SharedWorld &s, int rounding, bool prewarm)
{
	ArchiveW3DFileSource source(*s.mount->fs);
	WW3DAssetManager assets(source);
	WW3DDrawAssets drawAssets(assets);
	const int saved = std::fegetround();
	if (prewarm)
	{
		// the render side loads the models and animations first, in another rounding mode (the loaders decode the drawn values then)
		std::fesetround(FE_UPWARD);
		for (const ThingTemplate *t : s.world->things().templates())
		{
			for (const ThingTemplate::Nugget &n : t->drawModules().nuggets())
			{
				const W3DModelDrawModuleData *data = dynamic_cast<const W3DModelDrawModuleData *>(n.data.get());
				if (!data)
				{
					continue;
				}
				for (const ModelConditionInfo &ms : data->m_conditionStates)
				{
					std::string err;
					drawAssets.model(ms.modelName(), &err);
					for (const AnimationStateInfo &as : data->m_animationStates)
					{
						if (!as.animations.empty())
						{
							W3DAnimationLookup lookup;
							lookup.skeleton = ms.skeleton;
							lookup.modelAnimationPrefix = ms.modelAnimationPrefix;
							lookup.names = as.animations[0].animationNames;
							std::string resolved;
							drawAssets.animation(lookup, &resolved, &err);
						}
					}
				}
			}
		}
	}
	std::fesetround(rounding);
	DrawableLaunchBones bones(drawAssets, {});
	std::vector<std::uint32_t> out;
	for (const ThingTemplate *t : s.world->things().templates())
	{
		const std::vector<ThingTemplate::Nugget> &nuggets = t->drawModules().nuggets();
		for (size_t k = 0; k < nuggets.size(); ++k)
		{
			const W3DModelDrawModuleData *data = dynamic_cast<const W3DModelDrawModuleData *>(nuggets[k].data.get());
			bool names = false;
			for (const ModelConditionInfo &st : data ? data->m_conditionStates : std::vector<ModelConditionInfo>())
			{
				for (const std::string &b : st.weaponLaunchBone)
				{
					names = names || !b.empty();
				}
			}
			if (!names)
			{
				continue;
			}
			std::vector<ModelConditionFlags> sets{ ModelConditionFlags() };
			for (const AnimationStateInfo &as : data->m_animationStates)
			{
				sets.push_back(as.conditions);
			}
			for (const ModelConditionFlags &f : sets)
			{
				for (int slot = 0; slot < W3D_WEAPONSLOT_COUNT; ++slot)
				{
					float m[12] = {};
					const bool found = bones.getProjectileLaunchOffset(*data, DrawableLaunchBones::ModuleKey{ t->getName(), (int)k }, f, 1.0f, 0.0f, nullptr, slot,
						0, m);
					out.push_back(found ? 1u : 0u);
					for (float v : m)
					{
						std::uint32_t u;
						std::memcpy(&u, &v, 4);
						out.push_back(u);
					}
				}
			}
		}
	}
	CHECK(std::fegetround() == rounding); // the evaluation restored the caller's environment
	std::fesetround(saved);
	NumericState::normalizeFloatingPointEnvironment();
	return out;
}
} // namespace

TEST_CASE("render2 retail: the launch bones are bit identical in every rounding mode, cold or after the render side loaded the assets in another mode")
{
	if (!hudtest::haveWorld("render2 retail rounding"))
	{
		return;
	}
	// the filter table alone, computed afresh under each mode
	float nearest[256], down[256], up[256];
	PristinePose::computeFilterTable(nearest);
	std::fesetround(FE_DOWNWARD);
	PristinePose::computeFilterTable(down);
	std::fesetround(FE_UPWARD);
	PristinePose::computeFilterTable(up);
	std::fesetround(FE_TONEAREST);
	NumericState::normalizeFloatingPointEnvironment();
	CHECK(std::memcmp(nearest, down, sizeof(nearest)) == 0);
	CHECK(std::memcmp(nearest, up, sizeof(nearest)) == 0);

	SharedWorld &s = shared();
	const auto context = s.world->enterContext();
	const std::vector<std::uint32_t> a = sweepAnswers(s, FE_TONEAREST, false);
	const std::vector<std::uint32_t> b = sweepAnswers(s, FE_DOWNWARD, false);
	const std::vector<std::uint32_t> c = sweepAnswers(s, FE_TOWARDZERO, true);
	REQUIRE(a.size() > 1000);
	size_t found = 0;
	for (size_t i = 0; i < a.size(); i += 13)
	{
		found += a[i];
	}
	std::printf("  info: %zu launch-bone answers compared (%zu found a bone) under nearest, downward and (render prewarmed) toward-zero rounding\n", a.size() / 13,
		found);
	CHECK(found > 300);
	CHECK(a == b);
	CHECK(a == c);
}

TEST_CASE("render2 retail: a launcher without a drawable gets the same launch bone; the provider's flags are the drawable's; the scale is logic state")
{
	if (!hudtest::haveWorld("render2 retail no drawable"))
	{
		return;
	}
	SharedWorld &s = shared();
	LaunchArena a(s, true);
	Object *horde = a.place("GondorArcherHorde", 0, 500.0f, 500.0f);
	a.logic.runLogicFrame();
	a.logic.runLogicFrame();
	Object *archer = nullptr;
	for (Object *m : *horde->getContain()->getContainedItemsList())
	{
		archer = archer ? archer : m;
	}
	REQUIRE(archer != nullptr);
	const Drawable *d = a.drawableOf(*archer);
	REQUIRE(d != nullptr);
	// the flags the provider derives from logic state are the drawable's
	const ModelConditionFlags derived = DrawableLaunchBones::launchFlags(*archer);
	for (int bit = 0; bit < MODELCONDITION_COUNT; ++bit)
	{
		CHECK_MESSAGE(derived.test(bit) == d->getModelConditionFlags().test(bit), "bit " << bit);
	}
	CHECK(archer->getInstanceScale() == d->getInstanceScale());
	float withDrawable[12], without[12];
	REQUIRE(a.bones->launchOffset(*archer, 0, 0, withDrawable));
	// no drawable at all: the same bits (and a fresh provider, cold cache, the same again)
	archer->friend_bindToClient(nullptr);
	REQUIRE(a.bones->launchOffset(*archer, 0, 0, without));
	CHECK(std::memcmp(withDrawable, without, sizeof(without)) == 0);
	const unsigned generation = a.bones->generation();
	a.bones->reset();
	CHECK(a.bones->generation() == generation + 1);
	CHECK(a.bones->cachedStates() == 0);
	REQUIRE(a.bones->launchOffset(*archer, 0, 0, without));
	CHECK(std::memcmp(withDrawable, without, sizeof(without)) == 0);
	archer->friend_bindToClient(a.events.get());
	// the instance scale is logic state: twice the scale, twice the bone, and the state hash sees it
	const std::uint32_t h0 = a.logic.computeStateHash();
	archer->setInstanceScale(archer->getInstanceScale() * 2.0f);
	CHECK(a.logic.computeStateHash() != h0);
	float doubled[12];
	REQUIRE(a.bones->launchOffset(*archer, 0, 0, doubled));
	CHECK(doubled[11] == withDrawable[11] * 2.0f);
	archer->setInstanceScale(archer->getInstanceScale() * 0.5f);
	CHECK(a.logic.computeStateHash() == h0);
}

TEST_CASE("render2 retail: client frames do not change the launches - the same volleys with and without rendering give the same hash in every frame")
{
	if (!hudtest::haveWorld("render2 retail client frames"))
	{
		return;
	}
	const Run bare = run(true, 80, false);
	const Run drawn = run(true, 80, true);
	REQUIRE(bare.hashes.size() == drawn.hashes.size());
	for (size_t i = 0; i < bare.hashes.size(); ++i)
	{
		REQUIRE_MESSAGE(bare.hashes[i] == drawn.hashes[i], "frame " << i);
	}
	CHECK(bare.bonesFound > 0);
	CHECK(bare.bonesFound == drawn.bonesFound);
}
