// OpenBFME. GPL-3.0.
// The AI state machine's return codes as the binary uses them (RW state machine; one definition for the weapon code and the AI).
#pragma once

enum StateReturnType
{
	STATE_CONTINUE = 0,
	STATE_SUCCESS = -1,
	STATE_FAILURE = -2
};
