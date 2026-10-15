// OpenBFME. GPL-3.0.
// See GameNetwork/ProfileIdentity.h.

#include "GameNetwork/ProfileIdentity.h"

#include "Common/MD5.h"

#include <algorithm>
#include <sstream>

// the id is generated at configure time (cmake/EngineId.cmake) or supplied by the release pipeline: a build without one does not compile
#ifndef OPENBFME_ENGINE_ID
#error "OPENBFME_ENGINE_ID is not defined: configure through engine/CMakeLists.txt (cmake/Net.cmake) so the engine compatibility id is generated"
#endif
#ifndef OPENBFME_GIT_PROVENANCE
#define OPENBFME_GIT_PROVENANCE "none"
#endif

namespace
{
// the owned formats and rules a peer must share: bump the number of the one that changes
const char *const kFeatures = "net-wire 2, game-message 2, replay 2, lobby 2, state-hash-sections 1, lockstep-frame-model 1";
const char *const kNumeric = "IEEE binary32/binary64 through the numeric facade, canonical MXCSR 0x1F80 (round to nearest), no FMA contraction";

std::vector<std::string> lines(const std::string &t)
{
	std::vector<std::string> out;
	std::istringstream in(t);
	std::string l;
	while (std::getline(in, l))
	{
		out.push_back(l);
	}
	return out;
}
} // namespace

const char *ProfileIdentity::buildId()
{
	return OPENBFME_ENGINE_ID;
}

const char *ProfileIdentity::gitProvenance()
{
	return OPENBFME_GIT_PROVENANCE;
}

ProfileIdentity ProfileIdentity::compute(const std::vector<MountedArchive> &archives, RandomAlgorithm rng, const std::vector<std::string> &extraFeatures,
	SimulationProfile profile)
{
	std::ostringstream o;
	o << "openbfme-profile 1\n";
	o << (profile == SimulationProfile::Uber ? "profile uber\n" : "profile enhanced\n"); // lane UBER-1
	o << "engine " << OPENBFME_ENGINE_ID << "\n";
	std::vector<std::string> extra = extraFeatures;
	std::sort(extra.begin(), extra.end());
	o << "features " << kFeatures;
	for (const std::string &f : extra)
	{
		o << ", " << f;
	}
	o << "\n";
	o << "numeric " << kNumeric << "\n";
	o << "rng " << (rng == RandomAlgorithm::ZH_CarryChain ? "ZH_CarryChain" : "RotWK_GameDat_LCG") << "\n";
	o << "mods none\n";
	for (size_t i = 0; i < archives.size(); ++i)
	{
		const MountedArchive &a = archives[i];
		o << "archive " << i << " " << a.install << ":" << a.canonicalPath << " " << a.md5 << " " << a.size << "\n";
	}
	return fromText(o.str());
}

SimulationProfile ProfileIdentity::simulationProfile() const
{
	for (const std::string &l : lines(text))
	{
		if (l == "profile uber")
		{
			return SimulationProfile::Uber;
		}
	}
	return SimulationProfile::Faithful;
}

ProfileIdentity ProfileIdentity::fromText(const std::string &text)
{
	ProfileIdentity p;
	p.text = text;
	p.digest = MD5::ofBytes(text.data(), text.size());
	return p;
}

std::string ProfileIdentity::difference(const ProfileIdentity &ours, const ProfileIdentity &theirs)
{
	if (ours == theirs)
	{
		return "";
	}
	const std::vector<std::string> a = lines(ours.text), b = lines(theirs.text);
	std::string out = "profile " + ours.digest + " != " + theirs.digest;
	int shown = 0;
	bool archives = false;
	for (size_t i = 0; i < std::max(a.size(), b.size()); ++i)
	{
		const std::string x = i < a.size() ? a[i] : "(none)", y = i < b.size() ? b[i] : "(none)";
		if (x != y)
		{
			archives = archives || x.rfind("archive ", 0) == 0 || y.rfind("archive ", 0) == 0;
			if (shown < 3)
			{
				out += "; ours: '" + x + "' theirs: '" + y + "'";
			}
			++shown;
		}
	}
	if (archives)
	{
		// lane CONTRIB-1 (review): in player words. A disc install patched with EA's official patch and an All In One install keep different
		// archive lists (S-4700), so they cannot play together even though both are RotWK 2.01
		out += ". The two players' game files differ (for example one has a disc install patched with EA's official patch and the other the All In One "
			"install, or a mod): both players need the same game install";
	}
	return out;
}
