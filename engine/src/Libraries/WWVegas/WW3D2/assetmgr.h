// OpenBFME: faithful rebuild of The Battle for Middle-earth II: Rise of the Witch-king 2.01.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// The file-walking part of ZH WW3DAssetManager::Load_3D_Assets (assetmgr.cpp): split one .w3d
// file into its top-level prototypes. ZH hands each chunk to a registered PrototypeLoader and
// keeps the prototypes in hash tables; here the results are returned per file.
//
// Asset naming (BFME retail layout, observed in W3D.big / Textures*.big):
//   model or hierarchy "GUMAArms_SKN"   -> "art\w3d\gu\gumaarms_skn.w3d"
//   texture "GUManAtArms.tga"           -> "art\compiledtextures\gu\gumanatarms.dds"
// The two-letter folder is the first two characters of the name.

#pragma once

#include "Libraries/WWVegas/WW3D2/hcanim.h"
#include "Libraries/WWVegas/WW3D2/hlod.h"
#include "Libraries/WWVegas/WW3D2/hrawanim.h"
#include "Libraries/WWVegas/WW3D2/htree.h"
#include "Libraries/WWVegas/WW3D2/meshmdl.h"
#include "Libraries/WWVegas/WW3D2/part_ldr.h"
#include "Libraries/WWVegas/WW3D2/shdmesh.h"

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class ArchiveFileSystem;

struct W3DFileContents
{
	std::vector<MeshModelClass> Meshes;
	std::vector<HTreeClass> HTrees;
	std::vector<HLodDefClass> HLods;
	std::vector<W3dBoxStruct> Boxes;
	// Animations are decoded without their hierarchy (pivot count taken from the highest pivot a channel names);
	// the caller that knows the hierarchy checks it. Named "HIERARCHY.ANIM" (Get_Name).
	std::vector<HRawAnimClass> RawAnims;
	std::vector<HCompressedAnimClass> CompressedAnims;
	std::vector<ParticleEmitterDefClass> Emitters;
	std::vector<ShdMeshDefClass> ShdMeshes; // structure only (see shdmesh.h)
	std::vector<std::uint32_t> OtherChunks; // top-level chunks with no loader (none in the retail corpus); listed, not hidden

	const MeshModelClass *Find_Mesh(const std::string &name) const; // case-insensitive
	const HLodDefClass *Find_HLod(const std::string &name) const;
	const HTreeClass *Find_HTree(const std::string &name) const;
	const W3dBoxStruct *Find_Box(const std::string &name) const;
	const HAnimClass *Find_Animation(const std::string &fullName) const; // "HIERARCHY.ANIM", case-insensitive
};

bool Load_W3D_File(const std::uint8_t *data, size_t size, W3DFileContents &out, std::string *error);

// Virtual paths per the naming rules above.
std::string W3D_Asset_Path(const std::string &assetName);          // art\w3d\xx\name.w3d
std::string W3D_Compiled_Texture_Path(const std::string &textureName); // art\compiledtextures\xx\stem.dds

// ---------------------------------------------------------------------------------------------------------------------
// Prototype registry. Retail registers every top-level W3D chunk of the asset catalog by name (Load_Asset_Catalog 0x00938150)
// and creates render objects from the registry by lower-cased name (Create_Render_Obj, retail 0x008FF290). The registry is
// reached here lazily by the file the prototype's own loader would open (AssetCatalog::Source_Stem: mesh = name up to the
// first '.', HLOD = whole name, hierarchy = name without "h*", animation = text after the first '.'), which is exactly where
// the catalog's canonical records live, so no archive-wide scan is needed to resolve a name. Neither retail asset.dat is read
// (spec 1.1: the 2.01 RotWK one is the 2.02 patch's).
// ---------------------------------------------------------------------------------------------------------------------

// Where W3D files come from.
class W3DFileSource
{
public:
	virtual ~W3DFileSource() = default;
	virtual bool Exists(const std::string &path) const = 0;
	virtual bool Read(const std::string &path, std::vector<std::uint8_t> &out, std::string *error) = 0;
};

// The mounted retail archives (or any ArchiveFileSystem).
class ArchiveW3DFileSource : public W3DFileSource
{
public:
	explicit ArchiveW3DFileSource(ArchiveFileSystem &fs) : Fs(fs) {}
	bool Exists(const std::string &path) const override;
	bool Read(const std::string &path, std::vector<std::uint8_t> &out, std::string *error) override;

private:
	ArchiveFileSystem &Fs;
};

// One drawable member of an HLOD prototype (ZH HLodClass sub object). Boxes are collision / picking volumes, not drawn;
// emitters are particle systems (spec 3.9); both are carried so nothing is dropped silently.
struct RenderSubObject
{
	enum Kind { SUB_MESH, SUB_BOX, SUB_EMITTER, SUB_UNRESOLVED };
	std::string Name;       // as the HLOD names it
	int BoneIndex = 0;
	bool Aggregate = false; // from the aggregate array (attached after the LOD models)
	Kind Type = SUB_UNRESOLVED;
	const MeshModelClass *Mesh = nullptr;
	const W3dBoxStruct *Box = nullptr;
	const ParticleEmitterDefClass *Emitter = nullptr;
	std::string Reason;     // why a sub object is unresolved (ZH skips a null render object silently; here it is reported)
};

// Create_Render_Obj's result: a bare mesh (one default pivot) or an HLOD with its hierarchy and sub objects.
struct RenderObjPrototype
{
	enum ProtoType { PROTO_MESH, PROTO_HLOD };
	std::string Name;
	ProtoType Type = PROTO_MESH;
	const HLodDefClass *HLod = nullptr;       // PROTO_HLOD
	std::string HierarchyName;
	const HTreeClass *Tree = nullptr;         // the hierarchy, or DefaultTree
	bool HierarchyMissing = false;            // a hierarchy was named but not found: the default one-pivot tree is used (ZH animobj.cpp:107-120)
	HTreeClass DefaultTree;
	std::vector<RenderSubObject> SubObjects;  // highest LOD array, then the aggregates
	std::size_t ExtraLodArrays = 0;           // LOD arrays beyond the header's count (38 retail HLODs); not drawn
	std::size_t LodCount = 0;
};

// lane QA-1 (review r1): a client cache keyed by a prototype's address (GameClient/DrawablePick) must not outlive the prototype: a later manager can allocate a
// new prototype at the same address. The observer is called with each prototype a manager destroys, just before it goes (one observer, set once; client side only).
using W3DPrototypeEvictFn = void (*)(const RenderObjPrototype *);
void W3DSetPrototypeEvictObserver(W3DPrototypeEvictFn fn);

class WW3DAssetManager
{
public:
	explicit WW3DAssetManager(W3DFileSource &source) : Source(source) {}
	~WW3DAssetManager();
	WW3DAssetManager(const WW3DAssetManager &) = delete;
	WW3DAssetManager &operator=(const WW3DAssetManager &) = delete;

	// Parsed file contents, cached per path. nullptr + *error when the file is missing or malformed (the failure is
	// kept and returned again, never retried silently).
	const W3DFileContents *Load_File(const std::string &path, std::string *error);

	// ZH Get_HTree / Get_HAnim: nullptr + *error when the name is not registered. fullName is "HIERARCHY.ANIM".
	const HTreeClass *Get_HTree(const std::string &name, std::string *error);
	const HAnimClass *Get_HAnim(const std::string &fullName, std::string *error);

	// BFME2 animation name resolution (Open-BFME-1 Rva007657F0Animation.cpp, retail 0x007657F0): prefix[N].name[N], then
	// prefix.name, with no prefix the bare name; each candidate must exist. Returns false when none does.
	// bfmePrepVNV (what retail does to the name before the registry test) is not recovered and is not applied.
	bool Resolve_Animation(const std::string &prefix, const std::string &name, bool numbered, int number, std::string *resolvedName);

	// ZH Create_Render_Obj. The prototype is owned by the manager; nullptr + *error when the name is not registered.
	const RenderObjPrototype *Create_Render_Obj(const std::string &name, std::string *error);

	const std::vector<std::string> &Faults() const { return FaultList; } // files that failed to load, once each

private:
	struct Entry
	{
		std::unique_ptr<W3DFileContents> Contents;
		std::string Error;
	};
	W3DFileSource &Source;
	std::map<std::string, Entry> Files;
	std::map<std::string, std::unique_ptr<RenderObjPrototype>> Prototypes;
	std::vector<std::string> FaultList;
	// lane PERF-1: Get_HAnim's result per full name (found or not). The archives are fixed once mounted and Load_File keeps every file's outcome, so a
	// name's result never changes; a drawable's model condition change asks for its animation by name, which probed the file system every time.
	std::unordered_map<std::string, const HAnimClass *> AnimByName;

	const W3DFileContents *Try_Stem_File(const std::string &stem);
	void Resolve_Sub_Object(const std::string &name, int bone, bool aggregate, const HTreeClass &tree, RenderSubObject &out);
};
