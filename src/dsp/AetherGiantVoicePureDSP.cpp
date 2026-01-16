/*
  ==============================================================================

    AetherGiantVoicePureDSP.cpp
    Created: January 9, 2026
    Author: Bret Bouchard

    Pure DSP implementation of Giant Voice / Roar Engine
    - Physical modeling vocal synthesis
    - Formant filter bank (vocal tract)
    - Glottal excitation source
    - Multi-formant shaping (F1, F2, F3, F4)
    - Scale-aware: giant voice = massive vocal tract, slow articulation
    - MPE gesture mapping: pressure→diaphragm, timbre→vowel formants
    - Deep fundamentals (50-100Hz for giant voice)

  ==============================================================================
*/

#include "dsp/AetherGiantVoiceDSP.h"
#include "../../../../include/dsp/LookupTables.h"
#include "../../../../include/dsp/FastRNG.h"
#include <cstring>
#include <cstdio>
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
    // ARM NEON implementations

    inline float horizontalSum(float32x4_t v)
    {
        // Sum all 4 elements
        float32x2_t sum = vadd_f32(vget_low_f32(v), vget_high_f32(v));
        sum = vpadd_f32(sum, sum);
        return vget_lane_f32(sum, 0);
    }

    inline float processFormantsNEON(float input, GiantFormantFilter* formants, size_t count)
    {
        if (count >= 4)
        {
            // Process 4 formants in parallel
            float32x4_t outputs = vdupq_n_f32(0.0f);

            for (size_t i = 0; i < count; i += 4)
            {
                // Process each formant individually (biquad is sequential)
                // but we can vectorize the accumulation
                float f0 = formants[i + 0].processSample(input);
                float f1 = formants[i + 1].processSample(input);
                float f2 = formants[i + 2].processSample(input);
                float f3 = formants[i + 3].processSample(input);

                // Combine into vector and accumulate
                float32x4_t formantOutputs = vsetq_lane_f32(f0, vdupq_n_f32(0.0f), 0);
                formantOutputs = vsetq_lane_f32(f1, formantOutputs, 1);
                formantOutputs = vsetq_lane_f32(f2, formantOutputs, 2);
                formantOutputs = vsetq_lane_f32(f3, formantOutputs, 3);

                outputs = vaddq_f32(outputs, formantOutputs);
            }

            return horizontalSum(outputs);
        }

        // Fallback for less than 4 formants
        float output = 0.0f;
        for (size_t i = 0; i < count; ++i)
        {
            output += formants[i].processSample(input);
        }
        return output;
    }

#elif DSP_SIMD_AVX_AVAILABLE
    // x86 AVX implementations

    inline float horizontalSum(__m256 v)
    {
        // Sum all 8 elements
        __m128 sum128 = _mm_add_ps(_mm256_castps256_ps128(v),
                                   _mm256_extractf128_ps(v, 1));
        sum128 = _mm_hadd_ps(sum128, sum128);
        sum128 = _mm_hadd_ps(sum128, sum128);
        return _mm_cvtss_f32(sum128);
    }

    inline float processFormantsAVX(float input, GiantFormantFilter* formants, size_t count)
    {
        float output = 0.0f;

        // Process 8 formants at a time
        size_t i = 0;
        for (; i + 8 <= count; i += 8)
        {
            // Process 8 formants
            float f[8];
            for (int j = 0; j < 8; ++j)
            {
                f[j] = formants[i + j].processSample(input);
            }

            // Load into AVX vector
            __m256 formantOutputs = _mm256_set_ps(f[7], f[6], f[5], f[4],
                                                   f[3], f[2], f[1], f[0]);

            // Accumulate using temporary sum
            __m256 sum = _mm256_loadu_ps(&output);
            sum = _mm256_add_ps(sum, formantOutputs);

            // Horizontal sum and store
            output = horizontalSum(sum);
        }

        // Process remaining formants
        for (; i < count; ++i)
        {
            output += formants[i].processSample(input);
        }

        return output;
    }

#elif DSP_SIMD_SSE_AVAILABLE
    // x86 SSE implementations

    inline float horizontalSum(__m128 v)
    {
        __m128 sum = _mm_hadd_ps(v, v);
        sum = _mm_hadd_ps(sum, sum);
        return _mm_cvtss_f32(sum);
    }

    inline float processFormantsSSE(float input, GiantFormantFilter* formants, size_t count)
    {
        float output = 0.0f;

        // Process 4 formants at a time
        size_t i = 0;
        for (; i + 4 <= count; i += 4)
        {
            // Process 4 formants
            float f0 = formants[i + 0].processSample(input);
            float f1 = formants[i + 1].processSample(input);
            float f2 = formants[i + 2].processSample(input);
            float f3 = formants[i + 3].processSample(input);

            // Load into SSE vector
            __m128 formantOutputs = _mm_set_ps(f3, f2, f1, f0);

            // Accumulate
            __m128 sum = _mm_set_ss(output);
            sum = _mm_add_ps(sum, formantOutputs);

            output = horizontalSum(sum);
        }

        // Process remaining formants
        for (; i < count; ++i)
        {
            output += formants[i].processSample(input);
        }

        return output;
    }

#endif

} // namespace SIMD

//==============================================================================
// Utility Functions
//==============================================================================

static inline float clamp(float x, float min, float max)
{
    return (x < min) ? min : (x > max) ? max : x;
}

static inline float lerp(float a, float b, float t)
{
    return a + t * (b - a);
}

static inline float midiToFrequency(int midiNote)
{
    // Use LookupTables for MIDI to frequency conversion
    return SchillingerEcosystem::DSP::LookupTables::getInstance().midiToFreq(static_cast<float>(midiNote));
}

//==============================================================================
// Formant Lookup Tables
//==============================================================================

/**
 * Vowel formant definitions (based on speech synthesis research)
 * Frequencies in Hz for adult male, female, and giant-scaled voices
 */
struct VowelFormants
{
    const char* name;
    float f1, f2, f3, f4;  // Formant frequencies
    float b1, b2, b3, b4;  // Formant bandwidths (Hz)
};

// Standard vowel formants (adult male reference)
static const VowelFormants standardVowelTable[] =
{
    // Vowel     F1     F2      F3      F4     B1     B2     B3     B4
    { "Ah",     730,   1090,   2440,   3400,  80,    90,    120,   130 },
    { "Eh",     530,   1840,   2480,   3320,  70,    100,   110,   120 },
    { "Ee",     270,   2290,   3010,   3340,  60,    90,    100,   120 },
    { "Oh",     570,   840,    2410,   3370,  80,    80,    110,   130 },
    { "Oo",     300,   870,    2240,   3370,  70,    80,    100,   120 },
    { "Uh",     640,   1190,   2390,   3370,  70,    90,    110,   130 },
    { "Ih",     390,   2300,   2980,   3360,  60,    90,    100,   120 }
};

// Giant-scaled vowel formants (lower frequencies, wider bandwidths)
static const VowelFormants giantVowelTable[] =
{
    // Vowel     F1     F2      F3      F4     B1     B2     B3     B4
    { "Ah",     440,   650,    1460,   2040,  120,   135,   180,   195 },
    { "Eh",     320,   1100,   1490,   1990,  105,   150,   165,   180 },
    { "Ee",     160,   1370,   1810,   2000,  90,    135,   150,   180 },
    { "Oh",     340,   500,    1450,   2020,  120,   120,   165,   195 },
    { "Oo",     180,   520,    1340,   2020,  105,   120,   150,   180 },
    { "Uh",     380,   710,    1430,   2020,  105,   135,   165,   195 },
    { "Ih",     230,   1380,   1790,   2020,  90,    135,   150,   180 }
};

/**
 * Calculate frequency-dependent Q factor for formant filters
 * Based on vocal tract acoustics: higher formants have narrower relative bandwidth
 *
 * @param formantFreq  Center frequency of the formant (Hz)
 * @param bandwidthHz  Absolute bandwidth in Hz (typically 50-150 Hz for human voice)
 * @return             Q factor (frequency / bandwidth)
 */
static inline float calculateFormantQ(float formantFreq, float bandwidthHz)
{
    // Q = center frequency / bandwidth
    // Ensure minimum bandwidth to prevent excessive Q
    float minBandwidth = 50.0f;  // Minimum 50 Hz bandwidth
    float actualBandwidth = std::max(bandwidthHz, minBandwidth);

    return formantFreq / actualBandwidth;
}

/**
 * Convert bandwidth in Hz to bandwidth in octaves (for filter design)
 * BW_octaves = bandwidth_hz / (center_freq * ln(2))
 */
static inline float bandwidthHzToOctaves(float bandwidthHz, float centerFreq)
{
    return bandwidthHz / (centerFreq * 0.69314718f);
}

/**
 * Get vowel formant data by index
 * @param vowelIndex  Vowel index (0-6: Ah, Eh, Ee, Oh, Oo, Uh, Ih)
 * @param scale       Scale factor (1.0 = normal, 0.6 = giant)
 */
static inline VowelFormants getVowelFormants(int vowelIndex, float scale = 0.6f)
{
    vowelIndex = clamp(vowelIndex, 0, 6);

    // Interpolate between standard and giant formants based on scale
    const VowelFormants& standard = standardVowelTable[vowelIndex];
    const VowelFormants& giant = giantVowelTable[vowelIndex];

    VowelFormants result;
    float t = (1.0f - scale) / 0.4f;  // Map scale to interpolation factor

    result.name = standard.name;
    result.f1 = lerp(giant.f1, standard.f1, t);
    result.f2 = lerp(giant.f2, standard.f2, t);
    result.f3 = lerp(giant.f3, standard.f3, t);
    result.f4 = lerp(giant.f4, standard.f4, t);
    result.b1 = lerp(giant.b1, standard.b1, t);
    result.b2 = lerp(giant.b2, standard.b2, t);
    result.b3 = lerp(giant.b3, standard.b3, t);
    result.b4 = lerp(giant.b4, standard.b4, t);

    return result;
}

//==============================================================================
// BreathPressureGenerator Implementation
//==============================================================================

BreathPressureGenerator::BreathPressureGenerator()
    : rng(42)  // Fixed seed for determinism
{
    reset();
}

void BreathPressureGenerator::prepare(double sampleRate)
{
    sr = sampleRate;
    reset();
}

void BreathPressureGenerator::reset()
{
    currentPressure = 0.0f;
    targetPressure = 0.0f;
    envelopePhase = 0.0f;
    active = false;
    inOvershoot = false;
}

void BreathPressureGenerator::trigger(float velocity, float force, float aggression)
{
    active = true;
    envelopePhase = 0.0f;
    inOvershoot = true;

    // Target pressure depends on velocity and diaphragm force
    targetPressure = velocity * (0.5f + 0.5f * force);

    // Higher aggression = more overshoot
    float overshootAmount = params.pressureOvershoot * (1.0f + aggression);
    currentPressure = targetPressure * (1.0f + overshootAmount);
}

void BreathPressureGenerator::release(bool damping)
{
    active = false;
    envelopePhase = 2.0f;  // Release phase

    if (damping)
    {
        targetPressure = 0.0f;
        params.releaseTime = 0.05f;  // Fast release
    }
}

float BreathPressureGenerator::processSample()
{
    if (!active && envelopePhase >= 2.0f && currentPressure <= 0.001f)
    {
        currentPressure = 0.0f;
        return 0.0f;
    }

    // Process envelope
    if (envelopePhase < 1.0f)  // Attack/sustain
    {
        float attackCoeff = calculateAttackCoefficient();

        if (inOvershoot)
        {
            // Decay from overshoot to sustain
            currentPressure = lerp(currentPressure, targetPressure * params.sustainLevel, attackCoeff);

            if (std::abs(currentPressure - targetPressure * params.sustainLevel) < 0.01f)
            {
                inOvershoot = false;
                envelopePhase = 1.0f;  // Sustain
            }
        }
        else
        {
            // Attack to sustain
            currentPressure = lerp(currentPressure, targetPressure * params.sustainLevel, attackCoeff);
        }
    }
    else if (envelopePhase >= 2.0f)  // Release
    {
        float releaseCoeff = calculateReleaseCoefficient();
        currentPressure = lerp(currentPressure, 0.0f, releaseCoeff);

        if (currentPressure < 0.001f)
        {
            currentPressure = 0.0f;
            return 0.0f;
        }
    }

    // Add turbulence
    float turbulence = rng.next();  // -1 to 1
    turbulence *= params.turbulenceAmount * currentPressure;

    return currentPressure + turbulence;
}

void BreathPressureGenerator::setParameters(const Parameters& p)
{
    params = p;
}

float BreathPressureGenerator::calculateAttackCoefficient() const
{
    // Convert attack time to coefficient
    float timeInSamples = params.attackTime * static_cast<float>(sr);
    return 1.0f - std::exp(-2.0f / timeInSamples);
}

float BreathPressureGenerator::calculateReleaseCoefficient() const
{
    // Convert release time to coefficient
    float timeInSamples = params.releaseTime * static_cast<float>(sr);
    return 1.0f - std::exp(-2.0f / timeInSamples);
}

//==============================================================================
// VocalFoldOscillator Implementation
//==============================================================================

VocalFoldOscillator::VocalFoldOscillator()
    : rng(42)  // Fixed seed for determinism
{
    reset();
}

void VocalFoldOscillator::prepare(double sampleRate)
{
    sr = sampleRate;
    reset();
}

void VocalFoldOscillator::reset()
{
    phase = 0.0f;
    subPhase = 0.0f;
}

float VocalFoldOscillator::processSample(float pressure)
{
    float freq = calculateInstantaneousFrequency(pressure);

    // Advance phase
    float phaseIncrement = freq / static_cast<float>(sr);
    phase += phaseIncrement;
    if (phase >= 1.0f)
        phase -= 1.0f;

    // Advance subharmonic phase
    float subFreq = freq * 0.5f;
    float subIncrement = subFreq / static_cast<float>(sr);
    subPhase += subIncrement;
    if (subPhase >= 1.0f)
        subPhase -= 1.0f;

    // Generate waveform
    float mainWave = generateWaveform(phase, params.waveformMorph);
    float subWave = generateWaveform(subPhase, params.waveformMorph);

    // Mix with subharmonics
    float output = mainWave * (1.0f - params.subharmonicMix * 0.5f) +
                   subWave * params.subharmonicMix * 0.5f;

    // Add aspiration noise (breathiness) - always present, increases with pressure
    float aspirationNoise = rng.next();
    float aspirationAmount = 0.05f + pressure * 0.15f;  // 5-20% aspiration
    output += aspirationNoise * aspirationAmount;

    // Add chaos at high pressure
    if (pressure > 0.5f)
    {
        float chaosAmount = params.chaosAmount * (pressure - 0.5f) * 2.0f;
        float chaos = rng.next();
        output += chaos * chaosAmount * 0.3f;
    }

    // Add pitch instability
    if (params.pitchInstability > 0.0f)
    {
        float jitter = rng.next();
        output += jitter * params.pitchInstability * 0.1f;
    }

    return output;
}

void VocalFoldOscillator::setParameters(const Parameters& p)
{
    params = p;
}

void VocalFoldOscillator::setFrequency(float freq)
{
    params.frequency = freq;
}

void VocalFoldOscillator::setPitchMode(PitchMode mode)
{
    params.pitchMode = mode;
}

float VocalFoldOscillator::calculateInstantaneousFrequency(float pressure) const
{
    float freq = params.frequency;

    // Apply pitch instability
    if (params.pitchMode == PitchMode::Unstable && params.pitchInstability > 0.0f)
    {
        float drift = SchillingerEcosystem::DSP::fastSineLookup(pressure * 6.28f) * params.pitchInstability * 0.1f;
        freq *= (1.0f + drift);
    }

    // Pressure affects pitch slightly
    freq *= (1.0f + pressure * 0.05f);

    return clamp(freq, 20.0f, 5000.0f);
}

float VocalFoldOscillator::generateWaveform(float phase, float morph) const
{
    // Enhanced glottal pulse model based on Rosenberg-Liljencrants-Fant (RLF) waveform
    // Models the opening and closing phases of vocal folds

    // Glottal pulse parameters
    float openPhase = 0.6f;     // Opening phase (0-1)
    float closingPhase = 0.1f;  // Fast closing phase

    float glottalPulse;
    if (phase < openPhase)
    {
        // Opening phase (sinusoidal)
        float t = phase / openPhase;
        glottalPulse = 0.5f * (1.0f - SchillingerEcosystem::DSP::fastCosineLookup(t * 3.14159265359f));
    }
    else if (phase < openPhase + closingPhase)
    {
        // Closing phase (rapid decay)
        float t = (phase - openPhase) / closingPhase;
        glottalPulse = 0.5f * (1.0f - t);
    }
    else
    {
        // Closed phase (glottis shut)
        glottalPulse = 0.0f;
    }

    // Traditional sawtooth wave
    float saw = 2.0f * phase - 1.0f;

    // Simple pulse wave
    float pulse = (phase < 0.3f) ? 1.0f : -0.5f;

    // Morph between sawtooth, simple pulse, and glottal pulse
    if (morph < 0.5f)
    {
        // Saw to simple pulse
        float t = morph * 2.0f;
        return lerp(saw, pulse, t);
    }
    else
    {
        // Simple pulse to glottal pulse
        float t = (morph - 0.5f) * 2.0f;
        return lerp(pulse, glottalPulse, t);
    }
}

//==============================================================================
// GiantFormantFilter Implementation
//==============================================================================

GiantFormantFilter::GiantFormantFilter()
{
    reset();
}

void GiantFormantFilter::prepare(double sampleRate)
{
    sr = sampleRate;
    coefficientsDirty = true;
    reset();
}

void GiantFormantFilter::reset()
{
    b0 = 1.0f; b1 = 0.0f; b2 = 0.0f;
    a1 = 0.0f; a2 = 0.0f;
    x1 = 0.0f; x2 = 0.0f;
    y1 = 0.0f; y2 = 0.0f;
}

float GiantFormantFilter::processSample(float input)
{
    if (coefficientsDirty)
    {
        calculateCoefficients();
        coefficientsDirty = false;
    }

    // Biquad direct form I
    float output = b0 * input + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;

    x2 = x1;
    x1 = input;
    y2 = y1;
    y1 = output;

    // Guard against NaN propagating through filter
    if (std::isnan(output) || std::isinf(output))
    {
        output = 0.0f;
        // Reset filter state to prevent further NaN
        x1 = 0.0f; x2 = 0.0f; y1 = 0.0f; y2 = 0.0f;
    }

    return output * amplitude;
}

void GiantFormantFilter::setFrequency(float freq)
{
    frequency = clamp(freq, 50.0f, 8000.0f);
    coefficientsDirty = true;
}

void GiantFormantFilter::setBandwidth(float bw)
{
    bandwidth = clamp(bw, 0.1f, 4.0f);
    coefficientsDirty = true;
}

void GiantFormantFilter::setBandwidthHz(float bwHz)
{
    // Convert bandwidth in Hz to octaves
    // Guard against division by zero if frequency is not set yet
    if (frequency > 0.0f)
    {
        bandwidth = bandwidthHzToOctaves(bwHz, frequency);
    }
    else
    {
        bandwidth = 1.0f;  // Safe default
    }
    coefficientsDirty = true;
}

void GiantFormantFilter::setQ(float q)
{
    // Convert Q to bandwidth in octaves
    // BW_octaves = 2/ln(2) * asinh(1/(2*Q))
    if (q > 0.0f)
    {
        bandwidth = 2.88539f / std::asinh(1.0f / (2.0f * q));
    }
    coefficientsDirty = true;
}

void GiantFormantFilter::setAmplitude(float amp)
{
    amplitude = clamp(amp, 0.0f, 2.0f);
}

void GiantFormantFilter::calculateCoefficients()
{
    // Guard against invalid parameters
    if (frequency <= 0.0f || bandwidth <= 0.0f || sr <= 0.0f)
    {
        // Use safe default values
        b0 = 1.0f; b1 = 0.0f; b2 = 0.0f;
        a1 = 0.0f; a2 = 0.0f;
        return;
    }

    // Bandpass filter design with Q-based bandwidth
    float omega = 2.0f * 3.14159265359f * frequency / static_cast<float>(sr);

    // Clamp omega to avoid numerical issues
    omega = std::max(0.0001f, std::min(omega, 3.14159f));

    // Calculate alpha from bandwidth (in octaves)
    float sinOmega = SchillingerEcosystem::DSP::fastSineLookup(omega);

    // Guard against division by zero in alpha calculation
    if (std::abs(sinOmega) < 0.0001f)
    {
        sinOmega = 0.0001f;
    }

    float sinhArg = std::log(2.0f) / 2.0f * bandwidth * omega / sinOmega;

    // Clamp sinh argument to avoid overflow/NaN
    sinhArg = std::max(-10.0f, std::min(sinhArg, 10.0f));

    float alpha = sinOmega * std::sinh(sinhArg);

    // Guard against NaN/infinity from sinh
    if (std::isnan(alpha) || std::isinf(alpha))
    {
        alpha = 0.1f;  // Safe default
    }

    // Clamp alpha to avoid unstable filters
    alpha = std::max(-0.99f, std::min(alpha, 10.0f));

    float b0 = alpha;
    float b1 = 0.0f;
    float b2 = -alpha;
    float a0 = 1.0f + alpha;
    float a1 = -2.0f * SchillingerEcosystem::DSP::fastCosineLookup(omega);
    float a2 = 1.0f - alpha;

    // Guard against division by zero
    if (std::abs(a0) < 0.0001f)
    {
        a0 = 0.0001f;
    }

    // Normalize
    this->b0 = b0 / a0;
    this->b1 = b1 / a0;
    this->b2 = b2 / a0;
    this->a1 = a1 / a0;
    this->a2 = a2 / a0;

    // Final NaN check - if anything is NaN, use safe defaults
    if (std::isnan(this->b0) || std::isnan(this->b1) || std::isnan(this->b2) ||
        std::isnan(this->a1) || std::isnan(this->a2))
    {
        b0 = 1.0f; b1 = 0.0f; b2 = 0.0f;
        a1 = 0.0f; a2 = 0.0f;
    }
}

//==============================================================================
// FormantStack Implementation
//==============================================================================

FormantStack::FormantStack()
{
    // Initialize with 4 formants
    formants.resize(4);
}

void FormantStack::prepare(double sampleRate)
{
    sr = sampleRate;

    for (auto& formant : formants)
    {
        formant.prepare(sampleRate);
    }

    initializeVowel(VowelShape::Ah, 0.5f);
}

void FormantStack::reset()
{
    for (auto& formant : formants)
    {
        formant.reset();
    }

    driftPhase = 0.0f;
}

float FormantStack::processSample(float input)
{
    // Update formant drift
    if (params.formantDrift > 0.0f)
    {
        driftPhase += params.formantDrift * 0.0001f;
        if (driftPhase > 1.0f)
            driftPhase -= 1.0f;

        updateFormantFrequencies();
    }

    // Process through formant filters using SIMD when available
#if DSP_SIMD_NEON_AVAILABLE
    return SIMD::processFormantsNEON(input, formants.data(), formants.size());
#elif DSP_SIMD_AVX_AVAILABLE
    return SIMD::processFormantsAVX(input, formants.data(), formants.size());
#elif DSP_SIMD_SSE_AVAILABLE
    return SIMD::processFormantsSSE(input, formants.data(), formants.size());
#else
    // Scalar fallback - process through formant filters in series
    float output = input;
    for (auto& formant : formants)
    {
        output = formant.processSample(output);
    }
    return output;
#endif
}

void FormantStack::setParameters(const Parameters& p)
{
    params = p;

    if (p.vowelShape != VowelShape::Custom)
    {
        initializeVowel(p.vowelShape, p.openness);
    }
    else
    {
        // Apply custom formants with default bandwidths
        if (formants.size() >= 1)
        {
            formants[0].setFrequency(p.f1);
            formants[0].setBandwidthHz(100.0f);  // Default bandwidth
            formants[0].setAmplitude(1.0f);
        }
        if (formants.size() >= 2)
        {
            formants[1].setFrequency(p.f2);
            formants[1].setBandwidthHz(110.0f);
            formants[1].setAmplitude(0.9f);
        }
        if (formants.size() >= 3)
        {
            formants[2].setFrequency(p.f3);
            formants[2].setBandwidthHz(120.0f);
            formants[2].setAmplitude(0.7f);
        }
        if (formants.size() >= 4)
        {
            formants[3].setFrequency(p.f4);
            formants[3].setBandwidthHz(130.0f);
            formants[3].setAmplitude(0.5f);
        }
    }
}

void FormantStack::setVowelShape(VowelShape shape, float openness)
{
    params.vowelShape = shape;
    params.openness = openness;
    initializeVowel(shape, openness);
}

int FormantStack::getVowelIndex(VowelShape shape) const
{
    switch (shape)
    {
        case VowelShape::Ah: return 0;
        case VowelShape::Eh: return 1;
        case VowelShape::Ee: return 2;
        case VowelShape::Oh: return 3;
        case VowelShape::Oo: return 4;
        case VowelShape::Uh: return 5;
        case VowelShape::Ih: return 6;
        default: return 0;
    }
}

void FormantStack::initializeVowel(VowelShape shape, float openness)
{
    // Get vowel formants from lookup table
    int vowelIdx = getVowelIndex(shape);
    float scale = params.giantScale;  // Use parameter scale

    VowelFormants vowel = getVowelFormants(vowelIdx, scale);

    // Set base frequencies from lookup table
    baseF1 = vowel.f1;
    baseF2 = vowel.f2;
    baseF3 = vowel.f3;
    baseF4 = vowel.f4;

    // Apply openness modulation (subtle formant shifting)
    float opennessMod = (openness - 0.5f) * 0.3f;  // Reduced modulation for realism
    baseF1 *= (1.0f + opennessMod);
    baseF2 *= (1.0f - opennessMod * 0.3f);
    baseF3 *= (1.0f - opennessMod * 0.2f);

    updateFormantFrequencies();
}

void FormantStack::updateFormantFrequencies()
{
    // Get current vowel bandwidths
    int vowelIdx = getVowelIndex(params.vowelShape);
    float scale = params.giantScale;
    VowelFormants vowel = getVowelFormants(vowelIdx, scale);

    if (formants.size() >= 1)
    {
        float f1 = baseF1 * (1.0f + SchillingerEcosystem::DSP::fastSineLookup(driftPhase * 6.28f) * params.formantDrift * 0.1f);
        formants[0].setFrequency(f1);
        // Use Hz bandwidth from lookup table for realistic vocal acoustics
        formants[0].setBandwidthHz(vowel.b1);
        formants[0].setAmplitude(1.0f);
    }

    if (formants.size() >= 2)
    {
        float f2 = baseF2 * (1.0f + SchillingerEcosystem::DSP::fastCosineLookup(driftPhase * 6.28f * 1.3f) * params.formantDrift * 0.1f);
        formants[1].setFrequency(f2);
        formants[1].setBandwidthHz(vowel.b2);
        formants[1].setAmplitude(0.9f);
    }

    if (formants.size() >= 3)
    {
        float f3 = baseF3 * (1.0f + SchillingerEcosystem::DSP::fastSineLookup(driftPhase * 6.28f * 0.7f) * params.formantDrift * 0.1f);
        formants[2].setFrequency(f3);
        formants[2].setBandwidthHz(vowel.b3);
        formants[2].setAmplitude(0.7f);
    }

    if (formants.size() >= 4)
    {
        float f4 = baseF4 * (1.0f + SchillingerEcosystem::DSP::fastCosineLookup(driftPhase * 6.28f * 0.5f) * params.formantDrift * 0.1f);
        formants[3].setFrequency(f4);
        formants[3].setBandwidthHz(vowel.b4);
        formants[3].setAmplitude(0.5f);
    }
}

//==============================================================================
// SubharmonicGenerator Implementation
//==============================================================================

SubharmonicGenerator::SubharmonicGenerator()
    : rng(42)  // Fixed seed for determinism
{
    reset();
}

void SubharmonicGenerator::prepare(double sampleRate)
{
    sr = sampleRate;
    reset();
}

void SubharmonicGenerator::reset()
{
    octavePhase = 0.0f;
    fifthPhase = 0.0f;
    currentOctaveShift = 1.0f;
    currentFifthShift = 1.0f;
}

float SubharmonicGenerator::processSample(float input, float fundamental)
{
    if (params.octaveMix <= 0.0f && params.fifthMix <= 0.0f)
        return input;

    updateInstability();

    // Generate octave down
    float octaveFreq = fundamental * 0.5f * currentOctaveShift;
    float octaveIncrement = octaveFreq / static_cast<float>(sr);
    octavePhase += octaveIncrement;
    if (octavePhase >= 1.0f)
        octavePhase -= 1.0f;

    float octave = SchillingerEcosystem::DSP::fastSineLookup(octavePhase * 6.28318530718f);

    // Generate fifth down
    float fifthFreq = fundamental * 0.6666667f * currentFifthShift;
    float fifthIncrement = fifthFreq / static_cast<float>(sr);
    fifthPhase += fifthIncrement;
    if (fifthPhase >= 1.0f)
        fifthPhase -= 1.0f;

    float fifth = SchillingerEcosystem::DSP::fastSineLookup(fifthPhase * 6.28318530718f);

    // Mix subharmonics
    float output = input;
    output += octave * params.octaveMix * 0.5f;
    output += fifth * params.fifthMix * 0.3f;

    return output;
}

void SubharmonicGenerator::setParameters(const Parameters& p)
{
    params = p;
}

void SubharmonicGenerator::updateInstability()
{
    if (params.instability > 0.0f)
    {
        float drift = rng.next();
        currentOctaveShift = 1.0f + drift * params.instability * 0.05f;

        drift = rng.next();
        currentFifthShift = 1.0f + drift * params.instability * 0.05f;
    }
}

//==============================================================================
// ChestResonator Implementation
//==============================================================================

ChestResonator::ChestResonator()
{
    reset();
}

void ChestResonator::prepare(double sampleRate)
{
    sr = sampleRate;
    chestMode.prepare(sampleRate, params.chestResonance);
    reset();
}

void ChestResonator::reset()
{
    chestMode.reset();
    lowpassState = 0.0f;
}

float ChestResonator::processSample(float input)
{
    // Excite chest mode
    float chestExcitation = input * params.bodySize;
    float chestOutput = chestMode.processSample(chestExcitation);

    // Lowpass filtering for body size
    float lpCoeff = calculateLowpassCoefficient(params.bodySize);
    lowpassState = lerp(lowpassState, input + chestOutput, lpCoeff);

    return lowpassState;
}

void ChestResonator::setParameters(const Parameters& p)
{
    params = p;
    chestMode.prepare(sr, p.chestResonance);
}

float ChestResonator::calculateLowpassCoefficient(float bodySize) const
{
    // Larger body = more lowpass filtering
    float cutoff = 200.0f + (1.0f - bodySize) * 3000.0f;
    float wc = 2.0f * 3.14159265359f * cutoff / static_cast<float>(sr);
    return 1.0f - std::exp(-wc);
}

void ChestResonator::ChestMode::prepare(double sampleRate, float resonance)
{
    sr = sampleRate;
    decay = 0.99f + resonance * 0.009f;
}

float ChestResonator::ChestMode::processSample(float excitation)
{
    amplitude += excitation * 0.1f;
    phase += frequency / static_cast<float>(sr);
    if (phase >= 1.0f)
        phase -= 1.0f;

    float output = SchillingerEcosystem::DSP::fastSineLookup(phase * 6.28318530718f) * amplitude;
    amplitude *= decay;

    return output;
}

void ChestResonator::ChestMode::reset()
{
    phase = 0.0f;
    amplitude = 0.0f;
}

//==============================================================================
// GiantVoice Implementation
//==============================================================================

void GiantVoice::prepare(double sampleRate)
{
    breath.prepare(sampleRate);
    vocalFolds.prepare(sampleRate);
    formants.prepare(sampleRate);
    subharmonics.prepare(sampleRate);
    chest.prepare(sampleRate);
}

void GiantVoice::reset()
{
    breath.reset();
    vocalFolds.reset();
    formants.reset();
    subharmonics.reset();
    chest.reset();

    midiNote = -1;
    velocity = 0.0f;
    active = false;
}

void GiantVoice::trigger(int note, float vel, const GiantVoiceGesture& gestureParams,
                        const GiantScaleParameters& scaleParams)
{
    midiNote = note;
    velocity = vel;
    gesture = gestureParams;
    scale = scaleParams;
    active = true;

    // Calculate fundamental frequency (scale-aware)
    float baseFreq = midiToFrequency(note);

    // Scale affects frequency (larger = lower)
    float scaleMultiplier = 1.0f / (1.0f + scale.scaleMeters * 0.1f);
    float fundamental = baseFreq * scaleMultiplier;

    // Set vocal fold frequency
    VocalFoldOscillator::Parameters vocalParams;
    vocalParams.frequency = fundamental;
    vocalParams.pitchInstability = gesture.roughness * 0.5f;
    vocalParams.chaosAmount = gesture.aggression * 0.3f;
    vocalParams.waveformMorph = gesture.aggression;
    vocalParams.subharmonicMix = 0.3f;
    vocalFolds.setParameters(vocalParams);

    // Trigger breath pressure
    BreathPressureGenerator::Parameters breathParams;
    breathParams.attackTime = 0.2f + scale.transientSlowing * 1.8f;  // 200ms - 2s attack
    breathParams.sustainLevel = gesture.force;
    breathParams.releaseTime = 0.5f + scale.transientSlowing * 1.0f;
    breathParams.turbulenceAmount = gesture.roughness * 0.5f;
    breathParams.pressureOvershoot = gesture.aggression * 0.3f;
    breath.setParameters(breathParams);

    breath.trigger(vel, gesture.force, gesture.aggression);

    // Set formant parameters
    FormantStack::Parameters formantParams;
    formantParams.vowelShape = FormantStack::VowelShape::Ah;  // Start with Ah vowel
    formantParams.openness = gesture.openness;
    formantParams.formantDrift = 0.1f;
    formantParams.giantScale = 0.6f;  // Giant scale factor
    formants.setParameters(formantParams);

    // Set subharmonic parameters
    SubharmonicGenerator::Parameters subParams;
    subParams.octaveMix = 0.3f;
    subParams.fifthMix = 0.2f;
    subParams.instability = gesture.roughness * 0.5f;
    subharmonics.setParameters(subParams);

    // Set chest parameters
    ChestResonator::Parameters chestParams;
    chestParams.chestFrequency = 80.0f;
    chestParams.chestResonance = 0.7f;
    chestParams.bodySize = scale.scaleMeters / 20.0f;
    chest.setParameters(chestParams);
}

void GiantVoice::release(bool damping)
{
    breath.release(damping);
}

float GiantVoice::processSample()
{
    if (!active && !breath.isActive())
        return 0.0f;

    // Generate breath pressure
    float pressure = breath.processSample();

    if (std::isnan(pressure) || std::isinf(pressure))
        return 0.0f;

    if (pressure < 0.001f)
    {
        active = false;
        return 0.0f;
    }

    // Generate glottal source
    float glottal = vocalFolds.processSample(pressure);

    if (std::isnan(glottal) || std::isinf(glottal))
        return 0.0f;

    // Apply formant filtering
    float formantOutput = formants.processSample(glottal);

    if (std::isnan(formantOutput) || std::isinf(formantOutput))
        return 0.0f;

    // Add subharmonics
    float fundamental = vocalFolds.getParameters().frequency;
    float withSubharmonics = subharmonics.processSample(formantOutput, fundamental);

    if (std::isnan(withSubharmonics) || std::isinf(withSubharmonics))
        return 0.0f;

    // Apply chest resonance
    float output = chest.processSample(withSubharmonics);

    if (std::isnan(output) || std::isinf(output))
        return 0.0f;

    // Scale by velocity
    output *= velocity;

    // Safety limit
    output = clamp(output, -1.0f, 1.0f);

    // Final NaN guard - if NaN made it through the chain, return silence
    if (std::isnan(output) || std::isinf(output))
    {
        return 0.0f;
    }

    return output;
}

bool GiantVoice::isActive() const
{
    return active || breath.isActive();
}

//==============================================================================
// GiantVoiceManager Implementation
//==============================================================================

GiantVoiceManager::GiantVoiceManager()
{
}

void GiantVoiceManager::prepare(double sampleRate, int maxVoices)
{
    currentSampleRate = sampleRate;
    voices.clear();

    for (int i = 0; i < maxVoices; ++i)
    {
        auto voice = std::make_unique<GiantVoice>();
        voice->prepare(sampleRate);
        voices.push_back(std::move(voice));
    }
}

void GiantVoiceManager::reset()
{
    for (auto& voice : voices)
    {
        voice->reset();
    }
}

GiantVoice* GiantVoiceManager::findFreeVoice()
{
    for (auto& voice : voices)
    {
        if (!voice->isActive())
            return voice.get();
    }

    // Voice stealing: find oldest voice
    return voices[0].get();
}

GiantVoice* GiantVoiceManager::findVoiceForNote(int note)
{
    for (auto& voice : voices)
    {
        if (voice->midiNote == note && voice->isActive())
            return voice.get();
    }
    return nullptr;
}

void GiantVoiceManager::handleNoteOn(int note, float velocity,
                                    const GiantVoiceGesture& gesture,
                                    const GiantScaleParameters& scale)
{
    GiantVoice* voice = findFreeVoice();
    if (voice)
    {
        voice->trigger(note, velocity, gesture, scale);
    }
}

void GiantVoiceManager::handleNoteOff(int note, bool damping)
{
    GiantVoice* voice = findVoiceForNote(note);
    if (voice)
    {
        voice->release(damping);
    }
}

void GiantVoiceManager::allNotesOff()
{
    for (auto& voice : voices)
    {
        voice->release(true);
    }
}

float GiantVoiceManager::processSample()
{
    float output = 0.0f;

    for (auto& voice : voices)
    {
        if (voice->isActive())
        {
            output += voice->processSample();
        }
    }

    // Soft clip to prevent distortion
    if (output > 1.0f)
        output = 1.0f - std::exp(-output + 1.0f);
    else if (output < -1.0f)
        output = -1.0f + std::exp(output + 1.0f);

    return output;
}

int GiantVoiceManager::getActiveVoiceCount() const
{
    int count = 0;
    for (const auto& voice : voices)
    {
        if (voice->isActive())
            count++;
    }
    return count;
}

void GiantVoiceManager::setFormantParameters(const FormantStack::Parameters& params)
{
    for (auto& voice : voices)
    {
        voice->formants.setParameters(params);
    }
}

void GiantVoiceManager::setSubharmonicParameters(const SubharmonicGenerator::Parameters& params)
{
    for (auto& voice : voices)
    {
        voice->subharmonics.setParameters(params);
    }
}

void GiantVoiceManager::setChestParameters(const ChestResonator::Parameters& params)
{
    for (auto& voice : voices)
    {
        voice->chest.setParameters(params);
    }
}

//==============================================================================
// AetherGiantVoicePureDSP Implementation
//==============================================================================

AetherGiantVoicePureDSP::AetherGiantVoicePureDSP()
{
}

AetherGiantVoicePureDSP::~AetherGiantVoicePureDSP()
{
}

bool AetherGiantVoicePureDSP::prepare(double sampleRate, int blockSize)
{
    sampleRate_ = sampleRate;
    blockSize_ = blockSize;

    voiceManager_.prepare(sampleRate, maxVoices_);

    // Initialize scale parameters
    currentScale_.scaleMeters = params_.scaleMeters;
    currentScale_.massBias = params_.massBias;
    currentScale_.airLoss = params_.airLoss;
    currentScale_.transientSlowing = params_.transientSlowing;

    // Initialize gesture parameters
    currentGesture_.force = params_.force;
    currentGesture_.aggression = params_.aggression;
    currentGesture_.openness = params_.openness;
    currentGesture_.roughness = params_.roughness;

    return true;
}

void AetherGiantVoicePureDSP::reset()
{
    voiceManager_.reset();
}

void AetherGiantVoicePureDSP::process(float** outputs, int numChannels, int numSamples)
{
    // Clear output buffers
    for (int ch = 0; ch < numChannels; ++ch)
    {
        std::memset(outputs[ch], 0, sizeof(float) * numSamples);
    }

    // Guard against NaN master volume
    float masterVol = params_.masterVolume;
    if (std::isnan(masterVol) || std::isinf(masterVol))
    {
        masterVol = 0.8f;  // Safe default
    }

    // Process samples
    for (int sample = 0; sample < numSamples; ++sample)
    {
        float mono = voiceManager_.processSample();

        // Apply master volume
        mono *= masterVol;

        // Guard against NaN in output
        if (std::isnan(mono) || std::isinf(mono))
        {
            mono = 0.0f;
        }

        // Output to all channels
        for (int ch = 0; ch < numChannels; ++ch)
        {
            outputs[ch][sample] = mono;
        }
    }
}

void AetherGiantVoicePureDSP::handleEvent(const DSP::ScheduledEvent& event)
{
    switch (event.type)
    {
        case DSP::ScheduledEvent::NOTE_ON:
        {
            voiceManager_.handleNoteOn(
                event.data.note.midiNote,
                event.data.note.velocity,
                currentGesture_,
                currentScale_
            );
            break;
        }

        case DSP::ScheduledEvent::NOTE_OFF:
        {
            voiceManager_.handleNoteOff(event.data.note.midiNote);
            break;
        }

        case DSP::ScheduledEvent::PARAM_CHANGE:
        {
            setParameter(event.data.param.paramId, event.data.param.value);
            break;
        }

        case DSP::ScheduledEvent::CONTROL_CHANGE:
        {
            // Map MIDI CC to parameters
            int cc = event.data.controlChange.controllerNumber;
            float value = event.data.controlChange.value / 127.0f;

            if (cc == 1)  // Modulation wheel -> roughness
            {
                setParameter("roughness", value);
            }
            else if (cc == 2)  // Breath control -> force
            {
                setParameter("force", value);
            }
            else if (cc == 11)  // Expression -> aggression
            {
                setParameter("aggression", value);
            }
            break;
        }

        case ScheduledEvent::RESET:
        {
            reset();
            break;
        }

        default:
            break;
    }
}

float AetherGiantVoicePureDSP::getParameter(const char* paramId) const
{
    std::string id(paramId);

    if (id == "breathAttack") return params_.breathAttack;
    if (id == "breathSustain") return params_.breathSustain;
    if (id == "breathRelease") return params_.breathRelease;
    if (id == "turbulence") return params_.turbulence;

    if (id == "pitchInstability") return params_.pitchInstability;
    if (id == "chaosAmount") return params_.chaosAmount;
    if (id == "waveformMorph") return params_.waveformMorph;
    if (id == "subharmonicMix") return params_.subharmonicMix;

    if (id == "vowelOpenness") return params_.vowelOpenness;
    if (id == "formantDrift") return params_.formantDrift;

    if (id == "chestFrequency") return params_.chestFrequency;
    if (id == "chestResonance") return params_.chestResonance;
    if (id == "bodySize") return params_.bodySize;

    if (id == "scaleMeters") return params_.scaleMeters;
    if (id == "massBias") return params_.massBias;
    if (id == "airLoss") return params_.airLoss;
    if (id == "transientSlowing") return params_.transientSlowing;

    if (id == "force") return params_.force;
    if (id == "aggression") return params_.aggression;
    if (id == "openness") return params_.openness;
    if (id == "roughness") return params_.roughness;

    if (id == "masterVolume") return params_.masterVolume;

    return 0.0f;
}

void AetherGiantVoicePureDSP::setParameter(const char* paramId, float value)
{
    std::string id(paramId);

    if (id == "breathAttack") params_.breathAttack = value;
    else if (id == "breathSustain") params_.breathSustain = value;
    else if (id == "breathRelease") params_.breathRelease = value;
    else if (id == "turbulence") params_.turbulence = value;

    else if (id == "pitchInstability") params_.pitchInstability = value;
    else if (id == "chaosAmount") params_.chaosAmount = value;
    else if (id == "waveformMorph") params_.waveformMorph = value;
    else if (id == "subharmonicMix") params_.subharmonicMix = value;

    else if (id == "vowelOpenness") params_.vowelOpenness = value;
    else if (id == "formantDrift") params_.formantDrift = value;

    else if (id == "chestFrequency") params_.chestFrequency = value;
    else if (id == "chestResonance") params_.chestResonance = value;
    else if (id == "bodySize") params_.bodySize = value;

    else if (id == "scaleMeters")
    {
        params_.scaleMeters = value;
        currentScale_.scaleMeters = value;
    }
    else if (id == "massBias")
    {
        params_.massBias = value;
        currentScale_.massBias = value;
    }
    else if (id == "airLoss")
    {
        params_.airLoss = value;
        currentScale_.airLoss = value;
    }
    else if (id == "transientSlowing")
    {
        params_.transientSlowing = value;
        currentScale_.transientSlowing = value;
    }

    else if (id == "force")
    {
        params_.force = value;
        currentGesture_.force = value;
    }
    else if (id == "aggression")
    {
        params_.aggression = value;
        currentGesture_.aggression = value;
    }
    else if (id == "openness")
    {
        params_.openness = value;
        currentGesture_.openness = value;
    }
    else if (id == "roughness")
    {
        params_.roughness = value;
        currentGesture_.roughness = value;
    }

    else if (id == "masterVolume") params_.masterVolume = value;

    applyParameters();
}

void AetherGiantVoicePureDSP::applyParameters()
{
    // Update formant parameters
    FormantStack::Parameters formantParams;
    formantParams.vowelShape = FormantStack::VowelShape::Ah;  // Start with Ah vowel
    formantParams.openness = params_.vowelOpenness;
    formantParams.formantDrift = params_.formantDrift;
    formantParams.giantScale = 0.6f;  // Giant scale factor
    voiceManager_.setFormantParameters(formantParams);

    // Update subharmonic parameters
    SubharmonicGenerator::Parameters subParams;
    subParams.octaveMix = params_.subharmonicMix;
    subParams.fifthMix = 0.2f;
    subParams.instability = params_.pitchInstability;
    voiceManager_.setSubharmonicParameters(subParams);

    // Update chest parameters
    ChestResonator::Parameters chestParams;
    chestParams.chestFrequency = params_.chestFrequency;
    chestParams.chestResonance = params_.chestResonance;
    chestParams.bodySize = params_.bodySize;
    voiceManager_.setChestParameters(chestParams);
}

bool AetherGiantVoicePureDSP::savePreset(char* jsonBuffer, int jsonBufferSize) const
{
    int offset = 0;

    // Write JSON header
    const char* header = "{\n";
    int headerLen = std::strlen(header);
    if (offset + headerLen >= jsonBufferSize)
        return false;
    std::memcpy(jsonBuffer + offset, header, headerLen);
    offset += headerLen;

    // Write all parameters
    if (!writeJsonParameter("breathAttack", params_.breathAttack, jsonBuffer, offset, jsonBufferSize))
        return false;
    if (!writeJsonParameter("breathSustain", params_.breathSustain, jsonBuffer, offset, jsonBufferSize))
        return false;
    if (!writeJsonParameter("breathRelease", params_.breathRelease, jsonBuffer, offset, jsonBufferSize))
        return false;
    if (!writeJsonParameter("turbulence", params_.turbulence, jsonBuffer, offset, jsonBufferSize))
        return false;

    if (!writeJsonParameter("pitchInstability", params_.pitchInstability, jsonBuffer, offset, jsonBufferSize))
        return false;
    if (!writeJsonParameter("chaosAmount", params_.chaosAmount, jsonBuffer, offset, jsonBufferSize))
        return false;
    if (!writeJsonParameter("waveformMorph", params_.waveformMorph, jsonBuffer, offset, jsonBufferSize))
        return false;
    if (!writeJsonParameter("subharmonicMix", params_.subharmonicMix, jsonBuffer, offset, jsonBufferSize))
        return false;

    if (!writeJsonParameter("vowelOpenness", params_.vowelOpenness, jsonBuffer, offset, jsonBufferSize))
        return false;
    if (!writeJsonParameter("formantDrift", params_.formantDrift, jsonBuffer, offset, jsonBufferSize))
        return false;

    if (!writeJsonParameter("chestFrequency", params_.chestFrequency, jsonBuffer, offset, jsonBufferSize))
        return false;
    if (!writeJsonParameter("chestResonance", params_.chestResonance, jsonBuffer, offset, jsonBufferSize))
        return false;
    if (!writeJsonParameter("bodySize", params_.bodySize, jsonBuffer, offset, jsonBufferSize))
        return false;

    if (!writeJsonParameter("scaleMeters", params_.scaleMeters, jsonBuffer, offset, jsonBufferSize))
        return false;
    if (!writeJsonParameter("massBias", params_.massBias, jsonBuffer, offset, jsonBufferSize))
        return false;
    if (!writeJsonParameter("airLoss", params_.airLoss, jsonBuffer, offset, jsonBufferSize))
        return false;
    if (!writeJsonParameter("transientSlowing", params_.transientSlowing, jsonBuffer, offset, jsonBufferSize))
        return false;

    if (!writeJsonParameter("force", params_.force, jsonBuffer, offset, jsonBufferSize))
        return false;
    if (!writeJsonParameter("aggression", params_.aggression, jsonBuffer, offset, jsonBufferSize))
        return false;
    if (!writeJsonParameter("openness", params_.openness, jsonBuffer, offset, jsonBufferSize))
        return false;
    if (!writeJsonParameter("roughness", params_.roughness, jsonBuffer, offset, jsonBufferSize))
        return false;

    if (!writeJsonParameter("masterVolume", params_.masterVolume, jsonBuffer, offset, jsonBufferSize))
        return false;

    // Write JSON footer (remove trailing comma)
    offset -= 2;  // Remove ",\n"
    const char* footer = "\n}\n";
    int footerLen = std::strlen(footer);
    if (offset + footerLen >= jsonBufferSize)
        return false;
    std::memcpy(jsonBuffer + offset, footer, footerLen);
    offset += footerLen;

    jsonBuffer[offset] = '\0';
    return true;
}

bool AetherGiantVoicePureDSP::loadPreset(const char* jsonData)
{
    double value;

    if (parseJsonParameter(jsonData, "breathAttack", value))
        params_.breathAttack = static_cast<float>(value);

    if (parseJsonParameter(jsonData, "breathSustain", value))
        params_.breathSustain = static_cast<float>(value);

    if (parseJsonParameter(jsonData, "breathRelease", value))
        params_.breathRelease = static_cast<float>(value);

    if (parseJsonParameter(jsonData, "turbulence", value))
        params_.turbulence = static_cast<float>(value);

    if (parseJsonParameter(jsonData, "pitchInstability", value))
        params_.pitchInstability = static_cast<float>(value);

    if (parseJsonParameter(jsonData, "chaosAmount", value))
        params_.chaosAmount = static_cast<float>(value);

    if (parseJsonParameter(jsonData, "waveformMorph", value))
        params_.waveformMorph = static_cast<float>(value);

    if (parseJsonParameter(jsonData, "subharmonicMix", value))
        params_.subharmonicMix = static_cast<float>(value);

    if (parseJsonParameter(jsonData, "vowelOpenness", value))
        params_.vowelOpenness = static_cast<float>(value);

    if (parseJsonParameter(jsonData, "formantDrift", value))
        params_.formantDrift = static_cast<float>(value);

    if (parseJsonParameter(jsonData, "chestFrequency", value))
        params_.chestFrequency = static_cast<float>(value);

    if (parseJsonParameter(jsonData, "chestResonance", value))
        params_.chestResonance = static_cast<float>(value);

    if (parseJsonParameter(jsonData, "bodySize", value))
        params_.bodySize = static_cast<float>(value);

    if (parseJsonParameter(jsonData, "scaleMeters", value))
    {
        params_.scaleMeters = static_cast<float>(value);
        currentScale_.scaleMeters = static_cast<float>(value);
    }

    if (parseJsonParameter(jsonData, "massBias", value))
    {
        params_.massBias = static_cast<float>(value);
        currentScale_.massBias = static_cast<float>(value);
    }

    if (parseJsonParameter(jsonData, "airLoss", value))
    {
        params_.airLoss = static_cast<float>(value);
        currentScale_.airLoss = static_cast<float>(value);
    }

    if (parseJsonParameter(jsonData, "transientSlowing", value))
    {
        params_.transientSlowing = static_cast<float>(value);
        currentScale_.transientSlowing = static_cast<float>(value);
    }

    if (parseJsonParameter(jsonData, "force", value))
    {
        params_.force = static_cast<float>(value);
        currentGesture_.force = static_cast<float>(value);
    }

    if (parseJsonParameter(jsonData, "aggression", value))
    {
        params_.aggression = static_cast<float>(value);
        currentGesture_.aggression = static_cast<float>(value);
    }

    if (parseJsonParameter(jsonData, "openness", value))
    {
        params_.openness = static_cast<float>(value);
        currentGesture_.openness = static_cast<float>(value);
    }

    if (parseJsonParameter(jsonData, "roughness", value))
    {
        params_.roughness = static_cast<float>(value);
        currentGesture_.roughness = static_cast<float>(value);
    }

    if (parseJsonParameter(jsonData, "masterVolume", value))
        params_.masterVolume = static_cast<float>(value);

    applyParameters();
    return true;
}

int AetherGiantVoicePureDSP::getActiveVoiceCount() const
{
    return voiceManager_.getActiveVoiceCount();
}

float AetherGiantVoicePureDSP::calculateFrequency(int midiNote) const
{
    float freq = midiToFrequency(midiNote);

    // Apply scale-based frequency adjustment
    float scaleMultiplier = 1.0f / (1.0f + currentScale_.scaleMeters * 0.1f);
    return freq * scaleMultiplier;
}

bool AetherGiantVoicePureDSP::writeJsonParameter(const char* name, double value,
                                                 char* buffer, int& offset, int bufferSize) const
{
    char line[256];
    int len = std::snprintf(line, sizeof(line), "  \"%s\": %.6g,\n", name, value);

    if (offset + len >= bufferSize)
        return false;

    std::memcpy(buffer + offset, line, len);
    offset += len;

    return true;
}

bool AetherGiantVoicePureDSP::parseJsonParameter(const char* json, const char* param, double& value) const
{
    // Simple JSON parser (not robust, but works for our simple format)
    char search[256];
    std::snprintf(search, sizeof(search), "\"%s\":", param);

    const char* found = std::strstr(json, search);
    if (!found)
        return false;

    found += std::strlen(search);

    // Skip whitespace
    while (*found == ' ' || *found == '\t' || *found == '\n')
        found++;

    // Parse number
    char* end;
    value = std::strtod(found, &end);

    return end != found;
}

//==============================================================================
// Factory Registration
//==============================================================================

// Factory registration disabled for plugin builds
/*
namespace {
    struct AetherGiantVoiceRegistrar {
        AetherGiantVoiceRegistrar() {
            registerInstrumentFactory("AetherGiantVoice", []() -> InstrumentDSP* {
                return new AetherGiantVoicePureDSP();
            });
        }
    } registrar;
}
*/

} // namespace DSP
