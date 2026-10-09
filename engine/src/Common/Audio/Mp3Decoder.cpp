// OpenBFME. GPL-3.0.
//
// MPEG-1/2/2.5 Layer III decoder. Written from the ISO/IEC 11172-3 / 13818-3 decoding process; no code from
// any other decoder. Section numbers below refer to ISO 11172-3. See Mp3Decoder.h for scope and decisions.

#include "Common/Audio/Mp3Decoder.h"

#include "Common/Audio/Mp3Tables.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace AudioDecode
{

namespace
{

constexpr double kPi = 3.14159265358979323846;

// ---- tables that are formulas or small enough to be written here ----------------------------------------------

// Scalefactor band boundaries (2.4.3.4.9.3 / 13818-3 table): index 0..2 MPEG-1 (44.1, 48, 32 kHz),
// 3..5 MPEG-2 (22.05, 24, 16 kHz), 6..8 MPEG-2.5 (11.025, 12, 8 kHz).
const uint16_t kSfbLong[9][23] = {
	{0, 4, 8, 12, 16, 20, 24, 30, 36, 44, 52, 62, 74, 90, 110, 134, 162, 196, 238, 288, 342, 418, 576},
	{0, 4, 8, 12, 16, 20, 24, 30, 36, 42, 50, 60, 72, 88, 106, 128, 156, 190, 230, 276, 330, 384, 576},
	{0, 4, 8, 12, 16, 20, 24, 30, 36, 44, 54, 66, 82, 102, 126, 156, 194, 240, 296, 364, 448, 550, 576},
	{0, 6, 12, 18, 24, 30, 36, 44, 54, 66, 80, 96, 116, 140, 168, 200, 238, 284, 336, 396, 464, 522, 576},
	{0, 6, 12, 18, 24, 30, 36, 44, 54, 66, 80, 96, 114, 136, 162, 194, 232, 278, 332, 394, 464, 540, 576},
	{0, 6, 12, 18, 24, 30, 36, 44, 54, 66, 80, 96, 116, 140, 168, 200, 238, 284, 336, 396, 464, 522, 576},
	{0, 6, 12, 18, 24, 30, 36, 44, 54, 66, 80, 96, 116, 140, 168, 200, 238, 284, 336, 396, 464, 522, 576},
	{0, 6, 12, 18, 24, 30, 36, 44, 54, 66, 80, 96, 116, 140, 168, 200, 238, 284, 336, 396, 464, 522, 576},
	{0, 12, 24, 36, 48, 60, 72, 88, 108, 132, 160, 192, 232, 280, 336, 400, 476, 566, 568, 570, 572, 574, 576},
};
const uint16_t kSfbShort[9][14] = {
	{0, 4, 8, 12, 16, 22, 30, 40, 52, 66, 84, 106, 136, 192},
	{0, 4, 8, 12, 16, 22, 28, 38, 50, 64, 80, 100, 126, 192},
	{0, 4, 8, 12, 16, 22, 30, 42, 58, 78, 104, 138, 180, 192},
	{0, 4, 8, 12, 18, 24, 32, 42, 56, 74, 100, 132, 174, 192},
	{0, 4, 8, 12, 18, 26, 36, 48, 62, 80, 104, 136, 180, 192},
	{0, 4, 8, 12, 18, 26, 36, 48, 62, 80, 104, 134, 174, 192},
	{0, 4, 8, 12, 18, 26, 36, 48, 62, 80, 104, 134, 174, 192},
	{0, 4, 8, 12, 18, 26, 36, 48, 62, 80, 104, 134, 174, 192},
	{0, 8, 16, 24, 36, 52, 72, 96, 124, 160, 162, 164, 166, 192},
};
const uint8_t kPretab[22] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 3, 3, 3, 2, 0};

// MPEG-1 scalefac_compress -> (slen1, slen2) (2.4.2.7)
const uint8_t kSlen1[16] = {0, 0, 0, 0, 3, 1, 1, 1, 2, 2, 2, 3, 3, 3, 4, 4};
const uint8_t kSlen2[16] = {0, 1, 2, 3, 0, 1, 2, 3, 1, 2, 3, 1, 2, 3, 2, 3};

// MPEG-2 LSF: number of scalefactors per partition, [block class: long, short, mixed][case][partition] (13818-3 2.4.3.2)
const uint8_t kLsfPartitions[3][6][4] = {
	{{6, 5, 5, 5}, {6, 5, 7, 3}, {11, 10, 0, 0}, {7, 7, 7, 0}, {6, 6, 6, 3}, {8, 8, 5, 0}},
	{{9, 9, 9, 9}, {9, 9, 12, 6}, {18, 18, 0, 0}, {12, 12, 12, 0}, {12, 9, 9, 6}, {15, 12, 9, 0}},
	{{6, 9, 9, 9}, {6, 9, 12, 6}, {15, 18, 0, 0}, {6, 15, 12, 0}, {6, 12, 9, 6}, {6, 18, 9, 0}},
};

const uint8_t kLinbits16[8] = {1, 2, 3, 4, 6, 8, 10, 13}; // tables 16..23 (all share Huffman table 16)
const uint8_t kLinbits24[8] = {4, 5, 6, 7, 8, 9, 11, 13}; // tables 24..31 (share Huffman table 24)

const uint16_t kBitrateV1[16] = {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0};
const uint16_t kBitrateV2[16] = {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0};
const uint32_t kSampleRates[3][3] = {{44100, 48000, 32000}, {22050, 24000, 16000}, {11025, 12000, 8000}};

constexpr int kMaxReservoir = 511;
constexpr int kMaxFrameBytes = 1441 + 8; // 320 kbit/s at 32 kHz plus padding

struct HuffLut
{
	std::vector<uint16_t> lut; // [peek(maxLen)] -> (length << 8) | (x << 4 | y); 0 = no such code
	int maxLen = 0;
	int n = 0;
};

// Lee's recursive fast DCT-II: X[k] = sum_n x[n] cos(pi (2n + 1) k / (2N)). Coefficient tables 1 / (2 cos(pi (2i + 1) / 2N)).
struct Constants
{
	HuffLut big[mp3tab::kHuffmanTableCount];
	int bigIndex[32]; // ISO table number -> index into big[] (or -1)
	uint16_t quad[2][64];
	float pow43[8208];
	float quarter[4];
	float dwin[512];
	float imdct18[18][18];
	float imdct6[6][6];
	float win36[4][36];
	float win12[12];
	float lee32[16], lee16[8], lee8[4], lee4[2], lee2[1];
	float cs[8], ca[8];
	float tanRatio[7]; // MPEG-1 intensity: tan(is_pos * pi / 12)

	Constants()
	{
		for (int i = 0; i < 32; ++i) bigIndex[i] = -1;
		for (int t = 0; t < mp3tab::kHuffmanTableCount; ++t)
		{
			const mp3tab::HuffmanSource &src = mp3tab::kHuffmanSources[t];
			bigIndex[src.tableNumber] = t;
			HuffLut &h = big[t];
			h.n = src.n;
			int maxLen = 0;
			for (int i = 0; i < src.n * src.n; ++i) maxLen = std::max<int>(maxLen, src.len[i]);
			h.maxLen = maxLen;
			h.lut.assign(size_t(1) << maxLen, 0);
			for (int x = 0; x < src.n; ++x)
			{
				for (int y = 0; y < src.n; ++y)
				{
					int len = src.len[x * src.n + y];
					uint32_t code = src.code[x * src.n + y];
					size_t lo = size_t(code) << (maxLen - len);
					size_t count = size_t(1) << (maxLen - len);
					uint16_t entry = uint16_t((len << 8) | (x << 4) | y);
					for (size_t k = 0; k < count; ++k) h.lut[lo + k] = entry;
				}
			}
		}
		for (int q = 0; q < 2; ++q)
		{
			std::memset(quad[q], 0, sizeof(quad[q]));
			for (int v = 0; v < 16; ++v)
			{
				int len = mp3tab::kQuadSources[q].len[v];
				uint32_t code = mp3tab::kQuadSources[q].code[v];
				for (uint32_t k = 0; k < (1u << (6 - len)); ++k) quad[q][(code << (6 - len)) + k] = uint16_t((len << 8) | v);
			}
		}
		for (int i = 0; i < 8208; ++i) pow43[i] = float(std::pow(double(i), 4.0 / 3.0));
		for (int i = 0; i < 4; ++i) quarter[i] = float(std::pow(2.0, i / 4.0));
		for (int i = 0; i <= 256; ++i) dwin[i] = float(mp3tab::kSynthWindow257[i]) / 65536.0f;
		for (int i = 1; i < 256; ++i)
		{
			float v = float(mp3tab::kSynthWindow257[i]) / 65536.0f;
			dwin[512 - i] = (i & 63) ? -v : v;
		}
		for (int n = 0; n < 18; ++n)
			for (int k = 0; k < 18; ++k) imdct18[n][k] = float(std::cos(kPi * (2 * n + 1) * (2 * k + 1) / 72.0));
		for (int n = 0; n < 6; ++n)
			for (int k = 0; k < 6; ++k) imdct6[n][k] = float(std::cos(kPi * (2 * n + 1) * (2 * k + 1) / 24.0));
		for (int i = 0; i < 36; ++i)
		{
			win36[0][i] = float(std::sin(kPi / 36.0 * (i + 0.5)));
			win36[1][i] = i < 18 ? win36[0][i] : i < 24 ? 1.0f : i < 30 ? float(std::sin(kPi / 12.0 * (i - 18 + 0.5))) : 0.0f;
			win36[3][i] = i < 6 ? 0.0f : i < 12 ? float(std::sin(kPi / 12.0 * (i - 6 + 0.5))) : i < 18 ? 1.0f : win36[0][i];
			win36[2][i] = 0.0f; // short blocks use win12
		}
		for (int i = 0; i < 12; ++i) win12[i] = float(std::sin(kPi / 12.0 * (i + 0.5)));
		fillLee(lee32, 32);
		fillLee(lee16, 16);
		fillLee(lee8, 8);
		fillLee(lee4, 4);
		fillLee(lee2, 2);
		static const double ci[8] = {-0.6, -0.535, -0.33, -0.185, -0.095, -0.041, -0.0142, -0.0037};
		for (int i = 0; i < 8; ++i)
		{
			double sq = std::sqrt(1.0 + ci[i] * ci[i]);
			cs[i] = float(1.0 / sq);
			ca[i] = float(ci[i] / sq);
		}
		for (int i = 0; i < 7; ++i) tanRatio[i] = float(std::tan(i * kPi / 12.0));
	}

	static void fillLee(float *table, int n)
	{
		for (int i = 0; i < n / 2; ++i) table[i] = float(1.0 / (2.0 * std::cos(kPi * (2 * i + 1) / (2.0 * n))));
	}
};

const Constants &constants()
{
	static const Constants c; // built once; thread-safe initialisation, read-only afterwards
	return c;
}

template <int N>
struct LeeDct;
template <>
struct LeeDct<1>
{
	static void run(const float *x, float *X, const Constants &) { X[0] = x[0]; }
};

template <int N>
inline const float *leeCoef(const Constants &c);
template <>
inline const float *leeCoef<32>(const Constants &c) { return c.lee32; }
template <>
inline const float *leeCoef<16>(const Constants &c) { return c.lee16; }
template <>
inline const float *leeCoef<8>(const Constants &c) { return c.lee8; }
template <>
inline const float *leeCoef<4>(const Constants &c) { return c.lee4; }
template <>
inline const float *leeCoef<2>(const Constants &c) { return c.lee2; }

template <int N>
struct LeeDct
{
	static void run(const float *x, float *X, const Constants &c)
	{
		constexpr int H = N / 2;
		const float *coef = leeCoef<N>(c);
		float a[H], b[H], A[H], B[H];
		for (int i = 0; i < H; ++i)
		{
			a[i] = x[i] + x[N - 1 - i];
			b[i] = (x[i] - x[N - 1 - i]) * coef[i];
		}
		LeeDct<H>::run(a, A, c);
		LeeDct<H>::run(b, B, c);
		for (int k = 0; k < H - 1; ++k)
		{
			X[2 * k] = A[k];
			X[2 * k + 1] = B[k] + B[k + 1];
		}
		X[N - 2] = A[H - 1];
		X[N - 1] = B[H - 1];
	}
};

// ---- bit reader ------------------------------------------------------------------------------------------------

struct BitReader
{
	const uint8_t *buf = nullptr; // readable for 8 bytes past the last valid byte (zeros)
	uint32_t pos = 0;             // bit position

	uint32_t peek(int n) const // 1..32 bits
	{
		const uint8_t *p = buf + (pos >> 3);
		uint64_t v = (uint64_t(p[0]) << 32) | (uint64_t(p[1]) << 24) | (uint64_t(p[2]) << 16) | (uint64_t(p[3]) << 8) | p[4];
		int shift = 40 - int(pos & 7) - n;
		return uint32_t((v >> shift) & ((uint64_t(1) << n) - 1));
	}
	uint32_t get(int n)
	{
		if (n == 0) return 0;
		uint32_t v = peek(n);
		pos += uint32_t(n);
		return v;
	}
};

[[noreturn]] void fail(const std::string &what, uint64_t offset)
{
	throw AudioDecodeError(what, offset);
}

struct GranuleChannel
{
	uint32_t part23Length = 0;
	uint32_t bigValues = 0;
	uint32_t globalGain = 0;
	uint32_t scalefacCompress = 0;
	bool windowSwitching = false;
	uint32_t blockType = 0;
	bool mixed = false;
	uint32_t tableSelect[3] = {};
	uint32_t subblockGain[3] = {};
	uint32_t region0Count = 0;
	uint32_t region1Count = 0;
	bool preflag = false;
	bool scalefacScale = false;
	bool count1Table = false;

	bool shortBlocks() const { return windowSwitching && blockType == 2; }
};

struct Scalefactors
{
	uint8_t l[23] = {};
	uint8_t s[13][3] = {};
	// MPEG-2 intensity stereo (right channel): the largest legal is_pos of each band, (1 << slen) - 1
	uint8_t lMax[23] = {};
	uint8_t sMax[13][3] = {};
};

int sfbRow(const Mp3FrameHeader &h)
{
	int base = h.version == 1 ? 0 : h.version == 2 ? 3 : 6;
	uint32_t r0 = h.version == 1 ? 44100u : h.version == 2 ? 22050u : 11025u;
	uint32_t r1 = h.version == 1 ? 48000u : h.version == 2 ? 24000u : 12000u;
	return base + (h.sampleRate == r0 ? 0 : h.sampleRate == r1 ? 1 : 2);
}

inline float pow2q(const Constants &c, int e4) // 2^(e4 / 4)
{
	int q = e4 & 3;
	return std::ldexp(c.quarter[q], (e4 - q) / 4);
}

inline int16_t toS16(float x)
{
	float v = std::floor(x * 32768.0f + 0.5f);
	return int16_t(v > 32767.0f ? 32767 : (v < -32768.0f ? -32768 : int(v)));
}

void imdct36(const Constants &c, const float *in, float *out36)
{
	float y[18];
	for (int n = 0; n < 18; ++n)
	{
		const float *row = c.imdct18[n];
		float acc = 0.0f;
		for (int k = 0; k < 18; ++k) acc += row[k] * in[k];
		y[n] = acc;
	}
	for (int i = 0; i < 9; ++i) out36[i] = y[9 + i];
	for (int i = 9; i < 27; ++i) out36[i] = -y[26 - i];
	for (int i = 27; i < 36; ++i) out36[i] = -y[i - 27];
}

void imdct12(const Constants &c, const float *in, float *out12)
{
	float y[6];
	for (int n = 0; n < 6; ++n)
	{
		const float *row = c.imdct6[n];
		float acc = 0.0f;
		for (int k = 0; k < 6; ++k) acc += row[k] * in[k];
		y[n] = acc;
	}
	for (int i = 0; i < 3; ++i) out12[i] = y[3 + i];
	for (int i = 3; i < 9; ++i) out12[i] = -y[8 - i];
	for (int i = 9; i < 12; ++i) out12[i] = -y[i - 9];
}

// One polyphase synthesis slot (2.4.3.4.10): 32 subband samples in, 32 PCM samples out. `ring` is the 1024-entry
// V vector kept as a ring whose newest 64 values start at `voff`.
void synthesisSlot(const Constants &c, float *ring, int &voff, const float *S, float *out32)
{
	float C[32];
	LeeDct<32>::run(S, C, c); // C[m] = sum_k S[k] cos(m (2k + 1) pi / 64)
	voff = (voff - 64) & 1023;
	float *V = ring + voff;
	// V[i] = sum_k cos((16 + i)(2k + 1) pi / 64) S[k]; cos(j(2k+1)pi/64) is odd about j = 32 and j = 64 (negated)
	for (int i = 0; i < 16; ++i) V[i] = C[i + 16];
	V[16] = 0.0f;
	for (int i = 17; i <= 48; ++i) V[i] = -C[48 - i];
	for (int i = 49; i < 64; ++i) V[i] = -C[i - 48];
	float acc[32];
	for (int j = 0; j < 32; ++j) acc[j] = 0.0f;
	for (int q = 0; q < 16; ++q)
	{
		const float *v = ring + ((voff + 64 * q + 32 * (q & 1)) & 1023);
		const float *d = c.dwin + 32 * q;
		for (int j = 0; j < 32; ++j) acc[j] += d[j] * v[j];
	}
	for (int j = 0; j < 32; ++j) out32[j] = acc[j];
}

} // namespace

// ---- header ----------------------------------------------------------------------------------------------------

bool parseMp3Header(const uint8_t *p, Mp3FrameHeader &h, const char **why)
{
	auto bad = [&](const char *text) {
		if (why) *why = text;
		return false;
	};
	if (p[0] != 0xFF || (p[1] & 0xE0) != 0xE0) return bad("no frame sync");
	unsigned versionBits = (p[1] >> 3) & 3;
	unsigned layerBits = (p[1] >> 1) & 3;
	if (versionBits == 1) return bad("reserved MPEG version");
	if (layerBits == 0) return bad("reserved layer");
	if (layerBits != 1) return bad(layerBits == 2 ? "Layer II is not supported" : "Layer I is not supported");
	unsigned bitrateIndex = p[2] >> 4;
	unsigned rateIndex = (p[2] >> 2) & 3;
	if (bitrateIndex == 0) return bad("free-format bitrate is not supported");
	if (bitrateIndex == 15) return bad("invalid bitrate index 15");
	if (rateIndex == 3) return bad("reserved sample rate index");
	h.version = versionBits == 3 ? 1 : versionBits == 2 ? 2 : 25;
	h.crcProtected = (p[1] & 1) == 0;
	h.bitrate = uint32_t(h.version == 1 ? kBitrateV1[bitrateIndex] : kBitrateV2[bitrateIndex]) * 1000u;
	h.sampleRate = kSampleRates[h.version == 1 ? 0 : h.version == 2 ? 1 : 2][rateIndex];
	h.padding = (p[2] >> 1) & 1;
	h.channelMode = p[3] >> 6;
	h.modeExtension = (p[3] >> 4) & 3;
	h.channels = h.channelMode == 3 ? 1 : 2;
	if (h.channelMode != 1) h.modeExtension = 0;
	h.samplesPerFrame = h.version == 1 ? 1152 : 576;
	h.frameBytes = (h.version == 1 ? 144u : 72u) * h.bitrate / h.sampleRate + (h.padding ? 1u : 0u);
	h.sideInfoBytes = h.version == 1 ? (h.channels == 1 ? 17u : 32u) : (h.channels == 1 ? 9u : 17u);
	if (h.frameBytes < h.headerBytes() + h.sideInfoBytes) return bad("frame shorter than its header and side information");
	return true;
}

// ---- decoder state ---------------------------------------------------------------------------------------------

struct Mp3Decoder::State
{
	float overlap[2][32][18];
	float synth[2][1024];
	int synthOffset[2];
	uint8_t reservoir[kMaxReservoir];
	int reservoirLen;
	// scratch for the frame being decoded
	uint8_t mainBuf[kMaxReservoir + kMaxFrameBytes + 16];
	int32_t is[576];
	float xr[2][576];
	float sw[3][192]; // short-block spectrum as [window][frequency]
	float samples[18][32];
	Scalefactors sf[2][2]; // [granule][channel]
	int nz[2];             // lines at or above this index are zero

	State() { reset(); }
	void reset()
	{
		std::memset(overlap, 0, sizeof(overlap));
		std::memset(synth, 0, sizeof(synth));
		synthOffset[0] = synthOffset[1] = 0;
		std::memset(reservoir, 0, sizeof(reservoir));
		reservoirLen = 0;
		std::memset(mainBuf, 0, sizeof(mainBuf));
		std::memset(is, 0, sizeof(is));
		std::memset(xr, 0, sizeof(xr));
		std::memset(sw, 0, sizeof(sw));
		std::memset(samples, 0, sizeof(samples));
		nz[0] = nz[1] = 0;
	}

	void decode(const Mp3FrameHeader &h, const uint8_t *frame, int16_t *out, uint64_t base);
	uint32_t readScalefactorsV1(BitReader &br, const GranuleChannel &g, int gr, int ch, const bool scfsi[4]);
	uint32_t readScalefactorsV2(BitReader &br, const Mp3FrameHeader &h, GranuleChannel &g, int ch);
	void huffman(BitReader &br, uint32_t endBit, const GranuleChannel &g, const Mp3FrameHeader &h, int ch, uint64_t base);
	void requantize(const GranuleChannel &g, const Mp3FrameHeader &h, const Scalefactors &sf, int ch);
	void stereo(const Mp3FrameHeader &h, const GranuleChannel gc[2], const Scalefactors &sfRight);
	void antialias(const GranuleChannel &g, int ch);
	void reorderShort(const GranuleChannel &g, const Mp3FrameHeader &h, int ch);
	void imdct(const GranuleChannel &g, int ch);
	void synthesize(int ch, int channels, int16_t *out);
};

uint32_t Mp3Decoder::State::readScalefactorsV1(BitReader &br, const GranuleChannel &g, int gr, int ch, const bool scfsi[4])
{
	Scalefactors &sf = this->sf[gr][ch];
	sf = Scalefactors();
	int slen1 = kSlen1[g.scalefacCompress], slen2 = kSlen2[g.scalefacCompress];
	uint32_t start = br.pos;
	if (g.shortBlocks())
	{
		if (g.mixed)
		{
			for (int i = 0; i < 8; ++i) sf.l[i] = uint8_t(br.get(slen1));
			for (int s = 3; s < 6; ++s)
				for (int w = 0; w < 3; ++w) sf.s[s][w] = uint8_t(br.get(slen1));
		}
		else
		{
			for (int s = 0; s < 6; ++s)
				for (int w = 0; w < 3; ++w) sf.s[s][w] = uint8_t(br.get(slen1));
		}
		for (int s = 6; s < 12; ++s)
			for (int w = 0; w < 3; ++w) sf.s[s][w] = uint8_t(br.get(slen2));
	}
	else
	{
		// four band groups share an scfsi flag: granule 1 may reuse granule 0's values for a group
		static const int kGroupStart[5] = {0, 6, 11, 16, 21};
		for (int grp = 0; grp < 4; ++grp)
		{
			for (int s = kGroupStart[grp]; s < kGroupStart[grp + 1]; ++s)
			{
				if (gr == 1 && scfsi[grp])
					sf.l[s] = this->sf[0][ch].l[s];
				else
					sf.l[s] = uint8_t(br.get(grp < 2 ? slen1 : slen2));
			}
		}
	}
	return br.pos - start;
}

uint32_t Mp3Decoder::State::readScalefactorsV2(BitReader &br, const Mp3FrameHeader &h, GranuleChannel &g, int ch)
{
	// 13818-3 2.4.3.2: scalefac_compress is 9 bits; the right channel of an intensity-stereo frame is partitioned differently.
	Scalefactors &sf = this->sf[0][ch];
	sf = Scalefactors();
	uint32_t sfc = g.scalefacCompress;
	bool intensityRight = (h.modeExtension & 1) && ch == 1;
	int slen[4] = {0, 0, 0, 0};
	int row;
	g.preflag = false;
	if (!intensityRight)
	{
		if (sfc < 400)
		{
			slen[0] = int((sfc >> 4) / 5);
			slen[1] = int((sfc >> 4) % 5);
			slen[2] = int((sfc & 15) >> 2);
			slen[3] = int(sfc & 3);
			row = 0;
		}
		else if (sfc < 500)
		{
			uint32_t v = sfc - 400;
			slen[0] = int((v >> 2) / 5);
			slen[1] = int((v >> 2) % 5);
			slen[2] = int(v & 3);
			row = 1;
		}
		else
		{
			uint32_t v = sfc - 500;
			slen[0] = int(v / 3);
			slen[1] = int(v % 3);
			row = 2;
			g.preflag = true;
		}
	}
	else
	{
		uint32_t v = sfc >> 1;
		if (v < 180)
		{
			slen[0] = int(v / 36);
			slen[1] = int((v % 36) / 6);
			slen[2] = int((v % 36) % 6);
			row = 3;
		}
		else if (v < 244)
		{
			uint32_t u = v - 180;
			slen[0] = int((u & 63) >> 4);
			slen[1] = int((u & 15) >> 2);
			slen[2] = int(u & 3);
			row = 4;
		}
		else
		{
			uint32_t u = v - 244;
			slen[0] = int(u / 3);
			slen[1] = int(u % 3);
			row = 5;
		}
	}
	int cls = g.shortBlocks() ? (g.mixed ? 2 : 1) : 0;
	uint32_t start = br.pos;
	int idx = 0;
	for (int p = 0; p < 4; ++p)
	{
		uint8_t maxPos = uint8_t((1 << slen[p]) - 1);
		for (int i = 0; i < kLsfPartitions[cls][row][p]; ++i, ++idx)
		{
			uint8_t v = uint8_t(br.get(slen[p]));
			if (cls == 0)
			{
				if (idx < 22) { sf.l[idx] = v; sf.lMax[idx] = maxPos; }
			}
			else if (cls == 1)
			{
				if (idx / 3 < 13) { sf.s[idx / 3][idx % 3] = v; sf.sMax[idx / 3][idx % 3] = maxPos; }
			}
			else if (idx < 6)
			{
				sf.l[idx] = v;
				sf.lMax[idx] = maxPos;
			}
			else
			{
				int j = idx - 6;
				if (3 + j / 3 < 13) { sf.s[3 + j / 3][j % 3] = v; sf.sMax[3 + j / 3][j % 3] = maxPos; }
			}
		}
	}
	return br.pos - start;
}

void Mp3Decoder::State::huffman(BitReader &br, uint32_t endBit, const GranuleChannel &g, const Mp3FrameHeader &h, int ch, uint64_t base)
{
	const Constants &c = constants();
	std::memset(is, 0, sizeof(is));
	uint32_t nBig = g.bigValues * 2;
	if (nBig > 576) fail("big_values " + std::to_string(g.bigValues) + " exceeds 288", base);
	const uint16_t *sfbL = kSfbLong[sfbRow(h)];
	uint32_t region1, region2;
	if (g.windowSwitching)
	{
		// Decision: with window switching the region counts are implicit (2.4.2.7): region 0 ends at line 36 for short
		// and mixed blocks and at the 8th long band for start / stop blocks; only two table_select values are sent, so
		// the second region runs to the end of the big values.
		region1 = g.blockType == 2 ? 3u * kSfbShort[sfbRow(h)][3] : sfbL[8];
		region2 = 576;
	}
	else
	{
		unsigned r1 = g.region0Count + 1;
		unsigned r2 = g.region0Count + g.region1Count + 2;
		region1 = sfbL[std::min(r1, 22u)];
		region2 = sfbL[std::min(r2, 22u)];
	}
	region1 = std::min(region1, nBig);
	region2 = std::min(region2, nBig);

	uint32_t line = 0;
	for (int region = 0; region < 3; ++region)
	{
		uint32_t stop = region == 0 ? region1 : region == 1 ? region2 : nBig;
		uint32_t tableNo = g.tableSelect[region];
		if (stop <= line) continue;
		if (tableNo == 0)
		{
			line = stop; // table 0: no bits, zero values
			continue;
		}
		if (tableNo == 4 || tableNo == 14) fail("Huffman table " + std::to_string(tableNo) + " is not defined", base);
		int linbits = 0;
		int ti;
		if (tableNo >= 24)
		{
			ti = c.bigIndex[24];
			linbits = kLinbits24[tableNo - 24];
		}
		else if (tableNo >= 16)
		{
			ti = c.bigIndex[16];
			linbits = kLinbits16[tableNo - 16];
		}
		else
		{
			ti = c.bigIndex[tableNo];
		}
		const HuffLut &H = c.big[ti];
		const uint16_t *lut = H.lut.data();
		int maxLen = H.maxLen;
		for (; line < stop; line += 2)
		{
			uint16_t e = lut[br.peek(maxLen)];
			int len = e >> 8;
			if (len == 0) fail("invalid Huffman code in table " + std::to_string(tableNo), base + br.pos / 8);
			br.pos += uint32_t(len);
			int x = (e >> 4) & 15, y = e & 15;
			if (linbits && x == 15) x += int(br.get(linbits));
			if (x && br.get(1)) x = -x;
			if (linbits && y == 15) y += int(br.get(linbits));
			if (y && br.get(1)) y = -y;
			is[line] = x;
			is[line + 1] = y;
			if (br.pos > endBit) fail("Huffman data of a granule overruns its part2_3_length", base + br.pos / 8);
		}
	}
	// count1 region: quadruples of -1 / 0 / 1 until part2_3_length is used up
	const uint16_t *qlut = c.quad[g.count1Table ? 1 : 0];
	line = nBig;
	while (line + 4 <= 576 && br.pos < endBit)
	{
		uint16_t e = qlut[br.peek(6)];
		int len = e >> 8;
		if (len == 0) fail("invalid count1 Huffman code", base + br.pos / 8);
		br.pos += uint32_t(len);
		int v = e & 15;
		int q[4] = {(v >> 3) & 1, (v >> 2) & 1, (v >> 1) & 1, v & 1};
		for (int k = 0; k < 4; ++k)
		{
			if (q[k] && br.get(1)) q[k] = -1;
		}
		if (br.pos > endBit) break; // the final quadruple ran past the granule's bits: padding, not data
		for (int k = 0; k < 4; ++k) is[line + k] = q[k];
		line += 4;
	}
	nz[ch] = int(line);
}

void Mp3Decoder::State::requantize(const GranuleChannel &g, const Mp3FrameHeader &h, const Scalefactors &sf, int ch)
{
	const Constants &c = constants();
	float *xr = this->xr[ch];
	int n = nz[ch];
	std::memset(xr, 0, sizeof(this->xr[ch]));
	int row = sfbRow(h);
	const uint16_t *sfbL = kSfbLong[row];
	const uint16_t *sfbS = kSfbShort[row];
	int gain = int(g.globalGain) - 210;
	int mult = g.scalefacScale ? 4 : 2; // quarter-octaves per scalefactor step

	auto lineValue = [&](int i, float factor) {
		int v = is[i];
		if (v == 0) return;
		float m = c.pow43[v < 0 ? -v : v] * factor;
		xr[i] = v < 0 ? -m : m;
	};

	bool shortBlocks = g.shortBlocks();
	if (!shortBlocks || g.mixed)
	{
		int longBands = 22;
		if (shortBlocks)
		{
			longBands = 0;
			while (longBands < 22 && sfbL[longBands] != 36) ++longBands;
		}
		for (int s = 0; s < longBands; ++s)
		{
			int a = sfbL[s], b = std::min<int>(sfbL[s + 1], n);
			if (a >= b) break;
			int e4 = gain - mult * (sf.l[s] + (g.preflag && !shortBlocks ? kPretab[s] : 0));
			float factor = pow2q(c, e4);
			for (int i = a; i < b; ++i) lineValue(i, factor);
		}
	}
	if (shortBlocks)
	{
		int first = g.mixed ? 3 : 0;
		if (g.mixed && sfbS[3] * 3 != 36) fail("mixed blocks at this sample rate are not supported", 0);
		for (int s = first; s < 13; ++s)
		{
			int width = sfbS[s + 1] - sfbS[s];
			for (int w = 0; w < 3; ++w)
			{
				int a = 3 * sfbS[s] + w * width;
				if (a >= n) continue;
				int e4 = gain - 8 * int(g.subblockGain[w]) - mult * sf.s[s][w];
				float factor = pow2q(c, e4);
				for (int i = a; i < a + width && i < n; ++i) lineValue(i, factor);
			}
		}
	}
}

void Mp3Decoder::State::stereo(const Mp3FrameHeader &h, const GranuleChannel gc[2], const Scalefactors &sfRight)
{
	if (h.channels != 2 || h.channelMode != 1 || h.modeExtension == 0) return;
	const Constants &c = constants();
	bool ms = (h.modeExtension & 2) != 0;
	bool intensity = (h.modeExtension & 1) != 0;
	float *l = xr[0], *r = xr[1];
	int nzMax = std::max(nz[0], nz[1]);
	const float kInvSqrt2 = 0.70710678118654752f;
	int row = sfbRow(h);
	const uint16_t *sfbL = kSfbLong[row];
	const uint16_t *sfbS = kSfbShort[row];

	auto midSide = [&](int a, int b) {
		for (int i = a; i < b; ++i)
		{
			float m = l[i], s = r[i];
			l[i] = (m + s) * kInvSqrt2;
			r[i] = (m - s) * kInvSqrt2;
		}
	};
	// both channels can be non-zero up to the longer of the two spectra afterwards (the alias reduction needs this)
	nz[0] = nz[1] = nzMax;
	if (!intensity)
	{
		if (ms) midSide(0, nzMax);
		return;
	}
	// Intensity stereo (2.4.3.4.9.3, 13818-3 2.4.3.4.9.3): bands above the last non-zero right-channel line are
	// intensity coded, the position is the right channel's scalefactor of the band. NOT verified against a
	// reference decoder (no retail file and no available encoder produces it); implemented from the standard.
	const GranuleChannel &gR = gc[1];
	float lsfK = (gR.scalefacCompress & 1) ? 0.70710678118654752f : 0.84089641525371454f;
	auto applyBand = [&](int a, int b, int isPos, int isMax) {
		float gl = 1.0f, gr = 1.0f;
		bool illegal;
		if (h.version == 1)
		{
			illegal = isPos >= 7;
			if (!illegal)
			{
				float ratio = c.tanRatio[isPos];
				gl = ratio / (1.0f + ratio);
				gr = 1.0f / (1.0f + ratio);
			}
		}
		else
		{
			illegal = isPos == isMax;
			if (!illegal)
			{
				if (isPos & 1)
					gl = std::pow(lsfK, float((isPos + 1) / 2));
				else if (isPos > 0)
					gr = std::pow(lsfK, float(isPos / 2));
			}
		}
		if (illegal)
		{
			if (ms) midSide(a, b); // "illegal" position: the band is coded as ordinary (M/S or L/R) stereo
			return;
		}
		for (int i = a; i < b; ++i)
		{
			float x = l[i];
			l[i] = x * gl;
			r[i] = x * gr;
		}
	};
	if (!gR.shortBlocks())
	{
		int firstBand = 22;
		for (int s = 0; s < 22; ++s)
		{
			if (sfbL[s] >= nz[1])
			{
				firstBand = s;
				break;
			}
		}
		if (ms) midSide(0, sfbL[firstBand]);
		for (int s = firstBand; s < 22; ++s)
		{
			int sfIndex = s < 21 ? s : 20;
			applyBand(sfbL[s], sfbL[s + 1], sfRight.l[sfIndex], sfRight.lMax[sfIndex]);
		}
	}
	else
	{
		for (int w = 0; w < 3; ++w)
		{
			int firstBand = 13;
			for (int s = 12; s >= 0; --s)
			{
				int width = sfbS[s + 1] - sfbS[s];
				int a = 3 * sfbS[s] + w * width;
				bool nonzero = false;
				for (int i = a; i < a + width && !nonzero; ++i) nonzero = r[i] != 0.0f;
				if (nonzero) break;
				firstBand = s;
			}
			for (int s = 0; s < 13; ++s)
			{
				int width = sfbS[s + 1] - sfbS[s];
				int a = 3 * sfbS[s] + w * width;
				if (s < firstBand)
				{
					if (ms) midSide(a, a + width);
				}
				else
				{
					int sfIndex = s < 12 ? s : 11;
					applyBand(a, a + width, sfRight.s[sfIndex][w], sfRight.sMax[sfIndex][w]);
				}
			}
		}
	}
}

void Mp3Decoder::State::antialias(const GranuleChannel &g, int ch)
{
	if (g.shortBlocks() && !g.mixed) return;
	const Constants &c = constants();
	float *xr = this->xr[ch];
	int boundaries = g.shortBlocks() ? 1 : 31; // mixed blocks: only between subbands 0 and 1
	for (int sb = 1; sb <= boundaries; ++sb)
	{
		if (18 * sb - 8 >= nz[ch]) break;
		float *p = xr + 18 * sb;
		for (int i = 0; i < 8; ++i)
		{
			float a = p[-1 - i], b = p[i];
			p[-1 - i] = a * c.cs[i] - b * c.ca[i];
			p[i] = b * c.cs[i] + a * c.ca[i];
		}
	}
}

void Mp3Decoder::State::reorderShort(const GranuleChannel &g, const Mp3FrameHeader &h, int ch)
{
	const uint16_t *sfbS = kSfbShort[sfbRow(h)];
	const float *xr = this->xr[ch];
	std::memset(sw, 0, sizeof(sw));
	for (int s = g.mixed ? 3 : 0; s < 13; ++s)
	{
		int width = sfbS[s + 1] - sfbS[s];
		for (int w = 0; w < 3; ++w)
			for (int j = 0; j < width; ++j) sw[w][sfbS[s] + j] = xr[3 * sfbS[s] + w * width + j];
	}
}

void Mp3Decoder::State::imdct(const GranuleChannel &g, int ch)
{
	const Constants &c = constants();
	const float *xr = this->xr[ch];
	bool shortBlocks = g.shortBlocks();
	for (int sb = 0; sb < 32; ++sb)
	{
		float *ov = overlap[ch][sb];
		float res[18];
		bool shortSb = shortBlocks && !(g.mixed && sb < 2);
		if (!shortSb)
		{
			const float *in = xr + 18 * sb;
			bool zero = true;
			for (int k = 0; k < 18; ++k)
			{
				if (in[k] != 0.0f)
				{
					zero = false;
					break;
				}
			}
			if (zero)
			{
				for (int t = 0; t < 18; ++t)
				{
					res[t] = ov[t];
					ov[t] = 0.0f;
				}
			}
			else
			{
				int bt = shortBlocks ? 0 : int(g.blockType); // the low two subbands of a mixed block use the normal window
				const float *win = c.win36[bt];
				float o[36];
				imdct36(c, in, o);
				for (int t = 0; t < 18; ++t)
				{
					res[t] = o[t] * win[t] + ov[t];
					ov[t] = o[18 + t] * win[18 + t];
				}
			}
		}
		else
		{
			float tmp[36];
			for (int t = 0; t < 36; ++t) tmp[t] = 0.0f;
			for (int w = 0; w < 3; ++w)
			{
				const float *in = sw[w] + 6 * sb;
				bool zero = true;
				for (int k = 0; k < 6; ++k) zero = zero && in[k] == 0.0f;
				if (zero) continue;
				float o[12];
				imdct12(c, in, o);
				for (int i = 0; i < 12; ++i) tmp[6 + 6 * w + i] += o[i] * c.win12[i];
			}
			for (int t = 0; t < 18; ++t)
			{
				res[t] = tmp[t] + ov[t];
				ov[t] = tmp[18 + t];
			}
		}
		if (sb & 1)
		{
			for (int t = 1; t < 18; t += 2) res[t] = -res[t]; // frequency inversion
		}
		for (int t = 0; t < 18; ++t) samples[t][sb] = res[t];
	}
}

void Mp3Decoder::State::synthesize(int ch, int channels, int16_t *out)
{
	const Constants &c = constants();
	float pcm[32];
	for (int t = 0; t < 18; ++t)
	{
		synthesisSlot(c, synth[ch], synthOffset[ch], samples[t], pcm);
		int16_t *dst = out + size_t(t) * 32 * channels + ch;
		for (int j = 0; j < 32; ++j) dst[size_t(j) * channels] = toS16(pcm[j]);
	}
}

void Mp3Decoder::State::decode(const Mp3FrameHeader &h, const uint8_t *frame, int16_t *out, uint64_t base)
{
	const int nch = h.channels;
	const int ngr = h.version == 1 ? 2 : 1;
	// ---- side information (2.4.1.7), copied into a zero-padded buffer so the bit reader never leaves the frame
	uint8_t sibuf[64] = {};
	std::memcpy(sibuf, frame + h.headerBytes(), h.sideInfoBytes);
	BitReader si;
	si.buf = sibuf;
	uint32_t mainDataBegin = si.get(h.version == 1 ? 9 : 8);
	si.get(h.version == 1 ? (nch == 1 ? 5 : 3) : (nch == 1 ? 1 : 2)); // private bits
	bool scfsi[2][4] = {};
	if (h.version == 1)
	{
		for (int ch = 0; ch < nch; ++ch)
			for (int b = 0; b < 4; ++b) scfsi[ch][b] = si.get(1) != 0;
	}
	GranuleChannel gc[2][2];
	for (int gr = 0; gr < ngr; ++gr)
	{
		for (int ch = 0; ch < nch; ++ch)
		{
			GranuleChannel &g = gc[gr][ch];
			g.part23Length = si.get(12);
			g.bigValues = si.get(9);
			g.globalGain = si.get(8);
			g.scalefacCompress = si.get(h.version == 1 ? 4 : 9);
			g.windowSwitching = si.get(1) != 0;
			if (g.windowSwitching)
			{
				g.blockType = si.get(2);
				g.mixed = si.get(1) != 0;
				g.tableSelect[0] = si.get(5);
				g.tableSelect[1] = si.get(5);
				for (int w = 0; w < 3; ++w) g.subblockGain[w] = si.get(3);
				if (g.blockType == 0) fail("block_type 0 together with window_switching_flag", base);
				if (g.blockType != 2) g.mixed = false;
			}
			else
			{
				for (int k = 0; k < 3; ++k) g.tableSelect[k] = si.get(5);
				g.region0Count = si.get(4);
				g.region1Count = si.get(3);
			}
			g.preflag = h.version == 1 ? si.get(1) != 0 : false;
			g.scalefacScale = si.get(1) != 0;
			g.count1Table = si.get(1) != 0;
		}
	}
	// ---- main data: the bit reservoir (2.4.3.1) supplies main_data_begin bytes that precede this frame's own
	const uint8_t *data = frame + h.headerBytes() + h.sideInfoBytes;
	size_t dataLen = h.frameBytes - h.headerBytes() - h.sideInfoBytes;
	if (mainDataBegin > uint32_t(reservoirLen))
	{
		fail("bit reservoir underflow: main_data_begin " + std::to_string(mainDataBegin) + " but only " + std::to_string(reservoirLen) +
				" reservoir bytes are available",
			base);
	}
	std::memcpy(mainBuf, reservoir + reservoirLen - mainDataBegin, mainDataBegin);
	std::memcpy(mainBuf + mainDataBegin, data, dataLen);
	size_t total = mainDataBegin + dataLen;
	std::memset(mainBuf + total, 0, 8);

	uint32_t cursor = 0;
	for (int gr = 0; gr < ngr; ++gr)
	{
		for (int ch = 0; ch < nch; ++ch)
		{
			GranuleChannel &g = gc[gr][ch];
			uint32_t endBit = cursor + g.part23Length;
			if (endBit > total * 8)
			{
				fail("granule " + std::to_string(gr) + " channel " + std::to_string(ch) + " needs " + std::to_string(g.part23Length) +
						" bits but the frame's main data ends " + std::to_string(endBit - total * 8) + " bits earlier",
					base);
			}
			BitReader br;
			br.buf = mainBuf;
			br.pos = cursor;
			uint32_t scaleBits = h.version == 1 ? readScalefactorsV1(br, g, gr, ch, scfsi[ch]) : readScalefactorsV2(br, h, g, ch);
			if (scaleBits > g.part23Length) fail("scalefactors overrun part2_3_length", base);
			huffman(br, endBit, g, h, ch, base);
			requantize(g, h, sf[h.version == 1 ? gr : 0][ch], ch);
			cursor = endBit;
		}
		if (nch == 2) stereo(h, gc[gr], sf[h.version == 1 ? gr : 0][1]);
		for (int ch = 0; ch < nch; ++ch)
		{
			const GranuleChannel &g = gc[gr][ch];
			antialias(g, ch);
			if (g.shortBlocks()) reorderShort(g, h, ch);
			imdct(g, ch);
			synthesize(ch, nch, out + size_t(gr) * 576 * nch);
		}
	}
	// ---- reservoir: keep the last 511 bytes of (old reservoir ++ this frame's main data)
	if (dataLen >= size_t(kMaxReservoir))
	{
		std::memcpy(reservoir, data + dataLen - kMaxReservoir, kMaxReservoir);
		reservoirLen = kMaxReservoir;
	}
	else
	{
		int keep = std::min(reservoirLen, kMaxReservoir - int(dataLen));
		std::memmove(reservoir, reservoir + reservoirLen - keep, size_t(keep));
		std::memcpy(reservoir + keep, data, dataLen);
		reservoirLen = keep + int(dataLen);
	}
}

// ---- Mp3Decoder ------------------------------------------------------------------------------------------------

Mp3Decoder::Mp3Decoder() : m_state(new State()) {}
Mp3Decoder::~Mp3Decoder() = default;

void Mp3Decoder::reset()
{
	m_state->reset();
}

size_t Mp3Decoder::decodeFrame(const Mp3FrameHeader &header, const uint8_t *frame, int16_t *out, uint64_t streamOffset)
{
	m_state->decode(header, frame, out, streamOffset);
	return header.samplesPerFrame;
}

// ---- Mp3Stream -------------------------------------------------------------------------------------------------

namespace
{

uint32_t rdBe32(const uint8_t *p)
{
	return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}

bool sameStream(const Mp3FrameHeader &a, const Mp3FrameHeader &b)
{
	return a.version == b.version && a.sampleRate == b.sampleRate && a.channels == b.channels;
}

// A frame candidate found while searching through junk must be followed by another consistent frame (or by the
// end of the data / an ID3v1 tag) so random 0xFF 0xFB byte pairs in junk are not taken for frames.
bool chainOk(const uint8_t *d, size_t size, size_t pos, const Mp3FrameHeader *ref)
{
	Mp3FrameHeader h;
	if (size - pos < 4 || !parseMp3Header(d + pos, h)) return false;
	if (ref && !sameStream(*ref, h)) return false;
	size_t next = pos + h.frameBytes;
	if (next > size) return false;
	if (next == size) return true;
	if (size - next == 128 && std::memcmp(d + next, "TAG", 3) == 0) return true;
	Mp3FrameHeader h2;
	return size - next >= 4 && parseMp3Header(d + next, h2) && sameStream(h, h2);
}

size_t findChain(const uint8_t *d, size_t size, size_t from, const Mp3FrameHeader *ref, size_t scanLimit = size_t(-1))
{
	size_t last = scanLimit < size ? scanLimit : size;
	for (size_t p = from; p + 4 <= size && p < last; ++p)
	{
		if (d[p] == 0xFF && (d[p + 1] & 0xE0) == 0xE0 && chainOk(d, size, p, ref)) return p;
	}
	return size_t(-1);
}

} // namespace

bool looksLikeMp3(const uint8_t *data, size_t size)
{
	return findChain(data, size, 0, nullptr, 65536) != size_t(-1);
}

Mp3Stream::Mp3Stream(const uint8_t *data, size_t size, std::vector<std::string> *warnings) : m_data(data), m_size(size)
{
	auto warn = [&](const std::string &text) {
		if (warnings) warnings->push_back(text);
	};
	size_t pos = 0;
	// ID3v2 tag(s): "ID3", version, revision, flags, 4 x 7-bit size (2.4.3.2 of the ID3v2 informal standard)
	while (size - pos >= 10 && std::memcmp(data + pos, "ID3", 3) == 0 && data[pos + 3] != 0xFF && data[pos + 4] != 0xFF &&
		(data[pos + 6] | data[pos + 7] | data[pos + 8] | data[pos + 9]) < 0x80)
	{
		uint32_t tagSize = (uint32_t(data[pos + 6]) << 21) | (uint32_t(data[pos + 7]) << 14) | (uint32_t(data[pos + 8]) << 7) | data[pos + 9];
		size_t total = 10 + size_t(tagSize) + ((data[pos + 3] >= 4 && (data[pos + 5] & 0x10)) ? 10 : 0);
		if (total > size - pos) fail("ID3v2 tag of " + std::to_string(total) + " bytes runs past the end of the file", pos);
		pos += total;
	}
	// first frame
	Mp3FrameHeader first;
	const char *why = nullptr;
	if (size - pos >= 4 && parseMp3Header(data + pos, first, &why))
	{
		m_firstFrame = pos;
	}
	else
	{
		size_t q = findChain(data, size, pos, nullptr);
		if (q == size_t(-1))
		{
			fail(size - pos < 4 ? "no MPEG audio frame found" : std::string("no valid MPEG Layer III frame found (first header: ") + (why ? why : "invalid") + ")", pos);
		}
		// Decision: bytes between the ID3v2 tag and the first frame are skipped and reported.
		warn("junk-before-first-frame: " + std::to_string(q - pos) + " byte(s) at offset " + std::to_string(pos));
		m_firstFrame = q;
		parseMp3Header(data + q, first);
	}
	if (m_firstFrame + first.frameBytes > size)
	{
		fail("the only MPEG frame is truncated (" + std::to_string(size - m_firstFrame) + " of " + std::to_string(first.frameBytes) +
				" bytes)",
			m_firstFrame);
	}
	m_samplesPerFrame = first.samplesPerFrame;

	// Xing / Info metadata frame (optionally with a LAME tag): not audio
	size_t audioStart = m_firstFrame;
	{
		size_t tagAt = m_firstFrame + first.headerBytes() + first.sideInfoBytes;
		size_t frameEnd = m_firstFrame + first.frameBytes;
		if (frameEnd - tagAt >= 8 && (std::memcmp(data + tagAt, "Xing", 4) == 0 || std::memcmp(data + tagAt, "Info", 4) == 0))
		{
			uint32_t flags = rdBe32(data + tagAt + 4);
			size_t p = tagAt + 8;
			if (flags & 1) p += 4;
			if (flags & 2) p += 4;
			if (flags & 4) p += 100;
			if (flags & 8) p += 4;
			if (p + 24 <= frameEnd)
			{
				const uint8_t *enc = data + p;
				bool lame = std::memcmp(enc, "LAME", 4) == 0 || std::memcmp(enc, "Lavf", 4) == 0 || std::memcmp(enc, "Lavc", 4) == 0;
				if (lame)
				{
					m_encoderDelay = (uint32_t(enc[21]) << 4) | (enc[22] >> 4);
					m_encoderPadding = (uint32_t(enc[22] & 15) << 8) | enc[23];
					m_gapless = true;
				}
			}
			audioStart = frameEnd;
		}
	}
	m_vbri = audioStart == m_firstFrame && m_firstFrame + 40 <= size && std::memcmp(data + m_firstFrame + 36, "VBRI", 4) == 0;
	m_audioStart = audioStart;
	m_walkPos = audioStart;
	m_streamHeader = first;

	// walk every frame header
	pos = audioStart;
	uint32_t count = 0;
	m_endOffset = size;
	while (pos < size)
	{
		if (size - pos < 4)
		{
			warn("trailing-bytes: " + std::to_string(size - pos) + " byte(s) after the last MPEG frame at offset " + std::to_string(pos));
			m_endOffset = pos;
			break;
		}
		Mp3FrameHeader h;
		if (parseMp3Header(data + pos, h))
		{
			if (!sameStream(first, h))
			{
				fail("MPEG stream parameters change at this frame (" + std::to_string(h.sampleRate) + " Hz " +
						std::to_string(h.channels) + " ch, was " + std::to_string(first.sampleRate) + " Hz " + std::to_string(first.channels) +
						" ch)",
					pos);
			}
			if (pos + h.frameBytes > size)
			{
				// Decision: a final frame that is cut short cannot be decoded (its main data is incomplete); it is dropped and reported.
				warn("truncated-last-frame: MPEG frame at offset " + std::to_string(pos) + " (" + std::to_string(size - pos) + " of " +
					std::to_string(h.frameBytes) + " bytes) dropped");
				m_endOffset = pos;
				break;
			}
			++count;
			pos += h.frameBytes;
			continue;
		}
		// not a frame header here
		if (size - pos == 128 && std::memcmp(data + pos, "TAG", 3) == 0)
		{
			m_endOffset = pos; // ID3v1 tag: standard trailer, not a defect
			break;
		}
		size_t q = findChain(data, size, pos + 1, &first);
		if (q == size_t(-1))
		{
			// Decision: junk after the last frame is ignored and reported (an ID3v1 tag is not reported).
			warn("trailing-bytes: " + std::to_string(size - pos) + " byte(s) after the last MPEG frame at offset " + std::to_string(pos));
			m_endOffset = pos;
			break;
		}
		// Decision: junk between frames is skipped and reported; the frame after it may then fail on a reservoir underflow.
		warn("junk-between-frames: " + std::to_string(q - pos) + " byte(s) at offset " + std::to_string(pos));
		pos = q;
	}
	m_audioFrameCount = count;
	if (count == 0) fail("the file contains no audio frame", audioStart);

	uint64_t total = uint64_t(count) * first.samplesPerFrame;
	uint64_t skip = 0, endTrim = 0;
	if (m_gapless)
	{
		// LAME tag: the decoder delay is 528 + 1 samples; trim delay + 529 at the start, padding - 529 at the end.
		skip = uint64_t(m_encoderDelay) + 529;
		endTrim = m_encoderPadding > 529 ? m_encoderPadding - 529 : 0;
		if (skip + endTrim >= total)
		{
			warn("gapless-tag: LAME tag delay " + std::to_string(m_encoderDelay) + " / padding " + std::to_string(m_encoderPadding) +
				" exceed the stream length; gapless trimming ignored");
			m_gapless = false;
			skip = endTrim = 0;
		}
	}
	m_skipRemaining = skip;
	m_initialSkip = skip;
	m_info.format = Format::Mp3;
	m_info.sampleRate = first.sampleRate;
	m_info.channels = first.channels;
	m_info.frames = total - skip - endTrim;
	m_info.bitsPerSample = 16;
	m_info.blockAlign = 0;
	m_decoder.reset(new Mp3Decoder());
	m_frameBuf.assign(size_t(first.samplesPerFrame) * first.channels, 0);
}

Mp3Stream::~Mp3Stream() = default;

void Mp3Stream::rewind()
{
	m_decoder->reset();
	m_delivered = 0;
	m_frameBufPos = m_frameBufCount = 0;
	m_walkPos = m_audioStart;
	m_skipRemaining = m_initialSkip;
}

bool Mp3Stream::nextFrame(Mp3FrameHeader &h, size_t &offset)
{
	const Mp3FrameHeader &first = m_streamHeader;
	while (m_walkPos < m_endOffset)
	{
		if (m_endOffset - m_walkPos >= 4 && parseMp3Header(m_data + m_walkPos, h) && sameStream(first, h) &&
			m_walkPos + h.frameBytes <= m_endOffset)
		{
			offset = m_walkPos;
			m_walkPos += h.frameBytes;
			return true;
		}
		size_t q = findChain(m_data, m_size, m_walkPos + 1, &first);
		if (q == size_t(-1) || q >= m_endOffset) return false;
		m_walkPos = q;
	}
	return false;
}

size_t Mp3Stream::read(int16_t *out, size_t maxFrames)
{
	size_t delivered = 0;
	const size_t channels = m_info.channels;
	while (delivered < maxFrames && m_delivered < m_info.frames)
	{
		if (m_frameBufPos >= m_frameBufCount)
		{
			Mp3FrameHeader h;
			size_t offset = 0;
			if (!nextFrame(h, offset)) break;
			m_frameBufCount = m_decoder->decodeFrame(h, m_data + offset, m_frameBuf.data(), offset);
			m_frameBufPos = 0;
			if (m_skipRemaining)
			{
				size_t drop = size_t(std::min<uint64_t>(m_skipRemaining, m_frameBufCount));
				m_frameBufPos = drop;
				m_skipRemaining -= drop;
			}
			continue;
		}
		size_t n = std::min<size_t>(m_frameBufCount - m_frameBufPos, maxFrames - delivered);
		n = size_t(std::min<uint64_t>(n, m_info.frames - m_delivered));
		std::memcpy(out + delivered * channels, m_frameBuf.data() + m_frameBufPos * channels, n * channels * sizeof(int16_t));
		m_frameBufPos += n;
		delivered += n;
		m_delivered += n;
	}
	return delivered;
}

} // namespace AudioDecode
