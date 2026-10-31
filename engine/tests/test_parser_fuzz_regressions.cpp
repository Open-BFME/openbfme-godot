// OpenBFME unit tests. GPL-3.0.
// Minimized parser fuzz inputs (engine/fuzz): each must be refused with an error, never read out of bounds. No game files.

#include "doctest.h"

#include "GameClient/CursorFile.h"
#include "W3dTestUtil.h"
#include "Libraries/WWVegas/WW3D2/assetmgr.h"
#include "Libraries/WWVegas/WW3D2/w3d_file.h"

#include <cstdint>
#include <string>
#include <vector>

TEST_CASE("parser fuzz regression: a cursor whose bitmap header size runs past the file is refused")
{
	const std::vector<std::uint8_t> bytes = {
		0x00, 0x00, 0x02, 0x00, 0x01, 0x00, 0x01, 0x01, 0x00, 0x00, 0x0c, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x16, 0x00, 0x00, 0x00, 0x28, 0x01, 0x07, 0x00, 0x01, 0x00, 0x00, 0x00, 0x02, 0x00,
		0x00, 0x00, 0x01, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	};
	CursorImage image;
	std::string error;
	CHECK_FALSE(DecodeCursorFile(bytes, image, &error));
	CHECK(error == "the bitmap header or palette lies outside the file");
}

TEST_CASE("parser fuzz regression: an HLod whose LodCount exceeds its chunk is refused before allocating")
{
	W3dHLodHeaderStruct header{};
	header.LodCount = 0xFFFFFFFFu;
	w3dtest::setName(header.Name, sizeof(header.Name), "FUZZ");
	w3dtest::ChunkWriter body, file;
	body.chunk(W3D_CHUNK_HLOD_HEADER, w3dtest::ChunkWriter::of(header));
	file.wrapper(W3D_CHUNK_HLOD, body);
	W3DFileContents contents;
	std::string error;
	CHECK_FALSE(Load_W3D_File(file.bytes.data(), file.bytes.size(), contents, &error));
	CHECK(error.find("LodCount 4294967295 exceeds the HLod chunk") != std::string::npos);
}
