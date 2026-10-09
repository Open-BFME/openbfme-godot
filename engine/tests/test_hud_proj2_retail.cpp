// File name: shares the retail world of the HUD tests (hudtest::shared(); doctest runs the test_hud_* files in name order). PROJ-2 retail tests: siege stones that look
// like retail. A Gondor trebuchet and a Mordor catapult (with drawables and the launch bone provider) shell an enemy building on a flat arena; every launch is recorded
// with the launcher's model condition flags, its transform and the bone the provider answered, and the first stone is followed frame by frame (logic positions and the
// positions the drawable is drawn at). Expected values come from the retail INI data and the binary's formulas, evaluated independently here: the launch bone from the
// W3D hierarchy and animation files at the attack state's FrameForPristineBonePositions, the arc from the Bezier control points of RW 0x85E658 in double precision.
// They run only when ROTWK_INSTALL and BFME2_INSTALL are set (otherwise SKIP).

#include "doctest.h"
#include "HudTestUtil.h"
#include "PeImage.h"

#include "Common/ModelState.h"
#include "Common/PlayerList.h"
#include "Common/Team.h"
#include "GameClient/ClientEvents.h"
#include "GameClient/DrawableLaunchBones.h"
#include "GameClient/DrawableManager.h"
#include "GameClient/LogicSnapshot.h"
#include "GameClient/RenderInterpolation.h"
#include "GameLogic/AI/AIWorld.h"
#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/Combat/CombatState.h"
#include "GameLogic/Economy.h"
#include "GameLogic/EconomySettings.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Module/ProjectileModules.h"
#include "GameLogic/Object/Object.h"
#include "GameLogic/Object/RetailObjectWorld.h"
#include "GameLogic/Weapon.h"
#include "Libraries/WWVegas/WW3D2/hanim.h"
#include "Libraries/WWVegas/WW3D2/htree.h"

#include "PathfindTestUtil.h"
#include "RetailTestMount.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace
{
using hudtest::SharedWorld;
using hudtest::shared;

std::string flagNames(const Object &o)
{
	std::string s;
	for (int bit = 0; bit < 19 * 32; ++bit)
	{
		if (o.testModelCondition(bit))
		{
			s += std::string(ModelCondition::nameOf(bit)) + " ";
		}
	}
	return s;
}

// the provider as the logic asks it: the launcher's flags, transform and drawable state at the moment of the launch, and the answer
struct LaunchRecorder : ProjectileLaunchOffsets
{
	struct Call
	{
		ObjectID launcher = 0;
		unsigned frame = 0;
		Coord3D pos{};
		float basis[9] = {};
		float bone[12] = {};
		bool found = false;
		bool firing = false, firingOrPreattack = false;
		std::string flags;
		const W3DModelDrawModuleData *data = nullptr;
		const ModelConditionInfo *modelState = nullptr;
		const AnimationStateInfo *state = nullptr;
	};
	DrawableLaunchBones &inner;
	std::vector<Call> calls;
	std::function<const Drawable *(const Object &)> drawableOf; ///< SMOOTH-1: the render side's drawable of an object (the test's diagnostics)
	explicit LaunchRecorder(DrawableLaunchBones &b) : inner(b) {}
	bool launchOffset(const Object &launcher, int wslot, int barrel, float launch[12]) override
	{
		Call c;
		c.launcher = launcher.getID();
		c.frame = launcher.logic().getFrame();
		c.pos = *launcher.getPosition();
		std::memcpy(c.basis, launcher.getBasis(), sizeof(c.basis));
		c.flags = flagNames(launcher);
		c.firing = launcher.testModelCondition(ModelCondition::indexOf("FIRING_A"));
		c.firingOrPreattack = launcher.testModelCondition(ModelCondition::indexOf("FIRING_OR_PREATTACK_A"));
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
					break;
				}
			}
		}
		calls.push_back(c);
		return c.found;
	}
};

struct SiegeArena
{
	std::unique_ptr<RetailObjectWorld::ContextScope> context;
	ArchiveW3DFileSource source;
	WW3DAssetManager assets;
	WW3DDrawAssets drawAssets;
	TeamFactory teams;
	PlayerList players;
	std::unique_ptr<DrawableLaunchBones> bones;
	std::unique_ptr<LaunchRecorder> recorder;
	std::unique_ptr<DrawableManager> drawables;
	GameLogic logic;
	std::unique_ptr<ClientEventRecorder> events; // SMOOTH-1: the logic's client hooks; the drawables apply its events
	std::shared_ptr<const LogicSnapshot> lastSnapshot;
	AIWorldConfig cfg;
	pathtest::SyntheticTerrain terrain;
	std::unique_ptr<AIWorld> ai;

	SiegeArena(SharedWorld &s, const char *factionA, const char *factionB)
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
		setup.players.push_back({ "A", factionA, true, 0, 0, 1 });
		setup.players.push_back({ "B", factionB, false, 1, 0, 2 });
		const std::vector<std::string> errors = players.setupSkirmish(setup);
		REQUIRE_MESSAGE(errors.empty(), (errors.empty() ? "" : errors[0]));
		std::string err;
		REQUIRE_MESSAGE(GameLogicSettingsLoader::load(*s.mount->fs, logic.settings(), &err), err);
		REQUIRE_MESSAGE(EconomySettings::load(*s.mount->fs, logic.economy().settings(), &err), err);
		logic.productionSettings() = s.world->productionSettings(); // the drawables' construction look asks the build time of a damaged building
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
		recorder = std::make_unique<LaunchRecorder>(*bones);
		recorder->drawableOf = [this](const Object &o) { return drawableOf(o); };
		logic.combat().setLaunchOffsets(recorder.get());
	}
	// the drawable of an object after the events so far were applied
	Drawable *drawableOf(const Object &o)
	{
		drawables->applyEvents(events->take());
		return drawables->findByObject(o.getID());
	}
	// the completed frame as the client sees it: the events, then the snapshot (the client never writes the logic)
	void publish()
	{
		drawables->applyEvents(events->take());
		lastSnapshot = LogicSnapshot::build(logic, lastSnapshot.get(), false, 0);
	}
	// one client frame of `ms` at `alpha` on the published snapshot
	void clientFrame(double ms, double alpha)
	{
		drawables->advance(ms);
		drawables->syncTransforms(*lastSnapshot, alpha, true);
	}
	~SiegeArena()
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

BezierProjectileBehavior *launchedBezierOf(Object *o, ObjectID launcher)
{
	for (const std::unique_ptr<BehaviorModule> &m : o->modules())
	{
		if (BezierProjectileBehavior *b = dynamic_cast<BezierProjectileBehavior *>(m.get()))
		{
			return (!b->flightPath().empty() && b->projectileGetLauncherID() == launcher) ? b : nullptr;
		}
	}
	return nullptr;
}

struct Frame
{
	unsigned frame = 0;
	size_t step = 0;
	Coord3D pos{}, recorded{};
	ObjectSnapshot rec;             // the projectile's record in the published snapshot of the frame
	std::vector<Coord3D> drawn;     // the drawable's position in the client frames of kAlphas
	std::vector<Coord3D> expected;  // RenderInterpolation::retailPose of the record at the same alphas
	std::vector<float> opacity;     // Drawable::drawOpacity in those client frames
};
// six client frames per logic frame (retail's 30 Hz client over its 5 Hz logic)
const double kAlphas[] = { 0.0, 1.0 / 6.0, 2.0 / 6.0, 3.0 / 6.0, 4.0 / 6.0, 5.0 / 6.0 };
const double kClientFrameMs = 1000.0 / 30.0;

struct Stone
{
	ObjectID id = 0;
	unsigned launchFrame = 0;
	int gone = -1; // the frame the object was no longer there
	std::string templateName;
	std::vector<Coord3D> path;
	Coord3D start{}, end{};
	int segments = 0;
	float speed = 0.0f;
	const BezierProjectileBehaviorModuleData *data = nullptr;
	std::vector<Frame> frames;
};

struct SiegeRun
{
	std::vector<LaunchRecorder::Call> calls;
	std::vector<Stone> stones;
	std::vector<std::string> launcherFlags;     // after every frame
	std::vector<std::string> launcherAnimState; // the launcher's first model draw's animation state after every frame
	std::vector<std::uint32_t> hashes;
	std::vector<unsigned> loopFrames;           // the logic frame of every loop index
	Coord3D launcherPos{};
	float launcherBasis[9] = {};
};

// the launcher at (500, 500) facing +x shells the building `dist` east of it for `frames` frames; every stone is followed (two at most)
SiegeRun runSiege(const char *faction, const char *launcher, const char *enemyFaction, const char *target, float dist, int frames)
{
	SharedWorld &s = shared();
	SiegeRun out;
	SiegeArena a(s, faction, enemyFaction);
	Object *l = a.place(launcher, 0, 500.0f, 500.0f);
	Object *t = a.place(target, 1, 500.0f + dist, 500.0f);
	a.logic.runLogicFrame();
	a.logic.runLogicFrame();
	REQUIRE(l->getAIUpdateInterface()->aiForceAttackObject(t, CMD_FROM_PLAYER));
	out.launcherPos = *l->getPosition();
	std::memcpy(out.launcherBasis, l->getBasis(), sizeof(out.launcherBasis));
	for (int f = 0; f < frames; ++f)
	{
		a.logic.runLogicFrame();
		out.hashes.push_back(a.logic.computeStateHash());
		out.loopFrames.push_back(a.logic.getFrame());
		out.launcherFlags.push_back(flagNames(*l));
		a.publish();
		std::string anim;
		if (const Drawable *d = a.drawables->findByObject(l->getID()))
		{
			for (const DrawEntry &e : d->entries())
			{
				if (e.draw && e.draw->currentAnimationState())
				{
					anim = e.draw->currentAnimationState()->stateName;
					break;
				}
			}
		}
		out.launcherAnimState.push_back(anim);
		std::map<ObjectID, bool> seen;
		for (Object *o = a.logic.getFirstObject(); o; o = o->getNextObject())
		{
			// a thrown object of this launcher: a projectile, or any object whose BezierProjectileBehavior was launched (the troll sling throws a ROCK prop)
			BezierProjectileBehavior *b = launchedBezierOf(o, l->getID());
			if (!b)
			{
				continue;
			}
			seen[o->getID()] = true;
			Stone *st = nullptr;
			for (Stone &x : out.stones)
			{
				st = x.id == o->getID() ? &x : st;
			}
			if (!st)
			{
				if (out.stones.size() >= 2)
				{
					continue;
				}
				out.stones.emplace_back();
				st = &out.stones.back();
				st->id = o->getID();
				st->launchFrame = a.logic.getFrame();
				st->templateName = o->getTemplate()->getName();
				st->path = b->flightPath();
				st->start = b->flightStart();
				st->end = b->flightEnd();
				st->segments = b->segments();
				st->speed = b->flightSpeed();
				st->data = b->data();
			}
			Frame fr;
			fr.frame = a.logic.getFrame();
			fr.step = b->currentStep();
			fr.pos = *o->getPosition();
			fr.recorded = o->getRecordedPosition();
			if (const ObjectSnapshot *rec = a.lastSnapshot->find(o->getID()))
			{
				fr.rec = *rec;
			}
			st->frames.push_back(fr);
		}
		// the client frames of this logic frame: the drawn position, retail's pose of the record, the fade
		for (double alpha : kAlphas)
		{
			a.clientFrame(kClientFrameMs, alpha);
			for (Stone &st : out.stones)
			{
				if (st.frames.empty() || st.frames.back().frame != a.logic.getFrame())
				{
					continue;
				}
				Frame &fr = st.frames.back();
				if (const Drawable *d = a.drawables->findByObject(st.id))
				{
					fr.drawn.push_back(*d->getPosition());
					fr.expected.push_back(RenderInterpolation::retailPose(fr.rec, a.lastSnapshot->frame, alpha).position);
					fr.opacity.push_back(d->drawOpacity());
				}
			}
		}
		for (Stone &st : out.stones)
		{
			if (st.gone < 0 && !seen.count(st.id))
			{
				st.gone = (int)a.logic.getFrame();
			}
		}
	}
	out.calls = a.recorder->calls;
	return out;
}

// ---- an independent evaluation of the launch bone from the W3D files (see test_hud_render2_retail.cpp for the derivation) ----
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

Mat fileBone(const HTreeClass &tree, const HAnimClass *anim, float frame, int target)
{
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
		double tr0[3] = { p.BaseTrans.X, p.BaseTrans.Y, p.BaseTrans.Z };
		Mat local = Mat::fromQT(q, tr0);
		if (anim)
		{
			Vector3 tr;
			anim->Get_Translation(tr, *it, frame);
			for (int r = 0; r < 3; ++r)
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

// the cubic Bezier of RW 0x85E658's control points (flat ground: the highest terrain along the line is 0) in double precision
struct Curve
{
	double cp[4][3];
	void at(double t, double out[3]) const
	{
		const double u = 1.0 - t;
		const double b[4] = { u * u * u, 3 * u * u * t, 3 * u * t * t, t * t * t };
		for (int k = 0; k < 3; ++k)
		{
			out[k] = b[0] * cp[0][k] + b[1] * cp[1][k] + b[2] * cp[2][k] + b[3] * cp[3][k];
		}
	}
};
Curve curveOf(const Stone &st)
{
	const BezierProjectileBehaviorModuleData &d = *st.data;
	Curve c;
	const double s[3] = { st.start.x, st.start.y, st.start.z }, e[3] = { st.end.x, st.end.y, st.end.z };
	const double highest = std::max({ 0.0, s[2], e[2] });
	for (int k = 0; k < 3; ++k)
	{
		c.cp[0][k] = s[k];
		c.cp[3][k] = e[k];
	}
	for (int k = 0; k < 2; ++k)
	{
		c.cp[1][k] = s[k] + (e[k] - s[k]) * d.m_firstPercentIndent;
		c.cp[2][k] = s[k] + (e[k] - s[k]) * d.m_secondPercentIndent;
	}
	c.cp[1][2] = highest + d.m_firstHeight;
	c.cp[2][2] = highest + d.m_secondHeight;
	return c;
}

struct Case
{
	const char *faction, *launcher, *enemyFaction, *target, *weapon, *projectile, *attackState;
	float dist;
	float weaponSpeedPerSecond, firstHeight, secondHeight, firstPct, secondPct; // the retail INI values (weapon.ini, the projectile's BezierProjectileBehavior)
	int pristineFrame;                                                          // FrameForPristineBonePositions of the attack state
	const char *model, *animation;
};
const Case kCases[] = {
	{ "FactionMen", "GondorTrebuchet", "FactionMordor", "MordorBarracks", "GondorTrebuchetRock", "GondorTrebuchetRockProjectile", "Attacking", 420.0f, 321.0f, 73.0f, 100.0f,
		0.30f, 0.70f, 35, "GUSiegTreb_SKN", "GUSiegTreb_SKL.GUSiegTreb_ATAK" },
	{ "FactionMordor", "MordorCatapult", "FactionMen", "GondorBarracks", "MordorCatapultRock", "MordorCatapultRockProjectile", "Attack", 420.0f, 201.0f, 53.0f, 53.0f, 0.20f,
		0.80f, 24, "MUCatapult_SKN", "MUCatapult_SKL.MUCatapult_ATKA" },
};

const SiegeRun &cachedRun(size_t i)
{
	static std::map<size_t, SiegeRun> runs;
	auto it = runs.find(i);
	if (it == runs.end())
	{
		const Case &c = kCases[i];
		it = runs.emplace(i, runSiege(c.faction, c.launcher, c.enemyFaction, c.target, c.dist, 60)).first;
	}
	return it->second;
}
} // namespace

TEST_CASE("proj2 retail: a siege stone leaves the launch bone of the THROW - the attack state's pristine pose at FrameForPristineBonePositions (RW 0x74C4A7 -> 0x69036C sets FIRING before the shot)")
{
	if (!hudtest::haveWorld("proj2 retail launch"))
	{
		return;
	}
	for (size_t i = 0; i < sizeof(kCases) / sizeof(kCases[0]); ++i)
	{
		const Case &c = kCases[i];
		INFO(std::string(c.launcher));
		const SiegeRun &r = cachedRun(i);
		REQUIRE(!r.calls.empty());
		REQUIRE(!r.stones.empty());
		const LaunchRecorder::Call &call = r.calls.front();
		INFO("flags at the launch: " << call.flags);
		// the shot's flags: FIRING_A and FIRING_OR_PREATTACK_A (not the BETWEEN_FIRING_SHOTS_A the status helper computes for a ready weapon)
		CHECK(call.firing);
		CHECK(call.firingOrPreattack);
		CHECK(call.flags.find("BETWEEN_FIRING_SHOTS_A") == std::string::npos);
		REQUIRE(call.found);
		REQUIRE(call.state != nullptr);
		CHECK(call.state->stateName == c.attackState);
		CHECK(call.state->frameForPristineBonePositions == c.pristineFrame);
		REQUIRE(call.modelState != nullptr);
		CHECK(call.modelState->modelName() == c.model);
		// the bone from the files: the model's hierarchy posed with the attack animation at FrameForPristineBonePositions (clamped to the last frame)
		SharedWorld &s = shared();
		ArchiveW3DFileSource source(*s.mount->fs);
		WW3DAssetManager assets(source);
		WW3DDrawAssets drawAssets(assets);
		std::string err;
		const RenderObjPrototype *proto = assets.Create_Render_Obj(c.model, &err);
		REQUIRE_MESSAGE(proto, err);
		REQUIRE(proto->Tree != nullptr);
		W3DAnimationLookup lookup;
		lookup.names = { c.animation };
		std::string resolved;
		const HAnimClass *anim = drawAssets.animation(lookup, &resolved, &err);
		REQUIRE_MESSAGE(anim != nullptr, err);
		const int frame = std::min(c.pristineFrame, anim->Get_Num_Frames() - 1);
		const int boneIndex = proto->Tree->Get_Bone_Index("PROJECTILE");
		REQUIRE(boneIndex > 0);
		const Mat bone = fileBone(*proto->Tree, anim, (float)frame, boneIndex);
		std::printf("  info: %s fired with [%s]: state %s, %s frame %d, bone PROJECTILE at (%.3f, %.3f, %.3f), provider (%.3f, %.3f, %.3f)\n", c.launcher, call.flags.c_str(),
			call.state->stateName.c_str(), resolved.c_str(), frame, bone.m[0][3], bone.m[1][3], bone.m[2][3], call.bone[3], call.bone[7], call.bone[11]);
		CHECK(std::fabs(call.bone[3] - bone.m[0][3]) < 1e-3);
		CHECK(std::fabs(call.bone[7] - bone.m[1][3]) < 1e-3);
		CHECK(std::fabs(call.bone[11] - bone.m[2][3]) < 1e-3);
		// the stone's start = T(launcher) * bone (RW 0x70BCE7)
		const Stone &st = r.stones.front();
		const float *t = call.basis;
		const double bx = call.bone[3], by = call.bone[7], bz = call.bone[11];
		CHECK(std::fabs(st.start.x - (t[0] * bx + t[1] * by + t[2] * bz + call.pos.x)) < 1e-3);
		CHECK(std::fabs(st.start.y - (t[3] * bx + t[4] * by + t[5] * bz + call.pos.y)) < 1e-3);
		CHECK(std::fabs(st.start.z - (t[6] * bx + t[7] * by + t[8] * bz + call.pos.z)) < 1e-3);
		// the throw releases the stone high above the machine; the idle pose's bone (where the stone started before PROJ-2) is near the ground
		CHECK(st.start.z - call.pos.z > 40.0f);
		DrawableLaunchBones bones(drawAssets, {});
		float idle[12];
		ModelConditionFlags none;
		REQUIRE(bones.getProjectileLaunchOffset(*shared().world->things().findTemplate(c.launcher), none, 1.0f, 0.0f, 0, 0, idle));
		std::printf("  info: %s idle pose bone (%.3f, %.3f, %.3f)\n", c.launcher, idle[3], idle[7], idle[11]);
		CHECK(idle[11] < 15.0f);
	}
}

TEST_CASE("proj2 retail: the stone's arc - control points of RW 0x85E658 from the INI heights and indents, the apex, the speed and the segment count")
{
	if (!hudtest::haveWorld("proj2 retail arc"))
	{
		return;
	}
	for (size_t i = 0; i < sizeof(kCases) / sizeof(kCases[0]); ++i)
	{
		const Case &c = kCases[i];
		INFO(std::string(c.launcher));
		const SiegeRun &r = cachedRun(i);
		REQUIRE(!r.stones.empty());
		const Stone &st = r.stones.front();
		CHECK(st.templateName == c.projectile);
		REQUIRE(st.data != nullptr);
		// the retail data: the parsers of the module data (percent / 100) and the weapon (velocity per second / 5 frames)
		CHECK(st.data->m_firstHeight == c.firstHeight);
		CHECK(st.data->m_secondHeight == c.secondHeight);
		CHECK(st.data->m_firstPercentIndent == doctest::Approx(c.firstPct));
		CHECK(st.data->m_secondPercentIndent == doctest::Approx(c.secondPct));
		CHECK_FALSE(st.data->m_ignoreTerrainHeight);
		CHECK(st.data->m_curveFlattenMinDist == 0.0f);
		CHECK(st.speed == doctest::Approx(c.weaponSpeedPerSecond / 5.0f));
		// the curve evaluated independently; the path points are on it (the forward differenced points of RW 0x9612A3: float32, within a small tolerance)
		const Curve curve = curveOf(st);
		REQUIRE((int)st.path.size() == st.segments);
		double apex = -1e9, pathApex = -1e9, length = 0.0;
		double prev[3];
		curve.at(0.0, prev);
		for (int k = 1; k <= 2000; ++k)
		{
			double p[3];
			curve.at(k / 2000.0, p);
			apex = std::max(apex, p[2]);
			length += std::sqrt((p[0] - prev[0]) * (p[0] - prev[0]) + (p[1] - prev[1]) * (p[1] - prev[1]) + (p[2] - prev[2]) * (p[2] - prev[2]));
			std::memcpy(prev, p, sizeof(prev));
		}
		for (int k = 0; k < st.segments; ++k)
		{
			// getSegmentPoints(n): point k at t = k / (n - 1)
			double p[3];
			curve.at((double)k / (double)(st.segments - 1), p);
			INFO("point " << k);
			CHECK(std::fabs(st.path[(size_t)k].x - p[0]) < 0.05);
			CHECK(std::fabs(st.path[(size_t)k].y - p[1]) < 0.05);
			CHECK(std::fabs(st.path[(size_t)k].z - p[2]) < 0.05);
			pathApex = std::max(pathApex, (double)st.path[(size_t)k].z);
		}
		// the segment count: ceil(approximate length / speed) (RW 0x9D6A35's length is an approximation: allow one segment)
		const int want = (int)std::ceil(length / st.speed);
		CHECK(std::abs(st.segments - want) <= 1);
		std::printf("  info: %s: start z %.2f, end z %.2f, control z %.2f / %.2f, apex %.2f (%.2f above the launch) over %.1f, path apex %.2f, %d segments at %.2f per frame\n", c.launcher,
			st.start.z, st.end.z, curve.cp[1][2], curve.cp[2][2], apex, apex - st.start.z, std::sqrt((st.end.x - st.start.x) * (st.end.x - st.start.x) + (st.end.y - st.start.y) * (st.end.y - st.start.y)),
			pathApex, st.segments, st.speed);
		CHECK(pathApex > apex - 3.0);
		CHECK(pathApex <= apex + 0.05);
		CHECK(apex - st.start.z > 30.0); // a real arc above the release point
	}
}

TEST_CASE("proj2 retail: the timing - the arm stays in its attack state through the shot, the stone takes two path points in the launch frame (RW 0x85EF34), the WeaponStatusHelper sets FIRING after the shot")
{
	if (!hudtest::haveWorld("proj2 retail timing"))
	{
		return;
	}
	for (size_t i = 0; i < sizeof(kCases) / sizeof(kCases[0]); ++i)
	{
		const Case &c = kCases[i];
		INFO(std::string(c.launcher));
		const SiegeRun &r = cachedRun(i);
		REQUIRE(!r.stones.empty());
		const Stone &st = r.stones.front();
		REQUIRE(!st.frames.empty());
		const Frame &first = st.frames.front();
		CHECK(first.frame == st.launchFrame);
		CHECK(first.step == 2u);
		CHECK(first.pos.x == st.path[1].x);
		CHECK(first.pos.z == st.path[1].z);
		CHECK(first.recorded.x == st.path[0].x);
		CHECK(first.recorded.z == st.path[0].z);
		// every later frame: one path point more, recorded = the point before
		for (size_t k = 1; k < st.frames.size(); ++k)
		{
			const Frame &fr = st.frames[k];
			INFO("frame " << fr.frame);
			CHECK(fr.step == k + 2);
			REQUIRE(fr.step - 1 < st.path.size());
			CHECK(fr.pos.x == st.path[fr.step - 1].x);
			CHECK(fr.pos.z == st.path[fr.step - 1].z);
			CHECK(fr.recorded.x == st.path[fr.step - 2].x);
		}
		// gone segments - 1 frames after the launch frame (the update after the last point detonates)
		CHECK(st.gone == (int)st.launchFrame + st.segments - 1);
		// the launcher's flags at the end of the launch frame: FIRING_A (the helper saw the shot of this frame, RW 0x68E197 lastFireFrame == now)
		const size_t launchIdx = (size_t)(std::find(r.loopFrames.begin(), r.loopFrames.end(), st.launchFrame) - r.loopFrames.begin());
		REQUIRE(launchIdx < r.launcherFlags.size());
		CHECK(r.launcherFlags[launchIdx].find("FIRING_A ") != std::string::npos);
		// the attack animation state is never left between the wind-up and the end of the shot's FiringDuration / follow-through
		size_t firstAttack = r.launcherAnimState.size();
		for (size_t k = 0; k < r.launcherAnimState.size(); ++k)
		{
			if (r.launcherAnimState[k] == c.attackState)
			{
				firstAttack = std::min(firstAttack, k);
			}
		}
		REQUIRE(firstAttack <= launchIdx);
		for (size_t k = firstAttack; k <= launchIdx + 3 && k < r.launcherAnimState.size(); ++k)
		{
			INFO("loop frame " << k << " flags " << r.launcherFlags[k]);
			CHECK(r.launcherAnimState[k] == c.attackState);
		}
	}
}

TEST_CASE("proj2 retail: the drawn stone follows the arc - retail's Catmull-Rom through the path points with the update's look-ahead point (RW 0x85F6E7, 0x6765B9), close to the Bezier curve")
{
	if (!hudtest::haveWorld("proj2 retail drawn"))
	{
		return;
	}
	for (size_t i = 0; i < sizeof(kCases) / sizeof(kCases[0]); ++i)
	{
		const Case &c = kCases[i];
		INFO(std::string(c.launcher));
		const SiegeRun &r = cachedRun(i);
		REQUIRE(!r.stones.empty());
		const Stone &st = r.stones.front();
		const Curve curve = curveOf(st);
		std::vector<std::array<double, 3>> dense;
		for (int k = 0; k <= 4000; ++k)
		{
			double p[3];
			curve.at(k / 4000.0, p);
			dense.push_back({ p[0], p[1], p[2] });
		}
		double pathApex = -1e9, drawnApex = -1e9, worst = 0.0;
		for (const Coord3D &p : st.path)
		{
			pathApex = std::max(pathApex, (double)p.z);
		}
		int samples = 0;
		for (const Frame &fr : st.frames)
		{
			INFO("frame " << fr.frame << " step " << fr.step);
			// the snapshot's P3 is the update's look-ahead point: the next path point (the point itself at the end)
			CHECK(fr.rec.hasNext);
			const Coord3D &want = fr.step < st.path.size() ? st.path[fr.step] : st.path.back();
			CHECK(fr.rec.nextPos.x == want.x);
			CHECK(fr.rec.nextPos.z == want.z);
			REQUIRE(fr.drawn.size() == sizeof(kAlphas) / sizeof(kAlphas[0]));
			for (size_t k = 0; k < fr.drawn.size(); ++k)
			{
				const Coord3D &d = fr.drawn[k];
				INFO("alpha " << kAlphas[k]);
				CHECK(std::fabs(d.x - fr.expected[k].x) < 1e-4);
				CHECK(std::fabs(d.y - fr.expected[k].y) < 1e-4);
				CHECK(std::fabs(d.z - fr.expected[k].z) < 1e-4);
				double best = 1e18;
				for (const std::array<double, 3> &q : dense)
				{
					const double dx = d.x - q[0], dy = d.y - q[1], dz = d.z - q[2];
					best = std::min(best, dx * dx + dy * dy + dz * dz);
				}
				worst = std::max(worst, std::sqrt(best));
				drawnApex = std::max(drawnApex, (double)d.z);
				++samples;
			}
		}
		std::printf("  info: %s: %d drawn samples, the farthest %.3f from the Bezier curve; drawn apex %.2f, path apex %.2f\n", c.launcher, samples, worst, drawnApex, pathApex);
		CHECK(samples >= 6 * (st.segments - 2));
		CHECK(worst < 1.5); // the Catmull-Rom spline through the curve's points stays on the curve
		CHECK(drawnApex >= pathApex - 1e-3);
		CHECK(drawnApex <= pathApex + 2.0);
	}
}

TEST_CASE("proj2 retail: the stone's drawable fades in - hidden for InvisibleFrames (100 ms: 3 client frames), then faded in over FadeInTime (RW 0x85EE00 -> 0x67309D, 0x675AB7)")
{
	if (!hudtest::haveWorld("proj2 retail fade"))
	{
		return;
	}
	for (size_t i = 0; i < sizeof(kCases) / sizeof(kCases[0]); ++i)
	{
		const Case &c = kCases[i];
		INFO(std::string(c.launcher));
		const SiegeRun &r = cachedRun(i);
		REQUIRE(!r.stones.empty());
		const Stone &st = r.stones.front();
		REQUIRE(st.data != nullptr);
		CHECK(st.data->m_invisibleFrames == 100u);
		CHECK(st.data->m_fadeInTime == 100u);
		REQUIRE(st.frames.size() >= 2);
		const std::vector<float> &first = st.frames[0].opacity;
		REQUIRE(first.size() == 6u);
		std::printf("  info: %s: the stone's opacity in the client frames of the launch frame: %.3f %.3f %.3f %.3f %.3f %.3f, then %.3f\n", c.launcher, first[0], first[1], first[2],
			first[3], first[4], first[5], st.frames[1].opacity[0]);
		// the start (hidden, mode 5), two more hidden frames, the end of the invisible frames (fade in from 0), 1/3, 2/3, then opaque
		CHECK(first[0] == 0.0f);
		CHECK(first[1] == 0.0f);
		CHECK(first[2] == 0.0f);
		CHECK(first[3] == 0.0f);
		CHECK(first[4] == doctest::Approx(1.0 / 3.0).epsilon(1e-4));
		CHECK(first[5] == doctest::Approx(2.0 / 3.0).epsilon(1e-4));
		for (size_t k = 1; k < st.frames.size(); ++k)
		{
			for (float o : st.frames[k].opacity)
			{
				CHECK(o == 1.0f);
			}
		}
	}
}

TEST_CASE("proj2 retail: the same siege twice gives the same state hash in every frame")
{
	if (!hudtest::haveWorld("proj2 retail hash"))
	{
		return;
	}
	const SiegeRun &a = cachedRun(0);
	const Case &c = kCases[0];
	const SiegeRun b = runSiege(c.faction, c.launcher, c.enemyFaction, c.target, c.dist, 60);
	REQUIRE(a.hashes.size() == b.hashes.size());
	for (size_t k = 0; k < a.hashes.size(); ++k)
	{
		INFO("frame " << k);
		CHECK(a.hashes[k] == b.hashes[k]);
	}
}

TEST_CASE("proj2 stops: S-1000 is in GameLogic::report().stops exactly once and names the siege launch")
{
	if (!hudtest::haveWorld("proj2 stops"))
	{
		return;
	}
	SiegeArena a(shared(), "FactionMen", "FactionMordor");
	const GameLogic::Report report = a.logic.report();
	int matches = 0;
	for (const std::string &line : report.stops)
	{
		if (line.rfind("[S-1000] ", 0) == 0)
		{
			++matches;
			CHECK(line.find("siege launch") != std::string::npos);
			CHECK(line.find("0x68E197") != std::string::npos);
		}
	}
	CHECK(matches == 1);
}

TEST_CASE("proj2 retail: every faction's siege thrower fires with its FIRING conditions set - the launch pose is the attack state's, the stone arcs above its release point")
{
	if (!hudtest::haveWorld("proj2 retail siege sweep"))
	{
		return;
	}
	struct Sweep
	{
		const char *faction, *launcher, *enemyFaction, *target;
		float dist;
	};
	const Sweep sweep[] = {
		{ "FactionDwarves", "DwarvenCatapult", "FactionMordor", "MordorBarracks", 420.0f },
		{ "FactionIsengard", "IsengardBallista", "FactionMen", "GondorBarracks", 400.0f },
		{ "FactionAngmar", "AngmarTrollSling", "FactionMen", "GondorBarracks", 420.0f }, // throws a KindOf ROCK prop (RockBigTroll) with its own BezierProjectileBehavior
	};
	for (const Sweep &c : sweep)
	{
		INFO(std::string(c.launcher));
		const SiegeRun r = runSiege(c.faction, c.launcher, c.enemyFaction, c.target, c.dist, 80);
		REQUIRE_MESSAGE(!r.calls.empty(), (std::string(c.launcher) + " never launched"));
		REQUIRE(!r.stones.empty());
		const LaunchRecorder::Call &call = r.calls.front();
		const Stone &st = r.stones.front();
		double pathApex = -1e9;
		for (const Coord3D &p : st.path)
		{
			pathApex = std::max(pathApex, (double)p.z);
		}
		std::printf("  info: %s fired %s with [%s] in state %s: release %.1f above the launcher (bone %.2f %.2f %.2f), apex %.1f above the release, %d segments at %.2f per frame\n",
			c.launcher, st.templateName.c_str(), call.flags.c_str(), call.state ? call.state->stateName.c_str() : "-", st.start.z - call.pos.z, call.bone[3], call.bone[7],
			call.bone[11], pathApex - st.start.z, st.segments, st.speed);
		CHECK(call.flags.find("FIRING_") != std::string::npos);
		CHECK(call.flags.find("BETWEEN_FIRING_SHOTS_A") == std::string::npos);
		CHECK(call.found);
		CHECK(st.frames.front().step == 2u);
		CHECK(pathApex >= st.start.z);
	}
}

namespace
{
// a hand-made published frame: the launcher and the stone, the local player's shroud with the launcher's and the end's cells as asked
struct FadeProbe
{
	std::shared_ptr<LogicSnapshot> snap = std::make_shared<LogicSnapshot>();
	ObjectSnapshot rec;
	FadeProbe(ObjectID stone, ObjectID launcher, bool launcherSeen, bool endSeen)
	{
		snap->frame = 40;
		auto shroud = std::make_shared<ShroudView>();
		shroud->displayed = true;
		shroud->localPlayer = 0;
		shroud->countX = 4;
		shroud->countY = 1;
		shroud->cellSize = 100.0f;
		shroud->status = { (std::uint8_t)CELLSHROUD_CLEAR, (std::uint8_t)CELLSHROUD_CLEAR, (std::uint8_t)CELLSHROUD_CLEAR,
			(std::uint8_t)(endSeen ? CELLSHROUD_CLEAR : CELLSHROUD_FOGGED) };
		snap->shroud = shroud;
		ObjectSnapshot l;
		l.id = launcher;
		l.position = Coord3D{ 50.0f, 50.0f, 0.0f };
		l.objectShroud = launcherSeen ? (int)OBJECTSHROUD_CLEAR : (int)OBJECTSHROUD_FOGGED;
		rec.id = stone;
		rec.projectile = true;
		rec.projectileFireFrame = 40;
		rec.projectileLauncher = launcher;
		rec.projectileEnd = Coord3D{ 350.0f, 50.0f, 0.0f };
		rec.projectileSegments = 8;
		snap->objects = { l, rec };
		if (snap->objects[0].id > snap->objects[1].id)
		{
			std::swap(snap->objects[0], snap->objects[1]);
		}
	}
};
} // namespace

TEST_CASE("proj2 client: the fade's branches - launcher and end seen: hidden then faded in; only the launcher: fade out over the flight; only the end: fade in over it; neither: hidden")
{
	if (!hudtest::haveWorld("proj2 client fade"))
	{
		return;
	}
	struct Want
	{
		bool launcherSeen, endSeen;
		int mode;
		bool hidden;
		float opacity;
	};
	// RW 0x67309D (mode 5, hidden), RW 0x670A50 (mode 2, opaque, 6 * 8 = 48 client frames), RW 0x670AA2 (mode 1, transparent, 48), RW 0x6718FB(1)
	const Want wants[] = { { true, true, 5, true, 1.0f }, { true, false, 2, false, 1.0f }, { false, true, 1, false, 0.0f }, { false, false, 0, true, 1.0f } };
	for (const Want &w : wants)
	{
		INFO("launcher seen " << w.launcherSeen << " end seen " << w.endSeen);
		SiegeArena b(shared(), "FactionMen", "FactionMordor"); // a fresh drawable per branch
		Object *l2 = b.place("GondorTrebuchet", 0, 50.0f, 50.0f);
		Object *s2 = b.place("GondorTrebuchetRockProjectile", 0, 60.0f, 50.0f);
		Drawable *d = b.drawableOf(*s2);
		REQUIRE(d != nullptr);
		FadeProbe probe(s2->getID(), l2->getID(), w.launcherSeen, w.endSeen);
		d->startProjectileFade(probe.rec, *probe.snap);
		CHECK(d->fade().mode == w.mode);
		CHECK(d->fade().hidden == w.hidden);
		CHECK(d->fade().opacity == w.opacity);
		if (w.mode == 1 || w.mode == 2)
		{
			CHECK(d->fade().target == 48.0);
			for (int k = 0; k < 24; ++k)
			{
				d->updateFade(kClientFrameMs);
			}
			CHECK(d->drawOpacity() == doctest::Approx(0.5).epsilon(1e-4)); // half way through the flight
			for (int k = 0; k < 24; ++k)
			{
				d->updateFade(kClientFrameMs);
			}
			CHECK(d->fade().mode == 0);
			CHECK(d->drawOpacity() == (w.mode == 1 ? 1.0f : 0.0f));
		}
		if (w.mode == 5)
		{
			CHECK(d->fade().target == 3.0);
			CHECK(d->fade().fadeIn == 3.0);
		}
		// the same launch again changes nothing (the drawable saw it)
		const Drawable::Fade before = d->fade();
		d->startProjectileFade(probe.rec, *probe.snap);
		CHECK(d->fade().mode == before.mode);
		CHECK(d->fade().counter == before.counter);
	}
}

TEST_CASE("proj2 client: S-1001 is in DrawableManager::report().stops exactly once")
{
	if (!hudtest::haveWorld("proj2 client stops"))
	{
		return;
	}
	SiegeArena a(shared(), "FactionMen", "FactionMordor");
	int matches = 0;
	for (const std::string &line : a.drawables->report().stops)
	{
		if (line.rfind("[S-1001] ", 0) == 0)
		{
			++matches;
			CHECK(line.find("projectile fade") != std::string::npos);
		}
	}
	CHECK(matches == 1);
}

TEST_CASE("proj2 binary: the client runs 30 frames per 5 logic frames - RW 0x63CF0F .. 0x63CF27 writes 30 / 5 = 6 into engine +0x38 (the fog fade's scale, S-1001)")
{
	const retailtest::PeImage *pe = retailtest::PeImage::fromEnvironment();
	if (!pe)
	{
		retailtest::printSkip("proj2 binary facts (RW_GAME_DAT unset)");
		return;
	}
	// mov eax, [0xD9F60C]; cdq; idiv dword [0xD9F608]; ... cvtsi2ss; mov [edi + 0x38], eax
	std::vector<std::uint8_t> code;
	REQUIRE(pe->read(0x63CF0F, 0x19, &code));
	const std::uint8_t want[] = { 0xA1, 0x0C, 0xF6, 0xD9, 0x00, 0x99, 0xF7, 0x3D, 0x08, 0xF6, 0xD9, 0x00 };
	for (size_t i = 0; i < sizeof(want); ++i)
	{
		CHECK(code[i] == want[i]);
	}
	CHECK(pe->u32At(0xD9F60C) == 30u);
	CHECK(pe->u32At(0xD9F608) == 5u);
	CHECK(pe->u32At(0xD9F60C) / pe->u32At(0xD9F608) == 6u);
	CHECK(code[0x18] == 0x89); // mov [edi + 0x38], eax: 89 47 38 at RW 0x63CF27
}
