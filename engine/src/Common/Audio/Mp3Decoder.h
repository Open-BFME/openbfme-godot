// OpenBFME. GPL-3.0.
//
// MPEG-1 / MPEG-2 / MPEG-2.5 Layer III decoder, written from the ISO/IEC 11172-3 and 13818-3 algorithms
// (side information, scalefactors, Huffman, requantisation, M/S and intensity stereo, alias reduction,
// IMDCT with window switching, frequency inversion, polyphase synthesis). The constant tables that the
// standard defines numerically (Huffman codes, the synthesis window) are in Mp3Tables.cpp.
//
// Target facts: the retail music, speech and ambient streams are MPEG-1 Layer III, 44.1 / 48 kHz, CBR
// 64-256 kbit/s, mono / stereo / joint stereo (MS only), plus one MPEG-2 22.05 kHz mono file; retail plays
// them through Miles' mssmp3.asi, whose rounding cannot be reproduced bit for bit, so this is a float
// decoder verified against mpg123 and ffmpeg (within 2 LSB peak, below 1 LSB RMS after rounding; see
// tools/audio/oracle_check.py). It is a client-side audio path, not part of the deterministic simulation.
//
// Instances are independent (no global mutable state; the constant tables are built once, read-only).
//
// Gapless handling (Decision): the decoder delay of 528 + 1 samples is NOT trimmed, because a plain MP3
// carries no information about it. When the first frame is a Xing / Info frame with a LAME tag, the encoder
// delay and padding it records are applied (start skip = delay + 528 + 1, end trim = padding - 528 - 1), the
// same as mpg123 and ffmpeg do; that frame itself is never decoded as audio. Retail files have no such tag.
//
// Defects (reported through `warnings`, decisions marked in Mp3Decoder.cpp): bytes between ID3v2 tag and the
// first frame, junk between frames, trailing bytes after the last frame (an ID3v1 tag is standard and not
// reported), a truncated last frame (dropped), CRC-protected frames (the CRC is skipped, not verified).
// Everything else that is malformed (reserved header bits, free-format, Layer I/II, a stream parameter change,
// bit reservoir underflow, impossible side information, Huffman overrun) throws AudioDecodeError.

#pragma once

#include "Common/Audio/AudioDecode.h"

#include <memory>
#include <string>
#include <vector>

namespace AudioDecode
{

struct Mp3FrameHeader
{
	uint8_t version = 0;         // 1 = MPEG-1, 2 = MPEG-2, 25 = MPEG-2.5
	bool crcProtected = false;   // 2 CRC bytes follow the header
	uint32_t bitrate = 0;        // bit/s
	uint32_t sampleRate = 0;
	bool padding = false;
	uint8_t channelMode = 0;     // 0 stereo, 1 joint stereo, 2 dual channel, 3 mono
	uint8_t modeExtension = 0;   // joint stereo: bit0 intensity, bit1 mid/side
	uint16_t channels = 0;
	uint32_t frameBytes = 0;     // whole frame including the 4 header bytes
	uint32_t samplesPerFrame = 0; // 1152 (MPEG-1) or 576
	uint32_t sideInfoBytes = 0;
	uint32_t headerBytes() const { return crcProtected ? 6u : 4u; }
};

// Parses 4 header bytes. Returns false (with *why describing the first problem) when they are not a Layer III
// frame header this decoder can handle (bad sync, reserved version, Layer I/II, free-format / bad bitrate or
// sample-rate index). Only the first 4 bytes are read.
bool parseMp3Header(const uint8_t *p, Mp3FrameHeader &out, const char **why = nullptr);

// True when `data` holds an MPEG Layer III frame chain within its first 64 KiB (used by sniff() for files that
// have junk in front of the first frame). Does not throw.
bool looksLikeMp3(const uint8_t *data, size_t size);

// Frame-at-a-time decoder (the streaming building block). Keeps the bit reservoir and the overlap /
// synthesis state between calls, so frames must be fed in order; reset() starts a new stream.
class Mp3Decoder
{
public:
	Mp3Decoder();
	~Mp3Decoder();
	Mp3Decoder(const Mp3Decoder &) = delete;
	Mp3Decoder &operator=(const Mp3Decoder &) = delete;

	void reset();

	// `frame` points at the sync bytes of a frame whose header is `header` and which has at least
	// header.frameBytes bytes available. Writes header.samplesPerFrame * header.channels interleaved samples
	// to `out` and returns header.samplesPerFrame. `streamOffset` is only used in error messages.
	size_t decodeFrame(const Mp3FrameHeader &header, const uint8_t *frame, int16_t *out, uint64_t streamOffset = 0);

private:
	struct State;
	std::unique_ptr<State> m_state;
};

// Whole-file view of an MP3: ID3v2 skipping, frame walk, optional Xing/LAME gapless tag, trimming.
// The constructor walks every frame header (no audio decoding) so info().frames is exact; read() then
// decodes on demand, so a long music track can be streamed in small chunks. The caller keeps `data` alive.
class Mp3Stream
{
public:
	Mp3Stream(const uint8_t *data, size_t size, std::vector<std::string> *warnings = nullptr);
	~Mp3Stream();
	Mp3Stream(const Mp3Stream &) = delete;
	Mp3Stream &operator=(const Mp3Stream &) = delete;

	const AudioInfo &info() const { return m_info; }
	uint32_t audioFrames() const { return m_audioFrameCount; }  // MPEG frames that carry audio
	uint32_t samplesPerFrame() const { return m_samplesPerFrame; }
	uint32_t encoderDelay() const { return m_encoderDelay; }    // from a LAME tag, else 0
	uint32_t encoderPadding() const { return m_encoderPadding; }
	bool hasGaplessTag() const { return m_gapless; }
	// True when the first frame carries a Fraunhofer VBRI header (106 retail files). Decision: that frame is a valid
	// frame whose audio is silence, and it is decoded like any other (mpg123 does the same; ffmpeg drops it, so its
	// output is one frame shorter). Xing / Info frames, by contrast, are skipped.
	bool hasVbriFrame() const { return m_vbri; }

	// Decodes up to maxFrames sample frames (per channel) into `out` (interleaved, maxFrames * channels values);
	// returns the number delivered, 0 at the end of the stream.
	size_t read(int16_t *out, size_t maxFrames);
	// Sample frames delivered so far.
	uint64_t position() const { return m_delivered; }
	void rewind();

private:
	bool nextFrame(Mp3FrameHeader &header, size_t &offset); // advances m_walkPos through m_data, skipping defects

	const uint8_t *m_data;
	size_t m_size;
	AudioInfo m_info;
	uint32_t m_samplesPerFrame = 0;
	uint32_t m_audioFrameCount = 0;
	uint32_t m_encoderDelay = 0;
	uint32_t m_encoderPadding = 0;
	bool m_gapless = false;
	bool m_vbri = false;
	size_t m_firstFrame = 0;  // offset of the first MPEG frame (possibly the Xing / Info metadata frame)
	size_t m_audioStart = 0;  // offset of the first audio frame
	Mp3FrameHeader m_streamHeader; // parameters every frame must share
	uint64_t m_initialSkip = 0;
	size_t m_endOffset = 0;   // offset where the frame walk ends
	// playback
	size_t m_walkPos = 0;
	uint64_t m_delivered = 0;
	uint64_t m_skipRemaining = 0;
	std::unique_ptr<Mp3Decoder> m_decoder;
	std::vector<int16_t> m_frameBuf;  // one decoded frame
	size_t m_frameBufPos = 0;
	size_t m_frameBufCount = 0;       // sample frames in m_frameBuf
};

} // namespace AudioDecode
