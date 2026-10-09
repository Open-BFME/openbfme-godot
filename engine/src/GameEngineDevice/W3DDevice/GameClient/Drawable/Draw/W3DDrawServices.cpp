// OpenBFME. GPL-3.0. See W3DDrawServices.h.

#include "GameEngineDevice/W3DDevice/GameClient/Drawable/Draw/W3DDrawServices.h"


std::vector<std::string> W3DClientRandom::unverified() const
{
	const char *which = algorithm() == RandomAlgorithm::ZH_CarryChain ? "ZH_CarryChain (ZH, BFME2 1.06 0x633EC3)" : "RotWK_GameDat_LCG (RotWK game.dat 0x6D315D)";
	return { std::string("S-093: the client RNG of a clean RotWK 2.01 is unproven; using ") + which +
		". BFME2 1.06's client wrappers (0x63404A / 0x634111) call the six-word carry generator 0x633EC3; the RotWK game.dat on this machine "
		"(community modified, S-001) has its client wrappers (0x6D32E4 / 0x6D33AB) call the LCG replacement 0x6D315D that the logic RNG shares "
		"(S-080), one patch site; a clean 2.01 most likely uses the carry generator (inference)." };
}

const HAnimClass *WW3DDrawAssets::animation(const W3DAnimationLookup &lookup, std::string *resolvedName, std::string *error)
{
	std::string lastError;
	for (const std::string &rawName : lookup.names)
	{
		// RW 0x4BD4CE: the first "#(MODEL)" is replaced by the ModelAnimationPrefix
		std::string name = rawName;
		const size_t macro = name.find("#(MODEL)");
		if (macro != std::string::npos)
		{
			name = name.substr(0, macro) + lookup.modelAnimationPrefix + name.substr(macro + 8);
		}
		if (name.empty())
		{
			continue;
		}
		std::string resolved;
		// WW3DAssetManager::Resolve_Animation is the RW 0x4BD789 candidate rule: skeleton[number].name[number], then (when numbered
		// and a skeleton is given) skeleton.name; with no skeleton the name alone, which for a HIERARCHY.ANIM name is the registry key.
		if (!m_assets.Resolve_Animation(lookup.skeleton, name, lookup.numbered, lookup.number, &resolved))
		{
			lastError = "animation " + name + " (skeleton " + (lookup.skeleton.empty() ? std::string("none") : lookup.skeleton) + ") is not registered";
			continue;
		}
		std::string getError;
		if (const HAnimClass *anim = m_assets.Get_HAnim(resolved, &getError))
		{
			if (resolvedName)
			{
				*resolvedName = resolved;
			}
			return anim;
		}
		lastError = getError;
	}
	if (error)
	{
		*error = lastError.empty() ? std::string("the AnimationName list is empty") : lastError;
	}
	return nullptr;
}
