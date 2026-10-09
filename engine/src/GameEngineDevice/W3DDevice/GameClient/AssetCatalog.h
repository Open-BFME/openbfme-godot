// OpenBFME: faithful rebuild of The Battle for Middle-earth II: Rise of the Witch-king 2.01.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// The BFME asset catalog (retail Load_Asset_Catalog 0x00938150, Open-BFME-1
// GameEngineDevice/Source/W3DDevice/GameClient/AssetCatalog.cpp), built by scanning the W3D files
// of the mounted archives instead of reading asset.dat.
//
// Retail reads a prebuilt asset.dat: per file a list of (name, type, offset, length) records, and
// registers a prototype for a record only if no prototype with that (lower-cased) name exists yet.
// Neither retail asset.dat can be trusted for RotWK 2.01 (RotWK's is from the 2.02 patch, BFME2's
// predates the files RotWK overrides), so the catalog is rebuilt from the files themselves. The
// records are the same: the type, the name key and the (offset, length) of the top-level chunk in
// the file, offset being the chunk header position and length the chunk size plus its 8 byte header.
// Keys (spec w3d-and-draw.md section 1.2; ZH hcanim.cpp:260-262 for animations):
//   MESH   container.mesh, or mesh with no container         W3dMeshHeader3Struct names
//   HLOD   name                                              W3dHLodHeaderStruct::Name
//   HIER   h*name                                            W3dHierarchyStruct::Name
//   ANIM   a*hierarchy.name                                  raw or compressed animation header
//   BOX    name                                              W3dBoxStruct::Name
//   PART   name                                              W3dEmitterHeaderStruct::Name
// All lower-cased. Header names are 16 characters; a longer source name is truncated and the key
// keeps the truncation: it decides which file the loader opens (spec section 1.2), do not "fix" it.
//
// Which record wins a key (the PLAN's "no invented precedence"): a record is registered only when it lives in the
// file retail's prototype loader would open for that name, because the catalog entry's (offset, length) is read
// from that file and no other:
//   mesh        name up to the first '.', plus ".w3d"; the whole name plus ".w3d" without a '.' (Rva00970EC0Proto_Load.cpp)
//   box         name up to the first '.', plus ".w3d"; WITHOUT a '.' the literal name, no ".w3d" (Rva00972480Proto_LoadBox.cpp:62-68)
//   HLOD        the whole name, plus ".w3d"              (Rva00970880Proto_Load_HLOD.cpp)
//   hierarchy   the name without its "h*", plus ".w3d"   (HierarchyPrototypeLoad.cpp:118)
//   animation   the text after the first '.', plus ".w3d" (Load_Animation_W3D.cpp:148-157)
//   emitter     the whole name (no donor loader; the four retail emitters agree)
// Every one of the 39,874 prototype records of retail BFME2 asset.dat satisfies this. In the 1.06 archives 23 keys
// are defined by more than one file (PTGRASS03 in ptgrass03.w3d and in cine_bones02/03/04, gbmtnewfinal, shell04brddur,
// shellmap01): the rule picks the oracle's file for the 18 that have a canonical file, and the 5 without one are in
// no catalog record at all. Such non-canonical records are scanned and kept in Files() with Canonical == false, never
// registered. The tool that built asset.dat is in no binary or source we have, so its tie-break between two
// canonical files with the same name is unknown: Find() takes the file at the 2-letter path of spec section 1.4,
// else the first by (leaf, path), and Canonical_Ties() counts the cases (acceptance stop if it is ever non-zero).
// Retail's own load rule is first registration wins (AssetCatalog.cpp:71, Render_Obj_Exists at BFME2 0x0061F0D0).
//
// Fail closed per file (spec 2.3): a file whose chunk tree does not walk cleanly registers nothing and is listed in
// Faults(); its records are not kept either.

#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

class ArchiveFileSystem;

// asset.dat record types, as the file stores them (big-endian character order).
enum AssetCatalogType : std::uint32_t
{
	ASSET_TYPE_TEX = 0x00544558,  // 'TEX'  (textures: not in W3D files)
	ASSET_TYPE_BOX = 0x00424F58,  // 'BOX'
	ASSET_TYPE_AGGR = 0x41474752, // 'AGGR'
	ASSET_TYPE_ANIM = 0x414E494D, // 'ANIM'
	ASSET_TYPE_HIER = 0x48494552, // 'HIER'
	ASSET_TYPE_PART = 0x50415254, // 'PART'
	ASSET_TYPE_MESH = 0x4D455348, // 'MESH'
	ASSET_TYPE_HLOD = 0x484C4F44, // 'HLOD'
};

const char *Asset_Type_Name(std::uint32_t type); // "MESH", ... or "?" for an unknown value

struct AssetCatalogRecord
{
	std::string Key;      // lower-cased registry key
	bool Canonical = false; // lives in the file retail's loader would open for this name (see above); only these register
	std::uint32_t Type = 0;
	std::string File;     // lower-cased leaf file name ("gumaarms_skn.w3d")
	std::string Path;     // virtual path of the file ("art\w3d\gu\gumaarms_skn.w3d")
	std::uint32_t Offset = 0; // of the chunk header inside the file
	std::uint32_t Length = 0; // chunk size + 8
};

struct AssetCatalogFileFault
{
	std::string Path;
	std::string Reason;
};

class AssetCatalog
{
public:
	// The key helpers, for callers that look prototypes up.
	static std::string Mesh_Key(const std::string &container, const std::string &mesh);
	static std::string Hierarchy_Key(const std::string &name);
	static std::string Animation_Key(const std::string &hierarchyName, const std::string &name);

	// The lower-cased file stem (no directory, no ".w3d") retail's loader opens for a registered name; empty when
	// the loader would open nothing (an animation key without a '.').
	static std::string Source_Stem(const std::string &key, std::uint32_t type);

	// Records of one W3D file, in file order. Top-level chunks that are not prototypes (SHDMESH, and
	// anything else) are not records; their ids are appended to *skippedIds if given. Returns false and
	// sets *error when a chunk is malformed or a prototype chunk has no readable header.
	static bool Scan_W3D_File(const std::string &virtualPath, const std::uint8_t *data, size_t size,
		std::vector<AssetCatalogRecord> &out, std::vector<std::uint32_t> *skippedIds, std::string *error);

	// Adds one file's records. The file's records are kept in Files() whether or not their keys were
	// already registered; the registry keeps the first.
	bool Add_W3D_File(const std::string &virtualPath, const std::uint8_t *data, size_t size, std::string *error);

	// Scans every *.w3d in the mounted file system, sorted by (lower-cased leaf name, path). A file
	// that fails to scan is reported in Faults() and skipped, not fatal. Returns false only if the file
	// system cannot be read at all.
	bool Scan_File_System(ArchiveFileSystem &fileSystem, std::string *error);

	const AssetCatalogRecord *Find(const std::string &key) const; // lower-cased key; first registration
	const std::vector<std::vector<AssetCatalogRecord>> &Files() const { return FileRecords; }
	const std::vector<std::string> &File_Names() const { return FileNames; }
	const std::vector<std::uint64_t> &File_Sizes() const { return FileSizes; } // parallel to File_Names()
	const std::vector<AssetCatalogFileFault> &Faults() const { return FaultList; }
	const std::map<std::uint32_t, std::uint64_t> &Skipped_Chunk_Counts() const { return SkippedCounts; }

	size_t Registered_Count() const { return Registry.size(); }
	size_t Record_Count() const { return RecordCount; }
	size_t Uncatalogued_Count() const { return UncataloguedCount; } // scanned records not in their loader's file; never registered
	size_t Canonical_Ties() const { return CanonicalTieCount; }    // canonical records whose key another canonical file also holds
	size_t Shadowed_Count() const { return RecordCount - UncataloguedCount - Registry.size(); } // canonical, key taken by an earlier file

private:
	std::map<std::string, AssetCatalogRecord> Registry;
	std::vector<std::vector<AssetCatalogRecord>> FileRecords;
	std::vector<std::string> FileNames; // parallel to FileRecords: lower-cased virtual path
	std::vector<std::uint64_t> FileSizes;
	std::vector<AssetCatalogFileFault> FaultList;
	std::map<std::uint32_t, std::uint64_t> SkippedCounts;
	size_t RecordCount = 0;
	size_t UncataloguedCount = 0;
	size_t CanonicalTieCount = 0;
};
