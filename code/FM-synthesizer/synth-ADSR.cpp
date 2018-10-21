
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
	void ADSR::Start(unsigned sampleCount, float velocity)
	{
		SFM_ASSERT(velocity <= 1.f);

		m_sampleOffs = sampleCount;
		m_velocity = velocity;

		m_attack = kSampleRate/8;
		m_decay = kSampleRate/4;
		m_release = kSampleRate/4;

		m_sustain = 0.9f;

		m_releasing = false;
	}

	void ADSR::Stop(unsigned sampleCount)
	{
		// Always use current amplitude for release
		m_sustain = ADSR::Sample(sampleCount);

		m_sampleOffs = sampleCount;
		m_releasing = true;
	}

	float ADSR::Sample(unsigned sampleCount)
	{
		/* const */ unsigned sample = sampleCount-m_sampleOffs;

		float amplitude = 0.f;

		if (false == m_releasing)
		{
			if (sample <= m_attack)
			{
				// Build up to full attack (linear)
				const float step = 1.f/m_attack;
				const float delta = sample*step;
				amplitude = delta;
				SFM_ASSERT(amplitude >= 0.f && amplitude <= 1.f);
			}
			else if (sample > m_attack && sample <= m_attack+m_decay)
			{
				// Decay to sustain (cubic)
				sample -= m_attack;
				const float step = 1.f/m_decay;
				const float delta = powf(sample*step, 3.f);
				amplitude = lerpf(1.f, m_sustain, delta);
				SFM_ASSERT(amplitude <= 1.f && amplitude >= m_sustain);
			}
			else
			{
				return m_sustain;
			}
		}
		else
		{
			// Sustain level and sample offset are adjusted on NOTE_OFF (inverse exp.)
			if (sample <= m_release)
			{
				const float step = 1.f/m_release;
				const float delta = sample*step;
				const float invExp = 1.f - (1.f-delta)*(1.f-delta);
				amplitude = lerpf<float>(m_sustain, 0.f, delta*delta);
				SFM_ASSERT(amplitude >= 0.f && amplitude <= m_sustain);
			}
		}

		return amplitude;
	}
}
