// PORT-100 (issue fork issue 260): LargeGroupAudio/HandOffModeDuration — the hand-off of crowd sounds.
// Synthetic INI only, no game files. Expectations from RotWK:
//   * the field's own parser RW 0x73B071: ms * 0.005, ceiling, stored as one byte at map + 0xE4; over 255 frames is a load error
//     ("Duration %u must be less than %d frames", RW 0xC25100). retail maps use 3000 ms = 15 frames.
//   * when a playing cell's sound is displaced to another cell, the receiving cell's counter is the map's HandOffModeDuration
//     (RW 0x9721C7: [pair + 0x3C] -> [map + 0xE4], set by RW 0x9D8668 at cell + 0x46), and the sound then glides by
//     1/steps of the remaining way per logic frame until the counter runs out (GridCell update RW 0x9D8018);
//     at counter 0 (no field) the sound follows at MaximumAudioSpeed instead (RW 0x9D80CF..).
// The movement is LargeGroupAudioHandOff::advance, the one path LiveGameAudio::largeGroupUpdate moves its group sounds with;
// the transfer of the handle between cells is in that same pass (LiveGameAudio.cpp, "Port 100").

#include "doctest.h"

#include "IniTestUtil.h"

#include "Common/Audio/AudioIni.h"
#include "Common/Audio/LargeGroupAudio.h"

namespace
{
struct Fixture
{
	initest::Fixture fx;
	AudioIniState state;
	Fixture() { state.registerBlocks(fx.env.blocks); }
	std::string load(const std::string &text) { return initest::loadError(fx.env, "synthetic.ini", text); }
};

const char kBase[] =
	"AudioEvent DefaultSoundEffect\n  Volume = 100\n  Priority = high\n  Limit = 4\n  MinRange = 160\n  MaxRange = 640\n"
	"  PitchShift = -0 0\n  PlayPercent = 100\n  Type = ui player\nEnd\n"
	"AudioEvent Loop\n  Sounds = a\nEnd\n";

std::string mapWithHandOff(const char *durationLine)
{
	return std::string(kBase) + "LargeGroupAudioMap Fire\n  Sound\n    Sound = Loop\n    Key = Orc\n  End\n  Size = 100\n"
		"  StartThreshold = 3\n  StopThreshold = 2\n" + durationLine + "  MaximumAudioSpeed = 45\nEnd\n";
}
} // namespace

TEST_CASE("PORT-100 HandOffModeDuration parse: 3000 ms is 15 frames, truncated, over 255 frames is RotWK's load error (RW 0x73B071)")
{
	Fixture f;
	REQUIRE_MESSAGE(f.load(mapWithHandOff("  HandOffModeDuration = 3000\n")).empty(), "load");
	const LargeGroupAudioMap *m = f.state.largeGroupAudio.find("Fire");
	REQUIRE(m != nullptr);
	CHECK(m->handOffModeDurationFrames == 15u); // retail value (largegroupaudio.ini: SmallFireLoop and the other loops)

	// the ceiling of the generic duration reading: 3100 ms * 0.005 = 15.5 -> 16 frames (RW 0x73B09C..0x73B0A7)
	Fixture g;
	REQUIRE_MESSAGE(g.load(mapWithHandOff("  HandOffModeDuration = 3100\n")).empty(), "load");
	CHECK(g.state.largeGroupAudio.find("Fire")->handOffModeDurationFrames == 16u);

	// one byte at map + 0xE4: 52000 ms = 260 frames does not fit (RW 0x73B0B4..)
	Fixture h;
	const std::string err = h.load(mapWithHandOff("  HandOffModeDuration = 52000\n"));
	CHECK(err.find("Duration 52000 must be less than 255 frames") != std::string::npos);

	// no field: 0, the counter a fresh sound starts with (RW 0x972238..)
	Fixture n;
	REQUIRE_MESSAGE(n.load(mapWithHandOff("")).empty(), "load");
	CHECK(n.state.largeGroupAudio.find("Fire")->handOffModeDurationFrames == 0u);
}

TEST_CASE("PORT-100 hand-off glide: a handed-off sound reaches the new cell in exactly HandOffModeDuration frames (RW 0x9D8018)")
{
	Fixture f;
	REQUIRE_MESSAGE(f.load(mapWithHandOff("  HandOffModeDuration = 3000\n")).empty(), "load");
	const unsigned frames = f.state.largeGroupAudio.find("Fire")->handOffModeDurationFrames;
	REQUIRE(frames == 15u);

	// the sound is handed off at (0, 0, 0); the receiving cell's preferred position is (150, 30, 15)
	LargeGroupAudioHandOff handOff;
	handOff.begin(frames);
	Coord3D pos{ 0.0f, 0.0f, 0.0f };
	const Coord3D target{ 150.0f, 30.0f, 15.0f };
	pos = handOff.advance(pos, target, 9.0f); // MaximumAudioSpeed must not cut the glide short
	CHECK(pos.x == doctest::Approx(10.0f));   // 1/15 of the way // tolerance: float32 glide steps (RW 0x9D8018 arithmetic in float)
	CHECK(pos.y == doctest::Approx(2.0f)); // tolerance: float32 glide steps (RW 0x9D8018 arithmetic in float)
	CHECK(pos.z == doctest::Approx(1.0f)); // tolerance: float32 glide steps (RW 0x9D8018 arithmetic in float)
	CHECK(handOff.steps == 14u);
	for (int i = 1; i < 15; ++i)
	{
		pos = handOff.advance(pos, target, 9.0f);
	}
	CHECK(pos.x == doctest::Approx(150.0f).epsilon(0.001)); // exactly there on the 15th frame // tolerance: float32 glide steps (RW 0x9D8018 arithmetic in float)
	CHECK(pos.y == doctest::Approx(30.0f).epsilon(0.001)); // tolerance: float32 glide steps (RW 0x9D8018 arithmetic in float)
	CHECK(pos.z == doctest::Approx(15.0f).epsilon(0.001)); // tolerance: float32 glide steps (RW 0x9D8018 arithmetic in float)
	CHECK_FALSE(handOff.active());
	// after the glide the counter is spent: the next move is the MaximumAudioSpeed clamp again
	const Coord3D next = handOff.advance(pos, Coord3D{ 240.0f, 30.0f, 15.0f }, 9.0f);
	CHECK(next.x == doctest::Approx(159.0f)); // tolerance: float32 glide steps (RW 0x9D8018 arithmetic in float)
}

TEST_CASE("PORT-100 no hand-off (counter 0): the sound follows at MaximumAudioSpeed, as before (RW 0x9D80CF..)")
{
	LargeGroupAudioHandOff handOff; // no begin(): HandOffModeDuration absent or 0
	CHECK_FALSE(handOff.active());
	const Coord3D far = handOff.advance(Coord3D{ 0.0f, 0.0f, 0.0f }, Coord3D{ 30.0f, 40.0f, 0.0f }, 10.0f);
	CHECK(far.x == doctest::Approx(6.0f)); // 10 units along the 50-unit way // tolerance: float32 glide steps (RW 0x9D8018 arithmetic in float)
	CHECK(far.y == doctest::Approx(8.0f)); // tolerance: float32 glide steps (RW 0x9D8018 arithmetic in float)
	const Coord3D near = handOff.advance(Coord3D{ 0.0f, 0.0f, 0.0f }, Coord3D{ 3.0f, 4.0f, 0.0f }, 10.0f);
	CHECK(near.x == doctest::Approx(3.0f)); // inside one frame's speed: at the preferred position at once // tolerance: float32 glide steps (RW 0x9D8018 arithmetic in float)
	CHECK(near.y == doctest::Approx(4.0f)); // tolerance: float32 glide steps (RW 0x9D8018 arithmetic in float)
}
