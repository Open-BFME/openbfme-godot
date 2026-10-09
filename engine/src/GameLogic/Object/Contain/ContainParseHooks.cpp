// OpenBFME. GPL-3.0.
//
// See GameLogic/ContainParseHooks.h.

#include "GameLogic/ContainParseHooks.h"

ContainParseHooks &TheContainParseHooks()
{
	static thread_local ContainParseHooks hooks; // SMOOTH-1: per thread, like the stores of the world context
	return hooks;
}
