// OpenBFME. GPL-3.0.
//
// Asset packs (modding SDK piece 1, lane SDK-0): drag-and-drop replacements of retail models and textures, cosmetic only. OpenBFME feature,
// not a retail one (retail has no such layer; mods replace assets through -mod, Common/ModMount.h, and those replacements ARE data).
//
// A pack is a folder. Every file in it (any subfolder, any case) whose name is a retail asset's file name replaces that asset for drawing:
//  * textures: <stem>.dds / .tga / .png / .jpg replace the W3D texture <stem> whatever its retail extension or folder (the texture
//    lookup of Libraries/WWVegas/WW3D2/textureloader.cpp: art\compiledtextures\xx\, art\textures\);
//  * models: <name>.w3d replaces the W3D file <name>.w3d (meshes, HLODs, hierarchies and animations it holds), whatever its folder
//    (WW3DAssetManager's W3D_Asset_Path: art\w3d\xx\).
// Several packs: the LAST one given wins a name. One pack holding a name twice (two folders, or two texture formats of one stem) is an
// error naming both files. Every file is listed and sized at load; a file that changes afterwards is a read error.
//
// Why cosmetic only: the packs reach the RENDER side only (AssetPackW3DFileSource for the drawables' / instancer's asset managers and
// W3DMaterialFactory's textures). The simulation's asset manager (LiveGame's m_simAssetSource: launch bones RW 0x6756A1, PristinePose)
// reads the mounted ArchiveFileSystem, which packs never touch; so launch positions, the logic state and the profile identity are the same
// with or without packs, and a pack may be used online. (Picking uses the drawn model, so a pack can change what a click hits: input, sent
// as ordinary commands, never a desync.)
//
// Not covered yet (reported in AssetPacks::notCovered()): particle, terrain, UI (Apt) and decal textures, and model formats other than W3D.

#pragma once

#include "Libraries/WWVegas/WW3D2/assetmgr.h"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

struct AssetPackFile
{
	enum class Kind
	{
		Texture,
		Model
	};
	Kind kind = Kind::Texture;
	std::string pack;     // the pack folder's name
	std::string relative; // the file below the pack folder, '/' separators, on-disk case
	std::string diskPath;
	std::string extension; // lower case, without the dot ("dds", "w3d")
	std::uint64_t size = 0;
	long long mtime = 0;
};

class AssetPacks
{
public:
	// Adds one pack folder after the ones added before (a later pack wins a name). False + errors (nothing of the pack added) when the folder
	// cannot be read or holds a name twice.
	bool addPack(const std::string &folder, std::vector<std::string> &errors);

	bool empty() const { return m_textures.empty() && m_models.empty(); }
	// the replacement of a W3D texture name ("GUMaArms.tga", "gumaarms": the extension and the case do not matter), or nullptr
	const AssetPackFile *texture(const std::string &textureName) const;
	// the replacement of a W3D file, by any virtual path ending in its file name ("art\w3d\gu\gumaarms_skn.w3d"), or nullptr
	const AssetPackFile *model(const std::string &w3dPath) const;
	// reads a pack file, checking it is still the file listed at load (size and last-write time)
	static bool read(const AssetPackFile &file, std::vector<std::uint8_t> &out, std::string *error);

	const std::vector<std::string> &packs() const { return m_packs; }
	size_t textureCount() const { return m_textures.size(); }
	size_t modelCount() const { return m_models.size(); }
	// "<pack>: <file> replaces <earlier pack>'s" lines (a later pack winning a name) and the files no lookup can use (unknown extensions)
	const std::vector<std::string> &notes() const { return m_notes; }
	static const char *notCovered();

private:
	std::vector<std::string> m_packs;
	std::map<std::string, AssetPackFile> m_textures; // texture stem, lower case
	std::map<std::string, AssetPackFile> m_models;   // file name, lower case ("gumaarms_skn.w3d")
	std::vector<std::string> m_notes;
};

// The render side's W3D source with the packs in front (models only; textures go through W3DMaterialFactory::Set_Asset_Packs). Never give it
// to the simulation's asset manager (LiveGame's m_simAssetSource): see "Why cosmetic only" above.
class AssetPackW3DFileSource : public W3DFileSource
{
public:
	AssetPackW3DFileSource(std::unique_ptr<W3DFileSource> base, const AssetPacks &packs) : Base(std::move(base)), Packs(packs) {}
	bool Exists(const std::string &path) const override { return Packs.model(path) != nullptr || Base->Exists(path); }
	bool Read(const std::string &path, std::vector<std::uint8_t> &out, std::string *error) override
	{
		if (const AssetPackFile *f = Packs.model(path))
		{
			return AssetPacks::read(*f, out, error);
		}
		return Base->Read(path, out, error);
	}
	bool FlatAssetLayout() const override { return Base->FlatAssetLayout(); }

private:
	std::unique_ptr<W3DFileSource> Base;
	const AssetPacks &Packs;
};
