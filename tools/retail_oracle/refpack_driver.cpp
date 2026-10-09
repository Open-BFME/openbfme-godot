// OpenBFME. GPL-3.0.
// Test driver: runs the engine's REF_decode (engine/src/Libraries/Compression/EAC/refdecode.cpp) on RefPack
// streams. stdin: one stream per line as hex (the bytes after the 8-byte "EAR\0"+size envelope). stdout, per line:
//   ok <consumed source bytes> <decoded bytes as hex>      or      err <message>
// tools/retail_oracle/test_refpack_oracle.py compares it with the retail REF_decode (RotWK 0xAA17E0).
#include "Libraries/Compression/EAC/refdecode.h"

#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

int main()
{
	std::string line;
	while (std::getline(std::cin, line))
	{
		while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
		{
			line.pop_back();
		}
		std::vector<std::uint8_t> in;
		for (size_t i = 0; i + 1 < line.size(); i += 2)
		{
			in.push_back((std::uint8_t)std::stoi(line.substr(i, 2), nullptr, 16));
		}
		std::vector<std::uint8_t> out;
		size_t consumed = 0;
		std::string err;
		if (!REF_decode(in.data(), in.size(), out, &consumed, &err))
		{
			std::printf("err %s\n", err.c_str());
			continue;
		}
		std::string hex;
		hex.reserve(out.size() * 2);
		static const char *d = "0123456789abcdef";
		for (std::uint8_t b : out)
		{
			hex.push_back(d[b >> 4]);
			hex.push_back(d[b & 15]);
		}
		std::printf("ok %zu %s\n", consumed, hex.c_str());
	}
	return 0;
}
