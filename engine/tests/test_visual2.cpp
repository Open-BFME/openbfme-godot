// Lane VISUAL-2: the owner's playtest bugs of 2026-10-17. The fog of war leak (particles of objects the local player cannot see, the drawable's shroud
// look), the tree sway (W3DTreeDraw trees in the script engine's breeze) and the binary facts behind them.
// The retail tests SKIP loudly when ROTWK_INSTALL / BFME2_INSTALL are unset; the binary facts need RW_GAME_DAT. GPL-3.0.

#include "CreepTestUtil.h"
#include "doctest.h"
#include "HudTestUtil.h"
#include "PeImage.h"

using retailtest::PeImage;

#include "Common/ModelState.h"
#include "GameClient/DrawableShroud.h"
#include "GameLogic/Module/AIUpdate.h"
#include "GameLogic/Damage.h"
#include "GameLogic/Module/ActiveBody.h"
#include "GameLogic/Object/Object.h"
#include "GameClient/FXList.h"
#include "GameClient/HudObjects.h"
#include "GameClient/FXPlayback.h"
#include "GameClient/LiveGameAudio.h"
#include "GameClient/LogicSnapshot.h"
#include "GameLogic/FXEvents.h"
#include "GameLogic/Module/BehaviorModule.h"
#include "GameLogic/Module/DozerAIUpdate.h"
#include "GameClient/LiveFX.h"
#include "GameClient/Visual2Stops.h"
#include "GameEngineDevice/W3DDevice/GameClient/W3DVegetationSway.h"
#include "GameLogic/ScriptEngine/ScriptEngine.h"
#include "GameLogic/System/ShroudManager.h"

#include <cmath>
#include <cstring>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace
{
// a client random whose draws are known: real(lo, hi) = lo + (hi - lo) * fraction, value(lo, hi) = a fixed pick
struct FixedRandom : public W3DDrawRandom
{
	float fraction = 0.25f;
	int pick = 7;
	int reals = 0, values = 0;
	int value(int lo, int hi) override
	{
		++values;
		return pick < lo ? lo : pick > hi ? hi : pick;
	}
	float real(float lo, float hi) override
	{
		++reals;
		return lo + (hi - lo) * fraction;
	}
};

W3DVegetationSway::Breeze retailDefaultBreeze()
{
	// the retail reset's values, written out here (RW 0x609685 reads 0xBF9F20 / 0xBF9F1C / 0xBF9F18 / 0xBDAD78 and 0xD9F608 * 10; the binary facts
	// test below reads the same words from game.dat)
	W3DVegetationSway::Breeze b;
	const double dir = 3.14159265358979323846 / 3.0;
	b.directionX = (float)std::sin(dir);
	b.directionY = (float)std::cos(dir);
	b.intensity = 0.035f * 3.14159265f / 4.0f;
	b.lean = 0.02f * 3.14159265f / 4.0f;
	b.randomness = 0.2f;
	b.breezePeriod = 50;
	b.breezeVersion = 0;
	return b;
}
} // namespace

// ---- the drawable's shroud look (RW 0x64849E, BFME2 decomp GameClientDrawableTOC.cpp:1837) ------------------------------------------------------
TEST_CASE("visual2 shroud look: an object seen clear stays unobscured for 2 s (10 logic frames) after it leaves the clear, 5 s when dead; never seen: at once")
{
	DrawableShroudLook look;
	// never clear: a fogged / shrouded status obscures at once (the clear frame is 0)
	look.update((int)OBJECTSHROUD_FOGGED, false, 100);
	CHECK(look.fullyObscured);
	CHECK_FALSE(look.hidden);
	look.update((int)OBJECTSHROUD_SHROUDED, false, 101);
	CHECK(look.fullyObscured);
	CHECK(look.hidden);
	CHECK(look.clearFrame == 0u);
	// seen clear at frame 200: the clear frame is stored, nothing obscured
	look.update((int)OBJECTSHROUD_CLEAR, false, 200);
	CHECK(look.clearFrame == 200u);
	CHECK_FALSE(look.fullyObscured);
	CHECK_FALSE(look.hidden);
	// PARTIAL_CLEAR is not >= FOGGED and does not store the frame
	look.update((int)OBJECTSHROUD_PARTIAL_CLEAR, false, 201);
	CHECK(look.clearFrame == 200u);
	CHECK_FALSE(look.fullyObscured);
	// shrouded from frame 202: counted clear while frame < 200 + 2 * 5
	for (UnsignedInt f = 202; f < 210; ++f)
	{
		look.update((int)OBJECTSHROUD_SHROUDED, false, f);
		CHECK_FALSE(look.fullyObscured);
		CHECK_FALSE(look.hidden);
	}
	look.update((int)OBJECTSHROUD_SHROUDED, false, 210);
	CHECK(look.fullyObscured);
	CHECK(look.hidden);
	// an effectively dead object: 2 s + 3 s
	DrawableShroudLook dead;
	dead.update((int)OBJECTSHROUD_CLEAR, true, 300);
	dead.update((int)OBJECTSHROUD_FOGGED, true, 324);
	CHECK_FALSE(dead.fullyObscured);
	dead.update((int)OBJECTSHROUD_FOGGED, true, 325);
	CHECK(dead.fullyObscured);
	CHECK_FALSE(dead.hidden); // fogged: the device layer stands the drawable in for the ghost (S-561)
	// OBJECTSHROUD_INVALID (no shroud displayed): never obscured, the clear frame untouched (no record / ALWAYS_VISIBLE answer CLEAR, RW 0x68D8F7)
	DrawableShroudLook none;
	none.update(0, false, 50);
	CHECK_FALSE(none.fullyObscured);
	CHECK_FALSE(none.hidden);
	CHECK(none.clearFrame == 0u);
}

TEST_CASE("visual2 shroud look: another viewer (a caster's POV switch) starts without the grace; the same viewer keeps it")
{
	const unsigned long long a = DrawableShroudLook::viewerKeyOf(0, { 0 });
	const unsigned long long b = DrawableShroudLook::viewerKeyOf(1, { 1 });
	const unsigned long long team = DrawableShroudLook::viewerKeyOf(0, { 0, 2 });
	CHECK(a != b);
	CHECK(a != team);
	CHECK(DrawableShroudLook::viewerKeyOf(-2, {}) != DrawableShroudLook::viewerKeyOf(-1, {}));
	DrawableShroudLook look;
	look.update((int)OBJECTSHROUD_CLEAR, false, 100, a);
	look.update((int)OBJECTSHROUD_SHROUDED, false, 101, a); // the same viewer: in the grace
	CHECK_FALSE(look.hidden);
	look.update((int)OBJECTSHROUD_SHROUDED, false, 102, b); // the POV switched: hidden at once
	CHECK(look.hidden);
	CHECK(look.fullyObscured);
	CHECK(look.clearFrame == 0u);
	look.update((int)OBJECTSHROUD_SHROUDED, false, 103, a); // back: the old clear frame is gone too
	CHECK(look.hidden);
}

// ---- the vegetation sway (RW 0x4E5D2C / 0x4E61EE; vegetation.fxo blob 5) ---------------------------------------------------------------------------
TEST_CASE("visual2 sway: the first update builds the 100-entry table, the ten steps and factors from the breeze (expected values from the retail formula)")
{
	const W3DVegetationSway::Breeze b = retailDefaultBreeze();
	FixedRandom r;
	W3DVegetationSway sway; // the retail rate: 30 client frames / 5 logic frames
	CHECK(sway.version() == -1);
	sway.update(b, r, 1.0);
	CHECK(sway.version() == 0);
	CHECK(r.reals == 20); // per type: the step's draw, then the factor's
	// the draws span 1 -/+ randomness / 2 = [0.9, 1.1]; fraction 0.25 gives 0.95
	const double draw = 0.9 + 0.2 * 0.25;
	const double step = 100.0 / (50.0 * 6.0) * draw; // a cycle of 100 entries takes period * rate = 300 client frames = 10 s at 30 per second
	for (int t = 0; t < W3DVegetationSway::MAX_SWAY_TYPES; ++t)
	{
		CHECK(sway.step(t) == doctest::Approx(step).epsilon(1e-6)); // tolerance: float32 code vs the double re-derivation of the retail formula
		CHECK(sway.factor(t) == doctest::Approx(draw).epsilon(1e-6)); // tolerance: float32 code vs the double re-derivation of the retail formula
		CHECK(sway.offset(t) == doctest::Approx(step).epsilon(1e-6)); // tolerance: float32 code vs the double re-derivation of the retail formula;  offset 0, then one advance
	}
	for (int i : { 0, 1, 25, 50, 99 })
	{
		const double angle = (double)b.lean + (double)b.intensity * std::cos(i * 2.0 * 3.14159265358979323846 / 101.0);
		CHECK(sway.tableEntry(i).x == doctest::Approx(b.directionX * std::sin(angle)).epsilon(1e-5)); // tolerance: float32 code vs the double re-derivation of the retail formula
		CHECK(sway.tableEntry(i).y == doctest::Approx(b.directionY * std::sin(angle)).epsilon(1e-5)); // tolerance: float32 code vs the double re-derivation of the retail formula
		CHECK(sway.tableEntry(i).z == doctest::Approx(std::cos(angle) - 1.0).epsilon(1e-4)); // tolerance: float32 code vs the double re-derivation of the retail formula
	}
	// the same version again: no rebuild, no draw
	sway.update(b, r, 1.0);
	CHECK(r.reals == 20);
	// a new version (SET_TREE_SWAY): rebuilt, offsets back to 0 then one step
	W3DVegetationSway::Breeze b2 = b;
	b2.breezeVersion = 1;
	b2.breezePeriod = 20;
	sway.update(b2, r, 1.0);
	CHECK(r.reals == 40);
	CHECK(sway.offset(3) == doctest::Approx(100.0 / (20.0 * 6.0) * draw).epsilon(1e-6)); // tolerance: float32 code vs the double re-derivation of the retail formula
}

TEST_CASE("visual2 sway: smooth presentation closes the loop without an upright snap")
{
	auto b = retailDefaultBreeze();
	b.randomness = 0.0f;
	b.breezePeriod = 1; // step exactly 1 with the supplied rate
	FixedRandom random;
	W3DVegetationSway sway(100.0f);
	sway.update(b, random, 99.0);
	W3DVegetationSway::Vec3 out[W3DVegetationSway::MAX_SWAY_TYPES + 1];
	sway.swayOffsets(out); // preserve the raw retail gap
	CHECK(out[1].x == 0.0f);
	sway.update(b, random, 0.5);
	sway.swayOffsets(out, true);
	for (int t = 1; t <= W3DVegetationSway::MAX_SWAY_TYPES; ++t)
	{
		CHECK(out[t].x == ((sway.tableEntry(99).x + sway.tableEntry(0).x) * 0.5f));
		CHECK(out[t].y == ((sway.tableEntry(99).y + sway.tableEntry(0).y) * 0.5f));
		CHECK(out[t].z == ((sway.tableEntry(99).z + sway.tableEntry(0).z) * 0.5f));
	}
	CHECK(out[0].x == 0.0f);
	sway.update(b, random, 0.5); // phase exactly 100 also closes cleanly
	sway.swayOffsets(out, true);
	CHECK(out[1].x == sway.tableEntry(0).x);
	sway.update(b, random, 0.5);
	sway.swayOffsets(out, true);
	CHECK(out[1].x == ((sway.tableEntry(0).x + sway.tableEntry(1).x) * 0.5f));
}

TEST_CASE("visual2 sway: the offsets wrap above 100, the SwayOffsets lerp two table entries times the factor, entry 0 is no sway")
{
	W3DVegetationSway::Breeze b = retailDefaultBreeze();
	b.randomness = 0.0f; // every draw is 1: step 1 / 3, factor 1
	FixedRandom r;
	W3DVegetationSway sway;
	sway.advanceSeconds(b, r, 5.0); // 150 retail client frames
	CHECK(sway.offset(0) == doctest::Approx(50.0).epsilon(1e-4)); // tolerance: float32 code vs the double re-derivation of the retail formula
	W3DVegetationSway::Vec3 out[W3DVegetationSway::MAX_SWAY_TYPES + 1];
	sway.swayOffsets(out);
	CHECK(out[0].x == 0.0f);
	CHECK(out[0].y == 0.0f);
	CHECK(out[0].z == 0.0f);
	// at 50 (+ a rounding residue) the lerp is entry 50 within the residue
	CHECK(out[1].x == doctest::Approx(sway.tableEntry(50).x).epsilon(1e-3)); // tolerance: float32 code vs the double re-derivation of the retail formula
	// a quarter way between two entries
	W3DVegetationSway q;
	FixedRandom r2;
	W3DVegetationSway::Breeze bq = b;
	bq.breezePeriod = 4000; // step 100 / 24000 per client frame
	q.update(bq, r2, 1.0);
	const float off = q.offset(0);
	const int lo = (int)std::floor(off);
	const float f2 = off - (float)lo;
	q.swayOffsets(out);
	CHECK(out[1].x == doctest::Approx(q.tableEntry(lo).x * (1.0f - f2) + q.tableEntry(lo + 1).x * f2).epsilon(1e-6)); // tolerance: float32 code vs the double re-derivation of the retail formula
	// the wrap: 10 s of retail client frames (300) bring the offset round once (100 entries), not 99 (Zero Hour's wrap)
	W3DVegetationSway w;
	FixedRandom r3;
	w.advanceSeconds(b, r3, 10.0);
	CHECK(std::fabs(w.offset(0)) < 0.01f); // tolerance: float32 code vs the double re-derivation of the retail formula
	w.advanceSeconds(b, r3, 1.0); // 30 frames: 10 entries further
	CHECK(w.offset(0) == doctest::Approx(10.0).epsilon(1e-3)); // tolerance: float32 code vs the double re-derivation of the retail formula
	// a fractional frame advances by its fraction (144 fps: smooth, the same rate)
	W3DVegetationSway fr;
	FixedRandom r4;
	fr.update(b, r4, 0.5);
	CHECK(fr.offset(0) == doctest::Approx(100.0 / 300.0 * 0.5).epsilon(1e-5)); // tolerance: float32 code vs the double re-derivation of the retail formula
	// the tree's sway type: TaintedTree none, else GameClientRandomValue(1, 10)
	FixedRandom r5;
	CHECK(W3DVegetationSway::treeSwayType(true, r5) == 0);
	CHECK(r5.values == 0);
	CHECK(W3DVegetationSway::treeSwayType(false, r5) == 7);
	CHECK(r5.values == 1);
}

// ---- binary facts --------------------------------------------------------------------------------------------------------------------------------
namespace
{
float f32At(const PeImage &pe, std::uint32_t va)
{
	std::vector<std::uint8_t> b;
	REQUIRE(pe.read(va, 4, &b));
	float f;
	std::memcpy(&f, b.data(), 4);
	return f;
}
std::int32_t i32At(const PeImage &pe, std::uint32_t va)
{
	std::vector<std::uint8_t> b;
	REQUIRE(pe.read(va, 4, &b));
	std::int32_t v;
	std::memcpy(&v, b.data(), 4);
	return v;
}
std::string cstrAt(const PeImage &pe, std::uint32_t va)
{
	std::string s;
	std::vector<std::uint8_t> b;
	while (pe.read(va++, 1, &b) && b[0] != 0 && s.size() < 64)
	{
		s.push_back((char)b[0]);
	}
	return s;
}
} // namespace

TEST_CASE("visual2 binary facts (RW_GAME_DAT): the breeze reset, the sway rate, the tree's sway type, the shroud gates of the particles and the drawable")
{
	const PeImage *pe = PeImage::fromEnvironment();
	if (!pe)
	{
		retailtest::printSkip("visual2 binary facts (RW_GAME_DAT unset)");
		return;
	}
	auto bytes = [&](std::uint32_t va, size_t n) {
		std::vector<std::uint8_t> out;
		REQUIRE(pe->read(va, n, &out));
		return out;
	};
	// RW 0x609685 (ScriptEngine::reset) the breeze: direction 0xBF9F20, intensity 0xBF9F1C, lean 0xBF9F18, randomness 0xBDAD78, period = [0xD9F608] * 10
	CHECK(f32At(*pe, 0xBF9F20) == 1.0471975803375244f);
	CHECK(f32At(*pe, 0xBF9F1C) == 0.027488935738801956f);
	CHECK(f32At(*pe, 0xBF9F18) == 0.015707964077591896f);
	CHECK(f32At(*pe, 0xBDAD78) == 0.2f);
	CHECK(i32At(*pe, 0xD9F608) == 5);
	CHECK(i32At(*pe, 0xD9F60C) == W3DVegetationSway::kClientFramesPerSecond);
	CHECK(bytes(0x6097C9, 6) == std::vector<std::uint8_t>{ 0xD9, 0x05, 0x20, 0x9F, 0xBF, 0x00 });        // fld [0xBF9F20]
	CHECK(bytes(0x60982D, 3) == std::vector<std::uint8_t>{ 0x6B, 0xC0, 0x0A });                          // imul eax, eax, 10
	CHECK(bytes(0x609834, 7) == std::vector<std::uint8_t>{ 0x66, 0x89, 0x86, 0xC0, 0xA5, 0x01, 0x00 });  // mov [esi + 0x1A5C0], ax (the period)
	// RW 0x6031A9 (setSway): the version goes up first
	CHECK(bytes(0x6031B3, 7) == std::vector<std::uint8_t>{ 0x66, 0xFF, 0x86, 0xC2, 0xA5, 0x01, 0x00 });
	// RW 0x4ED566: the tree buffer's update hands TheScriptEngine + 0x1A5A8 to the sway update RW 0x4E5D2C
	CHECK(bytes(0x4ED56B, 5) == std::vector<std::uint8_t>{ 0x05, 0xA8, 0xA5, 0x01, 0x00 });
	CHECK(bytes(0x4ED573, 5) == std::vector<std::uint8_t>{ 0xE8, 0xB4, 0x87, 0xFF, 0xFF });
	// RW 0x4E5F40 / 0x4E648F: the sway rate is (float) TheGameEngine + 0x38 (client frames per logic frame: 30 / 5)
	CHECK(bytes(0x4E5F40, 5) == std::vector<std::uint8_t>{ 0xF3, 0x0F, 0x2A, 0x40, 0x38 });
	CHECK(W3DVegetationSway::kRetailSwayRate == (float)(W3DVegetationSway::kClientFramesPerSecond / i32At(*pe, 0xD9F608)));
	// RW 0x4E61EE: the SwayOffsets setter clears 11 vec4 (0xB0 bytes) and reads ten offsets at + 0x544
	CHECK(bytes(0x4E61FA, 5) == std::vector<std::uint8_t>{ 0x68, 0xB0, 0x00, 0x00, 0x00 });
	CHECK(bytes(0x4E6226, 7) == std::vector<std::uint8_t>{ 0xC7, 0x45, 0x6C, 0x0A, 0x00, 0x00, 0x00 });
	// RW 0x4ECC53: addTree's sway type is 0 when W3DTreeDraw data + 0x54 is set, else GameClientRandomValue(1, 10)
	CHECK(bytes(0x4ECC53, 4) == std::vector<std::uint8_t>{ 0x80, 0x7B, 0x54, 0x00 });
	CHECK(bytes(0x4ECC67, 4) == std::vector<std::uint8_t>{ 0x6A, 0x0A, 0x6A, 0x01 });
	// + 0x54 is TaintedTree in the W3DTreeDraw FieldParse table (RW 0xBE2E80, row 20)
	{
		std::vector<std::uint8_t> row = bytes(0xBE2E80 + 20 * 16, 16);
		std::uint32_t name, offset;
		std::memcpy(&name, row.data(), 4);
		std::memcpy(&offset, row.data() + 12, 4);
		CHECK(cstrAt(*pe, name) == "TaintedTree");
		CHECK(offset == 0x54u);
	}
	// RW 0x678F54 (Drawable::setFullyObscuredByShroud): the byte + 0x440
	CHECK(bytes(0x678F81, 6) == std::vector<std::uint8_t>{ 0x88, 0x9F, 0x40, 0x04, 0x00, 0x00 });
}

// ---- retail: the breeze on a live game -------------------------------------------------------------------------------------------------------------
TEST_CASE("visual2 retail: a game starts with the retail breeze; SET_TREE_SWAY's values replace it, raise the period to 1 and bump the version")
{
	if (!hudtest::haveWorld("visual2 retail breeze"))
	{
		return;
	}
	hudtest::Rig rig(hudtest::shared());
	ScriptEngine &se = rig.logic().scriptEngine();
	const W3DVegetationSway::Breeze want = retailDefaultBreeze();
	const ScriptEngine::BreezeInfo &b = se.getBreezeInfo();
	CHECK(b.direction == 1.0471975803375244f);
	CHECK(b.directionX == doctest::Approx(want.directionX).epsilon(1e-6)); // tolerance: float32 code vs the double re-derivation of the retail formula
	CHECK(b.directionY == doctest::Approx(want.directionY).epsilon(1e-6)); // tolerance: float32 code vs the double re-derivation of the retail formula
	CHECK(b.intensity == 0.027488935738801956f);
	CHECK(b.lean == 0.015707964077591896f);
	CHECK(b.randomness == 0.2f);
	CHECK(b.breezePeriod == 50);
	CHECK(b.breezeVersion == 0);
	// the presented snapshot carries it to the tree draw
	rig.frame(2);
	const std::shared_ptr<const LogicSnapshot> snap = rig.game->presentedSnapshot();
	REQUIRE(snap != nullptr);
	CHECK(snap->breeze.breezePeriod == 50);
	CHECK(snap->breeze.intensity == b.intensity);
	se.setSway(0.5f, 0.1f, 0.05f, 0, 0.4f);
	CHECK(b.breezeVersion == 1);
	CHECK(b.direction == 0.5f);
	CHECK(b.directionX == doctest::Approx(std::sin(0.5)).epsilon(1e-6)); // tolerance: float32 code vs the double re-derivation of the retail formula
	CHECK(b.directionY == doctest::Approx(std::cos(0.5)).epsilon(1e-6)); // tolerance: float32 code vs the double re-derivation of the retail formula
	CHECK(b.intensity == 0.1f);
	CHECK(b.lean == 0.05f);
	CHECK(b.breezePeriod == 1);
	CHECK(b.randomness == 0.4f);
	// a new game resets it (RW 0x609685)
	se.reset();
	CHECK(se.getBreezeInfo().breezeVersion == 0);
	CHECK(se.getBreezeInfo().breezePeriod == 50);
}

// ---- retail: particles in the shroud ---------------------------------------------------------------------------------------------------------------
namespace
{
// how the local player sees the far corner: never, with a permanent look at the whole map, or seen (the permanent look) and then lost (r2)
enum class Sight
{
	Never,
	Always,
	SeenThenLost
};

struct BurnRun
{
	size_t systemsNear = 0;       ///< live particle systems within 300 of the structure at the end
	size_t particlesNear = 0;     ///< their particles, summed over the frames
	int statusBefore = -1;        ///< the structure's status for the local player when it burns
	int statusAfter = -1;         ///< ... at the end
	bool shroudDisplayed = false;
	std::vector<size_t> perFrame; ///< r2: the particles near it after each measured frame
	std::vector<int> obscured;    ///< r2: its drawable's fully obscured flag (RW + 0x440) after each measured frame (-1: no drawable)
};

// the extent's corner furthest from the local base (the average of the local player's objects), 500 inside
Coord3D farCorner(hudtest::Rig &rig, const ShroudManager &sm)
{
	double bx = 0.0, by = 0.0;
	int n = 0;
	for (Object *o = rig.logic().getFirstObject(); o; o = o->getNextObject())
	{
		if (o->getControllingPlayer() == rig.local)
		{
			bx += o->getPosition()->x;
			by += o->getPosition()->y;
			++n;
		}
	}
	REQUIRE(n > 0); // tolerance: a local player always owns objects at the start; the count itself is not the claim
	bx /= n;
	by /= n;
	const float loX = sm.originX(), loY = sm.originY();
	const float hiX = loX + sm.cellSize() * (float)sm.cellCountX(), hiY = loY + sm.cellSize() * (float)sm.cellCountY();
	return Coord3D{ bx < (loX + hiX) * 0.5 ? hiX - 500.0f : loX + 500.0f, by < (loY + hiY) * 0.5 ? hiY - 500.0f : loY + 500.0f, 0.0f };
}

void countNear(FXPlayback &playback, const Coord3D &at, size_t &systems, size_t &particles)
{
	systems = 0;
	particles = 0;
	playback.particles().forEachSystem([&](const FXParticleSystem::ParticleSystem &ps) {
		const float dx = ps.position().x - at.x, dy = ps.position().y - at.y;
		if (!ps.isDestroyed() && dx * dx + dy * dy < 300.0f * 300.0f)
		{
			++systems;
			particles += ps.particleCount();
		}
	});
}

// one logic frame of the live effect player: the frame, the captured events played, the attached systems moved, six client steps
void fxFrame(hudtest::Rig &rig, LiveFX &fx, FXPlayback &playback)
{
	rig.frame(1);
	fx.flushPending();
	fx.updateAttachedSystems();
	for (int k = 0; k < 6; ++k)
	{
		playback.step();
	}
}

// a MordorOrcPit of `owner` at the far corner of the map from the local base, damaged to 40 % (its damaged state's fires and smoke), for `frames` logic
// frames with the live effect player; `sight`: how the local player sees it (SeenThenLost: 10 frames with the look, then the look taken away and
// `frames` measured frames)
BurnRun burningPit(Sight sight, int frames, const char *owner = "Player_2", const char *allyFaction = nullptr)
{
	hudtest::SharedWorld &s = hudtest::shared();
	BurnRun out;
	hudtest::Rig rig(s, "map mp fall back 4p", "FactionMen", allyFaction);
	auto playback = std::make_unique<FXPlayback>(*s.mount->fs, RandomAlgorithm::RotWK_GameDat_LCG);
	REQUIRE(playback->loadRetailData().empty());
	auto fx = std::make_unique<LiveFX>(*rig.game, *playback);
	const auto context = s.world->enterContext();
	creeptest::removeCreeps(rig.logic());
	ShroudManager *sm = rig.logic().shroud();
	REQUIRE(sm != nullptr);
	sm->setDisplayed(true); // as a game started from a game setup shows it (VIS-1: RW 0x62F91A); the test rig loads the map without
	out.shroudDisplayed = sm->displayed();
	const int me = rig.index();
	const Coord3D at = farCorner(rig, *sm);
	if (sight != Sight::Never)
	{
		sm->revealMapForPlayerPermanently(me);
	}
	const Player *ownerPlayer = rig.game->players().findPlayerWithName(owner);
	REQUIRE(ownerPlayer != nullptr);
	std::string err;
	Object *pit = rig.game->createObject("MordorOrcPit", ownerPlayer->getPlayerIndex(), at, 0.0f, &err);
	REQUIRE_MESSAGE(pit != nullptr, err);
	const ObjectID pid = pit->getID();
	rig.frame(2);
	pit = rig.logic().findObjectByID(pid);
	REQUIRE(pit != nullptr);
	{
		DamageInfo d;
		d.m_input.m_amount = pit->getBodyModule()->getMaxHealth() * 0.6f;
		d.m_input.m_damageType = DAMAGE_UNRESISTABLE;
		pit->attemptDamage(d);
	}
	out.statusBefore = (int)sm->clientObjectStatus(*pit, me);
	if (sight == Sight::SeenThenLost)
	{
		for (int i = 0; i < 10; ++i)
		{
			fxFrame(rig, *fx, *playback);
		}
		sm->undoRevealMapForPlayerPermanently(me);
	}
	for (int i = 0; i < frames; ++i)
	{
		fxFrame(rig, *fx, *playback);
		size_t systems = 0, particles = 0;
		countNear(*playback, at, systems, particles);
		out.particlesNear += particles;
		out.systemsNear = systems;
		out.perFrame.push_back(particles);
		const Drawable *d = rig.game->drawables().findByObject(pid);
		out.obscured.push_back(d ? (d->shroudLook().fullyObscured ? 1 : 0) : -1);
	}
	out.statusAfter = (int)sm->clientObjectStatus(*rig.logic().findObjectByID(pid), me);
	return out;
}
} // namespace

TEST_CASE("visual2 retail: an enemy structure burning in the shroud emits no particles (RW 0x5FA2CE, 0x5E2282); seen, the same fires emit")
{
	if (!hudtest::haveWorld("visual2 retail particles in the shroud"))
	{
		return;
	}
	const BurnRun hidden = burningPit(Sight::Never, 40);
	REQUIRE(hidden.shroudDisplayed);
	CHECK(hidden.statusBefore == (int)OBJECTSHROUD_SHROUDED);
	CHECK(hidden.systemsNear > 0);    // tolerance: presence of emission / systems is the claim; their counts belong to the FX-1 particle simulator;  the damaged state's fires exist (their drawable is there) ...
	CHECK(hidden.particlesNear == 0u); // ... and emit nothing while the local player cannot see the pit
	const BurnRun visible = burningPit(Sight::Always, 40);
	CHECK(visible.statusBefore == (int)OBJECTSHROUD_CLEAR);
	CHECK(visible.systemsNear > 0); // tolerance: presence of emission / systems is the claim; their counts belong to the FX-1 particle simulator
	CHECK(visible.particlesNear > 0u); // tolerance: presence of emission / systems is the claim; their counts belong to the FX-1 particle simulator
	MESSAGE("visual2: shrouded pit systems " << hidden.systemsNear << " particles " << hidden.particlesNear << "; seen pit systems " << visible.systemsNear
		<< " particles " << visible.particlesNear);
}

// ---- r2: the review's fog leaks ------------------------------------------------------------------------------------------------------------------
TEST_CASE("visual2 r2 retail: a structure seen and then fogged keeps emitting for the 2 s grace, then stops (RW 0x64849E, 0x5FA2CE); kept in sight, it emits on")
{
	if (!hudtest::haveWorld("visual2 r2 retail seen then fogged"))
	{
		return;
	}
	const BurnRun lost = burningPit(Sight::SeenThenLost, 60);
	REQUIRE(lost.shroudDisplayed);
	CHECK(lost.statusBefore == (int)OBJECTSHROUD_CLEAR);
	CHECK(lost.statusAfter == (int)OBJECTSHROUD_FOGGED); // seen, then out of sight: the last-seen (ghost) status
	REQUIRE(lost.obscured.size() == 60u);
	// the grace: 2 s = 10 logic frames after the last CLEAR status (the drawable reads the presented frame, which may be one behind the logic)
	for (size_t i = 0; i < 9; ++i)
	{
		CHECK_MESSAGE(lost.obscured[i] == 0, "frame " << i << " after the loss");
	}
	for (size_t i = 12; i < lost.obscured.size(); ++i)
	{
		CHECK_MESSAGE(lost.obscured[i] == 1, "frame " << i << " after the loss");
	}
	CHECK(lost.perFrame.front() > 0u); // tolerance: presence of particles is the claim (emitting during the grace)
	CHECK(lost.systemsNear > 0u);      // tolerance: presence is the claim: the damaged state's systems are still there ...
	CHECK(lost.perFrame.back() == 0u); // ... and emit nothing once fully obscured: the last particles have lived out their lifetime
	const BurnRun kept = burningPit(Sight::Always, 70);
	CHECK(kept.perFrame.back() > 0u); // tolerance: presence is the claim: the same fires in sight emit to the end
	MESSAGE("visual2 r2: seen then fogged, particles per frame first " << lost.perFrame.front() << " last " << lost.perFrame.back() << "; kept in sight last "
		<< kept.perFrame.back());
}

TEST_CASE("visual2 r2 retail: own and allied structures burning where the local player has no unit of its own still emit (shared vision)")
{
	if (!hudtest::haveWorld("visual2 r2 retail own and allied effects"))
	{
		return;
	}
	const BurnRun own = burningPit(Sight::Never, 20, "Player_1");
	CHECK(own.statusBefore == (int)OBJECTSHROUD_CLEAR);
	CHECK(own.perFrame.back() > 0u); // tolerance: presence of particles is the claim
	const BurnRun ally = burningPit(Sight::Never, 20, "Player_3", "FactionElves");
	CHECK(ally.statusBefore == (int)OBJECTSHROUD_CLEAR); // the ally's sight is the local player's (ShroudManager's shared vision)
	CHECK(ally.perFrame.back() > 0u); // tolerance: presence of particles is the claim
	const BurnRun enemy = burningPit(Sight::Never, 20, "Player_2", "FactionElves"); // control: the same rig, an enemy's pit
	CHECK(enemy.statusBefore == (int)OBJECTSHROUD_SHROUDED);
	CHECK(enemy.particlesNear == 0u);
}

namespace
{
// the FXLists (definition order) that are one plain ParticleSystem nugget: no PlayEvenIfShrouded, no culling, no filter, no model conditions, no weather
std::vector<const FXList *> plainParticleLists(FXPlayback &pb, size_t max)
{
	std::vector<const FXList *> out;
	for (const std::string &n : pb.fxLists().listNames())
	{
		const FXList *l = pb.fxLists().findFXList(n);
		if (!l || l->playEvenIfShrouded() || l->cullWindowFrames() != 0 || l->nuggets().size() != 1)
		{
			continue;
		}
		const FXNugget &g = *l->nuggets()[0];
		if (g.m_type != FX_NUGGET_PARTICLE_SYSTEM || g.m_sourceObjectFilter || g.m_objectFilter || g.m_requiredSourceModelConditions.any() ||
			g.m_excludedSourceModelConditions.any() || g.m_requiredSecondaryModelConditions.any() || g.m_excludedSecondaryModelConditions.any() || g.m_weather != 2)
		{
			continue;
		}
		out.push_back(l);
		if (out.size() >= max)
		{
			break;
		}
	}
	return out;
}

std::set<FXParticleSystem::ParticleSystemID> liveSystemIds(FXPlayback &pb)
{
	std::set<FXParticleSystem::ParticleSystemID> ids;
	pb.particles().forEachSystem([&](const FXParticleSystem::ParticleSystem &ps) {
		if (!ps.isDestroyed())
		{
			ids.insert(ps.id());
		}
	});
	return ids;
}
} // namespace

TEST_CASE("visual2 r2 retail: an enemy garrisoned in the shroud keeps its shroud record (RW 0x68C18F, PORTERFOG-1), plays no object effect (RW 0x5E2282: "
		  "its status is SHROUDED) and its systems are hidden (RW 0x5FA6E9); seen, the effect plays")
{
	if (!hudtest::haveWorld("visual2 r2 retail garrisoned enemy in the shroud"))
	{
		return;
	}
	hudtest::SharedWorld &s = hudtest::shared();
	hudtest::Rig rig(s);
	auto playback = std::make_unique<FXPlayback>(*s.mount->fs, RandomAlgorithm::RotWK_GameDat_LCG);
	REQUIRE(playback->loadRetailData().empty());
	auto fx = std::make_unique<LiveFX>(*rig.game, *playback);
	const auto context = s.world->enterContext();
	creeptest::removeCreeps(rig.logic());
	ShroudManager *sm = rig.logic().shroud();
	REQUIRE(sm != nullptr);
	sm->setDisplayed(true);
	const int me = rig.index();
	const Coord3D at = farCorner(rig, *sm);
	const int enemy = rig.game->players().findPlayerWithName("Player_2")->getPlayerIndex();
	std::string err;
	Object *keep = rig.game->createObject("GondorKeep", enemy, at, 0.0f, &err);
	REQUIRE_MESSAGE(keep != nullptr, err);
	Object *man = rig.game->createObject("GondorFighter", enemy, Coord3D{ at.x + 150.0f, at.y, 0.0f }, 0.0f, &err);
	REQUIRE_MESSAGE(man != nullptr, err);
	const ObjectID kid = keep->getID(), mid = man->getID();
	rig.frame(2);
	keep = rig.logic().findObjectByID(kid);
	man = rig.logic().findObjectByID(mid);
	REQUIRE((keep && man && keep->getContain() && keep->getContain()->isGarrisonable()));
	REQUIRE(keep->getContain()->addToContain(man));
	rig.frame(2);
	man = rig.logic().findObjectByID(mid);
	REQUIRE(man != nullptr);
	REQUIRE(man->getContainedBy() == rig.logic().findObjectByID(kid));
	// RW 0x68C18F (the contain's world exit) keeps the record (Object + 0x4E4) and only marks it dirty, forced (RW 0xB4E2A0); the record goes in the destructor
	// (RW 0x69A8BC). So the garrisoned enemy presents its own status: its cells are never seen, SHROUDED
	CHECK(sm->hasRecord(*man));
	{
		const std::shared_ptr<const LogicSnapshot> snap = rig.game->presentedSnapshot();
		REQUIRE(snap);
		const ObjectSnapshot *rec = snap->find(mid);
		REQUIRE(rec != nullptr);
		CHECK(rec->objectShroud == (int)OBJECTSHROUD_SHROUDED);
		CHECK(rec->shroudRecord);
		CHECK(rec->drawableHidden); // the keep encloses it (RW 0x6718FB)
	}
	const std::vector<const FXList *> lists = plainParticleLists(*playback, 40);
	REQUIRE(!lists.empty());
	// list `l` played on the garrisoned man as the logic's OBJECT_FX event: the particle systems it created (new ids after the play and one client step)
	auto playOnMan = [&](const FXList *l) {
		fx->flushPending(); // what the frames captured
		const std::set<FXParticleSystem::ParticleSystemID> before = liveSystemIds(*playback);
		Object *o = rig.logic().findObjectByID(mid);
		REQUIRE(o != nullptr);
		FXEvent e;
		e.kind = FXEvent::OBJECT_FX;
		e.site = "visual2 r2 test";
		e.frame = rig.logic().getFrame();
		e.fxList = &l->name();
		e.primary = mid;
		e.position = *o->getPosition();
		fx->onFXEvent(e);
		fx->flushPending();
		playback->step();
		size_t created = 0;
		for (FXParticleSystem::ParticleSystemID id : liveSystemIds(*playback))
		{
			created += before.count(id) ? 0 : 1;
		}
		return created;
	};
	// in the shroud: no list creates anything on the garrisoned enemy; its systems would not emit (its drawable is not drawn)
	std::vector<size_t> inShroud;
	for (const FXList *l : lists)
	{
		inShroud.push_back(playOnMan(l));
	}
	const Drawable *md = rig.game->drawables().findByObject(mid);
	REQUIRE(md != nullptr);
	CHECK(fx->attachedObject(mid, "", me).hidden);
	CHECK(fx->attachedDrawable(md->getID(), "").hidden);
	// seen: the first of these lists that creates a system plays (the cell at its position is clear); the contained man is still not drawn, the keep is
	sm->revealMapForPlayerPermanently(me);
	rig.frame(2);
	size_t pick = lists.size();
	for (size_t i = 0; i < lists.size() && pick == lists.size(); ++i)
	{
		if (playOnMan(lists[i]) > 0)
		{
			pick = i;
		}
	}
	REQUIRE_MESSAGE(pick < lists.size(), "none of the " << lists.size() << " plain ParticleSystem lists created a system on a seen object"); // tolerance: an index is found (the search fails closed); which list it is belongs to the retail data
	CHECK(inShroud[pick] == 0u); // the same list in the shroud created nothing
	CHECK(fx->attachedObject(mid, "", me).hidden);
	CHECK_FALSE(fx->attachedObject(kid, "", me).hidden);
	CHECK_FALSE(fx->attachedObject(kid, "", me).shrouded);
	// in the fog (seen, then out of sight): FOGGED cells are not CLEAR either
	sm->undoRevealMapForPlayerPermanently(me);
	rig.frame(2);
	CHECK(playOnMan(lists[pick]) == 0u);
	MESSAGE("visual2 r2: garrison test FXList " << lists[pick]->name() << " (candidate " << pick << " of " << lists.size() << ")");
}

TEST_CASE("porterfog1 retail: an enemy porter building in the shroud keeps its shroud record (RW 0x68C18F) and presents SHROUDED; its drawable fades out "
		  "over BuilderFadeOutTime and is hidden (RW 0x88DDC1 -> 0x670A50, RW 0x675B7E); when it comes out it fades in (RW 0x88D6CE -> 0x670AA2)")
{
	if (!hudtest::haveWorld("porterfog1 retail porter in the shroud"))
	{
		return;
	}
	hudtest::SharedWorld &s = hudtest::shared();
	hudtest::Rig rig(s);
	const auto context = s.world->enterContext();
	creeptest::removeCreeps(rig.logic());
	ShroudManager *sm = rig.logic().shroud();
	REQUIRE(sm != nullptr);
	sm->setDisplayed(true);
	REQUIRE(rig.logic().settings().builderFadeLoaded);
	const Coord3D at = farCorner(rig, *sm);
	Player *enemy = rig.game->players().findPlayerWithName("Player_2");
	REQUIRE(enemy != nullptr);
	enemy->depositMoney(5000);
	Object *porter = rig.make("IsengardPorter", at.x, at.y, enemy);
	REQUIRE(porter != nullptr);
	const ObjectID pid = porter->getID();
	DozerAIUpdate *dozer = dynamic_cast<DozerAIUpdate *>(porter->getAIUpdateInterface());
	REQUIRE(dozer != nullptr);
	const ThingTemplate *furnace = rig.logic().things().findTemplate("IsengardFurnace")->getFinalOverride();
	Coord3D site;
	bool found = false;
	for (float r = 120.0f; r <= 400.0f && !found; r += 40.0f)
	{
		for (int k = 0; k < 16 && !found; ++k)
		{
			Coord3D c{ at.x + (float)(r * std::cos(k * 0.39269908169872414)), at.y + (float)(r * std::sin(k * 0.39269908169872414)), 0.0f };
			c.z = rig.logic().getGroundHeight(c.x, c.y);
			if (dozer->siteIsLegal(*furnace, c, 0.0f, *enemy))
			{
				site = c;
				found = true;
			}
		}
	}
	REQUIRE(found);
	REQUIRE(dozer->construct(*furnace, site, 0.0f, *enemy) != nullptr);
	bool inside = false;
	for (int guard = 0; guard < 600 && !inside; ++guard)
	{
		rig.frame(1);
		porter = rig.logic().findObjectByID(pid);
		REQUIRE(porter != nullptr);
		inside = dozer->working() && !porter->isInWorld();
	}
	REQUIRE_MESSAGE(inside, "the porter never went into its site");
	const Drawable *d = rig.game->drawables().findByObject(pid);
	REQUIRE(d != nullptr);
	// the fade out starts with the work (sub task 3) over BuilderFadeOutTime client frames (the fade's target stays after it ends)
	bool hidden = false;
	for (int guard = 0; guard < 60 && !hidden; ++guard)
	{
		rig.frame(1);
		hidden = d->fade().hidden;
	}
	CHECK(hidden);
	CHECK(d->fade().target == (double)rig.logic().settings().builderFadeOutFrames);
	CHECK(d->fade().mode == 0);
	CHECK(d->drawOpacity() == 0.0f);
	porter = rig.logic().findObjectByID(pid);
	REQUIRE(porter != nullptr);
	CHECK(sm->hasRecord(*porter));
	CHECK(sm->lookRadius(*porter) == -1); // out of the world it does not look (RW 0x694A45)
	{
		const std::shared_ptr<const LogicSnapshot> snap = rig.game->presentedSnapshot();
		REQUIRE(snap);
		const ObjectSnapshot *rec = snap->find(pid);
		REQUIRE(rec != nullptr);
		CHECK(rec->shroudRecord);
		CHECK(rec->objectShroud == (int)OBJECTSHROUD_SHROUDED); // never seen cells
		CHECK_FALSE(rec->drawableHidden); // no container encloses it: the fade is what hides it
	}
	// the work ends: back in the world, the fade in starts at opacity 0 and shows the drawable
	bool out = false;
	for (int guard = 0; guard < 3000 && !out; ++guard)
	{
		rig.frame(1);
		porter = rig.logic().findObjectByID(pid);
		REQUIRE(porter != nullptr);
		out = porter->isInWorld();
	}
	REQUIRE_MESSAGE(out, "the porter did not come out of its site");
	rig.frame(1);
	CHECK_FALSE(d->fade().hidden);
	CHECK(d->fade().target == (double)rig.logic().settings().builderFadeInFrames); // the fade in over BuilderFadeInTime (RW 0x670AA2)
	CHECK(sm->lookRadius(*porter) == 3); // back in the world it looks again
}

namespace
{
// an enemy MordorOrcPit at the far corner; the camera looks at it
ObjectID enemyPitInView(hudtest::Rig &rig, ShroudManager &sm, Coord3D &at)
{
	at = farCorner(rig, sm);
	const int enemy = rig.game->players().findPlayerWithName("Player_2")->getPlayerIndex();
	std::string err;
	Object *pit = rig.game->createObject("MordorOrcPit", enemy, at, 0.0f, &err);
	REQUIRE_MESSAGE(pit != nullptr, err);
	at = *pit->getPosition();
	rig.lookAt(at);
	rig.frame(2);
	return pit->getID();
}

void dragAround(hudtest::Rig &rig, const Coord3D &at)
{
	const ICoord2D p = rig.screenOf(at);
	rig.leftDrag(p.x - 80, p.y - 80, p.x + 80, p.y + 80);
	rig.frame(1);
}
} // namespace

TEST_CASE("visual2 r2 retail: a drag box does not select an enemy the local player cannot see (addDrawableToList's shroud rule, S-288); seen, it does")
{
	if (!hudtest::haveWorld("visual2 r2 retail drag box in the shroud"))
	{
		return;
	}
	hudtest::SharedWorld &s = hudtest::shared();
	hudtest::Rig rig(s);
	const auto context = s.world->enterContext();
	creeptest::removeCreeps(rig.logic());
	ShroudManager *sm = rig.logic().shroud();
	REQUIRE(sm != nullptr);
	sm->setDisplayed(true);
	Coord3D at;
	const ObjectID pid = enemyPitInView(rig, *sm, at);
	REQUIRE(sm->clientObjectStatus(*rig.logic().findObjectByID(pid), rig.index()) == OBJECTSHROUD_SHROUDED);
	REQUIRE(rig.input->ui().selected().empty());
	dragAround(rig, at);
	CHECK_FALSE(rig.input->ui().isSelected(pid));
	CHECK(rig.input->ui().selected().empty());
	// the control: in sight, the same drag selects it (a lone enemy: selectEnemies)
	sm->revealMapForPlayerPermanently(rig.index());
	rig.frame(2);
	REQUIRE(sm->clientObjectStatus(*rig.logic().findObjectByID(pid), rig.index()) == OBJECTSHROUD_CLEAR);
	dragAround(rig, at);
	CHECK(rig.input->ui().isSelected(pid));
}

TEST_CASE("visual2 r2 retail: a selected enemy that goes into the fog is deselected on the next 30th client frame (InGameUI::update, RW 0x6A2449)")
{
	if (!hudtest::haveWorld("visual2 r2 retail fogged deselection"))
	{
		return;
	}
	hudtest::SharedWorld &s = hudtest::shared();
	hudtest::Rig rig(s);
	const auto context = s.world->enterContext();
	creeptest::removeCreeps(rig.logic());
	ShroudManager *sm = rig.logic().shroud();
	REQUIRE(sm != nullptr);
	sm->setDisplayed(true);
	sm->revealMapForPlayerPermanently(rig.index());
	Coord3D at;
	const ObjectID pid = enemyPitInView(rig, *sm, at);
	dragAround(rig, at);
	REQUIRE(rig.input->ui().isSelected(pid));
	sm->undoRevealMapForPlayerPermanently(rig.index());
	rig.frame(2);
	REQUIRE(sm->clientObjectStatus(*rig.logic().findObjectByID(pid), rig.index()) == OBJECTSHROUD_FOGGED);
	InGameUI &ui = rig.input->ui();
	// control: an own keep at the opposite corner (its own sight keeps it CLEAR), selected beside the pit, stays selected through the same frame
	const float loX = sm->originX(), loY = sm->originY();
	const float hiX = loX + sm->cellSize() * (float)sm->cellCountX(), hiY = loY + sm->cellSize() * (float)sm->cellCountY();
	std::string err;
	Object *own = rig.game->createObject("GondorKeep", rig.index(), Coord3D{ loX + hiX - at.x, loY + hiY - at.y, 0.0f }, 0.0f, &err);
	REQUIRE_MESSAGE(own != nullptr, err);
	const ObjectID ownId = own->getID();
	rig.frame(2);
	REQUIRE(sm->clientObjectStatus(*rig.logic().findObjectByID(ownId), rig.index()) == OBJECTSHROUD_CLEAR);
	ui.selectObject(ownId);
	REQUIRE(ui.isSelected(ownId));
	while (ui.clientFrame() % 30 != 29)
	{
		ui.advanceClientFrame();
	}
	rig.input->update();
	CHECK(ui.isSelected(pid)); // not on this client frame: only every 30th
	ui.advanceClientFrame();
	rig.input->update();
	CHECK_FALSE(ui.isSelected(pid));
	CHECK(ui.isSelected(ownId));
}

TEST_CASE("visual2 r2 retail: an omniscient view (all-vision observer, replay) answers the audio shroud query clear; the player's own view does not")
{
	if (!hudtest::haveWorld("visual2 r2 retail observer audio shroud"))
	{
		return;
	}
	hudtest::SharedWorld &s = hudtest::shared();
	hudtest::Rig rig(s);
	const auto context = s.world->enterContext();
	ShroudManager *sm = rig.logic().shroud();
	REQUIRE(sm != nullptr);
	sm->setDisplayed(true);
	const Coord3D at = farCorner(rig, *sm);
	// the local player's view (LiveGame's m_observedPlayer -1): the far corner is shrouded for it
	const std::shared_ptr<const LogicSnapshot> own = LogicSnapshot::build(rig.logic(), nullptr, false, 0, -1, false);
	REQUIRE((own->shroud && own->players));
	CHECK(own->shroud->localPlayer == rig.index());
	CHECK_FALSE(LiveGameAudio::shroudClearFor(own.get(), own->players->localIndex, at));
	// the observer's "all" view (LiveGame::observePlayer(-1) -> -2): its PlayerView has no local index (-1), its shroud view -2 (the review's mismatch)
	const std::shared_ptr<const LogicSnapshot> all = LogicSnapshot::build(rig.logic(), nullptr, false, 0, -2, false);
	REQUIRE((all->shroud && all->players));
	CHECK(all->shroud->localPlayer == -2);
	CHECK(all->players->localIndex == -1);
	CHECK(LiveGameAudio::shroudClearFor(all.get(), all->players->localIndex, at));
}

TEST_CASE("visual2 stops: S-4970 .. S-4974 reach the live report (GodotGameWorld's effect report) and S-682 no longer claims a clear shroud")
{
	const std::vector<std::string> lines = visual2StopLines();
	REQUIRE(lines.size() == 5);
	CHECK(lines[0].rfind("[S-4970] drawable shroud look:", 0) == 0);
	CHECK(lines[1].rfind("[S-4971] effects in the shroud:", 0) == 0);
	CHECK(lines[2].rfind("[S-4972] breeze:", 0) == 0);
	CHECK(lines[3].rfind("[S-4973] tree sway:", 0) == 0);
	CHECK(lines[3].find("final table interval blends back to entry 0") != std::string::npos);
	CHECK(lines[4].rfind("[S-4974] structure floors:", 0) == 0);
	bool found682 = false;
	for (const std::string &l : LiveFX::stops())
	{
		if (l.rfind("[S-682]", 0) == 0)
		{
			found682 = true;
			CHECK(l.find("the shroud is clear") == std::string::npos);
			CHECK(l.find("S-4971") != std::string::npos);
		}
	}
	CHECK(found682);
}

// lane RELFIX-2: why the hud6 harness (scripts/hud6_player.gd) places its barracks on the map. RW 0x6A2449 .. 0x6A24BF has no owner test: a selected
// drawable of the local player's own whose status (RW 0x68D8F7) is SHROUDED is deselected too, and an object outside the shroud grid covers no cell, which
// RW 0xB4E890 answers SHROUDED (total == 0). The same barracks on the map clears its own cells (ShroudClearingRange 160) and stays selected
TEST_CASE("visual2 r2 retail: an own barracks off the map is SHROUDED for its owner and deselected on the 30th client frame; on the map it stays")
{
	if (!hudtest::haveWorld("visual2 r2 retail own object off the map"))
	{
		return;
	}
	hudtest::SharedWorld &s = hudtest::shared();
	hudtest::Rig rig(s);
	const auto context = s.world->enterContext();
	creeptest::removeCreeps(rig.logic());
	ShroudManager *sm = rig.logic().shroud();
	REQUIRE(sm != nullptr);
	sm->setDisplayed(true);
	const Coord3D on = farCorner(rig, *sm);
	const float hiX = sm->originX() + sm->cellSize() * (float)sm->cellCountX();
	const Coord3D off{ hiX + 52.0f, on.y, 0.0f }; // Evendim's case: fortress.x + 450 = 5052 on a 5000-unit grid
	std::string err;
	Object *a = rig.game->createObject("GondorBarracks", rig.index(), on, 0.0f, &err);
	REQUIRE_MESSAGE(a != nullptr, err);
	Object *b = rig.game->createObject("GondorBarracks", rig.index(), off, 0.0f, &err);
	REQUIRE_MESSAGE(b != nullptr, err);
	const ObjectID onId = a->getID(), offId = b->getID();
	rig.frame(3);
	REQUIRE(sm->clientObjectStatus(*rig.logic().findObjectByID(onId), rig.index()) == OBJECTSHROUD_CLEAR);
	REQUIRE(sm->clientObjectStatus(*rig.logic().findObjectByID(offId), rig.index()) == OBJECTSHROUD_SHROUDED);
	InGameUI &ui = rig.input->ui();
	ui.selectObject(onId);
	ui.selectObject(offId);
	REQUIRE(ui.isSelected(onId));
	REQUIRE(ui.isSelected(offId));
	do
	{
		ui.advanceClientFrame();
	} while (ui.clientFrame() % 30 != 0);
	rig.input->update();
	CHECK(ui.isSelected(onId));
	CHECK_FALSE(ui.isSelected(offId));
}

// ---- lane FOGFX-1 (FB-0056: "some FX are visible in the fog of war, including construction dust") ------------------------------------------------
namespace
{
struct FogFxRun
{
	int status = -1;                        ///< the object's status for the local player at the end
	size_t dustSystems = 0;                 ///< BuildingContructDust systems on the foundation's drawable at the end
	size_t particlesNear = 0;               ///< particles within 300 of the object, summed over the frames
	unsigned long long frameEventsFired = 0; ///< LiveFX's animation FXEvents fired (RW 0x4BCE68)
};

// an enemy (Player_2) object at the far corner of the map from the local base, `frames` logic frames with the live effect player; `sight` Never: the
// local player never sees it, Always: the map revealed. `foundation`: a GondorBarracks foundation actively being built (its ACTIVELY_BEING_CONSTRUCTED
// state's ParticleSysBone BuildingContructDust, retail men/barracks.ini); else a MordorMumakil walking toward the map's centre (its moving state's
// FXEvent footsteps)
FogFxRun enemyInFog(Sight sight, bool foundation, int frames)
{
	hudtest::SharedWorld &s = hudtest::shared();
	FogFxRun out;
	hudtest::Rig rig(s, "map mp fall back 4p", "FactionMen");
	auto playback = std::make_unique<FXPlayback>(*s.mount->fs, RandomAlgorithm::RotWK_GameDat_LCG);
	REQUIRE(playback->loadRetailData().empty());
	auto fx = std::make_unique<LiveFX>(*rig.game, *playback);
	const auto context = s.world->enterContext();
	creeptest::removeCreeps(rig.logic());
	ShroudManager *sm = rig.logic().shroud();
	REQUIRE(sm != nullptr);
	sm->setDisplayed(true);
	const int me = rig.index();
	const Coord3D at = farCorner(rig, *sm);
	if (sight == Sight::Always)
	{
		sm->revealMapForPlayerPermanently(me);
	}
	const int enemy = rig.game->players().findPlayerWithName("Player_2")->getPlayerIndex();
	std::string err;
	Object *o = rig.game->createObject(foundation ? "GondorBarracks" : "MordorMumakil", enemy, at, 0.0f, &err);
	REQUIRE_MESSAGE(o != nullptr, err);
	const ObjectID id = o->getID();
	if (foundation)
	{
		// a foundation that rises (as test_fx3_particles: Construction.cpp startRise, RW 0x858931 ff)
		o->setConstructionPercent(0.0f);
		o->setStatus(OBJECT_STATUS_UNDER_CONSTRUCTION, true);
		o->setModelConditionState(ModelCondition::indexOf("ACTIVELY_BEING_CONSTRUCTED"), true);
		o->setModelConditionState(ModelCondition::indexOf("PARTIALLY_CONSTRUCTED"), true);
	}
	else
	{
		const float cx = sm->originX() + sm->cellSize() * (float)sm->cellCountX() * 0.5f, cy = sm->originY() + sm->cellSize() * (float)sm->cellCountY() * 0.5f;
		const float dx = cx - at.x, dy = cy - at.y, len = std::sqrt(dx * dx + dy * dy);
		REQUIRE(o->getAIUpdateInterface() != nullptr);
		o->getAIUpdateInterface()->aiMoveToPosition(Coord3D{ at.x + dx / len * 400.0f, at.y + dy / len * 400.0f, 0.0f }, CMD_FROM_SCRIPT);
	}
	for (int i = 0; i < frames; ++i)
	{
		fxFrame(rig, *fx, *playback);
		size_t systems = 0, particles = 0;
		countNear(*playback, *rig.logic().findObjectByID(id)->getPosition(), systems, particles);
		out.particlesNear += particles;
	}
	const Drawable *d = rig.game->drawables().findByObject(id);
	REQUIRE(d != nullptr);
	playback->particles().forEachSystem([&](const FXParticleSystem::ParticleSystem &ps) {
		if (!ps.isDestroyed() && ps.getTemplate().getName() == "BuildingContructDust" && ps.attachedDrawableID() == d->getID())
		{
			++out.dustSystems;
		}
	});
	out.status = (int)sm->clientObjectStatus(*rig.logic().findObjectByID(id), me);
	out.frameEventsFired = fx->stats().frameEventsFired;
	return out;
}
} // namespace

TEST_CASE("fogfx1 retail: an enemy foundation being built in the shroud makes no construction dust (RW 0x5FA2CE, 0x64849E); in sight, the same dust plays")
{
	if (!hudtest::haveWorld("fogfx1 retail construction dust in the shroud"))
	{
		return;
	}
	const FogFxRun hidden = enemyInFog(Sight::Never, true, 30);
	CHECK(hidden.status == (int)OBJECTSHROUD_SHROUDED);
	CHECK(hidden.dustSystems == 1u); // the ParticleSysBone system exists on the drawable ...
	CHECK(hidden.particlesNear == 0u); // ... and emits nothing while the local player cannot see the foundation
	const FogFxRun seen = enemyInFog(Sight::Always, true, 30);
	CHECK(seen.status == (int)OBJECTSHROUD_CLEAR);
	CHECK(seen.dustSystems == 1u);
	CHECK(seen.particlesNear > 0u); // tolerance: presence of emission is the claim; the counts belong to the FX-1 particle simulator
	MESSAGE("fogfx1: construction dust particles in the shroud " << hidden.particlesNear << ", in sight " << seen.particlesNear);
}

TEST_CASE("fogfx1 retail: a walking enemy in the shroud fires no animation FXEvents (doDrawModule RW 0x4C72BE -> 0x4BCE68 is not reached for a drawable "
		  "that is not drawn); in sight, its footsteps fire")
{
	if (!hudtest::haveWorld("fogfx1 retail frame events in the shroud"))
	{
		return;
	}
	const FogFxRun hidden = enemyInFog(Sight::Never, false, 40);
	CHECK(hidden.status == (int)OBJECTSHROUD_SHROUDED);
	CHECK(hidden.frameEventsFired == 0u);
	CHECK(hidden.particlesNear == 0u);
	const FogFxRun seen = enemyInFog(Sight::Always, false, 40);
	CHECK(seen.status == (int)OBJECTSHROUD_CLEAR);
	CHECK(seen.frameEventsFired > 0u); // tolerance: presence is the claim; the count follows the animation's frames
	MESSAGE("fogfx1: mumakil FXEvents fired in the shroud " << hidden.frameEventsFired << ", in sight " << seen.frameEventsFired);
}
