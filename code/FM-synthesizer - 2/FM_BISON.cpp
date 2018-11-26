
/*
	'FM. BISON' by syntherklaas.org, a subsidiary of visualizers.nl
*/

// Shut it, MSVC.
#define _CRT_SECURE_NO_WARNINGS

// C++11
#include <mutex>
#include <shared_mutex>
#include <atomic>
#include <deque>

#include "FM_BISON.h"

#include "synth-midi.h"
#include "synth-parameters.h"
#include "synth-ringbuffer.h"
#include "synth-math.h"
#include "synth-LUT.h"
#include "synth-DX-voice.h"
#include "synth-filter.h"
#include "synth-delay-line.h"

// Win32 MIDI input (M-AUDIO Oxygen 49 & Arturia BeatStep)
#include "Win-MIDI-in-Oxygen49.h"
#include "Win-MIDI-in-BeatStep.h"

// SDL2 audio output
#include "SDL2-audio-out.h"

namespace SFM
{
	/*
		Global sample counts.
	*/

 	static unsigned s_sampleCount = 0;
	static unsigned s_sampleOutCount = 0;

	/*
		Ring buffer.
	*/

	static std::mutex s_ringBufMutex;
	static FIFO s_ringBuf;

	/*
		State.

		May only be altered after acquiring lock.
	*/

	static std::mutex s_stateMutex;

	struct VoiceRequest
	{
		unsigned *pIndex;
		Waveform form;
		float frequency;
		float velocity;
	};

	struct VoiceReleaseRequest
	{
		unsigned index;
		float velocity;
	};

	// Voice req.
	static std::deque<VoiceRequest> s_voiceReq;
	static std::deque<VoiceReleaseRequest> s_voiceReleaseReq;

	// Parameters
	static Parameters s_parameters;

	// Voices
	static DX_Voice s_DXvoices[kMaxVoices];
	static unsigned s_active = 0;

	// Master filters
	static ButterworthFilter s_hardFilters[kMaxVoices];
	static TeemuFilter s_softFilters[kMaxVoices];
//	static ImprovedMOOGFilter s_softFilters[kMaxVoices];

	// Delay effect
	static DelayLine s_delayLine;
	static Oscillator s_delayLFO;
	
	/*
		Voice API.
	*/

	void TriggerVoice(unsigned *pIndex /* Will receive index to use with ReleaseVoice() */, Waveform form, float frequency, float velocity)
	{
		SFM_ASSERT(true == InAudibleSpectrum(frequency));

		std::lock_guard<std::mutex> lock(s_stateMutex);
		
		VoiceRequest request;
		request.pIndex = pIndex;
		request.form = form;
		request.frequency = frequency;
		request.velocity = velocity;
		s_voiceReq.push_front(request);

		// At this point there is no voice yet
		if (nullptr != pIndex)
			*pIndex = -1;
	}

	void ReleaseVoice(unsigned index, float velocity)
	{
		SFM_ASSERT(-1 != index);

		std::lock_guard<std::mutex> lock(s_stateMutex);

		VoiceReleaseRequest request; 
		request.index = index;
		request.velocity = velocity;

		s_voiceReleaseReq.push_front(request);
	}

	/*
		Voice logic.
	*/

	SFM_INLINE float CalcOpFreq(float frequency, FM_Patch::Operator &patchOp)
	{
//		frequency *= g_modRatioLUT[patchOp.coarse];

		frequency *= g_CM[patchOp.coarse][1];
		frequency *= patchOp.detune;
		frequency *= patchOp.fine;

		return frequency;
	}

	static void InitializeDXVoice(const VoiceRequest &request, unsigned iVoice)
	{
		SFM_ASSERT(false == s_stateMutex.try_lock());

		DX_Voice &voice = s_DXvoices[iVoice];
		voice.Reset();
		
		float frequency = request.frequency;

		// Randomize note frequency little if wanted (in (almost) entire cents)
		const float jitterAmt = kMaxNoteJitter*s_parameters.m_noteJitter;
		const int cents = int(ceilf(oscWhiteNoise()*jitterAmt));

		float jitter = 1.f;
		if (0 != cents)
			jitter = powf(2.f, (cents*0.01f)/12.f);
		
		FloatAssert(jitter);
		frequency *= jitter;

		// Each has a distinct effect (linear, exponential, inverse exponential)
		const float velocity       = request.velocity;
		const float velocityExp    = velocity*velocity;
		const float velocityInvExp = Clamp(invsqrf(velocity));
		
		// Master/global
		const float masterAmp = velocity*kMaxVoiceAmp;
		const float masterFreq = frequency;

		const float modDepth = s_parameters.modDepth*velocityInvExp;

		FM_Patch &patch = s_parameters.patch;

#if 1

		/*
			Test algorithm: single carrier & modulator
		*/

		// Carrier #1
		voice.m_operators[0].enabled = true;
		voice.m_operators[0].modulators[0] = 1;
		voice.m_operators[0].isCarrier = true;
		voice.m_operators[0].oscillator.Initialize(request.form, CalcOpFreq(masterFreq, patch.operators[0]), masterAmp*patch.operators[0].amplitude);

		// Modulator #1
		voice.m_operators[1].enabled = true;
		voice.m_operators[1].oscillator.Initialize(kSine, CalcOpFreq(masterFreq, patch.operators[1]), modDepth*patch.operators[1].amplitude);

		/*
			End of Algorithm
		*/

#endif

#if 0

		/*
			Test algorithm: Volca FM algorithm #5

			Verdict: works well if treated right, a keeper!
			QA: OK!
		*/

		// Carrier #1
		voice.m_operators[0].enabled = true;
		voice.m_operators[0].modulators[0] = 3;
		voice.m_operators[0].isCarrier = true;
		voice.m_operators[0].oscillator.Initialize(request.form, CalcOpFreq(masterFreq, patch.operators[0]), masterAmp*patch.operators[0].amplitude);

		// Carrier #2
		voice.m_operators[1].enabled = true;
		voice.m_operators[1].modulators[0] = 4;
		voice.m_operators[1].isCarrier = true;
		voice.m_operators[1].oscillator.Initialize(request.form, CalcOpFreq(masterFreq, patch.operators[1]), masterAmp*patch.operators[1].amplitude);

		// Carrier #3
		voice.m_operators[2].enabled = true;
		voice.m_operators[2].modulators[0] = 5;
		voice.m_operators[2].isCarrier = true;
		voice.m_operators[2].oscillator.Initialize(request.form, CalcOpFreq(masterFreq, patch.operators[2]), masterAmp*patch.operators[2].amplitude);

		// Modulator #1
		voice.m_operators[3].enabled = true;
		voice.m_operators[3].feedback = -1;
		voice.m_operators[3].oscillator.Initialize(kSine, CalcOpFreq(masterFreq, patch.operators[3]), modDepth*patch.operators[3].amplitude);

		// Modulator #2
		voice.m_operators[4].enabled = true;
		voice.m_operators[4].feedback = -1;
		voice.m_operators[4].oscillator.Initialize(kSine, CalcOpFreq(masterFreq, patch.operators[4]), modDepth*patch.operators[4].amplitude);

		// Modulator #3
		voice.m_operators[5].enabled = true;
		voice.m_operators[5].feedback = 5;
		voice.m_operators[5].oscillator.Initialize(kSine, CalcOpFreq(masterFreq, patch.operators[5]), modDepth*patch.operators[5].amplitude);

		/*
			End of Algorithm
		*/

#endif

#if 0

		/*
			Test algorithm: Kylian's algorithm #4
			Verdict: ...
		*/

		// Modulator #3
		voice.m_operators[3].enabled = true;
		voice.m_operators[3].oscillator.Initialize(kSine, CalcOpFreq(masterFreq, patch.operators[3]), modDepth*patch.operators[3].amplitude);

		// Modulator #2
		voice.m_operators[2].enabled = true;
		voice.m_operators[2].oscillator.Initialize(kSine, CalcOpFreq(masterFreq, patch.operators[2]), modDepth*patch.operators[2].amplitude);

		// Modulator #1
		voice.m_operators[1].enabled = true;
		voice.m_operators[1].feedback = 1;
		voice.m_operators[1].oscillator.Initialize(kSine, CalcOpFreq(masterFreq, patch.operators[1]), modDepth*patch.operators[1].amplitude);

		// Carrier #1
		voice.m_operators[0].enabled = true;
		voice.m_operators[0].modulators[0] = 1;
		voice.m_operators[0].modulators[1] = 2;
		voice.m_operators[0].modulators[2] = 3;
		voice.m_operators[0].isCarrier = true;
		voice.m_operators[0].oscillator.Initialize(request.form, CalcOpFreq(masterFreq, patch.operators[0]), masterAmp*patch.operators[0].amplitude);

		/*
			End of Algorithm
		*/

#endif

		// Freq. scale
		const float freqScale = masterFreq/g_midiFreqRange;

		// Set tremolo LFO
		const float tremFreq = s_parameters.tremolo*kAudibleLowHz*(freqScale+velocity);
		voice.m_tremolo.Initialize(kCosine, tremFreq, 1.f);

		// Set vibrato LFO
		const float vibFreq = s_parameters.vibrato*kAudibleLowHz*(freqScale+velocity);
		voice.m_vibrato.Initialize(kCosine, vibFreq, 2.f);

		// Set per-operator
		for (unsigned iOp = 0; iOp < kNumOperators; ++iOp)
		{
			// Tremolo/Vibrato
			voice.m_operators[iOp].vibrato = patch.operators[iOp].vibrato;
			voice.m_operators[iOp].tremolo = patch.operators[iOp].tremolo;

			// Mod env.
			ADSR::Parameters envParams;
			envParams.attack = s_parameters.patch.operators[iOp].modA;
			envParams.decay = s_parameters.patch.operators[iOp].modD;
			envParams.release = 0.f;
			envParams.sustain = 1.f;
			voice.m_operators[iOp].opEnv.Start(envParams, velocity);
		}

		// Start master ADSR
		voice.m_ADSR.Start(s_parameters.envParams, velocity);

		// Reset & start filter
		LadderFilter *pFilter;
		if (0 == s_parameters.filterType)
			pFilter = s_hardFilters+iVoice;
		else
			pFilter = s_softFilters+iVoice;

		pFilter->Reset();
		pFilter->Start(s_parameters.filterEnvParams, velocity);

		voice.m_pFilter = pFilter;
		
		// Enabled, up counter		
		voice.m_enabled = true;
		++s_active;

		// Store index if wanted
		if (nullptr != request.pIndex)
			*request.pIndex = iVoice;
		
	}

	// Update voices and handle trigger & release
	static void UpdateVoices()
	{
		SFM_ASSERT(false == s_stateMutex.try_lock());

		// Process release requests
		for (auto &request : s_voiceReleaseReq)
		{
			const unsigned index = request.index;

			SFM_ASSERT(-1 != index);

			DX_Voice &voice = s_DXvoices[index];

			const float velocity = request.velocity;

			// Stop main ADSR; this will automatically release the voice
			voice.m_ADSR.Stop(velocity);
			
			// Stop secondary envelopes

			// Not necessary; these have an attack and decay and nothing else, so they rest at sustain level
//			for (unsigned iOp = 0; iOp < kNumOperators; ++iOp)
//				voice.m_operators[iOp].opEnv.Stop(velocity);

			// Same as above
//			voice.m_pFilter->Stop(velocity);

			Log("Voice released: " + std::to_string(request.index));
		}

		// Update active voices
		for (unsigned iVoice = 0; iVoice < kMaxVoices; ++iVoice)
		{
			DX_Voice &voice = s_DXvoices[iVoice];
			const bool enabled = voice.m_enabled;

			if (true == enabled)
			{
				if (true == voice.m_ADSR.IsIdle())
				{
					voice.m_enabled = false;
					--s_active;
				}
			}
		}

		// Spawn new voice(s)
		while (s_voiceReq.size() > 0 && s_active < kMaxVoices-1)
		{
			// Pick first free voice (FIXME: need proper heuristic in case we're out of voices)
			for (unsigned iVoice = 0; iVoice < kMaxVoices; ++iVoice)
			{
				DX_Voice &voice = s_DXvoices[iVoice];
				if (false == voice.m_enabled)
				{
					const VoiceRequest request = s_voiceReq.front();
					InitializeDXVoice(request, iVoice);
					s_voiceReq.pop_front();

					Log("Voice triggered: " + std::to_string(iVoice));

					break;
				}
			}
		}

		// All release requests have been honoured; as for triggers, they may have to wait
		// if there's no slot available at this time.
		s_voiceReleaseReq.clear();
	}

	/*
		Parameter update.

		Currently there's only one of these for the Oxygen 49 (+ Arturia BeatStep) driver.

		Ideally all inputs interact on sample level, but this is impractical and only done for a few parameters.

		FIXME:
			- Update parameters every N samples (nuggets).
			- Remove all rogue parameter probes.
			  + WinMidi_GetPitchBend()
			  + WinMidi_GetMasterDrive()
			  + WinMidi_GetDelayRate()
			  + WinMidi_GetDelayFeedback()

		Most tweaking done here on the normalized input parameters should be adapted for the first VST attempt.
	*/

	static void UpdateState_Oxy49_BeatStep(float time)
	{
		std::lock_guard<std::mutex> lock(s_stateMutex);

		// Drive
		s_parameters.drive = dBToAmplitude(kDriveHidB)*WinMidi_GetMasterDrive();

		// Tremolo & vibrato
		s_parameters.tremolo = WinMidi_GetTremolo();
		s_parameters.vibrato = WinMidi_GetVibrato();

		// Master ADSR
		s_parameters.envParams.attack  = WinMidi_GetAttack();
		s_parameters.envParams.decay   = WinMidi_GetDecay();
		s_parameters.envParams.release = WinMidi_GetRelease();
		s_parameters.envParams.sustain = WinMidi_GetSustain();

		// Modulation depth
		const float alpha = 1.f/dBToAmplitude(-12.f);
		s_parameters.modDepth = WinMidi_GetModulation()*alpha;

		// Note jitter
		s_parameters.m_noteJitter = WinMidi_GetNoteJitter();

		// Filter
		s_parameters.filterInv = WinMidi_GetFilterInv();
		s_parameters.filterType = WinMidi_GetFilterType();
		s_parameters.filterWet = WinMidi_GetFilterWet();
		s_parameters.filterParams.drive = 1.f;
		s_parameters.filterParams.cutoff = WinMidi_GetCutoff();
		s_parameters.filterParams.resonance = WinMidi_GetResonance();

		// Filter envelope (ADS)
		s_parameters.filterEnvParams.attack = WinMidi_GetFilterA();
		s_parameters.filterEnvParams.decay = WinMidi_GetFilterD();
		s_parameters.filterEnvParams.release = 0.f; // Should never be used (no Stop() call)
		s_parameters.filterEnvParams.sustain = WinMidi_GetFilterS();

		// Delay
		s_parameters.delayWet = WinMidi_GetDelayWet();
		s_parameters.delayRate = WinMidi_GetDelayRate();
		s_parameters.delayWidth = WinMidi_GetDelayWidth();
		s_parameters.delayFeedback = WinMidi_GetDelayFeedback();

		// Update current operator
		const unsigned iOp = WinMidi_GetOperator();
		if (-1 != iOp)
		{
			SFM_ASSERT(iOp >= 0 && iOp < kNumOperators);

			FM_Patch::Operator &patchOp = s_parameters.patch.operators[iOp];
	
//			const unsigned index = unsigned(WinMidi_GetOperatorCoarse()*(g_numModRatios-1));
//			SFM_ASSERT(index < g_numModRatios);
//			patchOp.coarse = index;

			const unsigned index = unsigned(WinMidi_GetOperatorCoarse()*(g_CM_size-1));
			SFM_ASSERT(index < g_CM_size);
			patchOp.coarse = index;

			// Fine tuning (1 octave, ain't that much?)
			const float fine = WinMidi_GetOperatorFinetune();
			patchOp.fine = powf(2.f, fine);

			// Go up or down half a semitone
			// This is different from the DX7, but -7 to +7 semitones seems excessive
			float cents = ( -50.f + 100.f*WinMidi_GetOperatorDetune() ) * 0.01f;
			patchOp.detune = powf(2.f, cents/12.f);

			// Tremolo & vibrato
			patchOp.tremolo = WinMidi_GetOperatorTremolo();
			patchOp.vibrato = WinMidi_GetOperatorVibrato();

			// Modulation envelope
			patchOp.modA = WinMidi_GetModEnvA();
			patchOp.modD = WinMidi_GetModEnvD();
			
			// Amp./Index/Depth
			patchOp.amplitude = WinMidi_GetOperatorAmplitude();
		}
	}

	/*
		Render function.
	*/

	SFM_INLINE void ProcessDelay(float &mix)
	{
		// FIXME: keep as close to sampling as possible
		const float rate = WinMidi_GetDelayRate();
		const float feedback = WinMidi_GetDelayFeedback();

		// FIXME: perhaps create a dedicated LFO class that has nothing to do with tone instead of adding a function to Oscillator?
		s_delayLFO.PitchBend((feedback+rate)*24.f);
	
		// Bias LFO
		const float LFO = 0.5f + 0.5f*s_delayLFO.Sample(rate*rate);

		// Take single tap (FIXME?)
		const float width = s_parameters.delayWidth*kDelayBaseMul;
		const float tap = width*LFO;
		const float A = s_delayLine.Read(tap);

		// Write
		s_delayLine.Write(SoftClamp(mix + feedback*A));

		// Mix
		const float wet = s_parameters.delayWet*kRootHalf;
		mix = SoftClamp(mix + A*wet);
	}

	alignas(16) static float s_voiceBuffers[kMaxVoices][kRingBufferSize];

	// Returns loudest signal (linear amplitude)
	static float Render(float time)
	{
		float loudest = 0.f;

		// Lock ring buffer
		std::lock_guard<std::mutex> ringLock(s_ringBufMutex);

		// See if there's enough space in the ring buffer to justify rendering
		if (true == s_ringBuf.IsFull())
			return loudest;

		const unsigned available = s_ringBuf.GetAvailable();
		if (available > kMinSamplesPerUpdate)
			return loudest;

		const unsigned numSamples = kRingBufferSize-available;

		// Lock state
		std::lock_guard<std::mutex> stateLock(s_stateMutex);

		// Handle voice logic
		UpdateVoices();

		const unsigned numVoices = s_active;

		if (0 == numVoices)
		{
			loudest = 0.f;

			// Render silence (we still have to run the effects)
			for (unsigned iSample = 0; iSample < numSamples; ++iSample)
			{
				float mix = 0.f;

				ProcessDelay(mix);

				// Drive
				const float drive = WinMidi_GetMasterDrive();
				mix = ultra_tanhf(mix*drive);

				s_ringBuf.Write(mix);
			}
		}
		else
		{
			// Render dry samples for each voice (feedback)
			unsigned curVoice = 0;
			for (unsigned iVoice = 0; iVoice < kMaxVoices; ++iVoice)
			{
				DX_Voice &voice = s_DXvoices[iVoice];
				
				if (true == voice.m_enabled)
				{
					SFM_ASSERT(nullptr != voice.m_pFilter);
					LadderFilter &filter = *voice.m_pFilter;
				
					// This should be as close to the sample as possible (FIXME)
					const float bend = WinMidi_GetPitchBend()*kPitchBendRange;
					voice.SetPitchBend(bend);
	
					float *buffer = s_voiceBuffers[curVoice];
					for (unsigned iSample = 0; iSample < numSamples; ++iSample)
					{
						/* const */ float sample = voice.Sample(s_parameters);
						buffer[iSample] = sample;
					}

					// Filter voice
					filter.SetLiveParameters(s_parameters.filterParams);
					filter.Apply(buffer, numSamples, s_parameters.filterWet /* AKA 'contour' */, s_parameters.filterInv);

					// Apply ADSR (FIXME: slow?)
					for (unsigned iSample = 0; iSample < numSamples; ++iSample)
					{
						buffer[iSample] *= voice.m_ADSR.Sample();
					}

					++curVoice; // Do *not* use to index anything other than the temporary buffers
				}
			}

			loudest = 0.f;

			// Mix & store voices
			for (unsigned iSample = 0; iSample < numSamples; ++iSample)
			{
				const unsigned sampleCount = s_sampleCount+iSample;

				float mix = 0.f;
				for (unsigned iVoice = 0; iVoice < numVoices; ++iVoice)
				{
					const float sample = s_voiceBuffers[iVoice][iSample];
					SampleAssert(sample);

					mix = mix+sample;
				}

				ProcessDelay(mix);

				// Drive
				const float drive = WinMidi_GetMasterDrive();
				mix = ultra_tanhf(mix*drive);

				// Write
				SampleAssert(mix);
				s_ringBuf.Write(mix);

				loudest = std::max<float>(loudest, fabsf(mix));
			}
		}

		s_sampleCount += numSamples;

		return loudest;
	}

}; // namespace SFM

using namespace SFM;

/*
	SDL2 stream callback.
*/

static void SDL2_Callback(void *pData, uint8_t *pStream, int length)
{
	const unsigned numSamplesReq = length/sizeof(float);

	std::lock_guard<std::mutex> lock(s_ringBufMutex);
	{
		const unsigned numSamplesAvail = s_ringBuf.GetAvailable();
		const unsigned numSamples = std::min<unsigned>(numSamplesAvail, numSamplesReq);

		if (numSamplesAvail < numSamplesReq)
		{
//			SFM_ASSERT(false);
			Log("Buffer underrun");
		}

		float *pWrite = reinterpret_cast<float*>(pStream);
		for (unsigned iSample = 0; iSample < numSamples; ++iSample)
			*pWrite++ = s_ringBuf.Read();

		s_sampleOutCount += numSamples;
	}
}

/*
	Global (de-)initialization.
*/

bool Syntherklaas_Create()
{
	// Calc. LUTs
	CalculateLUTs();
	CalculateMidiToFrequencyLUT();

	// Reset sample count
	s_sampleCount = 0;
	s_sampleOutCount = 0;

	// Reset parameter state
	s_parameters.SetDefaults();

	// Reset runtime state
	for (unsigned iVoice = 0; iVoice < kMaxVoices; ++iVoice)
		s_DXvoices[iVoice].Reset();

	s_delayLine.Reset();
	s_delayLFO.Initialize(kCosine, kDelayBaseFreq, 1.f);

	// Reset voice deques
	s_voiceReq.clear();
	s_voiceReleaseReq.clear();

	// Oxygen 49 driver + SDL2
	const bool midiIn = WinMidi_Oxygen49_Start() && WinMidi_BeatStep_Start();
	const bool audioOut = SDL2_CreateAudio(SDL2_Callback);

	return true == midiIn && audioOut;
}

void Syntherklaas_Destroy()
{
	SDL2_DestroyAudio();

	WinMidi_Oxygen49_Stop();
	WinMidi_BeatStep_Stop();
}

/*
	Frame update (called by Bevacqua).
*/

float Syntherklaas_Render(uint32_t *pDest, float time, float delta)
{
	// Update state for M-AUDIO Oxygen 49 & Arturia BeatStep
	UpdateState_Oxy49_BeatStep(time);
	
	// Render
	float loudest = Render(time);

	// Start audio stream on first call
	static bool first = false;
	if (false == first)
	{
		SDL2_StartAudio();
		first = true;

		Log("FM. BISON is up & running!");
	}

	return loudest;
}
