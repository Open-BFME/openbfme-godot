// OpenBFME. GPL-3.0.
//
// See RandomValue.h for the target / donor facts. Port of ZH RandomValue.cpp with the RotWK
// game.dat generator (RW 0x6D315D / 0x6D31D4) selectable; stop S-080.

#include "Common/RandomValue.h"

#include "Common/NumericState.h"

#include <stdexcept>

namespace
{
// ZH RandomValue.cpp: ADC(SUM, A, B, C): SUM = A + B + C; C = (SUM < A) || (SUM < B).
inline std::uint32_t adc(std::uint32_t a, std::uint32_t b, std::uint32_t &carry)
{
	const std::uint32_t sum = a + b + carry;
	carry = (sum < a || sum < b) ? 1u : 0u;
	return sum;
}

// ZH RandomValue.cpp randomValue(); BFME2 1.06 0x633EC3.
std::uint32_t randomValueCarryChain(GameLogicRandom::Seed &seed)
{
	std::uint32_t ax;
	std::uint32_t c = 0;

	ax = adc(seed[5], seed[4], c);
	seed[4] = ax;
	ax = adc(ax, seed[3], c);
	seed[3] = ax;
	ax = adc(ax, seed[2], c);
	seed[2] = ax;
	ax = adc(ax, seed[1], c);
	seed[1] = ax;
	ax = adc(ax, seed[0], c);
	seed[0] = ax;

	// Increment the seed array, bubbling up the carries.
	if (!++seed[5])
	{
		if (!++seed[4])
		{
			if (!++seed[3])
			{
				if (!++seed[2])
				{
					if (!++seed[1])
					{
						++seed[0];
						++ax;
					}
				}
			}
		}
	}
	return ax;
}

// RotWK game.dat 0x6D315D:
//   mov eax,[ecx]; xor edx,edx; mov ebx,0x8088405; mul ebx; add eax,1; mov [ecx],eax; xor eax,edx
std::uint32_t randomValueLcg(GameLogicRandom::Seed &seed)
{
	const std::uint64_t product = (std::uint64_t)seed[0] * 0x08088405ull;
	const std::uint32_t low = (std::uint32_t)product + 1u;
	const std::uint32_t high = (std::uint32_t)(product >> 32);
	seed[0] = low;
	return low ^ high;
}

// ZH RandomValue.cpp seedRandom(). The word-by-word constants are the differences of the initial
// array: seed[k] = SEED + initial[k].
void seedRandomCarryChain(std::uint32_t seedValue, GameLogicRandom::Seed &seed)
{
	const GameLogicRandom::Seed init = GameLogicRandom::initialSeedArray();
	for (size_t i = 0; i < seed.size(); ++i)
	{
		seed[i] = seedValue + init[i];
	}
}

// RotWK game.dat 0x6D31D4: push ebx; push edx; mov ebx,0x7fffffed; mul ebx; seed[0..5] = eax.
void seedRandomLcg(std::uint32_t seedValue, GameLogicRandom::Seed &seed)
{
	const std::uint32_t low = (std::uint32_t)((std::uint64_t)seedValue * 0x7fffffedull);
	seed.fill(low);
}

// RW 0xBC3741..0xBC376F: 1.0f / (powf(2, 32) - 1.0f), the subtraction at PC24: exactly 2^-32.
const float kMultFactor = 1.0f / 4294967296.0f;
}

GameLogicRandom::Seed GameLogicRandom::initialSeedArray()
{
	return Seed{ 0xf22d0e56u, 0x883126e9u, 0xc624dd2fu, 0x0702c49cu, 0x9e353f7du, 0x6fdf3b64u };
}

GameLogicRandom::GameLogicRandom(RandomAlgorithm algorithm)
	: m_algorithm(algorithm)
	, m_seed(initialSeedArray())
{
	if (algorithm != RandomAlgorithm::ZH_CarryChain && algorithm != RandomAlgorithm::RotWK_GameDat_LCG)
	{
		throw std::logic_error("GameLogicRandom: unknown RandomAlgorithm (no default exists, stop S-080)");
	}
}

void GameLogicRandom::seedRandom(std::uint32_t seed)
{
	if (m_algorithm == RandomAlgorithm::ZH_CarryChain)
	{
		seedRandomCarryChain(seed, m_seed);
	}
	else
	{
		seedRandomLcg(seed, m_seed);
	}
}

std::uint32_t GameLogicRandom::initGameLogicRandom(std::uint32_t seed, std::int32_t frameOverride)
{
	// RW 0x6D3261: edx = TheGameLogic->frame; if it is -1 (or there is no TheGameLogic) use `seed`.
	const std::uint32_t used = (frameOverride != -1) ? (std::uint32_t)frameOverride : seed;
	seedRandom(used);
	m_baseSeed = used;
	return used;
}

std::vector<std::string> GameLogicRandom::unverified() const
{
	const char *which = m_algorithm == RandomAlgorithm::ZH_CarryChain ? "ZH_CarryChain (ZH, BFME2 1.06 0x633EC3)" : "RotWK_GameDat_LCG (RotWK game.dat 0x6D315D)";
	return { std::string("S-080: game-logic RNG algorithm of a clean RotWK 2.01 is unproven; using ") + which +
		". The RotWK game.dat on this machine (community modified, S-001) holds a seed[0]*0x08088405+1 generator where BFME2 1.06 and ZH have the six-word carry chain." };
}

std::uint32_t GameLogicRandom::randomValue()
{
	return m_algorithm == RandomAlgorithm::ZH_CarryChain ? randomValueCarryChain(m_seed) : randomValueLcg(m_seed);
}

int GameLogicRandom::getValue(int lo, int hi, const char *file, int line)
{
	// RW 0x6D328E / ZH GetGameLogicRandomValue.
	const std::uint32_t delta = (std::uint32_t)hi - (std::uint32_t)lo + 1u;
	int result;
	bool drew = true;
	if (delta == 0)
	{
		result = hi;
		drew = false;
	}
	else
	{
		// RW 0x6D32AD adds in 32 bits and wraps; do the add unsigned (signed overflow is undefined).
		result = (int)((randomValue() % delta) + (std::uint32_t)lo);
	}
	if (m_logCalls)
	{
		Call c;
		c.real = false;
		c.lo = lo;
		c.hi = hi;
		c.rlo = c.rhi = 0.0f;
		c.result = result;
		c.rresult = 0.0f;
		c.file = file ? file : "";
		c.line = line;
		c.drewNumber = drew;
		m_calls.push_back(c);
	}
	return result;
}

float GameLogicRandom::getValueReal(float lo, float hi, const char *file, int line)
{
	// RW 0x6D332C. delta is an SSE single subtraction; the x87 chain below runs at PC24.
	// Each x87 step is rounded to a 24-bit significand and (here) stored as float32; the retail
	// code keeps the intermediates in registers, which only differs when an intermediate leaves
	// the float32 exponent range (acceptance stop S-081 covers the numeric parity of the facade).
	const float delta = hi - lo;
	float result;
	bool drew = true;
	if (0.0f >= delta) // comiss xmm1(0), xmm0(delta); jae: ordered 0 >= delta
	{
		result = hi;
		drew = false;
	}
	else
	{
		const std::int32_t r = (std::int32_t)randomValue();
		double t = (double)r; // fild dword: exact
		if (r < 0)
		{
			t = (double)NumericState::pc24AddD(t, 4294967296.0); // fadd [0xBD8698] = 2^32f
		}
		t = (double)NumericState::pc24MulD(t, (double)kMultFactor); // fmul [0xDE4A6C]
		t = (double)NumericState::pc24MulD(t, (double)delta);       // fmul [delta]
		result = NumericState::pc24AddD(t, (double)lo);             // fadd [lo]; fstp dword
	}
	if (m_logCalls)
	{
		Call c;
		c.real = true;
		c.lo = c.hi = 0;
		c.rlo = lo;
		c.rhi = hi;
		c.result = 0;
		c.rresult = result;
		c.file = file ? file : "";
		c.line = line;
		c.drewNumber = drew;
		m_calls.push_back(c);
	}
	return result;
}

// ---- process-wide instance ---------------------------------------------------------------------
namespace
{
GameLogicRandom *&globalInstance()
{
	static GameLogicRandom *instance = nullptr;
	return instance;
}
}

void InitGameLogicRandomGlobal(RandomAlgorithm algorithm, std::uint32_t seed, std::int32_t frameOverride)
{
	GameLogicRandom *&g = globalInstance();
	delete g;
	g = new GameLogicRandom(algorithm);
	g->initGameLogicRandom(seed, frameOverride);
}

GameLogicRandom &TheGameLogicRandom()
{
	GameLogicRandom *g = globalInstance();
	if (!g)
	{
		throw std::logic_error("TheGameLogicRandom: InitGameLogicRandomGlobal was never called (the RNG algorithm has no default, stop S-080)");
	}
	return *g;
}

int GetGameLogicRandomValue(int lo, int hi, const char *file, int line)
{
	return TheGameLogicRandom().getValue(lo, hi, file, line);
}

float GetGameLogicRandomValueReal(float lo, float hi, const char *file, int line)
{
	return TheGameLogicRandom().getValueReal(lo, hi, file, line);
}
