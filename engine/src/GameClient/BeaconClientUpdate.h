// OpenBFME. GPL-3.0.
// Derived from Command & Conquer Generals Zero Hour, (c) 2001-2003 Electronic Arts Inc., GPL-3.0.
//
// BeaconClientUpdate (ZH GameClient/Drawable/Update/BeaconClientUpdate.cpp; RotWK RW 0x8CDA41 .. 0x8CDCE9), lane CHAT-1: the multiplayer beacon's client
// module: the minimap pulse while the beacon is shown, and the hidden beacon of an enemy's screen.
//
// TARGET FACTS (RotWK game.dat, caveat S-001):
//   * the data (field table RW 0xC757C4, constructor RW 0x8CDA86): RadarPulseFrequency (+ 0x08, default 0x1E = 30 frames) and RadarPulseDuration (+ 0x0C,
//     default 0xF = 15 frames), both parseDurationUnsignedInt (RW 0x73A429: ceil(ms * 0.005f) logic frames). Retail's MultiplayerBeacon: 1000 / 500 ms.
//   * clientUpdate (RW 0x8CDCE9): a shown drawable makes a radar event of type 6 (the Palantir's PingBeacon) at its position lasting duration * 0.2 s
//     (TheRadar createEvent RW 0x6D98FA) when the last pulse frame + the frequency is BELOW the logic frame (strictly), and remembers the frame; a hidden
//     one is deselected and sunk (z -100) every client frame.
//   * hideBeacon (RW 0x8CDC11): the drawable hidden (+ 0x43D), its shadows off, deselected; the smoke particle system stopped.
//   * the smoke (RW 0x8CDAAE): FXParticleSystem "BeaconSmoke%6.6X" of the indicator colour, else "BeaconSmokeFFFFFF" (untinted). INFERENCE (S-5151): no
//     retail player colour has such a system and there is no BeaconSmokeFFFFFF, so retail shows no smoke; the port makes none.
// INFERENCE: the last pulse starts at the first client update's logic frame (ZH's constructor reads TheGameLogic's frame; RotWK's was not read).

#pragma once

#include "Common/Module.h"

#include <cstdint>

class ModuleFactory;
class MultiIniFieldParse;

class BeaconClientUpdateModuleData : public ModuleData
{
public:
	std::uint32_t m_framesBetweenRadarPulses = 0x1E; ///< + 0x08
	std::uint32_t m_radarPulseDuration = 0xF;        ///< + 0x0C
	static void buildFieldParse(MultiIniFieldParse &p);
};

class BeaconClientUpdate : public DrawableModule
{
public:
	BeaconClientUpdate(Thing *thing, const BeaconClientUpdateModuleData *data);
	const BeaconClientUpdateModuleData *data() const { return m_data; }

	// RW 0x8CDCE9's radar half for a shown beacon at logic frame `frame`: true when a pulse is due now (the caller makes the radar event of type 6 lasting
	// pulseSeconds()); the frame is remembered
	bool pulseDue(std::uint32_t frame);
	float pulseSeconds() const { return (float)m_data->m_radarPulseDuration * 0.2f; }
	// RW 0x8CDC11 (the drawable's hidden flag is the caller's: Drawable::setClientHidden)
	void hideBeacon() { m_hidden = true; }
	bool hidden() const { return m_hidden; }

	static void registerClass(ModuleFactory &modules);

private:
	const BeaconClientUpdateModuleData *m_data;
	std::uint32_t m_lastRadarPulse = 0; ///< + 0x10
	bool m_started = false;
	bool m_hidden = false;
};
