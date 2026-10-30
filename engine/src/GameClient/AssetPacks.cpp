// OpenBFME. GPL-3.0.
// See GameClient/AssetPacks.h (lane SDK-0): cosmetic drag-and-drop asset packs.

#include "GameClient/AssetPacks.h"

#include "Common/AsciiString.h"
#include "Common/Sha256.h"

#include <cstdio>
#include <memory>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <system_error>

namespace fs = std::filesystem;

namespace
{

bool isTextureExtension(const std::string &e)
{
	return e == "dds" || e == "tga" || e == "png" || e == "jpg";
}

// lane SDK-1: the text a pack may carry besides its assets (its readme, its licence, the credits): never read by the game
bool isPackTextFile(const std::string &lowerName, const std::string &extension)
{
	return extension == "txt" || extension == "md" || lowerName == "openbfme-pack.json" || lowerName == "license" || lowerName == "licence" ||
		lowerName == "readme";
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
	return "asset packs replace W3D model files and the textures of models, particles, decals, roads, rivers, water and the sky; the "
		   "terrain tile textures, UI (Apt) textures and non-W3D model formats are not replaced yet (lane SDK-1)";
}

std::vector<std::string> AssetPacks::compareSkeletons(const W3DFileContents &original, const W3DFileContents &replacement)
{
	std::vector<std::string> out;
	auto same = [](const std::string &a, const std::string &b) { return AsciiStringUtil::lowered(a) == AsciiStringUtil::lowered(b); };
	for (const HTreeClass &o : original.HTrees)
	{
		const HTreeClass *r = replacement.Find_HTree(o.Get_Name());
		if (!r)
		{
			out.push_back("it has no hierarchy " + o.Get_Name() + " (the original's skeleton)");
			continue;
		}
		if (r->Num_Pivots() != o.Num_Pivots())
		{
			out.push_back("its hierarchy " + o.Get_Name() + " has " + std::to_string(r->Num_Pivots()) + " bones, the original's " +
				std::to_string(o.Num_Pivots()));
			continue;
		}
		for (int i = 0; i < o.Num_Pivots(); ++i)
		{
			if (!same(r->Get_Pivot(i).Name, o.Get_Pivot(i).Name) || r->Get_Pivot(i).ParentIdx != o.Get_Pivot(i).ParentIdx)
			{
				out.push_back("its hierarchy " + o.Get_Name() + ": bone " + std::to_string(i) + " is " + r->Get_Pivot(i).Name + " (parent " +
					std::to_string(r->Get_Pivot(i).ParentIdx) + "), the original's " + o.Get_Pivot(i).Name + " (parent " +
					std::to_string(o.Get_Pivot(i).ParentIdx) + ")");
				break;
			}
		}
	}
	for (const HLodDefClass &o : original.HLods)
	{
		const HLodDefClass *r = replacement.Find_HLod(o.Name);
		if (!r)
		{
			out.push_back("it has no model " + o.Name + " (the original's HLOD)");
		}
		else if (!same(r->HierarchyName, o.HierarchyName))
		{
			out.push_back("its model " + o.Name + " uses the hierarchy " + r->HierarchyName + ", the original's " + o.HierarchyName);
		}
	}
	auto anims = [&](const auto &list) {
		for (const auto &a : list)
		{
			if (!replacement.Find_Animation(a.Get_Name()))
			{
				out.push_back("it has no animation " + a.Get_Name());
			}
		}
	};
	anims(original.RawAnims);
	anims(original.CompressedAnims);
	return out;
}

bool AssetPacks::checkModels(W3DFileSource &base, std::vector<std::string> &errors)
{
	const size_t errorsBefore = errors.size();
	std::vector<std::string> misfits;
	for (const auto &e : m_models)
	{
		const AssetPackFile &f = e.second;
		const std::string label = "asset pack " + f.pack + ": " + f.relative;
		std::vector<std::uint8_t> mine, theirs;
		std::string error;
		W3DFileContents replacement, original;
		if (!read(f, mine, &error))
		{
			errors.push_back(error);
			continue;
		}
		if (!Load_W3D_File(mine.data(), mine.size(), replacement, &error))
		{
			errors.push_back(label + " is not a W3D file the game can read: " + error);
			continue;
		}
		const std::string path = W3D_Asset_Path(stemOf(e.first), base.FlatAssetLayout());
		if (!base.Exists(path))
		{
			m_notes.push_back(f.pack + ": " + f.relative + " replaces no model of the game or the mod (" + path + "): not used");
			misfits.push_back(e.first); // review r1: not used means not drawn (a hierarchy or HLOD naming a missing file would reach it)
			continue;
		}
		if (!base.Read(path, theirs, &error) || !Load_W3D_File(theirs.data(), theirs.size(), original, &error))
		{
			errors.push_back(label + ": cannot read the model it replaces, " + path + ": " + error);
			continue;
		}
		const std::vector<std::string> why = compareSkeletons(original, replacement);
		if (!why.empty())
		{
			std::string all;
			for (const std::string &w : why)
			{
				all += (all.empty() ? "" : "; ") + w;
			}
			m_notes.push_back(f.pack + ": " + f.relative + " does not fit " + path + " (" + all + "): not used, the game's model is drawn (a "
				"replacement model keeps the original's skeleton, models and animations, or the game's animations and effects would not fit it)");
			m_misfits.push_back(Refusal{ f.pack, f.relative, "different bones or models than the game's (" + all + ")", true });
			misfits.push_back(e.first);
			continue;
		}
	}
	for (const std::string &k : misfits)
	{
		m_models.erase(k);
	}
	return errors.size() == errorsBefore;
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
			// lane SDK-1: cosmetic only, so a whitelist: anything but a model, a texture or the pack's own text is refused (game data belongs
			// in a mod, which the lobby and the replays fingerprint)
			if (!isPackTextFile(lowerName, f.extension))
			{
				errors.push_back("asset pack " + name + ": " + f.relative + " is not a model or texture (.w3d, .dds, .tga, .png, .jpg): an asset pack "
					"only changes how the game looks; game data (INI, scripts, maps, archives, sounds, programs) belongs in a mod (-mod)");
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
	if (m_suspended || m_textures.empty())
	{
		return nullptr;
	}
	const std::string key = stemOf(AsciiStringUtil::lowered(fileNameOf(textureName)));
	auto it = m_textures.find(key);
	return it == m_textures.end() ? nullptr : &it->second;
}

const AssetPackFile *AssetPacks::model(const std::string &w3dPath) const
{
	if (m_suspended || m_models.empty())
	{
		return nullptr;
	}
	const std::string key = AsciiStringUtil::lowered(fileNameOf(w3dPath));
	auto it = m_models.find(key);
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

// ---- lane SDK-1c: the packs' presentation lines (profile 3, agreed with Anti-cheat) --------------------------------------------------------

bool AssetPacks::identityLines(std::vector<std::string> &out, std::string &error) const
{
	out.clear();
	std::map<std::string, std::vector<std::string>> byPack; // pack name -> its used files' lines
	auto add = [&](const char *kind, const std::map<std::string, AssetPackFile> &files) {
		for (const auto &kv : files)
		{
			std::vector<std::uint8_t> bytes;
			if (!read(kv.second, bytes, &error))
				return false;
			const auto d = Sha256::hash(bytes.data(), bytes.size());
			byPack[kv.second.pack].push_back(std::string(kind) + " " + kv.first + " " + HexBytes(d.data(), d.size()));
		}
		return true;
	};
	if (!add("model", m_models) || !add("texture", m_textures))
		return false;
	for (const std::string &name : m_packs)
	{
		auto it = byPack.find(name);
		if (it == byPack.end())
			continue;
		std::vector<std::string> &lines = it->second;
		std::sort(lines.begin(), lines.end());
		Sha256 h;
		for (const std::string &l : lines)
		{
			h.update(l.data(), l.size());
			h.update("\n", 1);
		}
		const auto d = h.finish();
		std::string shown = name;
		for (char &c : shown)
			if ((unsigned char)c < 0x20 || c == 0x7F)
				c = '?';
		out.push_back("pack " + shown + " " + std::to_string(lines.size()) + " " + HexBytes(d.data(), d.size()));
	}
	return true;
}
