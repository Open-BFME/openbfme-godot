// OpenBFME. GPL-3.0.
// See GameClient/AssetPacks.h (lane SDK-0): cosmetic drag-and-drop asset packs.

#include "GameClient/AssetPacks.h"

#include "Common/AsciiString.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <system_error>

namespace fs = std::filesystem;

namespace
{

bool isTextureExtension(const std::string &e)
{
	return e == "dds" || e == "tga" || e == "png" || e == "jpg";
}

// the last path component, without the folders ('\' or '/')
std::string fileNameOf(const std::string &path)
{
	const size_t slash = path.find_last_of("\\/");
	return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string stemOf(const std::string &fileName)
{
	const size_t dot = fileName.rfind('.');
	return dot == std::string::npos ? fileName : fileName.substr(0, dot);
}

} // namespace

const char *AssetPacks::notCovered()
{
	return "asset packs replace W3D model files and the textures of W3D model surfaces; particle, terrain, UI (Apt) and decal textures and "
		   "non-W3D model formats are not replaced yet (lane SDK-0)";
}

bool AssetPacks::addPack(const std::string &folder, std::vector<std::string> &errors)
{
	const size_t errorsBefore = errors.size();
	std::error_code ec;
	const fs::path root = fs::u8path(folder);
	if (!fs::is_directory(root, ec))
	{
		errors.push_back("asset pack folder does not exist: '" + folder + "'");
		return false;
	}
	std::string name = root.filename().u8string();
	if (name.empty())
	{
		name = root.parent_path().filename().u8string();
	}
	std::map<std::string, AssetPackFile> textures, models;
	std::vector<std::string> notes;
	std::vector<fs::path> files;
	for (fs::recursive_directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec))
	{
		std::error_code typeEc;
		if (it->is_symlink(typeEc))
		{
			errors.push_back("asset pack " + name + ": symbolic links are not supported: " + it->path().lexically_relative(root).u8string());
			it.disable_recursion_pending();
			continue;
		}
		if (it->is_regular_file(typeEc))
		{
			files.push_back(it->path());
		}
	}
	if (ec)
	{
		errors.push_back("asset pack " + name + ": could not list " + folder + ": " + ec.message());
	}
	std::sort(files.begin(), files.end()); // a fixed order: the duplicate errors and the notes do not depend on the directory order
	for (const fs::path &p : files)
	{
		AssetPackFile f;
		f.pack = name;
		f.relative = p.lexically_relative(root).generic_u8string();
		f.diskPath = p.u8string();
		std::string ext = AsciiStringUtil::lowered(p.extension().u8string());
		f.extension = ext.empty() ? ext : ext.substr(1);
		std::error_code statEc;
		f.size = (std::uint64_t)fs::file_size(p, statEc);
		if (!statEc)
		{
			f.mtime = (long long)fs::last_write_time(p, statEc).time_since_epoch().count();
		}
		if (statEc)
		{
			errors.push_back("asset pack " + name + ": cannot read " + f.relative + ": " + statEc.message());
			continue;
		}
		const std::string lowerName = AsciiStringUtil::lowered(p.filename().u8string());
		std::map<std::string, AssetPackFile> *into = nullptr;
		std::string key;
		if (isTextureExtension(f.extension))
		{
			f.kind = AssetPackFile::Kind::Texture;
			into = &textures;
			key = stemOf(lowerName);
		}
		else if (f.extension == "w3d")
		{
			f.kind = AssetPackFile::Kind::Model;
			into = &models;
			key = lowerName;
		}
		else
		{
			if (lowerName != "openbfme-pack.json" && lowerName != "readme.txt" && lowerName != "readme.md")
			{
				notes.push_back(name + ": " + f.relative + " is not a model or texture (.w3d, .dds, .tga, .png, .jpg): not used");
			}
			continue;
		}
		auto existing = into->find(key);
		if (existing != into->end())
		{
			errors.push_back("asset pack " + name + ": " + existing->second.relative + " and " + f.relative + " replace the same asset '" + key +
				"': keep one");
			continue;
		}
		(*into)[key] = f;
	}
	if (errors.size() != errorsBefore)
	{
		return false;
	}
	auto merge = [&notes, &name](const std::map<std::string, AssetPackFile> &from, std::map<std::string, AssetPackFile> &into) {
		for (const auto &e : from)
		{
			auto old = into.find(e.first);
			if (old != into.end())
			{
				notes.push_back(name + ": " + e.second.relative + " replaces " + old->second.pack + "'s " + old->second.relative);
			}
			into[e.first] = e.second;
		}
	};
	merge(textures, m_textures);
	merge(models, m_models);
	m_packs.push_back(name);
	m_notes.insert(m_notes.end(), notes.begin(), notes.end());
	return true;
}

const AssetPackFile *AssetPacks::texture(const std::string &textureName) const
{
	if (m_textures.empty())
	{
		return nullptr;
	}
	auto it = m_textures.find(stemOf(AsciiStringUtil::lowered(fileNameOf(textureName))));
	return it == m_textures.end() ? nullptr : &it->second;
}

const AssetPackFile *AssetPacks::model(const std::string &w3dPath) const
{
	if (m_models.empty())
	{
		return nullptr;
	}
	auto it = m_models.find(AsciiStringUtil::lowered(fileNameOf(w3dPath)));
	return it == m_models.end() ? nullptr : &it->second;
}

bool AssetPacks::read(const AssetPackFile &file, std::vector<std::uint8_t> &out, std::string *error)
{
	std::error_code ec;
	const fs::path p = fs::u8path(file.diskPath);
	const std::uint64_t size = (std::uint64_t)fs::file_size(p, ec);
	const long long mtime = ec ? 0 : (long long)fs::last_write_time(p, ec).time_since_epoch().count();
	if (ec || size != file.size || mtime != file.mtime)
	{
		if (error)
		{
			*error = "asset pack file changed or vanished since the packs were loaded (restart to use it): " + file.pack + "/" + file.relative;
		}
		return false;
	}
	std::ifstream in(p, std::ios::binary);
	out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
	if (out.size() != file.size)
	{
		if (error)
		{
			*error = "cannot read asset pack file " + file.pack + "/" + file.relative;
		}
		return false;
	}
	return true;
}
