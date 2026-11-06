// OpenBFME unit tests: every particle and FX shader FXPlayer can build (Libraries/WWVegas/WW3D2/fxshadergen.h)
// fits Intel's 29 varying locations, and its text is byte-identical to the pre-move builders in GodotFXPlayer.cpp
// (engine/tests/data/fxshadergen_v1.txt holds SHA-256 + length recorded from that implementation).

#include "doctest.h"

#include "Common/ShaderVaryings.h"
#include "Common/Sha256.h"
#include "GameClient/FXParticleSystem.h"
#include "Libraries/WWVegas/WW3D2/fxshadergen.h"

#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <string>

TEST_CASE("fx shader budget: every sprite, mesh and GPU particle variant fits and is byte-identical")
{
	std::map<std::string, std::pair<std::string, size_t>> golden;
	{
		std::ifstream file(std::string(OPENBFME_TESTDATA_DIR) + "/fxshadergen_v1.txt");
		REQUIRE(file.good());
		std::string line;
		while (std::getline(file, line))
		{
			if (line.empty() || line[0] == '#')
			{
				continue;
			}
			std::istringstream in(line);
			std::string kind, hash;
			int shader = 0, ground = 0, soft = 0;
			size_t length = 0;
			REQUIRE((in >> kind >> shader >> ground >> soft >> hash >> length));
			golden[kind + " " + std::to_string(shader) + " " + std::to_string(ground) + " " + std::to_string(soft)] = { hash, length };
		}
	}
	REQUIRE(golden.size() == 88);

	int checked = 0;
	int worst = -1;
	std::string worstName;
	for (int shader = FXParticleSystem::PARTICLE_SHADER_NONE; shader <= FXParticleSystem::PARTICLE_SHADER_W3D_EMISSIVE; ++shader)
	{
		for (int ground = 0; ground < 2; ++ground)
		{
			for (int soft = 0; soft < 2; ++soft)
			{
				const std::string key = "sprite " + std::to_string(shader) + " " + std::to_string(ground) + " " + std::to_string(soft);
				CAPTURE(key);
				const std::string code = FXShaderGen::SpriteShader(shader, ground != 0, soft != 0);
				const auto hash = Sha256::hash(code.data(), code.size());
				const auto it = golden.find(key);
				REQUIRE(it != golden.end());
				CHECK(HexBytes(hash.data(), hash.size()) == it->second.first);
				CHECK(code.size() == it->second.second);
				const ShaderVaryings::Result count = ShaderVaryings::Count(code, key, [](const std::string &, std::string &) { return false; });
				REQUIRE(count.ok);
				const bool effectiveSoft = soft != 0 && shader != FXParticleSystem::PARTICLE_SHADER_ALPHA_TEST;
				CHECK(count.used == 15 + 2 + (effectiveSoft ? 1 : 0)); // vcol, fog_range, soft_dist
				if (count.used > worst)
				{
					worst = count.used;
					worstName = key;
				}
				++checked;
			}
		}
		for (int soft = 0; soft < 2; ++soft)
		{
			const std::string key = "mesh " + std::to_string(shader) + " 0 " + std::to_string(soft);
			CAPTURE(key);
			const std::string code = FXShaderGen::MeshShader(shader, soft != 0);
			const auto hash = Sha256::hash(code.data(), code.size());
			const auto it = golden.find(key);
			REQUIRE(it != golden.end());
			CHECK(HexBytes(hash.data(), hash.size()) == it->second.first);
			CHECK(code.size() == it->second.second);
			const ShaderVaryings::Result count = ShaderVaryings::Count(code, key, [](const std::string &, std::string &) { return false; });
			REQUIRE(count.ok);
			const bool effectiveSoft = soft != 0 && shader != FXParticleSystem::PARTICLE_SHADER_ALPHA_TEST;
			CHECK(count.used == 15 + 2 + (effectiveSoft ? 1 : 0)); // vcol, fog_range, soft_dist
			if (count.used > worst)
			{
				worst = count.used;
				worstName = key;
			}
			++checked;
		}
		for (int soft = 0; soft < 2; ++soft)
		{
			const std::string key = "gpu " + std::to_string(shader) + " 0 " + std::to_string(soft);
			CAPTURE(key);
			const std::string code = FXShaderGen::GpuShader(shader, soft != 0);
			const auto hash = Sha256::hash(code.data(), code.size());
			const auto it = golden.find(key);
			REQUIRE(it != golden.end());
			CHECK(HexBytes(hash.data(), hash.size()) == it->second.first);
			CHECK(code.size() == it->second.second);
			const ShaderVaryings::Result count = ShaderVaryings::Count(code, key, [](const std::string &, std::string &) { return false; });
			REQUIRE(count.ok);
			CHECK(count.used == 15 + 4 + (shader == FXParticleSystem::PARTICLE_SHADER_ALPHA ? 1 : 0) + (soft ? 1 : 0)); // vcol, uv0, uv1, lerp_t (+ fog_range, soft_dist)
			if (count.used > worst)
			{
				worst = count.used;
				worstName = key;
			}
			++checked;
		}
	}
	CHECK(checked == 88);
	std::fprintf(stderr, "fx shader budget: %d variants, the most is %d/%d (%s)\n", checked, worst, ShaderVaryings::kMaxVaryings, worstName.c_str());
}
