// OpenBFME. GPL-3.0.
//
// PERF-4's prewarm wave cap, kept free of Godot so the unit tests can pin its environment contract:
// OPENBFME_PREWARM_WAVES_MAX unset or malformed keeps the default, 0 queues every remaining state, N > 0 caps at N.
#pragma once

#include <cstddef>
#include <cstdlib>

namespace prewarm
{
inline std::size_t waveLimit(const char *value, std::size_t remaining, std::size_t fallback = 120)
{
	if (!value)
	{
		return fallback;
	}
	char *end = nullptr;
	const long parsed = std::strtol(value, &end, 10);
	if (end == value || *end != '\0' || parsed < 0)
	{
		return fallback;
	}
	return parsed == 0 ? remaining : (std::size_t)parsed;
}
} // namespace prewarm
