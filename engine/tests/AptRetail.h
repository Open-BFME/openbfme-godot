// OpenBFME unit tests. GPL-3.0.
//
// Apt corpus tests share the process-wide pure 2.01 mount from RetailTestMount.cpp (retailtest::pureMount):
// one mount keeps the 213 archives open once, so suites that run together stay under the CRT's FILE* limit.
// The macro prints SKIP and returns when ROTWK_INSTALL / BFME2_INSTALL are unset; a set install that fails
// to mount is a test failure, never a skip.

#pragma once

#include "RetailTestMount.h"

#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"

#include <string>

struct AptRetail
{
	Win32BIGFileSystem &fs;
};

#define APT_RETAIL_STR2(x) #x
#define APT_RETAIL_STR(x) APT_RETAIL_STR2(x)

// Use at the top of a retail-gated test; `mountVar` is an AptRetail.
#define OPENBFME_REQUIRE_RETAIL(mountVar)                                                                  \
	retailtest::Mount *mountVar##Shared = retailtest::pureMount();                                         \
	if (!mountVar##Shared)                                                                                 \
	{                                                                                                      \
		retailtest::printSkip("an Apt corpus test (" __FILE__ ":" APT_RETAIL_STR(__LINE__) ")");          \
		return;                                                                                            \
	}                                                                                                      \
	REQUIRE_MESSAGE(mountVar##Shared->fs, mountVar##Shared->error);                                        \
	AptRetail mountVar{ *mountVar##Shared->fs }
