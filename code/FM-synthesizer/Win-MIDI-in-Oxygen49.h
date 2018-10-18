
/*
	Syntherklaas FM -- Windows (Win32) MIDI input explicitly designed for the M-AUDIO Oxygen 49.

	All values are within [0..1] range; rotaries and faders are interpolated.
	For time being this serves as the physical interface driver.
*/

#pragma once

namespace SFM
{
	unsigned WinMidi_GetNumDevices();

	bool WinMidi_Start(unsigned iDevice);
	void WinMidi_Stop();

	// Pull-style values
	float WinMidi_GetCutoff();
	float WinMidi_GetResonance();
	float WinMidi_GetFilterWetness();
};
