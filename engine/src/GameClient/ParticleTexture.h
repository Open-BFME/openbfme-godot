// OpenBFME. GPL-3.0.
//
// Particle texture resolution as RotWK performs it (notes fx-draw.md 1.2 / 3, fx-draw-gpu.md): the particle texture handle resolves its name lazily
// (RW 0x530d29: replace the extension, probe .dds, then .tga, then .jpg) through the path builder RW 0x477d1c (Art\CompiledTextures\<first two characters>\<name>;
// names starting `apt_` or shorter than two characters use Art\Textures\<name>). A PNG is opened only after the JPG candidate succeeded and is stored as an
// auxiliary image of the handle (+0x1c); it is never a fourth main candidate (the earlier lane note claiming one was wrong). A name that resolves to nothing
// draws the default texture, which is 1x1 white (RW 0x532962); the caller reports the miss.
//
// This is separate from the model texture resolver (textureloader.h, DDS then the written extension), which serves W3D meshes.

#pragma once

#include <functional>
#include <string>
#include <vector>

struct ParticleTextureResolution
{
	enum Format
	{
		NONE,
		DDS,
		TGA,
		JPG
	};
	bool Found = false;
	Format Kind = NONE;
	std::string Path;                ///< the file that exists
	std::string AuxPngPath;          ///< the PNG that follows a resolved JPG, when it exists (RW +0x1c); not drawn
	std::vector<std::string> Tried;
};

ParticleTextureResolution ResolveParticleTexture(const std::string &name, const std::function<bool(const std::string &)> &exists);
