// OpenBFME. GPL-3.0.
//
// EnvironmentActor interface (editions: both RotWK 2.01 and BFME2 1.06):
// Slot 31 of BehaviorModule (offset 0x7C in secondary interface, RW 0x8AFC5F / BFME2 0x4AC5F5).
// Implemented by environment destruction actors (e.g. RainOfFireUpdate).
//
// TARGET FACTS:
// - Slot 31 query: RW 0x8AFC5F / BFME2 0x4AC5F5 (iterates modules, calls secondary vtable +0x7C).
// - EnvironmentActor interface:
//   * slot 0: release(float amount) (RW 0x5015D4 / BFME2 slot 0)
//   * slot 1: advance(float amount) (RW 0x8AF79F / BFME2 0x4AC18D)

#pragma once

class EnvironmentActor
{
public:
	virtual ~EnvironmentActor() = default;
	virtual void release(float amount) = 0;
	virtual void advance(float amount) = 0;
};

