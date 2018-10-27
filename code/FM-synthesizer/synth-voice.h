
/*
	Syntherklaas FM - Voice.
*/

#pragma once

#include "synth-global.h"
#include "synth-carrier.h"
#include "synth-modulator.h"
#include "synth-ADSR.h."

#include <Windows.h>

namespace SFM
{
	class Voice
	{
	public:
		Voice() :
			m_enabled(false)
		{}

		bool m_enabled;

		// To be initialized manually
		Carrier m_carrier;
		Modulator m_modulator;
		Modulator m_ampMod;

		// Useful for wavetable samples
		bool m_oneShot;

		// Call before use to initialize pitched carriers and modulators (used for pitch bend)
		// Not the most elegant solution, but it doesn't depend on any delay line(s)
		Carrier m_carrierHi, m_carrierLo;
		Modulator m_modulatorHi, m_modulatorLo;
		void InitializeFeedback();

		float Sample(unsigned sampleCount, float pitchBend, float modBrightness, ADSR &envelope);
	};
}
