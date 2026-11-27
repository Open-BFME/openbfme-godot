// OpenBFME. GPL-3.0.
// SegLineRendererClass::Render's geometry (see SegLineGeometry.h). The body follows the BFME2 decomp seglinerenderer.cpp line for line; its retail
// quirks are kept and marked (the merge's next weight, the compaction leaving Parallel, the parallel merge projecting the current direction).
#include "GameClient/SegLineGeometry.h"

#include <algorithm>
#include <cassert>
#include <cmath>

namespace
{
struct V3
{
	float X = 0, Y = 0, Z = 0;
	V3() = default;
	V3(float x, float y, float z) : X(x), Y(y), Z(z) {}
	V3 operator+(const V3 &o) const { return { X + o.X, Y + o.Y, Z + o.Z }; }
	V3 operator-(const V3 &o) const { return { X - o.X, Y - o.Y, Z - o.Z }; }
	V3 operator*(float k) const { return { X * k, Y * k, Z * k }; }
	V3 operator-() const { return { -X, -Y, -Z }; }
	V3 &operator+=(const V3 &o) { X += o.X; Y += o.Y; Z += o.Z; return *this; }
	V3 &operator-=(const V3 &o) { X -= o.X; Y -= o.Y; Z -= o.Z; return *this; }
	V3 &operator*=(float k) { X *= k; Y *= k; Z *= k; return *this; }
	float Length2() const { return X * X + Y * Y + Z * Z; }
	void Normalize()
	{
		const float l2 = Length2();
		if (l2 != 0.0f)
		{
			const float inv = 1.0f / std::sqrt(l2);
			X *= inv;
			Y *= inv;
			Z *= inv;
		}
	}
};
float Dot(const V3 &a, const V3 &b)
{
	return a.X * b.X + a.Y * b.Y + a.Z * b.Z;
}
V3 Cross(const V3 &a, const V3 &b)
{
	return { a.Y * b.Z - a.Z * b.Y, a.Z * b.X - a.X * b.Z, a.X * b.Y - a.Y * b.X };
}
bool EqualWithinEpsilon(const V3 &a, const V3 &b, float e)
{
	return std::fabs(a.X - b.X) < e && std::fabs(a.Y - b.Y) < e && std::fabs(a.Z - b.Z) < e;
}
struct V4
{
	float X = 0, Y = 0, Z = 0, W = 0;
	V4 operator*(float k) const { return { X * k, Y * k, Z * k, W * k }; }
	V4 operator+(const V4 &o) const { return { X + o.X, Y + o.Y, Z + o.Z, W + o.W }; }
};

constexpr unsigned SEGLINE_CHUNK_SIZE = 128;
constexpr unsigned MAX_POINTS = 1 + SEGLINE_CHUNK_SIZE;
enum SegmentEdge
{
	TOP_EDGE = 0,
	BOTTOM_EDGE = 1,
	NUM_EDGES = 2
};
struct LineSegment
{
	V3 StartPlane;
	V3 EdgePlane[NUM_EDGES];
};
struct LineSegmentIntersection
{
	unsigned PointCount = 0;
	unsigned NextSegmentID = 0;
	V3 Direction;
	V3 Point;
	float TexV = 0;
	V4 RGBA;
	bool Fold = false;
	bool Parallel = false;
};

void emitVertex(SegLineMesh &out, const V3 &p, const V4 &c, float u, float v)
{
	out.vertices.push_back({ p.X, p.Y, p.Z });
	out.u.push_back(u);
	out.v.push_back(v);
	out.rgba.insert(out.rgba.end(), { c.X, c.Y, c.Z, c.W });
}
} // namespace

void SegLineGeometry::build(const std::vector<SegLineVec3> &input, const SegLineSettings &s, SegLineMesh &out)
{
	const unsigned num_points = (unsigned)input.size();
	if (num_points < 2)
	{
		return;
	}
	const float parallel_factor = 0.9f;
	const V4 colour{ s.color[0], s.color[1], s.color[2], s.opacity }; // subdivision_util without per-point colours
	unsigned chunk_size = SEGLINE_CHUNK_SIZE + 1; // (SEGLINE_CHUNK_SIZE >> SubdivisionLevel 0) + 1
	if (chunk_size > num_points) chunk_size = num_points;
	for (unsigned chidx = 0; chidx < num_points - 1; chidx += (chunk_size - 1))
	{
		unsigned point_cnt = std::min(num_points - chidx, chunk_size);
		V3 points[MAX_POINTS];
		float tex_v[MAX_POINTS];
		V4 diffuse[MAX_POINTS];
		float u_values[2];
		for (unsigned pidx = 0; pidx < point_cnt; pidx++)
		{
			points[pidx] = { input[chidx + pidx].x, input[chidx + pidx].y, input[chidx + pidx].z };
			diffuse[pidx] = colour;
			switch (s.mapMode)
			{
			case SegLineSettings::UNIFORM_WIDTH:
				tex_v[pidx] = 0.0f;
				break;
			default: // UNIFORM_LENGTH and TILED
				tex_v[pidx] = (float)(pidx + chidx) * s.tileFactor;
				break;
			}
		}
		u_values[0] = 0.0f;
		u_values[1] = s.mapMode == SegLineSettings::UNIFORM_LENGTH ? 0.0f : 1.0f;

		// the edge planes, the folds
		bool switch_edges = false;
		LineSegment segment[MAX_POINTS + 1];
		LineSegmentIntersection intersection[MAX_POINTS + 1][NUM_EDGES];
		const float radius = s.width * 0.5f;
		for (unsigned sidx = 1; sidx < point_cnt; sidx++)
		{
			V3 &curr_point = points[sidx - 1];
			V3 &next_point = points[sidx];
			if (EqualWithinEpsilon(curr_point, next_point, 0.0001f))
			{
				next_point.X += 0.001f;
			}
			V3 &segdir = segment[sidx].StartPlane;
			segdir = next_point - curr_point;
			segdir.Normalize();
			V3 nearest = curr_point + segdir * -Dot(segdir, curr_point);
			V3 offset = Cross(segdir, nearest);
			offset.Normalize();
			V3 top = curr_point + offset * radius;
			V3 bottom = curr_point + offset * -radius;
			V3 top_normal = Cross(top, segdir);
			top_normal.Normalize();
			segment[sidx].EdgePlane[TOP_EDGE] = top_normal;
			V3 bottom_normal = Cross(segdir, bottom);
			bottom_normal.Normalize();
			segment[sidx].EdgePlane[BOTTOM_EDGE] = bottom_normal;
			if (sidx > 1)
			{
				V3 prev_plane = Cross(points[sidx - 2], curr_point);
				prev_plane.Normalize();
				V3 curr_plane = Cross(curr_point, next_point);
				curr_plane.Normalize();
				const bool fold = Dot(prev_plane, curr_plane) < 0.0f;
				if (fold)
				{
					switch_edges = !switch_edges;
				}
				intersection[sidx][TOP_EDGE].Fold = fold;
				intersection[sidx][BOTTOM_EDGE].Fold = fold;
			}
			if (switch_edges)
			{
				segment[sidx].EdgePlane[TOP_EDGE] = -bottom_normal;
				segment[sidx].EdgePlane[BOTTOM_EDGE] = -top_normal;
			}
		}
		const unsigned numsegs = point_cnt - 1;
		unsigned num_intersections[NUM_EDGES] = { point_cnt, point_cnt };

		// the sentinels and the end directions
		for (int e = 0; e < NUM_EDGES; ++e)
		{
			LineSegmentIntersection &z = intersection[0][e];
			z.PointCount = 0;
			z.NextSegmentID = 0;
			z.Direction = { 1, 0, 0 };
			z.Point = {};
			z.TexV = 0.0f;
			z.RGBA = {};
			z.Fold = true;
			z.Parallel = false;
			LineSegmentIntersection &f = intersection[1][e];
			f.PointCount = 1;
			f.NextSegmentID = 1;
			f.Point = points[0];
			f.TexV = tex_v[0];
			f.RGBA = diffuse[0];
			f.Fold = true;
			f.Parallel = false;
		}
		V3 top, bottom, start_pl;
		{
			const V3 &first_point = points[0];
			const V3 *first_plane = segment[1].EdgePlane;
			top = first_point - first_plane[TOP_EDGE] * Dot(first_plane[TOP_EDGE], first_point);
			top.Normalize();
			intersection[1][TOP_EDGE].Direction = top;
			bottom = first_point - first_plane[BOTTOM_EDGE] * Dot(first_plane[BOTTOM_EDGE], first_point);
			bottom.Normalize();
			intersection[1][BOTTOM_EDGE].Direction = bottom;
			V3 segdir = points[1] - points[0];
			segdir.Normalize();
			start_pl = Cross(top, bottom);
			start_pl.Normalize();
			const V3 pl = Dot(segdir, start_pl) > 0.0f ? start_pl : -start_pl;
			segment[0].StartPlane = segment[0].EdgePlane[TOP_EDGE] = segment[0].EdgePlane[BOTTOM_EDGE] = pl;
			segment[1].StartPlane = segment[0].StartPlane;
		}
		{
			const unsigned last_isec = num_intersections[TOP_EDGE];
			for (int e = 0; e < NUM_EDGES; ++e)
			{
				LineSegmentIntersection &l = intersection[last_isec][e];
				l.PointCount = 1;
				l.NextSegmentID = numsegs + 1;
				l.Point = points[point_cnt - 1];
				l.TexV = tex_v[point_cnt - 1];
				l.RGBA = diffuse[point_cnt - 1];
				l.Fold = true;
				l.Parallel = false;
			}
			const V3 &last_point = points[point_cnt - 1];
			const V3 *last_plane = segment[numsegs].EdgePlane;
			top = last_point - last_plane[TOP_EDGE] * Dot(last_plane[TOP_EDGE], last_point);
			top.Normalize();
			intersection[last_isec][TOP_EDGE].Direction = top;
			bottom = last_point - last_plane[BOTTOM_EDGE] * Dot(last_plane[BOTTOM_EDGE], last_point);
			bottom.Normalize();
			intersection[last_isec][BOTTOM_EDGE].Direction = bottom;
			V3 segdir = points[point_cnt - 1] - points[point_cnt - 2];
			segdir.Normalize();
			start_pl = Cross(top, bottom);
			start_pl.Normalize();
			const V3 pl = Dot(segdir, start_pl) > 0.0f ? start_pl : -start_pl;
			segment[numsegs + 1].StartPlane = segment[numsegs + 1].EdgePlane[TOP_EDGE] = segment[numsegs + 1].EdgePlane[BOTTOM_EDGE] = pl;
		}

		// adjacent edge planes intersected; near parallel ones projected
		for (unsigned iidx = 2; iidx < num_intersections[TOP_EDGE]; iidx++)
		{
			const V3 &midpoint = points[iidx - 1];
			for (int e = 0; e < NUM_EDGES; ++e)
			{
				LineSegmentIntersection &is = intersection[iidx][e];
				is.PointCount = 1;
				is.NextSegmentID = iidx;
				is.Point = midpoint;
				is.TexV = tex_v[iidx - 1];
				is.RGBA = diffuse[iidx - 1];
			}
			for (int e = 0; e < NUM_EDGES; ++e)
			{
				LineSegmentIntersection &is = intersection[iidx][e];
				const float vdp = Dot(segment[iidx - 1].EdgePlane[e], segment[iidx].EdgePlane[e]);
				if (std::fabs(vdp) < parallel_factor)
				{
					is.Direction = Cross(segment[iidx - 1].EdgePlane[e], segment[iidx].EdgePlane[e]);
					is.Direction.Normalize();
					if (Dot(is.Direction, midpoint) < 0.0f)
					{
						is.Direction = -is.Direction;
					}
					is.Parallel = false;
				}
				else
				{
					V3 pl = vdp > 0.0f ? segment[iidx - 1].EdgePlane[e] + segment[iidx].EdgePlane[e] : segment[iidx - 1].EdgePlane[e] - segment[iidx].EdgePlane[e];
					pl.Normalize();
					is.Direction = midpoint - pl * Dot(pl, midpoint);
					is.Direction.Normalize();
					is.Parallel = true;
				}
			}
			start_pl = Cross(intersection[iidx][TOP_EDGE].Direction, intersection[iidx][BOTTOM_EDGE].Direction);
			start_pl.Normalize();
			segment[iidx].StartPlane = Dot(segment[iidx].StartPlane, start_pl) > 0.0f ? start_pl : -start_pl;
		}

		// compatible intersections merged until a pass merges nothing
		if (s.mergeIntersections)
		{
			bool merged = true;
			while (merged)
			{
				merged = false;
				for (int edge = TOP_EDGE; edge <= BOTTOM_EDGE; ++edge)
				{
					const unsigned num_isects = num_intersections[edge];
					unsigned iidx_r, iidx_w;
					for (iidx_r = 1, iidx_w = 1; iidx_r < num_isects; iidx_r++, iidx_w++)
					{
						LineSegmentIntersection *curr_int = &intersection[iidx_r][edge];
						LineSegmentIntersection *next_int = &intersection[iidx_r + 1][edge];
						LineSegmentIntersection *write_int = &intersection[iidx_w][edge];
						LineSegmentIntersection *prev_int = &intersection[iidx_w - 1][edge];
						LineSegment *next_seg = &segment[next_int->NextSegmentID];
						LineSegment *curr_seg = &segment[curr_int->NextSegmentID];
						LineSegment *prev_seg = &segment[prev_int->NextSegmentID];
						while ((!next_int->Fold && Dot(curr_int->Direction, next_seg->StartPlane) > 0.0f && Dot(curr_int->Direction, next_seg->EdgePlane[edge]) > 0.0f) ||
							(!curr_int->Fold && Dot(next_int->Direction, -curr_seg->StartPlane) > 0.0f && Dot(next_int->Direction, prev_seg->EdgePlane[edge]) > 0.0f))
						{
							const unsigned new_count = curr_int->PointCount + next_int->PointCount;
							const float oo_new_count = 1.0f / (float)new_count;
							const float curr_factor = oo_new_count * (float)curr_int->PointCount;
							const float next_factor = oo_new_count * (float)curr_int->PointCount; // retail: the current count in both weights
							const V3 new_point = curr_int->Point * curr_factor + next_int->Point * next_factor;
							const float new_tex_v = curr_int->TexV * curr_factor + next_int->TexV * next_factor;
							const V4 new_diffuse = curr_int->RGBA * curr_factor + next_int->RGBA * next_factor;
							bool new_parallel;
							V3 new_direction;
							const float vdp = Dot(prev_seg->EdgePlane[edge], next_seg->EdgePlane[edge]);
							if (std::fabs(vdp) < parallel_factor)
							{
								new_direction = Cross(prev_seg->EdgePlane[edge], next_seg->EdgePlane[edge]);
								new_direction.Normalize();
								if (Dot(new_direction, new_point) < 0.0f)
								{
									new_direction = -new_direction;
								}
								new_parallel = false;
							}
							else
							{
								V3 pl = vdp > 0.0f ? prev_seg->EdgePlane[edge] + next_seg->EdgePlane[edge] : prev_seg->EdgePlane[edge] - next_seg->EdgePlane[edge];
								pl.Normalize();
								if (curr_int->Parallel)
								{
									// retail VA 0x00594634 .. 0x0059467F: projects the existing direction
									new_direction = curr_int->Direction - pl * Dot(pl, curr_int->Direction);
									new_direction.Normalize();
								}
								else
								{
									new_direction = Cross(curr_seg->EdgePlane[edge], pl);
									new_direction.Normalize();
								}
								new_parallel = true;
							}
							if (s.mergeAbortFactor > 0.0f)
							{
								const float abort_dist = radius * s.mergeAbortFactor;
								const float abort_dist2 = abort_dist * abort_dist;
								const V3 diff_curr = curr_int->Point - new_direction * Dot(curr_int->Point, new_direction);
								if (diff_curr.Length2() > abort_dist2) break;
								const V3 next_curr = next_int->Point - new_direction * Dot(next_int->Point, new_direction);
								if (next_curr.Length2() > abort_dist2) break;
							}
							merged = true;
							curr_int->Direction = new_direction;
							curr_int->Parallel = new_parallel;
							curr_int->Point = new_point;
							curr_int->TexV = new_tex_v;
							curr_int->RGBA = new_diffuse;
							curr_int->PointCount = new_count;
							curr_int->NextSegmentID = next_int->NextSegmentID;
							curr_int->Fold = curr_int->Fold || next_int->Fold;
							num_intersections[edge]--;
							iidx_r++;
							if (iidx_r == num_isects)
							{
								break;
							}
							next_int = &intersection[iidx_r + 1][edge];
							next_seg = &segment[next_int->NextSegmentID];
						}
						// retail compaction copies Fold and leaves Parallel at its slot
						write_int->PointCount = curr_int->PointCount;
						write_int->NextSegmentID = curr_int->NextSegmentID;
						write_int->Point = curr_int->Point;
						write_int->TexV = curr_int->TexV;
						write_int->RGBA = curr_int->RGBA;
						write_int->Direction = curr_int->Direction;
						write_int->Fold = curr_int->Fold;
					}
					if (iidx_r == num_isects)
					{
						LineSegmentIntersection *write_int = &intersection[iidx_w][edge];
						const LineSegmentIntersection *curr_int = &intersection[iidx_r][edge];
						write_int->PointCount = curr_int->PointCount;
						write_int->NextSegmentID = curr_int->NextSegmentID;
						write_int->Point = curr_int->Point;
						write_int->TexV = curr_int->TexV;
						write_int->RGBA = curr_int->RGBA;
						write_int->Direction = curr_int->Direction;
						write_int->Fold = curr_int->Fold;
					}
				}
			}
		}

		// the vertices and triangles
		const std::uint16_t base = (std::uint16_t)out.vertices.size();
		unsigned vidx = 0;
		const float uo = s.uvOffset[0], vo = s.uvOffset[1];
		{
			const V3 &top_dir = intersection[1][TOP_EDGE].Direction;
			top = top_dir * Dot(points[0], top_dir);
			bottom = intersection[1][BOTTOM_EDGE].Direction;
			const float k = Dot(points[0], bottom);
			bottom.Z *= k; // retail scales Z first (an operator* temporary)
			bottom.Y *= k;
			bottom.X *= k;
			emitVertex(out, top, intersection[1][TOP_EDGE].RGBA, u_values[0] + uo, intersection[1][TOP_EDGE].TexV + vo);
			emitVertex(out, bottom, intersection[1][BOTTOM_EDGE].RGBA, u_values[1] + uo, intersection[1][BOTTOM_EDGE].TexV + vo);
			vidx = 2;
		}
		auto tri = [&](unsigned i, unsigned j, unsigned k) {
			out.indices.push_back((std::uint16_t)(base + i));
			out.indices.push_back((std::uint16_t)(base + j));
			out.indices.push_back((std::uint16_t)(base + k));
		};
		unsigned last_top_vidx = 0, last_bottom_vidx = 1, top_int_idx = 1, bottom_int_idx = 1, pidx = 0;
		unsigned residual_top_points = intersection[1][TOP_EDGE].PointCount;
		unsigned residual_bottom_points = intersection[1][BOTTOM_EDGE].PointCount;
		unsigned delta = std::min(residual_top_points, residual_bottom_points) - 1;
		residual_top_points -= delta;
		residual_bottom_points -= delta;
		pidx += delta;
		for (;;)
		{
			if (residual_top_points == 1 && residual_bottom_points == 1)
			{
				tri(last_top_vidx, last_bottom_vidx, vidx);
				tri(last_bottom_vidx, vidx + 1, vidx);
				last_top_vidx = vidx;
				last_bottom_vidx = vidx + 1;
				top_int_idx++;
				bottom_int_idx++;
				residual_top_points = intersection[top_int_idx][TOP_EDGE].PointCount;
				residual_bottom_points = intersection[bottom_int_idx][BOTTOM_EDGE].PointCount;
				pidx++;
				const V3 &top_dir = intersection[top_int_idx][TOP_EDGE].Direction;
				top = top_dir * Dot(points[pidx], top_dir);
				const V3 &bottom_dir = intersection[bottom_int_idx][BOTTOM_EDGE].Direction;
				bottom = bottom_dir * Dot(points[pidx], bottom_dir);
				if (s.expand > 0.0f) // retail VA 0x00594FF3 .. 0x00595080 (this + 0x1C)
				{
					V3 d = top - bottom;
					d *= s.expand;
					top += d;
					bottom -= d;
				}
				emitVertex(out, top, intersection[top_int_idx][TOP_EDGE].RGBA, u_values[0] + uo, intersection[top_int_idx][TOP_EDGE].TexV + vo);
				emitVertex(out, bottom, intersection[bottom_int_idx][BOTTOM_EDGE].RGBA, u_values[1] + uo, intersection[bottom_int_idx][BOTTOM_EDGE].TexV + vo);
				vidx += 2;
			}
			else if (residual_top_points > 1)
			{
				tri(last_top_vidx, last_bottom_vidx, vidx);
				last_bottom_vidx = vidx;
				residual_top_points--;
				bottom_int_idx++;
				residual_bottom_points = intersection[bottom_int_idx][BOTTOM_EDGE].PointCount;
				pidx++;
				const V3 &bottom_dir = intersection[bottom_int_idx][BOTTOM_EDGE].Direction;
				bottom = bottom_dir * Dot(points[pidx], bottom_dir);
				emitVertex(out, bottom, intersection[bottom_int_idx][BOTTOM_EDGE].RGBA, u_values[1] + uo, intersection[bottom_int_idx][BOTTOM_EDGE].TexV + vo);
				vidx++;
			}
			else
			{
				tri(last_top_vidx, last_bottom_vidx, vidx);
				last_top_vidx = vidx;
				residual_bottom_points--;
				top_int_idx++;
				residual_top_points = intersection[top_int_idx][TOP_EDGE].PointCount;
				pidx++;
				const V3 &top_dir = intersection[top_int_idx][TOP_EDGE].Direction;
				top = top_dir * Dot(points[pidx], top_dir);
				emitVertex(out, top, intersection[top_int_idx][TOP_EDGE].RGBA, u_values[0] + uo, intersection[top_int_idx][TOP_EDGE].TexV + vo);
				vidx++;
			}
			delta = std::min(residual_top_points, residual_bottom_points) - 1;
			residual_top_points -= delta;
			residual_bottom_points -= delta;
			pidx += delta;
			if ((top_int_idx >= num_intersections[TOP_EDGE] && residual_top_points == 1) || (bottom_int_idx >= num_intersections[BOTTOM_EDGE] && residual_bottom_points == 1))
			{
				break;
			}
		}
	}
}
