// OpenBFME. GPL-3.0.
//
// The install's packed textures (lane UI-2, shared with the HUD by lane HUD-4): a texture name whose file the install packs under another extension, in
// RotWK's order (RW 0x530D29). See GodotPackedTexture.cpp.

#pragma once

#include <godot_cpp/classes/image.hpp>

#include <string>

class AptFileSource;

namespace godot
{
// `stem` is the archive path without extension (art/compiledtextures/<two letters>/<lower-case stem>): <stem>.dds, else <stem>.jpg with <stem>.png as its
// second stream; null when none decodes. *pairedPng: the .png was merged (S-1482)
Ref<godot::Image> loadPackedTexture(AptFileSource &source, const std::string &stem, bool *pairedPng);
} // namespace godot
