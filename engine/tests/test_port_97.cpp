// OpenBFME. PORT-97 (issue fork issue 245) tests: TransportContain / HordeTransportContain's FadePassengerOnEnter and FadePassengerOnExit, the
// passengers' fade into and out of a transport. The two blocks are RW 0x86AAC2 .. 0x86ABCB (the end of TransportContain::onContaining,
// RW 0x86A943) and RW 0x86ADBD .. 0x86AEA1 (the end of TransportContain::onRemoving, RW 0x86ABDC); every guard and every call is read in
// docs/re/rotwk/RW_0x86AAAE.md.
//
// Synthetic data only (combattest::CombatWorld's engine INI reader and field tables): no retail / retail / mod file, value or byte. Every
// test name begins with PORT-97, so the lane's narrow filter is -tc="PORT-97*".
//
// What RW does (all of it client side: the rider's drawable fades over client frames, lane PROJ-2's DrawableFade.cpp):
//   * on enter, in this order: FadePassengerOnEnter (data + 0x16C, RW 0x86AAC2), the rider's drawable (Object + 0x84, RW 0x70E013,
//     RW 0x86AACF), EnterFadeTime (data + 0x170) != 0 (RW 0x86AAD9 .. 0x86AAEC) and FadeFilter (data + 0x168) through RW 0x7640C1 with
//     the rider and no other player (RW 0x86AAF2). Then ftol(EnterFadeTime * DAT 0xD9F624 = 0.03) client frames (RW 0x86AB63 .. 0x86AB6F)
//     and fadeIn (RW 0x670AA2) - or fadeOut (RW 0x670A50) when FadeReverse (data + 0x178) is set (RW 0x86ABB2 .. 0x86ABC6).
//   * on exit the same four guards with FadePassengerOnExit (data + 0x16D) and ExitFadeTime (data + 0x174), and NO FadeReverse read at all:
//     every exit fade is fadeIn (RW 0x86AE6D for a member, RW 0x86AE9C for the rider itself).
//   * a rider that is KindOf HORDE (template + 0x115 bit 5, RW 0x86AB08) and has a contain (RW 0x68C866, RW 0x86AB18) fades every object of
//     that contain's list (contain vslot 0x10C, RW 0x86AB48 .. 0x86AB91) instead of itself: the horde the mumak carries.
//   * the rider has already been hidden when the fade is asked for: RW's onContaining calls the contain slot 0x6C (RW 0x86B77B) before this
//     block, and the fade's own per-frame update shows the drawable again (RW 0x675AB7). The port keeps that order.
// The filter's default accepts nothing: the data constructor RW 0x86B425 sets FadeFilter with RW 0x763D11 (rule NONE, two empty masks), and
// RW 0x763543 decides rule NONE by "the include mask is not empty and is a subset of the template's KindOf" - false for an empty mask. That
// is why both retail blocks that use the fades name the filter (evilmen/mumakil.ini, the mumak's HordeTransportContain; the Isengard
// camps' TransportContain in evilfaction/structures/isengard/campsandcastles.ini): FadeFilter = ALL.

#include "doctest.h"

#include "GameClient/ClientEvents.h"
#include "CombatTestUtil.h"

#include "GameLogic/Combat/CombatNames.h"
#include "GameLogic/GameLogic.h"
#include "GameLogic/Module/TransportContain.h"
#include "GameLogic/Object/Contain/TransportContainRuntime.h"
#include "GameLogic/Object/Object.h"

#include <string>
#include <vector>

using namespace combattest;

namespace
{
// the container: `fadeLines` is the FadeFilter / FadePassengerOnEnter / EnterFadeTime / FadePassengerOnExit / ExitFadeTime / FadeReverse
// group, so a test can name the mod's shape or leave any of it out. Everything else is wide open (every side may enter, five slots) so that
// nothing but the fades decides what happens.
std::string transportObject(const std::string &fadeLines, const std::string &module)
{
	return "Object Port97Transport\n"
	       "  KindOf = STRUCTURE IMMOBILE\n"
	       "  Geometry = CYLINDER\n"
	       "  GeometryMajorRadius = 20\n"
	       "  GeometryMinorRadius = 20\n"
	       "  GeometryHeight = 30\n"
	       "  Body = ActiveBody ModuleTag_Body\n"
	       "    MaxHealth = 1000\n"
	       "  End\n"
	       "  Behavior = " + module + " ModuleTag_Contain\n"
	       "    Slots = 5\n"
	       "    AllowEnemiesInside = Yes\n"
	       "    AllowAlliesInside = Yes\n"
	       "    AllowOwnPlayerInsideOverride = Yes\n"
	       "    ObjectStatusOfContained = UNSELECTABLE\n" + fadeLines +
	       "  End\n"
	       "End\n";
}

const char *const kOthers =
	"Object Port97Rider\n"
	"  KindOf = INFANTRY SELECTABLE\n"
	"  TransportSlotCount = 1\n"
	"  Geometry = CYLINDER\n"
	"  GeometryMajorRadius = 8\n"
	"  GeometryMinorRadius = 8\n"
	"  GeometryHeight = 20\n"
	"  Body = ActiveBody ModuleTag_Body\n"
	"    MaxHealth = 100\n"
	"  End\n"
	"End\n"
	"Object Port97Horde\n"
	"  KindOf = HORDE SELECTABLE\n"
	"  TransportSlotCount = 1\n"
	"  Geometry = BOX\n"
	"  GeometryMajorRadius = 20\n"
	"  GeometryMinorRadius = 20\n"
	"  GeometryHeight = 20\n"
	"  Body = ImmortalBody ModuleTag_Body\n"
	"    MaxHealth = 1\n"
	"  End\n"
	"  Behavior = HordeContain ModuleTag_Contain\n"
	"    RankInfo = RankNumber:1 UnitType:Port97Rider Position:X:0 Y:0 Position:X:20 Y:0 Position:X:-20 Y:0\n"
	"    InitialPayload = Port97Rider 3\n"
	"  End\n"
	"End\n";

// one FadeFilter / FadePassenger* group; an empty `filter` leaves the field out (the data constructor's default)
std::string fadeGroup(const std::string &filter, bool onEnter, unsigned enterMs, bool onExit, unsigned exitMs, bool reverse)
{
	std::string s;
	if (!filter.empty())
	{
		s += "    FadeFilter = " + filter + "\n";
	}
	if (onEnter)
	{
		s += "    FadePassengerOnEnter = Yes\n    EnterFadeTime = " + std::to_string(enterMs) + "\n";
	}
	if (onExit)
	{
		s += "    FadePassengerOnExit = Yes\n    ExitFadeTime = " + std::to_string(exitMs) + "\n";
	}
	if (reverse)
	{
		s += "    FadeReverse = Yes\n";
	}
	return s;
}

struct Fade
{
	bool in = false;         ///< FADE_IN (RW 0x670AA2), false for FADE_OUT (RW 0x670A50)
	UnsignedInt frames = 0;  ///< the client frames the drawable fades over
	ObjectID object = INVALID_ID;
};

// the world and the recorder. ClientEventRecorder::objectCreated binds itself to every object made after the hooks are set
// (Object::friend_bindToClient): the port's stand-in for the drawable RW reads at Object + 0x84, which the simulation does not hold.
struct FadeWorld
{
	CombatWorld w;
	ClientEventRecorder events;

	explicit FadeWorld(const std::string &fadeLines, const std::string &module = "TransportContain")
		: w((transportObject(fadeLines, module) + kOthers).c_str()), events(*w.logic)
	{
		w.logic->setClientHooks(&events);
		w.combat().setAutoAcquireEnabled(false);
	}
	~FadeWorld() { w.logic->setClientHooks(nullptr); } // before the recorder goes, even when a REQUIRE ends the test

	Object *container() { return w.unit("Port97Transport", 'A', 200.0f, 200.0f); }
	Object *rider(char side = 'A') { return w.unit("Port97Rider", side, 210.0f, 200.0f); }
	Object *horde() { return w.unit("Port97Horde", 'A', 210.0f, 200.0f); }
	TransportContain *containOf(Object *o) { return dynamic_cast<TransportContain *>(o->getContain()); }

	void clear() { events.take(); }
	// the fade events since the last take, in order; `id` filters one object, INVALID_ID takes every one
	std::vector<Fade> fades(ObjectID id = INVALID_ID)
	{
		std::vector<Fade> out;
		for (const ClientEvent &e : events.take())
		{
			if ((e.kind == ClientEvent::FADE_IN || e.kind == ClientEvent::FADE_OUT) && (id == INVALID_ID || e.object == id))
			{
				Fade f;
				f.in = e.kind == ClientEvent::FADE_IN;
				f.frames = e.fadeFrames;
				f.object = e.object;
				out.push_back(f);
			}
		}
		return out;
	}
};
} // namespace

// The enter fade (RW 0x86AAC2 .. 0x86ABCB). The frames are ftol(EnterFadeTime * 0.03) (RW 0x86AB63 .. 0x86AB6F: fld DAT 0xD9F624, fmul
// EnterFadeTime, ftol) - 1000 ms is 30 client frames, 2000 ms is 60 - and the direction is fadeIn because FadeReverse is not set
// (RW 0x86ABB2 -> RW 0x670AA2). Without the ported block no fade is asked for at all, which is the failure this lane fixes.
TEST_CASE("PORT-97 enter: FadePassengerOnEnter fades the rider in over ftol(EnterFadeTime * 0.03) client frames (RW 0x86AAC2 .. 0x86ABCB)")
{
	struct Case
	{
		unsigned ms;
		UnsignedInt frames;
	};
	for (const Case &c : { Case{ 1000u, 30u }, Case{ 2000u, 60u } })
	{
		CAPTURE(c.ms);
		FadeWorld w(fadeGroup("ALL", true, c.ms, false, 0, false));
		Object *container = w.container();
		Object *rider = w.rider();
		REQUIRE(container != nullptr);
		REQUIRE(rider != nullptr);
		TransportContain *transport = w.containOf(container);
		REQUIRE(transport != nullptr);
		w.clear();
		REQUIRE(transport->addToContain(rider)); // RW 0x8674C2 -> onContaining RW 0x867530 -> the fade block
		const std::vector<Fade> f = w.fades(rider->getID());
		REQUIRE(f.size() == 1);
		CHECK(f[0].in);
		CHECK(f[0].frames == c.frames);
	}
}

// FadeReverse (data + 0x178, RW 0x86ABB2): the same frames, the other call (RW 0x670A50 fadeOut). The exit block has no such branch, so this
// flag moves the enter fade only.
TEST_CASE("PORT-97 enter reversed: FadeReverse turns the enter fade into fadeOut (RW 0x86ABB2 .. 0x86ABBF)")
{
	FadeWorld w(fadeGroup("ALL", true, 1000, false, 0, true));
	Object *container = w.container();
	Object *rider = w.rider();
	REQUIRE(container != nullptr);
	REQUIRE(rider != nullptr);
	TransportContain *transport = w.containOf(container);
	REQUIRE(transport != nullptr);
	w.clear();
	REQUIRE(transport->addToContain(rider));
	const std::vector<Fade> f = w.fades(rider->getID());
	REQUIRE(f.size() == 1);
	CHECK_FALSE(f[0].in);
	CHECK(f[0].frames == 30u);
}

// The exit fade (RW 0x86ADBD .. 0x86AEA1), the shape both retail blocks use: both flags on, FadeFilter = ALL, one second either way. The
// exit reads ExitFadeTime and asks for fadeIn (RW 0x86AE9C); FadeReverse is not read there, so a reversed transport still fades its
// passengers IN on the way out - the asymmetry is in the binary, and this test is what pins it.
TEST_CASE("PORT-97 exit: FadePassengerOnExit fades the rider in over ftol(ExitFadeTime * 0.03) and reads no FadeReverse (RW 0x86ADBD .. 0x86AEA1)")
{
	for (const bool reverse : { false, true })
	{
		CAPTURE(reverse);
		FadeWorld w(fadeGroup("ALL", true, 1000, true, 1000, reverse));
		Object *container = w.container();
		Object *rider = w.rider();
		REQUIRE(container != nullptr);
		REQUIRE(rider != nullptr);
		TransportContain *transport = w.containOf(container);
		REQUIRE(transport != nullptr);
		REQUIRE(transport->addToContain(rider));
		w.clear(); // the enter fade of the same rider is not what this test reads
		transport->removeFromContain(rider); // RW 0x8665BE -> onRemoving RW 0x86ABDC -> the fade block
		const std::vector<Fade> f = w.fades(rider->getID());
		REQUIRE(f.size() == 1);
		CHECK(f[0].in); // RW 0x86AE9C: fadeIn in both cases, FadeReverse or not
		CHECK(f[0].frames == 30u);
	}
}

// The horde branch (RW 0x86AB08 .. 0x86AB91): a rider that is KindOf HORDE (template + 0x115 bit 5) and has a contain fades every object of
// that contain's list (contain vslot 0x10C), each through its own drawable (RW 0x70E013), and the horde object itself fades nothing - the
// mumak's Haradrim horde, whose members are what the player sees on its back.
TEST_CASE("PORT-97 horde: a HORDE rider fades every member of its contain, not itself (RW 0x86AB08 .. 0x86AB91)")
{
	FadeWorld w(fadeGroup("ALL", true, 1000, true, 2000, false), "HordeTransportContain");
	Object *container = w.container();
	Object *horde = w.horde();
	REQUIRE(container != nullptr);
	REQUIRE(horde != nullptr);
	TransportContain *transport = w.containOf(container);
	REQUIRE(transport != nullptr);
	REQUIRE(horde->isKindOf((unsigned)CombatNames::kindOf("HORDE")));
	ContainModuleInterface *hordeContain = horde->getContain();
	REQUIRE(hordeContain != nullptr);
	const ContainModuleInterface::ContainedItemsList *members = hordeContain->getContainedItemsList();
	REQUIRE(members != nullptr);
	REQUIRE(members->size() == 3);
	w.clear();
	REQUIRE(transport->addToContain(horde));
	const std::vector<Fade> f = w.fades();
	REQUIRE(f.size() == 3); // one per member (RW 0x86AB87), not one for the horde
	for (const Fade &one : f)
	{
		CHECK(one.in);
		CHECK(one.frames == 30u);
		CHECK(one.object != horde->getID());
	}
	CHECK(w.fades(horde->getID()).empty());

	// the exit fades the same members, over ExitFadeTime's frames, and still nothing for the horde (RW 0x86AE3E .. 0x86AE79)
	w.clear();
	transport->removeFromContain(horde);
	const std::vector<Fade> out = w.fades();
	REQUIRE(out.size() == 3);
	for (const Fade &one : out)
	{
		CHECK(one.in);
		CHECK(one.frames == 60u); // ftol(2000 * 0.03)
	}
}

// The two flags (RW 0x86AAC2 / RW 0x86ADBD): with them absent the data is false and no fade is asked for - the neighbouring behaviour, a
// transport without the fields loads and unloads its passengers as it always did.
TEST_CASE("PORT-97 flags off: a transport without FadePassengerOnEnter / OnExit fades nothing (RW 0x86AAC2, RW 0x86ADBD)")
{
	FadeWorld w(fadeGroup("ALL", false, 0, false, 0, false));
	Object *container = w.container();
	Object *rider = w.rider();
	REQUIRE(container != nullptr);
	REQUIRE(rider != nullptr);
	TransportContain *transport = w.containOf(container);
	REQUIRE(transport != nullptr);
	w.clear();
	REQUIRE(transport->addToContain(rider));
	CHECK(w.fades().empty());
	transport->removeFromContain(rider);
	CHECK(w.fades().empty());
}

// The other two gates, both in the binary: FadeFilter's default (the data constructor RW 0x86B425 sets it with RW 0x763D11: rule NONE and
// two empty masks, which RW 0x763543 answers with false) and a fade time of exactly 0 (RW 0x86AAD9 .. 0x86AAEC, RW 0x86ADD5 .. 0x86ADE8).
// A passenger then pops in and out with no fade at all - and no client call is made for it either.
TEST_CASE("PORT-97 gates: the default FadeFilter and a zero fade time ask for no fade (RW 0x86AAF2, RW 0x86AAD9 .. 0x86AAEC)")
{
	for (const std::string &lines : { fadeGroup("", true, 1000, true, 1000, false), // no FadeFilter
	                                  fadeGroup("ALL", true, 0, true, 0, false) })  // FadeFilter = ALL, both times 0
	{
		CAPTURE(lines);
		FadeWorld w(lines);
		Object *container = w.container();
		Object *rider = w.rider();
		REQUIRE(container != nullptr);
		REQUIRE(rider != nullptr);
		TransportContain *transport = w.containOf(container);
		REQUIRE(transport != nullptr);
		w.clear();
		REQUIRE(transport->addToContain(rider));
		CHECK(w.fades().empty());
		transport->removeFromContain(rider);
		CHECK(w.fades().empty());
	}
}
