// OpenBFME. GPL-3.0.
//
// SparseMatchFinder: port of ZH GameEngine/Include/Common/SparseMatchFinder.h (DONOR). The BFME2 binary instantiates it as
// SparseMatchFinder<ModelConditionInfo, BitFlags<N>> (BFME2 symbol findBestInfo 0x0033D46A, countInverseIntersection 0x0033AE53,
// spec w3d-and-draw.md 4.3). The RotWK TARGET does NOT select draw states with it: its model state and animation state
// lookups (RW 0x4B4379 / 0x4B4443) take the first state in definition order whose conditions are all in the query, which is what
// W3DModelDrawModuleData::findBestInfo / findBestAnimationState implement. This class is kept, tested, as the donor reference and
// for any BFME2-era data that needs best-match selection; no draw module uses it.
//
// findBestInfoSlow (ZH SparseMatchFinder.h:123-186), ported exactly:
//   for each matchable in vector order, for each of its "yes" condition sets from LAST to FIRST:
//     yes   = |bits & set|                 (countConditionIntersection)
//     extra = |set \ bits|                 (countConditionInverseIntersection(bits, set) = bits.countInverseIntersection(set))
//     take it when yes > bestYes, or yes >= bestYes and extra < bestExtra     (initial bestYes 0, bestExtra 999)
// Ties keep the earlier candidate. A candidate is never rejected for requiring bits the caller lacks; it only loses the
// tiebreak. The result is cached per exact bit set; a miss (no matchable at all) is not cached and returns nullptr
// (ZH DEBUG_ASSERTCRASHes there; here the caller decides).
//
// The cache holds pointers into the vector it was filled from: use one finder per vector and clear() it whenever the vector
// changes.
//
// MATCHABLE needs getConditionsYesCount() and getNthConditionsYes(i).

#pragma once

#include <cstddef>
#include <map>
#include <vector>

template <typename MATCHABLE, typename BITSET>
class SparseMatchFinder
{
public:
	void clear() { m_bestMatches.clear(); }

	const MATCHABLE *findBestInfo(const std::vector<MATCHABLE> &v, const BITSET &bits) const
	{
		auto it = m_bestMatches.find(bits);
		if (it != m_bestMatches.end() && it->second != nullptr)
		{
			return it->second;
		}
		const MATCHABLE *info = findBestInfoSlow(v, bits);
		if (info != nullptr)
		{
			m_bestMatches[bits] = info;
		}
		return info;
	}

	// The uncached rule, exposed so tests can compare it with the cache.
	static const MATCHABLE *findBestInfoSlow(const std::vector<MATCHABLE> &v, const BITSET &bits)
	{
		const MATCHABLE *result = nullptr;
		int bestYesMatch = 0;
		int bestYesExtraneousBits = 999;
		for (typename std::vector<MATCHABLE>::const_iterator it = v.begin(); it != v.end(); ++it)
		{
			for (int i = it->getConditionsYesCount() - 1; i >= 0; --i)
			{
				const BITSET &yesFlags = it->getNthConditionsYes(i);
				const int yesMatch = bits.countIntersection(yesFlags);
				const int yesExtraneousBits = bits.countInverseIntersection(yesFlags);
				if ((yesMatch > bestYesMatch) || (yesMatch >= bestYesMatch && yesExtraneousBits < bestYesExtraneousBits))
				{
					result = &(*it);
					bestYesMatch = yesMatch;
					bestYesExtraneousBits = yesExtraneousBits;
				}
			}
		}
		return result;
	}

private:
	mutable std::map<BITSET, const MATCHABLE *> m_bestMatches;
};
