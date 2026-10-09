// OpenBFME infrastructure: back-to-front order of blended instances.
//
// Alpha-blended surfaces do not write depth, so overlapping instances must be drawn far to near. ZH's dynamic sort sorts by the view-space
// depth of the object's transformed centre (dx8renderer.cpp sorting path); the BFME2 sort was not separately recovered, so this uses the
// pivot position the instance's mesh hangs on. The renderer (Godot) draws MultiMesh instances in buffer order, so the order is made here.

#pragma once

#include <algorithm>
#include <cstddef>
#include <numeric>
#include <set>
#include <vector>

// depth[i] = distance of item i along the view direction (larger = farther). Returns item indices, farthest first; equal depths keep
// their original order (stable), so a scene is deterministic.
inline std::vector<int> W3D_Back_To_Front(const std::vector<float> &depth)
{
	std::vector<int> order(depth.size());
	std::iota(order.begin(), order.end(), 0);
	std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return depth[(size_t)a] > depth[(size_t)b]; });
	return order;
}

// One draw batch (a MultiMesh): the camera depths of its blended instances this frame and the render priorities of its blended surfaces.
// Godot draws a MultiMesh as one instanced draw per surface, so instances of two batches can never be interleaved: a global back-to-front
// order is impossible whenever the depths of two batches with a common priority interleave (stop S-029, docs/STOPS.md).
struct W3DSortBatch
{
	std::vector<float> Depth;
	std::set<int> Priorities;
};

// True when neither whole-batch draw order (X then Y, or Y then X) is back to front, and the batches share a render priority (different
// priorities are ordered by the renderer, not by depth). X then Y is back to front when every X is at least as far as every Y
// (minX >= maxY); Y then X when minY >= maxX. Both fail exactly when minX < maxY and minY < maxX. That covers an instance of one batch
// strictly between two of the other (A {20, 2}, B {10}) and identical ranges (A {20, 2}, B {20, 2}); batches that only touch (equal
// depth at the ends), or are separated, or hold a single common depth, can still be drawn one after the other.
inline bool W3D_Batches_Interleave(const W3DSortBatch &x, const W3DSortBatch &y)
{
	bool shared = false;
	for (int p : x.Priorities)
		if (y.Priorities.count(p)) shared = true;
	if (!shared || x.Depth.empty() || y.Depth.empty()) return false;
	const auto mx = std::minmax_element(x.Depth.begin(), x.Depth.end());
	const auto my = std::minmax_element(y.Depth.begin(), y.Depth.end());
	return *mx.first < *my.second && *my.first < *mx.second;
}

// Number of batch pairs that cannot be ordered back to front.
inline size_t W3D_Count_Interleaved_Batches(const std::vector<W3DSortBatch> &batches)
{
	size_t n = 0;
	for (size_t i = 0; i < batches.size(); ++i)
		for (size_t j = i + 1; j < batches.size(); ++j)
			if (W3D_Batches_Interleave(batches[i], batches[j])) ++n;
	return n;
}
