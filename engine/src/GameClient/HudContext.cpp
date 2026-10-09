// OpenBFME. GPL-3.0.
// See HudContext.h.

#include "GameClient/HudContext.h"

#include "Common/ArchiveFileSystem.h"

#include <cctype>
#include <cstdlib>
#include <sstream>

bool MouseSettings::load(ArchiveFileSystem &fs, MouseSettings &out, std::string *error)
{
	std::vector<std::uint8_t> bytes;
	if (!fs.readFile("data/ini/mouse.ini", bytes, error))
	{
		return false;
	}
	std::istringstream in(std::string(bytes.begin(), bytes.end()));
	std::string line;
	bool inMouse = false;
	bool haveDrag = false, have3d = false, haveMs = false;
	MouseSettings s;
	while (std::getline(in, line))
	{
		const size_t c = line.find(';');
		if (c != std::string::npos)
		{
			line.erase(c);
		}
		size_t b = line.find_first_not_of(" \t\r");
		if (b == std::string::npos)
		{
			continue;
		}
		line = line.substr(b);
		while (!line.empty() && std::isspace((unsigned char)line.back()))
		{
			line.pop_back();
		}
		if (!inMouse)
		{
			inMouse = line == "Mouse";
			continue;
		}
		if (line == "End")
		{
			break;
		}
		const size_t eq = line.find('=');
		if (eq == std::string::npos)
		{
			continue;
		}
		std::string key = line.substr(0, eq), val = line.substr(eq + 1);
		while (!key.empty() && std::isspace((unsigned char)key.back()))
		{
			key.pop_back();
		}
		char *end = nullptr;
		const long v = std::strtol(val.c_str(), &end, 10);
		const bool ok = end != val.c_str();
		if (key == "DragTolerance3D" || key == "DragToleranceMS" || key == "DragTolerance")
		{
			if (!ok)
			{
				if (error)
				{
					*error = "mouse.ini: " + key + " is not a number";
				}
				return false;
			}
			(key == "DragTolerance" ? s.dragTolerance : key == "DragTolerance3D" ? s.dragTolerance3D : s.dragToleranceMS) = (int)v;
			(key == "DragTolerance" ? haveDrag : key == "DragTolerance3D" ? have3d : haveMs) = true;
		}
	}
	if (!haveDrag || !have3d || !haveMs)
	{
		if (error)
		{
			*error = "mouse.ini: the Mouse block lacks DragTolerance, DragTolerance3D or DragToleranceMS";
		}
		return false;
	}
	out = s;
	return true;
}
