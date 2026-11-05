// OpenBFME. GPL-3.0.
//
// The particle and FX shaders FXPlayer builds as code strings (GodotFXPlayer.cpp): the pixel/vertex text of the
// CPU sprite and mesh particles and of the GPU particle draw (gpuparticle.fxo). Godot-free so the unit tests can
// generate every reachable variant and count its varyings; the device layer only assigns the text to a Shader.

#pragma once

#include <string>

namespace FXShaderGen
{

// PointGroup / GroundAligned sprites: `shader` is a FXParticleSystem::ParticleShader, `ground` the ground-aligned
// quad, `soft` the soft-particle fade (a reference-1 alpha test stays hard).
std::string SpriteShader(int shader, bool ground, bool soft);

// QuadDraw / ButterflyDraw / Streak / Lightning meshes (vertices generated on the CPU).
std::string MeshShader(int shader, bool soft);

// GpuDraw (gpuparticle.fxo technique Default); `shaderType` is the draw's ShaderType word.
std::string GpuShader(int shaderType, bool soft);

} // namespace FXShaderGen
