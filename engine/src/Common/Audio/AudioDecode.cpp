// OpenBFME. GPL-3.0.
//
// WAV container, PCM / IEEE float / IMA ADPCM / Microsoft ADPCM decoding, format sniffing and the probe
// entry points; MPEG Layer III lives in Mp3Decoder.cpp.
//
// Target facts: the retail archives hold WAV files whose chunks are 'fmt ' [+ 'fact'] + 'data' (+ LIST / cue /
// PAD / bext trailers after the data); formats are IMA ADPCM (0x11, 4 bit, 1 or 2 channels, blockAlign 512 /
// 1024 / 2048) and PCM16. The retail reader (Miles) is not available here, so every decision about defective
// files is inference and is marked "Decision:" below. Each such file produces a warning instead of being
// silently accepted.

#include "Common/Audio/AudioDecode.h"

#include "Common/Audio/Mp3Decoder.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace AudioDecode
{

namespace
{

uint16_t rd16(const uint8_t *p) { return uint16_t(p[0] | (p[1] << 8)); }
uint32_t rd32(const uint8_t *p) { return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24); }
int16_t rds16(const uint8_t *p) { return int16_t(rd16(p)); }

std::string hex16(unsigned v)
{
	char buf[16];
	std::snprintf(buf, sizeof(buf), "0x%04x", v);
	return buf;
}

std::string fourcc(const uint8_t *p)
{
	std::string s;
	for (int i = 0; i < 4; ++i)
	{
		char c = char(p[i]);
		s += (c >= 32 && c < 127) ? c : '?';
	}
	return s;
}

void warn(std::vector<std::string> *warnings, const std::string &text)
{
	if (warnings)
	{
		warnings->push_back(text);
	}
}

// ---- IMA ADPCM ---------------------------------------------------------------------------------------------

const int16_t kImaStep[89] = {7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73,
	80, 88, 97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
	876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358,
	5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794,
	32767};
const int8_t kImaIndex[8] = {-1, -1, -1, -1, 2, 4, 6, 8};

struct ImaState
{
	int predictor;
	int index;
};

inline int imaNibble(ImaState &s, unsigned nibble, ImaRounding rounding)
{
	int step = kImaStep[s.index];
	unsigned magnitude = nibble & 7;
	int diff;
	if (rounding == ImaRounding::Reference)
	{
		diff = step >> 3;
		if (magnitude & 4) diff += step;
		if (magnitude & 2) diff += step >> 1;
		if (magnitude & 1) diff += step >> 2;
	}
	else
	{
		diff = int((2 * magnitude + 1) * unsigned(step)) >> 3;
	}
	int p = (nibble & 8) ? s.predictor - diff : s.predictor + diff;
	s.predictor = p > 32767 ? 32767 : (p < -32768 ? -32768 : p);
	int idx = s.index + kImaIndex[magnitude];
	s.index = idx < 0 ? 0 : (idx > 88 ? 88 : idx);
	return s.predictor;
}

// ---- Microsoft ADPCM ---------------------------------------------------------------------------------------

const int kMsAdapt[16] = {230, 230, 230, 230, 307, 409, 512, 614, 768, 614, 512, 409, 307, 230, 230, 230};

struct MsState
{
	int coef1, coef2;
	int delta;
	int s1, s2;
};

inline int msNibble(MsState &s, unsigned nibble, uint64_t errorOffset)
{
	// Microsoft's reference: (s1 * c1 + s2 * c2) / 256 with C division (toward zero), then add signed nibble * delta. Every intermediate is 64 bit
	// (s1 * c1 alone reaches 2^31), and a delta that no longer fits a signed 32 bit value is unrepresentable decoder state: an error, not a guess.
	const int64_t pred = ((int64_t)s.s1 * s.coef1 + (int64_t)s.s2 * s.coef2) / 256;
	const int64_t signedNibble = (nibble & 8) ? (int64_t)nibble - 16 : (int64_t)nibble;
	int64_t v = pred + signedNibble * (int64_t)s.delta;
	v = v > 32767 ? 32767 : (v < -32768 ? -32768 : v);
	s.s2 = s.s1;
	s.s1 = (int)v;
	const int64_t d = ((int64_t)kMsAdapt[nibble] * (int64_t)s.delta) >> 8;
	if (d > 0x7FFFFFFFll)
	{
		throw AudioDecodeError("MS ADPCM step size outgrew a signed 32 bit value", errorOffset);
	}
	s.delta = d < 16 ? 16 : (int)d;
	return (int)v;
}

// ---- WAV container -----------------------------------------------------------------------------------------

struct WavFormat
{
	uint16_t tag = 0; // effective tag (WAVE_FORMAT_EXTENSIBLE resolved to its sub-format)
	uint16_t channels = 0;
	uint32_t sampleRate = 0;
	uint16_t blockAlign = 0;
	uint16_t bits = 0;
	uint16_t samplesPerBlock = 0; // ADPCM: samples per channel in one block (derived from blockAlign)
	int coefs[32][2] = {};
	unsigned coefCount = 0;
};

struct WavLayout
{
	WavFormat fmt;
	bool hasFact = false;
	uint32_t fact = 0;
	uint64_t dataOffset = 0;
	uint64_t dataSize = 0; // bytes actually present (clamped to the file)
	AudioInfo info;
	// decoding plan
	uint64_t wholeBlocks = 0;      // ADPCM
	unsigned partialBlockFrames = 0; // ADPCM frames in the final partial block
	uint64_t decodedFrames = 0;    // before the fact trim
};

Format formatOf(const WavFormat &f)
{
	switch (f.tag)
	{
	case 1:
		return f.bits == 8 ? Format::Pcm8 : f.bits == 16 ? Format::Pcm16 : f.bits == 24 ? Format::Pcm24 : Format::Pcm32;
	case 3:
		return Format::Float32;
	case 0x11:
		return Format::ImaAdpcm;
	default:
		return Format::MsAdpcm;
	}
}

void parseFmt(const uint8_t *fmt, uint32_t size, uint64_t offset, WavFormat &out)
{
	if (size < 16)
	{
		throw AudioDecodeError("WAVE fmt chunk is " + std::to_string(size) + " bytes, at least 16 required", offset);
	}
	uint16_t tag = rd16(fmt);
	out.channels = rd16(fmt + 2);
	out.sampleRate = rd32(fmt + 4);
	out.blockAlign = rd16(fmt + 12);
	out.bits = rd16(fmt + 14);
	out.tag = tag;
	uint32_t cb = size >= 18 ? rd16(fmt + 16) : 0;
	if (tag == 0xFFFE)
	{
		static const uint8_t kBase[14] = {0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0xAA, 0x00, 0x38, 0x9B, 0x71};
		if (size < 40 || cb < 22)
		{
			throw AudioDecodeError("WAVE_FORMAT_EXTENSIBLE fmt chunk too short", offset);
		}
		out.tag = rd16(fmt + 24);
		if (std::memcmp(fmt + 26, kBase, 14) != 0)
		{
			throw AudioDecodeError("WAVE_FORMAT_EXTENSIBLE with a non-standard sub-format GUID", offset + 24);
		}
	}
	if (out.channels == 0)
	{
		throw AudioDecodeError("WAVE format has 0 channels", offset + 2);
	}
	if (out.sampleRate == 0)
	{
		throw AudioDecodeError("WAVE format has a sample rate of 0", offset + 4);
	}
	switch (out.tag)
	{
	case 1:
		if (out.bits != 8 && out.bits != 16 && out.bits != 24 && out.bits != 32)
		{
			throw AudioDecodeError("unsupported PCM bit depth " + std::to_string(out.bits), offset + 14);
		}
		break;
	case 3:
		if (out.bits != 32 && out.bits != 64)
		{
			throw AudioDecodeError("unsupported IEEE float bit depth " + std::to_string(out.bits), offset + 14);
		}
		break;
	case 0x11:
	{
		if (out.bits != 4)
		{
			throw AudioDecodeError("IMA ADPCM with " + std::to_string(out.bits) + " bits per sample is unsupported (4 required)",
				offset + 14);
		}
		if (out.channels > 8)
		{
			throw AudioDecodeError("IMA ADPCM with more than 8 channels is unsupported", offset + 2);
		}
		unsigned headerBytes = 4u * out.channels;
		if (out.blockAlign < headerBytes + 4u * out.channels || (out.blockAlign - headerBytes) % (4u * out.channels) != 0)
		{
			throw AudioDecodeError("IMA ADPCM blockAlign " + std::to_string(out.blockAlign) +
					" is not a 4-byte header plus whole 4-byte groups per channel",
				offset + 12);
		}
		out.samplesPerBlock = uint16_t(1 + (out.blockAlign - headerBytes) * 2 / out.channels);
		break;
	}
	case 2:
	{
		if (out.bits != 4)
		{
			throw AudioDecodeError("MS ADPCM with " + std::to_string(out.bits) + " bits per sample is unsupported (4 required)",
				offset + 14);
		}
		if (out.channels > 2)
		{
			throw AudioDecodeError("MS ADPCM with more than 2 channels is unsupported", offset + 2);
		}
		if (out.blockAlign < 7u * out.channels)
		{
			throw AudioDecodeError("MS ADPCM blockAlign " + std::to_string(out.blockAlign) + " is smaller than the block header",
				offset + 12);
		}
		if (size < 22 || cb < 4 || size < 18 + 4)
		{
			throw AudioDecodeError("MS ADPCM fmt chunk lacks the samplesPerBlock / coefficient extension", offset + 16);
		}
		unsigned numCoef = rd16(fmt + 20);
		if (numCoef == 0 || numCoef > 32 || size < 22 + numCoef * 4)
		{
			throw AudioDecodeError("MS ADPCM coefficient count " + std::to_string(numCoef) + " is invalid or truncated", offset + 20);
		}
		for (unsigned i = 0; i < numCoef; ++i)
		{
			out.coefs[i][0] = rds16(fmt + 22 + i * 4);
			out.coefs[i][1] = rds16(fmt + 24 + i * 4);
		}
		out.coefCount = numCoef;
		out.samplesPerBlock = uint16_t(2 + (out.blockAlign - 7u * out.channels) * 2 / out.channels);
		break;
	}
	default:
		throw AudioDecodeError("unsupported WAVE format tag " + hex16(out.tag), offset);
	}
}

WavLayout parseWav(const uint8_t *data, size_t size, std::vector<std::string> *warnings)
{
	if (size < 12)
	{
		throw AudioDecodeError("file is " + std::to_string(size) + " bytes, too short for a RIFF/WAVE header", 0);
	}
	if (std::memcmp(data, "RIFF", 4) != 0)
	{
		throw AudioDecodeError("missing RIFF signature", 0);
	}
	if (std::memcmp(data + 8, "WAVE", 4) != 0)
	{
		throw AudioDecodeError("RIFF form type is '" + fourcc(data + 8) + "', not WAVE", 8);
	}
	WavLayout lay;
	uint32_t riffSize = rd32(data + 4);
	// Decision: a RIFF size that disagrees with the file size is reported, the chunk walk follows the real bytes.
	if (uint64_t(riffSize) + 8 != size)
	{
		warn(warnings, "riff-size: RIFF size field " + std::to_string(riffSize) + " + 8 does not match the file size " + std::to_string(size));
	}
	bool haveFmt = false, haveData = false;
	size_t pos = 12;
	while (pos < size)
	{
		if (size - pos < 8)
		{
			warn(warnings, "trailing-bytes: " + std::to_string(size - pos) + " byte(s) after the last chunk at offset " + std::to_string(pos));
			break;
		}
		uint32_t csz = rd32(data + pos + 4);
		size_t body = pos + 8;
		uint64_t avail = size - body;
		std::string id = fourcc(data + pos);
		if (id == "fmt ")
		{
			if (csz > avail)
			{
				throw AudioDecodeError("fmt chunk of " + std::to_string(csz) + " bytes runs past the end of the file", pos);
			}
			if (!haveFmt)
			{
				parseFmt(data + body, csz, body, lay.fmt);
				haveFmt = true;
			}
			else
			{
				warn(warnings, "duplicate-chunk: duplicate fmt chunk at offset " + std::to_string(pos) + " ignored");
			}
		}
		else if (id == "fact")
		{
			if (csz >= 4 && csz <= avail && !lay.hasFact)
			{
				lay.hasFact = true;
				lay.fact = rd32(data + body);
			}
		}
		else if (id == "data")
		{
			if (!haveData)
			{
				haveData = true;
				lay.dataOffset = body;
				if (csz > avail)
				{
					// Decision: retail's reader is unknown; the bytes that are present are decoded and the overrun reported.
					warn(warnings, "data-overrun: data chunk declares " + std::to_string(csz) + " bytes but only " + std::to_string(avail) +
							" remain in the file; decoding the available bytes");
					lay.dataSize = avail;
				}
				else
				{
					lay.dataSize = csz;
				}
			}
			else
			{
				warn(warnings, "duplicate-chunk: duplicate data chunk at offset " + std::to_string(pos) + " ignored");
			}
		}
		uint64_t next = uint64_t(body) + csz + (csz & 1);
		if (next > size)
		{
			// the final chunk may lack its pad byte; any other overrun means the chunk list is cut off
			if (!(next == size + 1 && (csz & 1)) && id != "data")
			{
				warn(warnings, "chunk-overrun: chunk '" + id + "' at offset " + std::to_string(pos) + " declares " + std::to_string(csz) +
						" bytes, past the end of the file");
			}
			break;
		}
		pos = size_t(next);
	}
	if (!haveFmt)
	{
		throw AudioDecodeError("WAVE file has no fmt chunk", 12);
	}
	if (!haveData)
	{
		throw AudioDecodeError("WAVE file has no data chunk", 12);
	}

	const WavFormat &f = lay.fmt;
	AudioInfo &info = lay.info;
	info.format = formatOf(f);
	info.sampleRate = f.sampleRate;
	info.channels = f.channels;
	info.bitsPerSample = f.bits;
	info.blockAlign = f.blockAlign;
	uint64_t frames = 0;
	if (f.tag == 1 || f.tag == 3)
	{
		unsigned frameBytes = unsigned(f.channels) * (f.bits / 8);
		if (f.blockAlign != frameBytes)
		{
			// Decision: the sample layout is fixed by channels x bits; a divergent blockAlign is reported and ignored.
			warn(warnings, "block-align: blockAlign " + std::to_string(f.blockAlign) + " differs from channels x bytes per sample " +
					std::to_string(frameBytes) + "; using the latter");
		}
		info.blockAlign = frameBytes;
		frames = lay.dataSize / frameBytes;
		uint64_t trailing = lay.dataSize % frameBytes;
		if (trailing)
		{
			// Decision: a trailing partial sample frame (an odd trailing byte in 16-bit mono) is dropped and reported.
			warn(warnings, "partial-sample: " + std::to_string(trailing) + " trailing data byte(s) after the last whole sample frame ignored");
		}
		lay.decodedFrames = frames;
	}
	else
	{
		unsigned headerBytes = (f.tag == 0x11 ? 4u : 7u) * f.channels;
		lay.wholeBlocks = lay.dataSize / f.blockAlign;
		uint64_t rem = lay.dataSize % f.blockAlign;
		frames = lay.wholeBlocks * f.samplesPerBlock;
		if (rem)
		{
			if (rem >= headerBytes)
			{
				unsigned groupBytes = f.tag == 0x11 ? 4u * f.channels : f.channels;
				unsigned bodyBytes = unsigned(rem - headerBytes);
				if (f.tag == 0x11)
				{
					lay.partialBlockFrames = 1 + (bodyBytes / groupBytes) * 8;
				}
				else
				{
					lay.partialBlockFrames = 2 + bodyBytes * 2 / f.channels;
				}
				frames += lay.partialBlockFrames;
			}
			// Decision: a final block shorter than blockAlign is decoded as far as it goes (the block header is
			// self-contained), bytes that do not fill a whole 4-byte group are dropped; reported either way.
			warn(warnings, "partial-block: data ends with a partial ADPCM block of " + std::to_string(rem) + " bytes (blockAlign " +
					std::to_string(f.blockAlign) + ")");
		}
		lay.decodedFrames = frames;
		if (lay.hasFact)
		{
			// Decision: the 'fact' chunk is the true length of an ADPCM stream (its last block is padded to a whole
			// block; 12,562 of the 12,830 retail files carry a fact shorter than the whole blocks by less than one
			// block). The decoded length is min(fact, whole blocks). A fact LARGER than the data can hold (the data
			// was cut) is reported and the decoded length is used; a fact that discards a whole block or more is
			// reported too: that is either an encoder that padded extra silent blocks or truncated-in-reverse data.
			if (lay.fact > frames)
			{
				warn(warnings, "fact-exceeds-data: fact chunk says " + std::to_string(lay.fact) + " frames but the data holds only " +
						std::to_string(frames) + " (" + std::to_string(lay.fact - frames) + " missing)");
			}
			else
			{
				if (frames - lay.fact >= f.samplesPerBlock)
				{
					warn(warnings, "fact-trims-block: fact chunk says " + std::to_string(lay.fact) + " frames, " + std::to_string(frames - lay.fact) +
							" fewer than the data holds (a block is " + std::to_string(f.samplesPerBlock) + "); trimmed");
				}
				frames = lay.fact;
			}
		}
	}
	info.frames = frames;
	return lay;
}

// ---- sample conversion -------------------------------------------------------------------------------------

inline int16_t clip16(int64_t v)
{
	return int16_t(v > 32767 ? 32767 : (v < -32768 ? -32768 : v));
}

void convertPcm(const WavLayout &lay, const uint8_t *src, int16_t *dst, uint64_t frames, uint64_t errorBase)
{
	const WavFormat &f = lay.fmt;
	uint64_t samples = frames * f.channels;
	if (f.tag == 1)
	{
		switch (f.bits)
		{
		case 8:
			for (uint64_t i = 0; i < samples; ++i) dst[i] = int16_t((int(src[i]) - 128) * 256);
			break;
		case 16:
			for (uint64_t i = 0; i < samples; ++i) dst[i] = rds16(src + 2 * i);
			break;
		case 24:
			// truncating conversion to the top 16 bits (what ffmpeg's s32 -> s16 does)
			for (uint64_t i = 0; i < samples; ++i) dst[i] = int16_t(rd16(src + 3 * i + 1));
			break;
		default:
			for (uint64_t i = 0; i < samples; ++i) dst[i] = int16_t(rd16(src + 4 * i + 2));
			break;
		}
		return;
	}
	// IEEE float: round to nearest even after scaling by 32768, clip (ffmpeg's flt -> s16)
	for (uint64_t i = 0; i < samples; ++i)
	{
		double v;
		if (f.bits == 32)
		{
			uint32_t u = rd32(src + 4 * i);
			float x;
			std::memcpy(&x, &u, 4);
			v = x;
		}
		else
		{
			uint64_t u = uint64_t(rd32(src + 8 * i)) | (uint64_t(rd32(src + 8 * i + 4)) << 32);
			std::memcpy(&v, &u, 8);
		}
		if (v != v)
		{
			throw AudioDecodeError("NaN float sample", errorBase + i * (f.bits / 8));
		}
		v *= 32768.0;
		dst[i] = clip16(int64_t(std::nearbyint(v > 1e9 ? 1e9 : (v < -1e9 ? -1e9 : v))));
	}
}

} // namespace

const char *formatName(Format format)
{
	switch (format)
	{
	case Format::Pcm8: return "pcm8";
	case Format::Pcm16: return "pcm16";
	case Format::Pcm24: return "pcm24";
	case Format::Pcm32: return "pcm32";
	case Format::Float32: return "float";
	case Format::ImaAdpcm: return "ima-adpcm";
	case Format::MsAdpcm: return "ms-adpcm";
	case Format::Mp3: return "mp3";
	}
	return "?";
}

size_t decodeImaAdpcmBlock(const uint8_t *block, size_t size, unsigned channels, int16_t *out, uint64_t errorOffset,
	ImaRounding rounding)
{
	if (channels == 0 || channels > 8)
	{
		throw AudioDecodeError("IMA ADPCM block with " + std::to_string(channels) + " channels", errorOffset);
	}
	if (size < 4u * channels)
	{
		throw AudioDecodeError("IMA ADPCM block of " + std::to_string(size) + " bytes is shorter than its header", errorOffset);
	}
	ImaState st[8];
	for (unsigned c = 0; c < channels; ++c)
	{
		st[c].predictor = rds16(block + 4 * c);
		st[c].index = block[4 * c + 2];
		if (st[c].index > 88)
		{
			// Decision: an out-of-range step index cannot be decoded meaningfully (ffmpeg rejects it as well); throw.
			throw AudioDecodeError("IMA ADPCM block header has step index " + std::to_string(st[c].index) + " (> 88) for channel " +
					std::to_string(c),
				errorOffset + 4 * c + 2);
		}
		out[c] = int16_t(st[c].predictor);
	}
	size_t groups = (size - 4u * channels) / (4u * channels);
	const uint8_t *src = block + 4u * channels;
	for (size_t g = 0; g < groups; ++g)
	{
		for (unsigned c = 0; c < channels; ++c)
		{
			int16_t *dst = out + (1 + g * 8) * channels + c;
			for (int b = 0; b < 4; ++b)
			{
				uint8_t byte = *src++;
				dst[(2 * b) * channels] = int16_t(imaNibble(st[c], byte & 15, rounding));
				dst[(2 * b + 1) * channels] = int16_t(imaNibble(st[c], byte >> 4, rounding));
			}
		}
	}
	return 1 + groups * 8;
}

namespace
{

// Microsoft ADPCM block: `size` bytes starting at the block header; returns the frames written.
size_t decodeMsAdpcmBlock(const WavFormat &f, const uint8_t *block, size_t size, int16_t *out, uint64_t errorOffset)
{
	unsigned ch = f.channels;
	if (size < 7u * ch)
	{
		throw AudioDecodeError("MS ADPCM block shorter than its header", errorOffset);
	}
	MsState st[2];
	for (unsigned c = 0; c < ch; ++c)
	{
		unsigned idx = block[c];
		if (idx >= f.coefCount)
		{
			throw AudioDecodeError("MS ADPCM block predictor index " + std::to_string(idx) + " >= coefficient count " +
					std::to_string(f.coefCount),
				errorOffset + c);
		}
		st[c].coef1 = f.coefs[idx][0];
		st[c].coef2 = f.coefs[idx][1];
		st[c].delta = rds16(block + ch + 2 * c);
		st[c].s1 = rds16(block + 3 * ch + 2 * c);
		st[c].s2 = rds16(block + 5 * ch + 2 * c);
	}
	for (unsigned c = 0; c < ch; ++c)
	{
		out[c] = int16_t(st[c].s2);
		out[ch + c] = int16_t(st[c].s1);
	}
	const uint8_t *src = block + 7u * ch;
	size_t bodyBytes = size - 7u * ch;
	size_t frames = 2 + bodyBytes * 2 / ch;
	for (size_t i = 0; i < bodyBytes; ++i)
	{
		uint8_t byte = src[i];
		if (ch == 1)
		{
			out[2 + 2 * i] = int16_t(msNibble(st[0], byte >> 4, errorOffset));
			out[2 + 2 * i + 1] = int16_t(msNibble(st[0], byte & 15, errorOffset));
		}
		else
		{
			out[(2 + i) * 2] = int16_t(msNibble(st[0], byte >> 4, errorOffset));
			out[(2 + i) * 2 + 1] = int16_t(msNibble(st[1], byte & 15, errorOffset));
		}
	}
	return frames;
}

} // namespace

AudioInfo probeWav(const uint8_t *data, size_t size, std::vector<std::string> *warnings)
{
	return parseWav(data, size, warnings).info;
}

DecodedAudio decodeWav(const uint8_t *data, size_t size, std::vector<std::string> *warnings, const DecodeOptions *options)
{
	WavLayout lay = parseWav(data, size, warnings);
	const WavFormat &f = lay.fmt;
	ImaRounding rounding = options ? options->ima : ImaRounding::Reference;
	DecodedAudio result;
	result.info = lay.info;
	const uint8_t *src = data + lay.dataOffset;
	unsigned ch = f.channels;
	if (f.tag == 1 || f.tag == 3)
	{
		result.pcm.resize(size_t(lay.info.frames) * ch);
		convertPcm(lay, src, result.pcm.data(), lay.info.frames, lay.dataOffset);
		return result;
	}
	// ADPCM: decode every whole block (plus the partial one), then trim to the fact-limited length.
	size_t total = size_t(lay.decodedFrames) * ch;
	result.pcm.resize(total);
	int16_t *dst = result.pcm.data();
	size_t blocks = size_t(lay.wholeBlocks);
	for (size_t b = 0; b < blocks; ++b)
	{
		uint64_t off = lay.dataOffset + uint64_t(b) * f.blockAlign;
		size_t n = f.tag == 0x11 ? decodeImaAdpcmBlock(src + b * size_t(f.blockAlign), f.blockAlign, ch, dst, off, rounding)
								 : decodeMsAdpcmBlock(f, src + b * size_t(f.blockAlign), f.blockAlign, dst, off);
		dst += n * ch;
	}
	if (lay.partialBlockFrames)
	{
		size_t rem = size_t(lay.dataSize % f.blockAlign);
		uint64_t off = lay.dataOffset + uint64_t(blocks) * f.blockAlign;
		size_t n = f.tag == 0x11 ? decodeImaAdpcmBlock(src + blocks * size_t(f.blockAlign), rem, ch, dst, off, rounding)
								 : decodeMsAdpcmBlock(f, src + blocks * size_t(f.blockAlign), rem, dst, off);
		dst += n * ch;
	}
	result.pcm.resize(size_t(lay.info.frames) * ch);
	return result;
}

Container sniff(const uint8_t *data, size_t size)
{
	if (size >= 4 && std::memcmp(data, "RIFF", 4) == 0)
	{
		if (size >= 12 && std::memcmp(data + 8, "WAVE", 4) == 0)
		{
			return Container::Wav;
		}
		throw AudioDecodeError("RIFF file that is not a WAVE form", 8);
	}
	if (size >= 3 && std::memcmp(data, "ID3", 3) == 0)
	{
		return Container::Mp3;
	}
	if (size >= 2 && data[0] == 0xFF && (data[1] & 0xE0) == 0xE0)
	{
		return Container::Mp3;
	}
	if (looksLikeMp3(data, size))
	{
		return Container::Mp3;
	}
	char buf[40];
	std::snprintf(buf, sizeof(buf), "%02x %02x %02x %02x", size > 0 ? data[0] : 0, size > 1 ? data[1] : 0, size > 2 ? data[2] : 0,
		size > 3 ? data[3] : 0);
	throw AudioDecodeError(std::string("unrecognised audio content (first bytes ") + buf + ")", 0);
}

AudioInfo probe(const uint8_t *data, size_t size, std::vector<std::string> *warnings)
{
	if (sniff(data, size) == Container::Wav)
	{
		return probeWav(data, size, warnings);
	}
	return Mp3Stream(data, size, warnings).info();
}

DecodedAudio decode(const uint8_t *data, size_t size, std::vector<std::string> *warnings, const DecodeOptions *options)
{
	if (sniff(data, size) == Container::Wav)
	{
		return decodeWav(data, size, warnings, options);
	}
	Mp3Stream stream(data, size, warnings);
	DecodedAudio result;
	result.info = stream.info();
	result.pcm.resize(size_t(result.info.frames) * result.info.channels);
	size_t done = 0;
	while (done < result.info.frames)
	{
		size_t n = stream.read(result.pcm.data() + done * result.info.channels, size_t(result.info.frames) - done);
		if (n == 0)
		{
			throw AudioDecodeError("MP3 stream ended after " + std::to_string(done) + " of " + std::to_string(result.info.frames) +
					" sample frames",
				size);
		}
		done += n;
	}
	return result;
}

} // namespace AudioDecode
