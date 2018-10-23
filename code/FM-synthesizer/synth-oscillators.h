
/*
	Syntherklaas FM -- Oscillators.

	The idea is to, in due time, precalculate all these and use samplers during actual synthesis as is practically the case already
	for everything that depends on the sinus LUT.
*/

#pragma once

#include "synth-global.h"
#include "synth-random.h"

namespace SFM
{
	/*
		Available waveforms.
	*/

	enum Waveform
	{
		kSine,
		/* BLIT forms */
		kSoftSaw,
		kSoftSquare,
		/* Straight forms */
		kDigiSaw,
		kDigiSquare,
		kTriangle,
		kWhiteNoise,
		kPinkNoise
	};

	/*
		Sinus oscillator.
	*/

	SFM_INLINE float oscSine(float phase) { return lutsinf(phase); }

	/*
		Straight up sawtooth, square & triangle (will alias in most circumstances).
	*/

	SFM_INLINE float oscDigiSaw(float phase)
	{
		return fmodf(phase/kOscPeriod, 1.f)*2.f - 1.f;
	}

	SFM_INLINE float oscDigiSquare(float phase)
	{
		return lutsinf(phase) > 0.f ? 1.f : -1.f;
	}

	SFM_INLINE float oscTriangle(float phase)
	{
		return asinf(lutsinf(phase))*(2.f/kPI);
	}

	/*
		Band-limited saw and square (BLIT).
	*/

	const float kHarmonicsPrecHz = kAudibleLowHz;

	SFM_INLINE unsigned GetCarrierHarmonics(float frequency)
	{
		SFM_ASSERT(frequency >= 20.f);
		return 40; // FIXME (documented)
	}

	SFM_INLINE float oscSoftSaw(float phase, unsigned numHarmonics) 
	{ 
		phase *= -1.f;
		float harmonicPhase = phase;

		float signal = 0.f;

		for (unsigned iHarmonic = 0; iHarmonic < numHarmonics; ++iHarmonic)
		{
			signal += lutsinf(harmonicPhase)/(1.f+iHarmonic);
			harmonicPhase += phase;
		}

 		const float ampMul = 2.f/kPI;
 		signal *= ampMul;

		return signal;
	}

	SFM_INLINE float oscSoftSquare(float phase, unsigned numHarmonics) 
	{ 
		float harmonicPhase = phase;

		float signal = 0.f;

		for (unsigned iHarmonic = 0; iHarmonic < numHarmonics; iHarmonic += 2)
		{
			signal += lutsinf(harmonicPhase)/(1.f+iHarmonic);
			harmonicPhase += phase*2.f;
		}

 		const float ampMul = 4.f/kPI;
		signal *= ampMul;


		return signal;
	}

	/*
		Noise oscillator(s).
	*/

	SFM_INLINE float oscWhiteNoise(float phase)
	{
		return lutnoisef(phase + rand() /* Without this we'll definitely hear a pattern */);
	}

	// Paul Kellet's approximation to pink noise; basically just a filter
	// Taken from: http://www.firstpr.com.au/dsp/pink-noise/
	SFM_INLINE float oscPinkNoise(float phase)
	{
		const float white = oscWhiteNoise(phase);

		static float b0 = 0.f, b1 = 0.f, b2 = 0.f, b3 = 0.f, b4 = 0.f, b5 = 0.f, b6 = 0.f;
		static float pink = 0.5f;

		b0 = 0.99886f*b0 + white*0.0555179f;
		b1 = 0.99332f*b1 + white*0.0750759f; 
		b2 = 0.96900f*b2 + white*0.1538520f; 
		b3 = 0.86650f*b3 + white*0.3104856f; 
		b4 = 0.55000f*b4 + white*0.5329522f; 
		b5 = -0.7616f*b5 - white*0.0168980f; 
		
		// This is a bit of a judgement call but I prefer clearly hearing different noise over keeping
		// the not-too-exact spectral properties
		pink = lowpassf(b0+b1+b2+b3+b4+b5+b6 + white*0.5362f, pink, kGoldenRatio); 

		b6 = white*0.115926f;
		
		return pink;
   }
}
