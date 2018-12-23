
/*
	Syntherklaas FM -- Windows (Win32) MIDI input explicitly designed for the Arturia BeatStep; an addition to the Oxygen 49.
*/

#pragma once

#include "synth-oscillator.h"
#include "synth-vowel-filter.h"

namespace SFM
{
	bool WinMidi_BeatStep_Start();
	void WinMidi_BeatStep_Stop();

	/*
		Pull-style values, all normalized [0..1]
	*/

	// Operator vibrato control
	float WinMidi_GetOperatorVibrato();

	// Delay
	float WinMidi_GetDelayWet();
	float WinMidi_GetDelayRate();

	// Filter
	float WinMidi_GetFilterA();
	float WinMidi_GetFilterD();
	float WinMidi_GetFilterS();
	float WinMidi_GetFilterDrive();

	// Note jitter
	float WinMidi_GetNoteJitter();

	// Pitch env.
	float WinMidi_GetPitchA();
	float WinMidi_GetPitchD();
	float WinMidi_GetPitchL();

	// Vowel filter
	float WinMidi_GetVowelWet();
	float WinMidi_GetVowelBlend();
	VowelFilter::Vowel WinMidi_GetVowel();
}
