// OpenBFME. GPL-3.0.
//
// MilesMix: the left / right channel gains the Miles Sound System RotWK 2.01 ships (MSS 6.6g: mss32.dll, mss\msssoft.m3d) gives a voice,
// so the Godot device reproduces retail's stereo image and loudness law instead of its own pan.
//
// TARGET FACTS (lane AUDIO-5, read from the binaries; addresses are the DLLs' preferred image bases, mss32 0x21100000, msssoft 0x22400000):
//   * RotWK picks the 3D provider in RW 0x45FBB9: IDirectSound::GetSpeakerConfig (through AIL_get_DirectSound_info) maps the Windows
//     speaker setup to a Miles speaker type (+0xBF0: stereo / mono / direct out -> 0 two speakers, headphones -> 1, quad -> 3,
//     surround -> 2, 5.1 -> 4, 7.1 -> 5); "Dolby Surround" is opened only when that type is not 0 AND the audio LOD allows Dolby
//     (RW 0x450DC2, GameLOD.ini AudioLOD AllowDolby: High Yes, Low No); otherwise "Miles Fast 2D Positional Audio". The 3D rolloff
//     factor is set to 0 (RW 0x45FD77: Miles adds no distance attenuation of its own; the game's linear falloff decides the volume,
//     which Miles still raises to the power 5/3 and mutes beyond its maximum distance, below) and
//     AIL_set_3D_speaker_type gets the mapped type (headphones only through a debug toggle, RW 0x452BD3, never called).
//     The game has no speaker option of its own; this model is the Fast 2D provider (a stereo Windows speaker setup), stop S-1930.
//   * Coordinates go to Miles with z negated and up = (0, 0, -1) (RW 0x4525D5 listener orientation, 0x4525FF listener position,
//     0x45C101 / 0x45BD3C sample position), i.e. a left-handed frame in which the game's own right is Miles' right.
//   * mss32 AIL_set_3D_sample_distances (0x2111FD40) passes the larger of its two distances as the maximum: RotWK passes
//     (MinRange, 2 x MaxRange) (RW prep3DSample 0x45C0D8, the 2 at 0xBD889C), or 2 x GlobalMaxRange for a global event or one whose
//     MinVolume exceeds MinSampleVolume (RW 0x45C084-0x45C0B2). AudioEventInfo +0x94 is MinRange, +0x98 MaxRange (field rows 0xBEFEF0 /
//     0xBEFF00).
//   * msssoft Fast 2D per-sample update (0x22401060): beyond the maximum distance the sample is set silent (provider preference 1,
//     0x224132A0, default on); volume' = volume ^ (5/3) (_CIpow, the double 1.6666666269302368 at 0x2240E4F0); the rolloff term is 1
//     (factor 0); n = the unit vector listener -> sound; pan p = acos(n . R) / pi (R = up x face, 0x22401B13; 1/pi as the float at
//     0x2240E4E4), 0.5 when the distance is at most 0.0001; a sound behind the face (n . face < 0) is scaled by 0.75 (0x2240E4F8);
//     AIL_set_sample_volume_levels(left = p * volume', right = (1 - p) * volume').
//   * mss32 AIL_set_sample_volume_pan (internal 0x2112AD60), also behind AIL_set_stream_volume_pan (0x2111AFA0): volume' = volume ^ (5/3);
//     pan exactly 0.5 -> both levels volume' x 0.8122522 (2^-0.3, the float at 0x2114C018); otherwise left = (1 - pan)^0.3 x volume',
//     right = pan^0.3 x volume'. RotWK keeps a 2D sample's pan (RW 0x45B61A reads it back before setting the volume) and never sets
//     one, so 2D samples and streams play at Miles' default pan 0.5 (mss32's sample initialisation 0x2112AB00).
//   * the mixer (0x2112C226) scales the samples by level x master x 2048 (linear): the levels above are the channel gains.
// Not modelled (stop S-1930): the Dolby Surround provider (same left / right law without the 0.75, plus its "FB Pan" matrix filter on
// front / back), EAX, Miles' integer gain quantisation (2048 steps) and its x87 rounding.

#pragma once

#include "Common/INIDataTypes.h"

struct AudioEventInfo;
struct AudioSettings;

struct MilesChannelGains
{
	float left = 0.0f;
	float right = 0.0f;
};

// mss32 AIL_set_sample_volume_pan: a 2D sample or a stream at `volume` (0..1) and Miles pan (0 left .. 1 right, 0.5 centre).
MilesChannelGains milesSampleGains(float volume, float pan = 0.5f);

// msssoft "Miles Fast 2D Positional Audio": a 3D sample at `volume` (the game's effective volume, AIL_set_3D_sample_volume) and
// `soundPos`, heard by the listener at `listenerPos` facing `listenerFace` (the game's coordinates, z up; the face is horizontal and
// of unit length), silent beyond `maxDistance`.
MilesChannelGains milesFast2DGains(float volume, const Coord3D &listenerPos, const Coord3D &listenerFace, const Coord3D &soundPos, float maxDistance);

// The maximum distance Miles silences a 3D sample beyond (RW prep3DSample 0x45C047 through mss32's ordering, see above).
float milesMaxDistance(const AudioEventInfo &info, float minVolume, const AudioSettings &settings);
