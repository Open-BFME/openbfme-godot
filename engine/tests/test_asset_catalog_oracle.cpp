// OpenBFME unit tests: the asset catalog scanner against retail BFME2 1.06 asset.dat. GPL-3.0.
//
// F:\BFME2\asset.dat (EA 1.06, FILETIMEs from 2008) is used ONLY as an oracle here (spec
// w3d-and-draw.md section 5.4 item 2); the engine never reads an asset.dat. RotWK's asset.dat is
// from the 2.02 patch and is not touched by anything in this repository.
//
// Runs only when BFME2_INSTALL (and for the second case ROTWK_INSTALL) is set; prints SKIP otherwise.

#include "doctest.h"
#include "RetailTestMount.h"

#include "GameEngineDevice/W3DDevice/GameClient/AssetCatalog.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>

namespace
{

struct OracleEntry
{
	std::string Name; // as stored (upper case)
	std::uint32_t Type = 0;
	std::int32_t Offset = 0;
	std::uint32_t Length = 0;
};

struct OracleFile
{
	std::string Name;
	std::vector<OracleEntry> Entries;
};

// Format: Open-BFME-1 AssetCatalog.cpp Load_Asset_Catalog (retail 0x00938150).
//   u32 magic 0x45414C41, u32 version 0x102, u32 fileCount, u32 groupCount
//   per file: u8 len, name[len], 8 bytes (FILETIME), u16 n, n x { u8 len, name[len], u32 type, i32 offset, u32 length }
//   per group: u8 len, name, u8 len, name, u16 n, n x { u8 len, name }
struct Reader
{
	const std::vector<unsigned char> &B;
	size_t P = 0;
	bool Ok = true;
	explicit Reader(const std::vector<unsigned char> &b) : B(b) {}
	bool need(size_t n)
	{
		if (P + n > B.size())
		{
			Ok = false;
		}
		return Ok;
	}
	std::uint32_t u8() { if (!need(1)) return 0; return B[P++]; }
	std::uint32_t u16() { if (!need(2)) return 0; std::uint32_t v = B[P] | (B[P + 1] << 8); P += 2; return v; }
	std::uint32_t u32() { if (!need(4)) return 0; std::uint32_t v; std::memcpy(&v, &B[P], 4); P += 4; return v; }
	std::string str(size_t n) { if (!need(n)) return std::string(); std::string s((const char *)&B[P], n); P += n; return s; }
	void skip(size_t n) { if (need(n)) P += n; }
};

bool parseAssetDat(const std::vector<unsigned char> &bytes, std::vector<OracleFile> &files, std::string *error)
{
	Reader r(bytes);
	if (r.u32() != 0x45414C41 || r.u32() != 0x102)
	{
		*error = "asset.dat magic/version mismatch";
		return false;
	}
	std::uint32_t fileCount = r.u32();
	std::uint32_t groupCount = r.u32();
	for (std::uint32_t i = 0; i < fileCount && r.Ok; ++i)
	{
		OracleFile f;
		f.Name = r.str(r.u8());
		r.skip(8);
		std::uint32_t n = r.u16();
		for (std::uint32_t k = 0; k < n && r.Ok; ++k)
		{
			OracleEntry e;
			e.Name = r.str(r.u8());
			e.Type = r.u32();
			e.Offset = (std::int32_t)r.u32();
			e.Length = r.u32();
			f.Entries.push_back(e);
		}
		files.push_back(std::move(f));
	}
	for (std::uint32_t i = 0; i < groupCount && r.Ok; ++i)
	{
		r.str(r.u8());
		r.str(r.u8());
		std::uint32_t n = r.u16();
		for (std::uint32_t k = 0; k < n && r.Ok; ++k)
		{
			r.str(r.u8());
		}
	}
	if (!r.Ok || r.P != bytes.size())
	{
		*error = "asset.dat is not consumed exactly (" + std::to_string(r.P) + " of " + std::to_string(bytes.size()) + " bytes)";
		return false;
	}
	return true;
}

bool isCatalogType(std::uint32_t t)
{
	return t == ASSET_TYPE_MESH || t == ASSET_TYPE_HLOD || t == ASSET_TYPE_HIER || t == ASSET_TYPE_ANIM || t == ASSET_TYPE_BOX || t == ASSET_TYPE_PART;
}

std::string lowerStr(std::string s)
{
	for (char &c : s)
	{
		if (c >= 'A' && c <= 'Z')
		{
			c = (char)(c - 'A' + 'a');
		}
	}
	return s;
}

struct Comparison
{
	std::uint64_t Considered = 0;       // oracle entries of the types a W3D scan can produce
	std::uint64_t Match = 0;            // file, type, key, offset and length all equal
	std::uint64_t SpecMatch = 0;        // the spec's catcheck.py definition: file at art/w3d/<first 2 letters>/<leaf>, chunk of the same type at the offset, same length (key not compared)
	std::uint64_t FileAbsent = 0;       // no .w3d with that leaf name in the mounted archives
	std::uint64_t OffsetOutOfRange = 0; // the file is shorter than the offset
	std::uint64_t NoChunkAtOffset = 0;  // no top-level chunk starts at the offset
	std::uint64_t TypeOrKeyDiffers = 0; // a prototype chunk starts there but with another type or key
	std::uint64_t LengthDiffers = 0;    // right chunk, different length
	std::uint64_t NotCatalogTypes = 0;  // TEX / FXSH entries (textures and shader files are not W3D chunks)
	std::uint64_t OracleDuplicates = 0;
	std::uint64_t FileFailedValidation = 0; // the file is in the archives but its chunk tree is malformed: skipped whole (spec 2.3)
	std::uint64_t OracleNotCanonical = 0;   // oracle entries whose key does not name their own file (evidence for the registration rule: must be 0)
	std::uint64_t WinnerSameFile = 0;       // Find(key) returns a record of the oracle's file
	std::uint64_t WinnerMissing = 0;        // Find(key) finds nothing (the file is absent, skipped, or no longer holds the key)
	std::uint64_t WinnerOtherFile = 0;      // Find(key) returns a record of ANOTHER file: wrong geometry, must be 0
	std::uint64_t WinnerExact = 0;          // winner is the oracle's file, offset and length
	std::uint64_t ScannedNotInOracle = 0; // scanned records no oracle entry names
	std::uint64_t OracleFiles = 0, ScannedFiles = 0;
	std::map<std::string, std::uint64_t> MismatchByArchive; // owning archive of the file, for non-matches
	std::vector<std::string> Samples;
	std::vector<std::string> KeySamples;
};

Comparison compare(const AssetCatalog &catalog, const std::vector<OracleFile> &oracle, ArchiveFileSystem &fs)
{
	Comparison out;
	out.OracleFiles = oracle.size();
	out.ScannedFiles = catalog.File_Names().size();
	std::map<std::string, std::vector<size_t>> byLeaf;
	for (size_t i = 0; i < catalog.File_Names().size(); ++i)
	{
		const std::string &p = catalog.File_Names()[i];
		byLeaf[p.substr(p.find_last_of('\\') + 1)].push_back(i);
	}
	std::set<std::pair<size_t, std::uint32_t>> claimed; // (file index, offset) the oracle accounts for
	std::set<std::string> oracleNames;
	for (const OracleFile &f : oracle)
	{
		std::string leaf = lowerStr(f.Name);
		auto it = byLeaf.find(leaf);
		for (const OracleEntry &e : f.Entries)
		{
			if (!isCatalogType(e.Type))
			{
				out.NotCatalogTypes++;
				continue;
			}
			out.Considered++;
			// evidence for the registration rule: the oracle names every prototype by the file retail's loader opens
			if (AssetCatalog::Source_Stem(lowerStr(e.Name), e.Type) != leaf.substr(0, leaf.size() - 4))
			{
				out.OracleNotCanonical++;
			}
			// the winner a lookup actually returns, not just a matching record somewhere in Files()
			if (const AssetCatalogRecord *w = catalog.Find(e.Name))
			{
				if (w->File == leaf)
				{
					out.WinnerSameFile++;
					if ((std::int32_t)w->Offset == e.Offset && w->Length == e.Length && w->Type == e.Type)
					{
						out.WinnerExact++;
					}
				}
				else
				{
					out.WinnerOtherFile++;
					if (out.Samples.size() < 12)
					{
						out.Samples.push_back("WINNER IS ANOTHER FILE: " + e.Name + " oracle " + f.Name + " lookup " + w->Path);
					}
				}
			}
			else
			{
				out.WinnerMissing++;
			}
			if (!oracleNames.insert(lowerStr(e.Name) + "/" + std::to_string(e.Type)).second)
			{
				out.OracleDuplicates++;
			}
			auto note = [&](const char *what, size_t fileIndex) {
				if (out.Samples.size() < 12)
				{
					out.Samples.push_back(std::string(what) + ": " + f.Name + " " + e.Name + " " + Asset_Type_Name(e.Type) + " @" +
						std::to_string(e.Offset) + "+" + std::to_string(e.Length));
				}
				if (fileIndex != (size_t)-1)
				{
					std::string archive = fs.getArchiveFilenameForFile(catalog.File_Names()[fileIndex]);
					out.MismatchByArchive[archive]++;
				}
			};
			if (it == byLeaf.end())
			{
				out.FileAbsent++;
				note("file absent", (size_t)-1);
				continue;
			}
			{
				bool faulted = false;
				for (size_t fi : it->second)
				{
					for (const AssetCatalogFileFault &fault : catalog.Faults())
					{
						faulted = faulted || fault.Path == catalog.File_Names()[fi];
					}
				}
				if (faulted)
				{
					out.FileFailedValidation++;
					note("file fails validation (skipped)", it->second.front());
					continue;
				}
			}
			const AssetCatalogRecord *hit = nullptr;
			size_t hitFile = (size_t)-1;
			bool inRange = false;
			const std::string specPath = "art\\w3d\\" + leaf.substr(0, 2) + "\\" + leaf;
			for (size_t fi : it->second)
			{
				for (const AssetCatalogRecord &rec : catalog.Files()[fi])
				{
					if ((std::int32_t)rec.Offset == e.Offset)
					{
						hit = &rec;
						hitFile = fi;
						if (catalog.File_Names()[fi] == specPath && rec.Type == e.Type && rec.Length == e.Length)
						{
							out.SpecMatch++;
						}
					}
				}
			}
			if (!hit)
			{
				for (size_t fi : it->second)
				{
					if ((std::uint64_t)e.Offset + 8 <= catalog.File_Sizes()[fi])
					{
						inRange = true;
					}
				}
				if (inRange)
				{
					out.NoChunkAtOffset++;
					note("no chunk at offset", it->second.front());
				}
				else
				{
					out.OffsetOutOfRange++;
					note("offset past the file", it->second.front());
				}
				continue;
			}
			claimed.insert({ hitFile, hit->Offset });
			if (hit->Type != e.Type || hit->Key != lowerStr(e.Name))
			{
				out.TypeOrKeyDiffers++;
				note("type or key differs", hitFile);
				if (out.KeySamples.size() < 8)
				{
					out.KeySamples.push_back(f.Name + ": oracle " + Asset_Type_Name(e.Type) + " " + e.Name + " @" + std::to_string(e.Offset) + "+" +
						std::to_string(e.Length) + " vs scanned " + Asset_Type_Name(hit->Type) + " " + hit->Key + " +" + std::to_string(hit->Length));
				}
			}
			else if (hit->Length != e.Length)
			{
				out.LengthDiffers++;
				note("length differs", hitFile);
			}
			else
			{
				out.Match++;
			}
		}
	}
	for (size_t i = 0; i < catalog.Files().size(); ++i)
	{
		for (const AssetCatalogRecord &rec : catalog.Files()[i])
		{
			if (!claimed.count({ i, rec.Offset }))
			{
				out.ScannedNotInOracle++;
			}
		}
	}
	return out;
}

void report(const char *label, const AssetCatalog &catalog, const Comparison &c, double seconds)
{
	std::printf("[catalog oracle] %s: scanned %zu files, %zu faults, %.1f s\n", label, catalog.File_Names().size(), catalog.Faults().size(), seconds);
	std::printf("[catalog oracle] %s: oracle has %llu files, %llu W3D-derivable entries (+%llu TEX/FXSH entries not derivable from W3D files)\n",
		label, (unsigned long long)c.OracleFiles, (unsigned long long)c.Considered, (unsigned long long)c.NotCatalogTypes);
	std::printf("[catalog oracle] %s: MATCH %llu of %llu\n", label, (unsigned long long)c.Match, (unsigned long long)c.Considered);
	std::printf("[catalog oracle] %s: mismatch classes: file absent %llu, offset past file %llu, no chunk at offset %llu, type/key differs %llu, length differs %llu\n",
		label, (unsigned long long)c.FileAbsent, (unsigned long long)c.OffsetOutOfRange, (unsigned long long)c.NoChunkAtOffset,
		(unsigned long long)c.TypeOrKeyDiffers, (unsigned long long)c.LengthDiffers);
	std::printf("[catalog oracle] %s: file fails validation (skipped whole) %llu\n", label, (unsigned long long)c.FileFailedValidation);
	std::printf("[catalog oracle] %s: WINNERS (what Find() returns for every oracle key): oracle's file %llu (exact offset+length %llu), "
		"no winner %llu, ANOTHER file %llu; oracle keys not named by their own file: %llu\n", label, (unsigned long long)c.WinnerSameFile,
		(unsigned long long)c.WinnerExact, (unsigned long long)c.WinnerMissing, (unsigned long long)c.WinnerOtherFile, (unsigned long long)c.OracleNotCanonical);
	std::printf("[catalog oracle] %s: records %zu, registered %zu, never registered (not in their loader's file) %zu, shadowed %zu, canonical ties %zu\n",
		label, catalog.Record_Count(), catalog.Registered_Count(), catalog.Uncatalogued_Count(), catalog.Shadowed_Count(), catalog.Canonical_Ties());
	std::printf("[catalog oracle] %s: scanned records the oracle does not list: %llu; duplicate oracle names: %llu\n",
		label, (unsigned long long)c.ScannedNotInOracle, (unsigned long long)c.OracleDuplicates);
	for (const auto &kv : c.MismatchByArchive)
	{
		std::printf("[catalog oracle] %s: non-matching entries whose file is served by %s: %llu\n", label, kv.first.c_str(), (unsigned long long)kv.second);
	}
	for (const std::string &s : c.Samples)
	{
		std::printf("[catalog oracle] %s:   %s\n", label, s.c_str());
	}
	for (const std::string &s : c.KeySamples)
	{
		std::printf("[catalog oracle] %s:   key/type: %s\n", label, s.c_str());
	}
	for (const AssetCatalogFileFault &f : catalog.Faults())
	{
		std::printf("[catalog oracle] %s:   scan fault in %s: %s\n", label, f.Path.c_str(), f.Reason.c_str());
	}
}

bool loadOracle(std::vector<OracleFile> &oracle)
{
	std::vector<unsigned char> bytes;
	std::string error;
	std::string path = retailtest::bfme2Install() + "/asset.dat";
	REQUIRE_MESSAGE(retailtest::readLocalFile(path, bytes, &error), error);
	REQUIRE_MESSAGE(parseAssetDat(bytes, oracle, &error), error);
	return true;
}

} // namespace

TEST_CASE("oracle parser reads the asset.dat layout (synthetic file)")
{
	// 1 file "A.W3D" with one MESH record, 1 group with one dependency.
	std::vector<unsigned char> b;
	auto u32 = [&](std::uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back((unsigned char)(v >> (8 * i))); };
	auto str = [&](const char *s) { b.push_back((unsigned char)std::strlen(s)); for (const char *p = s; *p; ++p) b.push_back((unsigned char)*p); };
	u32(0x45414C41); u32(0x102); u32(1); u32(1);
	str("A.W3D");
	for (int i = 0; i < 8; ++i) b.push_back(0);
	b.push_back(1); b.push_back(0); // u16 n = 1
	str("A.MESH");
	u32(ASSET_TYPE_MESH); u32(52); u32(132);
	str("G"); str("H");
	b.push_back(1); b.push_back(0);
	str("DEP");
	std::vector<OracleFile> files;
	std::string error;
	REQUIRE_MESSAGE(parseAssetDat(b, files, &error), error);
	REQUIRE(files.size() == 1);
	CHECK(files[0].Name == "A.W3D");
	REQUIRE(files[0].Entries.size() == 1);
	CHECK(files[0].Entries[0].Name == "A.MESH");
	CHECK(files[0].Entries[0].Type == ASSET_TYPE_MESH);
	CHECK(files[0].Entries[0].Offset == 52);
	CHECK(files[0].Entries[0].Length == 132);

	b.push_back(0); // trailing byte: not consumed exactly
	files.clear();
	CHECK_FALSE(parseAssetDat(b, files, &error));
	CHECK(error.find("consumed exactly") != std::string::npos);
}

TEST_CASE("catalog oracle: scanning the BFME2 1.06 archives alone reproduces retail asset.dat")
{
	retailtest::Mount *mount = retailtest::bfme2Mount();
	if (!mount)
	{
		retailtest::printSkip("asset catalog oracle (BFME2 1.06 alone)");
		return;
	}
	REQUIRE_MESSAGE(mount->fs != nullptr, mount->error);
	std::vector<OracleFile> oracle;
	loadOracle(oracle);

	auto t0 = std::chrono::steady_clock::now();
	AssetCatalog catalog;
	std::string error;
	REQUIRE_MESSAGE(catalog.Scan_File_System(*mount->fs, &error), error);
	double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
	Comparison c = compare(catalog, oracle, *mount->fs);
	report("BFME2 1.06 alone", catalog, c, seconds);

	// The oracle lists 39,874 prototypes a W3D scan can produce: MESH 23,702 + ANIM 6,149 + HLOD 5,189 + HIER 4,116 +
	// BOX 714 + PART 4. The spec's 38,745 is the pure 2.01 mount, where RotWK overrides some BFME2 files; see the next case.
	CHECK(c.Considered == 23702 + 6149 + 5189 + 4116 + 714 + 4);
	CHECK(c.Match + c.FileAbsent + c.FileFailedValidation + c.OffsetOutOfRange + c.NoChunkAtOffset + c.TypeOrKeyDiffers + c.LengthDiffers == c.Considered);
	// Every mismatch is an entry whose file is absent from the archives or whose bytes are not the bytes the
	// catalog indexed (w3d.big copies of 11 files, see the report above). None is a key or type disagreement.
	CHECK(c.TypeOrKeyDiffers == 0);
	CHECK(c.OracleDuplicates == 0);
	// Winners: Find() never returns a record of a different file than the oracle's (PTGRASS03 resolves to ptgrass03.w3d,
	// not cine_bones02.w3d). No winner = 9 absent files + 10 skipped corrupt entries.
	CHECK(c.OracleNotCanonical == 0);
	CHECK(c.WinnerOtherFile == 0);
	CHECK(c.WinnerSameFile == 39855);
	CHECK(c.WinnerMissing == 19);
	CHECK(c.WinnerExact == 39694);
	CHECK(catalog.Canonical_Ties() == 0);
	CHECK(c.Match == 39694);
	CHECK(c.FileFailedValidation == 10); // the 7 corrupt files of spec 2.3 hold 10 oracle entries; skipped whole, fail closed
	CHECK(c.FileAbsent == 9);
	CHECK(c.OffsetOutOfRange == 7);
	CHECK(c.NoChunkAtOffset == 99);
	CHECK(c.LengthDiffers == 55);
	// the scanner found no malformed top-level chunk in any BFME2 file except the corrupt ones (spec section 2.3)
	CHECK(catalog.Faults().size() <= 7);
}

TEST_CASE("catalog oracle: over the pure 2.01 + 1.06 mount the spec's 38,745 records match")
{
	retailtest::Mount *mount = retailtest::pureMount();
	if (!mount)
	{
		retailtest::printSkip("asset catalog oracle (pure 2.01 mount)");
		return;
	}
	REQUIRE_MESSAGE(mount->fs != nullptr, mount->error);
	std::vector<OracleFile> oracle;
	loadOracle(oracle);

	auto t0 = std::chrono::steady_clock::now();
	AssetCatalog catalog;
	std::string error;
	REQUIRE_MESSAGE(catalog.Scan_File_System(*mount->fs, &error), error);
	double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
	Comparison c = compare(catalog, oracle, *mount->fs);
	report("pure 2.01 + 1.06", catalog, c, seconds);
	// Spec section 1.1: "BFME2 asset.dat matches 38,745 entries" against the pure 2.01 files. Under the spec's own
	// definition (catcheck.py: 2-letter path, same chunk type and length, key not compared) this scanner reproduces
	// exactly that number. Under the stricter definition used here (leaf name, type, key, offset and length) it is 38,748:
	// +4 entries that live under data/editor/molds/ (the spec's path rule cannot reach them) and -1 whose chunk
	// keeps its type and length but has another name (iuovrseer_skn BOUNDINGBOX).
	// The 10 entries of the 7 skipped corrupt files matched under catcheck.py (which does not skip files): add them back.
	CHECK(c.SpecMatch + c.FileFailedValidation == 38745);
	CHECK(c.SpecMatch == 38735);
	CHECK(c.Match == 38738);
	CHECK(c.FileFailedValidation == 10);
	CHECK(c.Considered == 39874);
	CHECK(c.FileAbsent == 9);
	CHECK(c.OffsetOutOfRange == 71);
	CHECK(c.NoChunkAtOffset == 946);
	CHECK(c.TypeOrKeyDiffers == 46);
	CHECK(c.LengthDiffers == 54);
	CHECK(c.Match + c.FileAbsent + c.FileFailedValidation + c.OffsetOutOfRange + c.NoChunkAtOffset + c.TypeOrKeyDiffers + c.LengthDiffers == c.Considered);
	// Winners: for every oracle key, Find() returns a record of the oracle's file or nothing, never another file.
	// No winner = the 9 absent files + the 10 skipped corrupt entries + 89 keys that RotWK's replacement files no longer define.
	CHECK(c.OracleNotCanonical == 0);
	CHECK(c.WinnerOtherFile == 0);
	CHECK(c.WinnerSameFile == 39766);
	CHECK(c.WinnerMissing == 108);
	CHECK(c.WinnerExact == 38738);
	CHECK(catalog.Canonical_Ties() == 0);
}
