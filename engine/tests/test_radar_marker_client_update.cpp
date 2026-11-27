// OpenBFME. GPL-3.0.
// Editions: both RotWK 2.01 and BFME2 1.06. RadarMarkerClientUpdate: RW 0x65287A / BFME2 0x652B1F (registry), clientUpdate RW 0x8CE116 / BFME2 0x4C9CF7,
// TheRadar's createMarker (vslot 12 / 0x30, RW 0x44F0FA), the marker's DeleteReference RW 0x6D8711 and drawEvents' marker loop RW 0x44EDAE. Synthetic only.

#include "Common/GameEdition.h"
#include "GameClient/Radar.h"
#include "GameClient/RadarMarkerClientUpdate.h"

#include "doctest.h"

#include <vector>

TEST_CASE("radar marker client update: TheRadar marker query, ping creation, movement and fadeout")
{
	for (GameEdition edition : { GameEdition::RotWK201, GameEdition::BFME2_106 })
	{
		CAPTURE(gameEditionName(edition));

		Radar radar;
		CHECK(TheRadar == &radar);

		RadarMarkerClientUpdateModuleData data;
		data.m_markerType = "PingOneRing";

		// Direct marker query on TheRadar (vslot 12 / 0x30, RW 0x44F0FA / BFME2 0x4C9CF7)
		RadarMarker *marker = TheRadar->createMarker("PingOneRing");
		REQUIRE(marker != nullptr);
		CHECK(marker->markerType() == "PingOneRing");
		CHECK(TheRadar->markers().size() == 1);

		// Position marker within default radar extent (0..1)
		marker->setPosition(Coord3D{ 0.2f, 0.3f, 0.0f });

		// Draw events projects marker to radar pixel (RW 0x44EDAE / BFME2 0x0004F515)
		std::vector<Radar::PingCall> calls;
		radar.drawEvents(0, 0, 128, 128, calls);

		// PingCall::Create and PingCall::Move emitted
		bool created = false;
		bool moved = false;
		for (const auto &c : calls)
		{
			if (c.kind == Radar::PingCall::Create && c.name == "PingOneRing")
			{
				created = true;
			}
			if (c.kind == Radar::PingCall::Move && c.id == marker->pingId())
			{
				moved = true;
			}
		}
		CHECK(created);
		CHECK(moved);

		// Move marker position by 0.4 units (moving > 50 radar pixels)
		calls.clear();
		marker->setPosition(Coord3D{ 0.6f, 0.7f, 0.0f });
		radar.drawEvents(0, 0, 128, 128, calls);
		bool moveAgain = false;
		for (const auto &c : calls)
		{
			if (c.kind == Radar::PingCall::Move && c.id == marker->pingId())
			{
				moveAgain = true;
			}
		}
		CHECK(moveAgain);

		// DeleteReference releases marker (RW 0x6D8711 / BFME2 0x002D76BB)
		marker->DeleteReference();
		CHECK(marker->isReleased());

		// Next drawEvents fades out ping and removes released marker
		calls.clear();
		radar.drawEvents(0, 0, 128, 128, calls);
		bool faded = false;
		for (const auto &c : calls)
		{
			if (c.kind == Radar::PingCall::FadeOut)
			{
				faded = true;
			}
		}
		CHECK(faded);
		CHECK(TheRadar->markers().empty());
	}
}
