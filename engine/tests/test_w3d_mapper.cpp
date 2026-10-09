// OpenBFME unit tests: texture mappers (WW3D2 mapper.cpp as retail BFME2 evaluates them). GPL-3.0.
// Every expected number is worked out by hand from the arguments and the formulas of the BFME2 / ZH Calculate_Texture_Matrix
// bodies cited in mapper.cpp; none is read back from the code under test.

#include "doctest.h"

#include "Libraries/WWVegas/WW3D2/mapper.h"

#include <cmath>
#include <string>

namespace
{
const float kPi = 3.14159265358979f;

std::unique_ptr<TextureMapperClass> make(int type, const std::string &args, std::uint32_t now = 0, MapperRandom rnd = MapperRandom())
{
	std::string error;
	auto m = Create_Texture_Mapper(type, 0, args, now, rnd, &error);
	INFO(error);
	REQUIRE(m);
	return m;
}
} // namespace

TEST_CASE("MapperArgs: Key=Value lines, ';' comments, case-insensitive keys, defaults")
{
	MapperArgs a;
	std::string error;
	REQUIRE_MESSAGE(a.Parse("UPerSec=0.25; In Hertz\r\nvpersec = -0.5\n\nClampFix=true\nLast=7", &error), error);
	CHECK(a.Get_Float("uPERsec", 9.0f) == doctest::Approx(0.25f));
	CHECK(a.Get_Float("VPerSec", 9.0f) == doctest::Approx(-0.5f));
	CHECK(a.Get_Bool("ClampFix", false) == true);
	CHECK(a.Get_Int("Last", 0) == 7);
	CHECK(a.Get_Float("Missing", 3.5f) == 3.5f);
	CHECK(a.Unread_Keys().empty());
	MapperArgs b;
	CHECK_FALSE(b.Parse("this is not an assignment", &error));
	CHECK(error.find("not Key=Value") != std::string::npos);
}

TEST_CASE("Scale mapper: matrix is diag(UScale, VScale)")
{
	auto m = make(W3D_MAPPING_SCALE, "UScale=2\nVScale=3");
	W3DTexMatrix t;
	m->Calculate_Texture_Matrix(t, 12345);
	CHECK(t.M[0][0] == 2.0f);
	CHECK(t.M[1][1] == 3.0f);
	CHECK(t.M[0][2] == 0.0f);
	CHECK(t.M[1][2] == 0.0f);
	CHECK_FALSE(m->Is_Time_Variant());
}

TEST_CASE("LinearOffset mapper: offset = -0.001 * perSec * ms, wrapped into [0,1) unless ClampFix")
{
	auto m = make(W3D_MAPPING_LINEAR_OFFSET, "UPerSec=1.0\nVPerSec=0", 1000);
	W3DTexMatrix t;
	// 500 ms later: u = 0 + (1.0 * -0.001) * 500 = -0.5, wrapped to 0.5.
	m->Calculate_Texture_Matrix(t, 1500);
	CHECK(t.M[0][2] == doctest::Approx(0.5f));
	CHECK(t.M[1][2] == doctest::Approx(0.0f));
	CHECK(t.M[0][0] == 1.0f);
	// another 500 ms from the stored 0.5: 0.5 - 0.5 = 0
	m->Calculate_Texture_Matrix(t, 2000);
	CHECK(t.M[0][2] == doctest::Approx(0.0f).epsilon(1e-6));
	CHECK(m->Is_Time_Variant());

	// ClampFix: no wrap, clamped to [-Scale, +Scale]. Scale 2, offset -0.5 stays -0.5; offset -5 clamps to -2.
	auto c = make(W3D_MAPPING_LINEAR_OFFSET, "UPerSec=1.0\nClampFix=yes\nUScale=2", 0);
	c->Calculate_Texture_Matrix(t, 500);
	CHECK(t.M[0][2] == doctest::Approx(-0.5f));
	CHECK(t.M[0][0] == 2.0f);
	c->Calculate_Texture_Matrix(t, 500 + 4500);
	CHECK(t.M[0][2] == doctest::Approx(-2.0f));
}

TEST_CASE("LinearOffset mapper: UOffset/VOffset seed the offset and Reset restarts from zero")
{
	auto m = make(W3D_MAPPING_LINEAR_OFFSET, "UOffset=0.25\nVOffset=0.75", 0);
	W3DTexMatrix t;
	m->Calculate_Texture_Matrix(t, 0);
	CHECK(t.M[0][2] == doctest::Approx(0.25f));
	CHECK(t.M[1][2] == doctest::Approx(0.75f));
	m->Reset(10);
	m->Calculate_Texture_Matrix(t, 10);
	CHECK(t.M[0][2] == doctest::Approx(0.0f));
}

TEST_CASE("Rotate mapper: a quarter turn about the centre (0.5, 0.5)")
{
	// Speed 0.25 Hz: after 1000 ms the angle is 2*pi*0.25 = pi/2, c = 0, s = 1.
	auto m = make(W3D_MAPPING_ROTATE, "Speed=0.25\nUCenter=0.5\nVCenter=0.5", 0);
	W3DTexMatrix t;
	m->Calculate_Texture_Matrix(t, 1000);
	CHECK(t.M[0][0] == doctest::Approx(0.0f).epsilon(1e-6));
	CHECK(t.M[0][1] == doctest::Approx(-1.0f));
	CHECK(t.M[0][2] == doctest::Approx(1.0f)); // -(c*0.5 - s*0.5 - 0.5) = 1
	CHECK(t.M[1][0] == doctest::Approx(1.0f));
	CHECK(t.M[1][1] == doctest::Approx(0.0f).epsilon(1e-6));
	CHECK(t.M[1][2] == doctest::Approx(0.0f).epsilon(1e-6)); // -(s*0.5 + c*0.5 - 0.5) = 0
	// The default speed when the argument is missing is 0.1 Hz.
	auto d = make(W3D_MAPPING_ROTATE, "", 0);
	d->Calculate_Texture_Matrix(t, 2500); // 0.1 Hz * 2.5 s = a quarter turn
	CHECK(t.M[1][0] == doctest::Approx(1.0f).epsilon(1e-5));
}

TEST_CASE("SineLinearOffset mapper: offset = amp * sin(freq * angle + phase * pi)")
{
	auto m = make(W3D_MAPPING_SINE_LINEAR_OFFSET, "UAmp=0.5\nUFreq=1\nUPhase=0\nVAmp=2\nVFreq=2\nVPhase=0.5", 0);
	W3DTexMatrix t;
	// 250 ms: angle = 250 * 2pi / 1000 = pi/2.
	m->Calculate_Texture_Matrix(t, 250);
	CHECK(t.M[0][2] == doctest::Approx(0.5f * std::sin(kPi / 2)).epsilon(1e-5));
	// v = 2 * sin(2 * pi/2 + 0.5 * pi) = 2 * sin(1.5 pi) = -2
	CHECK(t.M[1][2] == doctest::Approx(-2.0f).epsilon(1e-4));
}

TEST_CASE("StepLinearOffset mapper: whole steps only, remainder carried")
{
	// UStep 0.25 per step, 2 steps per second. After 1000 ms: StepsPerMilliSec = 0.002, 2 steps, offset 0.5.
	auto m = make(W3D_MAPPING_STEP_LINEAR_OFFSET, "UStep=0.25\nSPS=2", 0);
	W3DTexMatrix t;
	m->Calculate_Texture_Matrix(t, 400); // 0.8 of a step: none yet
	CHECK(t.M[0][2] == doctest::Approx(0.0f));
	m->Calculate_Texture_Matrix(t, 1000); // 1000 ms in total: two steps
	CHECK(t.M[0][2] == doctest::Approx(0.5f));
	m->Calculate_Texture_Matrix(t, 2000); // two more: 1.0 wraps to 0
	CHECK(t.M[0][2] == doctest::Approx(0.0f).epsilon(1e-5));
}

TEST_CASE("ZigZagLinearOffset mapper: offset rises for half the period and falls for the rest")
{
	// UPerSec 1, Period 2 s -> Speed 0.001 per ms, Period 2000 ms.
	auto m = make(W3D_MAPPING_ZIGZAG_LINEAR_OFFSET, "UPerSec=1\nPeriod=2", 0);
	W3DTexMatrix t;
	m->Calculate_Texture_Matrix(t, 500);
	CHECK(t.M[0][2] == doctest::Approx(0.5f)); // time 500
	m->Calculate_Texture_Matrix(t, 1500);      // remainder 1500 > half period: time = 2000 - 1500
	CHECK(t.M[0][2] == doctest::Approx(0.5f));
	m->Calculate_Texture_Matrix(t, 2500);      // one full period elapsed: remainder 500 again
	CHECK(t.M[0][2] == doctest::Approx(0.5f).epsilon(1e-5));
	// A negative period behaves like a positive one; a zero period yields no offset.
	auto n = make(W3D_MAPPING_ZIGZAG_LINEAR_OFFSET, "UPerSec=1\nPeriod=-2", 0);
	n->Calculate_Texture_Matrix(t, 500);
	CHECK(t.M[0][2] == doctest::Approx(0.5f));
	auto z = make(W3D_MAPPING_ZIGZAG_LINEAR_OFFSET, "UPerSec=1", 0);
	z->Calculate_Texture_Matrix(t, 500);
	CHECK(t.M[0][2] == 0.0f);
}

TEST_CASE("Grid mapper: 4x4 sheet at 10 frames per second")
{
	auto m = make(W3D_MAPPING_GRID, "FPS=10\nLog2Width=2", 0);
	W3DTexMatrix t;
	// 250 ms: 2 whole frames (100 ms each), 50 ms carried. Frame 2 -> column 2, row 0 -> u = 2/4.
	m->Calculate_Texture_Matrix(t, 250);
	CHECK(t.M[0][2] == doctest::Approx(0.5f));
	CHECK(t.M[1][2] == doctest::Approx(0.0f));
	// 750 ms later: 50 + 750 = 800 ms -> 8 frames -> frame 10 -> column 2, row 2.
	m->Calculate_Texture_Matrix(t, 1000);
	CHECK(t.M[0][2] == doctest::Approx(0.5f));
	CHECK(t.M[1][2] == doctest::Approx(0.5f));
	// Wraps over the 16 frame default: 10 + 7 = 17 -> 1
	m->Calculate_Texture_Matrix(t, 1700);
	CHECK(t.M[0][2] == doctest::Approx(0.25f));
	CHECK(t.M[1][2] == doctest::Approx(0.0f));
}

TEST_CASE("Grid mapper: negative FPS runs backwards from the last frame; Last and Offset limit and shift the sequence")
{
	auto m = make(W3D_MAPPING_GRID, "FPS=-10\nLog2Width=2", 0);
	W3DTexMatrix t;
	m->Calculate_Texture_Matrix(t, 0);
	// starts at frame 15 -> column 3, row 3
	CHECK(t.M[0][2] == doctest::Approx(0.75f));
	CHECK(t.M[1][2] == doctest::Approx(0.75f));
	m->Calculate_Texture_Matrix(t, 100); // one frame back: 14 -> column 2, row 3
	CHECK(t.M[0][2] == doctest::Approx(0.5f));
	CHECK(t.M[1][2] == doctest::Approx(0.75f));

	// Last=6: only frames 0..5 exist; Offset=2 starts at frame 2. After 400 ms: 2 + 4 = 6 -> 0.
	auto l = make(W3D_MAPPING_GRID, "FPS=10\nLog2Width=2\nLast=6\nOffset=2", 0);
	l->Calculate_Texture_Matrix(t, 0);
	CHECK(t.M[0][2] == doctest::Approx(0.5f));
	l->Calculate_Texture_Matrix(t, 400);
	CHECK(t.M[0][2] == doctest::Approx(0.0f));
	CHECK(t.M[1][2] == doctest::Approx(0.0f));
}

TEST_CASE("Environment mappers: the canonical (0.5, 0.5) environment matrix and the texgen they select")
{
	W3DTexMatrix t;
	auto env = make(W3D_MAPPING_ENVIRONMENT, "");
	CHECK(env->Get_TexGen() == W3D_TEXGEN_CAMERA_REFLECTION);
	env->Calculate_Texture_Matrix(t, 0);
	CHECK(t.M[0][0] == 0.5f);
	CHECK(t.M[0][3] == 0.5f);
	CHECK(t.M[1][1] == 0.5f);
	CHECK(t.M[1][3] == 0.5f);
	CHECK(t.M[2][2] == 1.0f);
	CHECK(t.M[3][3] == 1.0f);
	auto classic = make(W3D_MAPPING_CHEAP_ENVIRONMENT, "");
	CHECK(classic->Get_TexGen() == W3D_TEXGEN_CAMERA_NORMAL);
	CHECK_FALSE(classic->Is_Time_Variant());

	// World space variants keep the matrix before the view multiply and say so.
	auto ws = make(W3D_MAPPING_WS_ENVIRONMENT, "Axis=Y");
	CHECK(ws->View_Dependent());
	ws->Calculate_Texture_Matrix(t, 0);
	CHECK(t.M[0][0] == 0.5f);
	CHECK(t.M[1][2] == 0.5f); // Y axis: v takes the third component
	CHECK(t.M[1][1] == 0.0f);

	// Grid environment: del = 0.5 / width, offset by the frame cell.
	auto g = make(W3D_MAPPING_GRID_ENVIRONMENT, "FPS=10\nLog2Width=1", 0);
	g->Calculate_Texture_Matrix(t, 100); // frame 1 of a 2x2 grid: u offset 0.5, v offset 0
	CHECK(t.M[0][0] == doctest::Approx(0.25f));
	CHECK(t.M[0][3] == doctest::Approx(0.5f + 0.25f));
	CHECK(t.M[1][3] == doctest::Approx(0.0f + 0.25f));
}

TEST_CASE("Edge mapper: u comes from the normal's z, v scrolls with VPerSec")
{
	auto m = make(W3D_MAPPING_EDGE, "VPerSec=0.5\nVStart=0.25", 0);
	CHECK(m->Get_TexGen() == W3D_TEXGEN_CAMERA_NORMAL);
	W3DTexMatrix t;
	m->Calculate_Texture_Matrix(t, 1000); // 1 s: 0.25 + 0.5 = 0.75
	CHECK(t.M[0][2] == 0.5f);
	CHECK(t.M[0][3] == 0.5f);
	CHECK(t.M[1][3] == doctest::Approx(0.75f));
	auto r = make(W3D_MAPPING_EDGE, "UseReflect=1", 0);
	CHECK(r->Get_TexGen() == W3D_TEXGEN_CAMERA_REFLECTION);
}

TEST_CASE("Random mapper: angle and offset come from the injected generator, re-drawn only when FPS is non-zero")
{
	// First three draws: angle = 2*pi*0.25 (a quarter turn), centre (0.3, 0.6).
	const float draws[] = { 0.25f, 0.3f, 0.6f, 0.0f, 0.0f, 0.0f };
	int n = 0;
	MapperRandom rnd = [&]() { return draws[n++ % 6]; };
	auto m = make(W3D_MAPPING_RANDOM, "UScale=2\nVScale=4", 0, rnd);
	W3DTexMatrix t;
	m->Calculate_Texture_Matrix(t, 0);
	CHECK(t.M[0][0] == doctest::Approx(0.0f).epsilon(1e-6));
	CHECK(t.M[0][1] == doctest::Approx(-1.0f * 4.0f)); // -s * Sy
	CHECK(t.M[1][0] == doctest::Approx(1.0f * 2.0f));  // s * Sx
	CHECK(t.M[0][2] == doctest::Approx(0.3f));
	CHECK(t.M[1][2] == doctest::Approx(0.6f));
	CHECK(n == 3); // FPS = 0: no further draws
	m->Calculate_Texture_Matrix(t, 5000);
	CHECK(n == 3);
}

TEST_CASE("Create_Texture_Mapper: UV and SILHOUETTE make no mapper, an unknown type is flagged, bad arguments are errors")
{
	std::string error;
	bool unknown = false;
	CHECK(Create_Texture_Mapper(W3D_MAPPING_UV, 0, "", 0, MapperRandom(), &error, &unknown) == nullptr);
	CHECK(error.empty());
	CHECK_FALSE(unknown);
	CHECK(Create_Texture_Mapper(W3D_MAPPING_SILHOUETTE, 0, "", 0, MapperRandom(), &error, &unknown) == nullptr);
	CHECK_FALSE(unknown);
	// 67 is the invalid value one retail material carries (spec 3.4): ZH's switch has no case for it.
	CHECK(Create_Texture_Mapper(67, 0, "", 0, MapperRandom(), &error, &unknown) == nullptr);
	CHECK(unknown);
	CHECK(error.empty());

	CHECK(Create_Texture_Mapper(W3D_MAPPING_LINEAR_OFFSET, 0, "Bogus=1", 0, MapperRandom(), &error, &unknown) == nullptr);
	CHECK(error.find("bogus") != std::string::npos);
	CHECK(Create_Texture_Mapper(W3D_MAPPING_LINEAR_OFFSET, 0, "garbage", 0, MapperRandom(), &error, &unknown) == nullptr);
	CHECK(error.find("not Key=Value") != std::string::npos);
}
