
/*
	Syntherklaas FM -- 4-pole filter like the MOOG ladder.

	Credits:
		- https://github.com/ddiakopoulos/MoogLadders/tree/master/src
		- The paper by S. D'Angelo & V. V�lim�ki (2013).

	Filter does not require oversampling.

	FIXME: 
		- More than enough to optimize here if need be (SIMD, maybe), but let us wait for a target platform.
		- Consider using more precision or at least loook at possible precision issues.
*/

#ifndef _SFM_SYNTH_MOOG_LADDER_H_
#define _SFM_SYNTH_MOOG_LADDER_H_

#include "synth-global.h"

namespace SFM
{
	namespace MOOG
	{
		// Thermal voltage (26 milliwats at room temperature)
		// FIXME: offer different temperatures by a control next to cutoff and resonance?
		const float kVT = 0.312f;

		static float s_cutGain;
		static float s_resonance;
		static float s_drive;

		// Feedback values & deltas.
		static float s_V[4];
		static float s_dV[4];
		static float s_tV[4];

		void SetCutoff(float cutoff)
		{
			SFM_ASSERT(cutoff >= 0.f && cutoff <= 1000.f); 

			const float angular = (kPI*cutoff)/kSampleRate;
			const float gain = 4.f*kPI*kVT*cutoff*(1.f-angular)/(1.f+angular);
	
			s_cutGain = gain;
		}

		void SetResonance(float resonance)
		{
			SFM_ASSERT(resonance >= 0.f && resonance <= kPI);
			s_resonance = resonance;
		}

		void SetDrive(float drive)
		{
			s_drive = drive;
		}

		void ResetParameters()
		{
			SetCutoff(1000.f);
			SetResonance(0.1f);
			SetDrive(1.f);
		}

		void ResetFeedback()
		{
			for (unsigned iPole = 0; iPole < 4; ++iPole)
				s_V[iPole] = s_dV[iPole] = s_tV[iPole] = 0.f;
		}

		void Filter(float *pDest, unsigned numSamples, float wetness)
		{
			float dV0, dV1, dV2, dV3;

			const float &resonance = s_resonance;
			const float &cutGain = s_cutGain;
			const float &drive = s_drive;
			
			float *V  = s_V;
			float *dV = s_dV;
			float *tV = s_tV;

			// FIXME: find a more elegant, faster way to express this (SIMD, more LUTs)
			for (unsigned iSample = 0; iSample < numSamples; ++iSample)
			{
				// Fetch dry sample
				const float dry = pDest[iSample];

				dV0 = -cutGain * (fast_tanhf((dry*drive + resonance*V[3]) / (2.f*kVT)) + tV[0]);
				V[0] += (dV0 + dV[0]) / (2.f*kSampleRate);
				dV[0] = dV0;
				tV[0] = fast_tanhf(V[0]/(2.f*kVT));
			
				dV1 = cutGain * (tV[0] - tV[1]);
				V[1] += (dV1 + dV[1]) / (2.f*kSampleRate);
				dV[1] = dV1;
				tV[1] = fast_tanhf(V[1]/(2.f*kVT));
			
				dV2 = cutGain * (tV[1] - tV[2]);
				V[2] += (dV2 + dV[2]) / (2.f*kSampleRate);
				dV[2] = dV2;
				tV[2] = fast_tanhf(V[2]/(2.f*kVT));
			
				dV3 = cutGain * (tV[2] - tV[3]);
				V[3] += (dV3 + dV[3]) / (2.f*kSampleRate);
				dV[3] = dV3;
				tV[3] = fast_tanhf(V[3]/(2.f*kVT));

				// Round off and mix with dry (FIXME: atanf() LUT)
				pDest[iSample] = lerpf(dry, V[3], wetness); 

				// If this happens we're fucked
				SFM_ASSERT(false == IsNAN(V[3]));
			}
		}
	}
}

#endif // _SFM_SYNTH_MOOG_LADDER_H_
