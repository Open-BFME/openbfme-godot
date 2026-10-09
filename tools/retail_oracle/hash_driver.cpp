// OpenBFME. GPL-3.0.
// Test driver: runs the engine's hashes on each stdin line (bytes up to the newline; a CR before it is
// not part of the name) and prints two columns: the 32-bit INIMacroTable::hash
// (engine/src/Common/INI/INIMacro.cpp) and the 16-bit APT property hash AptPropertyMap::hash16
// (engine/src/Libraries/Source/Apt/AptObject.cpp, BFME2 0xAD3800 / RW 0xAE79A0), both in hex.
// tools/retail_oracle/test_retail_oracle.py compares retail's hashes with them.
#include "Common/INI.h"
#include "Libraries/Source/Apt/AptObject.h"

#include <cstdio>
#include <cstring>

int main()
{
	static char line[1 << 16];
	while (std::fgets(line, sizeof(line), stdin))
	{
		size_t n = std::strlen(line);
		while (n && (line[n - 1] == '\n' || line[n - 1] == '\r'))
			line[--n] = 0;
		std::printf("%08x %04x\n", INIMacroTable::hash(line), (unsigned)AptPropertyMap::hash16(line));
	}
	return 0;
}
