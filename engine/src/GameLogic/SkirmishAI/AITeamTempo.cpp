// OpenBFME. GPL-3.0.
// See GameLogic/SkirmishAI/AITeamTempo.h for the targets and what is not ported (stop S-7186).

#include "GameLogic/SkirmishAI/AITeamTempo.h"

#include "GameLogic/GameLogic.h"
#include "GameLogic/SimMath.h"

#include <cstdint>

namespace AITeamTempo
{
int baseSeconds(const GameLogicSettings &settings)
{
	// RW 0x8F7FFC: cvttss2si eax, [AIData + 8] -> [this + 0x20] (RW 0x8F8001). Truncation toward zero, not a round: retail's 0.8 is 0 seconds.
	return SimMath::cvttss2si(settings.teamSeconds);
}

int delayFrames(const GameLogicSettings &settings, int money)
{
	const int base = baseSeconds(settings);                        // RW 0x8F8219: mov eax, [esi + 0x20]
	int frames = base * kLogicFramesPerSecond;                     // RW 0x8F8223: imul eax, [0xD9F608] (5)
	const std::uint32_t cash = (std::uint32_t)money;               // RW 0x8F8236: mov edx, [Player + 0x94]
	if (cash < (std::uint32_t)settings.poorThreshold)                     // RW 0x8F823C cmp edx, [AIData + 0x10] / RW 0x8F823F jae (unsigned)
	{
		// RW 0x8F8241 cvtsi2ss / RW 0x8F8245 divss [AIData + 0x24] / RW 0x8F824A jmp; the truncation is RW 0x8F825A below
		frames = SimMath::cvttss2si(SimMath::divf32(SimMath::sseFromInt32(frames), settings.teamsPoorRate));
	}
	else if (cash > (std::uint32_t)settings.wealthyThreshold)             // RW 0x8F824C cmp edx, [AIData + 0xC] / RW 0x8F824F jbe (unsigned)
	{
		// RW 0x8F8251 cvtsi2ss / RW 0x8F8255 divss [AIData + 0x1C] -> the shared RW 0x8F825A
		frames = SimMath::cvttss2si(SimMath::divf32(SimMath::sseFromInt32(frames), settings.teamsWealthyRate));
	}
	// RW 0x8F825A cvttss2si eax, xmm0 / RW 0x8F825E mov [esi + 0x18], eax; between the thresholds the plain frames (RW 0x8F8261) stand
	return frames;
}

int startCost(const GameLogicSettings &settings, int unitCount)
{
	// RW 0x8F55C4 fild [ebp - 0x10] / RW 0x8F55CA fmul [AIData + 0x28] / RW 0x8F55CD _ftol2 (RW 0xA3CFA4: FISTP qword, truncation toward zero).
	// x87 keeps the product in extended precision; the binary64 product below is exact for every count under 2^29 (a float32 rate has 24 mantissa
	// bits), i.e. for every team the data can describe.
	return (int)SimMath::ftol2Low32((double)unitCount * (double)settings.teamResourcesToStart);
}

bool canStartTeam(const GameLogicSettings &settings, int unitCount, int money)
{
	// RW 0x8F55D2 mov ecx, [esi + 0x10] (the Player) / RW 0x8F55D5 cmp [ecx + 0x94], eax / RW 0x8F55DB jae (unsigned): below the cost the out
	// flag is set (RW 0x8F55E0) and RW 0x8F55E3 returns false, so the team is not started.
	return (std::uint32_t)money >= (std::uint32_t)startCost(settings, unitCount);
}
} // namespace AITeamTempo
