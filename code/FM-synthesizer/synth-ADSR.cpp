
/*
	Syntherklaas FM -- ADSR envelope.

	FIXME:
		- Drive using MIDI (velocity, ADSR)
		- Use note velocity
		- Ronny's idea: note velocity scales attack and release time and possibly curvature
*/

#include "synth-global.h"
#include "synth-ADSR.h"

namespace SFM
{
	void ADSR::Start(unsigned sampleCount, const Parameters &parameters, float velocity)
	{
		SFM_ASSERT(velocity <= 1.f);

		m_sampleOffs = sampleCount;
		m_parameters = parameters;
		m_velocity = velocity;

		m_releasing = false;
	}

	void ADSR::Stop(unsigned sampleCount)
	{
		// Always use current amplitude for release
		m_parameters.sustain = ADSR::Sample(sampleCount);

		m_sampleOffs = sampleCount;
		m_releasing = true;
	}

	float ADSR::Sample(unsigned sampleCount)
	{
		/* const */ unsigned sample = sampleCount-m_sampleOffs;

		float amplitude = 0.f;

		const unsigned attack = m_parameters.attack;
		const unsigned decay = m_parameters.decay;
		const unsigned release = m_parameters.release;
		const float sustain = m_parameters.sustain;

		if (false == m_releasing)
		{
			if (sample < attack)
			{
				// Build up to full attack (linear)
				const float step = 1.f/attack;
				const float delta = sample*step;
				amplitude = delta;
				SFM_ASSERT(amplitude >= 0.f && amplitude <= 1.f);
			}
			else if (sample > attack && sample <= attack+decay)
			{
				// Decay to sustain (inverse exp.)
				sample -= attack;
				const float step = 1.f/decay;
				const float invExp = invsqrf(sample*step);
				amplitude = lerpf(1.f, sustain, invExp*invExp);
				// SFM_ASSERT(amplitude <= 1.f && amplitude >= sustain);
				SFM_ASSERT(amplitude <= 1.f /* Can't vouch for upper limit, interpolation isn't 100% accurate */);
			}
			else
			{
				return sustain;
			}
		}
		else
		{
			// Sustain level and sample offset are adjusted on NOTE_OFF (exponential)
			if (sample < release)
			{
				const float step = 1.f/release;
				const float delta = sample*step;
				amplitude = lerpf<float>(sustain, 0.f, delta*delta);
				SFM_ASSERT(amplitude >= 0.f && amplitude <= sustain);
			}
		}

		return amplitude;
	}
}
