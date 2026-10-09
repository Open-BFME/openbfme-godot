// OpenBFME. GPL-3.0.
//
// Prefetch (lane PERF-3): a hint that memory will be read soon, for the client's loops over every drawable, whose objects, entry arrays and views are separate
// heap blocks (a battle's render frame was stalled on their cache misses: the main thread ran under one instruction per cycle). NOT A PORT: a hint changes
// no value and no order of anything (it may sit in the audited client files of the simulation manifest: it is no arithmetic).

#pragma once

#if defined(_MSC_VER) && !defined(__clang__)
#include <xmmintrin.h>
#define OPENBFME_PREFETCH(p) _mm_prefetch(reinterpret_cast<const char *>(p), _MM_HINT_T0)
#else
#define OPENBFME_PREFETCH(p) __builtin_prefetch(static_cast<const void *>(p))
#endif
