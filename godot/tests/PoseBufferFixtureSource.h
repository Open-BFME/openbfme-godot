// OpenBFME. GPL-3.0. Test-only synthetic file transport, injected into a scratch instancer before asset manager construction.
// Production setup and retail mounts are unchanged. Generated W3D files use the base source's BFME2 asset layout.
#pragma once

#include "Libraries/WWVegas/WW3D2/assetmgr.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>

class PoseBufferFixtureSource : public W3DFileSource
{
public:
	PoseBufferFixtureSource(std::unique_ptr<W3DFileSource> base, std::filesystem::path root)
		: Base(std::move(base)), Root(std::move(root)) {}

	bool Exists(const std::string &path) const override
	{
		const auto local = local_path(path);
		return (!local.empty() && std::filesystem::is_regular_file(local)) || Base->Exists(path);
	}

	bool Read(const std::string &path, std::vector<std::uint8_t> &out, std::string *error) override
	{
		const auto local = local_path(path);
		if (local.empty() || !std::filesystem::is_regular_file(local)) return Base->Read(path, out, error);
		std::ifstream file(local, std::ios::binary);
		if (!file)
		{
			if (error) *error = "cannot open synthetic fixture: " + path;
			return false;
		}
		out.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
		if (file.bad())
		{
			if (error) *error = "cannot read synthetic fixture: " + path;
			return false;
		}
		return true;
	}

	bool FlatAssetLayout() const override { return Base->FlatAssetLayout(); }

private:
	std::filesystem::path local_path(std::string path) const
	{
		std::transform(path.begin(), path.end(), path.begin(), [](unsigned char c) { return c == '\\' ? '/' : (char)std::tolower(c); });
		const std::filesystem::path relative(path);
		if (relative.is_absolute()) return {};
		for (const auto &part : relative) if (part == "..") return {};
		return Root / relative;
	}

	std::unique_ptr<W3DFileSource> Base;
	std::filesystem::path Root;
};

inline std::unique_ptr<W3DFileSource> pose_buffer_fixture_source(std::unique_ptr<W3DFileSource> base)
{
	const char *root = std::getenv("OPENBFME_W3D_FIXTURE_ROOT");
	if (!root || !*root || !std::filesystem::is_directory(root))
		throw std::runtime_error("OPENBFME_W3D_FIXTURE_ROOT must name the generated fixture directory");
	return std::make_unique<PoseBufferFixtureSource>(std::move(base), root);
}
