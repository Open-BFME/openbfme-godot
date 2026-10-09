// OpenBFME unit tests: the BFME draw module runtime (lane DRAW-1, spec w3d-and-draw.md 4.4-4.6, checklist steps 15-17).
// A synthetic soldier (skeleton, HLOD, animations of known length) is drawn through the real asset manager. Every expectation
// is worked out by hand from the INI text and the RotWK routines cited in W3DScriptedModelDraw.h (RW 0x4BF2D8 / 0x4BF15E /
// 0x4BE587 / 0x4B55D5 / 0x4BF560), never read back from the code under test.

#include "doctest.h"

#include "Common/AsciiString.h"
#include "W3DDrawTestUtil.h"
#include "W3dSynth.h"

#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DScriptedModelDraw.h"

#include <algorithm>
#include <deque>
#include <functional>
#include <limits>
#include <map>

using namespace drawtest;
using namespace w3dsynth;

namespace
{
// A scripted client random: value() pops the next queued number and records the (lo, hi) it was asked for; real() returns hi
// for an empty range (as the retail does) and the middle of the range otherwise.
class QueueRandom : public W3DDrawRandom
{
public:
	std::deque<int> values;
	std::vector<std::pair<int, int>> asked;
	int value(int lo, int hi) override
	{
		asked.emplace_back(lo, hi);
		if (values.empty())
		{
			throw std::logic_error("QueueRandom: unexpected value(" + std::to_string(lo) + ", " + std::to_string(hi) + ")");
		}
		const int v = values.front();
		values.pop_front();
		return v;
	}
	float real(float lo, float hi) override { return hi - lo <= 0.0f ? hi : (lo + hi) * 0.5f; }
	std::vector<std::string> unverified() const override { return { "S-093: test double source" }; }
};

class FakeScripts : public W3DDrawScriptHost
{
public:
	std::map<std::string, std::function<std::string(W3DDrawScriptApi &)>> byText;
	std::vector<std::string> ran;
	bool run(const std::string &script, W3DDrawScriptApi &api, std::string *returned, std::string *error) override
	{
		std::string key;
		for (char c : script)
		{
			if (c != ' ' && c != '\t')
			{
				key += c;
			}
		}
		ran.push_back(key);
		auto it = byText.find(key);
		if (it == byText.end())
		{
			*error = "no fake script for " + key;
			return false;
		}
		*returned = it->second(api);
		return true;
	}
};

bool clipIs(const W3DDrawTrack &t, const char *name) { return AsciiStringUtil::compareNoCase(t.clipName, name) == 0; }

// Skeleton ABUNIT_SKL: ROOT, HIP (child of root), HAND (child of hip), ARROW01 (child of hand), FIREBONE (child of hand); HLOD
// ABUNIT_SKN with a body on the hip, a sword on the hand and an arrow on the arrow bone, plus a second model ABUNIT_DEAD on the
// same skeleton. Animations are raw, 30 fps, with the given frame counts, named ABUNIT_<n> in art\w3d\ab\abunit_<n>.w3d.
void buildSoldier(MemoryFileSource &fs, const std::map<std::string, int> &animations)
{
	fs.Add("art\\w3d\\ab\\abunit_skl.w3d",
		hierarchyChunk("ABUNIT_SKL", { pivot("ROOTTRANSFORM", 0xFFFFFFFFu, 0, 0, 0), pivot("HIP", 0, 0, 0, 1), pivot("HAND", 1, 0, 2, 0), pivot("ARROW01", 2, 0, 0, 1),
										 pivot("FIREBONE", 2, 1, 0, 0) }));
	for (const char *model : { "ABUNIT_SKN", "ABUNIT_DEAD" })
	{
		std::vector<std::uint8_t> bytes;
		SynthMesh body;
		body.Vertices = { { 0, 0, 0 }, { 1, 0, 0 }, { 1, 1, 0 }, { 0, 1, 0 } };
		body.Triangles = { 0, 1, 2, 0, 2, 3 };
		body.Container = model;
		for (const char *part : { "BODY", "SWORD", "ARROW" })
		{
			body.Name = part;
			append(bytes, meshChunk(body));
		}
		append(bytes, hlodChunk(model, "ABUNIT_SKL", { { std::string(model) + ".BODY", 1 }, { std::string(model) + ".SWORD", 2 }, { std::string(model) + ".ARROW", 3 } }));
		fs.Add(std::string("art\\w3d\\ab\\") + (std::string(model) == "ABUNIT_SKN" ? "abunit_skn" : "abunit_dead") + ".w3d", bytes);
	}
	for (const auto &a : animations)
	{
		std::string lower = a.first;
		for (char &c : lower)
		{
			c = (char)std::tolower((unsigned char)c);
		}
		fs.Add("art\\w3d\\ab\\" + lower + ".w3d", rawAnimChunk(a.first.c_str(), "ABUNIT_SKL", (std::uint32_t)a.second, 30, { { ANIM_CHANNEL_Z, 1, 0, 1, { 0 } } }));
	}
}

struct Rig
{
	MemoryFileSource fs;
	std::unique_ptr<WW3DAssetManager> assets;
	std::unique_ptr<WW3DDrawAssets> drawAssets;
	Harness harness;
	QueueRandom random;
	FakeScripts scripts;
	std::unique_ptr<W3DScriptedModelDraw> draw;
	W3DModelDrawModuleData *data = nullptr;

	Rig(const std::map<std::string, int> &animations, const std::string &iniBody, W3DDrawModuleClass cls = W3D_DRAW_SCRIPTED_MODEL)
	{
		buildSoldier(fs, animations);
		assets.reset(new WW3DAssetManager(fs));
		drawAssets.reset(new WW3DDrawAssets(*assets));
		data = harness.parse(iniBody, cls);
		REQUIRE_MESSAGE(data, harness.error);
	}

	void start(bool withScripts = false, bool buildBones = false, const W3DScriptedModelDraw::Options &base = W3DScriptedModelDraw::Options())
	{
		W3DScriptedModelDraw::Options o = base;
		o.buildBones = buildBones;
		draw.reset(new W3DScriptedModelDraw(*data, *drawAssets, random, withScripts ? &scripts : nullptr, o));
	}
	void startHorde(int level)
	{
		W3DScriptedModelDraw::Options o;
		o.buildBones = false;
		draw.reset(new W3DHordeModelDraw(*dynamic_cast<W3DHordeModelDrawModuleData *>(data), level, *drawAssets, random, nullptr, o));
	}
};

bool hasStop(const W3DScriptedModelDraw &d, const char *id)
{
	for (const W3DStopHit &h : d.stops())
	{
		if (h.Id == id)
		{
			return true;
		}
	}
	return false;
}

// DefaultModelConditionState with the skeleton the animation names are resolved against (RW 0x4BEC4F passes the state's Skeleton)
const char *kDefaultState = "DefaultModelConditionState\n  Model = ABUNIT_SKN\n  Skeleton = ABUNIT_SKL\nEnd\n";

std::string anim(const char *name, const char *mode = "LOOP", const char *extra = "")
{
	return std::string("  Animation = ") + name + "\n    AnimationName = " + name + "\n    AnimationMode = " + mode + "\n" + extra + "  End\n";
}

const char *kIdleThree =
	"DefaultModelConditionState\n  Model = ABUNIT_SKN\n  Skeleton = ABUNIT_SKL\nEnd\n"
	"IdleAnimationState\n"
	"  StateName = STATE_Idle\n"
	"  Animation = IDLA\n    AnimationName = ABUNIT_IDLA\n    AnimationMode = ONCE\n    AnimationPriority = 20\n  End\n"
	"  Animation = IDLB\n    AnimationName = ABUNIT_IDLB\n    AnimationMode = ONCE\n  End\n"
	"  Animation = IDLC\n    AnimationName = ABUNIT_IDLC\n    AnimationMode = ONCE\n  End\n"
	"End\n"
	"End\n";
} // namespace

// ---------------------------------------------------------------------------------------------------------------------
TEST_CASE("draw runtime: the idle state picks by AnimationPriority, avoids repeating itself, and blends into the next pick")
{
	Rig r({ { "ABUNIT_IDLA", 31 }, { "ABUNIT_IDLB", 31 }, { "ABUNIT_IDLC", 31 } }, kIdleThree);
	// RW 0x4B4272: weights [20, 1, 1], total 22, draw value(0, 21) (RW 0x6D32E4); 19 lands in entry 0
	r.random.values = { 19 };
	r.start();
	REQUIRE(r.random.asked.size() == 1);
	CHECK(r.random.asked[0] == std::make_pair(0, 21));
	CHECK(r.draw->errors().empty());
	CHECK(r.draw->currentAnimationState()->stateName == "STATE_Idle");
	W3DDrawFrame f = r.draw->frame();
	CHECK(f.modelName == "ABUNIT_SKN");
	REQUIRE(f.model != nullptr);
	CHECK(f.trackCount == 1);
	// the model state's Skeleton (lower-cased by the parse) is the prefix of an undotted AnimationName (RW 0x4B9100)
	CHECK(f.tracks[0].clipName == "abunit_skl.ABUNIT_IDLA");
	CHECK(f.tracks[0].frame == doctest::Approx(0.0f));
	CHECK(!f.blending);

	// 1000 ms at 30 fps is 30 frames: the ONCE animation (31 frames, last = 30) reaches its last frame and is complete
	// (RW 0x4B271C mode 2: frames - 1 <= frame). A state without conditions then starts again (RW 0x4BFA77), avoiding index 0:
	// weights [19, 1, 1], total 21, draw value(0, 20); 19 skips entry 0 (19 - 19 = 0 is not < 0) and lands in entry 1.
	r.random.values = { 19 };
	r.draw->advance(1000.0);
	REQUIRE(r.random.asked.size() == 2);
	CHECK(r.random.asked[1] == std::make_pair(0, 20));
	f = r.draw->frame();
	CHECK(f.trackCount == 2);
	CHECK(clipIs(f.tracks[0], "abunit_skl.ABUNIT_IDLA"));
	CHECK(f.tracks[0].frame == doctest::Approx(30.0f).epsilon(0.001));
	CHECK(clipIs(f.tracks[1], "abunit_skl.ABUNIT_IDLB"));
	CHECK(f.tracks[1].frame == doctest::Approx(0.0f));
	// blend: AnimationBlendTime defaults to 5 frames: countdown = max(1, min(5, 31 - 1)) = 5, weight of the incoming 1 - 5/5 = 0
	CHECK(f.blending);
	CHECK(f.blendPercentage == doctest::Approx(0.0f));

	// 100 ms = 3 frames: the incoming track is at 3, the countdown 5 - 3 = 2, so the weight is 1 - 2/5 = 0.6 (RW 0x4B33FB)
	r.draw->advance(100.0);
	f = r.draw->frame();
	CHECK(f.tracks[1].frame == doctest::Approx(3.0f).epsilon(0.001));
	CHECK(f.blendPercentage == doctest::Approx(0.6f).epsilon(0.001));
	CHECK(f.motion0 == f.tracks[0].anim);
	CHECK(f.motion1 == f.tracks[1].anim);
	// 400 ms = 12 more frames: countdown 2 - 12 < 0, the incoming track becomes track 0 (at 3 + 12 = 15) and the blend ends
	r.draw->advance(400.0);
	f = r.draw->frame();
	CHECK(f.trackCount == 1);
	CHECK(!f.blending);
	CHECK(clipIs(f.tracks[0], "abunit_skl.ABUNIT_IDLB"));
	CHECK(f.tracks[0].frame == doctest::Approx(15.0f).epsilon(0.001));
	CHECK(r.draw->errors().empty());
}

namespace
{
// one MOVING state with one animation of the given mode
std::string oneAnim(const char *mode, const char *extra = "")
{
	return std::string(kDefaultState) + "AnimationState = MOVING\n" + anim("ABUNIT_RUN", mode, extra) + "End\nEnd\n";
}
} // namespace

TEST_CASE("draw runtime: the per track stepping of every animation mode (RW 0x4BF6FD-0x4BF8B4)")
{
	const ModelConditionFlags moving = flagsOf({ "MOVING" });
	{
		Rig r({ { "ABUNIT_RUN", 31 } }, oneAnim("LOOP"));
		r.start();
		r.draw->setModelConditionFlags(moving);
		CHECK(r.draw->frame().tracks[0].frame == doctest::Approx(0.0f));
		r.draw->advance(500.0); // +15
		CHECK(r.draw->frame().tracks[0].frame == doctest::Approx(15.0f).epsilon(0.001));
		r.draw->advance(700.0); // +21 -> 36 > 30: loops = (int)(36 / 30) = 1, frame = 36 - 30
		CHECK(r.draw->frame().tracks[0].frame == doctest::Approx(6.0f).epsilon(0.001));
		CHECK(r.draw->frame().tracks[0].completed);
	}
	{
		Rig r({ { "ABUNIT_RUN", 31 } }, oneAnim("ONCE"));
		r.start();
		r.draw->setModelConditionFlags(moving);
		r.draw->advance(2000.0); // +60, clamped to the last frame
		CHECK(r.draw->frame().tracks[0].frame == doctest::Approx(30.0f));
		CHECK(r.draw->frame().tracks[0].completed);
		r.draw->advance(500.0); // stays; a state with conditions and no RESTART flag does not start again
		CHECK(r.draw->frame().tracks[0].frame == doctest::Approx(30.0f));
		CHECK(r.draw->frame().trackCount == 1);
	}
	{
		Rig r({ { "ABUNIT_RUN", 31 } }, oneAnim("LOOP_PINGPONG"));
		r.start();
		r.draw->setModelConditionFlags(moving);
		r.draw->advance(1000.0); // +30 -> 30, not above the last frame
		CHECK(r.draw->frame().tracks[0].frame == doctest::Approx(30.0f).epsilon(0.001));
		r.draw->advance(500.0); // +15 -> 45 > 30: loops 1, wrapped 30, frame = (30 + 30) - 45 = 15, direction -1
		CHECK(r.draw->frame().tracks[0].frame == doctest::Approx(15.0f).epsilon(0.001));
		CHECK(r.draw->frame().tracks[0].direction == -1);
		r.draw->advance(300.0); // -9 -> 6
		CHECK(r.draw->frame().tracks[0].frame == doctest::Approx(6.0f).epsilon(0.001));
		r.draw->advance(400.0); // -12 -> -6 < 0: loops = (int)(6 / 30) = 0, direction +1, frame = 6
		CHECK(r.draw->frame().tracks[0].frame == doctest::Approx(6.0f).epsilon(0.001));
		CHECK(r.draw->frame().tracks[0].direction == 1);
	}
	{
		Rig r({ { "ABUNIT_RUN", 31 } }, oneAnim("LOOP_BACKWARDS"));
		r.start();
		r.draw->setModelConditionFlags(moving);
		CHECK(r.draw->frame().tracks[0].frame == doctest::Approx(30.0f)); // backward modes start on the last frame (RW 0x4BECEE)
		r.draw->advance(500.0); // -15
		CHECK(r.draw->frame().tracks[0].frame == doctest::Approx(15.0f).epsilon(0.001));
		r.draw->advance(700.0); // -21 -> -6: frame = -6 + (0 + 30) = 24
		CHECK(r.draw->frame().tracks[0].frame == doctest::Approx(24.0f).epsilon(0.001));
	}
	{
		Rig r({ { "ABUNIT_RUN", 31 } }, oneAnim("ONCE_BACKWARDS"));
		r.start();
		r.draw->setModelConditionFlags(moving);
		r.draw->advance(2000.0);
		CHECK(r.draw->frame().tracks[0].frame == doctest::Approx(0.0f));
		CHECK(r.draw->frame().tracks[0].completed);
	}
	{
		// MANUAL and PLAY_TO_FRAME do not advance (the switch has no case for 0 and 4)
		Rig r({ { "ABUNIT_RUN", 31 } }, oneAnim("MANUAL"));
		r.start();
		r.draw->setModelConditionFlags(moving);
		r.draw->advance(500.0);
		CHECK(r.draw->frame().tracks[0].frame == doctest::Approx(0.0f));
	}
	{
		// AnimationSpeedFactorRange = 2 2 doubles the rate (real(2, 2) returns hi)
		Rig r({ { "ABUNIT_RUN", 31 } }, oneAnim("LOOP", "    AnimationSpeedFactorRange = 2.0 2.0\n"));
		r.start();
		r.draw->setModelConditionFlags(moving);
		r.draw->advance(250.0); // 30 * 2 * 0.25 = 15
		CHECK(r.draw->frame().tracks[0].frame == doctest::Approx(15.0f).epsilon(0.001));
		CHECK(hasStop(*r.draw, "S-093")); // the real random draw is reported
	}
}

TEST_CASE("draw runtime: the track queue (RW 0x4B55D5): replace the incoming track, finish a blend more than half done, no blend for 0, queue behind MustCompleteBlend")
{
	const std::string text =
		std::string(kDefaultState) +
		"IdleAnimationState\n" + anim("ABUNIT_IDLA") + "End\n"
		"AnimationState = MOVING\n  Flags = RANDOMSTART\n" + anim("ABUNIT_RUN") + "End\n"
		"AnimationState = ATTACKING\n  Flags = START_FRAME_LAST\n" + anim("ABUNIT_RUN", "LOOP", "    AnimationBlendTime = 10\n") + "End\n"
		"AnimationState = DYING\n" + anim("ABUNIT_SHORT", "LOOP", "    AnimationBlendTime = 0\n") + "End\n"
		"End\n";
	Rig r({ { "ABUNIT_RUN", 31 }, { "ABUNIT_SHORT", 4 }, { "ABUNIT_IDLA", 31 } }, text);
	r.start();
	CHECK(clipIs(r.draw->frame().tracks[0], "abunit_skl.ABUNIT_IDLA")); // track 0: the idle loop
	r.random.values = { 7 };
	r.draw->setModelConditionFlags(flagsOf({ "MOVING" }));
	// RANDOMSTART: GameClientRandomValue(0, frames - 1) = (0, 30); the new animation is track 1, blending in over the default 5 frames
	CHECK(r.random.asked.back() == std::make_pair(0, 30));
	W3DDrawFrame f = r.draw->frame();
	CHECK(f.trackCount == 2);
	CHECK(clipIs(f.tracks[1], "abunit_skl.ABUNIT_RUN"));
	CHECK(f.tracks[1].frame == doctest::Approx(7.0f));
	CHECK(f.blendPercentage == doctest::Approx(0.0f));

	// ATTACKING arrives while that blend is just starting (countdown / initial = 5/5 = 1, not below 0.5): the incoming track is
	// REPLACED (RW 0x4B56D9-0x4B5706). START_FRAME_LAST starts it on frame 30; AnimationBlendTime 10 < frames - 1, so 10 frames.
	r.draw->setModelConditionFlags(flagsOf({ "ATTACKING" }));
	f = r.draw->frame();
	CHECK(f.trackCount == 2);
	CHECK(f.tracks[1].frame == doctest::Approx(30.0f));
	CHECK(f.blendPercentage == doctest::Approx(0.0f));
	// 100 ms = 3 frames: track 0 at 3, track 1 wraps (33 > 30: loops = 1) to 3, countdown 10 - 3 = 7: weight 0.3; another 100 ms: 0.6
	r.draw->advance(100.0);
	CHECK(r.draw->frame().tracks[1].frame == doctest::Approx(3.0f).epsilon(0.001));
	CHECK(r.draw->frame().blendPercentage == doctest::Approx(0.3f).epsilon(0.001));
	r.draw->advance(100.0);
	f = r.draw->frame();
	CHECK(f.tracks[0].frame == doctest::Approx(6.0f).epsilon(0.001));
	CHECK(f.tracks[1].frame == doctest::Approx(6.0f).epsilon(0.001));
	CHECK(f.blendPercentage == doctest::Approx(0.6f).epsilon(0.001));

	// MOVING again: countdown / initial = 4/10 = 0.4 < 0.5, so the running blend is finished first (the incoming track becomes
	// track 0, still at 6) and the new animation is track 1 from frame 9 with the default 5 frame blend (RW 0x4B56FC)
	r.random.values = { 9 };
	r.draw->setModelConditionFlags(flagsOf({ "MOVING" }));
	f = r.draw->frame();
	CHECK(f.trackCount == 2);
	CHECK(f.tracks[0].frame == doctest::Approx(6.0f).epsilon(0.001));
	CHECK(f.tracks[1].frame == doctest::Approx(9.0f));
	CHECK(f.blendPercentage == doctest::Approx(0.0f));

	// DYING has AnimationBlendTime 0: no blend (RW 0x4B563F: a zero blend time takes the plain path): the animation replaces
	// track 0 and track 1 is cleared
	r.draw->setModelConditionFlags(flagsOf({ "DYING" }));
	f = r.draw->frame();
	CHECK(f.trackCount == 1);
	CHECK(!f.blending);
	CHECK(clipIs(f.tracks[0], "abunit_skl.ABUNIT_SHORT"));
	CHECK(f.tracks[0].frame == doctest::Approx(0.0f));
	CHECK(r.draw->errors().empty());
}

TEST_CASE("draw runtime: AnimationMustCompleteBlend sends the next request to track 2, and the blend restarts with it when the first one ends")
{
	const std::string text =
		std::string(kDefaultState) +
		"IdleAnimationState\n" + anim("ABUNIT_IDLA") + "End\n"
		"AnimationState = MOVING\n" + anim("ABUNIT_RUN", "LOOP", "    AnimationMustCompleteBlend = Yes\n") + "End\n"
		"AnimationState = ATTACKING\n" + anim("ABUNIT_SHORT", "LOOP", "    AnimationBlendTime = 3\n") + "End\n"
		"End\n";
	Rig r({ { "ABUNIT_RUN", 31 }, { "ABUNIT_SHORT", 4 }, { "ABUNIT_IDLA", 31 } }, text);
	r.start();
	r.draw->setModelConditionFlags(flagsOf({ "MOVING" }));
	r.draw->setModelConditionFlags(flagsOf({ "ATTACKING" })); // track 1 is marked MustCompleteBlend: this one waits in track 2
	W3DDrawFrame f = r.draw->frame();
	REQUIRE(f.trackCount == 3);
	CHECK(clipIs(f.tracks[1], "abunit_skl.ABUNIT_RUN"));
	CHECK(clipIs(f.tracks[2], "abunit_skl.ABUNIT_SHORT"));
	// 200 ms = 6 frames: the first blend (5 frames) ends, track 1 becomes track 0 (6) and track 2 track 1: a 4 frame animation,
	// looping over frames 0..3, wraps 6 -> 0; the blend restarts at max(1, min(3, 4 - 1)) = 3 frames
	r.draw->advance(200.0);
	f = r.draw->frame();
	REQUIRE(f.trackCount == 2);
	CHECK(clipIs(f.tracks[0], "abunit_skl.ABUNIT_RUN"));
	CHECK(f.tracks[0].frame == doctest::Approx(6.0f).epsilon(0.001));
	CHECK(clipIs(f.tracks[1], "abunit_skl.ABUNIT_SHORT"));
	CHECK(f.tracks[1].frame == doctest::Approx(0.0f).epsilon(0.001));
	CHECK(f.blendPercentage == doctest::Approx(0.0f));
	CHECK(r.draw->errors().empty());
}

TEST_CASE("draw runtime: animations of another hierarchy are not blended (RW 0x4B55FB)")
{
	const std::string text =
		std::string(kDefaultState) +
		"ModelConditionState = DAMAGED\n  Model = ABUNIT_SKN\n  Skeleton = OTHER_SKL\nEnd\n"
		"IdleAnimationState\n" + anim("ABUNIT_IDLA") + "End\n"
		"AnimationState = DAMAGED\n" + anim("OTHER_A") + "End\n"
		"End\n";
	Rig r({ { "ABUNIT_IDLA", 31 } }, text);
	// an animation whose header hierarchy is OTHER_SKL, registered as OTHER_SKL.OTHER_A (file named after the part after the dot)
	r.fs.Add("art\\w3d\\ot\\other_a.w3d", rawAnimChunk("OTHER_A", "OTHER_SKL", 31, 30, { { ANIM_CHANNEL_Z, 1, 0, 1, { 0 } } }));
	r.start();
	r.draw->setModelConditionFlags(flagsOf({ "DAMAGED" }));
	const W3DDrawFrame f = r.draw->frame();
	CHECK(r.draw->errors().empty());
	CHECK(f.trackCount == 1); // it replaced track 0 outright
	CHECK(!f.blending);
	CHECK(clipIs(f.tracks[0], "other_skl.OTHER_A"));
}

TEST_CASE("draw runtime: a missing animation is an error, and the other animations of the state are tried in order (RW 0x4BEBB0)")
{
	const std::string text =
		std::string(kDefaultState) +
		"IdleAnimationState\n" + anim("ABUNIT_IDLA") + "End\n"
		"AnimationState = MOVING\n"
		"  Animation = GONE\n    AnimationName = ABUNIT_NOSUCH ABUNIT_RUN\n  End\n"           // the list: the first name that exists is used
		"  Animation = NOPE\n    AnimationName = ABUNIT_NOSUCH2\n    AnimationPriority = 100\n  End\n"
		"End\n"
		"End\n";
	Rig r({ { "ABUNIT_RUN", 31 }, { "ABUNIT_IDLA", 31 } }, text);
	r.start();
	// weights [1, 100], total 101: value 0 lands in entry 0 (GONE): its list resolves to the second name
	r.random.values = { 0 };
	r.draw->setModelConditionFlags(flagsOf({ "MOVING" }));
	W3DDrawFrame f = r.draw->frame();
	CHECK(r.draw->errors().empty());
	REQUIRE(f.trackCount == 2);
	CHECK(clipIs(f.tracks[1], "abunit_skl.ABUNIT_RUN"));
	// entry 1 (NOPE) has no resolvable name: it is reported, and the state's other animation (entry 0) plays instead
	r.draw->setModelConditionFlags(flagsOf({}));
	r.random.values = { 100 }; // 100 - 1 = 99 >= 0, then 99 - 100 < 0: entry 1
	r.draw->setModelConditionFlags(flagsOf({ "MOVING" }));
	REQUIRE(!r.draw->errors().empty());
	CHECK(r.draw->errors().back().find("ABUNIT_NOSUCH2") != std::string::npos);
	CHECK(r.draw->currentAnimationIndex() == 0);
	f = r.draw->frame();
	CHECK(clipIs(f.tracks[f.trackCount - 1], "abunit_skl.ABUNIT_RUN"));
}

TEST_CASE("draw runtime: #(MODEL) in an animation name is replaced by the state's ModelAnimationPrefix (RW 0x4BD4CE)")
{
	const std::string text =
		"DefaultModelConditionState\n  Model = ABUNIT_SKN\n  Skeleton = ABUNIT_SKL\n  ModelAnimationPrefix = ABUNIT\nEnd\n"
		"IdleAnimationState\n  Animation = A\n    AnimationName = #(MODEL)_IDLA\n    AnimationMode = LOOP\n  End\nEnd\n"
		"End\n";
	Rig r({ { "ABUNIT_IDLA", 31 } }, text);
	r.start();
	CHECK(r.draw->errors().empty());
	CHECK(clipIs(r.draw->frame().tracks[0], "abunit_skl.ABUNIT_IDLA"));
}

TEST_CASE("draw runtime: without a Skeleton the AnimationName is the registry name itself (a HIERARCHY.ANIM name)")
{
	const std::string text =
		"DefaultModelConditionState\n  Model = ABUNIT_SKN\nEnd\n"
		"IdleAnimationState\n  Animation = A\n    AnimationName = ABUNIT_SKL.ABUNIT_IDLA\n    AnimationMode = LOOP\n  End\nEnd\n"
		"AnimationState = MOVING\n  Animation = B\n    AnimationName = ABUNIT_IDLA\n    AnimationMode = LOOP\n  End\nEnd\n"
		"End\n";
	Rig r({ { "ABUNIT_IDLA", 31 } }, text);
	r.start();
	CHECK(r.draw->errors().empty());
	CHECK(clipIs(r.draw->frame().tracks[0], "ABUNIT_SKL.ABUNIT_IDLA"));
	// the undotted name with no skeleton is not a registry name: reported
	r.draw->setModelConditionFlags(flagsOf({ "MOVING" }));
	REQUIRE(!r.draw->errors().empty());
	CHECK(r.draw->errors().back().find("not registered") != std::string::npos);
}

TEST_CASE("draw runtime: the model condition state and the animation state are matched separately; the render object is reused when the model is the same")
{
	const std::string text =
		std::string(kDefaultState) +
		"ModelConditionState = DAMAGED\n  Model = ABUNIT_SKN\n  Skeleton = ABUNIT_SKL\n  ModelAnimationPrefix = Damaged\nEnd\n"
		"ModelConditionState = DYING\n  Model = ABUNIT_DEAD\n  Skeleton = ABUNIT_SKL\nEnd\n"
		"IdleAnimationState\n  StateName = STATE_Idle\n" + anim("ABUNIT_IDLA") + "End\n"
		"AnimationState = MOVING\n  StateName = STATE_Moving\n" + anim("ABUNIT_RUN") + "End\n"
		"End\n";
	Rig r({ { "ABUNIT_IDLA", 31 }, { "ABUNIT_RUN", 31 } }, text);
	r.start();
	CHECK(r.draw->renderObjectsCreated() == 1);
	CHECK(r.draw->frame().animationStateName == "STATE_Idle");
	// DAMAGED changes the model condition state but keeps the model: no new render object; the animation state is still idle
	r.draw->setModelConditionFlags(flagsOf({ "DAMAGED" }));
	CHECK(r.draw->renderObjectsCreated() == 1);
	CHECK(r.draw->currentModelState()->modelAnimationPrefix == "Damaged");
	CHECK(r.draw->frame().animationStateName == "STATE_Idle");
	// MOVING + DAMAGED: the model state stays (DAMAGED) and the animation state becomes moving
	r.draw->setModelConditionFlags(flagsOf({ "DAMAGED", "MOVING" }));
	CHECK(r.draw->renderObjectsCreated() == 1);
	CHECK(r.draw->frame().animationStateName == "STATE_Moving");
	// DYING picks the other model: the render object is recreated
	r.draw->setModelConditionFlags(flagsOf({ "DYING", "MOVING" }));
	CHECK(r.draw->renderObjectsCreated() == 2);
	CHECK(r.draw->frame().modelName == "ABUNIT_DEAD");
	CHECK(r.draw->frame().model->Name == "ABUNIT_DEAD");
	// the same flags again change nothing
	r.draw->setModelConditionFlags(flagsOf({ "DYING", "MOVING" }));
	CHECK(r.draw->renderObjectsCreated() == 2);
	CHECK(r.draw->errors().empty());
}

namespace
{
const char *kSelectedModule =
	"DefaultModelConditionState\n  Model = ABUNIT_SKN\n  Skeleton = ABUNIT_SKL\nEnd\n"
	"IdleAnimationState\n  StateName = STATE_Idle\n  Animation = IDLA\n    AnimationName = ABUNIT_IDLA\n    AnimationMode = LOOP\n  End\nEnd\n"
	"AnimationState = SELECTED\n  StateName = STATE_Selected\n"
	"  BeginScript\n    CHECK_PREV\n  EndScript\n"
	"  Animation = SELA\n    AnimationName = ABUNIT_SELA\n    AnimationMode = LOOP\n  End\n"
	"  Animation = SELB\n    AnimationName = ABUNIT_SELB\n    AnimationMode = LOOP\n  End\n"
	"End\n"
	"AnimationState = DYING\n  StateName = STATE_Dying\n  Animation = DIEA\n    AnimationName = ABUNIT_DIEA\n    AnimationMode = ONCE\n  End\nEnd\n"
	"TransitionState = TRANS_IdleToSelected\n  Animation\n    AnimationName = ABUNIT_TRANS\n    AnimationMode = ONCE\n    AnimationBlendTime = 5\n  End\nEnd\n"
	"End\n";

void installSelectedScript(FakeScripts &scripts)
{
	scripts.byText["CHECK_PREV"] = [](W3DDrawScriptApi &api) -> std::string {
		// CurDrawablePrevAnimationState(): the first time the previous state is STATE_Idle, ask for the transition; the returned
		// string is an Animation label of the state
		if (api.prevAnimationState() == "STATE_Idle")
		{
			api.setTransitionAnimState("TRANS_IdleToSelected");
		}
		return api.modelCondition("MOVING") ? "SELB" : "SELA";
	};
}
} // namespace

TEST_CASE("draw runtime: a script requests a transition state; the target is pending and is selected again when the transition completes")
{
	Rig r({ { "ABUNIT_IDLA", 31 }, { "ABUNIT_SELA", 31 }, { "ABUNIT_SELB", 31 }, { "ABUNIT_DIEA", 31 }, { "ABUNIT_TRANS", 11 } }, kSelectedModule);
	installSelectedScript(r.scripts);
	r.start(true);
	CHECK(r.draw->frame().animationStateName == "STATE_Idle");

	// SELECTED: select -> apply -> the script runs and asks for the transition (RW 0x4BE7A9: pending, target = the entered state,
	// then the transition is selected and the apply returns)
	r.draw->setModelConditionFlags(flagsOf({ "SELECTED" }));
	CHECK(r.draw->currentAnimationState()->stateName == "TRANS_IdleToSelected");
	CHECK(r.draw->pendingStatePending());
	REQUIRE(r.draw->pendingTarget() != nullptr);
	CHECK(r.draw->pendingTarget()->stateName == "STATE_Selected");
	W3DDrawFrame f = r.draw->frame();
	CHECK(f.trackCount == 2); // the idle loop is track 0, the transition blends in (5 frames)
	CHECK(clipIs(f.tracks[1], "abunit_skl.ABUNIT_TRANS"));

	// The transition: ONCE, 11 frames (last 10), blend 5. Timeline in steps of 100 ms (3 frames):
	//   step 1: idle 3, transition 3, countdown 5 - 3 = 2
	//   step 2: idle 6, transition 6, countdown -1 < 0: the transition becomes track 0 (6), not complete (6 < 10)
	//   step 3: 9
	//   step 4: 12 -> clamped to 10: complete; pending: findByCondition(pending flags) = SELECTED, selected again
	r.draw->advance(100.0);
	r.draw->advance(100.0);
	f = r.draw->frame();
	CHECK(f.trackCount == 1);
	CHECK(clipIs(f.tracks[0], "abunit_skl.ABUNIT_TRANS"));
	CHECK(f.tracks[0].frame == doctest::Approx(6.0f).epsilon(0.001));
	r.draw->advance(100.0);
	CHECK(r.draw->currentAnimationState()->stateName == "TRANS_IdleToSelected");
	r.draw->advance(100.0);
	CHECK(r.draw->currentAnimationState()->stateName == "STATE_Selected");
	CHECK(!r.draw->pendingStatePending()); // the second run of the script did not ask for a transition again
	f = r.draw->frame();
	REQUIRE(f.trackCount == 2);
	CHECK(clipIs(f.tracks[0], "abunit_skl.ABUNIT_TRANS"));
	CHECK(f.tracks[0].frame == doctest::Approx(10.0f));
	CHECK(clipIs(f.tracks[1], "abunit_skl.ABUNIT_SELA")); // no MOVING flag: the script returns SELA, the first Animation
	CHECK(r.draw->currentAnimationIndex() == 0);
	CHECK(r.draw->errors().empty());

	// the log is the order things happened in
	const std::vector<std::string> &log = r.draw->log();
	REQUIRE(log.size() >= 7);
	CHECK(log[0] == "enter STATE_Idle");
	CHECK(log[1] == "enter STATE_Selected");
	CHECK(log[2] == "script STATE_Selected");
	CHECK(log[3] == "transition TRANS_IdleToSelected");
	CHECK(log[4] == "enter TRANS_IdleToSelected");
	CHECK(log[5] == "enter STATE_Selected"); // after the transition
	CHECK(log[6] == "script STATE_Selected");
	// scripts ran for the entered state twice and never for the transition (it has none)
	CHECK(r.scripts.ran.size() == 2);
}

TEST_CASE("draw runtime: the same request during a transition keeps it (and its flags win); a different one interrupts it at once (RW 0x4BF442)")
{
	{
		Rig r({ { "ABUNIT_IDLA", 31 }, { "ABUNIT_SELA", 31 }, { "ABUNIT_SELB", 31 }, { "ABUNIT_DIEA", 31 }, { "ABUNIT_TRANS", 11 } }, kSelectedModule);
		installSelectedScript(r.scripts);
		r.start(true);
		r.draw->setModelConditionFlags(flagsOf({ "SELECTED" }));
		REQUIRE(r.draw->currentAnimationState()->stateName == "TRANS_IdleToSelected");
		// the flags change to {SELECTED, MOVING}: they still select SELECTED, the pending target: the transition stays and the
		// pending flags are replaced
		r.draw->setModelConditionFlags(flagsOf({ "SELECTED", "MOVING" }));
		CHECK(r.draw->currentAnimationState()->stateName == "TRANS_IdleToSelected");
		CHECK(r.draw->pendingStatePending());
		for (int i = 0; i < 4; ++i)
		{
			r.draw->advance(100.0);
		}
		// the completed transition re-selects with the NEW flags: the script sees MOVING and returns SELB (index 1)
		CHECK(r.draw->currentAnimationState()->stateName == "STATE_Selected");
		CHECK(r.draw->currentAnimationIndex() == 1);
		CHECK(clipIs(r.draw->frame().tracks[1], "abunit_skl.ABUNIT_SELB"));
		CHECK(r.draw->errors().empty());
	}
	{
		Rig r({ { "ABUNIT_IDLA", 31 }, { "ABUNIT_SELA", 31 }, { "ABUNIT_SELB", 31 }, { "ABUNIT_DIEA", 31 }, { "ABUNIT_TRANS", 11 } }, kSelectedModule);
		installSelectedScript(r.scripts);
		r.start(true);
		r.draw->setModelConditionFlags(flagsOf({ "SELECTED" }));
		REQUIRE(r.draw->currentAnimationState()->stateName == "TRANS_IdleToSelected");
		// DYING is not the pending target: the pending state is cleared and the dying state starts at once
		r.draw->setModelConditionFlags(flagsOf({ "DYING" }));
		CHECK(r.draw->currentAnimationState()->stateName == "STATE_Dying");
		CHECK(!r.draw->pendingStatePending());
		CHECK(r.draw->pendingTarget() == nullptr);
		// the transition animation is still playing as track 0 / 1 and blends into the death
		CHECK(clipIs(r.draw->frame().tracks[r.draw->frame().trackCount - 1], "abunit_skl.ABUNIT_DIEA"));
	}
}

TEST_CASE("draw runtime: AllowToContinue keeps the previous state until its animation completes, then the pending state is selected")
{
	const std::string text =
		std::string(kDefaultState) +
		"IdleAnimationState\n" + anim("ABUNIT_IDLA") + "End\n"
		"AnimationState = MOVING\n  StateName = STATE_Move\n" + anim("ABUNIT_RUN", "ONCE") + "End\n"
		"AnimationState = ATTACKING\n  StateName = STATE_Attack\n  BeginScript\n    ALLOW\n  EndScript\n" + anim("ABUNIT_SHORT", "LOOP") + "End\n"
		"End\n";
	Rig r({ { "ABUNIT_RUN", 31 }, { "ABUNIT_SHORT", 4 }, { "ABUNIT_IDLA", 31 } }, text);
	r.scripts.byText["ALLOW"] = [](W3DDrawScriptApi &api) -> std::string { api.allowToContinue(); return ""; };
	r.start(true);
	r.draw->setModelConditionFlags(flagsOf({ "MOVING" }));
	r.draw->advance(200.0); // the 5 frame blend ends: RUN (ONCE) is track 0 at frame 6
	REQUIRE(r.draw->frame().trackCount == 1);
	CHECK(r.draw->frame().tracks[0].frame == doctest::Approx(6.0f).epsilon(0.001));

	// ATTACKING: its script calls AllowToContinue; the RUN animation (ONCE, frame 6 < 30) is not complete, so the state goes back
	// to STATE_Move (RW 0x4BE87F-0x4BE8B2) and STATE_Attack becomes the pending target
	r.draw->setModelConditionFlags(flagsOf({ "ATTACKING" }));
	CHECK(r.draw->currentAnimationState()->stateName == "STATE_Move");
	CHECK(r.draw->pendingStatePending());
	CHECK(r.draw->pendingTarget()->stateName == "STATE_Attack");
	CHECK(r.draw->frame().trackCount == 1); // nothing new started
	// the run completes at frame 30 (24 frames = 800 ms): the pending state is resolved with the pending flags and entered; its
	// script allows the continuation again, but the animation is now complete, so SHORT starts
	for (int i = 0; i < 8; ++i)
	{
		r.draw->advance(100.0);
	}
	CHECK(r.draw->currentAnimationState()->stateName == "STATE_Attack");
	W3DDrawFrame f = r.draw->frame();
	REQUIRE(f.trackCount == 2);
	CHECK(clipIs(f.tracks[0], "abunit_skl.ABUNIT_RUN"));
	CHECK(f.tracks[0].frame == doctest::Approx(30.0f));
	CHECK(clipIs(f.tracks[1], "abunit_skl.ABUNIT_SHORT"));
	CHECK(r.draw->errors().empty());
}

TEST_CASE("draw runtime: without a script host the script is not run and the stop is reported (S-091)")
{
	const std::string text =
		std::string(kDefaultState) +
		"AnimationState = SELECTED\n  StateName = STATE_Selected\n  BeginScript\n    CHECK_PREV\n  EndScript\n" + anim("ABUNIT_SELA") + "End\n"
		"End\n";
	Rig r({ { "ABUNIT_SELA", 31 } }, text);
	r.start(false);
	r.draw->setModelConditionFlags(flagsOf({ "SELECTED" }));
	CHECK(hasStop(*r.draw, "S-091"));
	CHECK(r.draw->frame().trackCount == 1); // the state still plays its animation
	CHECK(r.draw->errors().empty());
}

TEST_CASE("draw runtime: scripts hide and show sub objects, and everything on a bone below them")
{
	const std::string text =
		std::string(kDefaultState) +
		"AnimationState = SELECTED\n  BeginScript\n    HIDE_SWORD\n  EndScript\n" + anim("ABUNIT_RUN") + "End\n"
		"AnimationState = MOVING\n  BeginScript\n    SHOW_SWORD\n  EndScript\n" + anim("ABUNIT_RUN") + "End\n"
		"End\n";
	Rig r({ { "ABUNIT_RUN", 31 } }, text);
	r.scripts.byText["HIDE_SWORD"] = [](W3DDrawScriptApi &api) -> std::string { api.hideSubObject("sword"); return ""; };
	r.scripts.byText["SHOW_SWORD"] = [](W3DDrawScriptApi &api) -> std::string { api.showSubObject("SWORD"); return ""; };
	r.start(true);
	r.draw->setModelConditionFlags(flagsOf({ "SELECTED" }));
	// the sword sits on HAND (bone 2); the arrow on ARROW01 (bone 3), a child of HAND: both are hidden, the body (HIP) is not
	W3DDrawFrame f = r.draw->frame();
	REQUIRE(f.hiddenSubObjects.size() == 2);
	CHECK(f.hiddenSubObjects[0] == "ABUNIT_SKN.SWORD");
	CHECK(f.hiddenSubObjects[1] == "ABUNIT_SKN.ARROW");
	r.draw->setModelConditionFlags(flagsOf({ "MOVING" }));
	CHECK(r.draw->frame().hiddenSubObjects.empty());
	CHECK(r.draw->errors().empty());
	CHECK(hasStop(*r.draw, "S-098"));

	// an unknown sub object is an error that reaches the test, not a silent no-op
	r.scripts.byText["SHOW_SWORD"] = [](W3DDrawScriptApi &api) -> std::string { api.hideSubObject("nosuchpart"); return ""; };
	r.draw->setModelConditionFlags(flagsOf({ "SELECTED" }));
	r.draw->setModelConditionFlags(flagsOf({ "MOVING" }));
	REQUIRE(!r.draw->errors().empty());
	CHECK(r.draw->errors().back().find("nosuchpart") != std::string::npos);
}

TEST_CASE("draw runtime: W3DHordeModelDraw limits the pick of states that are not idle-like to the LodOptions row (RW 0x4C0917 / 0x478A6C)")
{
	const std::string text =
		"LodOptions = LOW\n  MaxRandomAnimations = 1\nEnd\n"
		"LodOptions = MEDIUM\n  MaxRandomAnimations = 2\nEnd\n"
		"LodOptions = HIGH\n  MaxRandomAnimations = 3\nEnd\n"
		+ std::string(kDefaultState) +
		"IdleAnimationState\n" + anim("ABUNIT_IDLA", "LOOP", "    AnimationPriority = 20\n") + anim("ABUNIT_IDLB") + anim("ABUNIT_IDLC") + "End\n"
		"AnimationState = DYING\n" + anim("ABUNIT_DIEA", "ONCE", "    AnimationPriority = 20\n") + anim("ABUNIT_DIEB", "ONCE") + anim("ABUNIT_DIEC", "ONCE") + "End\n"
		"End\n";
	const std::map<std::string, int> anims = { { "ABUNIT_IDLA", 31 }, { "ABUNIT_IDLB", 31 }, { "ABUNIT_IDLC", 31 }, { "ABUNIT_DIEA", 31 }, { "ABUNIT_DIEB", 31 }, { "ABUNIT_DIEC", 31 } };
	{
		Rig r(anims, text, W3D_DRAW_HORDE_MODEL);
		r.random.values = { 21 }; // the idle state is idle-like (no conditions): limit 9999, all three entries, weights [20,1,1] -> value(0,21)
		r.startHorde(0);          // LOW: MaxRandomAnimations = 1
		CHECK(r.random.asked[0] == std::make_pair(0, 21));
		CHECK(clipIs(r.draw->frame().tracks[0], "abunit_skl.ABUNIT_IDLC")); // 21 - 20 = 1, 1 - 1 = 0, 0 - 1 < 0: entry 2
		// DYING has conditions, no ShareAnimation, no RESTART flag: the row's limit applies: 1 entry -> no draw at all
		r.draw->setModelConditionFlags(flagsOf({ "DYING" }));
		CHECK(r.random.asked.size() == 1);
		CHECK(r.draw->currentAnimationIndex() == 0);
	}
	{
		Rig r(anims, text, W3D_DRAW_HORDE_MODEL);
		r.random.values = { 0, 20 };
		r.startHorde(1); // MEDIUM: 2 entries, weights [20, 1], total 21, value(0, 20); 20 -> 20 - 20 = 0 is not < 0, 0 - 1 < 0: entry 1
		r.draw->setModelConditionFlags(flagsOf({ "DYING" }));
		CHECK(r.random.asked.back() == std::make_pair(0, 20));
		CHECK(r.draw->currentAnimationIndex() == 1);
	}
	{
		Rig r(anims, text, W3D_DRAW_HORDE_MODEL);
		r.random.values = { 0, 21 };
		r.startHorde(2); // HIGH: 3 entries, weights [20, 1, 1], total 22, value(0, 21)
		r.draw->setModelConditionFlags(flagsOf({ "DYING" }));
		CHECK(r.random.asked.back() == std::make_pair(0, 21));
		CHECK(r.draw->currentAnimationIndex() == 2);
		CHECK(dynamic_cast<W3DHordeModelDraw *>(r.draw.get())->lodLevel() == 2);
	}
	{
		Rig r(anims, text, W3D_DRAW_HORDE_MODEL);
		CHECK_THROWS_AS(W3DHordeModelDraw(*dynamic_cast<W3DHordeModelDrawModuleData *>(r.data), 3, *r.drawAssets, r.random), std::runtime_error);
	}
}

TEST_CASE("draw runtime: a horde draw scales a RANDOMSTART start frame by RandomStartFramePercent (RW 0x478A74)")
{
	const std::string text =
		"LodOptions = HIGH\n  RandomStartFramePercent = 50\nEnd\n"
		+ std::string(kDefaultState) +
		"IdleAnimationState\n" + anim("ABUNIT_IDLA") + "End\n"
		"AnimationState = MOVING\n  Flags = RANDOMSTART\n" + anim("ABUNIT_RUN") + "End\n"
		"End\n";
	Rig r({ { "ABUNIT_IDLA", 31 }, { "ABUNIT_RUN", 31 } }, text, W3D_DRAW_HORDE_MODEL);
	r.startHorde(2);
	r.random.values = { 9 };
	r.draw->setModelConditionFlags(flagsOf({ "MOVING" }));
	CHECK(r.random.asked.back() == std::make_pair(0, 30));
	CHECK(r.draw->frame().tracks[1].frame == doctest::Approx(4.0f)); // 9 * 50 / 100 = 4 (integer division)
	// a plain scripted draw takes the draw as it is
	Rig s({ { "ABUNIT_IDLA", 31 }, { "ABUNIT_RUN", 31 } }, text.substr(text.find("DefaultModelConditionState")), W3D_DRAW_SCRIPTED_MODEL);
	s.start();
	s.random.values = { 9 };
	s.draw->setModelConditionFlags(flagsOf({ "MOVING" }));
	CHECK(s.draw->frame().tracks[1].frame == doctest::Approx(9.0f));
}

TEST_CASE("draw runtime: a missing animation is an error, FadeBegin/End ramps the opacity, UseWeaponTiming and Distance use their sources")
{
	{
		Rig r({}, kIdleThree);
		r.random.values = { 0 };
		r.start();
		CHECK(!r.draw->errors().empty()); // the animations are not in the archives: reported, nothing plays
		CHECK(r.draw->frame().trackCount == 0);
	}
	{
		const std::string text =
			std::string(kDefaultState) +
			"AnimationState = MOVING\n" + anim("ABUNIT_RUN", "ONCE", "    FadeBeginFrame = 10\n    FadeEndFrame = 20\n") + "End\n"
			"End\n";
		Rig r({ { "ABUNIT_RUN", 31 } }, text);
		r.start();
		r.draw->setModelConditionFlags(flagsOf({ "MOVING" }));
		r.draw->advance(100.0); // frame 3 < begin: not fading yet (fading out: opacity 1)
		CHECK(r.draw->frame().opacity == doctest::Approx(1.0f));
		r.draw->advance(400.0); // frame 15: ratio 0.5, fading out: 0.5
		CHECK(r.draw->frame().opacity == doctest::Approx(0.5f).epsilon(0.01));
		CHECK(!r.draw->frame().hidden);
		r.draw->advance(400.0); // frame 27 > end 20: opacity 0 and the object is hidden
		CHECK(r.draw->frame().opacity == doctest::Approx(0.0f));
		CHECK(r.draw->frame().hidden);
	}
	{
		// UseWeaponTiming (RW 0x4BEE24-0x4BEEA7): natural duration 31 * 1000 / 30 = 1033.3 ms -> (int)(1033.3 * 0.005) = 5 frames;
		// a weapon cycle of 10 frames gives a speed factor 5 / 10 = 0.5: 1000 ms advance 30 * 0.5 = 15 frames
		const std::string text =
			std::string(kDefaultState) +
			"AnimationState = ATTACKING\n" + anim("ABUNIT_RUN", "ONCE", "    AnimationBlendTime = 0\n    UseWeaponTiming = Yes\n") + "End\n"
			"End\n";
		Rig r({ { "ABUNIT_RUN", 31 } }, text);
		W3DScriptedModelDraw::Options o;
		o.weaponTimingFrames = []() { return 10; };
		r.start(false, false, o);
		r.draw->setModelConditionFlags(flagsOf({ "ATTACKING" }));
		r.draw->advance(1000.0);
		CHECK(r.draw->frame().tracks[0].frame == doctest::Approx(15.0f).epsilon(0.001));
		CHECK(!hasStop(*r.draw, "S-095"));
		// without a source the field is reported and the animation runs at its natural speed
		Rig q({ { "ABUNIT_RUN", 31 } }, text);
		q.start();
		q.draw->setModelConditionFlags(flagsOf({ "ATTACKING" }));
		q.draw->advance(500.0);
		CHECK(q.draw->frame().tracks[0].frame == doctest::Approx(15.0f).epsilon(0.001));
		CHECK(hasStop(*q.draw, "S-095"));
	}
	{
		// Distance (RW 0x4B67D4): one loop covers 20 units; the object moves 2 units per logic frame, so a loop should take
		// 20 / 2 * 200 ms = 2000 ms while the animation lasts 31 * 1000 / 30 = 1033.3 ms: the speed sync is 1033.3 / 2000 = 0.5167.
		// It is computed at the end of an advance and used by the next one.
		const std::string text =
			std::string(kDefaultState) +
			"AnimationState = MOVING\n" + anim("ABUNIT_RUN", "LOOP", "    AnimationBlendTime = 0\n    Distance = 20\n") + "End\n"
			"End\n";
		Rig r({ { "ABUNIT_RUN", 31 } }, text);
		W3DScriptedModelDraw::Options o;
		o.objectSpeed = []() { return 2.0f; };
		r.start(false, false, o);
		r.draw->setModelConditionFlags(flagsOf({ "MOVING" }));
		r.draw->advance(100.0); // sync is still 1: +3 frames
		CHECK(r.draw->frame().tracks[0].frame == doctest::Approx(3.0f).epsilon(0.001));
		r.draw->advance(1000.0); // 30 * 0.5167 * 1000 * 0.001 = 15.5 frames
		CHECK(r.draw->frame().tracks[0].frame == doctest::Approx(18.5f).epsilon(0.002));
	}
}

// lane ANIM-1: the weapon cycle of UseWeaponTiming (RW 0x4BEE31 .. 0x4BEEA2): a negative answer is "no current weapon" (RW 0x4BEE3F: the speed factor is
// left alone, nothing reported); 0 is retail's division by zero (an infinite speed), reported as stop S-1580 and played at the natural speed
TEST_CASE("draw runtime: UseWeaponTiming without a current weapon keeps the speed; a weapon cycle of 0 frames is stop S-1580")
{
	const std::string text =
		std::string(kDefaultState) +
		"AnimationState = ATTACKING\n" + anim("ABUNIT_RUN", "ONCE", "    AnimationBlendTime = 0\n    UseWeaponTiming = Yes\n") + "End\n"
		"End\n";
	for (int cycle : { -1, 0 })
	{
		INFO(cycle);
		Rig r({ { "ABUNIT_RUN", 31 } }, text);
		W3DScriptedModelDraw::Options o;
		o.weaponTimingFrames = [cycle]() { return cycle; };
		r.start(false, false, o);
		r.draw->setModelConditionFlags(flagsOf({ "ATTACKING" }));
		r.draw->advance(500.0); // the natural speed: 30 * 0.5 = 15 frames
		CHECK(r.draw->frame().tracks[0].frame == doctest::Approx(15.0f).epsilon(0.001));
		CHECK_FALSE(hasStop(*r.draw, "S-095"));
		CHECK(hasStop(*r.draw, "S-1580") == (cycle == 0));
	}
}

TEST_CASE("draw runtime: RESTART_ANIM_WHEN_COMPLETE starts the state again when its animation is complete; without it the last frame holds")
{
	const std::string text =
		std::string(kDefaultState) +
		"AnimationState = MOVING\n  Flags = RESTART_ANIM_WHEN_COMPLETE\n" + anim("ABUNIT_RUN", "ONCE") + "End\n"
		"AnimationState = ATTACKING\n" + anim("ABUNIT_RUN", "ONCE") + "End\n"
		"End\n";
	Rig r({ { "ABUNIT_RUN", 31 } }, text);
	r.start();
	r.draw->setModelConditionFlags(flagsOf({ "MOVING" }));
	r.draw->advance(1000.0); // frame 30: complete -> RW 0x4BFA9D: bit 5 set -> apply(state, -1): the only animation again (one entry: no pick)
	W3DDrawFrame f = r.draw->frame();
	REQUIRE(f.trackCount == 2);
	CHECK(f.tracks[0].frame == doctest::Approx(30.0f));
	CHECK(f.tracks[1].frame == doctest::Approx(0.0f));
	r.draw->setModelConditionFlags(flagsOf({ "ATTACKING" }));
	for (int i = 0; i < 20; ++i)
	{
		r.draw->advance(100.0);
	}
	f = r.draw->frame();
	CHECK(f.trackCount == 1);
	CHECK(f.tracks[0].frame == doctest::Approx(30.0f)); // no RESTART flag: it holds
}

TEST_CASE("draw runtime: the constructor's <DefaultEmptyIdleAnimationState> is the idle fallback when no IdleAnimationState exists (RW 0x4C87DD-0x4C8800)")
{
	// no IdleAnimationState: only MOVING has an animation
	const std::string text =
		std::string(kDefaultState) +
		"AnimationState = MOVING\n  StateName = STATE_Move\n" + anim("ABUNIT_RUN") + "End\n"
		"End\n";
	Rig r({ { "ABUNIT_RUN", 31 } }, text);
	// the module data carries the empty state first (the constructor pushes it before any parsed state)
	REQUIRE(r.data->m_animationStates.size() == 2);
	CHECK(r.data->m_animationStates[0].stateName == "<DefaultEmptyIdleAnimationState>");
	CHECK(!r.data->m_animationStates[0].conditions.any());
	CHECK(r.data->m_animationStates[0].animations.empty());
	r.start();
	CHECK(r.draw->currentAnimationState()->stateName == "<DefaultEmptyIdleAnimationState>");
	CHECK(r.draw->frame().trackCount == 0);
	r.draw->setModelConditionFlags(flagsOf({ "MOVING" }));
	CHECK(r.draw->currentAnimationState()->stateName == "STATE_Move");
	REQUIRE(r.draw->frame().trackCount >= 1);
	// clearing MOVING selects the empty idle state again: the MOVING state does not stay current
	r.draw->setModelConditionFlags(ModelConditionFlags());
	CHECK(r.draw->currentAnimationState()->stateName == "<DefaultEmptyIdleAnimationState>");
	CHECK(r.draw->errors().empty());
	const std::vector<std::string> &log = r.draw->log();
	CHECK(log.back() == "enter <DefaultEmptyIdleAnimationState>");
}

TEST_CASE("draw runtime: a module with a TransitionState and no IdleAnimationState does not use the transition as its idle state")
{
	const std::string text =
		std::string(kDefaultState) +
		"AnimationState = MOVING\n  StateName = STATE_Move\n" + anim("ABUNIT_RUN") + "End\n"
		"TransitionState = TRANS_A\n  Animation\n    AnimationName = ABUNIT_TRANS\n    AnimationMode = ONCE\n  End\nEnd\n"
		"End\n";
	Rig r({ { "ABUNIT_RUN", 31 }, { "ABUNIT_TRANS", 11 } }, text);
	REQUIRE(r.data->m_animationStates.size() == 3);
	r.start();
	CHECK(r.draw->currentAnimationState()->stateName == "<DefaultEmptyIdleAnimationState>");
	CHECK(r.draw->frame().trackCount == 0);
	r.draw->setModelConditionFlags(flagsOf({ "MOVING" }));
	r.draw->setModelConditionFlags(ModelConditionFlags());
	CHECK(r.draw->currentAnimationState()->stateName == "<DefaultEmptyIdleAnimationState>");
	CHECK(r.draw->errors().empty());
}

TEST_CASE("draw runtime: an explicit IdleAnimationState goes in front of the constructor's empty state")
{
	Rig r({ { "ABUNIT_IDLA", 31 }, { "ABUNIT_IDLB", 31 }, { "ABUNIT_IDLC", 31 } }, kIdleThree);
	REQUIRE(r.data->m_animationStates.size() == 2);
	CHECK(r.data->m_animationStates[0].stateName == "STATE_Idle");
	CHECK(r.data->m_animationStates[1].stateName == "<DefaultEmptyIdleAnimationState>");
	CHECK(r.data->findBestAnimationState(ModelConditionFlags())->stateName == "STATE_Idle");
}

TEST_CASE("draw runtime: the module needs a state for the empty condition set")
{
	Harness h;
	W3DModelDrawModuleData *d = h.parse("ModelConditionState = DYING\n  Model = ABUNIT_SKN\nEnd\nEnd\n"); // first state must be NONE: parse error
	CHECK(d == nullptr);
	d = h.parse("End\n"); // a module with no model state at all
	MemoryFileSource fs;
	WW3DAssetManager am(fs);
	WW3DDrawAssets assets(am);
	QueueRandom rnd;
	CHECK_THROWS_WITH_AS(W3DScriptedModelDraw(*d, assets, rnd), "*** ASSET ERROR: all draw modules must have an IDLE state", std::runtime_error);
}

TEST_CASE("client random: the generator is named, not defaulted; both algorithms from the shared RandomValue; own state; provenance reported (S-093)")
{
	// the first three raw draws from the static initial seed array {F22D0E56, 883126E9, C624DD2F, 0702C49C, 9E353F7D, 6FDF3B64},
	// computed outside the code under test (a 64 bit product for the LCG, a six word add with carry chain for the donor generator)
	W3DClientRandom lcg(RandomAlgorithm::RotWK_GameDat_LCG);
	CHECK(lcg.next() == 0x1f6ce910u);
	CHECK(lcg.next() == 0x9852dbe5u);
	CHECK(lcg.next() == 0xc1ca9245u);
	W3DClientRandom carry(RandomAlgorithm::ZH_CarryChain);
	CHECK(carry.next() == 0x559a51edu);
	CHECK(carry.next() == 0x274ea7f5u);
	CHECK(carry.next() == 0xe827f7e0u);
	// seedRandom(1): the LCG fills all six words with 1 * 0x7FFFFFED; the carry chain adds 1 to every word of the initial array
	W3DClientRandom lcgSeeded(RandomAlgorithm::RotWK_GameDat_LCG);
	lcgSeeded.seed(1);
	for (std::uint32_t w : lcgSeeded.seedArray())
	{
		CHECK(w == 0x7fffffedu);
	}
	CHECK(lcgSeeded.next() == 0xe35a71a3u);
	W3DClientRandom carrySeeded(RandomAlgorithm::ZH_CarryChain);
	carrySeeded.seed(1);
	CHECK(carrySeeded.next() == 0x559a51f3u);
	CHECK(carrySeeded.next() == 0x274ea80au);

	// value(): rand % delta + lo with delta = hi - lo + 1 unsigned: the first LCG draw 0x1F6CE910 % 5 + 5
	W3DClientRandom a(RandomAlgorithm::RotWK_GameDat_LCG);
	CHECK(a.value(5, 9) == 0x1f6ce910u % 5 + 5);
	W3DClientRandom c(RandomAlgorithm::RotWK_GameDat_LCG);
	for (int i = 0; i < 200; ++i)
	{
		const int v = c.value(5, 9);
		CHECK(v >= 5);
		CHECK(v <= 9);
		const float f = c.real(1.0f, 2.0f);
		CHECK(f >= 1.0f);
		CHECK(f <= 2.0f);
	}
	CHECK(c.value(3, 2) == 2);         // delta == 0 gives hi (the unsigned range 3..2 wraps to 0)
	CHECK(c.real(2.0f, 2.0f) == 2.0f); // an empty range gives hi
	CHECK(c.value(0, -1) == -1);
	for (int i = 0; i < 50; ++i)
	{
		const int v = c.value(-3, 3);
		CHECK(v >= -3);
		CHECK(v <= 3);
	}
	W3DClientRandom same1(RandomAlgorithm::ZH_CarryChain), same2(RandomAlgorithm::ZH_CarryChain);
	for (int i = 0; i < 50; ++i)
	{
		CHECK(same1.next() == same2.next());
	}

	// the logic generator is a different instance: drawing from the client random does not move it
	GameLogicRandom logic(RandomAlgorithm::RotWK_GameDat_LCG);
	const GameLogicRandom::Seed before = logic.seedArray();
	W3DClientRandom client(RandomAlgorithm::RotWK_GameDat_LCG);
	for (int i = 0; i < 10; ++i)
	{
		client.next();
	}
	CHECK(logic.seedArray() == before);
	CHECK(client.seedArray() != before);

	// provenance: both algorithms report S-093 with the cross references
	for (RandomAlgorithm alg : { RandomAlgorithm::ZH_CarryChain, RandomAlgorithm::RotWK_GameDat_LCG })
	{
		const std::vector<std::string> lines = W3DClientRandom(alg).unverified();
		REQUIRE(lines.size() == 1);
		CHECK(lines[0].find("S-093") == 0);
		CHECK(lines[0].find("S-080") != std::string::npos);
		CHECK(lines[0].find("S-001") != std::string::npos);
		CHECK(lines[0].find(alg == RandomAlgorithm::ZH_CarryChain ? "using ZH_CarryChain" : "using RotWK_GameDat_LCG") != std::string::npos);
	}
}

TEST_CASE("client random: the range arithmetic is unsigned and wraps, no signed overflow (UBSan clean for INT_MIN / INT_MAX)")
{
	W3DClientRandom r(RandomAlgorithm::RotWK_GameDat_LCG);
	// value(INT_MIN, INT_MAX): delta = 0xFFFFFFFF + 1 wraps to 0 in uint32: hi, with no draw
	const GameLogicRandom::Seed before = r.seedArray();
	CHECK(r.value(std::numeric_limits<int>::min(), std::numeric_limits<int>::max()) == std::numeric_limits<int>::max());
	CHECK(r.seedArray() == before);
	// value(INT_MIN, INT_MAX - 1): delta = 0xFFFFFFFF; the first draw 0x1F6CE910 < delta so the offset is the draw itself, added to lo in 32 bits
	// 0x1F6CE910 + 0x80000000 = 0x9F6CE910 as a signed int is -1620383472
	CHECK(r.value(std::numeric_limits<int>::min(), std::numeric_limits<int>::max() - 1) == (int)0x9f6ce910u);
	// value(INT_MAX, INT_MIN): hi - lo + 1 = 0x80000000 - 0x7FFFFFFF + 1 = 2 (unsigned): draw % 2 + INT_MAX wraps
	const int v = r.value(std::numeric_limits<int>::max(), std::numeric_limits<int>::min());
	CHECK((v == std::numeric_limits<int>::max() || v == std::numeric_limits<int>::min()));
	// real(-FLT_MAX, FLT_MAX): the float delta overflows to +inf, which is > 0: no UB, a finite or infinite float, never a crash
	const float f = r.real(-std::numeric_limits<float>::max(), std::numeric_limits<float>::max());
	CHECK((f == f)); // not NaN... (inf * 0 would be NaN; the draw is non-zero here)
}

TEST_CASE("draw runtime: stops S-094 (parse), S-095 (stored fields), S-096 (camera fade) and S-097 (bones from the bind pose) are reported")
{
	const std::string text =
		"AlphaCameraFadeOuterRadius = 200\n"
		"AlphaCameraFadeInnerRadius = 100\n"
		"DefaultModelConditionState\n  Model = ABUNIT_SKN\n  Skeleton = ABUNIT_SKL\n  WeaponLaunchBone = PRIMARY ARROW\n  WeaponFireFXBone = PRIMARY FIREBONE\n"
		"  ParticleSysBone = HIP Smoke\nEnd\n"
		"IdleAnimationState\n  EnteringStateFX = FX_Idle\n  ShareAnimation = Yes\n  FXEvent = Frame:3 Name: FX_A\n  LuaEvent = Frame: 5 Data: doIt OnStateEnter\n  Animation\n    AnimationName = ABUNIT_IDLA ABUNIT_EXTRA\n  End\nEnd\n"
		"TimeOfDayTexture = a.tga NIGHT 1 b.tga\n"
		"AttachModel\n  Bone = HIP\n  Model = ABUNIT_SKN\nEnd\n"
		"End\n";
	Rig r({ { "ABUNIT_IDLA", 31 } }, text);
	r.start(false, true);
	CHECK(hasStop(*r.draw, "S-095")); // an FXEvent is stored, not played
	CHECK(hasStop(*r.draw, "S-096"));
	CHECK(hasStop(*r.draw, "S-097"));
	CHECK(hasStop(*r.draw, "S-090")); // AttachModel is parsed; the attached model update is not ported
	const std::vector<std::string> items = r.data->unverifiedParseItems();
	REQUIRE(items.size() == 4);
	CHECK(items[0].find("[S-094] 1 ParticleSysBone, 1 EnteringStateFX and 1 FXEvent") == 0);
	CHECK(items[1].find("1 AnimationName lists") != std::string::npos);
	CHECK(items[2].find("TimeOfDayTexture") != std::string::npos);
	CHECK(items[3].find("constructor defaults not recovered") != std::string::npos);
	CHECK(hasStop(*r.draw, "S-094")); // the draw reports the parse items at construction
	CHECK(r.draw->errors().empty());

	// bones, worked out by hand from the skeleton: ROOT (0,0,0); HIP at (0,0,1) under ROOT; HAND at (0,2,0) under HIP;
	// ARROW01 at (0,0,1) under HAND; FIREBONE at (1,0,0) under HAND. World positions: HAND (0,2,1), ARROW01 (0,2,2), FIREBONE (1,2,1).
	const W3DModelBones &b = r.draw->bones();
	REQUIRE(b.valid);
	Vector3 p;
	REQUIRE(b.findPristineBonePos("arrow01", p));
	CHECK(p.X == doctest::Approx(0.0f));
	CHECK(p.Y == doctest::Approx(2.0f));
	CHECK(p.Z == doctest::Approx(2.0f));
	int index = 0;
	REQUIRE(b.findPristineBone("firebone", &index) != nullptr);
	CHECK(index == 4);
	REQUIRE(b.findPristineBonePos("firebone", p));
	CHECK(p.X == doctest::Approx(1.0f));
	CHECK(p.Y == doctest::Approx(2.0f));
	CHECK(p.Z == doctest::Approx(1.0f));
	// the PRIMARY barrel: ZH loop: i = 1: recoil/muzzle empty; fx "firebone01" not found; launch "arrow01" found (bone 3) -> a
	// barrel with fxBone 0
	REQUIRE(b.barrels[0].size() == 1);
	CHECK(b.barrels[0][0].fxBone == 0);
	CHECK(b.barrels[0][0].projectileOffset.Get_Translation().Z == doctest::Approx(2.0f));
	CHECK(b.findPristineBone("arrow") == nullptr);
	CHECK(b.findPristineBone("arrow01") != nullptr);
}

TEST_CASE("draw runtime: isolated probes report S-095 for entry FX and model-state particles, S-090 for logged Lua events, S-094 for the constructor defaults")
{
	{
		// a plain module: the unrecovered constructor members are reported by every draw
		Rig r({ { "ABUNIT_IDLA", 31 } }, kIdleThree);
		r.random.values = { 0 };
		r.start();
		CHECK(hasStop(*r.draw, "S-094"));
		CHECK(!hasStop(*r.draw, "S-095"));
		CHECK(!hasStop(*r.draw, "S-090"));
	}
	{
		// EnteringStateFX alone (no FXEvent, no ParticleSysBone)
		const std::string text = std::string(kDefaultState) + "IdleAnimationState\n  EnteringStateFX = FX_Idle\n" + anim("ABUNIT_IDLA") + "End\nEnd\n";
		Rig r({ { "ABUNIT_IDLA", 31 } }, text);
		r.start();
		CHECK(hasStop(*r.draw, "S-095"));
		CHECK(r.draw->log().back() == "fx FX_Idle");
	}
	{
		// a ParticleSysBone of the model condition state
		const std::string text =
			"DefaultModelConditionState\n  Model = ABUNIT_SKN\n  Skeleton = ABUNIT_SKL\n  ParticleSysBone = HIP Smoke\nEnd\n"
			"IdleAnimationState\n" + anim("ABUNIT_IDLA") + "End\nEnd\n";
		Rig r({ { "ABUNIT_IDLA", 31 } }, text);
		r.start();
		CHECK(hasStop(*r.draw, "S-095"));
	}
	{
		// an OnStateEnter LuaEvent is logged on entering, not run
		const std::string text = std::string(kDefaultState) + "IdleAnimationState\n  LuaEvent = Frame: 5 Data: doIt OnStateEnter\n" + anim("ABUNIT_IDLA") + "End\nEnd\n";
		Rig r({ { "ABUNIT_IDLA", 31 } }, text);
		r.start();
		CHECK(hasStop(*r.draw, "S-090"));
		bool logged = false;
		for (const std::string &l : r.draw->log())
		{
			logged = logged || l == "lua enter doIt";
		}
		CHECK(logged);
	}
}

TEST_CASE("draw runtime: StaticSortLevelWhileFading defaults to -1 (RW 0x4C86DC) and an explicit 0 is reported under S-096")
{
	{
		Rig r({ { "ABUNIT_IDLA", 31 } }, kIdleThree);
		CHECK(r.data->m_staticSortLevelWhileFading == -1);
		r.random.values = { 0 };
		r.start();
		CHECK(!hasStop(*r.draw, "S-096")); // the default is not a use of the field
	}
	{
		const std::string text = std::string("StaticSortLevelWhileFading = 0\n") + kIdleThree;
		Rig r({ { "ABUNIT_IDLA", 31 }, { "ABUNIT_IDLB", 31 }, { "ABUNIT_IDLC", 31 } }, text);
		CHECK(r.data->m_staticSortLevelWhileFading == 0);
		r.random.values = { 0 };
		r.start();
		CHECK(hasStop(*r.draw, "S-096"));
	}
}

TEST_CASE("draw bones: the drawable scale is applied at the root, and a missing public bone is reported")
{
	MemoryFileSource fs;
	buildSoldier(fs, { { "ABUNIT_IDLA", 31 } });
	WW3DAssetManager am(fs);
	std::string err;
	const RenderObjPrototype *proto = am.Create_Render_Obj("ABUNIT_SKN", &err);
	REQUIRE_MESSAGE(proto, err);
	Harness h;
	W3DModelDrawModuleData *d = h.parse("DefaultModelConditionState\n  Model = ABUNIT_SKN\n  WeaponLaunchBone = PRIMARY ARROW\n  Turret = NoSuchBone\nEnd\nExtraPublicBone = HAND\nEnd\n");
	REQUIRE_MESSAGE(d, h.error);
	W3DModelBones bones;
	bones.build(d->m_conditionStates[0], proto, d->m_extraPublicBones, {}, 2.0f, nullptr, 0.0f);
	Vector3 p;
	REQUIRE(bones.findPristineBonePos("arrow01", p));
	CHECK(p.Y == doctest::Approx(4.0f)); // scale 2 at the root doubles (0,2,2)
	CHECK(p.Z == doctest::Approx(4.0f));
	REQUIRE(bones.findPristineBonePos("hand", p)); // the module's ExtraPublicBone
	CHECK(p.Z == doctest::Approx(2.0f));
	REQUIRE(bones.turrets.size() == 1);
	CHECK(bones.turrets[0].angleBone == 0);
	// reported twice, as ZH asserts twice: once as a public bone (every bone a state names is one) and once as the turret bone
	CHECK(std::count(bones.missingBones.begin(), bones.missingBones.end(), "nosuchbone") == 2);
	// sub objects as bones: a name that is a sub object (SWORD on HAND) is cached with its bone's transform when no bone has the name
	h.parse("DefaultModelConditionState\n  Model = ABUNIT_SKN\n  WeaponLaunchBone = PRIMARY Sword\nEnd\nEnd\n");
	W3DModelBones b2;
	b2.build(h.data->m_conditionStates[0], proto, {}, {}, 1.0f, nullptr, 0.0f);
	int idx = 0;
	REQUIRE(b2.findPristineBone("sword", &idx) != nullptr);
	CHECK(idx == 2); // the SWORD mesh hangs on HAND (bone 2)
	REQUIRE(b2.barrels[0].size() == 1);
}
