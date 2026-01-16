/*
  ==============================================================================

   AetherGiantPercussionPureDSP.cpp
   Giant Percussion Synthesizer - Physical Modeling Implementation

   Physical modeling of giant-scale percussion using modal synthesis:
   - Modal resonator bank (8-64 modes for gongs/bells/plates)
   - Nonlinear dispersion (inharmonicity)
   - Damping model (size-scaled decay times)
   - Strike/scrape excitation
   - Stereo radiation patterns

  ==============================================================================
*/

#include "dsp/AetherGiantPercussionDSP.h"
#include "dsp/InstrumentFactory.h"
#include "../../../../include/dsp/LookupTables.h"
#include "../../../../include/dsp/FastRNG.h"
#include <cmath>
#include <algorithm>

// Platform-specific SIMD includes
#if defined(__ARM_NEON) || defined(__aarch64__)
    #include <arm_neon.h>
    #define DSP_SIMD_NEON_AVAILABLE 1
#elif defined(__AVX__)
    #include <immintrin.h>
    #define DSP_SIMD_AVX_AVAILABLE 1
#elif defined(__SSE4_1__)
    #include <smmintrin.h>
    #define DSP_SIMD_SSE_AVAILABLE 1
#endif

namespace DSP {

//==============================================================================
// SIMD Utility Functions
//==============================================================================

namespace SIMD {

#if DSP_SIMD_NEON_AVAILABLE
    // ARM NEON implementations for modal processing

    inline float horizontalSum(float32x4_t v)
    {
        float32x2_t sum = vadd_f32(vget_low_f32(v), vget_high_f32(v));
        sum = vpadd_f32(sum, sum);
        return vget_lane_f32(sum, 0);
    }

    inline float processModesNEON(float excitation, ModalResonatorMode* modes, size_t count)
    {
        float32x4_t outputs = vdupq_n_f32(0.0f);

        // Process modes in groups of 4
        size_t i = 0;
        for (; i + 4 <= count; i += 4)
        {
            // Process 4 modes simultaneously
            float m0 = modes[i + 0].processSample(excitation);
            float m1 = modes[i + 1].processSample(excitation);
            float m2 = modes[i + 2].processSample(excitation);
            float m3 = modes[i + 3].processSample(excitation);

            // Combine into NEON vector
            float32x4_t modeOutputs = vsetq_lane_f32(m0, vdupq_n_f32(0.0f), 0);
            modeOutputs = vsetq_lane_f32(m1, modeOutputs, 1);
            modeOutputs = vsetq_lane_f32(m2, modeOutputs, 2);
            modeOutputs = vsetq_lane_f32(m3, modeOutputs, 3);

            // Accumulate
            outputs = vaddq_f32(outputs, modeOutputs);
        }

        // Horizontal sum
        float output = horizontalSum(outputs);

        // Process remaining modes (tail)
        for (; i < count; ++i)
        {
            output += modes[i].processSample(excitation);
        }

        return output;
    }

#elif DSP_SIMD_AVX_AVAILABLE
    // x86 AVX implementations for modal processing

    inline float horizontalSum(__m256 v)
    {
        __m128 sum128 = _mm_add_ps(_mm256_castps256_ps128(v),
                                   _mm256_extractf128_ps(v, 1));
        sum128 = _mm_hadd_ps(sum128, sum128);
        sum128 = _mm_hadd_ps(sum128, sum128);
        return _mm_cvtss_f32(sum128);
    }

    inline float horizontalSum128(__m128 v)
    {
        v = _mm_hadd_ps(v, v);
        v = _mm_hadd_ps(v, v);
        return _mm_cvtss_f32(v);
    }

    inline float processModesAVX(float excitation, ModalResonatorMode* modes, size_t count)
    {
        __m256 outputs = _mm256_setzero_ps();

        // Process modes in groups of 8
        size_t i = 0;
        for (; i + 8 <= count; i += 8)
        {
            // Process 8 modes
            float m[8];
            for (int j = 0; j < 8; ++j)
            {
                m[j] = modes[i + j].processSample(excitation);
            }

            // Load into AVX vector
            __m256 modeOutputs = _mm256_set_ps(m[7], m[6], m[5], m[4],
                                               m[3], m[2], m[1], m[0]);

            // Accumulate
            outputs = _mm256_add_ps(outputs, modeOutputs);
        }

        // Horizontal sum for AVX part
        float output = horizontalSum(outputs);

        // Process remaining modes in groups of 4
        for (; i + 4 <= count; i += 4)
        {
            float m0 = modes[i + 0].processSample(excitation);
            float m1 = modes[i + 1].processSample(excitation);
            float m2 = modes[i + 2].processSample(excitation);
            float m3 = modes[i + 3].processSample(excitation);

            __m128 modeOutputs = _mm_set_ps(m3, m2, m1, m0);
            output += horizontalSum128(modeOutputs);
        }

        // Process remaining modes (tail)
        for (; i < count; ++i)
        {
            output += modes[i].processSample(excitation);
        }

        return output;
    }

#elif DSP_SIMD_SSE_AVAILABLE
    // x86 SSE implementations for modal processing

    inline float horizontalSum(__m128 v)
    {
        v = _mm_hadd_ps(v, v);
        v = _mm_hadd_ps(v, v);
        return _mm_cvtss_f32(v);
    }

    inline float processModesSSE(float excitation, ModalResonatorMode* modes, size_t count)
    {
        __m128 outputs = _mm_setzero_ps();

        // Process modes in groups of 4
        size_t i = 0;
        for (; i + 4 <= count; i += 4)
        {
            // Process 4 modes
            float m0 = modes[i + 0].processSample(excitation);
            float m1 = modes[i + 1].processSample(excitation);
            float m2 = modes[i + 2].processSample(excitation);
            float m3 = modes[i + 3].processSample(excitation);

            // Load into SSE vector
            __m128 modeOutputs = _mm_set_ps(m3, m2, m1, m0);

            // Accumulate
            outputs = _mm_add_ps(outputs, modeOutputs);
        }

        // Horizontal sum
        float output = horizontalSum(outputs);

        // Process remaining modes (tail)
        for (; i < count; ++i)
        {
            output += modes[i].processSample(excitation);
        }

        return output;
    }

#endif

} // namespace SIMD

//==============================================================================
// ModalResonatorMode Implementation (SVF-based)
//==============================================================================

void ModalResonatorMode::prepare(double sr)
{
    sampleRate = sr;

    // Prepare SVF as bandpass filter for resonance
    svf.reset();
    svf.prepare({ sr, static_cast<juce::uint32>(sr), 1u }); // numChannels = 1 for mono

    // Configure as bandpass filter (resonator)
    svf.setType(juce::dsp::StateVariableTPTFilterType::bandpass);

    // Set initial parameters
    svf.setCutoffFrequency(frequency);
    svf.setResonance(Q);
}

float ModalResonatorMode::processSample(float input)
{
    // Smooth parameter updates to prevent zipper noise
    svf.setCutoffFrequency(frequency);
    svf.setResonance(Q);

    // Process input through SVF resonator
    // The SVF naturally resonates at its center frequency when excited
    float output = svf.processSample(0, input);

    // Apply amplitude envelope
    output *= amplitude;

    // Apply decay
    amplitude *= decay;

    return output;
}

void ModalResonatorMode::excite(float energy)
{
    amplitude = initialAmplitude * energy;

    // Give SVF an initial impulse to start resonance
    // This simulates the initial strike impulse
    svf.processSample(0, energy * 0.5f);  // Drive the SVF to start it ringing
}

void ModalResonatorMode::reset()
{
    amplitude = 0.0f;
    svf.reset();
}

//==============================================================================
// ModalResonatorBank Implementation
//==============================================================================

ModalResonatorBank::ModalResonatorBank()
{
    params.numModes = 16;
}

void ModalResonatorBank::prepare(double sampleRate)
{
    sr = sampleRate;
    initializeModes();
}

void ModalResonatorBank::reset()
{
    for (auto& mode : modes)
        mode.reset();
    scrapeEnergy = 0.0f;
}

void ModalResonatorBank::strike(float velocity, float force, float contactArea)
{
    for (auto& mode : modes)
    {
        // Different modes get different energy based on contact area
        // Small contact area = excites more high modes
        // Large contact area = excites more low modes
        float modeExcitation = velocity * force;

        // Frequency-based energy distribution
        float normalizedFreq = mode.frequency / 440.0f;
        float frequencyWeight = 1.0f / (1.0f + normalizedFreq * normalizedFreq);

        // Contact area affects brightness
        float brightnessWeight = (contactArea < 0.5f) ?
            (1.0f - contactArea * 0.5f) :  // Small = bright
            (0.5f + contactArea * 0.5f);   // Large = dark

        float energy = modeExcitation * frequencyWeight * brightnessWeight;
        mode.excite(energy);
    }
}

void ModalResonatorBank::scrape(float intensity, float roughness)
{
    scrapeEnergy = intensity * roughness;
}

float ModalResonatorBank::processSample()
{
    // Generate excitation signal (noise burst for SVFs to resonate)
    float excitation = 0.0f;
    if (scrapeEnergy > 0.001f)
    {
        static FastRNG rng(42);  // Fixed seed for determinism
        excitation = rng.next() * scrapeEnergy * 0.1f;
        scrapeEnergy *= 0.99f; // Decay scrape
    }

    // Process modes using SIMD when available
#if DSP_SIMD_NEON_AVAILABLE
    return SIMD::processModesNEON(excitation, modes.data(), modes.size());
#elif DSP_SIMD_AVX_AVAILABLE
    return SIMD::processModesAVX(excitation, modes.data(), modes.size());
#elif DSP_SIMD_SSE_AVAILABLE
    return SIMD::processModesSSE(excitation, modes.data(), modes.size());
#else
    // Scalar fallback - batch process all modes
    float output = 0.0f;
    for (size_t i = 0; i < modes.size(); ++i)
    {
        output += modes[i].processSample(excitation);
    }
    return output;
#endif
}

void ModalResonatorBank::setParameters(const Parameters& p)
{
    params = p;
    initializeModes();
}

float ModalResonatorBank::getTotalEnergy() const
{
    float energy = 0.0f;
    for (const auto& mode : modes)
        energy += mode.amplitude;
    return energy;
}

void ModalResonatorBank::initializeModes()
{
    modes.clear();
    modes.resize(params.numModes);

    switch (params.instrumentType)
    {
        case InstrumentType::Gong:
            initializeGongModes();
            break;
        case InstrumentType::Bell:
            initializeBellModes();
            break;
        case InstrumentType::Plate:
            initializePlateModes();
            break;
        case InstrumentType::Chime:
            initializeChimeModes();
            break;
        case InstrumentType::Bowl:
            initializeBowlModes();
            break;
        default:
            initializeGongModes();
            break;
    }

    // Prepare all modes
    for (auto& mode : modes)
        mode.prepare(sr);
}

void ModalResonatorBank::initializeGongModes()
{
    // Gongs have inharmonic partials
    float baseFreq = 100.0f / params.sizeMeters; // Size affects fundamental

    // Structure controls the spread between harmonic and inharmonic
    float structureSpread = params.structure; // 0 = harmonic, 1 = fully inharmonic

    for (int i = 0; i < params.numModes; ++i)
    {
        float ratio = static_cast<float>(i + 1);

        // Structure affects frequency spacing
        // Low structure = more harmonic, High structure = more inharmonic
        float inharmonicity = params.inharmonicity * structureSpread;
        float freqRatio = ratio * (1.0f + inharmonicity * static_cast<float>(i) / params.numModes);
        modes[i].frequency = baseFreq * freqRatio;

        // Calculate Q from decay time (Q determines how long the SVF resonates)
        // Higher decay = higher Q = longer resonance
        float baseDecay = calculateDecay(0.995f, modes[i].frequency, params.sizeMeters);
        modes[i].decay = baseDecay;

        // Convert decay coefficient to Q (approximately)
        // Q ≈ 1 / (1 - decay) * (frequency / sampleRate)
        modes[i].Q = (1.0f / (1.0f - baseDecay)) * (modes[i].frequency / static_cast<float>(sr));
        modes[i].Q = std::clamp(modes[i].Q, 1.0f, 100.0f); // Clamp to reasonable range

        modes[i].initialAmplitude = 1.0f / (1.0f + static_cast<float>(i) * 0.1f);
        modes[i].prepare(sr);
    }
}

void ModalResonatorBank::initializeBellModes()
{
    // Bells have harmonic partials with some stretch
    float baseFreq = 200.0f / params.sizeMeters;

    for (int i = 0; i < params.numModes; ++i)
    {
        // Bell partial ratios (approximate) - structure affects these ratios
        float ratios[] = {1.0f, 2.0f, 3.0f, 4.2f, 5.4f, 6.8f, 8.0f, 9.5f,
                          11.0f, 12.5f, 14.0f, 15.5f, 17.0f, 18.5f, 20.0f, 22.0f};
        int idx = i % 16;

        // Structure modifies ratios slightly
        float ratioMod = ratios[idx] * (1.0f + params.structure * 0.1f);
        modes[i].frequency = baseFreq * ratioMod;

        float baseDecay = calculateDecay(0.997f, modes[i].frequency, params.sizeMeters);
        modes[i].decay = baseDecay;

        // Higher Q for bells (longer decay)
        modes[i].Q = (1.0f / (1.0f - baseDecay)) * (modes[i].frequency / static_cast<float>(sr));
        modes[i].Q = std::clamp(modes[i].Q, 5.0f, 150.0f);

        modes[i].initialAmplitude = 1.0f / (1.0f + static_cast<float>(i) * 0.15f);
        modes[i].prepare(sr);
    }
}

void ModalResonatorBank::initializePlateModes()
{
    // Plates have complex mode patterns
    float baseFreq = 150.0f / params.sizeMeters;

    for (int i = 0; i < params.numModes; ++i)
    {
        // Chaotic mode ratios for plates - structure increases chaos
        float chaos = 0.8f + params.inharmonicity * params.structure;
        float freqRatio = 1.0f + static_cast<float>(i) * chaos;
        modes[i].frequency = baseFreq * freqRatio;

        float baseDecay = calculateDecay(0.993f, modes[i].frequency, params.sizeMeters);
        modes[i].decay = baseDecay;

        modes[i].Q = (1.0f / (1.0f - baseDecay)) * (modes[i].frequency / static_cast<float>(sr));
        modes[i].Q = std::clamp(modes[i].Q, 2.0f, 80.0f);

        modes[i].initialAmplitude = 1.0f / (1.0f + static_cast<float>(i) * 0.2f);
        modes[i].prepare(sr);
    }
}

void ModalResonatorBank::initializeChimeModes()
{
    // Chimes are nearly harmonic
    float baseFreq = 300.0f / params.sizeMeters;

    for (int i = 0; i < params.numModes; ++i)
    {
        float freqRatio = static_cast<float>(i + 1);
        modes[i].frequency = baseFreq * freqRatio;

        float baseDecay = calculateDecay(0.992f, modes[i].frequency, params.sizeMeters);
        modes[i].decay = baseDecay;

        modes[i].Q = (1.0f / (1.0f - baseDecay)) * (modes[i].frequency / static_cast<float>(sr));
        modes[i].Q = std::clamp(modes[i].Q, 3.0f, 100.0f);

        modes[i].initialAmplitude = 1.0f / (1.0f + static_cast<float>(i) * 0.12f);
        modes[i].prepare(sr);
    }
}

void ModalResonatorBank::initializeBowlModes()
{
    // Singing bowls have harmonic+ partials
    float baseFreq = 180.0f / params.sizeMeters;

    for (int i = 0; i < params.numModes; ++i)
    {
        float freqRatio = 1.0f + static_cast<float>(i) * 1.1f;
        modes[i].frequency = baseFreq * freqRatio;

        float baseDecay = calculateDecay(0.998f, modes[i].frequency, params.sizeMeters); // Very long decay
        modes[i].decay = baseDecay;

        // Very high Q for singing bowls
        modes[i].Q = (1.0f / (1.0f - baseDecay)) * (modes[i].frequency / static_cast<float>(sr));
        modes[i].Q = std::clamp(modes[i].Q, 10.0f, 200.0f);

        modes[i].initialAmplitude = 1.0f / (1.0f + static_cast<float>(i) * 0.08f);
        modes[i].prepare(sr);
    }
}

float ModalResonatorBank::calculateDecay(float baseDecay, float frequency, float size)
{
    // Larger instruments have MUCH longer decay (giant scale effect)
    // Size > 1.0m should dramatically increase decay time
    float sizeMultiplier = 1.0f + (size - 1.0f) * 0.5f; // Increased from 0.1 to 0.5 for giant scale

    // Lower frequencies decay more slowly
    float freqMultiplier = 1.0f + (440.0f - frequency) / 440.0f * 0.2f; // Increased from 0.1 to 0.2

    float decay = baseDecay * sizeMultiplier * freqMultiplier;

    // Apply global damping (reduced for giant instruments)
    float dampingFactor = params.damping;
    if (size > 2.0f)
    {
        // Giant instruments have less damping
        dampingFactor *= 0.5f;
    }
    decay = 1.0f - (1.0f - decay) * (1.0f - dampingFactor);

    // For giant instruments (> 2m), allow decay to be extremely close to 1.0
    float minDecay = (size > 2.0f) ? 0.999f : 0.9f;
    float maxDecay = (size > 2.0f) ? 0.99999f : 0.9999f;

    // Clamp
    return std::clamp(decay, minDecay, maxDecay);
}

//==============================================================================
// StrikeExciter Implementation
//==============================================================================

StrikeExciter::StrikeExciter()
    : rng(42)  // Fixed seed for determinism
{
}

void StrikeExciter::prepare(double sampleRate)
{
    sr = sampleRate;
    reset();
}

void StrikeExciter::reset()
{
    clickPhase = 0.0f;
    clickDecay = 0.0f;
}

float StrikeExciter::processSample(float velocity, float force, float contactArea, float roughness)
{
    float output = 0.0f;

    // Generate click transient
    if (clickDecay > 0.001f)
    {
        output += generateClick() * params.clickAmount;
        clickDecay *= 0.95f;
    }

    // Generate mallet noise
    if (force > 0.0f)
    {
        output += generateNoise(roughness) * params.noiseAmount * force;
    }

    // Apply brightness filter (simple highpass/lowpass balance)
    float brightness = params.brightness;
    float brightComponent = output * brightness;
    float darkComponent = output * (1.0f - brightness) * 0.5f;
    output = brightComponent + darkComponent;

    return output * velocity;
}

void StrikeExciter::setParameters(const Parameters& p)
{
    params = p;
}

float StrikeExciter::generateClick()
{
    // Sharp exponential click
    clickPhase += 0.3f;
    float click = std::exp(-clickPhase * 3.0f) * SchillingerEcosystem::DSP::fastSineLookup(clickPhase * 20.0f);
    return click * clickDecay;
}

float StrikeExciter::generateNoise(float roughness)
{
    // Filtered noise based on mallet type
    float noise = rng.next();

    // Mallet type affects noise color
    switch (params.malletType)
    {
        case MalletType::Soft:
            // More low frequency noise
            noise *= 0.3f;
            break;
        case MalletType::Medium:
            noise *= 0.5f;
            break;
        case MalletType::Hard:
            // More high frequency noise
            noise *= 0.7f;
            break;
        case MalletType::Metal:
            // Very bright, harsh noise
            noise *= 1.0f;
            break;
    }

    return noise * (0.5f + roughness * 0.5f);
}

//==============================================================================
// NonlinearDispersion Implementation
//==============================================================================

NonlinearDispersion::NonlinearDispersion()
{
    initializeDelays();
}

void NonlinearDispersion::prepare(double sampleRate)
{
    sr = sampleRate;
    initializeDelays();
}

void NonlinearDispersion::reset()
{
    std::fill(allpassDelays.begin(), allpassDelays.end(), 0.0f);
    writeIndex = 0;
}

float NonlinearDispersion::processSample(float input, float inharmonicity)
{
    // Simple allpass-based dispersion
    // More inharmonicity = more phase distortion at high frequencies

    if (allpassDelays.empty())
        return input;

    // Process through allpass filters
    float output = input;
    int numStages = static_cast<int>(allpassDelays.size());

    for (int i = 0; i < numStages; ++i)
    {
        float delay = allpassDelays[i];
        float coefficient = inharmonicity * 0.5f * (1.0f - static_cast<float>(i) / numStages);

        // First-order allpass
        float temp = output - coefficient * delay;
        float newDelay = coefficient * temp + delay;
        output = temp;

        allpassDelays[i] = newDelay;
    }

    return output;
}

void NonlinearDispersion::setInharmonicity(float amount)
{
    inharmonicity = std::clamp(amount, 0.0f, 1.0f);
}

void NonlinearDispersion::initializeDelays()
{
    constexpr int numDelays = 4;
    allpassDelays.resize(numDelays, 0.0f);
    delaySizes.resize(numDelays);

    // Prime number delay sizes for rich dispersion
    delaySizes[0] = 7;
    delaySizes[1] = 11;
    delaySizes[2] = 13;
    delaySizes[3] = 17;
}

//==============================================================================
// StereoRadiationPattern Implementation
//==============================================================================

StereoRadiationPattern::StereoRadiationPattern()
{
}

void StereoRadiationPattern::prepare(double sampleRate)
{
    sr = sampleRate;
    reset();
}

void StereoRadiationPattern::reset()
{
    hfLeft = 0.0f;
    hfRight = 0.0f;
    lfLeft = 0.0f;
    lfRight = 0.0f;
}

void StereoRadiationPattern::processSample(float input, float& left, float& right)
{
    // Frequency-dependent panning
    // Low frequencies are omnidirectional
    // High frequencies are directional

    float lfGain = 0.707f; // -3dB for each channel (mono-ish)
    float hfGain = 1.0f;

    // Calculate stereo gains
    float leftGain, rightGain;
    calculatePanGains(1000.0f, leftGain, rightGain); // Assume 1kHz for "high freq"

    // Apply rotation
    float rotationOffset = params.rotation * static_cast<float>(M_PI) * 0.25f;
    float cosRot = SchillingerEcosystem::DSP::fastCosineLookup(rotationOffset);
    float sinRot = SchillingerEcosystem::DSP::fastSineLookup(rotationOffset);

    float rotatedLeft = leftGain * cosRot - rightGain * sinRot;
    float rotatedRight = leftGain * sinRot + rightGain * cosRot;

    // Combine LF and HF
    float width = params.width;
    float stereoSpread = width * 0.5f;

    left = input * (lfGain * (1.0f - stereoSpread) + rotatedLeft * stereoSpread);
    right = input * (lfGain * (1.0f - stereoSpread) + rotatedRight * stereoSpread);

    // Apply directionality
    float dir = params.highFrequencyDirectionality;
    left = left * (1.0f - dir * 0.3f) + left * dir;
    right = right * (1.0f - dir * 0.3f) + right * dir;
}

void StereoRadiationPattern::setParameters(const Parameters& p)
{
    params = p;
}

void StereoRadiationPattern::calculatePanGains(float frequency, float& leftGain, float& rightGain)
{
    // Simple constant power panning
    float pan = 0.5f; // Center by default
    float angle = pan * static_cast<float>(M_PI) * 0.5f;

    leftGain = SchillingerEcosystem::DSP::fastCosineLookup(angle);
    rightGain = SchillingerEcosystem::DSP::fastSineLookup(angle);
}

//==============================================================================
// GiantPercussionVoice Implementation
//==============================================================================

void GiantPercussionVoice::prepare(double sampleRate)
{
    resonator.prepare(sampleRate);
    exciter.prepare(sampleRate);
    dispersion.prepare(sampleRate);
    radiation.prepare(sampleRate);
}

void GiantPercussionVoice::reset()
{
    resonator.reset();
    exciter.reset();
    dispersion.reset();
    radiation.reset();
    active = false;
    midiNote = -1;
    velocity = 0.0f;
}

void GiantPercussionVoice::trigger(int note, float vel, const GiantGestureParameters& gesture,
                                   const GiantScaleParameters& scaleParams)
{
    midiNote = note;
    velocity = vel;
    this->gesture = gesture;
    this->scale = scaleParams;

    // Trigger exciter
    float excitation = exciter.processSample(vel, gesture.force, gesture.contactArea, gesture.roughness);

    // Strike resonator
    resonator.strike(vel, gesture.force, gesture.contactArea);

    active = true;
}

float GiantPercussionVoice::processSample(float& left, float& right)
{
    if (!active)
        return 0.0f;

    // Process resonator
    float mono = resonator.processSample();

    // Apply dispersion
    mono = dispersion.processSample(mono, 0.3f);

    // Check if voice is done
    if (resonator.getTotalEnergy() < 0.0001f)
        active = false;

    // Apply stereo radiation
    radiation.processSample(mono, left, right);

    return mono;
}

bool GiantPercussionVoice::isActive() const
{
    return active;
}

//==============================================================================
// GiantPercussionVoiceManager Implementation
//==============================================================================

GiantPercussionVoiceManager::GiantPercussionVoiceManager()
{
}

void GiantPercussionVoiceManager::prepare(double sampleRate, int maxVoices)
{
    currentSampleRate = sampleRate;
    voices.clear();

    for (int i = 0; i < maxVoices; ++i)
    {
        auto voice = std::make_unique<GiantPercussionVoice>();
        voice->prepare(sampleRate);
        voices.push_back(std::move(voice));
    }
}

void GiantPercussionVoiceManager::reset()
{
    for (auto& voice : voices)
        voice->reset();
}

GiantPercussionVoice* GiantPercussionVoiceManager::findFreeVoice()
{
    // First try to find inactive voice
    for (auto& voice : voices)
    {
        if (!voice->isActive())
            return voice.get();
    }

    // If all active, steal oldest (first in list)
    return voices[0].get();
}

GiantPercussionVoice* GiantPercussionVoiceManager::findVoiceForNote(int note)
{
    // Find active voice for this note
    for (auto& voice : voices)
    {
        if (voice->isActive() && voice->midiNote == note)
            return voice.get();
    }
    return nullptr;
}

void GiantPercussionVoiceManager::handleNoteOn(int note, float velocity, const GiantGestureParameters& gesture,
                                                const GiantScaleParameters& scale)
{
    GiantPercussionVoice* voice = findFreeVoice();
    if (voice)
        voice->trigger(note, velocity, gesture, scale);
}

void GiantPercussionVoiceManager::handleNoteOff(int note)
{
    GiantPercussionVoice* voice = findVoiceForNote(note);
    if (voice)
    {
        // Percussion naturally decays, so note off doesn't stop immediately
        // Just mark for eventual cleanup
        voice->active = false;
    }
}

void GiantPercussionVoiceManager::allNotesOff()
{
    for (auto& voice : voices)
        voice->reset();
}

void GiantPercussionVoiceManager::processSample(float& left, float& right)
{
    left = 0.0f;
    right = 0.0f;

    for (auto& voice : voices)
    {
        if (voice->isActive())
        {
            float voiceLeft, voiceRight;
            voice->processSample(voiceLeft, voiceRight);
            left += voiceLeft;
            right += voiceRight;
        }
    }
}

int GiantPercussionVoiceManager::getActiveVoiceCount() const
{
    int count = 0;
    for (const auto& voice : voices)
        if (voice->isActive())
            ++count;
    return count;
}

void GiantPercussionVoiceManager::setResonatorParameters(const ModalResonatorBank::Parameters& params)
{
    for (auto& voice : voices)
        voice->resonator.setParameters(params);
}

void GiantPercussionVoiceManager::setExciterParameters(const StrikeExciter::Parameters& params)
{
    for (auto& voice : voices)
        voice->exciter.setParameters(params);
}

void GiantPercussionVoiceManager::setRadiationParameters(const StereoRadiationPattern::Parameters& params)
{
    for (auto& voice : voices)
        voice->radiation.setParameters(params);
}

//==============================================================================
// AetherGiantPercussionPureDSP Implementation
//==============================================================================

AetherGiantPercussionPureDSP::AetherGiantPercussionPureDSP()
{
}

AetherGiantPercussionPureDSP::~AetherGiantPercussionPureDSP()
{
}

bool AetherGiantPercussionPureDSP::prepare(double sampleRate, int blockSize)
{
    sampleRate_ = sampleRate;
    blockSize_ = blockSize;

    voiceManager_.prepare(sampleRate, maxVoices_);

    applyParameters();

    return true;
}

void AetherGiantPercussionPureDSP::reset()
{
    voiceManager_.reset();
}

void AetherGiantPercussionPureDSP::process(float** outputs, int numChannels, int numSamples)
{
    // Clear buffers
    for (int ch = 0; ch < numChannels; ++ch)
        std::fill(outputs[ch], outputs[ch] + numSamples, 0.0f);

    // Process samples
    for (int i = 0; i < numSamples; ++i)
    {
        float left = 0.0f;
        float right = 0.0f;

        voiceManager_.processSample(left, right);

        // Apply master volume
        left *= params_.masterVolume;
        right *= params_.masterVolume;

        // Soft clamp to prevent overflow
        left = std::clamp(left, -1.0f, 1.0f);
        right = std::clamp(right, -1.0f, 1.0f);

        if (numChannels >= 2)
        {
            outputs[0][i] += left;
            outputs[1][i] += right;
        }
        else if (numChannels == 1)
        {
            outputs[0][i] += (left + right) * 0.5f;
        }
    }
}

void AetherGiantPercussionPureDSP::handleEvent(const ScheduledEvent& event)
{
    switch (event.type)
    {
        case ScheduledEvent::NOTE_ON:
        {
            GiantScaleParameters scale;
            scale.scaleMeters = params_.scaleMeters;
            scale.massBias = params_.massBias;
            scale.airLoss = params_.airLoss;
            scale.transientSlowing = params_.transientSlowing;

            GiantGestureParameters gesture;
            gesture.force = params_.force;
            gesture.speed = params_.speed;
            gesture.contactArea = params_.contactArea;
            gesture.roughness = params_.roughness;

            voiceManager_.handleNoteOn(event.data.note.midiNote, event.data.note.velocity, gesture, scale);
            break;
        }

        case ScheduledEvent::NOTE_OFF:
            voiceManager_.handleNoteOff(event.data.note.midiNote);
            break;

        case ScheduledEvent::RESET:
            reset();
            break;

        default:
            break;
    }
}

float AetherGiantPercussionPureDSP::getParameter(const char* paramId) const
{
    std::string id(paramId);

    if (id == "instrumentType") return params_.instrumentType;
    if (id == "sizeMeters") return params_.sizeMeters;
    if (id == "thickness") return params_.thickness;
    if (id == "materialHardness") return params_.materialHardness;
    if (id == "damping") return params_.damping;
    if (id == "numModes") return params_.numModes;
    if (id == "inharmonicity") return params_.inharmonicity;
    if (id == "structure") return params_.structure;
    if (id == "malletType") return params_.malletType;
    if (id == "clickAmount") return params_.clickAmount;
    if (id == "noiseAmount") return params_.noiseAmount;
    if (id == "brightness") return params_.brightness;
    if (id == "stereoWidth") return params_.stereoWidth;
    if (id == "hfDirectionality") return params_.hfDirectionality;
    if (id == "scaleMeters") return params_.scaleMeters;
    if (id == "massBias") return params_.massBias;
    if (id == "airLoss") return params_.airLoss;
    if (id == "transientSlowing") return params_.transientSlowing;
    if (id == "force") return params_.force;
    if (id == "speed") return params_.speed;
    if (id == "contactArea") return params_.contactArea;
    if (id == "roughness") return params_.roughness;
    if (id == "masterVolume") return params_.masterVolume;

    return 0.0f;
}

void AetherGiantPercussionPureDSP::setParameter(const char* paramId, float value)
{
    std::string id(paramId);

    if (id == "instrumentType") params_.instrumentType = value;
    else if (id == "sizeMeters") params_.sizeMeters = value;
    else if (id == "thickness") params_.thickness = value;
    else if (id == "materialHardness") params_.materialHardness = value;
    else if (id == "damping") params_.damping = value;
    else if (id == "numModes") params_.numModes = static_cast<int>(value);
    else if (id == "inharmonicity") params_.inharmonicity = value;
    else if (id == "structure") params_.structure = value;
    else if (id == "malletType") params_.malletType = value;
    else if (id == "clickAmount") params_.clickAmount = value;
    else if (id == "noiseAmount") params_.noiseAmount = value;
    else if (id == "brightness") params_.brightness = value;
    else if (id == "stereoWidth") params_.stereoWidth = value;
    else if (id == "hfDirectionality") params_.hfDirectionality = value;
    else if (id == "scaleMeters") params_.scaleMeters = value;
    else if (id == "massBias") params_.massBias = value;
    else if (id == "airLoss") params_.airLoss = value;
    else if (id == "transientSlowing") params_.transientSlowing = value;
    else if (id == "force") params_.force = value;
    else if (id == "speed") params_.speed = value;
    else if (id == "contactArea") params_.contactArea = value;
    else if (id == "roughness") params_.roughness = value;
    else if (id == "masterVolume") params_.masterVolume = value;

    applyParameters();
}

bool AetherGiantPercussionPureDSP::savePreset(char* jsonBuffer, int jsonBufferSize) const
{
    int offset = 0;

    writeJsonParameter("instrumentType", params_.instrumentType, jsonBuffer, offset, jsonBufferSize);
    writeJsonParameter("sizeMeters", params_.sizeMeters, jsonBuffer, offset, jsonBufferSize);
    writeJsonParameter("thickness", params_.thickness, jsonBuffer, offset, jsonBufferSize);
    writeJsonParameter("materialHardness", params_.materialHardness, jsonBuffer, offset, jsonBufferSize);
    writeJsonParameter("damping", params_.damping, jsonBuffer, offset, jsonBufferSize);
    writeJsonParameter("numModes", params_.numModes, jsonBuffer, offset, jsonBufferSize);
    writeJsonParameter("inharmonicity", params_.inharmonicity, jsonBuffer, offset, jsonBufferSize);
    writeJsonParameter("structure", params_.structure, jsonBuffer, offset, jsonBufferSize);
    writeJsonParameter("malletType", params_.malletType, jsonBuffer, offset, jsonBufferSize);
    writeJsonParameter("clickAmount", params_.clickAmount, jsonBuffer, offset, jsonBufferSize);
    writeJsonParameter("noiseAmount", params_.noiseAmount, jsonBuffer, offset, jsonBufferSize);
    writeJsonParameter("brightness", params_.brightness, jsonBuffer, offset, jsonBufferSize);
    writeJsonParameter("stereoWidth", params_.stereoWidth, jsonBuffer, offset, jsonBufferSize);
    writeJsonParameter("hfDirectionality", params_.hfDirectionality, jsonBuffer, offset, jsonBufferSize);
    writeJsonParameter("scaleMeters", params_.scaleMeters, jsonBuffer, offset, jsonBufferSize);
    writeJsonParameter("massBias", params_.massBias, jsonBuffer, offset, jsonBufferSize);
    writeJsonParameter("airLoss", params_.airLoss, jsonBuffer, offset, jsonBufferSize);
    writeJsonParameter("transientSlowing", params_.transientSlowing, jsonBuffer, offset, jsonBufferSize);
    writeJsonParameter("force", params_.force, jsonBuffer, offset, jsonBufferSize);
    writeJsonParameter("speed", params_.speed, jsonBuffer, offset, jsonBufferSize);
    writeJsonParameter("contactArea", params_.contactArea, jsonBuffer, offset, jsonBufferSize);
    writeJsonParameter("roughness", params_.roughness, jsonBuffer, offset, jsonBufferSize);
    writeJsonParameter("masterVolume", params_.masterVolume, jsonBuffer, offset, jsonBufferSize);

    return (offset < jsonBufferSize);
}

bool AetherGiantPercussionPureDSP::loadPreset(const char* jsonData)
{
    double value;

    if (parseJsonParameter(jsonData, "instrumentType", value))
        params_.instrumentType = static_cast<float>(value);
    if (parseJsonParameter(jsonData, "sizeMeters", value))
        params_.sizeMeters = static_cast<float>(value);
    if (parseJsonParameter(jsonData, "thickness", value))
        params_.thickness = static_cast<float>(value);
    if (parseJsonParameter(jsonData, "materialHardness", value))
        params_.materialHardness = static_cast<float>(value);
    if (parseJsonParameter(jsonData, "damping", value))
        params_.damping = static_cast<float>(value);
    if (parseJsonParameter(jsonData, "numModes", value))
        params_.numModes = static_cast<int>(value);
    if (parseJsonParameter(jsonData, "inharmonicity", value))
        params_.inharmonicity = static_cast<float>(value);
    if (parseJsonParameter(jsonData, "structure", value))
        params_.structure = static_cast<float>(value);
    if (parseJsonParameter(jsonData, "malletType", value))
        params_.malletType = static_cast<float>(value);
    if (parseJsonParameter(jsonData, "clickAmount", value))
        params_.clickAmount = static_cast<float>(value);
    if (parseJsonParameter(jsonData, "noiseAmount", value))
        params_.noiseAmount = static_cast<float>(value);
    if (parseJsonParameter(jsonData, "brightness", value))
        params_.brightness = static_cast<float>(value);
    if (parseJsonParameter(jsonData, "stereoWidth", value))
        params_.stereoWidth = static_cast<float>(value);
    if (parseJsonParameter(jsonData, "hfDirectionality", value))
        params_.hfDirectionality = static_cast<float>(value);
    if (parseJsonParameter(jsonData, "scaleMeters", value))
        params_.scaleMeters = static_cast<float>(value);
    if (parseJsonParameter(jsonData, "massBias", value))
        params_.massBias = static_cast<float>(value);
    if (parseJsonParameter(jsonData, "airLoss", value))
        params_.airLoss = static_cast<float>(value);
    if (parseJsonParameter(jsonData, "transientSlowing", value))
        params_.transientSlowing = static_cast<float>(value);
    if (parseJsonParameter(jsonData, "force", value))
        params_.force = static_cast<float>(value);
    if (parseJsonParameter(jsonData, "speed", value))
        params_.speed = static_cast<float>(value);
    if (parseJsonParameter(jsonData, "contactArea", value))
        params_.contactArea = static_cast<float>(value);
    if (parseJsonParameter(jsonData, "roughness", value))
        params_.roughness = static_cast<float>(value);
    if (parseJsonParameter(jsonData, "masterVolume", value))
        params_.masterVolume = static_cast<float>(value);

    applyParameters();
    return true;
}

int AetherGiantPercussionPureDSP::getActiveVoiceCount() const
{
    return voiceManager_.getActiveVoiceCount();
}

void AetherGiantPercussionPureDSP::applyParameters()
{
    ModalResonatorBank::Parameters resonatorParams;
    resonatorParams.instrumentType = static_cast<ModalResonatorBank::InstrumentType>(
        static_cast<int>(params_.instrumentType));
    resonatorParams.sizeMeters = params_.sizeMeters;
    resonatorParams.thickness = params_.thickness;
    resonatorParams.materialHardness = params_.materialHardness;
    resonatorParams.damping = params_.damping;
    resonatorParams.numModes = static_cast<int>(params_.numModes);
    resonatorParams.inharmonicity = params_.inharmonicity;
    resonatorParams.structure = params_.structure;

    voiceManager_.setResonatorParameters(resonatorParams);

    StrikeExciter::Parameters exciterParams;
    exciterParams.malletType = static_cast<StrikeExciter::MalletType>(
        static_cast<int>(params_.malletType));
    exciterParams.clickAmount = params_.clickAmount;
    exciterParams.noiseAmount = params_.noiseAmount;
    exciterParams.brightness = params_.brightness;

    voiceManager_.setExciterParameters(exciterParams);

    StereoRadiationPattern::Parameters radiationParams;
    radiationParams.width = params_.stereoWidth;
    radiationParams.highFrequencyDirectionality = params_.hfDirectionality;
    radiationParams.rotation = 0.0f;

    voiceManager_.setRadiationParameters(radiationParams);
}

float AetherGiantPercussionPureDSP::calculateFrequency(int midiNote) const
{
    // Use LookupTables for MIDI to frequency conversion
    return SchillingerEcosystem::DSP::LookupTables::getInstance().midiToFreq(static_cast<float>(midiNote));
}

bool AetherGiantPercussionPureDSP::writeJsonParameter(const char* name, double value,
                                                      char* buffer, int& offset, int bufferSize) const
{
    std::string json = std::string("\"") + name + "\": " + std::to_string(value) + ",\n";

    if (offset + json.length() >= static_cast<size_t>(bufferSize))
        return false;

    std::memcpy(buffer + offset, json.c_str(), json.length());
    offset += static_cast<int>(json.length());

    return true;
}

bool AetherGiantPercussionPureDSP::parseJsonParameter(const char* json, const char* param, double& value) const
{
    std::string search = std::string("\"") + param + "\":";
    const char* found = std::strstr(json, search.c_str());

    if (!found)
        return false;

    found += search.length();

    // Parse number
    char* end;
    value = std::strtod(found, &end);

    return (end != found);
}

//==============================================================================
// Factory Registration
//==============================================================================

// Factory registration disabled for plugin builds
/*
namespace {
    struct AetherGiantPercussionRegistrar {
        AetherGiantPercussionRegistrar() {
            registerInstrumentFactory("AetherGiantPercussion", []() -> InstrumentDSP* {
                return new AetherGiantPercussionPureDSP();
            });
        }
    } registrar;
}
*/

}  // namespace DSP
