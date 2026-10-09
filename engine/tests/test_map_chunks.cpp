// OpenBFME unit tests: map chunk parsers on synthetic chunk bytes. GPL-3.0.
//
// One test per parser. The bytes are built by tests/MapTestUtil.h from the layouts in spec
// maps-and-terrain.md 1.5-1.7; every expected value is written out literally in the test.

#include "doctest.h"
#include "MapTestUtil.h"

#include "Common/DataChunk.h"
#include "GameClient/MapUtil.h"

#include <string>

using namespace maptest;

namespace
{

bool loadMap(const Bytes &data, LoadedMap &out, std::string *err, bool strict = true)
{
	MapReadOptions opt;
	opt.strict = strict;
	return MapReader::load(data, "synthetic.map", opt, out, err);
}

Bytes heightMapChunk(Toc &t, int w, int h, int border, std::vector<std::pair<int, int>> boundaries, const std::vector<int> &heights)
{
	W p;
	p.i32(w).i32(h).i32(border).i32((int)boundaries.size());
	for (auto &b : boundaries)
	{
		p.i32(b.first).i32(b.second);
	}
	p.i32(w * h);
	for (int v : heights)
	{
		p.u16(v);
	}
	return chunk(t, "HeightMapData", 5, p.b);
}

} // namespace

// ------------------------------------------------------------------------------------------------
// DataChunkInput
// ------------------------------------------------------------------------------------------------

TEST_CASE("DataChunkInput: table of contents, nested parse, registered userData wins, leftover and unknown chunks are recorded")
{
	Toc t;
	Bytes child1 = chunk(t, "Leaf", 3, W().i32(7).i32(8).b);
	Bytes child2 = chunk(t, "Leaf", 4, W().i32(9).b);
	Bytes outer = chunk(t, "Outer", 1, W().u8(0x55).raw(cat(child1, child2)).b);
	Bytes stray = chunk(t, "Mystery", 2, W().i32(1).b);
	Bytes leftover = chunk(t, "Leftover", 1, W().i32(1).i32(2).b);
	Bytes data = file(t, cat(cat(outer, stray), leftover));

	struct State
	{
		std::vector<std::pair<int, std::int32_t>> leaves; // version, first int
		int outerHeader = -1;
		std::vector<void *> userDataSeen;
	} st;
	int argMarker = 0;

	ChunkInputStream stream(data.data(), data.size());
	DataChunkInput in(&stream);
	REQUIRE(in.isValidFileType());
	CHECK(in.contents().size() == 4);
	CHECK(in.contents().getName(1) == "Leaf");
	CHECK(in.contents().getName(99).empty()); // unknown id -> empty, like DataChunkTableOfContents::getName

	in.registerParser("Outer", "", [](DataChunkInput &f, DataChunkInfo *, void *ud) {
		State *s = (State *)ud;
		s->outerHeader = f.readByte();
		return f.parse(nullptr); // the children must still receive their OWN registered userData
	}, &st);
	in.registerParser("Leaf", "Outer", [](DataChunkInput &f, DataChunkInfo *info, void *ud) {
		State *s = (State *)ud;
		s->leaves.push_back({ info->version, f.readInt() });
		return true;
	}, &st);
	// registered without userData: parse()'s own argument is passed instead
	in.registerParser("Leftover", "", [](DataChunkInput &f, DataChunkInfo *, void *ud) {
		CHECK(*(int *)ud == 0); // &argMarker
		f.readInt(); // reads 4 of 8 bytes
		return true;
	});

	REQUIRE(in.parse(&argMarker));
	CHECK(st.outerHeader == 0x55);
	REQUIRE(st.leaves.size() == 2);
	CHECK(st.leaves[0].first == 3);
	CHECK(st.leaves[0].second == 7); // the second int (8) of the first leaf is left over
	CHECK(st.leaves[1].first == 4);
	CHECK(st.leaves[1].second == 9);

	// issues: Leaf v3 has 4 leftover bytes, "Mystery" is unknown, "Leftover" has 4 unread bytes
	for (const auto &i : in.issues())
	{
		INFO("issue kind " << i.kind << " label " << i.label << " parent '" << i.parentLabel << "' bytes " << i.bytes);
	}
	REQUIRE(in.issues().size() == 3);
	CHECK(in.issues()[0].kind == DataChunkInput::Issue::LeftoverBytes);
	CHECK(in.issues()[0].label == "Leaf");
	CHECK(in.issues()[0].parentLabel == "Outer");
	CHECK(in.issues()[0].bytes == 4);
	CHECK(in.issues()[1].kind == DataChunkInput::Issue::UnknownChunk);
	CHECK(in.issues()[1].label == "Mystery");
	CHECK(in.issues()[1].bytes == 4);
	CHECK(in.issues()[2].kind == DataChunkInput::Issue::LeftoverBytes);
	CHECK(in.issues()[2].label == "Leftover");
	CHECK(in.issues()[2].bytes == 4);

	// the chunk log: file order with depth
	REQUIRE(in.chunkLog().size() == 5);
	CHECK(in.chunkLog()[0].depth == 0);
	CHECK(in.chunkLog()[1].depth == 1);
	CHECK(in.chunkLog()[1].version == 3);
	CHECK(in.chunkLog()[2].version == 4);
}

TEST_CASE("DataChunkInput: files without a CkMp table, truncated chunks and reads past a chunk are errors")
{
	Bytes legacy = { 'x', 'x', 'x', 'x', 1, 0, 0, 0 };
	ChunkInputStream s1(legacy.data(), legacy.size());
	DataChunkInput in1(&s1);
	CHECK_FALSE(in1.isValidFileType());
	CHECK_FALSE(in1.parse()); // ZH: cannot parse a legacy file

	// chunk size claims more than the file has
	{
		Toc t;
		Bytes c = chunk(t, "A", 1, W().i32(1).b);
		c[6] = 0x40; // size byte 0 -> 0x40 > what is left
		Bytes data = file(t, c);
		ChunkInputStream s(data.data(), data.size());
		DataChunkInput in(&s);
		in.registerParser("A", "", [](DataChunkInput &, DataChunkInfo *, void *) { return true; });
		CHECK_THROWS_AS(in.parse(), MapParseError);
	}
	// reading past the end of a chunk
	{
		Toc t;
		Bytes data = file(t, chunk(t, "A", 1, W().u8(1).b));
		ChunkInputStream s(data.data(), data.size());
		DataChunkInput in(&s);
		in.registerParser("A", "", [](DataChunkInput &f, DataChunkInfo *, void *) {
			f.readInt();
			return true;
		});
		CHECK_THROWS_AS(in.parse(), MapParseError);
	}
	// a child chunk that overruns its parent
	{
		Toc t;
		Bytes child = chunk(t, "B", 1, W().i32(1).i32(2).b);
		Bytes parent = chunk(t, "A", 1, W().raw(Bytes(child.begin(), child.end() - 4)).b); // parent too short for the child
		// parent size covers header + 4 of the child's 8 payload bytes; append the rest after the parent so the file is whole
		Bytes data = file(t, cat(parent, Bytes(4, 0)));
		ChunkInputStream s(data.data(), data.size());
		DataChunkInput in(&s);
		in.registerParser("A", "", [](DataChunkInput &f, DataChunkInfo *, void *ud) { return f.parse(ud); });
		in.registerParser("B", "A", [](DataChunkInput &, DataChunkInfo *, void *) { return true; });
		CHECK_THROWS_AS(in.parse(), MapParseError);
	}
}

TEST_CASE("DataChunkInput: Dict reads all five value types, NameKey checks type and id")
{
	Toc t;
	D d(t);
	d.b("flag", true).i("count", -5).r("scale", 1.5f).s("name", "Hobbit").u("title", u"Wärd");
	W payload;
	payload.raw(d.bytes());
	payload.i32(t.key("waypointID", 3)); // a NameKey
	Bytes data = file(t, chunk(t, "Holder", 1, payload.b));

	ChunkInputStream stream(data.data(), data.size());
	DataChunkInput in(&stream);
	struct Out
	{
		Dict dict;
		std::string nameKey;
	} out;
	in.registerParser("Holder", "", [](DataChunkInput &f, DataChunkInfo *, void *ud) {
		Out *o = (Out *)ud;
		o->dict = f.readDict();
		o->nameKey = f.readNameKey();
		return true;
	}, &out);
	Dict &got = out.dict;
	std::string &nameKey = out.nameKey;
	REQUIRE(in.parse());
	CHECK(in.issues().empty());
	REQUIRE(got.getPairCount() == 5);
	bool ex = false;
	CHECK(got.getBool("flag", &ex) == true);
	CHECK(ex);
	CHECK(got.getInt("count") == -5);
	CHECK(got.getReal("scale") == 1.5f);
	CHECK(got.getAsciiString("name") == "Hobbit");
	CHECK(got.getUnicodeString("title") == std::u16string(u"Wärd"));
	CHECK(got.getInt("flag", &ex) == 0); // wrong type -> not present as an int
	CHECK_FALSE(ex);
	CHECK(nameKey == "waypointID");
	CHECK(got.pairs()[0].key == "flag"); // file order is kept
}

TEST_CASE("DataChunkInput: an unknown Dict type code is a corrupt-file error")
{
	Toc t;
	D d(t);
	d.bad("oops", 9);
	Bytes data = file(t, chunk(t, "Holder", 1, d.bytes()));
	ChunkInputStream stream(data.data(), data.size());
	DataChunkInput in(&stream);
	in.registerParser("Holder", "", [](DataChunkInput &f, DataChunkInfo *, void *) {
		f.readDict();
		return true;
	});
	CHECK_THROWS_AS(in.parse(), MapParseError);
}

// ------------------------------------------------------------------------------------------------
// HeightMapData
// ------------------------------------------------------------------------------------------------

TEST_CASE("HeightMapData v5: size, border, boundaries, 16-bit heights")
{
	Toc t;
	Bytes data = file(t, heightMapChunk(t, 3, 2, 1, { { 1, 0 }, { 2, 0 } }, { 0, 1, 256, 65535, 4, 5 }));
	LoadedMap m;
	std::string err;
	REQUIRE_MESSAGE(loadMap(data, m, &err), err);
	const WorldHeightMap &h = m.heightMap;
	CHECK(h.m_width == 3);
	CHECK(h.m_height == 2);
	CHECK(h.m_borderSize == 1);
	REQUIRE(h.m_boundaries.size() == 2);
	CHECK(h.m_boundaries[0].x == 1);
	CHECK(h.m_boundaries[1].x == 2);
	REQUIRE(h.m_data.size() == 6);
	CHECK(h.getHeight(0, 0) == 0);
	CHECK(h.getHeight(1, 0) == 1);
	CHECK(h.getHeight(2, 0) == 256);
	CHECK(h.getHeight(0, 1) == 65535); // row-major: index = y*w + x
	CHECK(h.getHeight(2, 1) == 5);
	CHECK(m.hasHeightMap);
	CHECK(m.envelope == "raw");
	CHECK(m.topLevelOrder == std::vector<std::string>{ "HeightMapData" });
	CHECK(m.versions.at("/HeightMapData") == std::set<int>{ 5 });
}

TEST_CASE("HeightMapData v4 promotes u8 heights to u16 as (b*16.0+0.5), v3 synthesises one boundary")
{
	Toc t;
	W p;
	p.i32(2).i32(2).i32(0).i32(1).i32(1).i32(1).i32(4).u8(255).u8(1).u8(0).u8(128);
	Bytes data = file(t, chunk(t, "HeightMapData", 4, p.b));
	LoadedMap m;
	std::string err;
	REQUIRE_MESSAGE(loadMap(data, m, &err), err);
	CHECK(m.heightMap.m_data == std::vector<std::uint16_t>{ 4080, 16, 0, 2048 });

	Toc t3;
	// v3 has no boundary list: [w-2b, h-2b] = [4, 3]; its heights are 8-bit like v4.
	W p3b;
	p3b.i32(6).i32(5).i32(1).i32(30);
	for (int i = 0; i < 30; ++i)
	{
		p3b.u8(i);
	}
	Bytes d3 = file(t3, chunk(t3, "HeightMapData", 3, p3b.b));
	LoadedMap m3;
	REQUIRE_MESSAGE(loadMap(d3, m3, &err), err);
	REQUIRE(m3.heightMap.m_boundaries.size() == 1);
	CHECK(m3.heightMap.m_boundaries[0].x == 4);
	CHECK(m3.heightMap.m_boundaries[0].y == 3);
	CHECK(m3.heightMap.getHeight(5, 4) == (std::uint16_t)(29 * 16.0f + 0.5f));
}

TEST_CASE("HeightMapData rejects dataSize != width*height and unsupported versions")
{
	Toc t;
	W p;
	p.i32(3).i32(2).i32(0).i32(0).i32(5);
	for (int i = 0; i < 5; ++i)
	{
		p.u16(i);
	}
	LoadedMap m;
	std::string err;
	CHECK_FALSE(loadMap(file(t, chunk(t, "HeightMapData", 5, p.b)), m, &err));
	CHECK(err.find("dataSize 5 != width*height") != std::string::npos);

	Toc t2;
	CHECK_FALSE(loadMap(file(t2, chunk(t2, "HeightMapData", 2, W().i32(1).b)), m, &err));
	CHECK(err.find("HeightMapData version 2 not supported") != std::string::npos);
}

// ------------------------------------------------------------------------------------------------
// BlendTileData
// ------------------------------------------------------------------------------------------------

namespace
{
// A blend tile record (v>=4 form).
void blendRecord(W &p, int blendNdx, int horiz, int vert, int rd, int ld, int inverted, int longDiag, int custom)
{
	p.i32(blendNdx).u8(horiz).u8(vert).u8(rd).u8(ld).u8(inverted).u8(longDiag).i32(custom).u32(0x7ADA0000u);
}
} // namespace

TEST_CASE("BlendTileData v18: wide index arrays, odd-width bit planes, tile and class tables, index patching")
{
	Toc t;
	const int w = 9, h = 2, n = w * h, stride = 2; // (9+7)/8 = 2 bytes per row
	Bytes hm = heightMapChunk(t, w, h, 0, { { 9, 2 } }, std::vector<int>(n, 100));
	W p;
	p.i32(n);
	for (int i = 0; i < n; ++i) p.u16(i * 3);                         // tileNdx
	for (int i = 0; i < n; ++i) p.i32(i == 5 ? 1 : (i == 6 ? 7 : 0)); // blend: [5]=1 ok, [6]=7 out of range
	for (int i = 0; i < n; ++i) p.i32(i == 7 ? 1 : 0);                // extra blend
	for (int i = 0; i < n; ++i) p.i32(i == 3 ? 1 : (i == 4 ? 5 : 0)); // cliff: [3]=1 ok, [4]=5 out of range
	// planes (stride*h = 4 bytes): cliff state sets cell (0,0) and (8,0)
	p.u8(0x01).u8(0x01).u8(0x00).u8(0x00);
	p.raw(Bytes(4, 0xFF)); // A
	p.raw(Bytes(4, 0x0F)); // B
	p.raw(Bytes(4, 0x00)); // taint
	p.raw(Bytes(4, 0xF0)); // extra pass
	for (int i = 0; i < n; ++i) p.u8(i % 4); // flammability
	p.raw(Bytes(4, 0xFF)); // visibility
	p.i32(8).i32(2).i32(2); // numBitmapTiles, numBlendedTiles, numCliffInfo
	p.i32(2);               // texture classes
	p.i32(0).i32(4).i32(2).i32(0).astr("Grass");
	p.i32(4).i32(4).i32(2).i32(0).astr("Rock");
	p.i32(0).i32(0);        // numEdgeTiles, numEdgeTextureClasses
	blendRecord(p, 9, 1, 0, 0, 0, 2, 1, -1);
	p.i32(4).f32(0.125f).f32(0.25f).f32(0.375f).f32(0.5f).f32(0.625f).f32(0.75f).f32(0.875f).f32(1.0f).u8(1).u8(0);
	Bytes data = file(t, cat(hm, chunk(t, "BlendTileData", 18, p.b)));

	LoadedMap m;
	std::string err;
	REQUIRE_MESSAGE(loadMap(data, m, &err), err);
	const WorldHeightMap &wm = m.heightMap;
	REQUIRE(wm.m_hasBlendTileData);
	CHECK(wm.m_blendTileVersion == 18);
	CHECK(wm.m_planeStride == stride);
	CHECK(wm.m_tileNdxes[4] == 12);
	CHECK(wm.m_blendTileNdxes[5] == 1);
	CHECK(wm.m_blendTileNdxes[6] == 0);   // patched: 7 >= numBlendedTiles
	CHECK(wm.m_extraBlendTileNdxes[7] == 1);
	CHECK(wm.m_cliffInfoNdxes[3] == 1);
	CHECK(wm.m_cliffInfoNdxes[4] == 0);   // patched: 5 >= numCliffInfo
	CHECK(wm.m_patchReport.blendIndicesPatched == 1);
	CHECK(wm.m_patchReport.cliffIndicesPatched == 1);
	CHECK(wm.m_patchReport.extraBlendIndicesPatched == 0);
	// planes: bit (x&7) of byte [y*stride + (x>>3)]
	CHECK(wm.getCliffState(0, 0));
	CHECK(wm.getCliffState(8, 0)); // second byte, bit 0: only reachable with the (w+7)/8 stride
	CHECK_FALSE(wm.getCliffState(1, 0));
	CHECK_FALSE(wm.getCliffState(0, 1));
	CHECK_FALSE(wm.getCliffState(9, 0)); // outside the map
	CHECK(wm.m_planeA.size() == 4);
	CHECK(wm.planeBit(wm.m_planeA, 8, 1));
	CHECK(wm.planeBit(wm.m_planeB, 0, 0));
	CHECK_FALSE(wm.planeBit(wm.m_planeB, 4, 0));
	CHECK(wm.planeBit(wm.m_planeExtraPass, 4, 0));
	CHECK(wm.m_flammability.size() == (size_t)n);
	CHECK(wm.m_flammability[7] == 3);
	CHECK(wm.m_planeVisibility.size() == 4);
	CHECK(wm.m_numBitmapTiles == 8);
	REQUIRE(wm.m_textureClasses.size() == 2);
	CHECK(wm.m_textureClasses[1].name == "Rock");
	CHECK(wm.m_textureClasses[1].firstTile == 4);
	CHECK(wm.m_textureClasses[1].width == 2);
	REQUIRE(wm.m_blendedTiles.size() == 2);
	CHECK(wm.m_blendedTiles[1].blendNdx == 9);
	CHECK(wm.m_blendedTiles[1].horiz == 1);
	CHECK(wm.m_blendedTiles[1].inverted == 2);
	CHECK(wm.m_blendedTiles[1].longDiagonal == 1);
	CHECK(wm.m_blendedTiles[1].customBlendEdgeClass == -1);
	REQUIRE(wm.m_cliffInfo.size() == 2);
	CHECK(wm.m_cliffInfo[1].tileIndex == 4);
	CHECK(wm.m_cliffInfo[1].u0 == 0.125f);
	CHECK(wm.m_cliffInfo[1].u1 == 0.375f);
	CHECK(wm.m_cliffInfo[1].u2 == 0.625f);
	CHECK(wm.m_cliffInfo[1].v3 == 1.0f);
	CHECK(wm.m_cliffInfo[1].flip);
	CHECK_FALSE(wm.m_cliffInfo[1].mutant);
}

TEST_CASE("BlendTileData v11 uses 16-bit index arrays; v7 reads the cliff plane with the old (w+1)/8 row length")
{
	std::string err;
	{
		Toc t;
		const int w = 8, h = 1, n = 8;
		Bytes hm = heightMapChunk(t, w, h, 0, { { 8, 1 } }, std::vector<int>(n, 0));
		W p;
		p.i32(n);
		for (int i = 0; i < n; ++i) p.u16(i);                  // tile
		for (int i = 0; i < n; ++i) p.u16(i == 2 ? 1 : 0);     // blend (16-bit before v14)
		for (int i = 0; i < n; ++i) p.u16(0);                  // extra
		for (int i = 0; i < n; ++i) p.u16(0);                  // cliff
		p.u8(0x80);                                            // cliff state: cell 7
		p.u8(0x01);                                            // A: cell 0   (v>=10)
		p.u8(0x02);                                            // B: cell 1   (v>=11)
		p.i32(4).i32(2).i32(1).i32(1);                         // bitmap tiles, blended, cliffInfo(1 = dummy only), classes
		p.i32(0).i32(4).i32(2).i32(0).astr("Dirt");
		p.i32(0).i32(0);
		blendRecord(p, 3, 0, 1, 0, 0, 1, 0, -1);
		Bytes data = file(t, cat(hm, chunk(t, "BlendTileData", 11, p.b)));
		LoadedMap m;
		REQUIRE_MESSAGE(loadMap(data, m, &err), err);
		CHECK(m.heightMap.m_blendTileNdxes[2] == 1);
		CHECK(m.heightMap.getCliffState(7, 0));
		CHECK_FALSE(m.heightMap.getCliffState(6, 0));
		CHECK(m.heightMap.planeBit(m.heightMap.m_planeA, 0, 0));
		CHECK(m.heightMap.planeBit(m.heightMap.m_planeB, 1, 0));
		CHECK(m.heightMap.m_planeTaint.empty()); // v<14: no such plane
		CHECK(m.heightMap.m_numCliffInfo == 1);
	}
	{
		// v7: width 9 -> saved row length (9+1)/8 = 1 byte per row; ZH widens it into the 2-byte stride.
		Toc t;
		const int w = 9, h = 2, n = 18;
		Bytes hm = heightMapChunk(t, w, h, 0, { { 9, 2 } }, std::vector<int>(n, 0));
		W p;
		p.i32(n);
		for (int a = 0; a < 4; ++a)
		{
			for (int i = 0; i < n; ++i) p.u16(0); // tile, blend, extra, cliff
		}
		p.u8(0x03).u8(0x80); // two rows of one byte: row0 cells 0,1; row1 cell 7
		p.i32(1).i32(1).i32(1).i32(1);
		p.i32(0).i32(1).i32(1).i32(0).astr("X");
		p.i32(0).i32(0);
		Bytes data = file(t, cat(hm, chunk(t, "BlendTileData", 7, p.b)));
		LoadedMap m;
		REQUIRE_MESSAGE(loadMap(data, m, &err), err);
		CHECK(m.heightMap.m_planeStride == 2);
		CHECK(m.heightMap.getCliffState(0, 0));
		CHECK(m.heightMap.getCliffState(1, 0));
		CHECK_FALSE(m.heightMap.getCliffState(2, 0));
		CHECK(m.heightMap.getCliffState(7, 1));
		CHECK_FALSE(m.heightMap.getCliffState(8, 1));
	}
}

TEST_CASE("BlendTileData errors: before HeightMapData, bad sentinel, unsupported version")
{
	std::string err;
	LoadedMap m;
	{
		Toc t;
		CHECK_FALSE(loadMap(file(t, chunk(t, "BlendTileData", 18, W().i32(4).b)), m, &err));
		CHECK(err.find("before HeightMapData") != std::string::npos);
	}
	{
		Toc t;
		Bytes hm = heightMapChunk(t, 2, 1, 0, { { 2, 1 } }, { 0, 0 });
		CHECK_FALSE(loadMap(file(t, cat(hm, chunk(t, "BlendTileData", 6, W().i32(2).b))), m, &err));
		CHECK(err.find("BlendTileData version 6 not supported") != std::string::npos);
	}
	{
		Toc t;
		const int n = 2;
		Bytes hm = heightMapChunk(t, 2, 1, 0, { { 2, 1 } }, { 0, 0 });
		W q;
		q.i32(n);
		for (int i = 0; i < n; ++i) q.u16(0);
		for (int a = 0; a < 3; ++a)
			for (int i = 0; i < n; ++i) q.i32(0);
		q.u8(0); // cliff state (stride 1 x 1 row)
		q.u8(0).u8(0).u8(0).u8(0); // A, B, taint, extra pass
		q.u8(0).u8(0);             // flammability (2 cells)
		q.u8(0);                   // visibility
		q.i32(1).i32(2).i32(1).i32(1);
		q.i32(0).i32(1).i32(1).i32(0).astr("X");
		q.i32(0).i32(0);
		q.i32(0).u8(1).u8(0).u8(0).u8(0).u8(0).u8(0).i32(-1).u32(0x12345678u); // wrong sentinel
		CHECK_FALSE(loadMap(file(t, cat(hm, chunk(t, "BlendTileData", 18, q.b))), m, &err));
		CHECK(err.find("lacks the 0x7ADA0000 sentinel") != std::string::npos);
	}
}

// ------------------------------------------------------------------------------------------------
// The other chunks
// ------------------------------------------------------------------------------------------------

TEST_CASE("WorldInfo, MPPositionList, ObjectsList parse; objects carry flags, dict and the derived waypoint flag")
{
	Toc t;
	D info(t);
	info.i("weather", 1).s("mapName", "map mp test").b("isScenarioMultiplayer", true).r("cameraPitchAngle", 37.5f);
	Bytes worldInfo = chunk(t, "WorldInfo", 1, info.bytes());

	Bytes pos1 = chunk(t, "MPPositionInfo", 1, W().u8(1).u8(1).u8(1).i32(-1).i32(0).b);
	Bytes pos2 = chunk(t, "MPPositionInfo", 1, W().u8(1).u8(0).u8(0).i32(2).i32(2).astr("Gondor").astr("Mordor").b);
	Bytes mp = chunk(t, "MPPositionList", 0, cat(pos1, pos2));

	D wp(t);
	wp.i("waypointID", 5).s("waypointName", "Player_1_Start").s("originalOwner", "/team").i("objectInitialHealth", 100);
	Bytes o1 = chunk(t, "Object", 3, W().f32(100.5f).f32(-20.0f).f32(7.25f).f32(1.5f).i32(0).astr("*Waypoints/Waypoint").raw(wp.bytes()).b);
	D plain(t);
	plain.b("objectEnabled", true);
	Bytes o2 = chunk(t, "Object", 3, W().f32(0).f32(0).f32(0).f32(0).i32(FLAG_ROAD_POINT1 | FLAG_ROAD_CORNER_TIGHT).astr("RoadDirt").raw(plain.bytes()).b);
	Bytes o3 = chunk(t, "Object", 3, W().f32(1).f32(2).f32(5000).f32(0).i32(0).astr("").raw(plain.bytes()).b); // empty name, z beyond ZH's range
	Bytes objects = chunk(t, "ObjectsList", 3, cat(cat(o1, o2), o3));

	LoadedMap m;
	std::string err;
	REQUIRE_MESSAGE(loadMap(file(t, cat(cat(worldInfo, mp), objects)), m, &err), err);

	REQUIRE(m.chunks.hasWorldInfo);
	CHECK(m.chunks.worldInfo.getInt("weather") == 1);
	CHECK(m.chunks.worldInfo.getAsciiString("mapName") == "map mp test");
	CHECK(m.chunks.worldInfo.getBool("isScenarioMultiplayer"));
	CHECK(m.chunks.worldInfo.getReal("cameraPitchAngle") == 37.5f);

	REQUIRE(m.chunks.mpPositions.size() == 2);
	CHECK(m.chunks.mpPositions[0].isHuman);
	CHECK(m.chunks.mpPositions[0].isComputer);
	CHECK(m.chunks.mpPositions[0].loadAIScript);
	CHECK(m.chunks.mpPositions[0].team == -1);
	CHECK(m.chunks.mpPositions[0].sideRestriction.empty());
	CHECK_FALSE(m.chunks.mpPositions[1].isComputer);
	CHECK(m.chunks.mpPositions[1].team == 2);
	CHECK(m.chunks.mpPositions[1].sideRestriction == std::vector<std::string>{ "Gondor", "Mordor" });

	REQUIRE(m.chunks.objects.size() == 3);
	const MapObject &a = m.chunks.objects[0];
	CHECK(a.m_location.x == 100.5f);
	CHECK(a.m_location.y == -20.0f);
	CHECK(a.m_location.z == 7.25f); // v3 keeps z
	CHECK(a.m_angle == 1.5f);
	CHECK(a.m_objectName == "*Waypoints/Waypoint");
	CHECK(a.isWaypoint());
	CHECK(a.getWaypointID() == 5);
	CHECK(a.getWaypointName() == "Player_1_Start");
	CHECK(a.m_properties.getAsciiString("originalOwner") == "/team");
	const MapObject &b = m.chunks.objects[1];
	CHECK(b.getFlag(FLAG_ROAD_POINT1));
	CHECK(b.getFlag(FLAG_ROAD_CORNER_TIGHT));
	CHECK_FALSE(b.getFlag(FLAG_ROAD_POINT2));
	CHECK_FALSE(b.isWaypoint());
	CHECK(m.chunks.objects[2].m_objectName.empty());
	CHECK(m.chunks.objectsOutsideZhZRange == 1);
	REQUIRE(m.warnings.size() == 1); // the empty-template object is reported, not rejected
	CHECK(m.warnings[0].find("empty template name") != std::string::npos);
	CHECK(m.versions.at("/MPPositionList") == std::set<int>{ 0 });
	CHECK(m.versions.at("ObjectsList/Object") == std::set<int>{ 3 });
}

TEST_CASE("SidesList v6 (lead byte, build lists with z forced to 0), Teams, BuildLists (NameKey), LibraryMapLists")
{
	Toc t;
	D side(t);
	side.s("playerName", "Plyr1").b("playerIsHuman", true).u("playerDisplayName", u"One");
	W entry;
	entry.astr("Fortress").astr("GondorFortress").f32(10).f32(20).f32(30).f32(0.5f).u8(1).i32(3).astr("OnBuilt").i32(100).u8(1).u8(0).u8(1);
	W sl;
	sl.u8(1).i32(1).raw(side.bytes()).i32(1).raw(entry.b);
	Bytes sides = chunk(t, "SidesList", 6, sl.b);

	D team(t);
	team.s("teamName", "teamPlyr1").s("teamOwner", "Plyr1").b("teamIsSingleton", true);
	Bytes teams = chunk(t, "Teams", 1, W().i32(1).raw(team.bytes()).b);

	W bl;
	bl.i32(2).i32(t.key("Gondor", 3)).i32(1).raw(entry.b).i32(t.key("UNKNOWN", 3)).i32(0);
	Bytes buildLists = chunk(t, "BuildLists", 1, bl.b);

	Bytes lm1 = chunk(t, "LibraryMaps", 1, W().i32(2).astr("Libraries\\Lib_A\\Lib_A.map").astr("Libraries\\Lib_B\\Lib_B.map").b);
	Bytes lm2 = chunk(t, "LibraryMaps", 1, W().i32(0).b);
	Bytes libs = chunk(t, "LibraryMapLists", 1, cat(lm1, lm2));

	LoadedMap m;
	std::string err;
	REQUIRE_MESSAGE(loadMap(file(t, cat(cat(sides, libs), cat(teams, buildLists))), m, &err), err);
	const SidesList &s = m.sides;
	CHECK(s.sidesVersion == 6);
	CHECK(s.hasLeadByte);
	CHECK(s.leadByte == 1);
	REQUIRE(s.sides.size() == 1);
	CHECK(s.sides[0].dict.getAsciiString("playerName") == "Plyr1");
	CHECK(s.sides[0].dict.getUnicodeString("playerDisplayName") == std::u16string(u"One"));
	REQUIRE(s.sides[0].buildList.size() == 1);
	const BuildListInfo &b = s.sides[0].buildList[0];
	CHECK(b.buildingName == "Fortress");
	CHECK(b.templateName == "GondorFortress");
	CHECK(b.location.x == 10.0f);
	CHECK(b.location.y == 20.0f);
	CHECK(b.rawZ == 30.0f);
	CHECK(b.location.z == 0.0f); // ZH SidesList.cpp:270
	CHECK(b.angle == 0.5f);
	CHECK(b.initiallyBuilt);
	CHECK(b.numRebuilds == 3);
	CHECK(b.script == "OnBuilt");
	CHECK(b.health == 100);
	CHECK(b.whiner);
	CHECK_FALSE(b.unsellable);
	CHECK(b.repairable);
	REQUIRE(s.teams.size() == 1);
	CHECK(s.teams[0].getAsciiString("teamOwner") == "Plyr1");
	REQUIRE(s.factionBuildLists.size() == 2);
	CHECK(s.factionBuildLists[0].factionSide == "Gondor");
	CHECK(s.factionBuildLists[0].entries.size() == 1);
	CHECK(s.factionBuildLists[1].factionSide == "UNKNOWN");
	CHECK(s.factionBuildLists[1].entries.empty());
	REQUIRE(s.libraryMaps.size() == 2);
	CHECK(s.libraryMaps[0] == std::vector<std::string>{ "Libraries\\Lib_A\\Lib_A.map", "Libraries\\Lib_B\\Lib_B.map" });
	CHECK(s.libraryMaps[1].empty());
}

TEST_CASE("Script tree: nested groups, v4 script, or-conditions, conditions v6, actions v3 with a COORD3D parameter")
{
	Toc t;
	auto param = [](W &w, int type, int i, float r, const std::string &s) { w.i32(type).i32(i).f32(r).astr(s); };

	W cond;
	cond.i32(7).i32(t.key("UNIT_HAS_OBJECT_STATUS", 3)).i32(2);
	param(cond, 1, 5, 0.0f, "");
	cond.i32(16).f32(1).f32(2).f32(3); // COORD3D
	cond.i32(1).i32(0);                // flagA, flagB
	Bytes condChunk = chunk(t, "Condition", 6, cond.b);
	Bytes orChunk = chunk(t, "OrCondition", 1, condChunk);

	W act;
	act.i32(3).i32(t.key("SET_FLAG", 3)).i32(1);
	param(act, 8, 0, 0.0f, "MyFlag");
	act.i32(1); // tail
	Bytes actChunk = chunk(t, "ScriptAction", 3, act.b);
	W actF;
	actF.i32(4).i32(t.key("CLEAR_FLAG", 3)).i32(0).i32(0);
	Bytes actFalse = chunk(t, "ScriptActionFalse", 3, actF.b);

	W sh;
	sh.astr("Init").astr("a comment").astr("cond comment").astr("act comment");
	sh.u8(1).u8(0).u8(1).u8(1).u8(0).u8(0); // active, oneShot, easy, normal, hard, subroutine
	sh.i32(2).u8(1).u8(0).i32(4).u8(3).astr("Target").astr("ALL");
	Bytes script = chunk(t, "Script", 4, cat(sh.b, cat(cat(orChunk, actChunk), actFalse)));

	W sh2; // a v2 script: header + delayEvaluationSeconds only
	sh2.astr("Plain").astr("").astr("").astr("").u8(0).u8(1).u8(0).u8(0).u8(0).u8(1).i32(9);
	Bytes script2 = chunk(t, "Script", 2, sh2.b);

	Bytes innerGroup = chunk(t, "ScriptGroup", 3, cat(W().astr("Inner").u8(0).u8(1).b, script2));
	Bytes outerGroup = chunk(t, "ScriptGroup", 3, cat(cat(W().astr("Outer").u8(1).u8(0).b, script), innerGroup));
	Bytes list1 = chunk(t, "ScriptList", 1, cat(outerGroup, script2));
	Bytes list2 = chunk(t, "ScriptList", 1, Bytes());
	Bytes psl = chunk(t, "PlayerScriptsList", 6, cat(list1, list2));

	LoadedMap m;
	std::string err;
	REQUIRE_MESSAGE(loadMap(file(t, psl), m, &err), err);
	REQUIRE(m.hasPlayerScripts);
	CHECK(m.playerScripts.version == 6);
	REQUIRE(m.playerScripts.lists.size() == 2);
	CHECK(m.playerScripts.lists[1].items.empty());
	const ScriptList &l = m.playerScripts.lists[0];
	REQUIRE(l.items.size() == 2); // outer group, then a script, in file order
	REQUIRE(l.items[0].group);
	REQUIRE(l.items[1].script);
	CHECK(l.countScripts() == 3);
	CHECK(l.countGroups() == 2);
	const ScriptGroup &og = *l.items[0].group;
	CHECK(og.name == "Outer");
	CHECK(og.isActive);
	CHECK_FALSE(og.isSubroutine);
	REQUIRE(og.items.size() == 2);
	REQUIRE(og.items[0].script);
	const Script &s = *og.items[0].script;
	CHECK(s.name == "Init");
	CHECK(s.comment == "a comment");
	CHECK(s.actionComment == "act comment");
	CHECK(s.isActive);
	CHECK_FALSE(s.isOneShot);
	CHECK(s.easy);
	CHECK(s.normal);
	CHECK_FALSE(s.hard);
	CHECK(s.delayEvaluationSeconds == 2);
	CHECK(s.actionsFireSequentially);
	CHECK(s.loopCount == 4);
	CHECK(s.sequentialTargetType == 3);
	CHECK(s.sequentialTargetName == "Target");
	CHECK(s.unknownV4 == "ALL");
	REQUIRE(s.orConditions.size() == 1);
	REQUIRE(s.orConditions[0].conditions.size() == 1);
	const ScriptCondition &c = s.orConditions[0].conditions[0];
	CHECK(c.type == 7);
	CHECK(c.internalName == "UNIT_HAS_OBJECT_STATUS");
	CHECK(c.flagA == 1);
	CHECK(c.flagB == 0);
	REQUIRE(c.params.size() == 2);
	CHECK(c.params[0].type == 1);
	CHECK(c.params[0].intValue == 5);
	CHECK(c.params[1].type == 16);
	CHECK(c.params[1].x == 1.0f);
	CHECK(c.params[1].z == 3.0f);
	REQUIRE(s.actions.size() == 1);
	CHECK(s.actions[0].internalName == "SET_FLAG");
	CHECK(s.actions[0].params[0].stringValue == "MyFlag");
	CHECK(s.actions[0].tail == 1);
	REQUIRE(s.falseActions.size() == 1);
	CHECK(s.falseActions[0].type == 4);
	CHECK(s.falseActions[0].tail == 0);
	REQUIRE(og.items[1].group);
	CHECK(og.items[1].group->name == "Inner");
	CHECK(og.items[1].group->isSubroutine);
	REQUIRE(og.items[1].group->items.size() == 1);
	CHECK(og.items[1].group->items[0].script->name == "Plain");
	CHECK(og.items[1].group->items[0].script->delayEvaluationSeconds == 9);
	CHECK(og.items[1].group->items[0].script->unknownV4.empty());
	CHECK(l.items[1].script->isOneShot);
	CHECK(l.items[1].script->isSubroutine);
	CHECK(m.versions.at("ScriptGroup/ScriptGroup") == std::set<int>{ 3 });
	CHECK(m.versions.at("Script/ScriptActionFalse") == std::set<int>{ 3 });
}

TEST_CASE("Water, river, wave, trigger and post-effect chunks")
{
	Toc t;
	Bytes triggers = chunk(t, "TriggerAreas", 1,
		W().i32(1).astr("Area1").astr("").i32(12).i32(3).f32(0).f32(0).f32(10).f32(0).f32(10).f32(10).i32(0).b);

	W sw;
	sw.i32(1).u32(77).astr("Moat").astr("layer").f32(0.25f).u8(1).astr("bump.tga").astr("sky.tga").i32(3);
	sw.f32(0).f32(0).f32(100).f32(0).f32(100).f32(50);
	sw.i32(294).astr("Wtr_Moat.W3D").astr("LUTDepthTint.tga");
	Bytes water = chunk(t, "StandingWaterAreas", 2, sw.b);

	W rv;
	rv.i32(1).u32(5).astr("River1").astr("").f32(0.5f).u8(0);
	rv.astr("river.tga").astr("noise.tga").astr("edge.tga").astr("sparkle.tga");
	rv.u8(10).u8(20).u8(30).u8(0).f32(0.75f).i32(290).astr("Medium").i32(2);
	rv.f32(0).f32(0).f32(10).f32(0).f32(0).f32(20).f32(10).f32(20);
	Bytes rivers = chunk(t, "RiverAreas", 2, rv.b);

	W wv;
	wv.i32(1).u32(9).astr("Wave1").astr("").f32(1.0f).u8(1).i32(2).f32(1).f32(2).f32(3).f32(4).i32(0);
	for (int i = 1; i <= 9; ++i) wv.i32(i * 10);
	wv.astr("wave256.tga").i32(1);
	Bytes waves = chunk(t, "StandingWaveAreas", 2, wv.b);

	Bytes post = chunk(t, "PostEffectsChunk", 1, W().u8(1).astr("LookupTablePostEffect").f32(0.5f).astr("VWHeath_vol.tga").b);

	LoadedMap m;
	std::string err;
	REQUIRE_MESSAGE(loadMap(file(t, cat(cat(triggers, water), cat(cat(rivers, waves), post))), m, &err), err);

	REQUIRE(m.chunks.triggerAreas.size() == 1);
	CHECK(m.chunks.triggerAreas[0].name == "Area1");
	CHECK(m.chunks.triggerAreas[0].id == 12);
	REQUIRE(m.chunks.triggerAreas[0].points.size() == 3);
	CHECK(m.chunks.triggerAreas[0].points[2].x == 10.0f);
	CHECK(m.chunks.triggerAreas[0].points[2].y == 10.0f);
	CHECK(m.chunks.triggerAreas[0].tail == 0);

	REQUIRE(m.chunks.standingWaterAreas.size() == 1);
	const StandingWaterArea &a = m.chunks.standingWaterAreas[0];
	CHECK(a.uniqueId == 77);
	CHECK(a.uvScrollSpeed == 0.25f);
	CHECK(a.additiveBlending);
	CHECK(a.bumpMapTexture == "bump.tga");
	REQUIRE(a.points.size() == 3);
	CHECK(a.points[1].x == 100.0f);
	CHECK(a.waterHeight == 294);
	CHECK(a.fxShader == "Wtr_Moat.W3D");
	CHECK(a.depthColors == "LUTDepthTint.tga");

	REQUIRE(m.chunks.riverAreas.size() == 1);
	const RiverArea &r = m.chunks.riverAreas[0];
	CHECK(r.riverTexture == "river.tga");
	CHECK(r.sparkleTexture == "sparkle.tga");
	CHECK(r.r == 10);
	CHECK(r.g == 20);
	CHECK(r.b == 30);
	CHECK(r.alpha == 0.75f);
	CHECK(r.waterHeight == 290);
	CHECK(r.minimumWaterLod == "Medium");
	REQUIRE(r.lines.size() == 2);
	CHECK(r.lines[1].y1 == 20.0f);

	REQUIRE(m.chunks.standingWaveAreas.size() == 1);
	const StandingWaveArea &wa = m.chunks.standingWaveAreas[0];
	CHECK(wa.points.size() == 2);
	CHECK(wa.finalWidth == 10);
	CHECK(wa.distanceFromShore == 90);
	CHECK(wa.texture == "wave256.tga");
	CHECK(wa.enablePcaWave == 1);

	REQUIRE(m.chunks.postEffects.size() == 1);
	CHECK(m.chunks.postEffects[0].name == "LookupTablePostEffect");
	CHECK(m.chunks.postEffects[0].blendFactor == 0.5f);
	CHECK(m.chunks.postEffects[0].lookupImage == "VWHeath_vol.tga");
}

TEST_CASE("RiverAreas v3 is refused (layout unverified), PolygonTriggers v5 parse")
{
	Toc t;
	LoadedMap m;
	std::string err;
	CHECK_FALSE(loadMap(file(t, chunk(t, "RiverAreas", 3, W().i32(0).b)), m, &err));
	CHECK(err.find("RiverAreas version 3") != std::string::npos);

	Toc t2;
	W p;
	p.i32(1).astr("Pond").astr("Layer1").i32(3).u8(1).u8(1).i32(2);
	for (int i = 0; i < 6; ++i) p.astr(std::string("tex") + std::to_string(i));
	p.u8(1).u8(11).u8(22).u8(33).u8(0).f32(0.5f).f32(0.25f).f32(0.75f);
	p.i32(2).i32(1).i32(2).i32(3).i32(4).i32(5).i32(6);
	REQUIRE_MESSAGE(loadMap(file(t2, chunk(t2, "PolygonTriggers", 5, p.b)), m, &err), err);
	REQUIRE(m.chunks.polygonTriggers.size() == 1);
	const PolygonTrigger &pt = m.chunks.polygonTriggers[0];
	CHECK(pt.name == "Pond");
	CHECK(pt.layer == "Layer1");
	CHECK(pt.id == 3);
	CHECK(pt.isWaterArea);
	CHECK(pt.isRiver);
	CHECK(pt.riverStart == 2);
	CHECK(pt.riverTexture == "tex0");
	CHECK(pt.skyTexture == "tex5");
	CHECK(pt.additiveBlending);
	CHECK(pt.colorG == 22);
	CHECK(pt.uvScrollY == 0.25f);
	CHECK(pt.alpha == 0.75f);
	REQUIRE(pt.points.size() == 2);
	CHECK(pt.points[1].z == 6);
}

TEST_CASE("GlobalLighting: light order within a time of day, shadow colour after the three vectors, v8 extra")
{
	// Light value = tod*100 + lightSlot*10 + component, so each slot is identifiable from the number.
	auto build = [](int version) {
		Toc t;
		W p;
		p.i32(2); // timeOfDay
		for (int tod = 0; tod < 4; ++tod)
		{
			for (int slot = 0; slot < 9; ++slot)
			{
				for (int c = 0; c < 9; ++c)
				{
					p.f32((float)(tod * 100 + slot * 10 + c));
				}
			}
		}
		p.f32(2.0f).i32(1);
		for (int i = 0; i < 9; ++i) p.f32(0.5f);
		p.u32(0x7FA0A0A0u);
		if (version >= 8)
		{
			p.f32(1.0f).f32(0.75f).f32(0.5f);
		}
		return file(t, chunk(t, "GlobalLighting", version, p.b));
	};
	for (int version : { 7, 8 })
	{
		CAPTURE(version);
		LoadedMap m;
		std::string err;
		REQUIRE_MESSAGE(loadMap(build(version), m, &err), err);
		const GlobalLightingData &g = m.chunks.lighting;
		CHECK(g.version == version);
		CHECK(g.timeOfDay == 2);
		// file slot order: terrain[0], objects[0], objects[1], objects[2], terrain[1], terrain[2], infantry[0..2]
		const TimeOfDayLights &L1 = g.tod[1];
		CHECK(L1.terrain[0].ambient[0] == 100.0f);
		CHECK(L1.terrain[0].diffuse[1] == 104.0f);
		CHECK(L1.terrain[0].lightPos[2] == 108.0f);
		CHECK(L1.objects[0].ambient[0] == 110.0f);
		CHECK(L1.objects[2].ambient[0] == 130.0f);
		CHECK(L1.terrain[1].ambient[0] == 140.0f); // slot 4, NOT slot 1 (the OpenSAGE order is wrong)
		CHECK(L1.terrain[2].ambient[0] == 150.0f);
		CHECK(L1.infantry[0].ambient[0] == 160.0f);
		CHECK(L1.infantry[2].lightPos[2] == 188.0f);
		CHECK(g.tod[3].terrain[0].ambient[0] == 300.0f);
		CHECK(g.terrainLightingMultiplier == 2.0f);
		CHECK(g.flagDBD == 1);
		CHECK(g.vecC[2] == 0.5f);
		CHECK(g.shadowColor == 0x7FA0A0A0u);
		CHECK(g.hasV8Extra == (version == 8));
		if (version == 8)
		{
			CHECK(g.v8Extra[1] == 0.75f);
		}
	}
	// an unlisted version is refused rather than guessed
	Toc t;
	LoadedMap m;
	std::string err;
	CHECK_FALSE(loadMap(file(t, chunk(t, "GlobalLighting", 6, W().i32(1).b)), m, &err));
	CHECK(err.find("GlobalLighting version 6") != std::string::npos);
}

TEST_CASE("EnvironmentData v2/v3, NamedCameras, CameraAnimationList (free + look), WaypointsList, SkyboxSettings")
{
	Toc t;
	Bytes env3 = chunk(t, "EnvironmentData", 3, W().f32(12.5f).f32(0.375f).u8(1).astr("TSNoiseUrb.tga").astr("TSCloudMed.tga").b);
	Bytes cams = chunk(t, "NamedCameras", 2,
		W().i32(1).f32(1).f32(2).f32(3).astr("Intro").f32(10).f32(20).f32(30).f32(40).f32(50).f32(60).b);

	// the type word is stored reversed: bytes 'e','e','r','f' read as "free"
	auto rev = [](const std::string &s) { return Bytes{ (std::uint8_t)s[3], (std::uint8_t)s[2], (std::uint8_t)s[1], (std::uint8_t)s[0] }; };
	W free;
	free.raw(rev("free")).astr("Fly").u32(30).u32(5).u32(1);
	free.u32(7).raw(rev("catm")).f32(1).f32(2).f32(3).f32(0).f32(0).f32(0).f32(1).f32(55);
	W look;
	look.raw(rev("look")).astr("Orbit").u32(60).u32(0).u32(1);
	look.u32(3).raw(rev("line")).f32(4).f32(5).f32(6).f32(0.5f).f32(45);
	look.u32(1).u32(9).raw(rev("line")).f32(7).f32(8).f32(9);
	Bytes anims = chunk(t, "CameraAnimationList", 3, cat(W().i32(2).b, cat(free.b, look.b)));

	Bytes wps = chunk(t, "WaypointsList", 1, W().i32(2).i32(1).i32(2).i32(2).i32(1).b);
	Bytes sky = chunk(t, "SkyboxSettings", 1, W().f32(1).f32(2).f32(3).f32(4.5f).f32(0.25f).astr("DefaultSky").b);

	LoadedMap m;
	std::string err;
	REQUIRE_MESSAGE(loadMap(file(t, cat(cat(env3, cams), cat(cat(anims, wps), sky))), m, &err), err);
	const EnvironmentData &e = m.chunks.environment;
	CHECK(e.version == 3);
	CHECK(e.waterMaxAlphaDepth == 12.5f);
	CHECK(e.deepWaterAlpha == 0.375f);
	CHECK(e.isMacroTextureStretched);
	CHECK(e.macroTexture == "TSNoiseUrb.tga");
	CHECK(e.cloudTexture == "TSCloudMed.tga");
	REQUIRE(m.chunks.namedCameras.size() == 1);
	CHECK(m.chunks.namedCameras[0].name == "Intro");
	CHECK(m.chunks.namedCameras[0].position.z == 3.0f);
	CHECK(m.chunks.namedCameras[0].values[5] == 60.0f);
	REQUIRE(m.chunks.cameraAnimations.size() == 2);
	const CameraAnimation &fa = m.chunks.cameraAnimations[0];
	CHECK(fa.type == "free");
	CHECK(fa.name == "Fly");
	CHECK(fa.numFrames == 30);
	CHECK(fa.startOffset == 5);
	REQUIRE(fa.keys.size() == 1);
	CHECK(fa.keys[0].frame == 7);
	CHECK(fa.keys[0].interp == "catm");
	CHECK(fa.keys[0].quat[3] == 1.0f);
	CHECK(fa.keys[0].fovLike == 55.0f);
	const CameraAnimation &la = m.chunks.cameraAnimations[1];
	CHECK(la.type == "look");
	REQUIRE(la.keys.size() == 1);
	CHECK(la.keys[0].roll == 0.5f);
	CHECK(la.keys[0].fovLike == 45.0f);
	REQUIRE(la.lookAtKeys.size() == 1);
	CHECK(la.lookAtKeys[0].frame == 9);
	CHECK(la.lookAtKeys[0].interp == "line");
	CHECK(la.lookAtKeys[0].lookAt[2] == 9.0f);
	REQUIRE(m.chunks.waypointLinks.size() == 2);
	CHECK(m.chunks.waypointLinks[0].from == 1);
	CHECK(m.chunks.waypointLinks[0].to == 2);
	CHECK(m.chunks.waypointLinks[1].from == 2);
	CHECK(m.chunks.skybox.scale == 4.5f);
	CHECK(m.chunks.skybox.textureScheme == "DefaultSky");

	// v2 environment has no water-depth floats
	Toc t2;
	LoadedMap m2;
	REQUIRE_MESSAGE(loadMap(file(t2, chunk(t2, "EnvironmentData", 2, W().u8(0).astr("m.tga").astr("c.tga").b)), m2, &err), err);
	CHECK(m2.chunks.environment.macroTexture == "m.tga");
	CHECK(m2.chunks.environment.waterMaxAlphaDepth == 0.0f);
}

TEST_CASE(".scb extras: ScriptImportSize, ScriptsPlayers (with and without dicts), ScriptTeams until the end of the chunk")
{
	Toc t;
	D pd(t);
	pd.s("playerName", "Plyr").b("playerIsHuman", false);
	Bytes imp = chunk(t, "ScriptImportSize", 1, W().u32(11).u32(22).b);
	Bytes players = chunk(t, "ScriptsPlayers", 2, W().i32(1).i32(1).astr("Plyr").raw(pd.bytes()).b);
	D td1(t), td2(t);
	td1.s("teamName", "t1");
	td2.s("teamName", "t2");
	Bytes teams = chunk(t, "ScriptTeams", 1, cat(td1.bytes(), td2.bytes()));
	LoadedMap m;
	std::string err;
	REQUIRE_MESSAGE(loadMap(file(t, cat(cat(imp, players), teams)), m, &err), err);
	CHECK(m.chunks.scb.hasImportSize);
	CHECK(m.chunks.scb.importSize1 == 22);
	CHECK(m.chunks.scb.playerNames == std::vector<std::string>{ "Plyr" });
	REQUIRE(m.chunks.scb.playerDicts.size() == 1);
	CHECK(m.chunks.scb.playerDicts[0].getAsciiString("playerName") == "Plyr");
	REQUIRE(m.chunks.scb.scriptTeams.size() == 2);
	CHECK(m.chunks.scb.scriptTeams[1].getAsciiString("teamName") == "t2");

	Toc t2;
	LoadedMap m2;
	REQUIRE_MESSAGE(loadMap(file(t2, chunk(t2, "ScriptsPlayers", 2, W().i32(0).i32(2).astr("A").astr("B").b)), m2, &err), err);
	CHECK(m2.chunks.scb.playerNames == std::vector<std::string>{ "A", "B" });
	CHECK(m2.chunks.scb.playerDicts.empty());
}

// ------------------------------------------------------------------------------------------------
// Whole-file behaviour
// ------------------------------------------------------------------------------------------------

TEST_CASE("MapReader: EAR envelope end to end, strict mode refuses unknown chunks and leftover bytes, non-strict reports them")
{
	Toc t;
	Bytes good = file(t, cat(heightMapChunk(t, 2, 1, 0, { { 2, 1 } }, { 10, 20 }), chunk(t, "WorldInfo", 1, D(t).i("weather", 0).bytes())));
	LoadedMap m;
	std::string err;
	REQUIRE_MESSAGE(loadMap(earWrap(good), m, &err), err);
	CHECK(m.envelope == "EAR");
	CHECK(m.stops == (std::vector<std::string>{ "S-037", "S-038" })); // script type re-match and validateSides are reported stops
	CHECK(m.validateSidesPending);
	CHECK(m.decodedSize == good.size());
	CHECK(m.storedSize > good.size());
	CHECK(m.heightMap.getHeight(1, 0) == 20);
	CHECK(m.topLevelOrder == (std::vector<std::string>{ "HeightMapData", "WorldInfo" }));
	CHECK(m.tocEntries == t.names.size());
	CHECK(m.issues.empty());

	// an unregistered top-level chunk
	Toc t2;
	Bytes withUnknown = file(t2, cat(heightMapChunk(t2, 2, 1, 0, { { 2, 1 } }, { 1, 2 }), chunk(t2, "FogSettings", 1, W().i32(1).b)));
	CHECK_FALSE(loadMap(withUnknown, m, &err));
	CHECK(err.find("unknown chunk 'FogSettings'") != std::string::npos);
	REQUIRE_MESSAGE(loadMap(withUnknown, m, &err, false), err);
	REQUIRE(m.issues.size() == 1);
	CHECK(m.issues[0].unknownChunk);
	CHECK(m.issues[0].label == "FogSettings");

	// trailing bytes inside a known chunk
	Toc t3;
	W p;
	p.i32(2).i32(1).i32(0).i32(1).i32(2).i32(1).i32(2).u16(1).u16(2).u8(0xEE); // one stray byte
	Bytes withLeftover = file(t3, chunk(t3, "HeightMapData", 5, p.b));
	CHECK_FALSE(loadMap(withLeftover, m, &err));
	CHECK(err.find("leftover bytes in chunk 'HeightMapData'") != std::string::npos);
	REQUIRE_MESSAGE(loadMap(withLeftover, m, &err, false), err);
	for (const auto &i : m.issues)
	{
		INFO("issue label " << i.label << " parent '" << i.parentLabel << "' bytes " << i.bytes << " unknown " << i.unknownChunk);
	}
	REQUIRE(m.issues.size() == 1);
	CHECK_FALSE(m.issues[0].unknownChunk);
	CHECK(m.issues[0].bytes == 1);

	// not a chunk file at all, and a corrupt envelope
	Bytes junk(32, 0x41);
	CHECK_FALSE(loadMap(junk, m, &err));
	CHECK(err.find("no 'CkMp' table of contents") != std::string::npos);
	Bytes bad = earWrap(good);
	bad[4] = 0x7F; // declared size no longer matches the RefPack stream
	CHECK_FALSE(loadMap(bad, m, &err));
	CHECK(err.find("envelope:") != std::string::npos);
}

// ------------------------------------------------------------------------------------------------
// Hostile input (Sol review of d661596d, findings 1, 2 and 8). Every count in a map sizes an
// allocation or a recursion, so a tiny file must be refused with a MapParseError-class load error
// before anything large is allocated or the stack is spent.
// ------------------------------------------------------------------------------------------------

namespace
{
// A v18 BlendTileData payload for a 2x1 map, up to and including the three tile counts. `counts` are
// numBitmapTiles, numBlendedTiles, numCliffInfo; nothing follows them, so any declared record count is
// far larger than the payload.
Bytes tinyBlendTileData(Toc &t, int blended, int cliff)
{
	const int w = 2, h = 1, n = w * h;
	Bytes hm = heightMapChunk(t, w, h, 0, { { 2, 1 } }, { 1, 2 });
	W p;
	p.i32(n);
	for (int i = 0; i < n; ++i) p.u16(0);
	for (int i = 0; i < 3 * n; ++i) p.i32(0); // blend, extra blend, cliff
	p.raw(Bytes(1, 0));                       // cliff state plane: stride (2+7)/8 = 1, h = 1
	p.raw(Bytes(1, 0));                       // A
	p.raw(Bytes(1, 0));                       // B
	p.raw(Bytes(1, 0));                       // taint
	p.raw(Bytes(1, 0));                       // extra pass
	for (int i = 0; i < n; ++i) p.u8(0);      // flammability
	p.raw(Bytes(1, 0));                       // visibility
	p.i32(1).i32(blended).i32(cliff);
	p.i32(0);                                 // texture classes
	p.i32(0).i32(0);                          // numEdgeTiles, edge classes
	return cat(hm, chunk(t, "BlendTileData", 18, p.b));
}
} // namespace

TEST_CASE("BlendTileData: counts larger than the payload are refused before any allocation (INT_MAX blended tiles / cliff infos)")
{
	LoadedMap m;
	std::string err;
	{
		Toc t;
		// 2^31-1 blended tile records would be ~2^31 * sizeof(TBlendTileInfo) bytes of vector storage
		CHECK_FALSE(loadMap(file(t, tinyBlendTileData(t, 0x7FFFFFFF, 1)), m, &err));
		CHECK_MESSAGE(err.find("blended tile records needs 38654705628 bytes but the chunk has 0 left") != std::string::npos, err);
	}
	{
		Toc t;
		CHECK_FALSE(loadMap(file(t, tinyBlendTileData(t, 1, 0x7FFFFFFF)), m, &err));
		CHECK_MESSAGE(err.find("cliff info records needs 81604378548 bytes but the chunk has 0 left") != std::string::npos, err);
	}
	{
		// a count that fits the payload still has to be the right size: 2 blended tiles need 18 bytes
		Toc t;
		CHECK_FALSE(loadMap(file(t, tinyBlendTileData(t, 2, 1)), m, &err));
		CHECK_MESSAGE(err.find("blended tile records needs 18 bytes but the chunk has 0 left") != std::string::npos, err);
	}
}

TEST_CASE("HeightMapData: a truncated 16384 x 16384 map is refused before the 512 MiB height array is allocated")
{
	Toc t;
	W p;
	const std::int32_t cells = 16384 * 16384; // 268435456
	p.i32(16384).i32(16384).i32(0).i32(1).i32(16384).i32(16384).i32(cells);
	p.u16(1).u16(2); // 4 bytes of the 536870912 the chunk claims
	LoadedMap m;
	std::string err;
	CHECK_FALSE(loadMap(file(t, chunk(t, "HeightMapData", 5, p.b)), m, &err));
	CHECK_MESSAGE(err.find("height array needs 536870912 bytes but the chunk has 4 left") != std::string::npos, err);
}

TEST_CASE("BlendTileData: index arrays and bit planes are checked against the chunk before they are sized")
{
	// the height chunk is small but legal; the blend payload stops after the tile index array, so the
	// first index array (2 cells * 2 bytes) is larger than what is left
	Toc t2;
	Bytes hm = heightMapChunk(t2, 2, 1, 0, { { 2, 1 } }, { 1, 2 });
	W p;
	p.i32(2).u16(0).u16(0); // dataSize 2, tiles; the three i16 index arrays are missing
	LoadedMap m;
	std::string err;
	CHECK_FALSE(loadMap(file(t2, cat(hm, chunk(t2, "BlendTileData", 11, p.b))), m, &err));
	CHECK_MESSAGE(err.find("index array needs 4 bytes but the chunk has 0 left") != std::string::npos, err);
}

TEST_CASE("Chunk nesting deeper than DataChunkInput::MAX_CHUNK_NESTING is a loud error, not a stack overflow")
{
	Toc t;
	Bytes nest;
	const int depth = 6000; // each level is a ScriptGroup parsed by recursion into DataChunkInput::parse
	for (int i = 0; i < depth; ++i)
	{
		nest = chunk(t, "ScriptGroup", 3, cat(W().astr("g").u8(1).u8(0).b, nest));
	}
	Bytes data = file(t, chunk(t, "PlayerScriptsList", 6, chunk(t, "ScriptList", 1, nest)));
	LoadedMap m;
	std::string err;
	CHECK_FALSE(loadMap(data, m, &err));
	CHECK_MESSAGE(err.find("chunks nested deeper than 32 levels") != std::string::npos, err);

	// the retail corpus nests no deeper than a few levels, so a normal tree is unaffected: depth 20
	Toc t2;
	Bytes ok;
	for (int i = 0; i < 20; ++i)
	{
		ok = chunk(t2, "ScriptGroup", 3, cat(W().astr("g").u8(1).u8(0).b, ok));
	}
	REQUIRE_MESSAGE(loadMap(file(t2, chunk(t2, "PlayerScriptsList", 6, chunk(t2, "ScriptList", 1, ok))), m, &err), err);
	CHECK(m.playerScripts.lists[0].countGroups() == 20);
}

TEST_CASE("EAR envelope: bytes after the RefPack terminator are refused (strict), the stream itself must end the file")
{
	Toc t;
	Bytes good = file(t, heightMapChunk(t, 2, 1, 0, { { 2, 1 } }, { 10, 20 }));
	LoadedMap m;
	std::string err;
	REQUIRE_MESSAGE(loadMap(earWrap(good), m, &err), err);
	Bytes junk = earWrap(good);
	junk.insert(junk.end(), { 'J', 'U', 'N', 'K' });
	CHECK_FALSE(loadMap(junk, m, &err));
	CHECK_MESSAGE(err.find("4 trailing byte(s) after the terminator") != std::string::npos, err);
}
