// OpenBFME. GPL-3.0.
//
// The game-logic random number generator. Port of ZH GameEngine/Source/Common/RandomValue.cpp
// (GetGameLogicRandomValue / GetGameLogicRandomValueReal, InitGameLogicRandom, seedRandom,
// randomValue) with the retail variation recorded below. Lane HORDE-1; stop S-080.
//
// TARGET FACTS (RotWK game.dat on this machine; PLAN rule 9 / stop S-001 apply):
//   * GetGameLogicRandomValue(lo, hi, file, line)       RW 0x6D328E
//       delta = hi - lo + 1 (unsigned); delta == 0 returns hi; else (randomValue(logic) % delta) + lo.
//       When the log file global (RW 0xDE4A30) is set it also formats a line; not ported (no I/O).
//   * GetGameLogicRandomValueReal(lo, hi, file, line)   RW 0x6D332C
//       delta = hi - lo (SSE subss); delta <= 0 (ordered) returns hi; else
//       fild(int32 r), + 2^32 (RW 0xBD8698) when r was negative, * theMultFactor (RW 0xDE4A6C),
//       * delta, + lo, stored as float32. The x87 steps run at PC24 (setFPMode, RW 0x440809).
//   * theMultFactor = 1.0f / (powf(2, 32) - 1.0f) (static initialiser RW 0xBC3741..0xBC376F; the
//       fsub rounds 2^32 - 1 to 2^32 at PC24, so the stored float is exactly 2^-32).
//   * the logic seed array is at RW 0xDA1CA4 (client 0xDA1C74, audio 0xDA1C8C); each starts
//       {0xF22D0E56, 0x883126E9, 0xC624DD2F, 0x0702C49C, 0x9E353F7D, 0x6FDF3B64}.
//   * InitGameLogicRandom(seed) RW 0x6D3261: if TheGameLogic (RW 0xDE4364) exists and its field at
//       +0x1228 (the frame counter) is not -1, THAT value seeds the generator instead of `seed`;
//       the value actually used is stored in theGameLogicBaseSeed (RW 0xDE4A68, GetGameLogicRandomSeed
//       0x6D3204). InitRandom(seed) RW 0x6D321C does the same for all three arrays.
//   * randomValue(seed*) RW 0x6D315D (21 bytes) is NOT the ZH carry-add generator:
//         eax = seed[0]; edx:eax = eax * 0x08088405; eax += 1; seed[0] = eax; return eax ^ edx;
//     seed[1..5] are never read or written by it. seedRandom(SEED, seed*) RW 0x6D31D4:
//         edx:eax = SEED * 0x7FFFFFED; seed[0..5] = eax (all six words get the same value).
//     The 119-byte region RW 0x6D315D..0x6D31D3 (the size of BFME2 1.06's randomValue at 0x633EC3)
//     holds that 21-byte function followed by 98 NOP bytes, and 0x6D31D4 replaces a seedRandom of
//     the same slot: the signature of a binary patch.
//
// DONOR FACTS:
//   * ZH RandomValue.cpp randomValue(): six-word add-with-carry chain, then an increment that
//     bubbles carries through seed[5]..seed[0]; seedRandom adds the constant differences of the
//     initial array to SEED word by word.
//   * BFME2 1.06 game.dat (the clean donor binary; MD5-verified install) contains exactly that ZH
//     generator at 0x633EC3 (119 bytes: add/cmp/jb carry chain) with seed arrays at 0xDBA3A0,
//     0xDBA3B8, 0xDBA3D0. Open-BFME-2 `Code/GameEngine/Source/Common/RandomValue_randomValue.cpp`
//     reproduces it.
//
// WHAT THIS MEANS (acceptance stop S-080): the RotWK game.dat on this machine is community
// modified (S-001) and its RNG differs from BFME2 1.06 and ZH. Whether a clean RotWK 2.01 carries
// the ZH generator (the patch replaced it) or the LCG (EA changed it in 2.01) cannot be decided
// without a clean binary or a retail replay/trace. Both generators are therefore implemented and
// the caller must NAME one: there is no default. Anything that consumes this RNG for retail
// parity (horde slot jitter, ...) inherits the stop.
//
// Instance based instead of ZH's file-scope statics so tests and sims can run side by side; the
// ZH-named free functions below operate on one process-wide instance that must be initialised.

#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

enum class RandomAlgorithm
{
	// ZH RandomValue.cpp, BFME2 1.06 game.dat 0x633EC3. Six-word carry chain.
	ZH_CarryChain,
	// RotWK game.dat on this machine, RW 0x6D315D: seed[0] * 0x08088405 + 1, returned xor the high word.
	RotWK_GameDat_LCG
};

class GameLogicRandom
{
public:
	typedef std::array<std::uint32_t, 6> Seed;

	// The static initial seed array of all three generators (RW 0xDA1CA4 / BFME2 0xDBA3D0 / ZH).
	static Seed initialSeedArray();

	explicit GameLogicRandom(RandomAlgorithm algorithm);

	RandomAlgorithm algorithm() const { return m_algorithm; }

	// InitGameLogicRandom (RW 0x6D3261). `frameOverride` is TheGameLogic+0x1228 (the logic frame
	// counter) when TheGameLogic exists, or -1 when it does not / has no override: any value other
	// than -1 replaces `seed`. The value used is returned and kept as the base seed.
	std::uint32_t initGameLogicRandom(std::uint32_t seed, std::int32_t frameOverride);

	// seedRandom (ZH / RW 0x6D31D4) on the instance's own array.
	void seedRandom(std::uint32_t seed);

	// Acceptance stop report (PLAN "Acceptance stops"): the lines every user of this generator must
	// surface. Non-empty for both algorithms: neither is proven to be the clean RotWK 2.01 one.
	std::vector<std::string> unverified() const;

	// GetGameLogicRandomSeed (RW 0x6D3204).
	std::uint32_t baseSeed() const { return m_baseSeed; }
	const Seed &seedArray() const { return m_seed; }

	// randomValue(): one draw of the raw 32-bit generator.
	std::uint32_t randomValue();

	// GetGameLogicRandomValue (RW 0x6D328E). `file` / `line` are the ZH __FILE__/__LINE__ arguments;
	// they only reach the debug log, which is not ported, but they are recorded in the call log
	// when it is enabled so call order and call sites can be asserted.
	int getValue(int lo, int hi, const char *file, int line);
	// GetGameLogicRandomValueReal (RW 0x6D332C).
	float getValueReal(float lo, float hi, const char *file, int line);

	// Optional call log: every getValue/getValueReal call in order (tests assert retail call order).
	struct Call
	{
		bool real;
		int lo, hi;        // integer call bounds
		float rlo, rhi;    // real call bounds
		int result;        // integer result
		float rresult;     // real result
		std::string file;
		int line;
		bool drewNumber;   // false when the call returned `hi` without drawing (delta 0 / <= 0)
	};
	void enableCallLog(bool on) { m_logCalls = on; }
	const std::vector<Call> &callLog() const { return m_calls; }
	void clearCallLog() { m_calls.clear(); }

private:
	RandomAlgorithm m_algorithm;
	Seed m_seed;
	std::uint32_t m_baseSeed = 0;
	bool m_logCalls = false;
	std::vector<Call> m_calls;
};

// ---- ZH-named free functions over one process-wide generator --------------------------------
// There is no implicit generator: until InitGameLogicRandomGlobal is called they throw
// std::logic_error (PLAN rule 10).
void InitGameLogicRandomGlobal(RandomAlgorithm algorithm, std::uint32_t seed, std::int32_t frameOverride);
GameLogicRandom &TheGameLogicRandom();
int GetGameLogicRandomValue(int lo, int hi, const char *file, int line);
float GetGameLogicRandomValueReal(float lo, float hi, const char *file, int line);
#define GameLogicRandomValue(lo, hi) GetGameLogicRandomValue(lo, hi, __FILE__, __LINE__)
#define GameLogicRandomValueReal(lo, hi) GetGameLogicRandomValueReal(lo, hi, __FILE__, __LINE__)
