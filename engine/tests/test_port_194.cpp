// OpenBFME unit tests (lane PORT-194, fork issue 461): the crowd response of the unit voice -- RotWK's CrowdResponse block parser and the threshold
// rule. Addresses are from the RotWK 2.01 game.dat: the block parser 0x826674 (block table entry at VA 0xDA775C), its field table VA 0xC5184C (Weight
// +4 through 0x42EC5E, Threshold +8 through 0x8264D2), the threshold record 0x825B52, the floor lookup 0x825C45 and the vote that hands it the objects
// table's element count at 0x8DEFF4.

#include "doctest.h"

#include "LogicTestUtil.h"

#include "GameClient/UnitVoiceResponse.h"
#include "GameLogic/GameMessage.h"
#include "GameLogic/Object/Object.h"

#include <memory>
#include <set>
#include <string>
#include <vector>

namespace
{
struct Port194Sink : UnitVoiceResponse::Sink
{
	std::set<std::string> known;
	std::vector<std::string> sounds;
	bool soundExists(const std::string &name) override { return known.count(name) != 0; }
	void playSound(const std::string &name, ObjectID, int) override { sounds.push_back(name); }
	void reportEva(const std::string &, const Coord3D *) override {}
	void clear() { sounds.clear(); }
};

const char kPort194Objects[] =
	"Object CrowdMan\n"
	"  KindOf = INFANTRY SELECTABLE\n"
	"  VoiceSelect = CrowdManSelect\n"
	"  CrowdResponseKey = GoodMen\n"
	"End\n"
	"Object CrowdOrc\n"
	"  KindOf = INFANTRY SELECTABLE\n"
	"  VoiceSelect = CrowdOrcSelect\n"
	"  CrowdResponseKey = BadMen\n"
	"End\n"
	"Object CrowdGhost\n"
	"  KindOf = INFANTRY SELECTABLE\n"
	"  VoiceSelect = CrowdGhostSelect\n"
	"  CrowdResponseKey = MissingMen\n" // a key that names no CrowdResponse block at all
	"End\n"
	"Object CrowdRock\n"
	"  KindOf = INFANTRY SELECTABLE\n"
	"  VoiceSelect = CrowdRockSelect\n" // a candidate whose template names no key
	"End\n";

// GoodMen: Threshold 1 / 2 / 3 -> CrowdOne / CrowdTwo / CrowdThree; BadMen: Weight 50, one threshold; the weights and counts are this lane's synthetic
// data, the rules they exercise are RW 0x8DEFB9 (Weight +4 added to the key's total) and RW 0x8DEFF4 (every collected object counts for the threshold)
const char kPort194Crowds[] =
	"CrowdResponse GoodMen\n"
	"  Threshold = 1\n"
	"    VoiceSelect = CrowdOne\n"
	"  End\n"
	"  Threshold = 2\n"
	"    VoiceSelect = CrowdTwo\n"
	"  End\n"
	"  Threshold = 3\n"
	"    VoiceSelect = CrowdThree\n"
	"  End\n"
	"  Weight = 100\n"
	"End\n"
	"CrowdResponse BadMen\n"
	"  Threshold = 1\n"
	"    VoiceSelect = CrowdOrcSelect\n"
	"  End\n"
	"  Weight = 50\n"
	"End\n";

struct Port194Rig
{
	logictest::LogicWorld f;
	Port194Sink sink;
	std::unique_ptr<UnitVoiceResponse> voice;
	Port194Rig()
	{
		const std::string err = f.w.load(kPort194Objects, INI_LOAD_OVERWRITE, "crowd194.ini");
		REQUIRE_MESSAGE(err.empty(), err);
		for (const char *s : { "CrowdManSelect", "CrowdOrcSelect", "CrowdGhostSelect", "CrowdRockSelect", "CrowdOne", "CrowdTwo", "CrowdThree" })
		{
			sink.known.insert(s);
		}
		voice = std::make_unique<UnitVoiceResponse>(*f.logic, sink);
		voice->setLocalPlayerIndex(f.players.findPlayerWithName("Alice")->getPlayerIndex());
	}
	ObjectID make(const char *name) { return f.make(name, f.teamOf("Alice"))->getID(); }
};

// the crowd sound a selection played: the last sound of the choice (the crowd slot follows the object's own sound)
std::string crowdOf(const Port194Sink &sink)
{
	return sink.sounds.empty() ? std::string() : sink.sounds.back();
}
} // namespace

TEST_CASE("PORT-194 unit voice: the Threshold entry is chosen by the number of collected objects, not by the objects that voted for the key (RW 0x8DEFF4 / 0x825C45)")
{
	Port194Rig r;
	REQUIRE(r.voice->parseCrowdResponses(kPort194Crowds, nullptr));
	const ObjectID man1 = r.make("CrowdMan"), man2 = r.make("CrowdMan"), orc = r.make("CrowdOrc"), ghost = r.make("CrowdGhost"),
				   rock = r.make("CrowdRock");

	// one man: the first threshold
	CHECK(r.voice->pickAndPlay({ man1 }, MSG_CREATE_SELECTED_GROUP));
	CHECK(r.sink.sounds == (std::vector<std::string>{ "CrowdManSelect", "CrowdOne" }));
	r.sink.clear();

	// two men: the second
	CHECK(r.voice->pickAndPlay({ man1, man2 }, MSG_CREATE_SELECTED_GROUP));
	CHECK(crowdOf(r.sink) == "CrowdTwo");
	r.sink.clear();

	// two men and an orc: GoodMen wins the vote (2 * 100 against BadMen's 50), and the threshold is measured against three objects (RW 0x8DEFF4)
	CHECK(r.voice->pickAndPlay({ man1, man2, orc }, MSG_CREATE_SELECTED_GROUP));
	CHECK(crowdOf(r.sink) == "CrowdThree");
	r.sink.clear();

	// four objects: the largest threshold at or below the count (RW 0x825C45)
	CHECK(r.voice->pickAndPlay({ man1, man2, man2, orc }, MSG_CREATE_SELECTED_GROUP));
	CHECK(crowdOf(r.sink) == "CrowdThree");
	r.sink.clear();

	// a man and an object whose key names no response: the orphan still counts for the threshold (RW 0x8DEEF9 inserts it into the objects table), and
	// the key that resolves nothing is reported (S-700: the port resolves the key at voice time, RW 0x825DC2 refuses the template instead)
	CHECK(r.voice->pickAndPlay({ man1, ghost }, MSG_CREATE_SELECTED_GROUP));
	CHECK(crowdOf(r.sink) == "CrowdTwo");
	CHECK(r.voice->stats().crowdResponseSkipped == 1);
	r.sink.clear();

	// a man and a rock (no key at all): the good men still speak, measured against two objects
	CHECK(r.voice->pickAndPlay({ man1, rock }, MSG_CREATE_SELECTED_GROUP));
	CHECK(crowdOf(r.sink) == "CrowdTwo");
	r.sink.clear();

	// one orc alone: BadMen's own threshold
	CHECK(r.voice->pickAndPlay({ orc }, MSG_CREATE_SELECTED_GROUP));
	CHECK(crowdOf(r.sink) == "CrowdOrcSelect"); // BadMen's Threshold 1 names the orc's own select sound
}

TEST_CASE("PORT-194 unit voice: the response's Weight (+4) votes for the key, and the largest total wins (RW 0x8DEFB9 / 0x8DEFDB)")
{
	Port194Rig r;
	// the same responses with the weights swapped: one orc now outweighs two men
	std::string text = kPort194Crowds;
	const std::string good = "  Weight = 100\n";
	const std::string bad = "  Weight = 50\n";
	text.replace(text.find(good), good.size(), "  Weight = 60\n");
	text.replace(text.find(bad), bad.size(), "  Weight = 250\n");
	REQUIRE(r.voice->parseCrowdResponses(text, nullptr));
	const ObjectID man1 = r.make("CrowdMan"), man2 = r.make("CrowdMan"), orc = r.make("CrowdOrc");
	CHECK(r.voice->pickAndPlay({ man1, man2, orc }, MSG_CREATE_SELECTED_GROUP));
	CHECK(crowdOf(r.sink) == "CrowdOrcSelect"); // BadMen 250 against GoodMen's 120
	CHECK(r.voice->crowdResponses().at("BadMen").weight == 250);
	CHECK(r.voice->crowdResponses().at("GoodMen").weight == 60);
}

TEST_CASE("PORT-194 unit voice: the CrowdResponse block parser refuses what RW 0x826674 / 0x8264D2 refuse (duplicate block, duplicate threshold, non-integer field)")
{
	Port194Rig r;
	std::string error;

	// RW 0xC5187C "Multiple CrowdResponse blocks named %s" (0x826674)
	CHECK_FALSE(r.voice->parseCrowdResponses("CrowdResponse GoodMen\n  Threshold = 1\n    VoiceSelect = CrowdOne\n  End\nEnd\n"
											"CrowdResponse GoodMen\n  Threshold = 2\n    VoiceSelect = CrowdTwo\n  End\nEnd\n",
		&error));
	CHECK(error.find("Multiple CrowdResponse blocks named GoodMen") != std::string::npos);
	CHECK(r.voice->crowdResponses().size() == 1); // the first block stays as it parsed

	// RW 0x826553 -> 0xC51808 "CrowdResponse: Multiple Thresholds with minimum units %d" (a name of its own: a refused block stays half-built in the
	// store, as RW's parse leaves it when the exception unwinds)
	error.clear();
	CHECK_FALSE(r.voice->parseCrowdResponses("CrowdResponse OddMen\n  Threshold = 2\n    VoiceSelect = CrowdTwo\n  End\n"
											"  Threshold = 2\n    VoiceSelect = CrowdThree\n  End\nEnd\n",
		&error));
	CHECK(error.find("CrowdResponse: Multiple Thresholds with minimum units 2") != std::string::npos);

	// RW 0x8264D2 -> 0x42E9D7 (the minimum-unit count is an integer)
	error.clear();
	CHECK_FALSE(r.voice->parseCrowdResponses("CrowdResponse SlowMen\n  Threshold = many\n    VoiceSelect = CrowdTwo\n  End\nEnd\n", &error));
	CHECK(error.find("Threshold needs an integer minimum-unit count") != std::string::npos);

	// RW 0xC5184C +4 -> 0x42EC5E (Weight is an integer)
	error.clear();
	CHECK_FALSE(r.voice->parseCrowdResponses("CrowdResponse HeavyMen\n  Weight = heavy\nEnd\n", &error));
	CHECK(error.find("Weight needs an integer") != std::string::npos);

	// an unknown field stays an error
	error.clear();
	CHECK_FALSE(r.voice->parseCrowdResponses("CrowdResponse RedMen\n  Colour = red\nEnd\n", &error));
	CHECK(error.find("unknown CrowdResponse field 'Colour'") != std::string::npos);
}

TEST_CASE("PORT-194 unit voice: a threshold record is sound-only, an EVA: override is refused in a row and in its UnitSpecificSounds block (RW 0x825B52)")
{
	Port194Rig r;
	std::string error;
	CHECK_FALSE(r.voice->parseCrowdResponses("CrowdResponse EvaMen\n  Threshold = 1\n    VoiceSelect = EVA:SomeEva\n  End\nEnd\n", &error));
	CHECK(error.find("You cannot use EVA: sound overrides in CrowdResponseTemplates") != std::string::npos);
	CHECK(error.find("(UnitSpecificSound block)") == std::string::npos); // 0xC51700, the record's own rows

	error.clear();
	CHECK_FALSE(r.voice->parseCrowdResponses("CrowdResponse NestMen\n  Threshold = 1\n    UnitSpecificSounds\n      VoiceGarrison = EVA:SomeEva\n    End\n  End\nEnd\n",
		&error));
	CHECK(error.find("You cannot use EVA: sound overrides in CrowdResponseTemplates (UnitSpecificSound block)") != std::string::npos); // 0xC516A8

	// a plain sound in the nested block is accepted and kept with the rows of the record (RW 0x825B52: the template's rows and the block at +0x1C0)
	REQUIRE(r.voice->parseCrowdResponses("CrowdResponse GoodMen\n  Threshold = 2\n    VoiceSelect = CrowdTwo\n    UnitSpecificSounds\n"
										 "      VoiceGarrison = CrowdThree\n    End\n  End\nEnd\n",
		nullptr));
	const UnitVoiceResponse::CrowdResponse &good = r.voice->crowdResponses().at("GoodMen");
	REQUIRE(good.thresholds.size() == 1);
	CHECK(good.thresholds[0].threshold == 2);
	CHECK(good.thresholds[0].voices.at("VoiceSelect") == "CrowdTwo");
	CHECK(good.thresholds[0].voices.at("VoiceGarrison") == "CrowdThree");
	CHECK(good.weight == 100); // the constructor's default, as RW 0x825EBB's record before Weight = ## is parsed
}

TEST_CASE("PORT-194 unit voice: no crowd entry below the first threshold, and no crowd sound without a response (the neighbouring behaviour)")
{
	Port194Rig r;
	REQUIRE(r.voice->parseCrowdResponses("CrowdResponse GoodMen\n  Threshold = 3\n    VoiceSelect = CrowdThree\n  End\nEnd\n", nullptr));
	const ObjectID man1 = r.make("CrowdMan"), man2 = r.make("CrowdMan");
	// two men: below the only threshold (3), so the object's own select sound alone (RW 0x825C45 answers nothing below the first key)
	CHECK(r.voice->pickAndPlay({ man1, man2 }, MSG_CREATE_SELECTED_GROUP));
	CHECK(r.sink.sounds == std::vector<std::string>{ "CrowdManSelect" });
	CHECK(r.voice->stats().crowdSounds == 0);
	// a selection of objects whose templates name no key: no vote, no crowd sound
	r.sink.clear();
	const ObjectID rock = r.make("CrowdRock");
	CHECK(r.voice->pickAndPlay({ rock }, MSG_CREATE_SELECTED_GROUP));
	CHECK(r.sink.sounds == std::vector<std::string>{ "CrowdRockSelect" });
	CHECK(r.voice->crowdResponses().size() == 1);
}
