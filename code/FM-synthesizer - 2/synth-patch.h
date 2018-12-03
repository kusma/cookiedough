
/*
	Syntherklaas FM -- "Dry" instrument patch.

	Volca (unofficial) manual: http://afrittemple.com/volca/volca_programming.pdf
*/

#pragma once

namespace SFM
{
	// Max. amount of fine tuning for fixed ratio operators
	const float kFixedFineScale = 9.772f;

	struct FM_Patch
	{
		// [0..1] unless stated otherwise
		struct Operator
		{
			// Frequency setting; related to LUT (synth-LUT.cpp)
			unsigned coarse; 
			float fine;
			float detune;

			// Fixed frequency (alters interpretation of coarse and fine)
			// "If Osc Mode is set to FIXED FREQ (HZ), COARSE adjustment is possible in four steps--1, 10, 100 and
			// 1000. FINE adjustment is possible from 1 to 9.772 times."
			bool fixed;
			
			// Linear
			float amplitude;

			// Tremolo & vibrato
			float tremolo;
			float vibrato;

			// Amp. envelope
			float opEnvA;
			float opEnvD;

			// Velocity sensitivity
			float velSens;

			// Pitch envelope influence
			float pitchEnvAmt;

			// Level scaling settings
			// This is very easy to stash into a couple of bits, but for now
			// I'll settle for inefficient verbosity (FIXME)
			unsigned levelScaleMiddle; // [0..127] MIDI
			bool leftLevelScaleExp, rightLevelScaleExp;
			float leftLevelScaleDir, rightLevelScaleDir; // -1.0 = Down, 0.0 = off, 1.0 = Up
 
			// Feedback amount
			// This only has effect when operator is used by itself or another in a feedback loop
			float feedbackAmt;
		};
	
		Operator operators[kNumOperators];

		void Reset()
		{
			for (unsigned iOp = 0; iOp < kNumOperators; ++iOp)
			{	
				Operator &OP = operators[iOp];

				// Note tone
				OP.coarse = 1;
				OP.fine = 0.f;
				OP.detune = 0.f;

				// Not fixed ratio
				OP.fixed = false;

				// Amplitude/Deoth
				OP.amplitude = 1.f;

				// No tremolo/vibrato
				OP.tremolo = 0.f;
				OP.vibrato = 0.f;

				// Flat amplitude env.
				OP.opEnvA = 0.f;
				OP.opEnvD = 0.f;

				// Fully sensitive
				OP.velSens = 1.f;

				// Full pitch env. response
				OP.pitchEnvAmt = 1.f;

				// No level scaling
				OP.levelScaleMiddle = 69;
				OP.leftLevelScaleExp = OP.rightLevelScaleExp = false;
				OP.leftLevelScaleDir = OP.rightLevelScaleDir = 0.f;

				// Zero feedback 
				OP.feedbackAmt = 0.f;
			}
		}
	};
}
