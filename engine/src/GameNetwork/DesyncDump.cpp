// OpenBFME. GPL-3.0.
// See GameNetwork/DesyncDump.h.

#include "GameNetwork/DesyncDump.h"

#include <cstdio>
#include <fstream>
#include <map>

std::string DesyncDump::fileName(const std::string &directory, UnsignedInt frame, const std::string &exeName, const std::string &playerName)
{
	// RW 0xBFD900 "DESYNC-%s-%s-%s.txt" with RW 0xBFD914 "Frame%d"
	std::string name = "DESYNC-Frame" + std::to_string(frame) + "-" + exeName + "-" + playerName + ".txt";
	for (char &c : name)
	{
		if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
		{
			c = '_';
		}
	}
	if (directory.empty())
	{
		return name;
	}
	const char last = directory.back();
	return directory + (last == '/' || last == '\\' ? "" : "/") + name;
}

bool DesyncDump::write(const std::string &path, const DesyncReport &report, int localSlot, const std::string &replayPath, std::string *error)
{
	std::ofstream o(path, std::ios::binary | std::ios::trunc); // LF lines on every OS, so a Windows peer's dump compares with a Linux peer's (lane WIN-1)
	if (!o)
	{
		if (error)
		{
			*error = "cannot write " + path;
		}
		return false;
	}
	o << "Frame #" << report.checkedOnFrame << "\n\n"; // RW 0xBFD804
	o << "local slot " << localSlot << "\n";
	o << report.text();
	// every object hash of every half (the report text shows the first 20 that differ)
	for (const auto &kv : report.halves)
	{
		o << "\n---------------------------------------------------------\nslot " << kv.first << " (frame " << kv.second.frame << ", hash ";
		char b[16];
		std::snprintf(b, sizeof(b), "0x%08X", kv.second.hash);
		o << b << ")\n";
		for (const GameLogic::StateHashSection &s : kv.second.sections)
		{
			std::snprintf(b, sizeof(b), "0x%08X", s.value);
			o << "section " << s.name << " " << b << "\n";
		}
		for (const GameLogic::ObjectStateHash &x : kv.second.objects)
		{
			std::snprintf(b, sizeof(b), "0x%08X", x.value);
			o << "object " << x.id << " " << x.templateName << " " << b << "\n";
		}
	}
	// RW 0xBFD780: the replay section (named and copied here, not embedded: S-1124)
	o << "\n--------------------------------------------------------\nREPLAY FILE\n---------------------------------------------------------\n";
	if (replayPath.empty())
	{
		o << "(this peer recorded no replay)\n";
	}
	else
	{
		const std::string copy = path.substr(0, path.size() >= 4 ? path.size() - 4 : path.size()) + ".replay";
		std::ifstream in(replayPath, std::ios::binary);
		std::ofstream out(copy, std::ios::binary | std::ios::trunc);
		if (in && out)
		{
			out << in.rdbuf();
			o << replayPath << " (copied to " << copy << ")\n";
		}
		else
		{
			o << replayPath << " (could not be copied)\n";
		}
	}
	o << "---------------------------------------------------------\n\n";
	if (!o)
	{
		if (error)
		{
			*error = "write failed: " + path;
		}
		return false;
	}
	return true;
}
