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
// with or without packs, and a pack is used online too (lane SDK-1c: online is permissive by the owner's choice; players may use different
// packs in one game). (Picking uses the drawn model, so a pack can change what a click hits: input, sent
// as ordinary commands, never a desync.)
//
// Lane SDK-1 adds: a whitelist (a pack holds models, textures and its own text only; anything else refuses the pack), the skeleton check
// (checkModels, run on the mounted files) and the textures of particles, decals, roads, rivers, water and the sky (GodotDevice/
// GodotAssetPackImage.h). Precedence: base game < --mod-install < -mod < asset packs (a pack draws over a mod's file of the same name too).
//
// Not covered yet (reported in AssetPacks::notCovered()): terrain tile, UI (Apt) textures, and model formats other than W3D.

#pragma once


#include "Libraries/WWVegas/WW3D2/assetmgr.h"

#include <cstdint>
#include <functional>
#include <set>
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

	bool empty() const { return m_suspended || (m_textures.empty() && m_models.empty()); }
	// lane SDK-1b (a): a switch that suspends the packs (every lookup answers nothing, so the game's own assets are drawn) while they stay
	// loaded; loaded() says whether there are any. Since SDK-1c every game uses them (game.gd switches them on for each game).
	void setSuspended(bool on) { m_suspended = on; }
	bool suspended() const { return m_suspended; }
	bool loaded() const { return !m_textures.empty() || !m_models.empty(); }
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

	// lane SDK-1: checks every replacement model against the file it replaces in `base` (the mounted game and mods, the simulation's view):
	// a replacement must keep the original's hierarchies (bone names, order and parents), its HLODs' hierarchy and its animations, since the
	// animations, the effects' bones and the logic's launch bones (which keep reading the original) all name them. A model that does not
	// fit is left out with a note naming why (the game's model is drawn: the rest of the pack still loads); a model replacing no file is a
	// note. False + errors when a pack model cannot be read or is not a W3D file.
	bool checkModels(W3DFileSource &base, std::vector<std::string> &errors);
	// the reasons `replacement` does not fit `original` (empty = it fits); the checks of checkModels
	static std::vector<std::string> compareSkeletons(const W3DFileContents &original, const W3DFileContents &replacement);
	// lane SDK-1c (owner, 10-27: online is permissive): packs draw in every game, LAN and online included, for whoever has them. Players may
	// use different packs in one game: the simulation never reads them, so they cannot desync. The SDK-1b online rules are gone; what stays
	// are the technical skips that protect the game itself: a model that does not fit the original's skeleton (misfits()) and a file that
	// cannot be read (an error naming it).
	struct Refusal
	{
		std::string pack, relative, why;
		bool model = false;
	};
	// the models left out because they do not fit the original (checkModels), for the launcher's card
	const std::vector<Refusal> &misfits() const { return m_misfits; }
	const std::map<std::string, AssetPackFile> &textures() const { return m_textures; }
	const std::map<std::string, AssetPackFile> &models() const { return m_models; }
	// lane SDK-1c (agreed with Anti-cheat for profile 3): one presentation line per pack, in mount order, for the profile text (the LAN START,
	// replays, the checker; never in the identity's digest, so players with other packs or none play together):
	//     pack <name> <files> <sha256>
	// <files>: how many of the pack's files the game uses (a later pack winning a name takes it; a model that does not fit is not used);
	// <sha256>: 64 lower-case hex of those files' lines, sorted, each "model <file name> <sha256 of its bytes>" or "texture <stem> <sha256 of
	// its bytes>" followed by '\n'. The name is the pack folder's, any character below 0x20 or 0x7F written as '?'; it is everything between
	// "pack " and the last two fields. A pack whose files the game uses none of has no line. Reads every used file; false + error (naming the
	// file) when one cannot be read. Call it after checkModels (the misfits are left out by then).
	bool identityLines(std::vector<std::string> &out, std::string &error) const;

private:
	std::vector<std::string> m_packs;
	std::map<std::string, AssetPackFile> m_textures; // texture stem, lower case
	std::map<std::string, AssetPackFile> m_models;   // file name, lower case ("gumaarms_skn.w3d")
	std::vector<std::string> m_notes;
	bool m_suspended = false;
	std::vector<Refusal> m_misfits;
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
