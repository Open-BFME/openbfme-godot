// OpenBFME. GPL-3.0.
//
// Lane CAMP-2: the picture of RotWK's movies. Every movie of the game (the campaign intros and exits, the logos, the menus' small movies) is On2 VP6
// in EA's chunked container (Data\Movies\*.vp6: "MVhd" + "MV0K" key / "MV0F" inter frames; the "_with_alpha" movies add an "AVhd" stream of the alpha
// as a second VP6 picture, "AV0K" / "AV0F"). The movies carry no sound: the stream plays the audio events named after the file (VideoPlayer.h).
//
// TARGET FACTS (rotwk201_game.exe, caveat S-001): the movie library opens the container (RW 0x5B4FC0 ..) and decodes VP6 with its own On2 decoder
// (RW 0x9A4E50 .. 0x9BD470); the codec tags are "vp60" (all campaign / logo movies), "vp61" (the menus' faction movies) and "VP60" (Mission_Rhudaur_Intro:
// key frames only). The container chunk layout (tag, little-endian size including the 8-byte chunk header) and the MVhd fields are VideoPlayer.h's.
// DONOR FACTS: the VP6 decoding process (range decoder, models, macroblock types and vectors, DC prediction, coefficient tokens, the Huffman mode, the
// VP3 IDCT and loop filter, the bicubic / bilinear block copy filters, the golden frame) follows FFmpeg's decoder (libavcodec vp6.c, vp56.c, vp56data.c,
// vp6data.h, vp3dsp.c, vp6dsp.c, h264chroma_template.c, videodsp_template.c, huffman.c; LGPL-2.1-or-later, used here under GPL-3.0, see NOTICE). The
// EA container's VP6 is stored upside down (FFmpeg's AV_CODEC_ID_VP6, "flip"): this decoder works in the bitstream's orientation and flips the output.
// INFERENCE (stop S-2340): the binary's decoder was not compared with this one; VP6 decoding is fully specified by the bitstream (integer IDCT, integer
// filters), so a conforming decoder reproduces the encoder's reconstruction; tests/test_camp2_vp6.cpp checks frames bit-exact against FFmpeg's.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

class VP6Decoder
{
public:
	VP6Decoder();
	~VP6Decoder();

	// decodes one frame (a MV0K / MV0F chunk's payload); false + *error for a damaged or unsupported frame (the frame is not shown)
	bool decodeFrame(const std::uint8_t *data, size_t size, std::string *error);

	int codedWidth() const { return m_width; }   ///< 16 * the stored macroblock columns
	int codedHeight() const { return m_height; } ///< 16 * the stored macroblock rows
	bool hasFrame() const { return m_hasFrame; }
	bool lastWasKey() const { return m_key; }

	// the last decoded frame, display orientation, top-left `width` x `height` of the coded picture: Y, U, V planes (U / V at half size, rounded up)
	void copyPlanes(std::vector<std::uint8_t> &y, std::vector<std::uint8_t> &u, std::vector<std::uint8_t> &v) const;
	// RGBA8, display orientation; BT.601 limited range as FFmpeg's default yuv420p -> rgb conversion. `alpha` (optional) is another decoder of the
	// same size whose luma is the alpha (the "_with_alpha" movies' AVhd stream)
	void toRGBA(int width, int height, std::uint8_t *rgba, const VP6Decoder *alpha = nullptr) const;
	// the luma sample at (x, y) in display orientation (tests)
	std::uint8_t lumaAt(int x, int y) const;

private:
	struct Impl;
	Impl *m;
	int m_width = 0, m_height = 0;
	bool m_hasFrame = false, m_key = false;
};

// EA's VP6 container: the chunks of one movie file (read whole), frame by frame
struct EAVP6Chunk
{
	std::uint32_t offset = 0; ///< the payload's offset in the file
	std::uint32_t size = 0;   ///< the payload's size (the chunk size minus its 8-byte header)
	bool key = false;
};

struct EAVP6Movie
{
	std::string codec;                 ///< MVhd's codec tag ("vp60", "vp61", "VP60")
	std::uint32_t width = 0, height = 0, frames = 0, largestChunk = 0;
	std::uint32_t rateNumerator = 0, rateDenominator = 0;
	std::vector<EAVP6Chunk> video;     ///< MV0K / MV0F in file order
	std::vector<EAVP6Chunk> alpha;     ///< AV0K / AV0F (the "_with_alpha" movies; empty otherwise)
	bool hasAlpha = false;

	// parses the chunk list; false + *error for anything retail's container would not be (an unknown chunk is an error, not skipped)
	// the largest picture side VP6 can code: its frame header stores the macroblock rows and columns in one byte each (255 x 16 pixels)
	static constexpr std::uint32_t kMaxDimension = 255 * 16;

	static bool parse(const std::uint8_t *data, size_t size, EAVP6Movie &out, std::string *error);
	// the same from a file, reading only the chunk headers (a campaign movie is up to 75 MB)
	static bool parseFile(const std::string &path, EAVP6Movie &out, std::string *error);
	double durationMs() const;
};
