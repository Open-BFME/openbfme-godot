// OpenBFME. GPL-3.0.
//
// Retail audio file decoders (lane AUDIO-1): WAV containers (PCM 8/16/24/32, IEEE float, IMA ADPCM,
// Microsoft ADPCM) and MPEG-1/2/2.5 Layer III, all decoded to interleaved signed 16-bit PCM.
// No Godot dependency; every decoder instance is self-contained (no global mutable state), so any
// number of files may be decoded concurrently.
//
// Target facts (surveyed from the RotWK 2.01 + BFME2 1.06 archives, see tools/audio/): sound effects and
// speech are WAV (IMA ADPCM format 0x11 mono/stereo with a 'fact' chunk, or PCM 16-bit), music and
// ambient streams are MPEG-1 Layer III (one MPEG-2 22.05 kHz file) that the retail engine plays through
// Miles' mssmp3.asi. Retail's own readers are not on this machine, so wherever their behaviour on a
// defective file is unknown the decision taken here is documented at the place it is taken and the
// defect is REPORTED through the optional `warnings` list; malformed data that cannot be decoded
// meaningfully throws AudioDecodeError (with the byte offset), never returns silence or a partial result.

#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace AudioDecode
{

enum class Format
{
	Pcm8,
	Pcm16,
	Pcm24,
	Pcm32,
	Float32,
	ImaAdpcm,
	MsAdpcm,
	Mp3
};

const char *formatName(Format format);

struct AudioInfo
{
	Format format = Format::Pcm16;
	uint32_t sampleRate = 0;
	uint16_t channels = 0;
	uint64_t frames = 0;        // sample frames per channel after decoding (after fact / gapless trimming)
	uint32_t bitsPerSample = 0; // of the stored data (4 for the ADPCM formats, 16 for Mp3's decoded output)
	uint32_t blockAlign = 0;    // bytes per sample frame (PCM), per ADPCM block; 0 for Mp3
	double durationMs() const { return sampleRate ? 1000.0 * double(frames) / double(sampleRate) : 0.0; }
};

struct DecodedAudio
{
	AudioInfo info;
	std::vector<int16_t> pcm; // interleaved s16, info.frames * info.channels values
};

class AudioDecodeError : public std::runtime_error
{
public:
	AudioDecodeError(const std::string &what, uint64_t offset)
		: std::runtime_error(what + " (byte offset " + std::to_string(offset) + ")"), m_offset(offset)
	{
	}
	uint64_t offset() const { return m_offset; }

private:
	uint64_t m_offset;
};

// IMA ADPCM step arithmetic. Reference = the IMA/Intel reference decoder: step>>3 plus step, step>>1, step>>2
// for the set magnitude bits (each shift floors separately). FfmpegMultiply = ((2*magnitude + 1) * step) >> 3,
// the shortcut ffmpeg's adpcm_ima_wav uses; it differs from Reference by up to a few LSB per sample, which
// accumulates to several hundred LSB inside a loud 2041-sample block. Reference is the default because
//  * target fact: Miles' own ADPCM encoder in the retail mss32.dll (the routine at 0x2110A850, the only user of
//    its IMA step table at 0x21142B78) rebuilds the predictor after every nibble as step>>3 plus the selected
//    shifted steps, i.e. the Reference series; and
//  * inference (tools/audio/ima_statistics.py): across the retail mono files the next block's header
//    predictor is closer to the last Reference sample than to the last FfmpegMultiply sample.
// The retail decoder itself (also Miles) is not read here. FfmpegMultiply exists only to compare bit-exactly
// against ffmpeg (tools/audio/oracle_check.py).
enum class ImaRounding
{
	Reference,
	FfmpegMultiply
};

struct DecodeOptions
{
	ImaRounding ima = ImaRounding::Reference;
};

// Headers only (an MP3 is walked frame header by frame header: cheap, exact frame count; ID3v2 / ID3v1 /
// trailing junk handled as in decode()). `warnings`, when given, receives one line per defect found.
AudioInfo probe(const uint8_t *data, size_t size, std::vector<std::string> *warnings = nullptr);

// Sniffs the content (RIFF/WAVE, or ID3v2 / MPEG sync) and decodes the whole file.
DecodedAudio decode(const uint8_t *data, size_t size, std::vector<std::string> *warnings = nullptr,
	const DecodeOptions *options = nullptr);

enum class Container
{
	Wav,
	Mp3
};
// What decode()/probe() will treat the bytes as; throws AudioDecodeError for anything else.
Container sniff(const uint8_t *data, size_t size);

// ---- WAV level (also used directly by tests) -------------------------------------------------------------

// Decodes the sample data of a WAV file. Same contract as decode() restricted to RIFF/WAVE.
DecodedAudio decodeWav(const uint8_t *data, size_t size, std::vector<std::string> *warnings = nullptr,
	const DecodeOptions *options = nullptr);
AudioInfo probeWav(const uint8_t *data, size_t size, std::vector<std::string> *warnings = nullptr);

// IMA ADPCM (Microsoft / Intel DVI, format 0x11) block decoder with the standard step/index tables.
// `block` holds blockAlign bytes (or a shorter whole-group tail); writes up to
// 1 + 8 * ((size - 4 * channels) / (4 * channels)) frames per channel, interleaved, returns that count.
// Throws on a step index above 88 in a block header.
size_t decodeImaAdpcmBlock(const uint8_t *block, size_t size, unsigned channels, int16_t *out, uint64_t errorOffset,
	ImaRounding rounding = ImaRounding::Reference);

} // namespace AudioDecode
