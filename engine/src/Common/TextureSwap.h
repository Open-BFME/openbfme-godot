// OpenBFME. GPL-3.0. Drawable texture replacement, including upgrade exclusions.
#pragma once

#include "Common/AsciiString.h"
#include <string>
#include <utility>
#include <vector>

struct TextureSwap
{
	TextureSwap() = default;
	TextureSwap(std::string from, std::string to, std::vector<std::string> excluded = {})
		: first(std::move(from)), second(std::move(to)), excludedSubObjects(std::move(excluded)) {}

	bool excludes(const std::string &mesh) const
	{
		const auto dot = mesh.find_last_of('.');
		const std::string leaf = dot == std::string::npos ? mesh : mesh.substr(dot + 1);
		for (const auto &name : excludedSubObjects)
		{
			if (AsciiStringUtil::compareNoCase(name, mesh) == 0 || AsciiStringUtil::compareNoCase(name, leaf) == 0) return true;
		}
		return false;
	}

	std::string first, second;
	std::vector<std::string> excludedSubObjects;
};
