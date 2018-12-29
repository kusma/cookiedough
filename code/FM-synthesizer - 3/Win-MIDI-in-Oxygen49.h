
/*
	Syntherklaas FM -- Windows (Win32) MIDI input explicitly designed for the M-AUDIO Oxygen 49.

	- Oxygen 49 must be set to PROGRAM 01.
	- For time being this serves as the (hardcoded) physical interface driver (w/BeatStep counterpart).
	- All values are within [0..1] range unless clearly stated otherwise.
	- It should be pretty straightforward to modify the implementation to fit another instrument.
*/

#pragma once

#include "synth-parameters.h"
#include "synth-stateless-oscillators.h"

namespace SFM
{
	bool WinMidi_Oxygen49_Start();
	void WinMidi_Oxygen49_Stop();

	/*
		Pull-style values, all normalized [0..1]
	*/

	// Operator envelope
	float WinMidi_GetOpAttack(unsigned iOp);
	float WinMidi_GetOpDecay(unsigned iOp);
	float WinMidi_GetOpSustain(unsigned iOp);
	float WinMidi_GetOpRelease(unsigned iOp);
	float WinMidi_GetOpAttackLevel(unsigned iOp);

	// Operator frequency
	bool  WinMidi_GetOpFixed(unsigned iOp);
	float WinMidi_GetOpCoarse(unsigned iOp);
	float WinMidi_GetOpFine(unsigned iOp);
	float WinMidi_GetOpDetune(unsigned iOp);

	// Output level
	float WinMidi_GetOpOutput(unsigned iOp);

	// Velocity sensitivity
	float WinMidi_GetOpVelSens(unsigned iOp);

	// Feedback
	float WinMidi_GetFeedback(unsigned iOp);
}
