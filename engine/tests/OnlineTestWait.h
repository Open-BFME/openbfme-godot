// OpenBFME unit tests. GPL-3.0.
#pragma once

#include <chrono>
#include <thread>

namespace onlinetest
{
// Real processes and sockets synchronize on their answers, not a number of ticks. The deadline detects a hang, not runner speed.
template <class Predicate> bool waitUntil(Predicate ready, std::chrono::milliseconds interval = std::chrono::milliseconds(1))
{
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
	for (;;)
	{
		if (ready())
		{
			return true;
		}
		if (std::chrono::steady_clock::now() >= deadline)
		{
			return false;
		}
		std::this_thread::sleep_for(interval);
	}
}
} // namespace onlinetest
