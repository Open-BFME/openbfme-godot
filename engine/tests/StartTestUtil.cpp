// OpenBFME unit tests. GPL-3.0. See StartTestUtil.h.

#include "StartTestUtil.h"

#include "GameEngineDevice/Win32Device/Common/Win32BIGFileSystem.h"

namespace starttest
{
namespace
{
// loads a fixture world; false when the install variables are unset
bool load(Shared &s)
{
	s.mount = retailtest::pureMount();
	if (s.mount && s.mount->fs)
	{
		s.world = std::make_unique<RetailObjectWorld>(*s.mount->fs);
		if (!s.world->load(&s.error))
		{
			s.world.reset();
		}
		else if (!MapObjectGameData::load(*s.mount->fs, s.options, &s.error) || !MapObjectGameData::loadPlayerTemplates(*s.mount->fs, s.options, &s.error) ||
			!MapCreationHooks::load(*s.mount->fs, s.options.creationScripts, &s.error) || !GameLogicSettingsLoader::load(*s.mount->fs, s.settings, &s.error))
		{
			s.world.reset();
		}
	}
	return s.mount != nullptr;
}
} // namespace

Shared *shared()
{
	static Shared s;
	static bool built = false;
	static bool available = false;
	if (!built)
	{
		built = true;
		available = load(s);
	}
	return available ? &s : nullptr;
}

Shared *peerWorld()
{
	static Shared s;
	static bool built = false;
	static bool available = false;
	if (!built)
	{
		built = true;
		available = load(s);
	}
	return available ? &s : nullptr;
}

std::unique_ptr<Shared> freshWorld()
{
	auto s = std::make_unique<Shared>();
	return load(*s) ? std::move(s) : nullptr;
}
} // namespace starttest
