
/*
	Syntherklaas FM -- ADSR envelope.
*/

#include "synth-global.h"
#include "synth-ADSR.h"

namespace SFM
{
	const float kThreshold = 0.01f;
	const float kFlatness = 64.f;

	SFM_INLINE float ToRate(float amount, float flatness)
	{
		return kThreshold + amount * flatness*kFlatness;
	}

	void ADSR::Start(const Parameters &parameters, float velocity, float freqScale /* = 0.f */)
	{
		m_ADSR.reset();

		SFM_ASSERT(velocity <= 1.f);

		// 25% shorter attack, 75% shorter decay on max. velocity
		const float attackScale  = 1.f - 0.25f*velocity;
		const float decayScale   = 1.f - 0.75f*velocity;

		// More velocity or more frequency both mean longer release
		const float releaseScale = 1.f+freqScale+velocity;
		
		// Attack & release have a minimum to prevent clicking
		const float attack  = floorf(attackScale*parameters.attack*kSampleRate);
		const float decay   = floorf(decayScale*parameters.decay*kSampleRate);
		const float sustain = parameters.sustain;
		const float release = std::max<float>(kSampleRate/1000.f /* 1ms min. */, floorf(releaseScale*parameters.release*kSampleRate));

		// Harder touch yields more linearity
		m_ADSR.setTargetRatioA(ToRate(velocity, velocity*attackScale));
		m_ADSR.setTargetRatioDR(ToRate(velocity, velocity*decayScale /* Can use decayScale because it is reset on Stop() */));

		m_ADSR.setAttackRate(attack);
		m_ADSR.setDecayRate(decay);
		m_ADSR.setReleaseRate(release);
		m_ADSR.setSustainLevel(sustain);

		m_ADSR.gate(true);
	}

	void ADSR::Stop(float velocity)
	{
		// More touch, more linearity
		m_ADSR.setTargetRatioDR(ToRate(velocity, velocity));

		// Go into release state.
		m_ADSR.gate(false);
	}
}
