// OpenBFME. GPL-3.0.
//
// The FXList block's parse-time name checks, answered from a world's own TheAudio / TheEva tables (PORT-330, stop S-190).
//
// RotWK resolves both names through GLOBAL tables, not through the FXList store:
//   * a `Sound` nugget's `Name` goes RW 0x73B217 -> RW 0x73AA94: "NoSound" is cleared first (the stricmp at the function's start, through
//     0x42DC9F), anything else asks TheAudio (RW 0xDE42FC) for the event through findAudioEventInfo (slot 0x12C) and throws code 3,
//     `Invalid Sound '%s'` (RW 0xC2500C), for an unknown name (the throw at RW 0x73AB1A .. 0x73AB3F);
//   * an `EvaEventOwner` / `EvaEventAlly` / `EvaEventEnemy` name goes RW 0x5DE588 -> RW 0x5DE0D8, TheEva's name -> index lookup (RW 0xDE3670):
//     "None" is -1 without a lookup (the same 0x42DC9F pre-test at RW 0x5DE0E3), a known name its index, and an unknown one -1, which
//     RW 0x5DE14E turns into a code-3 `Expected a recognized Eva event name or 'None'; got '%s'` (the string at RW 0xBF2678).
// Both subsystem inits run before TheFXListStore's own (GameEngine::init, RW 0x63AF2D onward: TheAudio RW 0x4538DA, TheEva RW 0x5DE837), so retail
// can never read an FXList block before the two tables exist. A world that owns the AudioIniState answers for them.
//
// The services hold the world, not the state's AudioIniState: RetailObjectWorld::reload replaces its State (and with it the audio tables), and the
// client's FX player keeps the pointer across reloads. Every answer reads the current state, so a reloaded world answers from the new tables.

#include "GameClient/FXList.h"

#include "Common/Audio/AudioIni.h"
#include "GameLogic/Object/RetailObjectWorld.h"

namespace
{
class WorldFXListParseServices : public FXListParseServices
{
public:
	explicit WorldFXListParseServices(const RetailObjectWorld &world) : m_world(world) {}

	// RW 0x73AA94: TheAudio's table holds the event under its exact name (findAudioEventInfo slot 0x12C, a case-sensitive key, so this is the same
	// predicate the weapon / contain parsers already ask: RetailObjectWorld::audioEventExists, RW 0x73AB45 / 0x73AAA9 practice)
	bool audioEventExists(const std::string &name) override { return m_world.audioEventExists(name); }

	// RW 0x5DE0D8's base-table answer: TheEva + 0x48. The port's other parse-time Eva lookups (ContainParseHooks::evaEventIndex,
	// UpgradeParseServices::evaEventIndex) take the same table; a name only a map.ini being loaded defines is not visible here (the overlay table
	// RW 0x5DE0D8 picks for load type 2 is not passed through the services: no retail FXList block relies on it).
	int evaEventIndex(const std::string &name) override { return m_world.audio().eva.findIndex(name, false); }

private:
	const RetailObjectWorld &m_world;
};
} // namespace

std::unique_ptr<FXListParseServices> MakeWorldFXListParseServices(const RetailObjectWorld &world)
{
	return std::unique_ptr<FXListParseServices>(new WorldFXListParseServices(world));
}
