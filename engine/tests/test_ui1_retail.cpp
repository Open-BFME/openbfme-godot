// OpenBFME unit tests. GPL-3.0.
// Lane UI-1: the interface gaps other lanes reported, pinned against the RotWK binary (SKIP when RW_GAME_DAT is unset) and the retail data (SKIP
// without the retail install).

#include "doctest.h"

#include "Libraries/WWVegas/WW3D2/textureloader.h"
#include "PeImage.h"
#include "RetailTestMount.h"

#include <cstdint>
#include <string>

TEST_CASE("ui1 binary facts: a missing texture draws as retail's 1 x 1 opaque magenta (RW 0x53193E)")
{
	CHECK(W3D_MISSING_TEXTURE_COLOR == 0xFFFF00FFu);
	const retailtest::PeImage *pe = retailtest::PeImage::fromEnvironment();
	if (!pe)
	{
		retailtest::printSkip("ui1 binary facts: missing texture (RW_GAME_DAT unset)");
		return;
	}
	// RW 0x531B07 .. 0x531B14: push edi(0), edi(0), ebx(1), ebx(1), 0x15, ebx(1), ebx(1); call RW 0x530BB3 (width, height, format, levels ...)
	CHECK(pe->hex(0x531B07, 7) == "57575353" "6a15" "53");
	CHECK(pe->hex(0x531B0E, 1) == "53");
	CHECK(pe->hex(0x531B14, 1) == "e8");
	CHECK(pe->u32At(0x531B15) + 0x531B19u == 0x530BB3u);
	// RW 0x531B44: push 0xFFFF00FF, then the pixel (0, 0) (push edi, push edi) is drawn by RW 0x5165E0
	CHECK(pe->hex(0x531B44, 1) == "68");
	CHECK(pe->u32At(0x531B45) == W3D_MISSING_TEXTURE_COLOR);
	CHECK(pe->hex(0x531B49, 2) == "5757");
}

TEST_CASE("ui1 binary facts: the lobby map list (MpGameSetup::InitGadgets RW 0x840906, the fill RW 0x8460B5, the sort commands RW 0x840143 ..)")
{
	const retailtest::PeImage *pe = retailtest::PeImage::fromEnvironment();
	if (!pe)
	{
		retailtest::printSkip("ui1 binary facts: map list (RW_GAME_DAT unset)");
		return;
	}
	// the five column widths {8, 2, 0x46, 10 (eax), 10 (eax)} handed to GadgetListBoxSetColumnWidths (RW 0x726D36)
	CHECK(pe->hex(0x84092E, 2) == "6a0a");             // push 0xA; pop eax: the last two widths
	CHECK(pe->hex(0x84093B, 2) == "6a05");             // five columns
	CHECK(pe->hex(0x840944, 21) == "c745ec08000000" "c745f002000000" "c745f446000000");
	CHECK(pe->u32At(0x84095A) + 0x84095Eu == 0x726D36u);
	// RW 0x8465D8: the name column is numColumns - 3, the player count column numColumns - 1
	CHECK(pe->hex(0x8465D8, 10) == "8b45d0" "8d48fd" "48" "894dd0");
	// the player count's format L"%d" (RW 0xBDF1B0)
	CHECK(pe->hex(0xBDF1B0, 6) == "250064000000");
	// OnSortName / OnSortPlayers / OnSortIcons push 0 / 2 / 4 to RW 0x84010E; the default primary key is 2 (RW 0x8451A9)
	CHECK(pe->hex(0x840143, 2) == "6a00");
	CHECK(pe->hex(0x84014D, 2) == "6a02");
	CHECK(pe->hex(0x840157, 2) == "6a04");
	CHECK(pe->hex(0x8451A9, 10) == "c783ac03000002000000");
}

TEST_CASE("ui1 binary facts: the contain slots the control bar's inventory reads (0x10 isGarrisonable, 0xC8 isDisplayedOnControlBar)")
{
	const retailtest::PeImage *pe = retailtest::PeImage::fromEnvironment();
	if (!pe)
	{
		retailtest::printSkip("ui1 binary facts: contain slots (RW_GAME_DAT unset)");
		return;
	}
	const std::uint32_t kFalse = 0x9188EB, kTrue = 0x8BD372;
	CHECK(pe->hex(kFalse, 3) == "32c0c3"); // xor al, al; ret
	CHECK(pe->hex(kTrue, 3) == "b001c3");  // mov al, 1; ret
	struct Table
	{
		const char *name;
		std::uint32_t va;
		bool garrisonable, displayed;
	};
	const Table tables[] = {
		{ "OpenContain", 0xC59AF0, false, false },
		{ "GarrisonContain", 0xC5C698, true, true },
		{ "HordeGarrisonContain", 0xC5C9F0, true, true },
		{ "TransportContain", 0xC5A690, false, true },
		{ "HordeTransportContain", 0xC5C370, false, true },
		{ "SiegeEngineContain", 0xC5D668, false, true },
		{ "HordeSiegeEngineContain", 0xC5DA10, false, true },
		{ "TunnelContain", 0xC5DCE0, false, true },
	};
	for (const Table &t : tables)
	{
		INFO(t.name);
		CHECK(pe->u32At(t.va + 0x10) == (t.garrisonable ? kTrue : kFalse));
		CHECK(pe->u32At(t.va + 0xC8) == (t.displayed ? kTrue : kFalse));
	}
	// TransportContain's slot 0x70 is getContainMax RW 0x869F86 (the table's identity)
	CHECK(pe->u32At(0xC5A690 + 0x70) == 0x869F86u);
	// the inventory press: MSG_EXIT (1053 = 0x41D) at RW 0x941033
	CHECK(pe->hex(0x941033, 5) == "681d040000");
}
