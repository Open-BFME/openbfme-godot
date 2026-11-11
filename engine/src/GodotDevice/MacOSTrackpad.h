// OpenBFME. GPL-3.0. App-local macOS rotation events; camera presentation only.
#pragma once
#include <cstdint>
#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif
namespace MacOSTrackpad
{
#if defined(__APPLE__) && !TARGET_OS_IPHONE
void *install(std::uint64_t receiver, int window);
void uninstall(void *monitor);
void shutdown();
#else
inline void *install(std::uint64_t, int) { return nullptr; }
inline void uninstall(void *) {}
inline void shutdown() {}
#endif
}
