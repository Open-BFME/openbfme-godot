// OpenBFME. GPL-3.0.
// See AptHoverInventory.h (lane HOVER-1). Client-only: reads movie files, never the logic.

#include "GameClient/GUI/AptHoverInventory.h"

#include "Libraries/Source/Apt/AptLoad.h"

#include <map>
#include <set>
#include <tuple>

namespace
{
struct Walker
{
	AptLoader &loader;
	std::string movie;
	std::map<std::pair<std::string, bool>, std::string> found; // (path, from export) -> the frames of its first placement chain
	std::set<std::tuple<const AptFile *, std::uint32_t, std::string>> visited;
	std::string error;

	// the character `id` of `file`, through an import when its slot is one
	bool resolve(const AptFile &file, std::uint32_t id, std::shared_ptr<const AptFile> &outFile, std::uint32_t &outId)
	{
		for (const AptImport &imp : file.imports)
		{
			if (imp.characterId == id)
			{
				AptResolvedImport r;
				if (!loader.resolveImport(file, imp, r, &error))
				{
					return false;
				}
				outFile = r.movie;
				outId = r.characterId;
				return true;
			}
		}
		outId = id;
		return true;
	}

	void frames(const std::shared_ptr<const AptFile> &file, const std::vector<AptFrame> &list, const std::string &prefix, bool fromExport, int depth,
	            const std::string &chain)
	{
		for (size_t fi = 0; fi < list.size(); ++fi)
		{
			const AptFrame &f = list[fi];
			for (const AptFrameItem &item : f.items)
			{
				if (!item.place || !(item.place->flags & APT_PLACE_HASCHARACTER) || item.place->characterId < 0)
				{
					continue;
				}
				character(file, (std::uint32_t)item.place->characterId, prefix + "/" + item.place->name, fromExport, depth + 1,
				          chain + (chain.empty() ? "" : "/") + std::to_string(fi));
			}
		}
	}

	void character(std::shared_ptr<const AptFile> file, std::uint32_t id, const std::string &path, bool fromExport, int depth, const std::string &chain)
	{
		if (depth > 24 || !error.empty())
		{
			return;
		}
		std::shared_ptr<const AptFile> target = file;
		std::uint32_t targetId = id;
		if (!resolve(*file, id, target, targetId) || !target || targetId >= target->characters.size())
		{
			return;
		}
		if (!visited.insert(std::make_tuple(target.get(), targetId, path)).second)
		{
			return;
		}
		const AptCharacter &c = target->characters[targetId];
		if (c.type == APT_CHAR_BUTTON)
		{
			found.insert({ { path, fromExport }, chain }); // the first chain found is kept
		}
		else if (c.type == APT_CHAR_SPRITE)
		{
			frames(target, c.frames, path, fromExport, depth, chain);
		}
	}
};
} // namespace

bool AptHoverInventory::walk(AptLoader &loader, const std::string &movieName, std::vector<AptHoverArea> &out, std::string *error)
{
	out.clear();
	std::string loadError;
	std::shared_ptr<const AptFile> file = loader.loadMovie(movieName, &loadError);
	if (!file)
	{
		if (error)
		{
			*error = loadError;
		}
		return false;
	}
	Walker w{ loader, movieName, {}, {}, {} };
	w.frames(file, file->frames, movieName, false, 0, std::string());
	for (const AptExport &e : file->exports)
	{
		w.character(file, e.characterId, movieName + ":" + e.name, true, 0, std::string());
	}
	if (!w.error.empty())
	{
		if (error)
		{
			*error = w.error;
		}
		return false;
	}
	for (const auto &f : w.found)
	{
		out.push_back({ movieName, f.first.first, f.first.second, f.second });
	}
	return true;
}

std::string AptHoverInventory::leafName(const std::string &path)
{
	const size_t slash = path.rfind('/');
	return slash == std::string::npos ? path : path.substr(slash + 1);
}
