// OpenBFME. GPL-3.0.
#pragma once

#include "Common/Recorder.h"
#include "GameClient/CommandLogLoad.h"
#include "GameClient/LiveGame.h"

#include <chrono>
#include <string>

// No world clone is available. The caller recreates the replay's start before a backward seek.
// Unlike CommandLogLoad, the replay driver remains installed when the target is reached.
struct ReplaySeek
{
	struct Result
	{
		UnsignedInt reached = 0;
		bool ready = false;
		std::string error;
	};

	static Result step(LiveGame &game, ReplayPlayback &playback, const ReplayFile &log, UnsignedInt target, unsigned budgetMs)
	{
		game.drainFrames();
		Result result;
		result.reached = game.protocolFrame();
		if (!log.complete || !log.lost.empty())
		{
			result.error = "Cannot seek an incomplete replay";
			return result;
		}
		if (target < result.reached || target > log.finalFrame)
		{
			result.error = "Seek target outside this playback range; restart the replay before seeking backwards";
			return result;
		}
		if (playback.hasMismatch())
		{
			result.error = "Replay hash mismatch at frame " + std::to_string(playback.firstMismatch().frame);
			return result;
		}
		const auto start = std::chrono::steady_clock::now();
		const bool threaded = game.logicThread();
		if (threaded)
		{
			game.setLogicThread(false);
		}
		while (result.reached < target)
		{
			if (!log.hashes.count(result.reached + 1) && CommandLogLoad::hashRecordedFor(log, result.reached + 1)) // lane UBER-2C: an Uber frame off the cadence has none
			{
				result.error = "Replay has no recorded hash for frame " + std::to_string(result.reached + 1);
				break;
			}
			if (game.stepFrame() != 1)
			{
				result.error = "Replay did not advance at frame " + std::to_string(result.reached);
				break;
			}
			result.reached = game.protocolFrame();
			if (playback.hasMismatch())
			{
				result.error = "Replay hash mismatch at frame " + std::to_string(playback.firstMismatch().frame);
				break;
			}
			if (std::chrono::steady_clock::now() - start >= std::chrono::milliseconds(budgetMs))
			{
				break;
			}
		}
		if (threaded)
		{
			game.setLogicThread(true);
		}
		result.ready = result.error.empty() && result.reached == target;
		return result;
	}
};
