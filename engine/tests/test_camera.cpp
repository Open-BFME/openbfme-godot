// OpenBFME tests of the retail tactical camera (lane CAM-1): the GameData camera fields, the camera maths and limits, the height field and the LookAt translator.
// The tests that need the install SKIP loudly when ROTWK_INSTALL / BFME2_INSTALL are unset; the binary-fact tests need RW_GAME_DAT. GPL-3.0.

#include "doctest.h"

#include "HudTestUtil.h"
#include "PeImage.h"

#include "Common/Dict.h"
#include "GameClient/CameraSettings.h"
#include "GameClient/DrawablePick.h"
#include "GameClient/HudObjects.h"
#include "GameClient/TacticalCamera.h"
#include "GameEngineDevice/W3DDevice/GameClient/WorldHeightMap.h"
#include "GameLogic/Map/TerrainLogic.h"

#include <cmath>
#include <cstring>
#include <iomanip>
#include <sstream>

using namespace hudtest;
using retailtest::PeImage;

namespace
{
const char *kGameData = R"(GameData
  DefaultCameraMinHeight = 120.0
  DefaultCameraMaxHeight = 300.0
  DefaultCameraPitchAngle = 37.5
  DefaultCameraYawAngle = 0.0
  DefaultCameraScrollSpeedScalar = 1.0
  CameraLockHeightDelta = 150.0
  CameraTerrainSampleRadiusForHeight = 1.0
  UseCameraInReplay = No
  CameraAdjustSpeed = 0.3
  ScrollAmountCutoff = 50.0
  EnforceMaxCameraHeight = No
  HorizontalScrollSpeedFactor = 0.4
  VerticalScrollSpeedFactor = 0.5
  ScreenEdgeScrollSpeedFactor = 1.0
  ScreenEdgeScrollRampTime = 0.25
  KeyboardScrollSpeedFactor = 1.0
  KeyboardCameraRotateSpeed = 0.1
  PartitionCellSize = 40.0
  SomethingElse = 5
End
)";

float f32At(const PeImage &pe, std::uint32_t va)
{
	const std::uint32_t u = pe.u32At(va);
	float f;
	std::memcpy(&f, &u, 4);
	return f;
}
} // namespace

TEST_CASE("camera: GameData's camera fields parse as the retail parsers do and a missing key is an error")
{
	CameraSettings s;
	std::string err;
	REQUIRE_MESSAGE(CameraSettings::scan(kGameData, s, &err), err);
	CHECK(s.defaultMinHeight == 120.0f);
	CHECK(s.defaultMaxHeight == 300.0f);
	CHECK(s.defaultPitchAngle == 37.5f);
	CHECK(s.defaultYawAngle == 0.0f);
	CHECK(s.cameraAdjustSpeed == 0.3f);
	CHECK(s.horizontalScrollSpeedFactor == 0.4f);
	CHECK(s.verticalScrollSpeedFactor == 0.5f);
	// RW 0x42EE37: seconds * 1000 truncated to an int
	CHECK(s.screenEdgeScrollRampTimeMs == 250);
	// the two keys the retail GameData lacks keep the constructor defaults
	CHECK(s.easeFactor == 0.2f);
	CHECK(s.keyboardDefaultScrollSpeedFactor == 1.0f);
	CHECK(s.mapHeightSmoothness == 1.0f);

	std::string text = kGameData;
	const size_t at = text.find("  CameraAdjustSpeed = 0.3\n");
	REQUIRE(at != std::string::npos);
	text.erase(at, std::strlen("  CameraAdjustSpeed = 0.3\n"));
	CameraSettings t;
	CHECK_FALSE(CameraSettings::scan(text, t, &err));
	CHECK(err.find("CameraAdjustSpeed") != std::string::npos);
	// a malformed real fails like the retail parser (INI scanReal)
	std::string bad = kGameData;
	bad.replace(bad.find("37.5"), 4, "abc");
	CHECK_FALSE(CameraSettings::scan(bad, t, &err));
}

TEST_CASE("camera: the map's camera values override GameData's, key by key")
{
	CameraSettings s;
	std::string err;
	REQUIRE(CameraSettings::scan(kGameData, s, &err));
	Dict d;
	d.setReal("cameraMaxHeight", 450.0f);
	d.setReal("cameraPitchAngle", 40.0f);
	d.setInt("cameraYawAngle", 5); // not a real: ignored, like RW's getReal(key, &exists)
	const MapCameraValues v = MapCameraValues::resolve(s, &d);
	CHECK(v.maxHeight == 450.0f);
	CHECK(v.pitchAngle == 40.0f);
	CHECK(v.minHeight == 120.0f);
	CHECK(v.yawAngle == 0.0f);
	CHECK(v.groundMinHeight == -9999999.0f);
	CHECK(v.groundMaxHeight == 9999999.0f);
	REQUIRE(v.overridden.size() == 2);
	const MapCameraValues none = MapCameraValues::resolve(s, nullptr);
	CHECK(none.maxHeight == 300.0f);
	CHECK(none.overridden.empty());
}

TEST_CASE("camera: the offset of the settings object (RW 0x5011D5): the default settings are ZH's offset, any other value the same pitch with a scaled distance")
{
	CameraSettings s;
	std::string err;
	REQUIRE(CameraSettings::scan(kGameData, s, &err));
	Coord3D o;
	float angle = 99.0f;
	bool legacy = false;
	TacticalCamera::computeCameraOffset(MapCameraValues::resolve(s, nullptr), o, angle, legacy);
	CHECK(legacy);
	CHECK(o.z == 300.0f);
	CHECK(o.y == doctest::Approx(-300.0 / std::tan(37.5 * 0.017453293005625408)).epsilon(1e-6));
	CHECK(o.x == doctest::Approx(0.0).epsilon(1e-9));
	CHECK(angle == 0.0f);
	// pitch 40: the other mode; the camera is 40 degrees above the horizon whatever the distance is
	Dict d;
	d.setReal("cameraPitchAngle", 40.0f);
	d.setReal("cameraYawAngle", 30.0f);
	TacticalCamera::computeCameraOffset(MapCameraValues::resolve(s, &d), o, angle, legacy);
	CHECK_FALSE(legacy);
	CHECK(o.z / -o.y == doctest::Approx(std::tan(40.0 * 0.017453293005625408)).epsilon(1e-5));
	CHECK(o.x == 0.0f);
	CHECK(angle == doctest::Approx(30.0 * 0.017453293).epsilon(1e-6));
	// at pitch 37.5 the distance constant 1.642665 makes the offset height about the max height again (RW 0xBE5640 = 1 / sin(37.5 degrees))
	Dict e;
	e.setReal("cameraMaxHeight", 300.0f);
	e.setReal("cameraMinHeight", 100.0f);
	TacticalCamera::computeCameraOffset(MapCameraValues::resolve(s, &e), o, angle, legacy);
	CHECK_FALSE(legacy);
	CHECK(o.z == doctest::Approx(300.0).epsilon(1e-4));
}

namespace
{
// a map of `n` x `n` height samples, all `base`, with one peak sample
WorldHeightMap flatMap(int n, std::uint16_t base, int peakX = -1, int peakY = -1, std::uint16_t peak = 0, int border = 1)
{
	WorldHeightMap m;
	m.m_width = n;
	m.m_height = n;
	m.m_borderSize = border;
	m.m_data.assign((size_t)n * (size_t)n, base);
	m.m_dataSize = n * n;
	if (peakX >= 0)
	{
		m.m_data[(size_t)peakY * (size_t)n + (size_t)peakX] = peak;
	}
	return m;
}
} // namespace

TEST_CASE("camera: the height field is the block maximum grown with a slope limit (RW 0x710107) and samples bilinearly (RW 0x70FE22)")
{
	// 16 x 16 samples = 4 x 4 cells of 40 units; raw 1280 = 50 world units; one peak of raw 5120 = 200 world units in the cell (1, 1)
	WorldHeightMap map = flatMap(16, 1280, 5, 5, 5120);
	CameraHeightField f;
	f.build(map, 1.0f, -9999999.0f, 9999999.0f);
	REQUIRE(f.ready());
	CHECK(f.gridWidth() == 4);
	CHECK(f.gridHeight() == 4);
	CHECK(f.cell(1, 1) == 200.0f);
	// a neighbour is at least peak - 40 * 1 * 0.6 = 176, a diagonal neighbour peak - 24 * 1.4 = 166.4
	CHECK(f.cell(0, 1) == doctest::Approx(176.0));
	CHECK(f.cell(2, 1) == doctest::Approx(176.0));
	CHECK(f.cell(0, 0) == doctest::Approx(166.4).epsilon(1e-5));
	CHECK(f.cell(3, 1) == doctest::Approx(152.0)); // two cells away: 200 - 2 * 24
	CHECK(f.cell(3, 3) == doctest::Approx(132.8).epsilon(1e-5)); // two diagonal steps from the peak: 200 - 2 * 33.6
	// smoothness 0 builds nothing: the camera samples the terrain around the point instead
	CameraHeightField none;
	none.build(map, 0.0f, -9999999.0f, 9999999.0f);
	CHECK_FALSE(none.ready());
	CHECK(none.sample(10.0f, 10.0f) == 0.0f);
	// the sample at a cell centre point: x' = (x + border * 10) / 40, so a cell is centred (and exact) at x = 40 * i - 10
	CHECK(f.sample(30.0f, 30.0f) == doctest::Approx(200.0).epsilon(1e-5)); // (30 + 10) / 40 = 1.0
	CHECK(f.sample(-10.0f, -10.0f) == doctest::Approx(f.cell(0, 0)).epsilon(1e-5));
	// halfway between the cells (1, 1) and (2, 1) (frac x 0.5, frac y 0): the lower triangle d10 + (1 - fx) * (d00 - d10) + fy * (d11 - d10)
	CHECK(f.sample(50.0f, 30.0f) == doctest::Approx(0.5 * 200.0 + 0.5 * 176.0).epsilon(1e-5));
	// ground limits clamp the samples before the grow
	CameraHeightField clamped;
	clamped.build(map, 1.0f, 0.0f, 100.0f);
	CHECK(clamped.cell(1, 1) == 100.0f);
	// min > max swaps (RW 0x7101B5)
	CameraHeightField swapped;
	swapped.build(map, 1.0f, 100.0f, 0.0f);
	CHECK(swapped.cell(1, 1) == 100.0f);
}


// ---- the binary (RW_GAME_DAT) ---------------------------------------------------------------------------------------------------------------------

TEST_CASE("camera: the GameData rows, constants and vtables the port relies on are in the binary")
{
	const PeImage *pe = PeImage::fromEnvironment();
	if (!pe)
	{
		retailtest::printSkip("camera binary facts (RW_GAME_DAT unset)");
		return;
	}
	// GameData field-table rows { name, parse proc, user data, offset }: row VA, name, GlobalData offset, parse proc (0x42ED00 parseReal, 0x42E558 parseBool, 0x42EE37 seconds -> ms)
	struct Row
	{
		std::uint32_t va;
		const char *name;
		std::uint32_t offset, proc;
	};
	const Row rows[] = {
		{ 0xBFF900, "DefaultCameraMinHeight", 0x9C, 0x42ED00 }, { 0xBFF910, "DefaultCameraMaxHeight", 0xA0, 0x42ED00 }, { 0xBFF920, "DefaultCameraPitchAngle", 0xA4, 0x42ED00 },
		{ 0xBFF930, "DefaultCameraYawAngle", 0xA8, 0x42ED00 }, { 0xBFF940, "DefaultCameraScrollSpeedScalar", 0xAC, 0x42ED00 }, { 0xBFF950, "CameraLockHeightDelta", 0xDD4, 0x42ED00 },
		{ 0xBFF960, "CameraEaseFactor", 0xDE0, 0x42ED00 }, { 0xC00390, "HorizontalScrollSpeedFactor", 0xA9C, 0x42ED00 }, { 0xC003A0, "VerticalScrollSpeedFactor", 0xAA0, 0x42ED00 },
		{ 0xC003B0, "ScreenEdgeScrollSpeedFactor", 0xAA4, 0x42ED00 }, { 0xC003C0, "ScreenEdgeScrollRampTime", 0xAA8, 0x42EE37 }, { 0xC003D0, "ScrollAmountCutoff", 0xAAC, 0x42ED00 },
		{ 0xC003E0, "CameraAdjustSpeed", 0xAB0, 0x42ED00 }, { 0xC003F0, "EnforceMaxCameraHeight", 0xAB4, 0x42E558 }, { 0xC00400, "KeyboardScrollSpeedFactor", 0xAF8, 0x42ED00 },
		{ 0xC00410, "KeyboardDefaultScrollSpeedFactor", 0xAFC, 0x42ED00 }, { 0xC00860, "KeyboardCameraRotateSpeed", 0xC2C, 0x42ED00 },
		{ 0xC00A20, "CameraTerrainSampleRadiusForHeight", 0xDD8, 0x42ED00 }, { 0xC000D0, "UseCameraInReplay", 0xB71, 0x42E558 }, { 0xBFF9F0, "PartitionCellSize", 0xD4, 0x42ED00 },
	};
	for (const Row &r : rows)
	{
		INFO(r.name);
		CHECK(pe->cstring(pe->u32At(r.va)) == r.name);
		CHECK(pe->u32At(r.va + 4) == r.proc);
		CHECK(pe->u32At(r.va + 12) == r.offset);
	}
	// the float constants of the camera code (virtual address, value)
	struct K
	{
		std::uint32_t va;
		float value;
	};
	const K consts[] = {
		{ 0xC0C320, 0.96f }, { 0xBF3DCC, 1.05f }, { 0xC0C324, 0.6283185482025146f }, { 0xC0C328, -0.6283185482025146f }, { 0xBE5634, 37.5f }, { 0xBDD424, 120.0f }, { 0xBD9E90, 300.0f },
		{ 0xBDD764, 250.0f }, { 0xBD1904, 0.25f }, { 0xBD88D8, 100.0f }, { 0xC53B04, 0.004999999888241291f }, { 0xBDD28C, 40.0f }, { 0xBDAD70, 0.6f }, { 0xBDBC98, 1.4f },
		{ 0xBDD378, 700.0f }, { 0xBDAD74, 0.95f }, { 0xBDD410, 0.8726646304130554f }, { 0xBDD730, 1800.0f }, { 0xBD83D8, 10.0f }, { 0xBE5640, 1.6426650285720825f },
		{ 0xBDB8EC, 0.0390625f }, { 0xBDAD78, 0.2f }, { 0xBD83D4, 0.1f },
	};
	for (const K &k : consts)
	{
		INFO("constant at " << std::hex << k.va);
		CHECK(f32At(*pe, k.va) == k.value);
	}
	// the W3DView vtable (RW 0xBDD490): setAngle +0xFC, getAngle +0x100, setPitch +0x104, scrollBy +0x5C, setHeightAboveGround +0x130; View::zoomIn / zoomOut +0x134 / +0x138
	CHECK(pe->u32At(0xBDD490 + 0xFC) == 0x48CA7F);
	CHECK(pe->u32At(0xBDD490 + 0x104) == 0x48CB23);
	CHECK(pe->u32At(0xBDD490 + 0x5C) == 0x48C774);
	CHECK(pe->u32At(0xBDD490 + 0x130) == 0x48CBFE);
	CHECK(pe->u32At(0xBDD490 + 0x134) == 0x65E803);
	CHECK(pe->u32At(0xBDD490 + 0x138) == 0x65E82A);
	// the LookAt translator's vtable (RW 0xC53D3C) has its translateGameMessage RW 0x83AC4A and the settings object's vtable (RW 0xBE5648) its offset function RW 0x5011D5
	CHECK(pe->u32At(0xBE5648 + 0x48) == 0x5011D5);
	CHECK(pe->u32At(0xBE5648 + 0x44) == 0x501162);
	// the translator is attached at priority 0x3C: push 0x3C; push eax; call at RW 0x646BC6
	CHECK(pe->hex(0x646BC6, 2) == "6a3c");
	// the zoom steps: fmul [0xC0C320] / fsub 1.0 and fmul [0xBF3DCC] / fadd 1.0 (RW 0x65E80F, 0x65E836)
	CHECK(pe->hex(0x65E80F, 6) == "d80d20c3c000");
	CHECK(pe->hex(0x65E836, 6) == "d80dcc3dbf00");
}

// ---- retail maps ----------------------------------------------------------------------------------------------------------------------------------

namespace
{
constexpr int kKeyUp = 0xC8, kKeyDown = 0xD0, kKeyLeft = 0xCB, kKeyRight = 0xCD, kKeyKp4 = 0x4B, kKeyKp6 = 0x4D, kKeyKp8 = 0x48, kKeyKp2 = 0x50;

struct CamRig
{
	SharedWorld &sh;
	ArchiveW3DFileSource source;
	WW3DAssetManager assets;
	std::unique_ptr<LiveGame> game;
	CameraSettings gd;
	std::unique_ptr<TacticalCamera> cam;
	std::unique_ptr<HudInput> input;
	Coord3D start;
	unsigned nowMs = 1000;
	int timeMs = 1000;

	explicit CamRig(SharedWorld &s, const char *mapName = "map mp fall back 4p")
		: sh(s)
		, source(*s.mount->fs)
		, assets(source)
	{
		game = std::make_unique<LiveGame>(*s.world, *s.mount->fs, assets, s.options);
		LiveGame::Options o;
		o.mapName = mapName;
		o.seed = 4711;
		o.slots.players.push_back({ "Player_1", "FactionMen", true, 0, 0, 0 });
		o.slots.players.push_back({ "Player_2", "FactionMordor", false, 1, 0, 1 });
		std::string err;
		REQUIRE_MESSAGE(game->load(o, &err), err);
		Player *local = game->players().findPlayerWithName("Player_1");
		REQUIRE(local != nullptr);
		game->players().setLocalPlayer(local);
		REQUIRE_MESSAGE(CameraSettings::load(*s.mount->fs, gd, &err), err);
		cam = std::make_unique<TacticalCamera>(gd);
		cam->setViewport(1024, 768);
		REQUIRE(game->logic().terrain() != nullptr);
		const Waypoint *w = game->logic().terrain()->findWaypointByName("Player_1_Start");
		REQUIRE(w != nullptr);
		start = w->location;
		const LoadedMap &lm = game->map();
		REQUIRE(lm.hasHeightMap);
		cam->startMap(game->logic(), lm.heightMap, lm.chunks.hasWorldInfo ? &lm.chunks.worldInfo : nullptr, start);
		input = std::make_unique<HudInput>(game->logic(), &game->ai(), *cam, game->commands(), s.mouse, s.meta);
		input->attachCamera(*cam);
	}
	~CamRig()
	{
		input.reset();
		game->logic().reset();
	}
	// one client frame (33 ms) after the queued input
	void frame(int n = 1)
	{
		for (int i = 0; i < n; ++i)
		{
			input->update();
			nowMs += 33;
			input->cameraFrame(nowMs);
		}
	}
	void key(int code, bool down)
	{
		input->key(code, down ? 2 : 1);
		input->update();
	}
	void move(int x, int y)
	{
		input->mouseMove(x, y, 0);
		input->update();
	}
	void button(HudInput::Button b, bool down, int x, int y)
	{
		timeMs += 20;
		input->mouseButton(b, down, x, y, 0, timeMs);
		input->update();
	}
	Coord3D pos() const { return cam->position(); }
};
} // namespace

TEST_CASE("camera: the install's GameData camera fields")
{
	if (!haveWorld("camera gamedata"))
	{
		return;
	}
	CameraSettings s;
	std::string err;
	REQUIRE_MESSAGE(CameraSettings::load(*shared().mount->fs, s, &err), err);
	CHECK(s.defaultMinHeight == 120.0f);
	CHECK(s.defaultMaxHeight == 300.0f);
	CHECK(s.defaultPitchAngle == 37.5f);
	CHECK(s.defaultYawAngle == 0.0f);
	CHECK(s.defaultScrollSpeedScalar == 1.0f);
	CHECK(s.lockHeightDelta == 150.0f);
	CHECK(s.terrainSampleRadius == 1.0f);
	CHECK(s.scrollAmountCutoff == 50.0f);
	CHECK(s.cameraAdjustSpeed == 0.3f);
	CHECK_FALSE(s.enforceMaxCameraHeight);
	CHECK_FALSE(s.useCameraInReplay);
	CHECK(s.horizontalScrollSpeedFactor == 0.4f);
	CHECK(s.verticalScrollSpeedFactor == 0.5f);
	CHECK(s.screenEdgeScrollSpeedFactor == 1.0f);
	CHECK(s.screenEdgeScrollRampTimeMs == 250);
	CHECK(s.keyboardScrollSpeedFactor == 1.0f);
	CHECK(s.keyboardCameraRotateSpeed == 0.1f);
	CHECK(s.partitionCellSize == 40.0f);
	// CameraEaseFactor is commented out in the retail GameData and KeyboardDefaultScrollSpeedFactor is absent: the constructor defaults
	CHECK(s.easeFactor == 0.2f);
	CHECK(s.keyboardDefaultScrollSpeedFactor == 1.0f);
}

TEST_CASE("camera: the camera starts on the start position at the default view of the retail map")
{
	if (!haveWorld("camera start"))
	{
		return;
	}
	CamRig r(shared());
	TacticalCamera &c = *r.cam;
	// the map's camera values: this map keeps GameData's (the independent survey: max 300, pitch 37.5, yaw 0, scroll 1)
	INFO("overridden keys: " << c.mapValues().overridden.size());
	CHECK(c.mapValues().pitchAngle == 37.5f);
	CHECK(c.mapValues().yawAngle == 0.0f);
	CHECK(c.mapValues().maxHeight == 300.0f);
	CHECK(c.minHeight() == c.mapValues().minHeight);
	// the camera looks at the start position (the constraint can only move it inwards)
	float lim[4];
	c.constraint(lim);
	REQUIRE(c.constraintValid());
	CHECK(c.position().x == doctest::Approx(std::min(std::max(r.start.x, lim[0]), lim[2])));
	CHECK(c.position().y == doctest::Approx(std::min(std::max(r.start.y, lim[1]), lim[3])));
	CHECK(c.getAngle() == 0.0f);
	CHECK(c.getHeightAboveGround() == c.maxHeight());
	// the default zoom puts the eye at the maximum height above the smoothed terrain (the height field): eye height = max + field height
	const float terrain = c.heightField().ready() ? c.heightField().sample(c.position().x, c.position().y) : c.heightAroundPos(c.position().x, c.position().y);
	CHECK(c.eye().z == doctest::Approx(c.maxHeight() + terrain).epsilon(1e-4));
	CHECK(c.target().x == doctest::Approx(c.position().x));
	CHECK(c.target().y == doctest::Approx(c.position().y));
	// north up, the camera south of the target at 37.5 degrees above the horizon
	CHECK(c.eye().x == doctest::Approx(c.target().x).epsilon(1e-4));
	CHECK(c.eye().y < c.target().y);
	const float rise = c.eye().z - c.target().z, back = c.target().y - c.eye().y;
	CHECK(rise / back == doctest::Approx(std::tan(37.5 * 0.017453293005625408)).epsilon(1e-4));
	// the horizontal field of view is 50 degrees; at 1024 x 768 the vertical one follows the aspect
	CHECK(c.horizontalFov() == doctest::Approx(0.8726646).epsilon(1e-6));
	CHECK(c.verticalFov() == doctest::Approx(2.0 * std::atan(std::tan(25.0 * 0.017453293005625408) / (1024.0 / 768.0))).epsilon(1e-5));
	// the look-at point is the centre of the screen
	ICoord2D px;
	REQUIRE(c.worldToScreen({ c.target().x, c.target().y, c.target().z }, px));
	CHECK(std::abs(px.x - 512) <= 1);
	CHECK(std::abs(px.y - 384) <= 1);
	// an idle camera stays where it is: the zoom is already the one the follow wants
	const float z0 = c.getZoom();
	r.frame(120);
	CHECK(c.getZoom() == doctest::Approx(z0).epsilon(1e-4));
	// the camera was not turned
	CHECK(r.input->lookAt()->isScrolling() == false);
}

TEST_CASE("camera: the wheel zooms by the retail steps between the retail height limits")
{
	if (!haveWorld("camera zoom"))
	{
		return;
	}
	CamRig r(shared());
	TacticalCamera &c = *r.cam;
	// one notch in: height * 0.96 - 1.0 (RW 0x65E803)
	r.input->mouseWheel(1, 500, 400);
	r.input->update();
	CHECK(c.getHeightAboveGround() == 300.0f * 0.96f - 1.0f);
	float h = c.getHeightAboveGround();
	for (int i = 0; i < 4; ++i)
	{
		r.input->mouseWheel(1, 500, 400);
		h = h * 0.96f - 1.0f;
	}
	r.input->update();
	CHECK(c.getHeightAboveGround() == h);
	// the height is limited to the map's minimum (120)
	for (int i = 0; i < 200; ++i)
	{
		c.zoomIn();
	}
	CHECK(c.getHeightAboveGround() == c.minHeight());
	CHECK(c.minHeight() == 120.0f);
	// the zoom follows with CameraAdjustSpeed: after a few dozen frames the eye is at the minimum height above the terrain
	r.frame(60);
	const float terrain = c.terrainHeightUnderCamera();
	CHECK(c.eye().z == doctest::Approx(120.0f + terrain).epsilon(1e-3));
	// one notch out: height * 1.05 + 1.0 (RW 0x65E82A), limited to the maximum (300)
	r.input->mouseWheel(-1, 500, 400);
	r.input->update();
	CHECK(c.getHeightAboveGround() == 120.0f * 1.05f + 1.0f);
	for (int i = 0; i < 200; ++i)
	{
		c.zoomOut();
	}
	CHECK(c.getHeightAboveGround() == 300.0f);
	// the numpad: 8 zooms in, 2 out, one step per client frame while held
	r.key(kKeyKp8, true);
	r.frame(3);
	r.key(kKeyKp8, false);
	CHECK(c.getHeightAboveGround() < 300.0f);
	const float low = c.getHeightAboveGround();
	r.key(kKeyKp2, true);
	r.frame(2);
	r.key(kKeyKp2, false);
	CHECK(c.getHeightAboveGround() > low);
}

TEST_CASE("camera: the arrow keys scroll by the retail tick and the speed grows with the zoom")
{
	if (!haveWorld("camera keys"))
	{
		return;
	}
	CamRig r(shared());
	TacticalCamera &c = *r.cam;
	const Coord3D p0 = c.position();
	// right: KeyboardScrollSpeedFactor * HorizontalScrollSpeedFactor * 100 = 40 per frame; the move is that times zoom * 0.25 * scalar along the right vector (+x with the camera unturned)
	r.key(kKeyRight, true);
	REQUIRE(r.input->lookAt()->isScrolling());
	CHECK(r.input->lookAt()->scrollType() == LookAtTranslator::SCROLL_KEY);
	CHECK(r.input->ui().isScrolling());
	const float zoom = c.getZoom();
	r.frame(1);
	CHECK(r.input->lookAt()->offsetX() == doctest::Approx(40.0f));
	CHECK(r.input->lookAt()->offsetY() == doctest::Approx(0.0f));
	CHECK(c.position().x - p0.x == doctest::Approx(40.0f * zoom * 0.25f * c.mapValues().scrollSpeedScalar).epsilon(0.01));
	CHECK(c.position().y == doctest::Approx(p0.y).epsilon(1e-3));
	r.key(kKeyRight, false);
	CHECK_FALSE(r.input->lookAt()->isScrolling());
	CHECK_FALSE(r.input->ui().isScrolling());
	// up: -(1.0 * 0.5 * 100) = -50 on y is "forward": +y on the map
	const Coord3D p1 = c.position();
	r.key(kKeyUp, true);
	r.frame(1);
	CHECK(r.input->lookAt()->offsetY() == doctest::Approx(-50.0f));
	CHECK(c.position().y > p1.y);
	r.key(kKeyUp, false);
	// left and down move the other way
	const Coord3D p2 = c.position();
	r.key(kKeyLeft, true);
	r.key(kKeyDown, true);
	r.frame(1);
	CHECK(c.position().x < p2.x);
	CHECK(c.position().y < p2.y);
	r.key(kKeyLeft, false);
	r.key(kKeyDown, false);
	// zoomed in (zoom smaller) the same key moves less
	for (int i = 0; i < 200; ++i)
	{
		c.zoomIn();
	}
	r.frame(60);
	const float zIn = c.getZoom();
	CHECK(zIn < zoom);
	const Coord3D p3 = c.position();
	r.key(kKeyRight, true);
	r.frame(1);
	const float moved = c.position().x - p3.x;
	r.key(kKeyRight, false);
	CHECK(moved == doctest::Approx(40.0f * zIn * 0.25f).epsilon(0.02));
}

TEST_CASE("camera: the right button scrolls only when dragged beyond the drag tolerance")
{
	if (!haveWorld("camera rmb"))
	{
		return;
	}
	CamRig r(shared());
	TacticalCamera &c = *r.cam;
	LookAtTranslator &la = *r.input->lookAt();
	const int tolerance = shared().mouse.dragTolerance;
	REQUIRE(tolerance > 0);
	// a click: press, a small move, release: nothing scrolls
	const Coord3D p0 = c.position();
	r.move(500, 400);
	r.button(HudInput::Button::Right, true, 500, 400);
	CHECK(la.isArmed());
	CHECK_FALSE(la.isScrolling());
	r.move(500 + tolerance, 400);
	CHECK_FALSE(la.isScrolling());
	r.frame(2);
	r.button(HudInput::Button::Right, false, 500 + tolerance, 400);
	CHECK_FALSE(la.isArmed());
	CHECK(c.position().x == p0.x);
	CHECK(c.position().y == p0.y);
	// a drag: one pixel beyond the tolerance starts the scroll (type 1); the move per frame is (pointer - anchor) * (0.4, 0.5) plus the unit direction * the same factors
	r.button(HudInput::Button::Right, true, 500, 400);
	r.move(500 + tolerance + 1, 400);
	REQUIRE(la.isScrolling());
	CHECK(la.scrollType() == LookAtTranslator::SCROLL_RMB);
	r.move(600, 400);
	const Coord3D p1 = c.position();
	const float zoom = c.getZoom();
	r.frame(1);
	// dx = 100 -> offset.x = 100 * 0.4 + 0.4 * 1.0 * 1.0 * (unit x = 1) = 40.4 ; y = 0
	CHECK(la.offsetX() == doctest::Approx(40.4f).epsilon(1e-4));
	CHECK(la.offsetY() == doctest::Approx(0.0f));
	CHECK(c.position().x - p1.x == doctest::Approx(40.4f * zoom * 0.25f).epsilon(0.02));
	// the release stops it
	r.button(HudInput::Button::Right, false, 600, 400);
	CHECK_FALSE(la.isScrolling());
	CHECK(la.scrollType() == LookAtTranslator::SCROLL_NONE);
	const Coord3D p2 = c.position();
	r.frame(3);
	CHECK(c.position().x == p2.x);
}

TEST_CASE("camera: the screen edge scrolls with a ramp of ScreenEdgeScrollRampTime")
{
	if (!haveWorld("camera edge"))
	{
		return;
	}
	CamRig r(shared());
	TacticalCamera &c = *r.cam;
	LookAtTranslator &la = *r.input->lookAt();
	// windowed (GlobalData Windowed): the edge does nothing
	la.setEdgeScrollEnabled(false);
	r.move(1023, 400);
	CHECK_FALSE(la.isScrolling());
	la.setEdgeScrollEnabled(true);
	r.move(1000, 400);
	la.setTime(r.nowMs); // retail reads timeGetTime when the scroll starts
	r.move(1023, 400); // x >= width - 3
	REQUIRE(la.isScrolling());
	CHECK(la.scrollType() == LookAtTranslator::SCROLL_SCREENEDGE);
	// the first frame is 33 ms into the 250 ms ramp: 33 * 100 / 250 = 13 percent of 1.0 * 1.0 * 0.4
	const float x0 = c.position().x;
	r.frame(1);
	CHECK(la.offsetX() == doctest::Approx(1.0f * 1.0f * 0.4f * 13.0f).epsilon(1e-4));
	CHECK(c.position().x > x0);
	// after the ramp it is the full 40 per frame
	r.frame(10);
	CHECK(la.offsetX() == doctest::Approx(40.0f).epsilon(1e-4));
	// leaving the edge stops it
	r.move(500, 400);
	CHECK_FALSE(la.isScrolling());
	// the top edge scrolls forward (y = 0 -> offset.y negative = up the map)
	const float y0 = c.position().y;
	r.move(500, 0);
	REQUIRE(la.isScrolling());
	r.frame(12);
	CHECK(la.offsetY() == doctest::Approx(-50.0f).epsilon(1e-4));
	CHECK(c.position().y > y0);
	r.move(500, 400);
}

TEST_CASE("camera: the middle button rotates, a middle click resets the view")
{
	if (!haveWorld("camera rotate"))
	{
		return;
	}
	CamRig r(shared());
	TacticalCamera &c = *r.cam;
	LookAtTranslator &la = *r.input->lookAt();
	r.move(500, 400);
	r.button(HudInput::Button::Middle, true, 500, 400);
	CHECK(la.isRotating());
	// 100 pixels to the right = 100 * 0.005 radians
	r.move(600, 400);
	CHECK(c.getAngle() == doctest::Approx(0.5f).epsilon(1e-4));
	// the eye turned with it: it is no longer straight south of the target
	CHECK(std::fabs(c.eye().x - c.target().x) > 10.0f);
	r.move(550, 400);
	CHECK(c.getAngle() == doctest::Approx(0.25f).epsilon(1e-4));
	r.button(HudInput::Button::Middle, false, 550, 400);
	CHECK_FALSE(la.isRotating());
	// not a click (moved); the angle stays
	CHECK(c.getAngle() == doctest::Approx(0.25f).epsilon(1e-4));
	// the numpad turns the camera by KeyboardCameraRotateSpeed per frame
	r.key(kKeyKp6, true);
	r.frame(2);
	r.key(kKeyKp6, false);
	CHECK(c.getAngle() == doctest::Approx(0.25f + 0.2f).epsilon(1e-4));
	r.key(kKeyKp4, true);
	r.frame(4);
	r.key(kKeyKp4, false);
	CHECK(c.getAngle() == doctest::Approx(0.25f + 0.2f - 0.4f).epsilon(1e-4));
	// a click on the middle button (same place, within 5 client frames) resets: angle 0, the default zoom
	c.zoomIn();
	c.zoomIn();
	r.button(HudInput::Button::Middle, true, 500, 400);
	r.frame(1);
	r.button(HudInput::Button::Middle, false, 500, 400);
	CHECK(c.getAngle() == 0.0f);
	CHECK(c.getHeightAboveGround() == c.maxHeight());
	// a long press is not a click
	r.button(HudInput::Button::Middle, true, 500, 400);
	r.move(520, 400);
	r.button(HudInput::Button::Middle, false, 520, 400);
	c.zoomIn();
	r.button(HudInput::Button::Middle, true, 500, 400);
	r.frame(6);
	r.button(HudInput::Button::Middle, false, 500, 400);
	CHECK(c.getHeightAboveGround() < c.maxHeight());
}

TEST_CASE("camera: the map's playable area limits the camera (border clamping)")
{
	if (!haveWorld("camera border"))
	{
		return;
	}
	CamRig r(shared());
	TacticalCamera &c = *r.cam;
	float maxX = 0, maxY = 0;
	REQUIRE(r.game->logic().terrain()->getExtent(0, maxX, maxY));
	float lim[4];
	c.constraint(lim);
	REQUIRE(c.constraintValid());
	// the constraint is the playable extent shrunk by the ground footprint of the view (the distance between the centre pick and the 95 percent pick), at most a quarter of the width
	const float off = lim[0];
	CHECK(off >= 0.0f);
	CHECK(off <= maxX * 0.25f);
	CHECK(lim[1] == doctest::Approx(off).epsilon(1e-5));
	CHECK(lim[2] == doctest::Approx(maxX - off).epsilon(1e-5));
	CHECK(lim[3] == doctest::Approx(maxY - off).epsilon(1e-5));
	INFO("extent " << maxX << " x " << maxY << ", constraint " << lim[0] << " " << lim[1] << " " << lim[2] << " " << lim[3]);
	// scrolling far to every side stops at the constraint and reports the edge bits (a positive y delta is down the screen: south)
	int mask = 0;
	for (int i = 0; i < 400; ++i)
	{
		mask = c.scrollBy(-1000.0f, 0.0f);
	}
	CHECK((mask & 1) != 0);
	CHECK(c.position().x == doctest::Approx(c.constraintValid() ? lim[0] : 0.0f).epsilon(1e-3));
	for (int i = 0; i < 400; ++i)
	{
		mask = c.scrollBy(1000.0f, 0.0f);
	}
	CHECK((mask & 2) != 0);
	c.constraint(lim);
	CHECK(c.position().x == doctest::Approx(lim[2]).epsilon(1e-3));
	for (int i = 0; i < 400; ++i)
	{
		mask = c.scrollBy(0.0f, 1000.0f);
	}
	CHECK((mask & 4) != 0);
	c.constraint(lim);
	CHECK(c.position().y == doctest::Approx(lim[1]).epsilon(1e-3));
	for (int i = 0; i < 400; ++i)
	{
		mask = c.scrollBy(0.0f, -1000.0f);
	}
	CHECK(((mask & 8) != 0) == true);
	c.constraint(lim);
	CHECK(c.position().y <= lim[3] + 1e-3f);
	// a jump (radar click, View::lookAt) outside is clamped the same way
	c.lookAt(Coord3D{ -5000.0f, -5000.0f, 0.0f });
	c.constraint(lim);
	CHECK(c.position().x == doctest::Approx(lim[0]).epsilon(1e-3));
	CHECK(c.position().y == doctest::Approx(lim[1]).epsilon(1e-3));
}

TEST_CASE("camera: a jump puts the point in the middle of the screen and the camera follows the terrain height")
{
	if (!haveWorld("camera jump"))
	{
		return;
	}
	CamRig r(shared());
	TacticalCamera &c = *r.cam;
	float maxX = 0, maxY = 0;
	REQUIRE(r.game->logic().terrain()->getExtent(0, maxX, maxY));
	// the highest and the lowest cell of the height field on the map, away from the borders
	float lo = 1e9f, hi = -1e9f;
	Coord3D low{}, high{};
	const CameraHeightField &f = c.heightField();
	REQUIRE(f.ready());
	float lim[4];
	c.constraint(lim);
	for (int gy = 4; gy < f.gridHeight() - 4; gy += 2)
	{
		for (int gx = 4; gx < f.gridWidth() - 4; gx += 2)
		{
			// the cell (gx, gy) is centred on the height samples (4 gx .. 4 gx + 3): x' = (x + border * 10) / 40 is an integer there
			const float border = (float)r.game->map().heightMap.getBorderSize() * 10.0f;
			const float x = (float)gx * 40.0f - border, y = (float)gy * 40.0f - border;
			if (x < lim[0] + 50 || x > lim[2] - 50 || y < lim[1] + 50 || y > lim[3] - 50)
			{
				continue;
			}
			const float h = f.cell(gx, gy);
			if (h < lo)
			{
				lo = h;
				low = Coord3D{ x, y, 0 };
			}
			if (h > hi)
			{
				hi = h;
				high = Coord3D{ x, y, 0 };
			}
		}
	}
	INFO("lowest " << lo << " highest " << hi);
	REQUIRE(hi > lo + 1.0f);
	for (const Coord3D &where : { low, high })
	{
		c.lookAt(where);
		CHECK(c.position().x == doctest::Approx(where.x));
		CHECK(c.position().y == doctest::Approx(where.y));
		ICoord2D px;
		REQUIRE(c.worldToScreen({ c.target().x, c.target().y, c.target().z }, px));
		CHECK(std::abs(px.x - 512) <= 1);
		CHECK(std::abs(px.y - 384) <= 1);
		r.frame(100);
		// the follow: the eye is at the height above the terrain under the camera again (not scrolling: no ground level change, the zoom settles)
		CHECK(c.eye().z == doctest::Approx(c.getHeightAboveGround() + c.terrainHeightUnderCamera()).epsilon(2e-3));
		CHECK(c.terrainHeightUnderCamera() == doctest::Approx(c.heightField().sample(c.position().x, c.position().y)).epsilon(1e-5));
	}
	// the eye over the high ground is higher than over the low ground by about the difference of the field heights
	c.lookAt(low);
	r.frame(100);
	const float eyeLow = c.eye().z, terrainLow = c.terrainHeightUnderCamera();
	c.lookAt(high);
	r.frame(100);
	INFO("low (" << low.x << ", " << low.y << ") terrain " << terrainLow << " eye " << eyeLow << "; high (" << high.x << ", " << high.y << ") terrain " << c.terrainHeightUnderCamera()
		<< " eye " << c.eye().z << " pos (" << c.position().x << ", " << c.position().y << ") zoom " << c.getZoom() << " offsetZ " << c.cameraOffset().z);
	CHECK(c.eye().z - eyeLow == doctest::Approx(hi - lo).epsilon(0.05));
}

TEST_CASE("camera: the stops are reported when the camera is attached")
{
	if (!haveWorld("camera stops"))
	{
		return;
	}
	CamRig r(shared());
	const std::vector<std::string> stops = r.input->stops();
	std::vector<std::string> mine;
	for (const std::string &s : stops)
	{
		if (s.compare(0, 7, "[S-450]") == 0 || s.compare(0, 7, "[S-451]") == 0 || s.compare(0, 7, "[S-452]") == 0 || s.compare(0, 7, "[S-453]") == 0 || s.compare(0, 7, "[S-454]") == 0
			|| s.compare(0, 7, "[S-455]") == 0 || s.compare(0, 7, "[S-456]") == 0)
		{
			mine.push_back(s);
		}
	}
	REQUIRE(mine.size() == 7);
	CHECK(mine == TacticalCamera::acceptanceStops());
}

TEST_CASE("camera: the render interpolation runs from the pose committed at the end of the previous client frame")
{
	if (!haveWorld("camera interpolation"))
	{
		return;
	}
	CamRig r(shared());
	TacticalCamera &c = *r.cam;
	// after startMap both poses are the start pose (no smear from the constructor's pose)
	CHECK(c.previousEye().x == c.eye().x);
	CHECK(c.previousEye().y == c.eye().y);
	CHECK(c.previousEye().z == c.eye().z);
	CHECK(c.previousTarget().x == c.target().x);
	// a key scroll: the translator moves the camera inside the client frame, before update(); the committed pose still is the one before the frame
	r.key(kKeyRight, true);
	const Coord3D before = c.eye();
	r.frame(1);
	CHECK(c.previousEye().x == before.x);
	CHECK(c.previousEye().y == before.y);
	CHECK(c.previousEye().z == before.z);
	CHECK(c.eye().x > before.x + 1.0f);
	// the next frame: the previous pose is the one the last frame ended on
	const Coord3D mid = c.eye();
	r.frame(1);
	CHECK(c.previousEye().x == mid.x);
	CHECK(c.previousTarget().x == doctest::Approx(mid.x));
	CHECK(c.eye().x > mid.x + 1.0f);
	r.key(kKeyRight, false);
}

// lane MOVE-2 r2: a pose a script sets between client frames (InGameHudNode::camera_look_at / camera_set_height, called every render frame by the viewers) is drawn
// at once: snapInterpolation makes the previous and committed poses the live one, so the interpolation never mixes an old committed pose with the new live one (the
// 30 Hz steps of a camera moved every render frame)
TEST_CASE("camera: a pose set between client frames is drawn at once (snapInterpolation)")
{
	if (!haveWorld("camera snap"))
	{
		return;
	}
	CamRig r(shared());
	TacticalCamera &c = *r.cam;
	r.frame(1);
	const Coord3D t0 = c.target();
	c.lookAt(Coord3D{ t0.x + 50.0f, t0.y, 0.0f });
	CHECK(c.previousEye().x != doctest::Approx(c.eye().x)); // without the snap the drawn pose would still start from the old one
	c.snapInterpolation();
	CHECK(c.previousEye().x == c.eye().x);
	CHECK(c.previousEye().y == c.eye().y);
	CHECK(c.previousTarget().x == c.target().x);
	// the next client frame starts from the snapped pose
	r.frame(1);
	CHECK(c.previousEye().x == doctest::Approx(c.eye().x));
}

// ---- lane PLAY-1 ----------------------------------------------------------------------------------------------------------------------------------

TEST_CASE("play1 camera: the free camera lifts the zoom-out limit to the map's extent, moves the far plane and the fog with the extra distance, and off is retail")
{
	if (!haveWorld("play1 free camera"))
	{
		return;
	}
	CamRig r(shared());
	TacticalCamera &c = *r.cam;
	CHECK_FALSE(c.freeCamera());
	CHECK(c.zoomOutLimit() == c.maxHeight());
	CHECK(c.farPlane() == 1800.0f); // RW 0x48B7B1
	CHECK(c.fogShift() == 0.0f);
	for (int i = 0; i < 200; ++i)
	{
		c.zoomOut();
	}
	CHECK(c.getHeightAboveGround() == c.maxHeight());
	r.frame(60);
	CHECK(c.fogShift() == 0.0f); // at retail's maximum nothing moves
	CHECK(c.farPlane() == 1800.0f);
	c.setFreeCamera(true);
	float mx = 0.0f, my = 0.0f;
	REQUIRE(r.game->logic().terrain()->getExtent(0, mx, my));
	CHECK(c.freeMaxHeight() == std::max(c.maxHeight(), std::max(mx, my)));
	CHECK(c.freeMaxHeight() > 3.0f * c.maxHeight());
	for (int i = 0; i < 400; ++i)
	{
		c.zoomOut();
	}
	CHECK(c.getHeightAboveGround() == c.freeMaxHeight());
	r.frame(90); // the zoom follows with CameraAdjustSpeed
	CHECK(c.eye().z > c.maxHeight() * 2.0f);
	CHECK(c.fogShift() > 0.0f);
	CHECK(c.farPlane() == 1800.0f + 2.0f * c.fogShift());
	// scrolling does not pull the free camera back down (the scroll's EnforceMaxCameraHeight compares with the zoom-out limit)
	const float high = c.getHeightAboveGround();
	r.key(kKeyRight, true);
	r.frame(10);
	r.key(kKeyRight, false);
	CHECK(c.getHeightAboveGround() == high);
	// the default view is still retail's maximum height
	c.setZoomToDefault();
	CHECK(c.getHeightAboveGround() == c.maxHeight());
	// off again: back to the retail limit
	for (int i = 0; i < 400; ++i)
	{
		c.zoomOut();
	}
	c.setFreeCamera(false);
	CHECK(c.getHeightAboveGround() == c.maxHeight());
	r.frame(120);
	CHECK(c.fogShift() == 0.0f);
	CHECK(c.farPlane() == 1800.0f);
}

TEST_CASE("play1 move hint: GameData's MoveHintName is read (GlobalData + 0x10) and the hints age by the camera's client frames")
{
	if (!haveWorld("play1 move hint"))
	{
		return;
	}
	CamRig r(shared());
	CHECK(r.gd.moveHintName == "SCMoveHint");
	InGameUI &ui = r.input->ui();
	ui.createMoveHint(Coord3D{ 100.0f, 200.0f, 5.0f });
	CHECK(ui.liveMoveHintCount() == 1);
	r.frame(InGameUI::MOVE_HINT_FRAMES);
	CHECK(ui.liveMoveHintCount() == 1);
	r.frame(1);
	CHECK(ui.liveMoveHintCount() == 0);
	// 256 slots, round robin (ZH m_nextMoveHint)
	for (int i = 0; i < InGameUI::MAX_MOVE_HINTS + 3; ++i)
	{
		ui.createMoveHint(Coord3D{ (float)i, 0.0f, 0.0f });
	}
	CHECK(ui.liveMoveHintCount() == InGameUI::MAX_MOVE_HINTS);
	// the first hint took slot 0, so these went to slots 1 .. 255, 0, 1, 2, 3
	CHECK(ui.moveHints()[3].pos.x == (float)(InGameUI::MAX_MOVE_HINTS + 2));
	CHECK(ui.moveHints()[4].pos.x == 3.0f);
}

TEST_CASE("play1 mouse setup: RotWK's GlobalData defaults to the alternate setup and Options.ini AlternateMouseSetup reads as RW 0x6E61D4 says")
{
	const PeImage *pe = PeImage::fromEnvironment();
	if (!pe)
	{
		retailtest::printSkip("play1 mouse setup binary facts (RW_GAME_DAT unset)");
		return;
	}
	auto bytes = [&](std::uint32_t va, size_t n) {
		std::vector<std::uint8_t> out;
		REQUIRE(pe->read(va, n, &out));
		return out;
	};
	// GlobalData::GlobalData RW 0x6429AD: eax = 1 (RW 0x6429E1 xor eax, eax; inc eax), then RW 0x642A4B: mov byte ptr [esi + 0x5C], al
	CHECK(bytes(0x6429E1, 3) == std::vector<std::uint8_t>{ 0x33, 0xC0, 0x40 });
	CHECK(bytes(0x642A4B, 3) == std::vector<std::uint8_t>{ 0x88, 0x46, 0x5C });
	// OptionPreferences::getAlternateMouseModeEnabled RW 0x6E61D4: the key, GlobalData + 0x5C when absent, strcmp(value, "yes") != 0 otherwise
	CHECK(pe->cstring(0xC1B2C8) == "AlternateMouseSetup");
	CHECK(pe->cstring(0xBD3D80) == "yes");
	CHECK(bytes(0x6E620A, 3) == std::vector<std::uint8_t>{ 0x8A, 0x40, 0x5C });
	CHECK(bytes(0x6E622D, 3) == std::vector<std::uint8_t>{ 0x0F, 0x95, 0xC0 }); // setne al
	// the start copies the answer into GlobalData (RW 0x641E72 / 0x641E7A)
	CHECK(bytes(0x641E7A, 3) == std::vector<std::uint8_t>{ 0x88, 0x46, 0x5C });
	// lane PLAY-1: MoveHintName's row { name, parseAsciiString RW 0x42EE5E, 0, GlobalData + 0x10 } (RW 0xBFF5C0)
	CHECK(pe->cstring(pe->u32At(0xBFF5C0)) == "MoveHintName");
	CHECK(pe->u32At(0xBFF5C4) == 0x42EE5Eu);
	CHECK(pe->u32At(0xBFF5CC) == 0x10u);
	// lane PLAY-1: the end-game timer: RW 0xBC2BD3 .. 0xBC2BDB [0xDE3BCC] = [0xD9F608] * 5 (LOGICFRAMES_PER_SECOND = 5), RW 0x602FFE stores it at + 0x1A204
	CHECK(pe->u32At(0xD9F608) == 5u);
	CHECK(bytes(0xBC2BD8, 3) == std::vector<std::uint8_t>{ 0x6B, 0xC0, 0x05 });
	CHECK(bytes(0x603003, 6) == std::vector<std::uint8_t>{ 0x89, 0x81, 0x04, 0xA2, 0x01, 0x00 });
}

TEST_CASE("play1 orders: with the tactical camera attached (its LookAt translator in the stream), a left click selects and a right click orders the move")
{
	if (!haveWorld("play1 orders with the camera"))
	{
		return;
	}
	CamRig r(shared());
	r.input->commandTranslator().setUseAlternateMouse(true); // RotWK's default (RW 0x642A4B)
	r.frame(5);
	// a unit of the local player near its start
	Player *local = r.game->players().getLocalPlayer();
	std::string err;
	// the owner's first selection in the game run: the Men's worker (a porter), whose context command is a move too
	Object *unit = r.game->createObject("MenPorter", local->getPlayerIndex(), Coord3D{ r.start.x + 150.0f, r.start.y - 150.0f, 0.0f }, 0.0f, &err);
	REQUIRE_MESSAGE(unit != nullptr, err);
	MESSAGE("unit " << unit->getTemplate()->getName());
	r.cam->lookAt(*unit->getPosition());
	r.frame(30);
	ICoord2D pu{ -1, -1 };
	REQUIRE(r.cam->worldToScreen(*unit->getPosition(), pu));
	r.move(pu.x, pu.y);
	r.button(HudInput::Button::Left, true, pu.x, pu.y);
	r.button(HudInput::Button::Left, false, pu.x, pu.y);
	r.frame(2);
	REQUIRE(r.input->ui().getSelectCount() == 1);
	const ICoord2D pg{ pu.x + 220, pu.y + 60 };
	// the pick of the game (InGameHud: the ray against the drawn model's triangles, lane QA-1): the ground 220 pixels beside the unit picks nothing
	// (the game run's first try clicked a second porter there: a right click on an own unit orders nothing, the scenario now looks for empty ground)
	r.input->context().pickRay = [&](const Object &o, const Coord3D &origin, const Coord3D &dir, float *t) {
		const Drawable *d = r.game->drawables().findByObject(o.getID());
		return d ? DrawablePick::rayTest(*d, origin, dir, t) : DrawablePick::Result::NotDrawn;
	};
	{
		const Object *picked = HudObjects::pickObject(r.input->context(), pg);
		CHECK_MESSAGE(picked == nullptr, "picked " << (picked ? picked->getTemplate()->getName() : std::string()));
	}
	r.move(pg.x, pg.y);
	r.frame(5);
	CHECK(r.input->ui().cursor() == std::string(MouseCursorName::Move));
	r.button(HudInput::Button::Right, true, pg.x, pg.y);
	r.button(HudInput::Button::Right, false, pg.x, pg.y);
	size_t moves = 0;
	for (const std::string &l : r.input->messageLog())
	{
		MESSAGE(l);
		moves += l.compare(0, 13, "MSG_DO_MOVETO") == 0 ? 1 : 0;
	}
	CHECK(moves == 1);
	CHECK(r.input->ui().liveMoveHintCount() == 1);
}
