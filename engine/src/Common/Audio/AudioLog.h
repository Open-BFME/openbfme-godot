// OpenBFME. GPL-3.0.
//
// AUDIO-3: the tag the audio request log (AudioManager::enableEventLog, AudioLogEntry::origin) gives the requests a caller makes, so a log line names the
// module, FX list or draw state that asked for a sound. A diagnostic only: no audio type here, so draw and logic code can tag without the audio headers.

#pragma once

#include <cstddef>
#include <string>
#include <utility>

namespace AudioLog
{
// AUDIO-3 r2: the process-wide gate (thread safe): true while at least one AudioManager has its request log on. With it off a Scope does no string work and
// the deferred logic calls capture no tag
bool enabled();
void addEnabled(int delta);
// the caller's tag for the requests made while a Scope lives (per thread, nested scopes join with '/'); the deferred logic calls keep the tag of the
// thread that queued them
const std::string &origin();
class Scope
{
public:
	explicit Scope(const char *tag);
	explicit Scope(const std::string &tag);
	// the tag is built by `make` only when the log is on (tags assembled from strings at a call site)
	template <class F, class = decltype(std::string(std::declval<F &>()()))>
	explicit Scope(F &&make) : m_previousSize(origin().size()), m_active(enabled())
	{
		if (m_active)
		{
			push(make());
		}
	}
	~Scope();
	Scope(const Scope &) = delete;
	Scope &operator=(const Scope &) = delete;

private:
	void push(const std::string &tag);
	size_t m_previousSize;
	bool m_active;
};
} // namespace AudioLog
