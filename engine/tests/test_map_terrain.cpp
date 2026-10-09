// OpenBFME unit tests: terrain textures, tile/blend/cliff UVs, ground height and the terrain mesh arrays,
// on hand-built maps. GPL-3.0. Expected values are computed by hand from the cited formulas (ZH/BFME1).

#include "doctest.h"

#include "GameClient/TGAFile.h"
#include "GameClient/TerrainAtlas.h"
#include "GameClient/TerrainTypes.h"
#include "GameEngineDevice/W3DDevice/GameClient/HeightMapMesh.h"
#include "GameEngineDevice/W3DDevice/GameClient/TerrainComposite.h"
#include "GameEngineDevice/W3DDevice/GameClient/WorldHeightMap.h"
#include "GameLogic/Map/TerrainLogic.h"

#include <cmath>
#include <string>
#include <vector>

namespace
{

typedef std::vector<std::uint8_t> Bytes;

Bytes makeTga(int type, int depth, int w, int h, std::uint8_t flags, const Bytes &pixels, int idLength = 0)
{
	Bytes b(18, 0);
	b[0] = (std::uint8_t)idLength;
	b[2] = (std::uint8_t)type;
	b[12] = (std::uint8_t)(w & 255);
	b[13] = (std::uint8_t)(w >> 8);
	b[14] = (std::uint8_t)(h & 255);
	b[15] = (std::uint8_t)(h >> 8);
	b[16] = (std::uint8_t)depth;
	b[17] = flags;
	b.insert(b.end(), (size_t)idLength, 0xAB);
	b.insert(b.end(), pixels.begin(), pixels.end());
	return b;
}

// A w*h map with every tile/blend/cliff array allocated and zeroed.
WorldHeightMap makeMap(int w, int h, int border = 0)
{
	WorldHeightMap m;
	m.m_width = w;
	m.m_height = h;
	m.m_borderSize = border;
	m.m_dataSize = w * h;
	m.m_data.assign((size_t)w * h, 0);
	m.m_hasBlendTileData = true;
	m.m_tileNdxes.assign((size_t)w * h, 0);
	m.m_blendTileNdxes.assign((size_t)w * h, 0);
	m.m_extraBlendTileNdxes.assign((size_t)w * h, 0);
	m.m_cliffInfoNdxes.assign((size_t)w * h, 0);
	m.m_planeStride = (w + 7) / 8;
	m.m_blendedTiles.assign(1, TBlendTileInfo());
	m.m_cliffInfo.assign(1, TCliffInfo());
	m.m_numBlendedTiles = 1;
	m.m_numCliffInfo = 1;
	return m;
}

TXTextureClass cls(const char *name, int first, int numTiles, int width)
{
	TXTextureClass c;
	c.name = name;
	c.firstTile = first;
	c.numTiles = numTiles;
	c.width = width;
	return c;
}

} // namespace

// ------------------------------------------------------------------------------------------------
// TGA and terrain types
// ------------------------------------------------------------------------------------------------

TEST_CASE("TGA: 24-bit BGR becomes RGBA in storage row order; the top-down flag is reported but rows are NOT flipped")
{
	// 2x2, rows in storage order: (B,G,R) = row0: red-ish then green-ish; row1: blue-ish then white
	Bytes px = { 0x00, 0x00, 0xFF,  0x00, 0xFF, 0x00,   0xFF, 0x00, 0x00,  0xFF, 0xFF, 0xFF };
	TGAImage img;
	std::string err;
	REQUIRE_MESSAGE(TGAFile::decode(makeTga(2, 24, 2, 2, 0x00, px).data(), 18 + px.size(), img, &err), err);
	CHECK(img.width == 2);
	CHECK(img.height == 2);
	CHECK_FALSE(img.topDownFlag);
	REQUIRE(img.rgba.size() == 16);
	CHECK(img.rgba[0] == 255); CHECK(img.rgba[1] == 0); CHECK(img.rgba[2] == 0); CHECK(img.rgba[3] == 255);   // pixel (0,0) red
	CHECK(img.rgba[4] == 0); CHECK(img.rgba[5] == 255);                                                       // (1,0) green
	CHECK(img.rgba[8] == 0); CHECK(img.rgba[10] == 255);                                                      // (0,1) blue

	Bytes top = makeTga(2, 24, 2, 2, 0x20, px);
	REQUIRE(TGAFile::decode(top.data(), top.size(), img, &err));
	CHECK(img.topDownFlag);
	CHECK(img.rgba[0] == 255); // same data: retail ignores the origin flag, rows stay in storage order

	// 32-bit keeps the stored alpha; an id field is skipped
	Bytes px32 = { 1, 2, 3, 77 };
	Bytes t32 = makeTga(2, 32, 1, 1, 0x08, px32, 5);
	REQUIRE(TGAFile::decode(t32.data(), t32.size(), img, &err));
	CHECK(img.rgba == (Bytes{ 3, 2, 1, 77 }));
}

TEST_CASE("TGA: RLE, colour-mapped, other depths, oversize and truncated files are errors")
{
	TGAImage img;
	std::string err;
	Bytes px(12, 0);
	Bytes rle = makeTga(10, 24, 2, 2, 0, px);
	CHECK_FALSE(TGAFile::decode(rle.data(), rle.size(), img, &err));
	CHECK(err.find("RLE is rejected") != std::string::npos);
	Bytes cm = makeTga(2, 24, 2, 2, 0, px);
	cm[1] = 1;
	CHECK_FALSE(TGAFile::decode(cm.data(), cm.size(), img, &err));
	CHECK(err.find("colour-mapped") != std::string::npos);
	Bytes d16 = makeTga(2, 16, 2, 2, 0, px);
	CHECK_FALSE(TGAFile::decode(d16.data(), d16.size(), img, &err));
	CHECK(err.find("pixel depth 16") != std::string::npos);
	Bytes big = makeTga(2, 24, 1025, 1, 0, Bytes(1025 * 3, 0));
	CHECK_FALSE(TGAFile::decode(big.data(), big.size(), img, &err));
	CHECK(err.find("out of range") != std::string::npos);
	Bytes trunc = makeTga(2, 24, 2, 2, 0, Bytes(11, 0));
	CHECK_FALSE(TGAFile::decode(trunc.data(), trunc.size(), img, &err));
	CHECK(err.find("truncated") != std::string::npos);
	CHECK_FALSE(TGAFile::decode(trunc.data(), 10, img, &err));
	CHECK(err.find("18-byte header") != std::string::npos);
}

TEST_CASE("TerrainTypes: Terrain blocks give name -> texture, case-insensitive; _nrm and archive path names")
{
	const char *ini =
		"; comment\n"
		"Terrain AsphaltType1\n"
		"  Texture = TXAsph01a.tga\n"
		"  Class = Type Misc NEXT Region Edoras\n"
		"End\n"
		"\n"
		"Terrain GrassA ; trailing\n"
		"  Texture = Grass A.tga ; spaces inside kept\n"
		"End\n"
		"Terrain NoTexture\n"
		"  Class = Type Misc\n"
		"End\n"
		"terrain grassa\n"
		"  Texture = Grass B.tga\n"
		"end\n";
	TerrainTypeIndex idx;
	TerrainTypes::scanText(ini, idx);
	REQUIRE(idx.findTexture("asphalttype1") != nullptr);
	CHECK(*idx.findTexture("ASPHALTTYPE1") == "TXAsph01a.tga");
	CHECK(idx.findTexture("NoTexture") == nullptr); // no Texture key: no entry
	CHECK(idx.findTexture("Missing") == nullptr);
	CHECK(*idx.findTexture("GrassA") == "Grass B.tga"); // the later block wins
	CHECK(idx.duplicates == 1);
	CHECK(idx.blocksScanned == 4);
	CHECK(TerrainTypeIndex::normalMapName("TXAsph01a.tga") == "TXAsph01a_nrm.tga");
	CHECK(TerrainTypeIndex::normalMapName("a.b.TGA") == "a.b_nrm.TGA");
	CHECK(TerrainTypeIndex::normalMapName("noext") == "noext_nrm");
	CHECK(TerrainTypeIndex::textureArchivePath("TXAsph01a.tga") == "art\\terrain\\txasph01a.tga");
}

// ------------------------------------------------------------------------------------------------
// Atlas packing and tile UVs
// ------------------------------------------------------------------------------------------------

TEST_CASE("TerrainAtlas: countTiles takes the largest square that fits, capped at 16x16 (BFME2)")
{
	CHECK(TerrainAtlas::countTiles(256, 256) == 16);
	CHECK(TerrainAtlas::countTiles(256, 128) == 4);   // min(4,2) = 2 -> 2x2
	CHECK(TerrainAtlas::countTiles(63, 64) == 0);
	CHECK(TerrainAtlas::countTiles(64, 64) == 1);
	CHECK(TerrainAtlas::countTiles(1024, 1024) == 256);
	CHECK(TerrainAtlas::countTiles(1088, 1088) == 0); // 17 tiles wide: refused
	CHECK(TerrainAtlas::countTiles(1024, 64) == 1);
}

TEST_CASE("TerrainAtlas::packTiles places classes widest first at (4 + col*72, 4 + row*72); tile rows are inverted")
{
	WorldHeightMap m = makeMap(2, 2);
	m.m_numBitmapTiles = 36;
	m.m_textureClasses = { cls("A", 0, 16, 4), cls("B", 16, 16, 4), cls("C", 32, 4, 2) };
	std::vector<bool> present(36, true);
	int usedHeight = TerrainAtlas::packTiles(m, present);
	CHECK(m.m_textureClasses[0].positionInTexture.x == 4);
	CHECK(m.m_textureClasses[0].positionInTexture.y == 4);
	CHECK(m.m_textureClasses[1].positionInTexture.x == 4 + 4 * 72);  // next free cell is column 4
	CHECK(m.m_textureClasses[1].positionInTexture.y == 4);
	CHECK(m.m_textureClasses[2].positionInTexture.x == 4 + 8 * 72);
	CHECK(m.m_textureClasses[2].positionInTexture.y == 4);
	CHECK(usedHeight == 4 + 4 * 64 + 4); // yOrigin + width*64 + TILE_OFFSET/2
	// tile (i=1, j=0) of class A: x = 4 + 64, y = yOrigin + (width-j-1)*64 = 4 + 3*64
	REQUIRE(m.m_tileLocations.size() == 36);
	CHECK(m.m_tileLocations[1].present);
	CHECK(m.m_tileLocations[1].x == 68);
	CHECK(m.m_tileLocations[1].y == 196);
	// tile (i=0, j=3): index 3*4 = 12 -> y = 4 + (4-3-1)*64 = 4
	CHECK(m.m_tileLocations[12].x == 4);
	CHECK(m.m_tileLocations[12].y == 4);

	// a missing tile is not placed
	present[1] = false;
	TerrainAtlas::packTiles(m, present);
	CHECK_FALSE(m.m_tileLocations[1].present);
	CHECK(m.m_tileLocations[0].present);
}

TEST_CASE("TerrainAtlas::packTiles grows the atlas downward when a map needs more than ZH's 28x28 = 784 tiles (S-030)")
{
	// BFME2/RotWK maps reach 1804 source tiles; the retail ZH grid stops at 784. 60 classes of 4x4 tiles = 960 tiles.
	WorldHeightMap m = makeMap(2, 2);
	m.m_numBitmapTiles = 960;
	for (int i = 0; i < 60; ++i)
	{
		m.m_textureClasses.push_back(cls("C", i * 16, 16, 4));
	}
	std::vector<bool> present(960, true);
	int used = TerrainAtlas::packTiles(m, present);
	// 7 classes (28 columns / 4) per class row; class i sits at column (i%7)*4, row (i/7)*4
	for (int i = 0; i < 60; ++i)
	{
		CAPTURE(i);
		CHECK(m.m_textureClasses[(size_t)i].positionInTexture.x == 4 + (i % 7) * 4 * 72);
		CHECK(m.m_textureClasses[(size_t)i].positionInTexture.y == 4 + (i / 7) * 4 * 72);
	}
	CHECK(used == 4 + 8 * 288 + 256 + 4); // the last class (i = 59) starts in class row 8
	CHECK(used > 2048);

	// more tiles than 8192 px / 72 rows can hold: the surplus classes are not placed (reported by the caller)
	WorldHeightMap big = makeMap(2, 2);
	big.m_numBitmapTiles = 16 * 220;
	for (int i = 0; i < 220; ++i)
	{
		big.m_textureClasses.push_back(cls("C", i * 16, 16, 4));
	}
	TerrainAtlas::packTiles(big, std::vector<bool>(16 * 220, true));
	int unplaced = 0;
	for (const TXTextureClass &c : big.m_textureClasses) unplaced += c.positionInTexture.x == 0;
	CHECK(unplaced > 0);
	CHECK(big.m_textureClasses[0].positionInTexture.x == 4);
}

TEST_CASE("getUVForNdx: quadrant of the 64-px tile by tileNdx&1 (right half) and tileNdx&2 (upper half); missing tile gives zeros")
{
	WorldHeightMap m = makeMap(2, 2);
	m.m_terrainTexHeight = 512;
	m.m_tileLocations.assign(4, TileLocation());
	m.m_tileLocations[1] = { 68, 196, true };
	float nU, nV, xU, xV;

	m.getUVForNdx(1 << 2 | 0, &nU, &nV, &xU, &xV); // lower-left quadrant (as displayed)
	CHECK(nU == 0.033203125f);   // 68/2048
	CHECK(xU == 0.048828125f);   // mid = 100/2048
	CHECK(nV == 0.4453125f);     // mid = 228/512
	CHECK(xV == 0.5078125f);     // 260/512
	m.getUVForNdx(1 << 2 | 3, &nU, &nV, &xU, &xV); // upper-right
	CHECK(nU == 0.048828125f);
	CHECK(xU == 0.064453125f);   // 132/2048
	CHECK(nV == 0.3828125f);     // 196/512
	CHECK(xV == 0.4453125f);
	m.getUVForNdx(1 << 2 | 1, &nU, &nV, &xU, &xV, true); // full tile
	CHECK(nU == 0.033203125f);
	CHECK(xU == 0.064453125f);
	CHECK(nV == 0.3828125f);
	CHECK(xV == 0.5078125f);

	m.getUVForNdx(2 << 2, &nU, &nV, &xU, &xV); // tile 2 has no pixels
	CHECK(nU == 0.0f); CHECK(nV == 0.0f); CHECK(xU == 0.0f); CHECK(xV == 0.0f);

	// getUVData lays the corners SW, SE, NE, NW as (nU,xV), (xU,xV), (xU,nV), (nU,nV)
	m.m_tileNdxes[0] = (std::int16_t)(1 << 2);
	float U[4], V[4];
	m.getUVData(0, 0, U, V, TerrainUvOptions());
	CHECK(U[0] == 0.033203125f); CHECK(V[0] == 0.5078125f);
	CHECK(U[1] == 0.048828125f); CHECK(V[1] == 0.5078125f);
	CHECK(U[2] == 0.048828125f); CHECK(V[2] == 0.4453125f);
	CHECK(U[3] == 0.033203125f); CHECK(V[3] == 0.4453125f);
}

// ------------------------------------------------------------------------------------------------
// Blend alpha table and flips
// ------------------------------------------------------------------------------------------------

namespace
{
struct BlendCase
{
	const char *name;
	int h, v, rd, ld; // direction (exactly one set)
	int inverted;     // bit0 I, bit1 F
	int longDiag;
	std::uint8_t alpha[4];
	bool flip;
};
} // namespace

TEST_CASE("getAlphaUVData: the corner alpha / flip table for every direction x inverted x long (spec 2.3, ZH :2066-2158)")
{
	// vertex order SW=0 SE=1 NE=2 NW=3. F = inverted&2 flips horizontals/verticals only.
	const BlendCase cases[] = {
		{ "horiz",        1, 0, 0, 0, 0, 0, { 0, 255, 255, 0 }, false },
		{ "horiz inv",    1, 0, 0, 0, 1, 0, { 255, 0, 0, 255 }, false },
		{ "horiz F",      1, 0, 0, 0, 2, 0, { 0, 255, 255, 0 }, true },
		{ "horiz inv F",  1, 0, 0, 0, 3, 0, { 255, 0, 0, 255 }, true },
		{ "vert",         0, 1, 0, 0, 0, 0, { 0, 0, 255, 255 }, false },
		{ "vert inv",     0, 1, 0, 0, 1, 0, { 255, 255, 0, 0 }, false },
		{ "vert F",       0, 1, 0, 0, 2, 0, { 0, 0, 255, 255 }, true },
		{ "right",        0, 0, 1, 0, 0, 0, { 0, 0, 255, 0 }, true },
		{ "right long",   0, 0, 1, 0, 0, 1, { 0, 255, 255, 255 }, true },
		{ "right inv",    0, 0, 1, 0, 1, 0, { 0, 255, 0, 0 }, false },
		{ "right inv lg", 0, 0, 1, 0, 1, 1, { 255, 255, 255, 0 }, false },
		{ "left",         0, 0, 0, 1, 0, 0, { 0, 0, 0, 255 }, false },
		{ "left long",    0, 0, 0, 1, 0, 1, { 255, 0, 255, 255 }, false },
		{ "left inv",     0, 0, 0, 1, 1, 0, { 255, 0, 0, 0 }, true },
		{ "left inv lg",  0, 0, 0, 1, 1, 1, { 255, 255, 0, 255 }, true },
		{ "none (base tile)", 0, 0, 0, 0, 0, 0, { 0, 0, 0, 0 }, false },
	};
	for (const BlendCase &c : cases)
	{
		INFO("case " << c.name);
		WorldHeightMap m = makeMap(2, 2);
		m.m_terrainTexHeight = 512;
		m.m_tileLocations.assign(4, TileLocation());
		m.m_blendTileNdxes[0] = 1;
		m.m_blendedTiles.resize(2);
		m.m_blendedTiles[1].horiz = (std::uint8_t)c.h;
		m.m_blendedTiles[1].vert = (std::uint8_t)c.v;
		m.m_blendedTiles[1].rightDiagonal = (std::uint8_t)c.rd;
		m.m_blendedTiles[1].leftDiagonal = (std::uint8_t)c.ld;
		m.m_blendedTiles[1].inverted = (std::uint8_t)c.inverted;
		m.m_blendedTiles[1].longDiagonal = (std::uint8_t)c.longDiag;
		float U[4], V[4];
		std::uint8_t a[4];
		bool flip = !c.flip;
		m.getAlphaUVData(0, 0, U, V, a, &flip, TerrainUvOptions());
		for (int k = 0; k < 4; ++k)
		{
			CHECK_MESSAGE(a[k] == c.alpha[k], "vertex " << k);
		}
		CHECK(flip == c.flip);

		// the extra (3-way) layer uses the same table
		m.m_extraBlendTileNdxes[0] = 1;
		std::uint8_t ae[4];
		bool needFlip = !c.flip, cliff = true;
		REQUIRE(m.getExtraAlphaUVData(0, 0, U, V, ae, &needFlip, &cliff, TerrainUvOptions()));
		for (int k = 0; k < 4; ++k)
		{
			CHECK_MESSAGE(ae[k] == c.alpha[k], "extra vertex " << k);
		}
		CHECK(needFlip == c.flip);
		CHECK_FALSE(cliff);
	}

	// a custom blend edge class cancels alpha and flip
	WorldHeightMap m = makeMap(2, 2);
	m.m_tileLocations.assign(4, TileLocation());
	m.m_blendTileNdxes[0] = 1;
	m.m_blendedTiles.resize(2);
	m.m_blendedTiles[1].rightDiagonal = 1;
	m.m_blendedTiles[1].customBlendEdgeClass = 3;
	float U[4], V[4];
	std::uint8_t a[4];
	bool flip = true;
	m.getAlphaUVData(0, 0, U, V, a, &flip, TerrainUvOptions());
	CHECK(a[0] == 0); CHECK(a[1] == 0); CHECK(a[2] == 0); CHECK(a[3] == 0);
	CHECK_FALSE(flip);

	// no extra layer on the cell
	CHECK_FALSE(m.getExtraAlphaUVData(1, 0, U, V, a, &flip, &flip, TerrainUvOptions()));
}

TEST_CASE("cliff UVs: donor atlas formula, the 256-px hypothesis with wrap, class matching and the height-forced flip")
{
	WorldHeightMap m = makeMap(2, 2);
	m.m_terrainTexHeight = 512;
	m.m_textureClasses = { cls("Cliff", 0, 16, 4), cls("Other", 16, 4, 2) };
	m.m_textureClasses[0].positionInTexture = { 4, 4 };
	m.m_tileLocations.assign(20, TileLocation());
	m.m_tileLocations[1] = { 68, 196, true };
	m.m_tileLocations[17] = { 600, 4, true };
	m.m_numCliffInfo = 2;
	m.m_cliffInfo.resize(2);
	TCliffInfo &ci = m.m_cliffInfo[1];
	ci.tileIndex = 5 << 2; // tile 5: inside class "Cliff"
	ci.u0 = 0.125f; ci.v0 = -0.125f; ci.u1 = 0.25f; ci.v1 = -0.125f; ci.u2 = 0.25f; ci.v2 = -0.25f; ci.u3 = 0.125f; ci.v3 = -0.25f;
	ci.flip = true;
	m.m_tileNdxes[0] = (std::int16_t)(1 << 2); // base tile 1: inside class "Cliff"
	m.m_cliffInfoNdxes[0] = 1;
	CHECK(m.cliffUvApplies(0, 1 << 2));
	CHECK_FALSE(m.cliffUvApplies(0, 17 << 2)); // a tile of another class: no override
	CHECK_FALSE(m.cliffUvApplies(1, 1 << 2));  // cell without cliff info

	float U[4], V[4];
	// donor/BFME1 formula: U = u + posX/2048; V = v * (2048/texHeight, INTEGER division = 4) + (posY + width*64)/texHeight
	TerrainUvOptions donor;
	donor.cliffUnit = CLIFF_UV_ATLAS_2048;
	bool flip = m.getUVForTileIndex(0, 1 << 2, U, V, donor);
	CHECK(flip); // returns the cliff entry's flip flag
	const float minU = 4.0f / 2048.0f, maxV = 260.0f / 512.0f;
	CHECK(U[0] == 0.125f + minU);
	CHECK(U[1] == 0.25f + minU);
	CHECK(V[0] == -0.125f * 4.0f + maxV);
	CHECK(V[2] == -0.25f * 4.0f + maxV);

	// hypothesis: u,v in 256-px units from the class' left/bottom edge; v negative goes up. classPx = 256.
	TerrainUvOptions hyp;
	hyp.cliffUnit = CLIFF_UV_BFME2_HYPOTHESIS_256PX_WRAP;
	flip = m.getUVForTileIndex(0, 1 << 2, U, V, hyp);
	CHECK(flip);
	// u0=0.125 -> 32 px -> (4+32)/2048; v0=-0.125 -> 256-32 = 224 px below the class top -> (4+224)/512
	CHECK(U[0] == 36.0f / 2048.0f);
	CHECK(V[0] == 228.0f / 512.0f);
	CHECK(U[1] == (4.0f + 64.0f) / 2048.0f);
	CHECK(V[2] == (4.0f + 192.0f) / 512.0f);
	// a cell that runs past the class wraps as a unit: u = 1.25 -> 320 px, offset floor(320/256)*256 = 256
	ci.u0 = 1.25f; ci.u1 = 1.375f; ci.u2 = 1.375f; ci.u3 = 1.25f;
	m.getUVForTileIndex(0, 1 << 2, U, V, hyp);
	CHECK(U[0] == (4.0f + 320.0f - 256.0f) / 2048.0f);
	CHECK(U[1] == (4.0f + 352.0f - 256.0f) / 2048.0f);

	// AdjustCliffTextures = No disables the override: plain tile UVs, no flip
	TerrainUvOptions off;
	off.adjustCliffTextures = false;
	flip = m.getUVForTileIndex(0, 1 << 2, U, V, off);
	CHECK_FALSE(flip);
	CHECK(U[0] == 68.0f / 2048.0f);

	// the cell's triangle flip: a matched cliff with flip set takes |p0-p2| > |p1-p3|
	m.m_cliffInfoNdxes[0] = 1;
	m.m_data = { 100, 0, 0, 300 }; // p0 = (0,0)=100, p1 = (1,0)=0, p3 = (0,1)=0, p2 = (1,1)=300 -> |p0-p2| = 200 > |p1-p3| = 0
	bool needFlip = false;
	std::uint8_t a[4];
	m.getAlphaUVData(0, 0, U, V, a, &needFlip, donor);
	CHECK(needFlip);
	m.m_data = { 100, 300, 300, 100 }; // |p0-p2| = 0, |p1-p3| = 0 -> no flip
	m.getAlphaUVData(0, 0, U, V, a, &needFlip, donor);
	CHECK_FALSE(needFlip);
}

TEST_CASE("cliff UVs: a cell that straddles its class edge is wrapped per pixel inside the class block, never read from a neighbour (Sol review)")
{
	// class "Cliff": 4 tiles wide = 256 px, block top-left (4,4) in a 2048 x 512 atlas; "Other" sits right of it
	WorldHeightMap m = makeMap(2, 2);
	m.m_terrainTexHeight = 512;
	m.m_textureClasses = { cls("Cliff", 0, 16, 4), cls("Other", 16, 4, 2) };
	m.m_textureClasses[0].positionInTexture = { 4, 4 };
	m.m_tileLocations.assign(20, TileLocation());
	m.m_tileLocations[1] = { 68, 196, true };
	m.m_numCliffInfo = 2;
	m.m_cliffInfo.resize(2);
	TCliffInfo &ci = m.m_cliffInfo[1];
	ci.tileIndex = 5 << 2;
	// u from 0.9 to 1.1 (230.4 px .. 281.6 px): the cell crosses the class's right edge at 256 px
	ci.u0 = 0.9f; ci.u1 = 1.1f; ci.u2 = 1.1f; ci.u3 = 0.9f;
	ci.v0 = -0.5f; ci.v1 = -0.5f; ci.v2 = -0.25f; ci.v3 = -0.25f;
	m.m_tileNdxes[0] = (std::int16_t)(1 << 2);
	m.m_cliffInfoNdxes[0] = 1;

	float U[4], V[4];
	ClassWrapRect rect;
	TerrainUvOptions hyp;
	hyp.cliffUnit = CLIFF_UV_BFME2_HYPOTHESIS_256PX_WRAP;
	m.getUVData(0, 0, U, V, hyp, &rect);
	REQUIRE(rect.valid());
	// the class block as fractions of the atlas: x 4/2048, y 4/512, 256 px square
	CHECK(rect.x0 == 4.0f / 2048.0f);
	CHECK(rect.y0 == 4.0f / 512.0f);
	CHECK(rect.w == 256.0f / 2048.0f);
	CHECK(rect.h == 256.0f / 512.0f);
	// corners are the UNWRAPPED positions: u1/u2 lie beyond the right edge of the block
	CHECK(U[0] == (4.0f + 0.9f * 256.0f) / 2048.0f);
	CHECK(U[1] == (4.0f + 1.1f * 256.0f) / 2048.0f);
	CHECK(U[1] > rect.x0 + rect.w);

	// without the wrap, sampling across the cell would read the 25.6 px of the NEXT atlas block ("Other"); with it,
	// every sample along the cell stays inside the class block
	bool leakedWithoutWrap = false;
	for (int i = 0; i <= 16; ++i)
	{
		const float t = (float)i / 16.0f;
		const float u = U[0] + t * (U[1] - U[0]);
		const float v = V[0] + t * (V[3] - V[0]);
		leakedWithoutWrap = leakedWithoutWrap || u >= rect.x0 + rect.w;
		float wu, wv;
		TerrainUv::wrapIntoClass(rect, u, v, wu, wv);
		CHECK(wu >= rect.x0);
		CHECK(wu < rect.x0 + rect.w);
		CHECK(wv >= rect.y0);
		CHECK(wv < rect.y0 + rect.h);
	}
	CHECK(leakedWithoutWrap);
	// the straddling corner wraps to the far side: 281.6 px -> 25.6 px past the block's left edge
	float wu, wv;
	TerrainUv::wrapIntoClass(rect, U[1], V[1], wu, wv);
	CHECK(wu == doctest::Approx((4.0f + 25.6f) / 2048.0f).epsilon(1e-5));
	// negative offsets wrap too (GLSL mod semantics)
	TerrainUv::wrapIntoClass(rect, rect.x0 - 0.25f * rect.w, rect.y0 + 0.5f * rect.h, wu, wv);
	CHECK(wu == doctest::Approx(rect.x0 + 0.75f * rect.w).epsilon(1e-5));
	CHECK(wv == doctest::Approx(rect.y0 + 0.5f * rect.h).epsilon(1e-5));

	// the donor unit has no wrap (the atlas is read as the donor does), and a cell without cliff info has none
	TerrainUvOptions donor;
	donor.cliffUnit = CLIFF_UV_ATLAS_2048;
	m.getUVData(0, 0, U, V, donor, &rect);
	CHECK_FALSE(rect.valid());
	m.m_cliffInfoNdxes[0] = 0;
	m.getUVData(0, 0, U, V, hyp, &rect);
	CHECK_FALSE(rect.valid());
	float wu2, wv2;
	TerrainUv::wrapIntoClass(rect, 0.7f, 0.3f, wu2, wv2); // no rect: the UV is returned unchanged
	CHECK(wu2 == 0.7f);
	CHECK(wv2 == 0.3f);

	// the mesh carries the rect on all four vertices of the cell and counts the straddling cell
	m.m_cliffInfoNdxes[0] = 1;
	HeightMapMeshOptions mo;
	mo.uv = hyp;
	std::vector<TerrainChunk> chunks;
	TerrainMeshStats stats;
	std::string err;
	REQUIRE_MESSAGE(HeightMapMesh::build(m, nullptr, mo, chunks, stats, &err), err);
	REQUIRE(chunks.size() == 1);
	const TerrainLayerMesh &base = chunks[0].layer[0];
	REQUIRE(base.vertexCount() == 4);
	REQUIRE(base.wrap.size() == 16);
	for (int k = 0; k < 4; ++k)
	{
		CHECK(base.wrap[(size_t)k * 4 + 0] == 4.0f / 2048.0f);
		CHECK(base.wrap[(size_t)k * 4 + 1] == 4.0f / 512.0f);
		CHECK(base.wrap[(size_t)k * 4 + 2] == 256.0f / 2048.0f);
		CHECK(base.wrap[(size_t)k * 4 + 3] == 256.0f / 512.0f);
	}
	CHECK(stats.wrapCells == 1);
	CHECK(stats.straddlingCells == 1);
	mo.uv = donor;
	REQUIRE(HeightMapMesh::build(m, nullptr, mo, chunks, stats, &err));
	CHECK(stats.wrapCells == 0);
	CHECK(chunks[0].layer[0].wrap[2] == 0.0f);
}

// ------------------------------------------------------------------------------------------------
// Ground height
// ------------------------------------------------------------------------------------------------

TEST_CASE("TerrainLogic: the SW-NE split, clip height, normal, extents, waypoints snapped to the ground")
{
	// 8x8 grid, border 1; heights raw units (x 10/256 world units): h(x,y) = 256*x + 512*y for a simple ramp
	WorldHeightMap m = makeMap(8, 8, 1);
	for (int y = 0; y < 8; ++y)
		for (int x = 0; x < 8; ++x)
			m.m_data[(size_t)(y * 8 + x)] = (std::uint16_t)(256 * x + 512 * y);
	m.m_boundaries = { { 6, 6 }, { 3, 4 } };
	MapChunks chunks;
	// two waypoints and a link
	{
		MapObject a;
		a.m_objectName = "*Waypoints/Waypoint";
		a.m_location = { 25.0f, 25.0f, 99.0f };
		a.m_properties.setInt("waypointID", 7);
		a.m_properties.setAsciiString("waypointName", "Start");
		a.m_properties.setAsciiString("waypointPathLabel1", "path1");
		a.m_properties.setBool("waypointPathBiDirectional", true);
		a.m_isWaypoint = true;
		MapObject b = a;
		b.m_properties.setInt("waypointID", 9);
		b.m_properties.setAsciiString("waypointName", "End");
		b.m_location = { 35.0f, 15.0f, 0.0f };
		chunks.objects = { a, b };
		chunks.waypointLinks = { { 7, 9 }, { 9, 77 } };
	}
	TerrainLogic t;
	std::vector<std::string> problems;
	t.init(m, chunks, &problems);

	// world (25,25): ix = floor(2.5)+border = 3, fx = 0.5; iy = 3, fy = 0.5. fy > fx is false -> lower triangle.
	// p0 = h(3,3) = 768+1536 = 2304; p1 = h(4,3) = 1024+1536 = 2560; p2 = h(4,4) = 1024+2048 = 3072
	// height = (p1 + fy*(p2-p1) + (1-fx)*(p0-p1)) * 10/256 = (2560 + 256 - 128) * 0.0390625 = 105
	float h = t.getGroundHeight(25.0f, 25.0f);
	CHECK(h == doctest::Approx(105.0f).epsilon(1e-6));
	// fy > fx: world (22,28): ix = 3 fx 0.2; iy = 3, fy = 0.8: upper triangle. p3 = h(3,4) = 768+2048 = 2816
	// height = (p3 + (1-fy)*(p0-p3) + fx*(p2-p3)) * scale = (2816 + 0.2*(-512) + 0.2*256) * scale = 2764.8 * 0.0390625
	CHECK(t.getGroundHeight(22.0f, 28.0f) == doctest::Approx(2764.8f * 0.0390625f).epsilon(1e-5));

	// off-map: the clip height of the clamped cell and an up normal
	Coord3D n;
	float off = t.getGroundHeight(-100.0f, 25.0f, &n);
	CHECK(n.x == 0.0f); CHECK(n.y == 0.0f); CHECK(n.z == 1.0f);
	CHECK(off == (float)t.getClipHeight(-9, 3) * (10.0f / 256.0f));
	CHECK(t.getClipHeight(-5, -5) == m.m_data[0]);
	CHECK(t.getClipHeight(99, 99) == m.m_data[63]);
	CHECK(t.getClipHeight(7, 0) == 256 * 7);

	// the ramp's slope: dz/dx = 256/20... normal leans against +x and +y; unit length
	t.getGroundHeight(25.0f, 25.0f, &n);
	CHECK(n.x < 0.0f);
	CHECK(n.y < 0.0f);
	CHECK(n.x * n.x + n.y * n.y + n.z * n.z == doctest::Approx(1.0f).epsilon(1e-5));

	float mx, my;
	REQUIRE(t.getExtent(0, mx, my));
	CHECK(mx == 60.0f); CHECK(my == 60.0f);
	REQUIRE(t.getExtent(1, mx, my));
	CHECK(mx == 30.0f); CHECK(my == 40.0f);
	CHECK_FALSE(t.getExtent(2, mx, my));
	float x0, y0, x1, y1;
	t.getMapExtentIncludingBorder(x0, y0, x1, y1);
	CHECK(x0 == -10.0f); CHECK(y0 == -10.0f); CHECK(x1 == 70.0f); CHECK(y1 == 70.0f);

	// waypoints: z snapped to the ground, labels, links; a link to an unknown id is reported
	REQUIRE(t.waypoints().size() == 2);
	const Waypoint *s = t.findWaypointById(7);
	REQUIRE(s != nullptr);
	CHECK(s->name == "Start");
	CHECK(s->location.z == doctest::Approx(105.0f).epsilon(1e-6)); // not the stored 99
	CHECK(s->label1 == "path1");
	CHECK(s->biDirectional);
	CHECK(s->linksTo == std::vector<int>{ 9 });
	CHECK(t.findWaypointByName("End") != nullptr);
	CHECK(t.findWaypointById(1) == nullptr);
	REQUIRE(problems.size() == 1);
	CHECK(problems[0].find("9 -> 77") != std::string::npos);
}

TEST_CASE("TerrainLogic: standing water queries use a non-convex polygon (even-odd) and the water height")
{
	WorldHeightMap m = makeMap(4, 4);
	MapChunks chunks;
	StandingWaterArea a;
	a.waterHeight = 294;
	// an L-shaped polygon
	a.points = { { 0, 0 }, { 20, 0 }, { 20, 10 }, { 10, 10 }, { 10, 20 }, { 0, 20 } };
	chunks.standingWaterAreas.push_back(a);
	TerrainLogic t;
	t.init(m, chunks);
	float z = 0;
	CHECK(t.getStandingWaterHeight(5, 5, z));
	CHECK(z == 294.0f);
	CHECK(t.getStandingWaterHeight(15, 5, z));
	CHECK(t.getStandingWaterHeight(5, 15, z));
	CHECK_FALSE(t.getStandingWaterHeight(15, 15, z)); // the notch of the L
	CHECK_FALSE(t.getStandingWaterHeight(-1, 5, z));
	CHECK(t.isUnderwater(5, 5, 290.0f));
	CHECK_FALSE(t.isUnderwater(5, 5, 300.0f));
}

// ------------------------------------------------------------------------------------------------
// Mesh arrays
// ------------------------------------------------------------------------------------------------

TEST_CASE("HeightMapMesh: four unshared vertices per cell, layers, flipped index pattern, vertex colours, normals")
{
	// 4x3 grid, flat at raw height 256 (= 10 world units), border 0 -> cells 3x2
	WorldHeightMap m = makeMap(4, 3, 0);
	std::fill(m.m_data.begin(), m.m_data.end(), 256);
	m.m_terrainTexHeight = 512;
	m.m_tileLocations.assign(4, TileLocation());
	m.m_tileLocations[0] = { 4, 4, true };
	m.m_textureClasses = { cls("A", 0, 4, 2) };
	m.m_textureClasses[0].positionInTexture = { 4, 4 };
	m.m_numBitmapTiles = 4;
	// cell (1,0): a blend with a right diagonal (flips) and an extra layer with a horizontal
	m.m_blendTileNdxes[1] = 1;
	m.m_extraBlendTileNdxes[1] = 2;
	m.m_blendedTiles.resize(3);
	m.m_blendedTiles[1].rightDiagonal = 1;
	m.m_blendedTiles[2].horiz = 1;
	m.m_blendedTiles[2].inverted = 0;
	m.m_numBlendedTiles = 3;

	GlobalLightingData lit;
	lit.timeOfDay = 2; // Afternoon -> index 1
	TimeOfDayLights &tod = lit.tod[1];
	tod.terrain[0].ambient[0] = tod.terrain[0].ambient[1] = tod.terrain[0].ambient[2] = 0.25f;
	tod.terrain[0].diffuse[0] = tod.terrain[0].diffuse[1] = tod.terrain[0].diffuse[2] = 0.25f;
	tod.terrain[0].lightPos[2] = -1.0f; // straight down
	tod.terrain[1].diffuse[0] = 0.25f; tod.terrain[1].diffuse[1] = 0.5f; tod.terrain[1].diffuse[2] = 0.0f;
	tod.terrain[1].lightPos[2] = -1.0f;
	// terrain[2] has zero diffuse
	// a different time of day must not be used
	lit.tod[0].terrain[0].ambient[0] = 1.0f;

	HeightMapMeshOptions opt;
	std::vector<TerrainChunk> chunks;
	TerrainMeshStats st;
	std::string err;
	REQUIRE_MESSAGE(HeightMapMesh::build(m, &lit, opt, chunks, st, &err), err);
	CHECK(st.cells == 6);
	CHECK(st.baseCells == 6);
	CHECK(st.blendCells == 1);
	CHECK(st.extraCells == 1);
	CHECK(st.flippedCells == 1);
	CHECK(st.timeOfDayIndex == 1);
	REQUIRE(chunks.size() == 1); // 3x2 cells fit one 32x32 chunk
	const TerrainChunk &c = chunks[0];
	CHECK(c.layer[0].vertexCount() == 24);
	CHECK(c.layer[0].index.size() == 36);
	CHECK(c.layer[1].vertexCount() == 4);
	CHECK(c.layer[2].vertexCount() == 4);

	// cell (0,0) is the first base cell: SW=(0,0), SE=(10,0), NE=(10,10), NW=(0,10), z = 256 * 10/256 = 10
	const TerrainLayerMesh &b = c.layer[0];
	CHECK(b.position[0] == 0.0f);  CHECK(b.position[1] == 0.0f);  CHECK(b.position[2] == 10.0f);
	CHECK(b.position[3] == 10.0f); CHECK(b.position[4] == 0.0f);
	CHECK(b.position[6] == 10.0f); CHECK(b.position[7] == 10.0f);
	CHECK(b.position[9] == 0.0f);  CHECK(b.position[10] == 10.0f);
	// flat terrain: normal straight up
	CHECK(b.normal[0] == 0.0f); CHECK(b.normal[1] == 0.0f); CHECK(b.normal[2] == 1.0f);
	// unflipped winding for Godot: (SW,NW,NE),(SW,NE,SE)
	const std::uint32_t un[6] = { 0, 3, 2, 0, 2, 1 };
	for (int i = 0; i < 6; ++i) CHECK(b.index[(size_t)i] == un[i]);
	// the second base cell (1,0) is flipped (right diagonal not inverted): (SE,SW,NW),(SE,NW,NE) offset by 4
	const std::uint32_t fl[6] = { 5, 4, 7, 5, 7, 6 };
	for (int i = 0; i < 6; ++i) CHECK(b.index[(size_t)(6 + i)] == fl[i]);

	// vertex colour: ambient 0.25 + accent terrain[1] (dot(-lightPos, up) = 1) * diffuse (0.25, 0.5, 0) = (0.5, 0.75, 0.25)
	// -> bytes (int)(x*255) = 127, 191, 63. The sun (terrain[0]) diffuse is NOT in the vertex colour by default.
	CHECK(b.color[0] == 127); CHECK(b.color[1] == 191); CHECK(b.color[2] == 63); CHECK(b.color[3] == 255);
	// BFME1 doTheLight verbatim adds the sun diffuse too: (0.75, 1.0 clamped, 0.5) -> 191, 255, 127
	HeightMapMeshOptions all = opt;
	all.colorMode = VERTEX_COLOR_ALL_GLOBAL_LIGHTS;
	std::vector<TerrainChunk> chunks2;
	TerrainMeshStats st2;
	REQUIRE(HeightMapMesh::build(m, &lit, all, chunks2, st2, &err));
	CHECK(chunks2[0].layer[0].color[0] == 191); CHECK(chunks2[0].layer[0].color[1] == 255); CHECK(chunks2[0].layer[0].color[2] == 127);

	// the blend layer carries the per-vertex alpha of the right diagonal (not inverted): only NE
	CHECK(c.layer[1].color[3] == 0);   // SW
	CHECK(c.layer[1].color[7] == 0);   // SE
	CHECK(c.layer[1].color[11] == 255);// NE
	CHECK(c.layer[1].color[15] == 0);  // NW
	// the blend layer shares the base triangulation (flipped)
	for (int i = 0; i < 6; ++i) CHECK(c.layer[1].index[(size_t)i] == fl[i] - 4);
	// the extra layer has its own flip: a plain horizontal does not flip
	for (int i = 0; i < 6; ++i) CHECK(c.layer[2].index[(size_t)i] == un[i]);
	// the named assumptions are reported
	bool s030 = false, s032 = false, s033 = false;
	for (const std::string &a : st.assumptions)
	{
		s030 |= a.find("S-030") == 0;
		s032 |= a.find("S-032") == 0;
		s033 |= a.find("S-033") == 0;
	}
	CHECK(s030);
	CHECK(s032);
	CHECK(s033);

	// without lighting every vertex is white and that is reported
	std::vector<TerrainChunk> chunks3;
	TerrainMeshStats st3;
	REQUIRE(HeightMapMesh::build(m, nullptr, opt, chunks3, st3, &err));
	CHECK(chunks3[0].layer[0].color[0] == 255);
	bool white = false;
	for (const std::string &a : st3.assumptions) white |= a.find("no GlobalLighting") != std::string::npos;
	CHECK(white);

	// a bad time of day is an error
	lit.timeOfDay = 9;
	CHECK_FALSE(HeightMapMesh::build(m, &lit, opt, chunks3, st3, &err));
	CHECK(err.find("timeOfDay 9") != std::string::npos);
}

TEST_CASE("HeightMapMesh: a slope tilts the normal and splits large maps into 32x32-cell chunks")
{
	// 70x40 grid, x ramp: every raw step of +256 per column = 10 world units over 10 units of x -> 45 degrees
	WorldHeightMap m = makeMap(70, 40, 0);
	for (int y = 0; y < 40; ++y)
		for (int x = 0; x < 70; ++x)
			m.m_data[(size_t)(y * 70 + x)] = (std::uint16_t)(256 * x);
	m.m_terrainTexHeight = 512;
	m.m_tileLocations.assign(1, TileLocation());
	std::vector<TerrainChunk> chunks;
	TerrainMeshStats st;
	std::string err;
	HeightMapMeshOptions opt;
	REQUIRE_MESSAGE(HeightMapMesh::build(m, nullptr, opt, chunks, st, &err), err);
	CHECK(st.cells == 69u * 39u);
	CHECK(chunks.size() == 3u * 2u); // ceil(69/32) x ceil(39/32)
	CHECK(chunks[0].cellX1 == 32);
	CHECK(chunks[2].cellX0 == 64);
	CHECK(chunks[2].cellX1 == 69);
	CHECK(chunks[3].cellY0 == 32);
	CHECK(chunks[3].cellY1 == 39);
	// interior vertex: l2r = (20, 0, 20) (+2 columns = 512 raw = 20 units), n2f = (0, 20, 0): normal = (-1, 0, 1)/sqrt2
	const TerrainLayerMesh &b = chunks[0].layer[0];
	const size_t v = 4 * (5 * 32 + 5); // cell (5,5)'s SW vertex lives at offset 4*cellIndex
	CHECK(b.normal[v * 3 + 0] == doctest::Approx(-0.70710678f).epsilon(1e-5));
	CHECK(b.normal[v * 3 + 1] == doctest::Approx(0.0f).epsilon(1e-6));
	CHECK(b.normal[v * 3 + 2] == doctest::Approx(0.70710678f).epsilon(1e-5));
	// chunk bounds in SAGE space
	CHECK(chunks[0].boundsMin[0] == 0.0f);
	CHECK(chunks[0].boundsMax[0] == 320.0f);
	CHECK(chunks[0].boundsMax[2] == 320.0f); // x = 32 -> raw 256*32 -> 320 world units
}

// ------------------------------------------------------------------------------------------------
// The composite (one pass, layers blended in gamma space)
// ------------------------------------------------------------------------------------------------

namespace
{
// A 4x4 grid (3x3 cells) whose first two cells carry blend and extra-blend tiles with every flip combination.
WorldHeightMap compositeTestMap()
{
	WorldHeightMap m = makeMap(4, 4);
	m.m_terrainTexHeight = 512;
	m.m_textureClasses = { cls("Grass", 0, 16, 4) };
	m.m_textureClasses[0].positionInTexture = { 4, 4 };
	m.m_tileLocations.assign(16, TileLocation());
	for (int i = 0; i < 16; ++i)
	{
		m.m_tileLocations[(size_t)i] = { 4 + 64 * (i % 4), 4 + 64 * (i / 4), true };
	}
	for (int i = 0; i < 16; ++i)
	{
		m.m_data[(size_t)i] = (std::uint16_t)(100 * i); // an uneven surface: the cliff flip rule is not involved
	}
	m.m_blendedTiles.assign(4, TBlendTileInfo());
	// 1: uninverted right diagonal -> alpha (0,0,1,0), needs the SE-NW flip
	m.m_blendedTiles[1].blendNdx = 5 << 2;
	m.m_blendedTiles[1].rightDiagonal = 1;
	// 2: horizontal, uninverted -> alpha (0,1,1,0), no flip
	m.m_blendedTiles[2].blendNdx = 6 << 2;
	m.m_blendedTiles[2].horiz = 1;
	// 3: inverted left diagonal -> alpha (1,0,0,0), needs the flip
	m.m_blendedTiles[3].blendNdx = 7 << 2;
	m.m_blendedTiles[3].leftDiagonal = 1;
	m.m_blendedTiles[3].inverted = 1;
	m.m_numBlendedTiles = 4;
	m.m_blendTileNdxes[0] = 1; // cell (0,0): layer 1 flipped
	m.m_blendTileNdxes[1] = 2; // cell (1,0): layer 1 straight
	m.m_extraBlendTileNdxes[1] = 3; // cell (1,0): layer 2 flipped while the base is straight (S-032: own flip)
	m.m_extraBlendTileNdxes[4] = 2; // cell (0,1): layer 2 only (a cell with an extra layer but no first blend)
	return m;
}

// Value of vertex attribute `attr(k)` at local point p inside layer cell `cell` by the barycentric
// interpolation a GPU does over the layer's own two triangles (indices as emitted by HeightMapMesh).
template <class F>
float gpuInterpolate(const TerrainLayerMesh &L, size_t cell, float lx, float ly, F attr)
{
	static const float kLocal[4][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
	for (int t = 0; t < 2; ++t)
	{
		const std::uint32_t i0 = L.index[cell * 6 + (size_t)t * 3] - (std::uint32_t)cell * 4;
		const std::uint32_t i1 = L.index[cell * 6 + (size_t)t * 3 + 1] - (std::uint32_t)cell * 4;
		const std::uint32_t i2 = L.index[cell * 6 + (size_t)t * 3 + 2] - (std::uint32_t)cell * 4;
		const float ax = kLocal[i0][0], ay = kLocal[i0][1], bx = kLocal[i1][0], by = kLocal[i1][1], cx = kLocal[i2][0], cy = kLocal[i2][1];
		const float det = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy);
		const float w0 = ((by - cy) * (lx - cx) + (cx - bx) * (ly - cy)) / det;
		const float w1 = ((cy - ay) * (lx - cx) + (ax - cx) * (ly - cy)) / det;
		const float w2 = 1.0f - w0 - w1;
		if (w0 >= -1e-6f && w1 >= -1e-6f && w2 >= -1e-6f)
		{
			return w0 * attr(i0) + w1 * attr(i1) + w2 * attr(i2);
		}
	}
	return std::nanf("");
}
} // namespace

TEST_CASE("TerrainComposite::interpolateCorner equals the barycentric interpolation over the layer's own triangles, flipped or not")
{
	WorldHeightMap m = compositeTestMap();
	HeightMapMeshOptions mo;
	std::vector<TerrainChunk> chunks;
	TerrainMeshStats stats;
	std::string err;
	REQUIRE_MESSAGE(HeightMapMesh::build(m, nullptr, mo, chunks, stats, &err), err);
	REQUIRE(chunks.size() == 1);
	const TerrainChunk &c = chunks[0];
	REQUIRE(c.layer[0].cellCount() == 9);
	REQUIRE(c.layer[1].cellCount() == 2);
	REQUIRE(c.layer[2].cellCount() == 2);
	// the flips the tile table above must produce (ZH getAlphaUVData / getExtraAlphaUVData)
	CHECK(c.layer[1].cellFlip == (std::vector<std::uint8_t>{ 1, 0 }));
	CHECK(c.layer[2].cellFlip == (std::vector<std::uint8_t>{ 1, 0 })); // cell 1 flipped, cell 3 straight
	CHECK(c.layer[0].cellFlip[0] == 1);
	CHECK(c.layer[0].cellFlip[1] == 0);
	CHECK(c.layer[1].cellId == (std::vector<std::uint32_t>{ 0, 1 }));
	CHECK(c.layer[2].cellId == (std::vector<std::uint32_t>{ 1, 3 }));

	int compared = 0;
	for (int l = 0; l < 3; ++l)
	{
		const TerrainLayerMesh &L = c.layer[l];
		for (size_t cell = 0; cell < L.cellCount(); ++cell)
		{
			const bool flip = L.cellFlip[cell] != 0;
			for (int a = 0; a < 3; ++a) // attribute: u, v, alpha
			{
				float corner[4];
				for (int k = 0; k < 4; ++k)
				{
					corner[k] = a == 0 ? L.uv[(cell * 4 + (size_t)k) * 2] : a == 1 ? L.uv[(cell * 4 + (size_t)k) * 2 + 1] : (float)L.color[(cell * 4 + (size_t)k) * 4 + 3] / 255.0f;
				}
				for (int iy = 0; iy <= 8; ++iy)
				{
					for (int ix = 0; ix <= 8; ++ix)
					{
						const float lx = ix / 8.0f, ly = iy / 8.0f;
						const float want = gpuInterpolate(L, cell, lx, ly, [&](std::uint32_t k) { return corner[k]; });
						REQUIRE_FALSE(std::isnan(want));
						CHECK(TerrainComposite::interpolateCorner(corner, flip, lx, ly) == doctest::Approx(want).epsilon(1e-5).scale(1.0));
						++compared;
					}
				}
			}
		}
	}
	CHECK(compared == (9 + 2 + 2) * 3 * 81);
	// the flip matters: alpha (0,0,1,0) at the centre is 0.5 straight (diagonal SW-NE passes through NE) and 0 flipped
	const float rightDiag[4] = { 0, 0, 1, 0 };
	CHECK(TerrainComposite::interpolateCorner(rightDiag, false, 0.5f, 0.5f) == doctest::Approx(0.5f));
	CHECK(TerrainComposite::interpolateCorner(rightDiag, true, 0.5f, 0.5f) == doctest::Approx(0.0f));
}

TEST_CASE("TerrainComposite::build: one opaque mesh per chunk, one record per cell with a blend layer, records hold the layers' UVs, alphas, flips and wrap rects")
{
	WorldHeightMap m = compositeTestMap();
	HeightMapMeshOptions mo;
	std::vector<TerrainChunk> chunks;
	TerrainMeshStats stats;
	std::string err;
	REQUIRE_MESSAGE(HeightMapMesh::build(m, nullptr, mo, chunks, stats, &err), err);
	std::vector<TerrainComposite::Chunk> out;
	TerrainComposite::Records rec;
	TerrainComposite::Stats cs;
	REQUIRE_MESSAGE(TerrainComposite::build(chunks, out, rec, cs, &err), err);
	REQUIRE(out.size() == 1);
	const TerrainComposite::Chunk &oc = out[0];
	// the composite draws the base layer's vertices and triangles
	CHECK(oc.vertexCount() == 36);
	CHECK(oc.index == chunks[0].layer[0].index);
	CHECK(oc.position == chunks[0].layer[0].position);
	CHECK(oc.uv == chunks[0].layer[0].uv);
	for (size_t i = 3; i < oc.color.size(); i += 4)
	{
		CHECK(oc.color[i] == 255);
	}
	// cells (0,0) (id 0), (1,0) (id 1) and (0,1) (id 3) have blend layers: three records in cell order
	CHECK(cs.cells == 9);
	CHECK(cs.recordCells == 3);
	CHECK(cs.layer1Cells == 2);
	CHECK(cs.layer2Cells == 2);
	CHECK(cs.layer2FlipDiffers == 1); // cell id 1: base straight, layer 2 flipped
	CHECK(rec.count == 3);
	CHECK(rec.width == TerrainComposite::RECORD_TEXELS * TerrainComposite::RECORDS_PER_ROW);
	CHECK(rec.height == 1);
	CHECK(rec.texels.size() == (size_t)rec.width * 4);
	const float want[9] = { 0, 0, 1, -1, -1, -1, -1, -1, -1 }; // base layer cells: records 0, 1, then none, record 2 for cell id 3
	for (size_t cell = 0; cell < 9; ++cell)
	{
		const float expected = cell == 0 ? 0.0f : cell == 1 ? 1.0f : cell == 3 ? 2.0f : -1.0f;
		for (size_t k = 0; k < 4; ++k)
		{
			CHECK(oc.record[cell * 4 + k] == expected);
			CHECK(oc.local[(cell * 4 + k) * 2] == (k == 1 || k == 2 ? 1.0f : 0.0f));
			CHECK(oc.local[(cell * 4 + k) * 2 + 1] == (k == 2 || k == 3 ? 1.0f : 0.0f));
		}
	}
	(void)want;
	// record 0 = cell (0,0): layer 1 alpha (0,0,1,0), flipped, no layer 2
	auto texel = [&](size_t r, int k, int ch) { return rec.texels[(r * TerrainComposite::RECORD_TEXELS + (size_t)k) * 4 + (size_t)ch]; };
	const TerrainLayerMesh &L1 = chunks[0].layer[1];
	CHECK(texel(0, 0, 0) == L1.uv[0]); // SW u
	CHECK(texel(0, 0, 1) == L1.uv[1]); // SW v
	CHECK(texel(0, 0, 2) == L1.uv[2]); // SE u
	CHECK(texel(0, 1, 0) == L1.uv[4]); // NE u
	CHECK(texel(0, 1, 3) == L1.uv[7]); // NW v
	CHECK(texel(0, 2, 0) == 0.0f);
	CHECK(texel(0, 2, 1) == 0.0f);
	CHECK(texel(0, 2, 2) == 1.0f);
	CHECK(texel(0, 2, 3) == 0.0f);
	CHECK(texel(0, 8, 0) == 1.0f); // flip1
	CHECK(texel(0, 8, 2) == 1.0f); // has1
	CHECK(texel(0, 8, 3) == 0.0f); // has2
	// record 1 = cell (1,0): layer 1 straight (alpha 0,1,1,0) and layer 2 flipped (alpha 1,0,0,0)
	CHECK(texel(1, 2, 1) == 1.0f);
	CHECK(texel(1, 2, 2) == 1.0f);
	CHECK(texel(1, 6, 0) == 1.0f);
	CHECK(texel(1, 6, 1) == 0.0f);
	CHECK(texel(1, 8, 0) == 0.0f); // flip1
	CHECK(texel(1, 8, 1) == 1.0f); // flip2
	CHECK(texel(1, 8, 2) == 1.0f);
	CHECK(texel(1, 8, 3) == 1.0f);
	// record 2 = cell (0,1): layer 2 only
	CHECK(texel(2, 8, 2) == 0.0f);
	CHECK(texel(2, 8, 3) == 1.0f);
	CHECK(texel(2, 6, 1) == 1.0f); // horizontal uninverted: alpha (0,1,1,0)
	CHECK(texel(2, 6, 2) == 1.0f);
	CHECK(texel(2, 6, 0) == 0.0f);
}

TEST_CASE("TerrainComposite::build refuses blend layers that are not subsequences of the base layer")
{
	WorldHeightMap m = compositeTestMap();
	HeightMapMeshOptions mo;
	std::vector<TerrainChunk> chunks;
	TerrainMeshStats stats;
	std::string err;
	REQUIRE(HeightMapMesh::build(m, nullptr, mo, chunks, stats, &err));
	chunks[0].layer[1].cellId[1] = 77; // a cell the base layer does not have
	std::vector<TerrainComposite::Chunk> out;
	TerrainComposite::Records rec;
	TerrainComposite::Stats cs;
	CHECK_FALSE(TerrainComposite::build(chunks, out, rec, cs, &err));
	CHECK(err.find("terrain composite:") != std::string::npos);
}
