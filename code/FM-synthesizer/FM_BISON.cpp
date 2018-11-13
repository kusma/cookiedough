
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
#include "synth-filter.h"
#include "synth-delayline.h"
#include "synth-math.h"
#include "synth-LUT.h"
#include "synth-formant.h"

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

	static std::deque<VoiceRequest> s_voiceReq;
	static std::deque<VoiceReleaseRequest> s_voiceReleaseReq;

	static Parameters s_parameters;
	static Voice s_voices[kMaxVoices];
	static unsigned s_active = 0;
	static ADSR s_ADSRs[kMaxVoices];
	static TeemuFilter s_teemuFilters[kMaxVoices];
	static ButterworthFilter s_cleanFilters[kMaxVoices];
	static ImprovedMOOGFilter s_improvedFilters[kMaxVoices];
	static DelayMatrix s_delayMatrix(kSampleRate/8); // Div. by multiples of 4 sounds OK
	static FormantShaper s_formantShapers[kMaxVoices];
	static LowpassFilter s_bassBoostLPF;
	
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

	static void InitializeVoice(const VoiceRequest &request, unsigned iVoice)
	{
		SFM_ASSERT(false == s_stateMutex.try_lock());

		Voice &voice = s_voices[iVoice];

		const float goldenTen = kGoldenRatio*10.f;

		float frequency = request.frequency;
		
		// Randomize note frequency little if wanted (in (almost) entire cents)
		const float drift = kMaxNoteDrift*s_parameters.m_noteDrift;
		const int cents = int(oscWhiteNoise(mt_randf())*drift);
		
		float jitter = 1.f;
		if (0 != cents)
			jitter = powf(2.f, (cents*0.01f)/12.f);
		
		FloatAssert(jitter);

		frequency *= jitter;

		const float velocity  = request.velocity;
		const bool  isWave    = oscIsWavetable(request.form);

		// Algorithm
		voice.m_algorithm = s_parameters.m_algorithm;
	
		// Initialize carrier(s)
		const float amplitude = velocity*kMaxVoiceAmp;
		const float carrierFreq = frequency;
	
		switch (voice.m_algorithm)
		{
		case kSingle:
			voice.m_carriers[0].Initialize(request.form, carrierFreq, amplitude);
			break;

		case kDoubleCarriers:
			{
				voice.m_carriers[0].Initialize(request.form, carrierFreq, amplitude);

				// Initialize a detuned second carrier by going from equal to a perfect-fifth semitone (gives a thicker, almost phaser-like sound)
				const float detune = powf(3.f/2.f, s_parameters.m_doubleDetune/12.f);
				voice.m_carriers[1].Initialize(request.form, carrierFreq*detune, amplitude*dBToAmplitude(-3.f));
			}	

			break;

		case kMiniMOOG:
			{
				voice.m_carriers[0].Initialize(request.form, carrierFreq, amplitude*s_parameters.m_carrierVol[0]);

				const float detune = powf(3.f/2.f, kMOOGDetuneRange*s_parameters.m_slavesDetune/12.f);
				const float slaveFreq = carrierFreq*detune;

				// Reduced amplitude for slaves
				const float slaveAmp = amplitude*dBToAmplitude(-3.f);

				voice.m_carriers[1].Initialize(WinMidi_GetCarrierOscillator2(), slaveFreq, slaveAmp*s_parameters.m_carrierVol[1]);
				voice.m_carriers[2].Initialize(WinMidi_GetCarrierOscillator3(), slaveFreq, slaveAmp*s_parameters.m_carrierVol[2]);

				// Want hard sync.?
				if (true == s_parameters.m_hardSync)
				{
					// Enslave them!
					voice.m_carriers[1].SyncTo(carrierFreq);
					voice.m_carriers[2].SyncTo(carrierFreq);
				}

				// Reset LPF
				voice.m_LPF.Reset();
			}

			break;
		}

		// Initialize freq. modulator
		const float ratio = s_parameters.m_modRatioM/s_parameters.m_modRatioC;
		const float modFrequency = frequency * powf(2.f, ratio/6.f);
		voice.m_modulator.Initialize(s_sampleCount, s_parameters.m_modIndex, modFrequency, s_parameters.m_modVibrato*goldenTen * velocity /* Less velocity: less vibrato */);

		// Initialize amplitude modulator (or 'tremolo')
		const float tremolo = s_parameters.m_tremolo;
		const float tremoloFreq = tremolo*0.25f*goldenTen; // FIXME?
		voice.m_AM.Initialize(kCosine, tremoloFreq, kMaxVoiceAmp);

		// One shot?
		voice.m_oneShot = s_parameters.m_loopWaves ? false : oscIsWavetable(request.form);

		// Pulse osc. duty cycle
		voice.m_pulseWidth = 0.1f + 0.8f*s_parameters.m_pulseWidth;

		// Get & reset filter
		switch (s_parameters.m_curFilter)
		{
		default:
		case kTeemuFilter: // A bit more expressive (default)
			voice.m_pFilter = s_teemuFilters+iVoice;
			break;

		case kButterworthFilter: // Completely different response curve than either other (harsher, "wider" resonance)
			voice.m_pFilter = s_cleanFilters+iVoice;
			break;

		case kImprovedMOOGFilter:
			voice.m_pFilter = s_improvedFilters+iVoice;
			break;
		}

		voice.m_pFilter->Reset();	

		// Set ADSRs (voice & filter)
		s_ADSRs[iVoice].Start(s_sampleCount, s_parameters.m_voiceADSR, velocity);
		voice.m_pFilter->Start(s_sampleCount, s_parameters.m_filterADSR, velocity);

		// Reset formant shaper
		s_formantShapers[iVoice].Reset();
		
		voice.m_enabled = true;
		++s_active;

		// Store index if wanted
		if (nullptr != request.pIndex)
			*request.pIndex = iVoice;
	}

	// - Handle all requested voice releases
	// - Updates all voices as follows:
	// - Update voices (includes termination)
	// - Trigger all requested voices
	static void UpdateVoices()
	{
		SFM_ASSERT(false == s_stateMutex.try_lock());

		// Process release requests
		for (auto &request : s_voiceReleaseReq)
		{
			SFM_ASSERT(-1 != request.index);
			
			Voice &voice = s_voices[request.index];

			// Stop voice ADSR; filter ADSR just sustains, and that should be OK
			s_ADSRs[request.index].Stop(s_sampleCount, request.velocity);

			Log("Voice released: " + std::to_string(request.index));
		}

		// Update active voices
		for (unsigned iVoice = 0; iVoice < kMaxVoices; ++iVoice)
		{
			Voice &voice = s_voices[iVoice];
			const bool enabled = voice.m_enabled;

			if (true == enabled)
			{
				// If in MiniMOOG mode, set lowpass cutoff
				if (kMiniMOOG == voice.m_algorithm)
					voice.m_LPF.SetCutoff(s_parameters.m_slavesLP);
				
				// See if any voices are done
				bool free = false;
				
				// If ADSR envelope has ran it's course, voice can be freed
				ADSR &envelope = s_ADSRs[iVoice];
				if (true == envelope.IsIdle())
					free = true;

				// One-shot done?
				// We'll just cut the sound of the first carrier if it's the MiniMOOG algorithm (see synth-voice.cpp)
				if (kMiniMOOG != voice.m_algorithm && (true == voice.m_oneShot && true == voice.HasCycled(s_sampleCount)))
					free = true;
				
				// Free
				if (true == free)
				{
					voice.m_enabled = false;
					--s_active;
				}
			}
		}

		while (s_voiceReq.size() > 0 && s_active < kMaxVoices-1)
		{
			// Pick first free voice (FIXME: need proper heuristic in case we're out of voices)
			for (unsigned iVoice = 0; iVoice < kMaxVoices; ++iVoice)
			{
				Voice &voice = s_voices[iVoice];
				if (false == voice.m_enabled)
				{
					const VoiceRequest request = s_voiceReq.front();
					InitializeVoice(request, iVoice);
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

		Currently there's only one of these for the Oxygen 49 + Arturia BeatStep driver.

		Ideally all inputs interact on sample level, but this is impractical and only done for a few parameters.

		FIXME:
			- Update parameters every N samples.
			- Remove all rogue parameter probes.
			  + WinMidi_GetPitchBend()
			  + WinMidi_GetMasterDrive()

		Most tweaking done here on the normalized input parameters should be adapted for the first VST attempt.
	*/

	static void UpdateState_Oxy49_BeatStep(float time)
	{
		std::lock_guard<std::mutex> lock(s_stateMutex);

		// Algorithm (1, 2 & 3)
		s_parameters.m_algorithm = WinMidi_GetAlgorithm();
		SFM_ASSERT(s_parameters.m_algorithm < kNumAlgorithms);
		
		// For algorithm #2
		s_parameters.m_doubleDetune = WinMidi_GetDoubleDetune();
		s_parameters.m_doubleVolume = WinMidi_GetDoubleVolume();

		// For algorithm #3
		const float slavesLP = WinMidi_GetSlavesLowpass();
		s_parameters.m_slavesDetune  = WinMidi_GetSlavesDetune();
		s_parameters.m_slavesLP      = std::max<float>(kEpsilon, clampf(0.f, 1.f, slavesLP*slavesLP));
		s_parameters.m_hardSync      = WinMidi_GetHardSync();
		s_parameters.m_slaveFM       = WinMidi_GetSlaveFM();
		s_parameters.m_carrierVol[0] = WinMidi_GetCarrierVolume1();
		s_parameters.m_carrierVol[1] = WinMidi_GetCarrierVolume2();
		s_parameters.m_carrierVol[2] = WinMidi_GetCarrierVolume3();

		// Drive
		s_parameters.m_drive = dBToAmplitude(kDriveHidB)*WinMidi_GetMasterDrive();

		// Note (VCO) drift
		s_parameters.m_noteDrift = WinMidi_GetNoteDrift();
	
		// Modulation index
		const float alpha = 1.f/dBToAmplitude(-12.f);
		s_parameters.m_modIndex = WinMidi_GetModulationIndex()*alpha;

		// Get ratio from precalculated table (see synth-LUT.cpp)
		const unsigned tabIndex = (unsigned) (WinMidi_GetModulationRatio()*(g_CM_table_size-1));
		SFM_ASSERT(tabIndex < g_CM_table_size);
		s_parameters.m_modRatioC = (float) g_CM_table[tabIndex][0];
		s_parameters.m_modRatioM = (float) g_CM_table[tabIndex][1];

		// Modulation brightness affects the modulator's oscillator blend
		s_parameters.m_modBrightness = WinMidi_GetModulationBrightness();

		// Modulation index LFO frequency
		const float frequency = WinMidi_GetModulationVibrato();
		s_parameters.m_modVibrato = frequency;

		// Noise
		s_parameters.m_noisyness = WinMidi_GetNoisyness();

		// Formant (vowel)
		s_parameters.m_formantVowel = WinMidi_GetFormantVowel();
		s_parameters.m_formant = WinMidi_GetFormant();
		s_parameters.m_formantStep =  WinMidi_GetFormantStep();

		// Nintendize
		s_parameters.m_Nintendize = WinMidi_GetNintendize();

		// Tremolo
		s_parameters.m_tremolo = WinMidi_GetTremolo();

		// Wavetable one-shot yes/no
		s_parameters.m_loopWaves = WinMidi_GetLoopWaves();

		// Pulse osc. width
		s_parameters.m_pulseWidth = WinMidi_GetPulseWidth();

		// Bass boost
		s_parameters.m_bassBoost = WinMidi_GetBassBoost();

		// Voice ADSR	
		s_parameters.m_voiceADSR.attack  = WinMidi_GetAttack();
		s_parameters.m_voiceADSR.decay   = WinMidi_GetDecay();
		s_parameters.m_voiceADSR.release = WinMidi_GetRelease();
		s_parameters.m_voiceADSR.sustain = WinMidi_GetSustain();

		// Filter ADS(R)
		s_parameters.m_filterADSR.attack  = WinMidi_GetFilterAttack();
		s_parameters.m_filterADSR.decay   = WinMidi_GetFilterDecay();
		s_parameters.m_filterADSR.sustain = WinMidi_GetFilterSustain();
		s_parameters.m_filterADSR.release = 0.f;

		// Filter parameters
		s_parameters.m_curFilter = WinMidi_GetCurFilter();
		s_parameters.m_filterContour = WinMidi_GetFilterContour();
		s_parameters.m_flipFilterEnv = WinMidi_GetFilterFlipEnvelope();
		s_parameters.m_filterParams.drive = dBToAmplitude(kMaxFilterDrivedB)*WinMidi_GetFilterDrive();
		s_parameters.m_filterParams.cutoff = WinMidi_GetFilterCutoff();
		s_parameters.m_filterParams.resonance = WinMidi_GetFilterResonance();

		// Feedback parameters
		s_parameters.m_feedback = WinMidi_GetFeedback();
		s_parameters.m_feedbackWetness = WinMidi_GetFeedbackWetness();
		s_parameters.m_feedbackPitch = -1.f + WinMidi_GetFeedbackPitch()*2.f;
	}

	/*
		Render function.
	*/

	alignas(16) static float s_voiceBuffers[kMaxVoices][kRingBufferSize];

	SFM_INLINE void ProcessDelay(float &mix)
	{
		// Process delay
		const float delayPitch = s_parameters.m_feedbackPitch;
		const float delayed = s_delayMatrix.Read(delayPitch)*kFeedbackAmplitude;
		s_delayMatrix.Write(mix, delayed*s_parameters.m_feedback);

		// Apply delay
		mix = mix + s_parameters.m_feedbackWetness*delayed;
	}

	// Shave bits off sample
	// This used to be slow (conversion) but with the right parameters the compiler doesn't make it a big deal
	SFM_INLINE float Nintendize(float sample)
	{
		int8_t quantized = int8_t(sample*127.f);
		quantized &= ~63;
		return quantized/127.f;
	}

	// Returns loudest signal (linear amplitude)
	static float Render(float time)
	{
		static float loudest = 0.f;

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
				s_ringBuf.Write(mix);
			}
		}
		else
		{
			// Render dry samples for each voice (feedback)
			unsigned curVoice = 0;
			for (unsigned iVoice = 0; iVoice < kMaxVoices; ++iVoice)
			{
				Voice &voice = s_voices[iVoice];

				if (true == voice.m_enabled)
				{
					// FIXME: move!
					const float bend = WinMidi_GetPitchBend()*kPitchBendRange;
					voice.PitchBend(bend);

					ADSR &voiceADSR = s_ADSRs[iVoice];

					FormantShaper &shaper = s_formantShapers[iVoice];

					const FormantShaper::Vowel vowel = s_parameters.m_formantVowel;
					const float formant = s_parameters.m_formant;
					const float formantStep = s_parameters.m_formantStep; // Between current and next vowel
	
					float *buffer = s_voiceBuffers[curVoice];
					for (unsigned iSample = 0; iSample < numSamples; ++iSample)
					{
						const unsigned sampleCount = s_sampleCount+iSample;
						/* const */ float sample = voice.Sample(sampleCount, s_parameters);

						// Blend with Nintendized version
						const float Nintendized = Nintendize(sample);
						sample = lerpf<float>(sample, Nintendized, invsqrf(s_parameters.m_Nintendize));

						// Shape by formant
						float shaped = shaper.Apply(sample, vowel, formantStep);
						sample = lerpf<float>(sample, shaped, formant);

						buffer[iSample] = sample;
					}

					// Filter voice
					voice.m_pFilter->SetLiveParameters(s_parameters.m_filterParams);
					voice.m_pFilter->Apply(buffer, numSamples, s_parameters.m_filterContour, s_parameters.m_flipFilterEnv, s_sampleCount);

					// Apply ADSR
					for (unsigned iSample = 0; iSample < numSamples; ++iSample)
					{
						const unsigned sampleCount = s_sampleCount+iSample;
						const float ADSR = voiceADSR.Sample();
						const float sample = buffer[iSample];
						buffer[iSample] = sample*ADSR;
					}

					++curVoice; // Do *not* use to index anything other than the temporary buffers
				}
			}

			loudest = 0.f;

			// Mix & store voices
			const float bassBoost = s_parameters.m_bassBoost;

			for (unsigned iSample = 0; iSample < numSamples; ++iSample)
			{
				const unsigned sampleCount = s_sampleCount+iSample;

				float mix = 0.f;
				for (unsigned iVoice = 0; iVoice < numVoices; ++iVoice)
				{
					const float sample = s_voiceBuffers[iVoice][iSample];
					SampleAssert(sample);

					mix = SoftClamp(mix+sample);
				}
				
				// Process delay
				ProcessDelay(mix);

				// Bass boost
				s_bassBoostLPF.SetCutoff(1.f/(kMaxSubLFO + kMaxSubLFO*bassBoost));
				mix += bassBoost*s_bassBoostLPF.Apply(mix);

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

#include "synth-test.h"

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
	{
		s_voices[iVoice].m_enabled = false;
		s_ADSRs[iVoice].Reset();
		s_improvedFilters[iVoice].Reset();
		s_cleanFilters[iVoice].Reset();
		s_teemuFilters[iVoice].Reset();
	}

	// Reset voice deques
	s_voiceReq.clear();
	s_voiceReleaseReq.clear();

	// Reset boost LPF
	s_bassBoostLPF.Reset();

	// Oxygen 49 driver + SDL2
	const bool midiIn = WinMidi_Oxygen49_Start() && WinMidi_BeatStep_Start();
	const bool audioOut = SDL2_CreateAudio(SDL2_Callback);

	// Test
//	OscTest();

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
//	ClickTest(time, 1.f);

	float loudest = 0.f;

	// Update state for M-AUDIO Oxygen 49 & Arturia BeatStep
	UpdateState_Oxy49_BeatStep(time);
	
	// Render
	loudest = Render(time);

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
