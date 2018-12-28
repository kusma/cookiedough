
/*
	Syntherklaas FM - Yamaha DX-style voice.
*/

#include "synth-global.h"
#include "synth-DX-voice.h"

namespace SFM
{
	float DX_Voice::Sample(const Parameters &parameters)
	{
		SFM_ASSERT(true == m_enabled);

		// Process all operators top-down
		float sampled[kNumOperators];

		float mix = 0.f;
		unsigned numCarriers = 0;
		for (int iOp = kNumOperators-1; iOp >= 0; --iOp)
		{
			// Top-down
			const unsigned index = iOp;

			Operator &voiceOp = m_operators[index];

			if (true == voiceOp.enabled)
			{
				// Get modulation (from 3 sources maximum; FIXME: too much?)
				float modulation = 0.f;
				for (unsigned iMod = 0; iMod < 3; ++iMod)
				{
					const unsigned iModulator = voiceOp.modulators[iMod];
					if (-1 != iModulator)
					{
						// Sanity checks
						SFM_ASSERT(iModulator < kNumOperators);
						SFM_ASSERT(iModulator > index);
						SFM_ASSERT(true == m_operators[iModulator].enabled);

						// Get sample
						modulation += sampled[iModulator];
					}
				}

				// Get feedback
				float feedback = 0.f;
				if (voiceOp.feedback != -1)
				{
					const unsigned iFeedback = voiceOp.feedback;

					// Sanity checks
					SFM_ASSERT(iFeedback < kNumOperators);
					SFM_ASSERT(true == m_operators[iFeedback].enabled);

					feedback = m_feedback[index];
				}

				// Calculate sample
				float sample = voiceOp.oscillator.Sample(modulation) + feedback;

				// Apply envelope
				float envelope = voiceOp.envelope.Sample();
				sample = sample*envelope;

				// Store final sample for modulation and feedback
				sampled[index] = sample;

				// If carrier: mix
				if (true == voiceOp.isCarrier)
				{
					mix = mix + sample;
					++numCarriers;
				}
			}
		} 

		// Scale voice by number of carriers
		SFM_ASSERT(0 != numCarriers);
		mix /= numCarriers;

		// Store feedback
		// Actual DX7 is rougher (bit shift) (see Dexed/Hexter impl.).
	 	for (unsigned iOp = 0; iOp < kNumOperators; ++iOp)
			m_feedback[iOp] = sampled[iOp]*m_operators[iOp].feedbackAmt;

		SampleAssert(mix);

		return mix;
	}
}
