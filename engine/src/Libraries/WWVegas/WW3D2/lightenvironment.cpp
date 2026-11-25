// OpenBFME: faithful rebuild of The Battle for Middle-earth II: Rise of the Witch-king 2.01.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// Lane LOCAL-38 (stop S-687). See lightenvironment.h for the RW facts; each step below cites the instruction it follows.

#include "Libraries/WWVegas/WW3D2/lightenvironment.h"

#include "Libraries/WWVegas/WW3D2/assetmgr.h"

#include <cctype>

#include <cmath>

namespace
{
float clamp01(float v)
{
	// RW 0x54063B: comiss 0 > v -> 0, else v > 1 -> 1
	if (0.0f > v)
	{
		return 0.0f;
	}
	if (v > 1.0f)
	{
		return 1.0f;
	}
	return v;
}
} // namespace

float LightEnvironmentClass::New_Contribution(const Vector3 &d)
{
	// RW 0x5403ED: xmm0 = z*z; xmm0 += y*y; xmm0 += x*x
	float s = d.Z * d.Z;
	s += d.Y * d.Y;
	s += d.X * d.X;
	return s;
}

float LightEnvironmentClass::Slot_Contribution(const Vector3 &d)
{
	// RW 0x540410: xmm4 = x*x; xmm4 += z*z; xmm4 += y*y
	float s = d.X * d.X;
	s += d.Z * d.Z;
	s += d.Y * d.Y;
	return s;
}

void LightEnvironmentClass::Reset(const Vector3 &objectCenter, const Vector3 &ambient)
{
	// RW 0x53F100: LightCount = 0, ObjectCenter, OutputAmbient
	m_count = 0;
	m_center = objectCenter;
	m_outputAmbient = ambient;
}

void LightEnvironmentClass::Init_Point_Or_Spot(InputLightStruct &out, const LightEnvSourceLight &light, const Vector3 &center)
{
	// RW 0x53F654 .. 0x53F6F6: Direction = position - centre; dist = sqrt((dz*dz + dy*dy) + dx*dx); normalized when dist > 0
	out.Direction = Vector3(light.position.X - center.X, light.position.Y - center.Y, light.position.Z - center.Z);
	float l2 = out.Direction.Z * out.Direction.Z;
	l2 += out.Direction.Y * out.Direction.Y;
	l2 += out.Direction.X * out.Direction.X;
	const float dist = (float)std::sqrt((double)l2);
	if (dist > 0.0f)
	{
		const float inv = 1.0f / dist;
		out.Direction.X *= inv;
		out.Direction.Y *= inv;
		out.Direction.Z *= inv;
	}
	// RW 0x53F6FB .. 0x53F790: the far attenuation (LightClass +0xC8 bit 0); 1 without it
	float atten = 1.0f;
	const float start = light.farAttenStart;
	const float end = light.farAttenEnd;
	if (light.farAttenuation)
	{
		if (std::fabs(end - start) < 1.0e-4f) // 0xBD19E0
		{
			if (dist > start)
			{
				atten = 0.0f;
			}
		}
		else
		{
			atten = 1.0f - (dist - start) / (end - start); // RW 0x53F757 (x87; float here, header INFERENCE)
			if (0.0f > atten)
			{
				atten = 0.0f;
			}
			else if (atten > 1.0f)
			{
				atten = 1.0f;
			}
		}
	}
	// SPOT (RW 0x53F796 .. 0x53F8C4): the cone factor. No RotWK dynamic light is a spot (the pool's lights are POINT, RW 0x46D59B), so the
	// cone is not reproduced; a spot is attenuated like a point light here.
	// RW 0x53F8CA .. 0x53F966: Ambient = ambient * intensity, Diffuse = diffuse * intensity
	out.Ambient = Vector3(light.ambient.X * light.intensity, light.ambient.Y * light.intensity, light.ambient.Z * light.intensity);
	out.Diffuse = Vector3(light.diffuse.X * light.intensity, light.diffuse.Y * light.intensity, light.diffuse.Z * light.intensity);
	// RW 0x53F96B .. 0x53F9C2: point flag (type 0), centre, inner / outer radius, the unattenuated copies
	out.Point = light.type == LightEnvSourceLight::POINT;
	out.Center = light.position;
	out.InnerRadius = start;
	out.OuterRadius = end;
	out.PointAmbient = out.Ambient;
	out.PointDiffuse = out.Diffuse;
	// RW 0x53F9C5 .. 0x53F9F8: rejected unless Diffuse.Length2 ((x*x + y*y) + z*z) > (float)(int)RW 0xDD8E4C, which is 0
	float d2 = out.Diffuse.X * out.Diffuse.X;
	d2 += out.Diffuse.Y * out.Diffuse.Y;
	d2 += out.Diffuse.Z * out.Diffuse.Z;
	const float cutoff2 = 0.0f; // RW 0x53F9E8: cvtsi2ss of the int at RW 0xDD8E4C (0 in the image; its setter RW 0x53F200 has no caller)
	if (d2 > cutoff2)
	{
		// RW 0x53FA03: not rejected, both scaled by the attenuation
		out.DiffuseRejected = false;
		out.Ambient = Vector3(out.Ambient.X * atten, out.Ambient.Y * atten, out.Ambient.Z * atten);
		out.Diffuse = Vector3(out.Diffuse.X * atten, out.Diffuse.Y * atten, out.Diffuse.Z * atten);
	}
	else
	{
		// RW 0x53FA5F: rejected, the diffuse folded into the ambient and zeroed
		out.DiffuseRejected = true;
		out.Ambient = Vector3(out.Ambient.X * atten + out.Diffuse.X * atten, out.Ambient.Y * atten + out.Diffuse.Y * atten,
			out.Ambient.Z * atten + out.Diffuse.Z * atten);
		out.Diffuse = Vector3();
	}
}

void LightEnvironmentClass::Init_Directional(InputLightStruct &out, const LightEnvSourceLight &light)
{
	// RW 0x53ED30: Direction = -transform Z (the caller passes it), Ambient and Diffuse as stored, not rejected, not a point light
	out.Direction = light.direction;
	out.Ambient = light.ambient;
	out.Diffuse = light.diffuse;
	out.DiffuseRejected = false;
	out.Point = false;
}

void LightEnvironmentClass::Add_Light(const LightEnvSourceLight &light)
{
	// RW 0x540261: almost black (all three below 0.05f, the raw +0xE0 diffuse) is not accepted
	const Vector3 &raw = light.useFilterDiffuse ? light.filterDiffuse : light.diffuse;
	if (raw.X < 0.05f && raw.Y < 0.05f && raw.Z < 0.05f)
	{
		return;
	}
	InputLightStruct nl; // RW 0x54028E: zero-initialised on the stack
	nl.Id = light.id;
	if (light.type == LightEnvSourceLight::DIRECTIONAL)
	{
		Init_Directional(nl, light); // RW 0x54047D
	}
	else
	{
		Init_Point_Or_Spot(nl, light, m_center); // RW 0x54033E
	}
	// RW 0x540358: FillIntensity (+0x224) != 0 scales the diffuse by the intensity once more; the environments of renderOneObject keep the
	// ctor's 0 (RW 0x53F0F1), so this never applies in RotWK and is not reproduced.
	// RW 0x540394: the ambient is added for every accepted light
	m_outputAmbient.X += nl.Ambient.X;
	m_outputAmbient.Y += nl.Ambient.Y;
	m_outputAmbient.Z += nl.Ambient.Z;
	// RW 0x5403D6: a rejected light that is not a point light goes no further
	if (nl.DiffuseRejected && !nl.Point)
	{
		return;
	}
	// RW 0x5403E3 .. 0x54044B: the first kept slot with a strictly smaller contribution takes the new light
	const float c = New_Contribution(nl.Diffuse);
	for (int i = 0; i < m_count; ++i)
	{
		if (c > Slot_Contribution(m_lights[i].Diffuse))
		{
			// RW 0x540487 .. 0x5404B8: move the weaker ones back (the last falls off when the array is full)
			for (int j = m_count; j > i; --j)
			{
				if (j < MAX_LIGHTS)
				{
					m_lights[j] = m_lights[j - 1];
				}
			}
			m_lights[i] = nl;                                                     // RW 0x5404D0
			m_count = m_count + 1 < MAX_LIGHTS ? m_count + 1 : MAX_LIGHTS; // RW 0x5404D5 .. 0x5404E7
			return;
		}
	}
	// RW 0x54044B: appended while there is room
	if (m_count < MAX_LIGHTS)
	{
		m_lights[m_count] = nl;
		++m_count;
	}
}

void LightEnvironmentClass::Pre_Render_Update()
{
	// RW 0x540507: Calculate_Fill_Light RW 0x53FAF0 returns at once with FillIntensity 0 (RW 0x53FB03); the output lights' camera-space
	// directions (RW 0x540528 ..) are the device's business; RW 0x54063B: the ambient clamped to [0, 1]
	m_outputAmbient.X = clamp01(m_outputAmbient.X);
	m_outputAmbient.Y = clamp01(m_outputAmbient.Y);
	m_outputAmbient.Z = clamp01(m_outputAmbient.Z);
}

int LightEnvironmentClass::Directional_Count() const
{
	// RW 0x53F140: the slots whose point byte (+0x39) is 0
	int n = 0;
	for (int i = 0; i < m_count; ++i)
	{
		n += m_lights[i].Point ? 0 : 1;
	}
	return n;
}

int LightEnvironmentClass::Directional_Index(int n) const
{
	// RW 0x53F170
	for (int i = 0; i < m_count; ++i)
	{
		if (!m_lights[i].Point)
		{
			if (n == 0)
			{
				return i;
			}
			--n;
		}
	}
	return -1;
}

int LightEnvironmentClass::Point_Count() const
{
	// RW 0x53F1A0
	int n = 0;
	for (int i = 0; i < m_count; ++i)
	{
		n += m_lights[i].Point ? 1 : 0;
	}
	return n;
}

int LightEnvironmentClass::Point_Index(int n) const
{
	// RW 0x53F1D0
	for (int i = 0; i < m_count; ++i)
	{
		if (m_lights[i].Point)
		{
			if (n == 0)
			{
				return i;
			}
			--n;
		}
	}
	return -1;
}

bool W3D_Spheres_Intersect(const Vector3 &c1, float r1, const Vector3 &c2, float r2)
{
	// RW 0x46E93D: d = c1 - c2; (dx*dx + dz*dz) + dy*dy < (r1 + r2)^2
	const float dx = c1.X - c2.X;
	const float dy = c1.Y - c2.Y;
	const float dz = c1.Z - c2.Z;
	const float r = r1 + r2;
	float d2 = dx * dx;
	d2 += dz * dz;
	d2 += dy * dy;
	return r * r > d2;
}

W3DObjectLightSet W3D_Collect_Object_Lights(const Vector3 &center, float radius, const Vector3 &sceneAmbient, const LightEnvSourceLight *suns, int sunCount,
	const LightEnvSourceLight *dynamicLights, int dynamicCount)
{
	W3DObjectLightSet out;
	LightEnvironmentClass env;
	env.Reset(center, sceneAmbient); // RW 0x46FB1F
	// RW 0x46FBF6 / 0x46FB6B: the global lights, all of them (their sphere is not tested)
	for (int i = 0; i < sunCount; ++i)
	{
		LightEnvSourceLight sun = suns[i];
		sun.type = LightEnvSourceLight::DIRECTIONAL;
		sun.id = i;
		env.Add_Light(sun);
	}
	// RW 0x46FC97 .. 0x46FD30: the dynamic lights: enabled (+0x144), a POINT one only when the spheres intersect
	for (int i = 0; i < dynamicCount; ++i)
	{
		const LightEnvSourceLight &l = dynamicLights[i];
		if (!l.enabled)
		{
			continue;
		}
		if (l.type == LightEnvSourceLight::POINT && !W3D_Spheres_Intersect(center, radius, l.position, l.farAttenEnd))
		{
			continue;
		}
		LightEnvSourceLight tagged = l;
		tagged.id = sunCount + i;
		env.Add_Light(tagged);
		++out.dynamicAdded;
	}
	env.Pre_Render_Update(); // RW 0x46FD7C
	out.ambient = env.Get_Equivalent_Ambient();
	// a global light is drawn when it holds one of the four slots (the SAS DirectionalLight[] is the non-point slots, RW 0x53F170)
	unsigned kept = 0;
	for (int s = 0; s < env.Get_Light_Count(); ++s)
	{
		const LightEnvironmentClass::InputLightStruct &in = env.Get_Input_Light(s);
		if (!in.Point && in.Id >= 0 && in.Id < sunCount)
		{
			kept |= 1u << in.Id;
		}
	}
	for (int i = 0; i < sunCount; ++i)
	{
		if (!(kept & (1u << i)))
		{
			out.droppedSuns |= 1u << i;
		}
	}
	out.pointSlots = env.Point_Count();
	const int p = env.Point_Index(0); // PointLight[0]: the retail effects declare one (RW 0x54D1E8 / 0x54D135 / 0x54D26F index 0)
	if (p >= 0)
	{
		const LightEnvironmentClass::InputLightStruct &in = env.Get_Input_Light(p);
		out.hasPoint = true;
		out.pointPosition = in.Center;
		out.pointColor = in.PointDiffuse;
		out.pointRange = in.OuterRadius;
		out.pointId = in.Id - sunCount;
	}
	return out;
}

namespace
{
Vector3 xformPoint(const Matrix3D &m, float x, float y, float z)
{
	return Vector3(m.Row[0][0] * x + m.Row[0][1] * y + m.Row[0][2] * z + m.Row[0][3], m.Row[1][0] * x + m.Row[1][1] * y + m.Row[1][2] * z + m.Row[1][3],
		m.Row[2][0] * x + m.Row[2][1] * y + m.Row[2][2] * z + m.Row[2][3]);
}

bool isBoundingBoxName(const std::string &name)
{
	// DONOR BFME2 HLodUpdateObjSpaceBoundingVolumes.cpp: the part after the first '.', compared case-insensitively with "BOUNDINGBOX"
	const size_t dot = name.find('.');
	const std::string seg = dot == std::string::npos ? name : name.substr(dot + 1);
	static const char kName[] = "BOUNDINGBOX";
	if (seg.size() != sizeof(kName) - 1)
	{
		return false;
	}
	for (size_t i = 0; i < seg.size(); ++i)
	{
		if (std::toupper((unsigned char)seg[i]) != kName[i])
		{
			return false;
		}
	}
	return true;
}

// ZH sphere.h SphereClass::Add_Sphere
void addSphere(Vector3 &c, float &r, const Vector3 &sc, float sr)
{
	if (sr == 0.0f)
	{
		return;
	}
	const float dx = sc.X - c.X, dy = sc.Y - c.Y, dz = sc.Z - c.Z;
	const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
	if (dist == 0.0f)
	{
		r = r > sr ? r : sr;
		return;
	}
	const float rnew = (dist + r + sr) / 2.0f;
	if (rnew < r)
	{
		return; // the new sphere is inside
	}
	if (rnew < sr)
	{
		c = sc;
		r = sr;
		return;
	}
	const float lerp = (rnew - r) / dist;
	c = Vector3(dx * lerp + c.X, dy * lerp + c.Y, dz * lerp + c.Z);
	r = rnew;
}
} // namespace

bool W3D_Render_Object_Bounding_Sphere(const RenderObjPrototype &proto, const std::vector<Matrix3D> &pivots, Vector3 &center, float &radius)
{
	auto pivot = [&](int i) -> Matrix3D {
		return i >= 0 && (size_t)i < pivots.size() ? pivots[(size_t)i] : Matrix3D();
	};
	// the highest LOD's sub objects come first, the aggregates after (RenderObjPrototype::SubObjects): the box is searched among the former, from the end
	for (size_t k = proto.SubObjects.size(); k-- > 0;)
	{
		const RenderSubObject &sub = proto.SubObjects[k];
		if (sub.Aggregate || sub.Type != RenderSubObject::SUB_BOX || !sub.Box || !isBoundingBoxName(sub.Name))
		{
			continue;
		}
		const Matrix3D m = pivot(sub.BoneIndex);
		const W3dVectorStruct &c = sub.Box->Center;
		const W3dVectorStruct &e = sub.Box->Extent;
		center = xformPoint(m, c.X, c.Y, c.Z);
		float ext[3];
		for (int i = 0; i < 3; ++i)
		{
			ext[i] = std::fabs(m.Row[i][0] * e.X) + std::fabs(m.Row[i][1] * e.Y) + std::fabs(m.Row[i][2] * e.Z);
		}
		radius = std::sqrt(ext[0] * ext[0] + ext[1] * ext[1] + ext[2] * ext[2]);
		return true;
	}
	bool any = false;
	for (const RenderSubObject &sub : proto.SubObjects)
	{
		if (sub.Type != RenderSubObject::SUB_MESH || !sub.Mesh)
		{
			continue;
		}
		const W3dVectorStruct &sc = sub.Mesh->Header.SphCenter;
		const Vector3 c = xformPoint(pivot(sub.BoneIndex), sc.X, sc.Y, sc.Z);
		if (!any)
		{
			center = c;
			radius = sub.Mesh->Header.SphRadius;
			any = true;
		}
		else
		{
			addSphere(center, radius, c, sub.Mesh->Header.SphRadius);
		}
	}
	return any;
}
