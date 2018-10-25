
/*
	Syntherklaas FM -- Delay line

	FIXME:
		- Offset support w/linear interpolation
		- Variable length driven by LFO
*/

#pragma once

#include "synth-modulator.h"
#include "synth-math.h"

namespace SFM
{
	class DelayLine
	{
	public:
		DelayLine(unsigned length) :
			m_readIdx(0)
,			m_writeIdx(0)
,			m_length(length)
		{
			SFM_ASSERT(length > 0 && length <= kSampleRate);
			Reset();
		}

		void Reset()
		{
			memset(m_buffer, 0, kSampleRate*sizeof(float));
		}

		void Write(float sample, float feedback)
		{
			const unsigned index = m_writeIdx++ % m_length;
			m_buffer[index] = m_buffer[index]*feedback + sample;
		}

		float Read()
		{
			const unsigned index = m_readIdx++;
			return m_buffer[index % m_length];
		}
			
	private:
		unsigned m_readIdx, m_writeIdx;
		unsigned m_length;

		alignas(16) float m_buffer[kSampleRate];
	};

	// Simple 1D matrix to blend between 3 delay lines.
	// It feels like a hack but it works.
	class DelayMatrix
	{
	public:
		DelayMatrix(unsigned lengthMid) :
			m_lineHi(lengthMid*2)
,			m_lineMid(lengthMid)
,			m_lineLo(lengthMid/2)
	{
	}

	void Write(float sample, float feedback)
	{
		m_lineHi.Write(sample, feedback);
		m_lineMid.Write(sample, feedback);
		m_lineLo.Write(sample, feedback);
	}

	float Read(float phase /* [-1..1] */)
	{
		SFM_ASSERT(fabsf(phase) <= 1.f);
		const float middle = m_lineMid.Read();

		if (phase < 0.f)
		{
			const float low = m_lineLo.Read();
			return lerpf<float>(low, middle, phase+1.f);
		}
		else
		{
			const float high = m_lineHi.Read();
			return lerpf<float>(middle, high, phase);
		}
	}

	private:
		DelayLine m_lineHi, m_lineMid, m_lineLo;
	};
}
