// OpenBFME. GPL-3.0. See ParticleTexture.h.

#include "GameClient/ParticleTexture.h"

#include <cctype>

ParticleTextureResolution ResolveParticleTexture(const std::string &name, const std::function<bool(const std::string &)> &exists)
{
	ParticleTextureResolution out;
	std::string lower = name;
	for (char &c : lower)
	{
		c = (char)std::tolower((unsigned char)c);
	}
	std::string stem = lower;
	const size_t dot = stem.find_last_of('.');
	if (dot != std::string::npos)
	{
		stem = stem.substr(0, dot);
	}
	if (stem.empty())
	{
		return out;
	}
	const bool textures = stem.compare(0, 4, "apt_") == 0 || stem.size() < 2;
	const std::string folder = textures ? "art\\textures\\" : "art\\compiledtextures\\" + stem.substr(0, 2) + "\\";
	struct Candidate
	{
		const char *ext;
		ParticleTextureResolution::Format kind;
	};
	for (const Candidate &c : { Candidate{ "dds", ParticleTextureResolution::DDS }, Candidate{ "tga", ParticleTextureResolution::TGA }, Candidate{ "jpg", ParticleTextureResolution::JPG } })
	{
		const std::string path = folder + stem + "." + c.ext;
		out.Tried.push_back(path);
		if (exists(path))
		{
			out.Found = true;
			out.Kind = c.kind;
			out.Path = path;
			break;
		}
	}
	if (out.Found && out.Kind == ParticleTextureResolution::JPG)
	{
		const std::string png = folder + stem + ".png";
		out.Tried.push_back(png);
		if (exists(png))
		{
			out.AuxPngPath = png;
		}
	}
	return out;
}
