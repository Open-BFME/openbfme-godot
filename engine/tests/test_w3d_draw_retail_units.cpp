// OpenBFME retail tests: real units drawn through the draw module runtime (lane DRAW-1, checklist steps 13-17).
// Runs only when ROTWK_INSTALL and BFME2_INSTALL are set (prints SKIP otherwise); uses the shared retail mount.
//
// The expectations are derived by hand from the INI text of Object GondorFighter (data\INI\Object\GoodFaction\Units\Men\
// GondorFighter.INI, quoted in the comments) with the matching and stepping rules of W3DScriptedModelDraw.h, and from the W3D
// animation files for frame counts. Nothing is read back from the code under test. The BeginScript bodies are the retail text, run
// by the real drawable Lua state (W3DLuaDrawScriptHost).

#include "doctest.h"

#include "RetailTestMount.h"
#include "W3DDrawRetail.h"
#include "W3DDrawTestUtil.h"

#include "Common/AsciiString.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DLuaDrawScriptHost.h"
#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DScriptedModelDraw.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <deque>

using namespace drawtest;

namespace
{
class ScriptedRandom : public W3DDrawRandom
{
public:
	std::deque<int> values;
	std::vector<std::pair<int, int>> asked;
	int value(int lo, int hi) override
	{
		asked.emplace_back(lo, hi);
		if (values.empty())
		{
			throw std::logic_error("ScriptedRandom: unexpected value(" + std::to_string(lo) + ", " + std::to_string(hi) + ")");
		}
		const int v = values.front();
		values.pop_front();
		return v;
	}
	float real(float lo, float hi) override { return hi - lo <= 0.0f ? hi : (lo + hi) * 0.5f; }
};

bool equalsNoCase(const std::string &a, const std::string &b) { return AsciiStringUtil::compareNoCase(a, b) == 0; }

const RetailDrawModule *findModule(const char *object, const char *className)
{
	for (const RetailDrawModule &m : retailDrawScan().modules)
	{
		if (m.data && m.object == object && m.className == className)
		{
			return &m;
		}
	}
	return nullptr;
}

struct Soldier
{
	ArchiveW3DFileSource source;
	WW3DAssetManager assets;
	WW3DDrawAssets drawAssets;
	ScriptedRandom random;
	W3DLuaDrawScriptHost host; // the real drawable Lua state (lane LUA-1; replaced the test-side subset interpreter of stop S-091)
	std::unique_ptr<W3DHordeModelDraw> draw;
	const W3DHordeModelDrawModuleData *data = nullptr;

	explicit Soldier(retailtest::Mount &mount) : source(*mount.fs), assets(source), drawAssets(assets) {}

	// the first idle pick (the idle state is idle-like, so the LodOptions limit does not apply)
	void start(int firstPick)
	{
		const RetailDrawModule *m = findModule("GondorFighter", "W3DHordeModelDraw");
		REQUIRE(m != nullptr);
		data = dynamic_cast<const W3DHordeModelDrawModuleData *>(m->data.get());
		REQUIRE(data != nullptr);
		W3DScriptedModelDraw::Options o;
		o.buildBones = false;
		random.values = { firstPick };
		draw.reset(new W3DHordeModelDraw(*data, 2, drawAssets, random, &host, o)); // LOD HIGH: MaxRandomAnimations 4
	}

	int framesOf(const char *clip)
	{
		std::string err;
		const HAnimClass *a = assets.Get_HAnim(std::string("GUMAArms_SKL.") + clip, &err);
		REQUIRE_MESSAGE(a, err);
		return a->Get_Num_Frames();
	}
};
} // namespace

TEST_CASE("retail: Gondor soldier (W3DHordeModelDraw): the module data as the INI text says")
{
	retailtest::Mount *mount = retailtest::pureMount();
	if (!mount)
	{
		retailtest::printSkip("retail: Gondor soldier module data");
		return;
	}
	REQUIRE_MESSAGE(mount->fs, mount->error);
	const RetailDrawModule *m = findModule("GondorFighter", "W3DHordeModelDraw");
	REQUIRE(m != nullptr);
	const W3DHordeModelDrawModuleData *d = dynamic_cast<const W3DHordeModelDrawModuleData *>(m->data.get());
	REQUIRE(d != nullptr);
	// LodOptions use the #defines of GameData.ini: LOW 1 animation / 10 frame delta, MEDIUM 4 / 4, HIGH 4 / 4 and Yes for HIGH
	CHECK(d->m_lodOptions[0].maxRandomAnimations == 1);
	CHECK(d->m_lodOptions[0].maxAnimFrameDelta == doctest::Approx(10.0f));
	CHECK(!d->m_lodOptions[0].allowMultipleModels);
	CHECK(d->m_lodOptions[1].maxRandomAnimations == 4);
	CHECK(d->m_lodOptions[2].maxRandomAnimations == 4);
	CHECK(d->m_lodOptions[2].allowMultipleModels);
	// RandomStartFramePercent is never written in the retail INI: the constructor defaults (RW 0x478289) stand: 0, 50, 100
	CHECK(d->m_lodOptions[0].randomStartFramePercent == 0);
	CHECK(d->m_lodOptions[1].randomStartFramePercent == 50);
	CHECK(d->m_lodOptions[2].randomStartFramePercent == 100);
	CHECK(d->m_okToChangeModelColor);
	CHECK(d->m_staticModelLODMode);
	CHECK(d->m_wadingParticleSys == "WaterRipplesTrail");
	// three model states: the default and WEAPONSET_PLAYER_UPGRADE use GUMAArms_SKN, USER_4 the old armor GUNumnrean_SKN
	REQUIRE(d->m_conditionStates.size() == 3);
	CHECK(d->m_defaultState == 0);
	CHECK(d->m_conditionStates[0].modelName() == "GUMAArms_SKN");
	CHECK(d->m_conditionStates[0].skeleton == "gumaarms_skl");
	CHECK(d->m_conditionStates[1].conditions == flagsOf({ "WEAPONSET_PLAYER_UPGRADE" }));
	CHECK(d->m_conditionStates[2].conditions == flagsOf({ "USER_4" }));
	CHECK(d->m_conditionStates[2].modelName() == "GUNumnrean_SKN");
	// the IdleAnimationState is first, with the 9 idle animations in INI order (IDLB has priority 20, the rest 1)
	const AnimationStateInfo &idle = d->m_animationStates[0];
	CHECK(idle.kind == AnimationStateInfo::KIND_IDLE);
	CHECK(idle.stateName == "STATE_Idle");
	REQUIRE(idle.animations.size() == 9);
	CHECK(idle.animations[0].clipName() == "GUManMocap_IDLB");
	CHECK(idle.animations[0].priority == 20);
	CHECK(idle.animations[0].blendTime == doctest::Approx(15.0f));
	CHECK(idle.animations[0].mode == W3D_ANIM_MODE_ONCE);
	CHECK(idle.animations[0].speedFactorMin == doctest::Approx(0.9f));
	CHECK(idle.animations[0].speedFactorMax == doctest::Approx(1.1f));
	CHECK(idle.animations[3].clipName() == "GUManMocap_IDLJ");
	CHECK(idle.animations[3].priority == 1);
	CHECK(!idle.beginScript.empty());
}

TEST_CASE("retail: Gondor soldier: idle, moving, attacking, dying")
{
	retailtest::Mount *mount = retailtest::pureMount();
	if (!mount)
	{
		retailtest::printSkip("retail: Gondor soldier");
		return;
	}
	REQUIRE_MESSAGE(mount->fs, mount->error);
	Soldier s(*mount);

	// ---- idle: IdleAnimationState, weighted pick over the first 4 of 9 animations -------------------------------------
	// the idle state is idle-like: no row limit, all 9 entries: weights [20, 1 x 8], total 28, draw value(0, 27);
	// 22 - 20 = 2, 2 - 1 = 1, 1 - 1 = 0, 0 - 1 < 0: entry 3 = IDLJ
	s.start(22);
	W3DScriptedModelDraw &d = *s.draw;
	REQUIRE(s.random.asked.size() == 1);
	CHECK(s.random.asked[0] == std::make_pair(0, 27));
	CHECK(d.errors().empty());
	for (const std::string &e : d.errors())
	{
		INFO(e);
	}
	W3DDrawFrame f = d.frame();
	CHECK(f.modelName == "GUMAArms_SKN");
	REQUIRE(f.model != nullptr);
	CHECK(equalsNoCase(f.model->HierarchyName, "GUMAArms_SKL"));
	CHECK(f.animationStateName == "STATE_Idle");
	REQUIRE(f.trackCount == 1);
	CHECK(equalsNoCase(f.tracks[0].clipName, "GUMAArms_SKL.GUManMocap_IDLJ"));
	CHECK(f.tracks[0].mode == W3D_ANIM_MODE_ONCE);
	CHECK(f.tracks[0].frame == doctest::Approx(0.0f));
	CHECK(s.host.scriptsRun() == 1); // the idle state's BeginScript ran once; previous state "" is not STATE_Selected: nothing requested
	CHECK(s.host.failures().empty());

	// ---- moving: the line "AnimationState = MOVING" (RUNB / RUNC, LOOP, RANDOMSTART) -----------------------------------
	// {MOVING}: "MOVING" is (1,0); every other MOVING state has extra bits ("MOVING ATTACKING" is (1,1)): the plain one wins
	const int runFrames = s.framesOf("GUManMocap_RUNC");
	// pick: weights [1, 1], value(0, 1): 1 lands in entry 1 (RUNC); RANDOMSTART: value(0, frames - 1)
	s.random.values = { 1, 5 };
	d.setModelConditionFlags(flagsOf({ "MOVING" }));
	REQUIRE(s.random.asked.size() == 3);
	CHECK(s.random.asked[1] == std::make_pair(0, 1));
	CHECK(s.random.asked[2] == std::make_pair(0, runFrames - 1));
	f = d.frame();
	CHECK(f.animationStateName == "");
	REQUIRE(f.trackCount == 2); // the idle animation keeps playing as track 0 while the run blends in
	CHECK(equalsNoCase(f.tracks[1].clipName, "GUMAArms_SKL.GUManMocap_RUNC"));
	CHECK(f.tracks[1].mode == W3D_ANIM_MODE_LOOP);
	CHECK(f.tracks[1].frame == doctest::Approx(5.0f));
	CHECK(f.blending);
	// the default blend time 5: 100 ms = 3 frames of the incoming track, countdown 5 - 3 = 2: weight 0.6
	d.advance(100.0);
	f = d.frame();
	CHECK(f.tracks[1].frame == doctest::Approx(8.0f).epsilon(0.001));
	CHECK(f.blendPercentage == doctest::Approx(0.6f).epsilon(0.001));
	// another 100 ms ends the blend: the run is track 0, 11 frames in (5 + 3 + 3)
	d.advance(100.0);
	f = d.frame();
	CHECK(f.trackCount == 1);
	CHECK(f.tracks[0].frame == doctest::Approx(11.0f).epsilon(0.001));
	CHECK(f.tracks[0].completed == false);

	// {MOVING, ATTACKING}: "MOVING ATTACKING" is (2,0) and wins: RUNA, LOOP, RANDOMSTART, one animation (no pick draw)
	const int runaFrames = s.framesOf("GUManMocap_RUNA");
	s.random.values = { 3 };
	d.setModelConditionFlags(flagsOf({ "MOVING", "ATTACKING" }));
	CHECK(s.random.asked.back() == std::make_pair(0, runaFrames - 1));
	f = d.frame();
	REQUIRE(f.trackCount == 2);
	CHECK(equalsNoCase(f.tracks[1].clipName, "GUMAArms_SKL.GUManMocap_RUNA"));
	CHECK(f.tracks[1].frame == doctest::Approx(3.0f));

	// ---- attacking: "AnimationState = FIRING_OR_PREATTACK_A" (ATKA, ONCE, UseWeaponTiming) ------------------------------
	const int atkFrames = s.framesOf("GUManMocap_ATKA");
	d.setModelConditionFlags(flagsOf({ "FIRING_OR_PREATTACK_A" }));
	f = d.frame();
	REQUIRE(f.trackCount >= 2);
	const W3DDrawTrack &incoming = f.tracks[f.trackCount - 1];
	CHECK(equalsNoCase(incoming.clipName, "GUMAArms_SKL.GUManMocap_ATKA"));
	CHECK(incoming.mode == W3D_ANIM_MODE_ONCE);
	CHECK(incoming.frame == doctest::Approx(0.0f));
	bool weaponTimingStop = false;
	for (const W3DStopHit &h : d.stops())
	{
		weaponTimingStop = weaponTimingStop || (h.Id == "S-095" && h.Message.find("UseWeaponTiming") != std::string::npos);
	}
	CHECK(weaponTimingStop); // UseWeaponTiming is parsed and reported, not acted on
	// run the clip to its end: a ONCE animation of a state with conditions and no RESTART flag holds its last frame
	for (int i = 0; i < 80; ++i)
	{
		d.advance(100.0);
	}
	f = d.frame();
	REQUIRE(f.trackCount == 1);
	CHECK(f.tracks[0].frame == doctest::Approx((float)(atkFrames - 1)));
	CHECK(equalsNoCase(f.tracks[0].clipName, "GUMAArms_SKL.GUManMocap_ATKA"));

	// ---- dying: "AnimationState = DYING" has DIEB, DIEC, DIED, DIEE (priority 1 each, ONCE) -----------------------------
	// {DYING}: (1,0) wins over "DYING SPLATTED" (1,1) etc.; weights [1,1,1,1], value(0, 3): 2 lands in entry 2 (DIED)
	s.random.values = { 2 };
	d.setModelConditionFlags(flagsOf({ "DYING" }));
	CHECK(s.random.asked.back() == std::make_pair(0, 3));
	f = d.frame();
	REQUIRE(f.trackCount == 2);
	CHECK(equalsNoCase(f.tracks[1].clipName, "GUMAArms_SKL.GUManMocap_DIED"));
	CHECK(f.tracks[1].mode == W3D_ANIM_MODE_ONCE);
	// the death plays to its last frame and stays there
	const int diedFrames = s.framesOf("GUManMocap_DIED");
	for (int i = 0; i < 200; ++i)
	{
		d.advance(100.0);
	}
	f = d.frame();
	REQUIRE(f.trackCount == 1);
	CHECK(f.tracks[0].frame == doctest::Approx((float)(diedFrames - 1)));
	CHECK(d.errors().empty());
	for (const std::string &e : d.errors())
	{
		INFO(e);
	}
	CHECK(s.host.failures().empty());

	// {DYING, BURNINGDEATH}: "DYING BURNINGDEATH" (2,0) with AnimationBlendTime 10 on its four animations
	s.random.values = { 0 };
	d.setModelConditionFlags(flagsOf({ "DYING", "BURNINGDEATH" }));
	f = d.frame();
	REQUIRE(f.trackCount == 2);
	CHECK(equalsNoCase(f.tracks[1].clipName, "GUMAArms_SKL.GUManMocap_DIEB"));
	d.advance(100.0); // 3 frames of the incoming: countdown 10 - 3 = 7: weight 0.3
	CHECK(d.frame().blendPercentage == doctest::Approx(0.3f).epsilon(0.001));
}

TEST_CASE("retail: Gondor soldier: the first state in INI order whose flags are all set wins (DYING is before the MOVING states)")
{
	retailtest::Mount *mount = retailtest::pureMount();
	if (!mount)
	{
		retailtest::printSkip("retail: Gondor soldier precedence");
		return;
	}
	REQUIRE_MESSAGE(mount->fs, mount->error);
	Soldier s(*mount);
	s.start(0);
	W3DScriptedModelDraw &d = *s.draw;
	// INI order (GondorFighter.INI): ... DYING BURNINGDEATH, DYING SPLATTED, DYING AFLAME, DYING, BURNINGDEATH, ... MOVING
	// FIRING_OR_PREATTACK_A, ..., MOVING ATTACKING, ..., MOVING. {DYING, MOVING, ATTACKING} holds all flags of "DYING" and of
	// "MOVING ATTACKING" and "MOVING"; "DYING" is defined first (RW 0x4B4443: first subset match, not the best match).
	// DYING has 4 animations (DIEB, DIEC, DIED, DIEE, priority 1): weights [1,1,1,1], value(0, 3): 1 lands in entry 1 (DIEC).
	s.random.values = { 1 };
	d.setModelConditionFlags(flagsOf({ "DYING", "MOVING", "ATTACKING" }));
	CHECK(s.random.asked.back() == std::make_pair(0, 3));
	const W3DDrawFrame f = d.frame();
	REQUIRE(f.trackCount == 2);
	CHECK(equalsNoCase(f.tracks[1].clipName, "GUMAArms_SKL.GUManMocap_DIEC"));
	CHECK(d.currentAnimationState()->conditions == flagsOf({ "DYING" }));
	CHECK(d.errors().empty());
}

TEST_CASE("retail: Gondor soldier: the model state changes the model only when the model differs; selected plays a transition first")
{
	retailtest::Mount *mount = retailtest::pureMount();
	if (!mount)
	{
		retailtest::printSkip("retail: Gondor soldier states");
		return;
	}
	REQUIRE_MESSAGE(mount->fs, mount->error);
	Soldier s(*mount);
	s.start(0);
	W3DScriptedModelDraw &d = *s.draw;
	CHECK(d.renderObjectsCreated() == 1);

	// WEAPONSET_PLAYER_UPGRADE has its own model state but the same model: no new render object. The animation state of that
	// flag ("AnimationState = WEAPONSET_PLAYER_UPGRADE", StateName STATE_Idle, RESTART_ANIM_WHEN_COMPLETE) takes over.
	s.random.values = { 0, 0 };
	d.setModelConditionFlags(flagsOf({ "WEAPONSET_PLAYER_UPGRADE" }));
	CHECK(d.renderObjectsCreated() == 1);
	CHECK(d.currentModelState() == &s.data->m_conditionStates[1]);
	CHECK(d.currentAnimationState()->stateName == "STATE_Idle");
	CHECK(d.currentAnimationState()->conditions == flagsOf({ "WEAPONSET_PLAYER_UPGRADE" }));
	// USER_4 is the old armor: another model
	d.setModelConditionFlags(flagsOf({ "USER_4" }));
	CHECK(d.renderObjectsCreated() == 2);
	CHECK(d.frame().modelName == "GUNumnrean_SKN");
	REQUIRE(d.frame().model != nullptr);
	CHECK(d.errors().empty());
	for (const std::string &e : d.errors())
	{
		INFO(e);
	}
	d.setModelConditionFlags(ModelConditionFlags());
	CHECK(d.renderObjectsCreated() == 3);
	CHECK(d.frame().modelName == "GUMAArms_SKN");
}

TEST_CASE("retail: Gondor soldier: SELECTED from idle plays TRANS_IdleToSelected first (the script of the state), then the state")
{
	retailtest::Mount *mount = retailtest::pureMount();
	if (!mount)
	{
		retailtest::printSkip("retail: Gondor soldier selected");
		return;
	}
	REQUIRE_MESSAGE(mount->fs, mount->error);
	Soldier s(*mount);
	s.start(0); // IDLB
	W3DScriptedModelDraw &d = *s.draw;
	REQUIRE(d.currentAnimationState()->stateName == "STATE_Idle");
	// the second BeginScript of the SELECTED block replaces the first: only "if Prev == "STATE_Idle" then
	// CurDrawableSetTransitionAnimState("TRANS_IdleToSelected") end" is left
	const AnimationStateInfo *selected = s.data->findStateByName("STATE_Selected");
	REQUIRE(selected != nullptr);
	CHECK(selected->beginScriptLines.size() == 2);
	CHECK(selected->beginScript.find("return") == std::string::npos);

	d.setModelConditionFlags(flagsOf({ "SELECTED" }));
	CHECK(d.currentAnimationState()->stateName == "TRANS_IdleToSelected");
	CHECK(d.pendingStatePending());
	// ATNA, ONCE, AnimationSpeedFactorRange 0.9 1.0: the incoming track
	W3DDrawFrame f = d.frame();
	REQUIRE(f.trackCount == 2);
	CHECK(equalsNoCase(f.tracks[1].clipName, "GUMAArms_SKL.GUManMocap_ATNA"));
	CHECK(f.tracks[1].mode == W3D_ANIM_MODE_ONCE);
	// play it out: the transition completes, SELECTED is entered with the previous state TRANS_IdleToSelected (so the script asks
	// for nothing) and its ATNB loop starts
	bool reached = false;
	for (int i = 0; i < 200 && !reached; ++i)
	{
		d.advance(100.0);
		reached = d.currentAnimationState()->stateName == "STATE_Selected";
	}
	REQUIRE(reached);
	CHECK(!d.pendingStatePending());
	f = d.frame();
	CHECK(equalsNoCase(f.tracks[f.trackCount - 1].clipName, "GUMAArms_SKL.GUManMocap_ATNB"));
	CHECK(f.tracks[f.trackCount - 1].mode == W3D_ANIM_MODE_LOOP);
	CHECK(d.errors().empty());
	for (const std::string &e : d.errors())
	{
		INFO(e);
	}
	CHECK(s.host.failures().empty());
	const std::vector<std::string> &log = d.log();
	// enter idle, enter selected, its script, the transition; then (after the transition) enter selected again and its script
	REQUIRE(log.size() >= 6);
	CHECK(log[0] == "enter STATE_Idle");
	CHECK(log[1] == "script STATE_Idle");
	CHECK(log[2] == "enter STATE_Selected");
	CHECK(log[3] == "script STATE_Selected");
	CHECK(log[4] == "transition TRANS_IdleToSelected");
}

namespace
{
int pivotIndexByName(const HTreeClass &tree, const std::string &name)
{
	for (int i = 0; i < tree.Num_Pivots(); ++i)
	{
		if (equalsNoCase(tree.Get_Pivot(i).Name, name))
		{
			return i;
		}
	}
	return -1;
}

// how many of NAME01, NAME02, ... exist as pivots, counting from 01 until the first gap (the ZH barrel loop)
int numberedRun(const HTreeClass &tree, const std::string &base)
{
	int n = 0;
	for (int i = 1; i <= 99; ++i)
	{
		char suffix[8];
		std::snprintf(suffix, sizeof(suffix), "%02d", i);
		if (pivotIndexByName(tree, base + suffix) < 0)
		{
			break;
		}
		++n;
	}
	return n;
}
} // namespace

TEST_CASE("retail: weapon launch bones, public bones and turret bones of an archer and a ship")
{
	retailtest::Mount *mount = retailtest::pureMount();
	if (!mount)
	{
		retailtest::printSkip("retail: bones");
		return;
	}
	REQUIRE_MESSAGE(mount->fs, mount->error);
	ArchiveW3DFileSource source(*mount->fs);
	WW3DAssetManager assets(source);

	// GondorArcher: DefaultModelConditionState has Model GUArcher_SKN and "WeaponLaunchBone = PRIMARY ARROW" (and TERTIARY ARROW)
	{
		const RetailDrawModule *m = findModule("GondorArcher", "W3DHordeModelDraw");
		REQUIRE(m != nullptr);
		const ModelConditionInfo &def = m->data->m_conditionStates[0];
		CHECK(def.modelName() == "GUArcher_SKN");
		REQUIRE(def.weaponLaunchBone.size() == 3);
		CHECK(def.weaponLaunchBone[0] == "arrow");
		CHECK(def.weaponLaunchBone[1].empty());
		CHECK(def.weaponLaunchBone[2] == "arrow");
		CHECK(def.publicBones == std::vector<std::string>{ "arrow" });
		std::string err;
		const RenderObjPrototype *proto = assets.Create_Render_Obj(def.modelName(), &err);
		REQUIRE_MESSAGE(proto, err);
		REQUIRE(proto->Tree != nullptr);
		W3DModelBones bones;
		bones.build(def, proto, m->data->m_extraPublicBones, {}, 1.0f, nullptr, 0.0f);
		CHECK(bones.valid);
		// the expected barrels: NAME01.. while they exist; with none, the unadorned ARROW bone makes one barrel
		const int run = numberedRun(*proto->Tree, "ARROW");
		const int plain = pivotIndexByName(*proto->Tree, "ARROW") > 0 ? 1 : 0;
		const size_t expected = (size_t)(run > 0 ? run : plain);
		std::printf("GUArcher: ARROW01.. run %d, plain ARROW %d, missing bones %zu\n", run, plain, bones.missingBones.size());
		CHECK(bones.barrels[0].size() == expected);
		CHECK(bones.barrels[2].size() == expected);
		CHECK(bones.barrels[1].empty());
		for (size_t i = 0; i < bones.barrels[0].size(); ++i)
		{
			if (run > 0)
			{
				char suffix[8];
				std::snprintf(suffix, sizeof(suffix), "%02d", (int)i + 1);
				CHECK(bones.barrels[0][i].fxBone == 0);
				const int idx = pivotIndexByName(*proto->Tree, std::string("ARROW") + suffix);
				// the launch bone's pristine transform is that pivot's bind pose transform
				const W3DPristineBone *pb = bones.findPristineBone(std::string("arrow") + suffix);
				REQUIRE(pb != nullptr);
				CHECK(pb->boneIndex == idx);
			}
		}
		CHECK(bones.missingBones.empty());
	}

	// EvilShoreBombardShip (ChildObject, its first Draw): Model AUBomShip_SKN, "Turret = B_CATAPULT", "ExtraPublicBone = ARROW", WeaponLaunchBone PROJECTILEROCK
	{
		const RetailDrawModule *m = findModule("EvilShoreBombardShip", "W3DScriptedModelDraw");
		REQUIRE(m != nullptr);
		const ModelConditionInfo &def = m->data->m_conditionStates[0];
		REQUIRE(def.turrets.size() == 1);
		CHECK(def.turrets[0].angleBone == "b_catapult");
		CHECK(def.turrets[0].artAngle == doctest::Approx(1.5707963f));
		CHECK(m->data->m_extraPublicBones == std::vector<std::string>{ "ARROW" });
		std::string err;
		const RenderObjPrototype *proto = assets.Create_Render_Obj(def.modelName(), &err);
		REQUIRE_MESSAGE(proto, err);
		REQUIRE(proto->Tree != nullptr);
		W3DModelBones bones;
		bones.build(def, proto, m->data->m_extraPublicBones, {}, 1.0f, nullptr, 0.0f);
		REQUIRE(bones.turrets.size() == 1);
		const int turretPivot = pivotIndexByName(*proto->Tree, "B_CATAPULT");
		std::printf("AUBomShip: B_CATAPULT pivot %d, missing %zu\n", turretPivot, bones.missingBones.size());
		CHECK(bones.turrets[0].angleBone == (turretPivot > 0 ? turretPivot : 0));
		CHECK(bones.turrets[0].pitchBone == 0); // no TurretPitch
		const int arrow = pivotIndexByName(*proto->Tree, "ARROW");
		CHECK((bones.findPristineBone("arrow") != nullptr) == (arrow > 0 || numberedRun(*proto->Tree, "ARROW") > 0));
		// the model has neither a PROJECTILEROCK bone (the launch bone the INI names) nor an ARROW bone (ExtraPublicBone): retail's
		// ZH ancestor DEBUG_CRASHes on these in debug builds and carries on; here they are reported, never silently dropped
		CHECK(bones.missingBones.size() == 3); // projectilerock (public bone), arrow (extra public bone), projectilerock (no barrel found)
		CHECK(std::count(bones.missingBones.begin(), bones.missingBones.end(), "projectilerock") == 2);
		CHECK(std::count(bones.missingBones.begin(), bones.missingBones.end(), "arrow") == 1);
		CHECK(bones.barrels[0].empty());
	}
}
