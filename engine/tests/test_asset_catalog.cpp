// OpenBFME unit tests: the asset catalog scanner on synthetic W3D files and BIG archives. GPL-3.0.
// Offsets and lengths are computed by hand from the chunk layout (8 byte chunk headers).

#include "doctest.h"
#include "BigTestUtil.h"
#include "W3dTestUtil.h"

#include "Common/ArchiveFile.h"
#include "GameEngineDevice/W3DDevice/GameClient/AssetCatalog.h"
#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"
#include "Libraries/WWVegas/WW3D2/w3d_file.h"

using namespace w3dtest;

namespace
{

std::vector<std::uint8_t> meshChunk(const char *container, const char *mesh)
{
	W3dMeshHeader3Struct mh = {};
	mh.Version = W3D_MAKE_VERSION(4, 2);
	setName(mh.MeshName, sizeof(mh.MeshName), mesh);
	setName(mh.ContainerName, sizeof(mh.ContainerName), container);
	ChunkWriter children;
	children.chunk(W3D_CHUNK_MESH_HEADER3, ChunkWriter::of(mh));
	ChunkWriter file;
	file.wrapper(W3D_CHUNK_MESH, children);
	return file.bytes;
}

void append(std::vector<std::uint8_t> &dst, const std::vector<std::uint8_t> &src)
{
	dst.insert(dst.end(), src.begin(), src.end());
}

std::vector<std::uint8_t> wrapped(std::uint32_t id, std::uint32_t headerId, const std::vector<std::uint8_t> &header)
{
	ChunkWriter children;
	children.chunk(headerId, header);
	ChunkWriter file;
	file.wrapper(id, children);
	return file.bytes;
}

// One file holding every prototype kind the catalog registers, plus a ShdMesh it must skip.
std::vector<std::uint8_t> everythingFile()
{
	std::vector<std::uint8_t> f;

	W3dHierarchyStruct hh = {};
	hh.Version = W3D_MAKE_VERSION(4, 1);
	setName(hh.Name, sizeof(hh.Name), "TEST_SKL");
	append(f, wrapped(W3D_CHUNK_HIERARCHY, W3D_CHUNK_HIERARCHY_HEADER, ChunkWriter::of(hh)));       // offset 0, 8 + 8 + 36 = 52

	append(f, meshChunk("TEST_SKN", "BODY"));                                                       // offset 52, 8 + 8 + 116 = 132

	W3dHLodHeaderStruct lh = {};
	lh.Version = W3D_MAKE_VERSION(1, 0);
	setName(lh.Name, sizeof(lh.Name), "TEST_SKN");
	setName(lh.HierarchyName, sizeof(lh.HierarchyName), "TEST_SKL");
	append(f, wrapped(W3D_CHUNK_HLOD, W3D_CHUNK_HLOD_HEADER, ChunkWriter::of(lh)));                  // offset 184, 8 + 8 + 40 = 56

	W3dAnimHeaderStruct ah = {};
	ah.Version = W3D_MAKE_VERSION(4, 1);
	setName(ah.Name, sizeof(ah.Name), "WALK");
	setName(ah.HierarchyName, sizeof(ah.HierarchyName), "TEST_SKL");
	ah.NumFrames = 10;
	ah.FrameRate = 30;
	append(f, wrapped(W3D_CHUNK_ANIMATION, W3D_CHUNK_ANIMATION_HEADER, ChunkWriter::of(ah)));        // offset 240, 8 + 8 + 44 = 60

	W3dCompressedAnimHeaderStruct ch = {};
	ch.Version = W3D_MAKE_VERSION(1, 0);
	setName(ch.Name, sizeof(ch.Name), "RUN");
	setName(ch.HierarchyName, sizeof(ch.HierarchyName), "TEST_SKL");
	ch.NumFrames = 10;
	ch.FrameRate = 30;
	append(f, wrapped(W3D_CHUNK_COMPRESSED_ANIMATION, W3D_CHUNK_COMPRESSED_ANIMATION_HEADER, ChunkWriter::of(ch))); // offset 300, 60

	W3dBoxStruct box = {};
	box.Version = W3D_MAKE_VERSION(1, 0);
	box.Attributes = 1;
	setName(box.Name, sizeof(box.Name), "TEST_SKN.BOUNDINGBOX");
	ChunkWriter boxChunk;
	boxChunk.chunk(W3D_CHUNK_BOX, ChunkWriter::of(box));
	append(f, boxChunk.bytes);                                                                      // offset 360, 8 + 68 = 76

	W3dEmitterHeaderStruct eh = {};
	eh.Version = 0x20000;
	setName(eh.Name, sizeof(eh.Name), "E_TEST");
	append(f, wrapped(W3D_CHUNK_EMITTER, W3D_CHUNK_EMITTER_HEADER, ChunkWriter::of(eh)));            // offset 436, 8 + 8 + 20 = 36

	ChunkWriter shd;
	shd.chunk(W3D_CHUNK_SHDMESH_NAME, { 'x', 0, 0, 0 });
	ChunkWriter shdFile;
	shdFile.wrapper(W3D_CHUNK_SHDMESH, shd);
	append(f, shdFile.bytes);                                                                       // offset 472: skipped
	return f;
}

} // namespace

TEST_CASE("catalog keys follow the spec: container.mesh, h*hierarchy, a*hierarchy.anim, all lower case")
{
	CHECK(AssetCatalog::Mesh_Key("GUMAArms_SKN", "MANATARMS") == "gumaarms_skn.manatarms");
	CHECK(AssetCatalog::Mesh_Key("", "BOWL") == "bowl");
	CHECK(AssetCatalog::Hierarchy_Key("GUMAARMS_SKL") == "h*gumaarms_skl");
	CHECK(AssetCatalog::Animation_Key("GUMAARMS_SKL", "GUMAARMS_IDLA") == "a*gumaarms_skl.gumaarms_idla");
}

TEST_CASE("scanner registers one record per top-level prototype chunk with its header offset and chunk size + 8")
{
	std::vector<std::uint8_t> file = everythingFile();
	std::vector<AssetCatalogRecord> records;
	std::vector<std::uint32_t> skipped;
	std::string error;
	REQUIRE_MESSAGE(AssetCatalog::Scan_W3D_File("art\\w3d\\te\\Test_SKN.w3d", file.data(), file.size(), records, &skipped, &error), error);

	// Canonical = the file stem (test_skn) is what retail's loader derives from the key: mesh and box up to the first
	// '.', HLOD the whole name; a hierarchy would be loaded from test_skl.w3d, an animation from walk.w3d / run.w3d
	// (text after the first '.'), an emitter from e_test.w3d, so those records of this file are not canonical.
	struct Expect { const char *key; std::uint32_t type; std::uint32_t offset, length; bool canonical; } expected[] = {
		{ "h*test_skl", ASSET_TYPE_HIER, 0, 52, false },
		{ "test_skn.body", ASSET_TYPE_MESH, 52, 132, true },
		{ "test_skn", ASSET_TYPE_HLOD, 184, 56, true },
		{ "a*test_skl.walk", ASSET_TYPE_ANIM, 240, 60, false },
		{ "a*test_skl.run", ASSET_TYPE_ANIM, 300, 60, false }, // a compressed animation keys the same way
		{ "test_skn.boundingbox", ASSET_TYPE_BOX, 360, 76, true },
		{ "e_test", ASSET_TYPE_PART, 436, 36, false },
	};
	REQUIRE(records.size() == 7);
	for (size_t i = 0; i < 7; ++i)
	{
		CHECK_MESSAGE(records[i].Key == expected[i].key, "record " << i);
		CHECK_MESSAGE(records[i].Type == expected[i].type, "record " << i);
		CHECK_MESSAGE(records[i].Offset == expected[i].offset, "record " << i);
		CHECK_MESSAGE(records[i].Length == expected[i].length, "record " << i);
		CHECK_MESSAGE(records[i].Canonical == expected[i].canonical, "record " << i);
		CHECK(records[i].File == "test_skn.w3d");
		CHECK(records[i].Path == "art\\w3d\\te\\test_skn.w3d");
	}
	CHECK(skipped == std::vector<std::uint32_t>{ W3D_CHUNK_SHDMESH }); // the ShdMesh is not a catalog prototype
	CHECK(std::string(Asset_Type_Name(ASSET_TYPE_MESH)) == "MESH");
	CHECK(ASSET_TYPE_HLOD == 0x484C4F44); // 'HLOD' as asset.dat stores it
}

TEST_CASE("headers at every byte alignment: an odd-sized chunk before the prototypes does not misalign the reads (UBSan: no typed read over the byte buffer)")
{
	// A chunk body of 1, 2 or 3 bytes puts every following header at an odd offset (8 + n). Reading a W3dHLodHeaderStruct (4 byte alignment) through a
	// pointer to that address is undefined behaviour; the scanner reads the names at byte offsets instead.
	for (std::uint32_t oddSize : { 1u, 2u, 3u, 5u })
	{
		const std::vector<std::uint8_t> reference = everythingFile();
		std::vector<std::uint8_t> file;
		ChunkWriter pre;
		pre.chunk(0x0000BEEF, std::vector<std::uint8_t>(oddSize, 0x5A)); // an unknown top-level chunk: skipped
		append(file, pre.bytes);
		append(file, reference);
		const std::uint32_t shift = 8 + oddSize;
		REQUIRE(((shift) & 3u) != 0u);

		std::vector<AssetCatalogRecord> records;
		std::vector<std::uint32_t> skipped;
		std::string error;
		REQUIRE_MESSAGE(AssetCatalog::Scan_W3D_File("art\\w3d\\te\\Test_SKN.w3d", file.data(), file.size(), records, &skipped, &error), error);
		const char *keys[] = { "h*test_skl", "test_skn.body", "test_skn", "a*test_skl.walk", "a*test_skl.run", "test_skn.boundingbox", "e_test" };
		const std::uint32_t offsets[] = { 0, 52, 184, 240, 300, 360, 436 };
		REQUIRE(records.size() == 7);
		for (size_t i = 0; i < 7; ++i)
		{
			CHECK_MESSAGE(records[i].Key == keys[i], "odd size " << oddSize << " record " << i);
			CHECK_MESSAGE(records[i].Offset == offsets[i] + shift, "odd size " << oddSize << " record " << i);
		}
		CHECK(skipped == std::vector<std::uint32_t>{ 0x0000BEEF, W3D_CHUNK_SHDMESH });
	}
}

TEST_CASE("a name that fills the 16 byte header field keeps its truncation in the key")
{
	// Header names are 16 bytes with no terminator when they are 16 characters long; ZH and the catalog use them as they stand.
	W3dAnimHeaderStruct ah = {};
	ah.Version = W3D_MAKE_VERSION(4, 1);
	std::memcpy(ah.Name, "EUHALDIR_CHRS_TX", 16); // 16 characters
	setName(ah.HierarchyName, sizeof(ah.HierarchyName), "H");
	std::vector<std::uint8_t> file = wrapped(W3D_CHUNK_ANIMATION, W3D_CHUNK_ANIMATION_HEADER, ChunkWriter::of(ah));
	std::vector<AssetCatalogRecord> records;
	std::string error;
	REQUIRE_MESSAGE(AssetCatalog::Scan_W3D_File("a.w3d", file.data(), file.size(), records, nullptr, &error), error);
	REQUIRE(records.size() == 1);
	CHECK(records[0].Key == "a*h.euhaldir_chrs_tx");
}

TEST_CASE("source stems: the file retail's prototype loader opens for each kind of name")
{
	// Open-BFME-1 loaders: mesh/box = name up to the first '.', HLOD = whole name, hierarchy = name after "h*",
	// animation = text after the first '.'.
	CHECK(AssetCatalog::Source_Stem("gumaarms_skn.manatarms", ASSET_TYPE_MESH) == "gumaarms_skn");
	CHECK(AssetCatalog::Source_Stem("bowl", ASSET_TYPE_MESH) == "bowl");
	CHECK(AssetCatalog::Source_Stem("gumaarms_skn.boundingbox", ASSET_TYPE_BOX) == "gumaarms_skn");
	CHECK(AssetCatalog::Source_Stem("gumaarms_skn", ASSET_TYPE_HLOD) == "gumaarms_skn");
	CHECK(AssetCatalog::Source_Stem("h*gumaarms_skl", ASSET_TYPE_HIER) == "gumaarms_skl");
	CHECK(AssetCatalog::Source_Stem("a*gumaarms_skl.gumaarms_idla", ASSET_TYPE_ANIM) == "gumaarms_idla");
	CHECK(AssetCatalog::Source_Stem("a*nodot", ASSET_TYPE_ANIM).empty()); // the loader opens nothing
	CHECK(AssetCatalog::Source_Stem("e_fire_sm", ASSET_TYPE_PART) == "e_fire_sm");
}

TEST_CASE("an undotted box name never registers: retail's box loader opens the literal name, no .w3d appended")
{
	// Open-BFME-1 Rva00972480Proto_LoadBox.cpp:62-68 / BFME2 RVA 0x001808D5-0x001808FC: the box loader replaces from the
	// first '.' and leaves an undotted name unchanged. BOXONLY inside boxonly.w3d is therefore not what retail opens.
	auto boxFile = [](const char *name) {
		W3dBoxStruct box = {};
		box.Version = W3D_MAKE_VERSION(1, 0);
		box.Attributes = 1;
		setName(box.Name, sizeof(box.Name), name);
		ChunkWriter w;
		w.chunk(W3D_CHUNK_BOX, ChunkWriter::of(box));
		return w.bytes;
	};
	std::string error;
	AssetCatalog undotted;
	std::vector<std::uint8_t> a = boxFile("BOXONLY");
	REQUIRE(undotted.Add_W3D_File("art\\w3d\\bo\\boxonly.w3d", a.data(), a.size(), &error));
	CHECK(undotted.Find("boxonly") == nullptr);
	CHECK(undotted.Registered_Count() == 0);
	CHECK(undotted.Uncatalogued_Count() == 1);
	CHECK(AssetCatalog::Source_Stem("boxonly", ASSET_TYPE_BOX).empty());
	// a mesh with no dot does append ".w3d" (Rva00970EC0Proto_Load.cpp:85-91) and a dotted box registers as before
	CHECK(AssetCatalog::Source_Stem("boxonly", ASSET_TYPE_MESH) == "boxonly");
	AssetCatalog dotted;
	std::vector<std::uint8_t> b = boxFile("BOXONLY.BOX01");
	REQUIRE(dotted.Add_W3D_File("art\\w3d\\bo\\boxonly.w3d", b.data(), b.size(), &error));
	CHECK(dotted.Find("boxonly.box01") != nullptr);
}

TEST_CASE("only records in their loader's file are registered: PTGRASS03 resolves to ptgrass03.w3d, not an earlier file")
{
	// Retail oracle: PTGRASS03 -> ptgrass03.w3d. cine_bones02.w3d also holds a mesh named PTGRASS03 and sorts first by
	// name, but retail's mesh loader opens ptgrass03.w3d for the name PTGRASS03, so a record anywhere else cannot be it.
	std::vector<std::uint8_t> wrong = meshChunk("", "PTGRASS03");
	std::vector<std::uint8_t> right = meshChunk("", "PTGRASS03");
	append(right, meshChunk("", "PTGRASS03")); // a longer file so the records differ
	AssetCatalog catalog;
	std::string error;
	REQUIRE(catalog.Add_W3D_File("art\\w3d\\ci\\cine_bones02.w3d", wrong.data(), wrong.size(), &error));
	REQUIRE(catalog.Add_W3D_File("art\\w3d\\pt\\ptgrass03.w3d", right.data(), right.size(), &error));
	const AssetCatalogRecord *winner = catalog.Find("PTGRASS03");
	REQUIRE(winner);
	CHECK(winner->File == "ptgrass03.w3d");
	CHECK(winner->Canonical);
	CHECK(winner->Offset == 0);
	CHECK(catalog.Record_Count() == 3);
	CHECK(catalog.Uncatalogued_Count() == 1); // the cine_bones02 copy is scanned and kept in Files() but never registered
	REQUIRE(catalog.Files()[0].size() == 1);
	CHECK_FALSE(catalog.Files()[0][0].Canonical);
	CHECK(catalog.Canonical_Ties() == 0);   // the second PTGRASS03 chunk is in the same file: not a tie between files
	CHECK(catalog.Registered_Count() == 1);

	// a key only a non-canonical file holds is not in the catalog at all (retail's loader would open a file that is not there)
	AssetCatalog only;
	REQUIRE(only.Add_W3D_File("art\\w3d\\ci\\cine_bones02.w3d", wrong.data(), wrong.size(), &error));
	CHECK(only.Find("ptgrass03") == nullptr);
	CHECK(only.Registered_Count() == 0);
}

TEST_CASE("two canonical files with one name: the 2-letter path of spec 1.4 wins, whatever the order; the tie is counted")
{
	std::vector<std::uint8_t> a = meshChunk("SHARED", "ONE");
	std::vector<std::uint8_t> b = meshChunk("SHARED", "ONE");
	append(b, meshChunk("SHARED", "TWO"));
	AssetCatalog catalog;
	std::string error;
	REQUIRE(catalog.Add_W3D_File("data\\editor\\shared.w3d", a.data(), a.size(), &error));
	REQUIRE(catalog.Add_W3D_File("art\\w3d\\sh\\shared.w3d", b.data(), b.size(), &error));
	CHECK(catalog.Canonical_Ties() == 1);
	const AssetCatalogRecord *one = catalog.Find("SHARED.ONE"); // lookups are case-insensitive
	REQUIRE(one);
	CHECK(one->Path == "art\\w3d\\sh\\shared.w3d");
	const AssetCatalogRecord *two = catalog.Find("shared.two");
	REQUIRE(two);
	CHECK(two->Offset == a.size());
	CHECK(catalog.Find("shared.three") == nullptr);
	CHECK(catalog.Shadowed_Count() == 1); // the data/editor copy of SHARED.ONE lost to the 2-letter path file
	REQUIRE(catalog.Files().size() == 2);
}

TEST_CASE("a file that fails validation registers nothing and keeps no records (spec 2.3, fail closed)")
{
	// one valid prototype followed by a chunk cut short: the first record must not be registered
	std::vector<std::uint8_t> file = meshChunk("OK", "MESH");
	append(file, meshChunk("OK", "TWO"));
	file.resize(file.size() - 10); // cut into the second chunk
	AssetCatalog catalog;
	std::string error;
	CHECK_FALSE(catalog.Add_W3D_File("art\\w3d\\ok\\ok.w3d", file.data(), file.size(), &error));
	CHECK(error.find("overruns") != std::string::npos);
	REQUIRE(catalog.Faults().size() == 1);
	CHECK(catalog.Faults()[0].Path == "art\\w3d\\ok\\ok.w3d");
	CHECK(catalog.Find("ok.mesh") == nullptr);
	CHECK(catalog.Registered_Count() == 0);
	CHECK(catalog.Record_Count() == 0);
	REQUIRE(catalog.Files().size() == 1);
	CHECK(catalog.Files()[0].empty());
	// it cannot claim a key before a valid later file either
	std::vector<std::uint8_t> good = meshChunk("OK", "MESH");
	REQUIRE(catalog.Add_W3D_File("art\\w3d\\ok\\ok2.w3d", good.data(), good.size(), &error) == true);
	CHECK(catalog.Find("ok.mesh") == nullptr); // ok2.w3d is not the file retail would open for "ok.mesh"

	// trailing bytes that are not a chunk header
	std::vector<std::uint8_t> trailing = meshChunk("T", "M");
	trailing.push_back(0);
	CHECK_FALSE(catalog.Add_W3D_File("t.w3d", trailing.data(), trailing.size(), &error));
	CHECK(error.find("partial chunk header") != std::string::npos);
	CHECK(catalog.Faults().size() == 2);

	// corruption deeper than the top level also fails the file: a mesh child overrunning its parent
	ChunkWriter inner;
	inner.u32(W3D_CHUNK_VERTICES);
	inner.u32(100); // claims 100 bytes inside a 4 byte parent
	inner.u32(0);
	ChunkWriter deep;
	deep.wrapper(W3D_CHUNK_MESH, inner);
	CHECK_FALSE(catalog.Add_W3D_File("d.w3d", deep.bytes.data(), deep.bytes.size(), &error));
	CHECK(catalog.Faults().size() == 3);
}

TEST_CASE("a mesh that does not start with its header, or a short box, is an error and nothing is guessed")
{
	ChunkWriter children;
	children.chunk(W3D_CHUNK_VERTICES, { 0, 0, 0, 0 });
	ChunkWriter file;
	file.wrapper(W3D_CHUNK_MESH, children);
	std::vector<AssetCatalogRecord> records;
	std::string error;
	CHECK_FALSE(AssetCatalog::Scan_W3D_File("m.w3d", file.bytes.data(), file.bytes.size(), records, nullptr, &error));
	CHECK(error.find("W3D_CHUNK_MESH_HEADER3") != std::string::npos);
	CHECK(records.empty());

	ChunkWriter box;
	box.chunk(W3D_CHUNK_BOX, { 1, 2, 3 });
	CHECK_FALSE(AssetCatalog::Scan_W3D_File("b.w3d", box.bytes.data(), box.bytes.size(), records, nullptr, &error));
	CHECK(error.find("short W3D_CHUNK_BOX") != std::string::npos);
}

TEST_CASE("scanning a mounted file system walks files sorted by leaf name and reads the owning archive's copy")
{
	// Two archives mounted first-wins: "early" supplies art\w3d\zz\late_name.w3d; "late" also supplies it
	// (shadowed) plus art\w3d\aa\aaa.w3d, which sorts first by leaf name.
	std::vector<std::uint8_t> mine = meshChunk("LATE_NAME", "EARLY");
	std::vector<std::uint8_t> shadowed = meshChunk("LATE_NAME", "SHADOWED");
	std::vector<std::uint8_t> other = meshChunk("AAA", "M");
	auto toStr = [](const std::vector<std::uint8_t> &v) { return std::string(v.begin(), v.end()); };

	Win32BIGFileSystem fs;
	std::string error;
	auto early = std::make_shared<const std::vector<std::uint8_t>>(
		bigtest::makeBig("BIGF", { { "Art\\W3D\\zz\\Late_Name.w3d", toStr(mine) } }));
	auto late = std::make_shared<const std::vector<std::uint8_t>>(
		bigtest::makeBig("BIGF", { { "art\\w3d\\zz\\late_name.w3d", toStr(shadowed) }, { "art\\w3d\\aa\\aaa.w3d", toStr(other) }, { "readme.txt", "x" } }));
	fs.mountArchive(fs.openArchiveFromMemory("early.big", early, &error), "early.big", false);
	fs.mountArchive(fs.openArchiveFromMemory("late.big", late, &error), "late.big", false);

	AssetCatalog catalog;
	REQUIRE_MESSAGE(catalog.Scan_File_System(fs, &error), error);
	REQUIRE(catalog.File_Names().size() == 2);
	CHECK(catalog.File_Names()[0] == "art\\w3d\\aa\\aaa.w3d");
	CHECK(catalog.File_Names()[1] == "art\\w3d\\zz\\late_name.w3d");
	CHECK(catalog.Find("late_name.early") != nullptr);
	CHECK(catalog.Find("late_name.shadowed") == nullptr); // the shadowed copy is never read
	CHECK(catalog.Find("aaa.m") != nullptr);
	CHECK(catalog.Faults().empty());

	Win32BIGFileSystem empty;
	AssetCatalog none;
	CHECK_FALSE(none.Scan_File_System(empty, &error));
	CHECK(error.find("no .w3d") != std::string::npos);
}
