// OpenBFME. GPL-3.0.
//
// StlportAlgorithm: the STLport (SGI) sort and heap algorithms RotWK's game.dat was built with, for the simulation code whose result depends on how
// equal elements end up ordered.  The host's std::sort / std::push_heap are not used there: libstdc++ partitions differently and MSVC differs again.
// The functions take a std::vector and index ranges (no iterator arithmetic in templates: the sim audit's dependent-arithmetic rule).  Lane WOTR-2.
//
// TARGET FACTS (RotWK game.dat, caveat S-001): the autoresolve battle's unit sort RW 0x8FE222 is `__introsort_loop(first, last, depth = 2 * lg(n))`
// (RW 0x8FDE42) then `__final_insertion_sort` (RW 0x8FD11D: threshold 16 elements, __insertion_sort on the first 16, __unguarded_insertion_sort on the
// rest; __linear_insert RW 0x8FC5C1 moves the whole prefix when comp(val, *first)); lg(n) is `for (k = 0; n != 1; n >>= 1) ++k` (RW 0x8FE22E).
// The attacker heap is push_heap RW 0x8FBE70 -> __push_heap RW 0x8FB983 (hole moves up while comp(parent, value)) and pop_heap RW 0x8FC4BE ->
// __adjust_heap RW 0x8FBF9F.
// DONOR FACTS: SGI STL 3.3 stl_algo.h / stl_heap.h (the code STLport 4.x ships): __introsort_loop picks the median of first, middle and last - 1 and
// partitions with __unguarded_partition; partial_sort (make_heap + sort_heap) at depth 0; __adjust_heap sinks the hole to a leaf then __push_heap.

#pragma once

#include <cstddef>
#include <utility>
#include <vector>

namespace Stlport
{

typedef std::ptrdiff_t Index;

template <class T, class Comp>
void unguardedLinearInsert(std::vector<T> &v, Index last, T val, Comp comp)
{
	Index next = last - 1;
	while (comp(val, v[(size_t)next]))
	{
		v[(size_t)last] = v[(size_t)next];
		last = next;
		--next;
	}
	v[(size_t)last] = val;
}

template <class T, class Comp>
void linearInsert(std::vector<T> &v, Index first, Index last, Comp comp)
{
	T val = v[(size_t)last];
	if (comp(val, v[(size_t)first]))
	{
		for (Index i = last; i != first; --i)
		{
			v[(size_t)i] = v[(size_t)(i - 1)];
		}
		v[(size_t)first] = val;
	}
	else
	{
		unguardedLinearInsert(v, last, val, comp);
	}
}

template <class T, class Comp>
void insertionSort(std::vector<T> &v, Index first, Index last, Comp comp)
{
	if (first == last)
	{
		return;
	}
	for (Index i = first + 1; i != last; ++i)
	{
		linearInsert(v, first, i, comp);
	}
}

template <class T, class Comp>
void unguardedInsertionSort(std::vector<T> &v, Index first, Index last, Comp comp)
{
	for (Index i = first; i != last; ++i)
	{
		unguardedLinearInsert(v, i, v[(size_t)i], comp);
	}
}

// the heap functions work on v[first .. first + len)
template <class T, class Comp>
void pushHeapHole(std::vector<T> &v, Index first, Index holeIndex, Index topIndex, T value, Comp comp)
{
	Index parent = (holeIndex - 1) / 2;
	while (holeIndex > topIndex && comp(v[(size_t)(first + parent)], value))
	{
		v[(size_t)(first + holeIndex)] = v[(size_t)(first + parent)];
		holeIndex = parent;
		parent = (holeIndex - 1) / 2;
	}
	v[(size_t)(first + holeIndex)] = value;
}

template <class T, class Comp>
void adjustHeap(std::vector<T> &v, Index first, Index holeIndex, Index len, T value, Comp comp)
{
	const Index topIndex = holeIndex;
	Index secondChild = 2 * holeIndex + 2;
	while (secondChild < len)
	{
		if (comp(v[(size_t)(first + secondChild)], v[(size_t)(first + secondChild - 1)]))
		{
			--secondChild;
		}
		v[(size_t)(first + holeIndex)] = v[(size_t)(first + secondChild)];
		holeIndex = secondChild;
		secondChild = 2 * (secondChild + 1);
	}
	if (secondChild == len)
	{
		v[(size_t)(first + holeIndex)] = v[(size_t)(first + secondChild - 1)];
		holeIndex = secondChild - 1;
	}
	pushHeapHole(v, first, holeIndex, topIndex, value, comp);
}

// push_heap over the whole vector: v[0 .. n - 1) is a heap, v[n - 1] the new element
template <class T, class Comp>
void pushHeap(std::vector<T> &v, Comp comp)
{
	const Index n = (Index)v.size();
	pushHeapHole(v, 0, n - 1, 0, v[(size_t)(n - 1)], comp);
}

// pop_heap over the whole vector: the top moves to the back
template <class T, class Comp>
void popHeap(std::vector<T> &v, Comp comp)
{
	const Index last = (Index)v.size() - 1;
	T value = v[(size_t)last];
	v[(size_t)last] = v[0];
	adjustHeap(v, 0, 0, last, value, comp);
}

template <class T, class Comp>
void makeHeap(std::vector<T> &v, Index first, Index len, Comp comp)
{
	if (len < 2)
	{
		return;
	}
	Index parent = (len - 2) / 2;
	for (;;)
	{
		adjustHeap(v, first, parent, len, v[(size_t)(first + parent)], comp);
		if (parent == 0)
		{
			return;
		}
		--parent;
	}
}

template <class T, class Comp>
void sortHeap(std::vector<T> &v, Index first, Index len, Comp comp)
{
	while (len > 1)
	{
		const Index last = first + len - 1;
		T value = v[(size_t)last];
		v[(size_t)last] = v[(size_t)first];
		adjustHeap(v, first, 0, len - 1, value, comp);
		--len;
	}
}

template <class T, class Comp>
void partialSort(std::vector<T> &v, Index first, Index middle, Index last, Comp comp)
{
	const Index len = middle - first;
	makeHeap(v, first, len, comp);
	for (Index i = middle; i < last; ++i)
	{
		if (comp(v[(size_t)i], v[(size_t)first]))
		{
			T value = v[(size_t)i];
			v[(size_t)i] = v[(size_t)first];
			adjustHeap(v, first, 0, len, value, comp);
		}
	}
	sortHeap(v, first, len, comp);
}

template <class T, class Comp>
const T &median(const T &a, const T &b, const T &c, Comp comp)
{
	if (comp(a, b))
	{
		if (comp(b, c))
		{
			return b;
		}
		if (comp(a, c))
		{
			return c;
		}
		return a;
	}
	if (comp(a, c))
	{
		return a;
	}
	if (comp(b, c))
	{
		return c;
	}
	return b;
}

template <class T, class Comp>
Index unguardedPartition(std::vector<T> &v, Index first, Index last, T pivot, Comp comp)
{
	for (;;)
	{
		while (comp(v[(size_t)first], pivot))
		{
			++first;
		}
		--last;
		while (comp(pivot, v[(size_t)last]))
		{
			--last;
		}
		if (!(first < last))
		{
			return first;
		}
		std::swap(v[(size_t)first], v[(size_t)last]);
		++first;
	}
}

const Index kThreshold = 16;

template <class T, class Comp>
void introsortLoop(std::vector<T> &v, Index first, Index last, Index depthLimit, Comp comp)
{
	while (last - first > kThreshold)
	{
		if (depthLimit == 0)
		{
			partialSort(v, first, last, last, comp);
			return;
		}
		--depthLimit;
		const Index mid = first + (last - first) / 2;
		T pivot = median(v[(size_t)first], v[(size_t)mid], v[(size_t)(last - 1)], comp);
		const Index cut = unguardedPartition(v, first, last, pivot, comp);
		introsortLoop(v, cut, last, depthLimit, comp);
		last = cut;
	}
}

template <class T, class Comp>
void finalInsertionSort(std::vector<T> &v, Index first, Index last, Comp comp)
{
	if (last - first > kThreshold)
	{
		insertionSort(v, first, first + kThreshold, comp);
		unguardedInsertionSort(v, first + kThreshold, last, comp);
	}
	else
	{
		insertionSort(v, first, last, comp);
	}
}

// std::sort(v.begin(), v.end(), comp) as STLport does it
template <class T, class Comp>
void sort(std::vector<T> &v, Comp comp)
{
	const Index n = (Index)v.size();
	if (n == 0)
	{
		return;
	}
	Index k = 0;
	for (Index m = n; m != 1; m >>= 1)
	{
		++k;
	}
	introsortLoop(v, 0, n, k * 2, comp);
	finalInsertionSort(v, 0, n, comp);
}

} // namespace Stlport
