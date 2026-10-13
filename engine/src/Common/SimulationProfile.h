// OpenBFME. GPL-3.0.
//
// SimulationProfile (lane UBER-1, docs/UBER.md): which simulation a game runs.
//
// NOT A PORT. Retail RotWK has one simulation; this is OpenBFME's switch between
//   * Faithful (the default): the retail-faithful simulation every other lane ports; its results, pins and replays never depend on Uber work;
//   * Uber (opt-in, "Uber performance mode (not compatible with the original game)", owner 2026-10-11): the same game rules, but the simulation may
//     compute things differently from retail to spread the work over every core (and later the GPU). It is deterministic across Uber machines
//     whatever their thread count, so Uber players can play each other; it never plays with a Faithful peer or a Faithful / retail replay.
// The profile travels in the game setup (SkirmishGameInfo::simulationProfile: the LAN start, the replay's start message), in the profile identity
// (GameNetwork/ProfileIdentity.h: "profile uber", compared at every lobby join and start and by every replay) and in the state hash (GameLogic: an
// Uber game hashes the profile first; a Faithful game hashes exactly what it did before UBER-1). A mismatch is a refused game, never a desync.

#pragma once

#include <cstdint>

enum class SimulationProfile : std::int32_t
{
	Faithful = 0,
	Uber = 1,
};

inline bool simulationProfileValid(std::int32_t v)
{
	return v == (std::int32_t)SimulationProfile::Faithful || v == (std::int32_t)SimulationProfile::Uber;
}

// the identity / log name: "faithful" or "uber"
inline const char *simulationProfileName(SimulationProfile p)
{
	return p == SimulationProfile::Uber ? "uber" : "faithful";
}
