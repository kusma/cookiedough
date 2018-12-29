
/*
	Syntherklaas FM -- Global lookup tables.
*/

#include "synth-global.h"
// #include "synth-LUT.h"
#include "synth-MIDI.h"

namespace SFM
{
	/*
		EG output level to modulation index table (for DX7); taken Hexter.
		I have not yet looked at what exactly this is, but it sounds good when applied literally (i.e. modulation operators).
		
		Source: https://github.com/smbolton/hexter/blob/master/src/dx7_voice_tables.c
	*/

	float g_DX7_EG_to_OL[257] = 
	{
		0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
		0.0f, 3.93186e-05f, 7.86372e-05f, 0.000117956f,
		0.000157274f, 0.000196593f, 0.00025495f, 0.000303188f,
		0.000360553f, 0.000428773f, 0.000509899f, 0.000606376f,
		0.000721107f, 0.000857545f, 0.0010198f, 0.00121275f,
		0.00132252f, 0.00144221f, 0.00171509f, 0.00187032f,
		0.0022242f, 0.0024255f, 0.00264503f, 0.00288443f,
		0.00314549f, 0.00343018f, 0.00374064f, 0.00407919f,
		0.00444839f, 0.00485101f, 0.00529006f, 0.00576885f,
		0.00629098f, 0.00686036f, 0.00748128f, 0.00815839f,
		0.00889679f, 0.00970201f, 0.0105801f, 0.0115377f,
		0.012582f, 0.0137207f, 0.0149626f, 0.0163168f,
		0.0177936f, 0.019404f, 0.0211602f, 0.0230754f,
		0.0251639f, 0.0274414f, 0.0299251f, 0.0326336f,
		0.0355871f, 0.0388081f, 0.0423205f, 0.0461508f,
		0.0503278f, 0.0548829f, 0.0598502f, 0.0652671f,
		0.0711743f, 0.0776161f, 0.084641f, 0.0923016f,
		0.100656f, 0.109766f, 0.1197f, 0.130534f,
		0.142349f, 0.155232f, 0.169282f, 0.184603f,
		0.201311f, 0.219532f, 0.239401f, 0.261068f,
		0.284697f, 0.310464f, 0.338564f, 0.369207f,
		0.402623f, 0.439063f, 0.478802f, 0.522137f,
		0.569394f, 0.620929f, 0.677128f, 0.738413f,
		0.805245f, 0.878126f, 0.957603f, 1.04427f,
		1.13879f, 1.24186f, 1.35426f, 1.47683f,
		1.61049f, 1.75625f, 1.91521f, 2.08855f,
		2.27758f, 2.48372f, 2.70851f, 2.95365f,
		3.22098f, 3.5125f, 3.83041f, 4.1771f,
		4.55515f, 4.96743f, 5.41702f, 5.9073f,
		6.44196f, 7.02501f, 7.66083f, 8.35419f,
		9.11031f, 9.93486f, 10.834f, 11.8146f,
		12.8839f, 14.05f, 15.3217f, 16.7084f,
		18.2206f, 19.8697f, 21.6681f, 23.6292f,
		23.6292f
	};

	// Sinus
	alignas(16) float g_sinLUT[kOscLUTSize];

	/*
		Depending on target platform/hardware I may want to dump this to disk/ROM.
		Also, in reality, I only neet 1/4th of it.
	*/

	void CalculateLUTs()
	{
		CalculateMIDIToFrequencyLUT();

		/* 
			Gordon-Smith oscillator (sine wave generator)
		*/

		const float frequency = 1.f;
		const float theta = k2PI*frequency/kOscLUTSize;
		const float epsilon = 2.f*sinf(theta/2.f);
		
		float N, prevN = sinf(-1.f*theta);
		float Q, prevQ = cosf(-1.f*theta);

		for (unsigned iStep = 0; iStep < kOscLUTSize; ++iStep)
		{
			Q = prevQ - epsilon*prevN;
			N = epsilon*Q + prevN;
			prevQ = Q;
			prevN = N;
			g_sinLUT[iStep] = Clamp(N);
		}
	}
}

