/*
  ==============================================================================

   AetherGiantHornsPureDSP.cpp
   Giant Horn Synthesizer - Physical Modeling Implementation

   Physical modeling components:
   - Lip reed exciter (nonlinear brass oscillation)
   - Bore waveguide (air column with reflection)
   - Bell radiation filter (directional output)
   - Formant shaping (instrument identity)
   - Giant scale physics (mass, inertia, air coupling)

  ==============================================================================
*/

#include "dsp/AetherGiantHornsDSP.h"
#include "dsp/AetherGiantBase.h"
#include "dsp/InstrumentFactory.h"
#include "../../../../include/dsp/LookupTables.h"
#include <cmath>
#include <algorithm>
#include <random>

namespace DSP {

//==============================================================================
// Lip Reed Exciter Implementation
//==============================================================================

LipReedExciter::LipReedExciter()
    : rng(std::random_device{}())
    , dist(0.0f, 1.0f)
{
}

void LipReedExciter::prepare(double sampleRate)
{
    sr = sampleRate;
    reset();
}

void LipReedExciter::reset()
{
    reedPosition = 0.0f;
    reedVelocity = 0.0f;
    currentPressure = 0.0f;
    phase = 0.0f;
    lipMass = 1.0f;
    lipStiffness = 1.0f;
    oscillationStarted = false;
    attackTransient = 0.0f;
}

float LipReedExciter::processSample(float pressure, float frequency)
{
    currentPressure = pressure * params.mouthPressure;

    // ENHANCED: Pressure-dependent oscillation threshold
    // Real brass instruments need minimum pressure to start oscillating
    float oscillationThreshold = calculateOscillationThreshold(frequency);

    // Check if oscillation should start
    if (!oscillationStarted && currentPressure > oscillationThreshold)
    {
        oscillationStarted = true;
    }
    else if (oscillationStarted && currentPressure < oscillationThreshold * 0.5f)
    {
        oscillationStarted = false;
    }

    // Calculate reed frequency based on tension and target
    float reedFreq = calculateReedFrequency(frequency);

    // ENHANCED: Lip mass and stiffness affect frequency and dynamics
    float massFactor = 1.0f / (1.0f + params.lipMass * 0.5f);
    float stiffnessFactor = 1.0f + params.lipStiffness * 0.3f;
    reedFreq *= massFactor * stiffnessFactor;

    // Phase increment
    float phaseInc = reedFreq / static_cast<float>(sr);
    phase += phaseInc;
    if (phase >= 1.0f)
        phase -= 1.0f;

    // ENHANCED: Realistic brass attack transients
    // Add burst of high-frequency energy at attack
    if (oscillationStarted && attackTransient < 1.0f)
    {
        attackTransient += 0.01f; // Attack transient buildup
    }
    else if (!oscillationStarted)
    {
        attackTransient = 0.0f;
    }

    // Nonlinear reed oscillation with transient enhancement
    float oscillation = SchillingerEcosystem::DSP::fastSineLookup(phase * 2.0f * static_cast<float>(M_PI));

    // ENHANCED: Add harmonics for brighter attack
    if (attackTransient > 0.0f)
    {
        float harmonic = SchillingerEcosystem::DSP::fastSineLookup(phase * 4.0f * static_cast<float>(M_PI));
        oscillation += harmonic * attackTransient * 0.3f * (1.0f - attackTransient);
    }

    // Apply nonlinear transfer function
    float transfer = nonlinearTransfer(oscillation);

    // Add chaos/growl at high pressure
    float chaos = 0.0f;
    if (currentPressure > params.chaosThreshold)
    {
        float chaosAmount = (currentPressure - params.chaosThreshold) *
                           params.growlAmount;
        chaos = dist(rng) * chaosAmount * 0.5f;
    }

    // ENHANCED: Reed dynamics with mass and stiffness
    // Mass-spring-damper system with pressure-dependent coupling
    float reedForce = currentPressure * transfer + chaos;

    // Lip mass affects acceleration (F = ma, so a = F/m)
    float massEffect = 1.0f / (1.0f + params.lipMass * 2.0f);
    reedVelocity += (reedForce - reedPosition * params.lipStiffness) * massEffect * 0.5f;

    // Lip stiffness affects restoring force
    float restoringForce = reedPosition * params.lipStiffness * 0.1f;
    reedVelocity -= restoringForce;

    // Damping (energy loss)
    reedVelocity *= 0.99f;
    reedPosition += reedVelocity;

    // ENHANCED: Pressure-dependent amplitude with threshold
    float amplitude = 0.0f;
    if (oscillationStarted)
    {
        // Smooth onset above threshold
        float excessPressure = currentPressure - oscillationThreshold;
        amplitude = std::tanh(excessPressure * 2.0f);
    }

    // Output is pressure-modulated reed motion
    float output = reedPosition * amplitude * 2.0f;

    // Soft clipping
    output = std::tanh(output);

    return output;
}

void LipReedExciter::setParameters(const Parameters& p)
{
    params = p;
}

float LipReedExciter::calculateReedFrequency(float targetFreq) const
{
    // Lip tension shifts the reed's natural frequency
    float tensionFactor = 1.0f + (params.lipTension - 0.5f) * 0.2f;
    return targetFreq * tensionFactor;
}

float LipReedExciter::calculateOscillationThreshold(float frequency) const
{
    // ENHANCED: Pressure-dependent oscillation threshold
    // Higher frequencies require more pressure
    // Lip tension raises the threshold
    float baseThreshold = 0.2f;
    float frequencyEffect = (frequency / 1000.0f) * 0.1f;
    float tensionEffect = params.lipTension * 0.15f;
    float stiffnessEffect = params.lipStiffness * 0.1f;

    return baseThreshold + frequencyEffect + tensionEffect + stiffnessEffect;
}

float LipReedExciter::nonlinearTransfer(float x) const
{
    // ENHANCED: More realistic nonlinear transfer function
    // Combines soft clipping with asymmetric behavior
    float nonlinear = std::tanh(x * (1.0f + params.nonlinearity * 2.0f));

    // Add asymmetry (real lips aren't symmetric)
    float asymmetric = (x > 0.0f) ? nonlinear : nonlinear * 0.8f;

    return asymmetric;
}

//==============================================================================
// Bore Waveguide Implementation
//==============================================================================

BoreWaveguide::BoreWaveguide()
{
    // OPTIMIZED: Calculate maximum required delay sizes based on physical constraints
    // Target: <100 KB per voice memory footprint
    //
    // Memory calculation: (forwardDelay + backwardDelay + cavity) * 4 bytes
    // 12K * 2 + 128 = 24,640 samples * 4 = 98,560 bytes = 96.25 KB per voice ✓
    //
    // Maximum bore length support:
    // At 48kHz: 12288 / 48000 * 343 / 2 = 43.7 meters
    // At 96kHz: 12288 / 96000 * 343 / 2 = 21.9 meters
    //
    // This supports giant instruments while staying under 100 KB target
    static constexpr int MAX_BORE_DELAY_SAMPLES = 12288;  // ~512ms round-trip at 48kHz

    // Mouthpiece cavity: 2ms delay typical
    // At 96kHz: 0.002 * 96000 = 192 samples (use 128 for 48kHz safety)
    static constexpr int MAX_CAVITY_DELAY_SAMPLES = 128;

    forwardDelay.resize(MAX_BORE_DELAY_SAMPLES);
    backwardDelay.resize(MAX_BORE_DELAY_SAMPLES);
    mouthpieceCavity.resize(MAX_CAVITY_DELAY_SAMPLES);

    // Store buffer sizes for circular buffer operations
    maxDelaySize = MAX_BORE_DELAY_SAMPLES;
    maxCavitySize = MAX_CAVITY_DELAY_SAMPLES;
}

void BoreWaveguide::prepare(double sampleRate)
{
    sr = sampleRate;
    updateDelayLength();
    reset();
}

void BoreWaveguide::reset()
{
    std::fill(forwardDelay.begin(), forwardDelay.end(), 0.0f);
    std::fill(backwardDelay.begin(), backwardDelay.end(), 0.0f);
    std::fill(mouthpieceCavity.begin(), mouthpieceCavity.end(), 0.0f);
    writeIndex = 0;
    bellState = 0.0f;
    cavityWriteIndex = 0;

    // Reset filter states
    cavityState = 0.0f;
    cylState = 0.0f;
    conState = 0.0f;
    flareState = 0.0f;
    hybridLF = 0.0f;
    hybridHF = 0.0f;
    stage1State = 0.0f;
    stage2State = 0.0f;
    stage3State = 0.0f;
    lfState = 0.0f;
    hfState = 0.0f;

    // Mark coefficients as dirty
    boreCoefficientsDirty = true;
    bellCoefficientsDirty = true;
    lossCoefficientsDirty = true;
}

float BoreWaveguide::processSample(float input)
{
    // ENHANCED: Apply mouthpiece cavity resonance first
    float cavityInput = processMouthpieceCavity(input);

    // ENHANCED: Apply bore shape characteristics
    float shapedInput = applyBoreShape(cavityInput);

    // Read from delays using circular buffer wrap
    int readIndex = (writeIndex - delayLength + maxDelaySize) % maxDelaySize;
    float forwardOut = forwardDelay[readIndex];
    float backwardOut = backwardDelay[readIndex];

    // Bell radiation and reflection
    float bellOutput = processBellRadiation(forwardOut);

    // ENHANCED: Frequency-dependent reflection at bell
    // Different bore shapes reflect differently
    float reflectionCoeff = calculateFrequencyDependentReflection();
    float reflection = bellOutput * reflectionCoeff;

    // Write to delays
    forwardDelay[writeIndex] = shapedInput - reflection;
    backwardDelay[writeIndex] = reflection;

    // Circular buffer wrap
    writeIndex = (writeIndex + 1) % maxDelaySize;

    return bellOutput;
}

void BoreWaveguide::setLengthMeters(float length)
{
    // Clamp to physically supported range based on buffer size
    // Max length = (maxDelaySize / sampleRate) * speedOfSound / 2
    // At 48kHz with 12K buffer: (12288 / 48000) * 343 / 2 ≈ 43.7 meters
    // We use 40 meters as a safe limit at 48kHz
    params.lengthMeters = std::clamp(length, 0.5f, 40.0f);
    updateDelayLength();
}

void BoreWaveguide::setBoreShape(BoreShape shape)
{
    params.boreShape = shape;
}

void BoreWaveguide::setParameters(const Parameters& p)
{
    params = p;
    updateDelayLength();
}

float BoreWaveguide::getFundamentalFrequency() const
{
    // Speed of sound is ~343 m/s
    // Open-open tube: f = c / (2 * L)
    constexpr float speedOfSound = 343.0f;
    return speedOfSound / (2.0f * params.lengthMeters);
}

void BoreWaveguide::updateDelayLength()
{
    // Delay = 2 * length / speedOfSound (round trip)
    constexpr float speedOfSound = 343.0f;
    float delaySeconds = (2.0f * params.lengthMeters) / speedOfSound;
    delayLength = static_cast<int>(delaySeconds * sr);

    // Clamp delay length to buffer size
    delayLength = std::clamp(delayLength, 1, maxDelaySize - 1);
}

float BoreWaveguide::processMouthpieceCavity(float input)
{
    // ENHANCED: Mouthpiece cavity resonance
    // Creates a small resonant chamber before the bore
    // Affects attack transients and high-frequency content

    // Cavity delay length (short, for small mouthpiece volume)
    int cavityDelay = static_cast<int>(0.002f * sr); // 2ms cavity
    cavityDelay = std::clamp(cavityDelay, 1, maxCavitySize - 1);

    int cavityReadIndex = (cavityWriteIndex - cavityDelay + maxCavitySize) % maxCavitySize;
    float cavityFeedback = mouthpieceCavity[cavityReadIndex];

    // Mouthpiece resonance frequency (typically 800-1500 Hz for brass)
    float resonanceFreq = 1000.0f;
    float resonanceCoeff = resonanceFreq / (resonanceFreq + static_cast<float>(sr) * 0.5f);

    cavityState = cavityState + resonanceCoeff * (input - cavityState);

    // Write to cavity delay
    mouthpieceCavity[cavityWriteIndex] = cavityState + cavityFeedback * 0.3f;
    cavityWriteIndex = (cavityWriteIndex + 1) % maxCavitySize;

    // Output combines direct and cavity-resonated signal
    return input * 0.7f + cavityState * 0.3f;
}

float BoreWaveguide::applyBoreShape(float input)
{
    // ENHANCED: Apply bore shape characteristics
    // Different bore shapes have different frequency responses

    switch (params.boreShape)
    {
        case BoreShape::Cylindrical:
            // Cylindrical: Even harmonics emphasized (trombone-like)
            return applyCylindricalBore(input);

        case BoreShape::Conical:
            // Conical: Odd harmonics emphasized (flugelhorn-like)
            return applyConicalBore(input);

        case BoreShape::Flared:
            // Flared: Bright, penetrating (tuba-like)
            return applyFlaredBore(input);

        case BoreShape::Hybrid:
        default:
            // Hybrid: Balanced response (most realistic)
            return applyHybridBore(input);
    }
}

float BoreWaveguide::applyCylindricalBore(float input)
{
    // Cylindrical bore emphasizes even harmonics
    // Creates a "hollower" sound
    if (boreCoefficientsDirty || cachedBoreShape != BoreShape::Cylindrical)
    {
        float cutoff = 1500.0f;
        cylCoeff = cutoff / (cutoff + static_cast<float>(sr) * 0.5f);
        cachedBoreShape = BoreShape::Cylindrical;
        boreCoefficientsDirty = false;
    }

    cylState = cylState + cylCoeff * (input - cylState);

    // Mix direct and filtered for even harmonic emphasis
    return input * 0.6f + cylState * 0.4f;
}

float BoreWaveguide::applyConicalBore(float input)
{
    // Conical bore emphasizes odd harmonics
    // Creates a "warmer" sound
    if (boreCoefficientsDirty || cachedBoreShape != BoreShape::Conical)
    {
        float cutoff = 800.0f;
        conCoeff = cutoff / (cutoff + static_cast<float>(sr) * 0.5f);
        cachedBoreShape = BoreShape::Conical;
        boreCoefficientsDirty = false;
    }

    conState = conState + conCoeff * (input - conState);

    // More filtering for warmer sound
    return input * 0.4f + conState * 0.6f;
}

float BoreWaveguide::applyFlaredBore(float input)
{
    // Flared bore is bright and penetrating
    // Emphasizes high frequencies
    if (boreCoefficientsDirty || cachedBoreShape != BoreShape::Flared)
    {
        float cutoff = 2500.0f;
        flareCoeff = cutoff / (cutoff + static_cast<float>(sr) * 0.5f);
        cachedBoreShape = BoreShape::Flared;
        boreCoefficientsDirty = false;
    }

    flareState = flareState + flareCoeff * (input - flareState);

    // High-frequency emphasis
    float hfBoost = input - flareState;
    return input + hfBoost * 0.5f;
}

float BoreWaveguide::applyHybridBore(float input)
{
    // Hybrid bore combines characteristics
    // Most realistic for complex instruments
    if (boreCoefficientsDirty || cachedBoreShape != BoreShape::Hybrid)
    {
        float lfCutoff = 600.0f;
        float hfCutoff = 2000.0f;

        hybridLFCoeff = lfCutoff / (lfCutoff + static_cast<float>(sr) * 0.5f);
        hybridHFCoeff = hfCutoff / (hfCutoff + static_cast<float>(sr) * 0.5f);
        cachedBoreShape = BoreShape::Hybrid;
        boreCoefficientsDirty = false;
    }

    hybridLF = hybridLF + hybridLFCoeff * (input - hybridLF);
    hybridHF = hybridHF + hybridHFCoeff * (input - hybridHF);

    // Balanced mix
    return input * 0.5f + hybridLF * 0.3f + (input - hybridHF) * 0.2f;
}

float BoreWaveguide::calculateFrequencyDependentReflection() const
{
    // ENHANCED: Frequency-dependent reflection coefficient
    // Higher frequencies reflect less (radiate more)

    float baseReflection = params.reflectionCoeff;

    // Bore shape affects reflection characteristics
    float shapeMod = 0.0f;
    switch (params.boreShape)
    {
        case BoreShape::Cylindrical:
            // Cylindrical: More uniform reflection
            shapeMod = 0.0f;
            break;
        case BoreShape::Conical:
            // Conical: Less reflection at high frequencies
            shapeMod = -0.1f;
            break;
        case BoreShape::Flared:
            // Flared: Much less reflection at high frequencies
            shapeMod = -0.2f;
            break;
        case BoreShape::Hybrid:
        default:
            // Hybrid: Moderate frequency dependence
            shapeMod = -0.05f;
            break;
    }

    // Apply flare factor
    float flareEffect = params.flareFactor * shapeMod;

    return std::clamp(baseReflection + flareEffect, 0.0f, 1.0f);
}

float BoreWaveguide::processBellRadiation(float input)
{
    // ENHANCED: Bell radiation with frequency-dependent characteristics

    // Calculate instantaneous frequency (approximated from zero crossings)
    // For more accurate modeling, we'd track the actual frequency
    float currentFreq = getFundamentalFrequency();

    // ENHANCED: Frequency-dependent radiation efficiency
    // High frequencies radiate more efficiently from larger bells
    float radiationGain = calculateBellRadiation(currentFreq);

    // ENHANCED: Bell filter with frequency-dependent response
    // Larger bells emphasize lower frequencies, smaller bells are brighter
    float bellSize = 1.0f + params.flareFactor;

    // Multi-stage bell filtering for realistic brass brightness
    float stage1 = bellRadiationStage1(input, bellSize);
    float stage2 = bellRadiationStage2(stage1, bellSize);
    float stage3 = bellRadiationStage3(stage2, bellSize);

    // Combine stages for complex bell resonance
    float bellOutput = stage1 * 0.5f + stage2 * 0.3f + stage3 * 0.2f;

    // ENHANCED: Apply radiation impedance modeling
    // Bell acts as a high-pass filter - high frequencies escape more easily
    float impedanceEffect = calculateRadiationImpedance(currentFreq, bellSize);
    bellOutput *= impedanceEffect;

    // ENHANCED: Apply loss with frequency dependence
    // High frequencies are attenuated more in the bore
    float loss = 1.0f - (params.lossPerMeter * params.lengthMeters * 0.01f);
    loss = std::clamp(loss, 0.0f, 1.0f);

    // High-frequency loss is greater
    float hfLoss = loss * (1.0f - 0.1f * params.lengthMeters);

    // Apply different losses to different frequency bands
    bellOutput = applyFrequencyDependentLoss(bellOutput, loss, hfLoss);

    return bellOutput * radiationGain;
}

float BoreWaveguide::calculateBellRadiation(float frequency) const
{
    // ENHANCED: Bell radiation increases with frequency
    // Reference: "bellRadiation" function from requirements
    // High frequencies radiate more efficiently from the bell

    // Normalize frequency to 0-1 range (up to 5kHz)
    float normalizedFreq = std::min(frequency / 5000.0f, 1.0f);

    // Bell flare increases radiation efficiency
    float flareEffect = 0.5f * params.flareFactor;

    // Radiation gain increases with frequency and flare
    float radiationGain = 1.0f + flareEffect * normalizedFreq;

    return radiationGain;
}

float BoreWaveguide::calculateRadiationImpedance(float frequency, float bellSize) const
{
    // ENHANCED: Radiation impedance modeling
    // Bell acts as a matching transformer between bore and free air

    // Higher frequencies have lower radiation impedance (radiate more easily)
    float freqEffect = std::sqrt(frequency / 1000.0f);

    // Larger bells have better impedance matching
    float sizeEffect = std::sqrt(bellSize);

    // Combined impedance effect
    float impedance = freqEffect * sizeEffect;

    // Normalize to reasonable range
    return std::clamp(impedance, 0.7f, 1.5f);
}

float BoreWaveguide::bellRadiationStage1(float input, float bellSize)
{
    // Stage 1: Low-frequency radiation (bell acts as resonator)
    if (bellCoefficientsDirty || cachedBellSize != bellSize)
    {
        float cutoff = 200.0f / bellSize;
        stage1Coeff = cutoff / (cutoff + static_cast<float>(sr) * 0.5f);

        cutoff = 1000.0f / (bellSize * 0.7f);
        stage2Coeff = cutoff / (cutoff + static_cast<float>(sr) * 0.5f);

        cutoff = 3000.0f / bellSize;
        stage3Coeff = cutoff / (cutoff + static_cast<float>(sr) * 0.5f);

        cachedBellSize = bellSize;
        bellCoefficientsDirty = false;
    }

    stage1State = stage1State + stage1Coeff * (input - stage1State);

    return stage1State;
}

float BoreWaveguide::bellRadiationStage2(float input, float bellSize)
{
    // Stage 2: Mid-frequency emphasis (bell brightness)
    // Coefficients are cached in stage1
    stage2State = stage2State + stage2Coeff * (input - stage2State);

    // High-frequency emphasis
    float hfBoost = input - stage2State;

    return input + hfBoost * 0.5f;
}

float BoreWaveguide::bellRadiationStage3(float input, float bellSize)
{
    // Stage 3: High-frequency radiation (bell flare)
    // Coefficients are cached in stage1
    stage3State = stage3State + stage3Coeff * (input - stage3State);

    // Directional radiation (high frequencies are more directional)
    float directional = input - stage3State * 0.5f;

    return directional;
}

float BoreWaveguide::applyFrequencyDependentLoss(float input, float lfLoss, float hfLoss)
{
    // Split into frequency bands
    // Low-pass for low frequencies
    if (lossCoefficientsDirty)
    {
        lfLossCoeff = 500.0f / (500.0f + static_cast<float>(sr) * 0.5f);
        hfLossCoeff = 1500.0f / (1500.0f + static_cast<float>(sr) * 0.5f);
        lossCoefficientsDirty = false;
    }

    lfState = lfState + lfLossCoeff * (input - lfState);

    // High-pass for high frequencies
    hfState = hfState + hfLossCoeff * (input - hfState);
    float hfContent = input - hfState;

    // Apply different losses
    return lfState * lfLoss + hfContent * hfLoss;
}

//==============================================================================
// Bell Radiation Filter Implementation
//==============================================================================

BellRadiationFilter::BellRadiationFilter() = default;

void BellRadiationFilter::prepare(double sampleRate)
{
    sr = sampleRate;
    reset();
}

void BellRadiationFilter::reset()
{
    shaperState = 0.0f;
}

float BellRadiationFilter::processSample(float input, float bellSize)
{
    // Cutoff frequency based on bell size (larger = lower cutoff)
    float cutoff = cutoffFrequency / bellSize;
    float filtered = radiationFilter(input, cutoff);

    return filtered;
}

void BellRadiationFilter::setCutoffFrequency(float freq)
{
    cutoffFrequency = freq;
}

float BellRadiationFilter::radiationFilter(float input, float cutoff)
{
    // Simple first-order lowpass for HF radiation
    float coeff = cutoff / (cutoff + static_cast<float>(sr) * 0.5f);
    shaperState = shaperState + coeff * (input - shaperState);
    return shaperState;
}

//==============================================================================
// Horn Formant Shaper Implementation
//==============================================================================

HornFormantShaper::HornFormantShaper()
{
    initializeHornType(HornType::Tuba);
}

void HornFormantShaper::prepare(double sampleRate)
{
    sr = sampleRate;
    for (auto& formant : formants)
    {
        formant.prepare(sampleRate);
    }
    reset();
}

void HornFormantShaper::reset()
{
    for (auto& formant : formants)
    {
        formant.reset();
    }

    // Reset filter states
    brightnessState = 0.0f;
    warmthState = 0.0f;
}

float HornFormantShaper::processSample(float input)
{
    // Process through formant filters
    float formantOutput = 0.0f;
    for (auto& formant : formants)
    {
        formantOutput += formant.processSample(input);
    }

    // Average formants
    if (!formants.empty())
    {
        formantOutput /= static_cast<float>(formants.size());
    }

    // Apply tonal shaping
    float bright = brightnessFilter(formantOutput, params.brightness);
    float warm = warmthFilter(bright, params.warmth);

    // Apply metalness (brass character)
    float metalness = params.metalness;
    float output = warm * (1.0f + metalness * 0.3f);

    return output;
}

void HornFormantShaper::setParameters(const Parameters& p)
{
    params = p;
}

void HornFormantShaper::setHornType(HornType type)
{
    params.hornType = type;
    initializeHornType(type);
}

float HornFormantShaper::brightnessFilter(float input, float amount)
{
    // High-frequency emphasis
    float coeff = (2000.0f * amount + 500.0f) /
                  (2000.0f * amount + 500.0f + static_cast<float>(sr));
    brightnessState = brightnessState + coeff * (input - brightnessState);
    return input + (input - brightnessState) * amount;
}

float HornFormantShaper::warmthFilter(float input, float amount)
{
    // Low-frequency emphasis
    float coeff = (200.0f * amount + 50.0f) /
                  (200.0f * amount + 50.0f + static_cast<float>(sr));
    warmthState = warmthState + coeff * (input - warmthState);
    return input * (1.0f - amount * 0.5f) + warmthState * amount;
}

void HornFormantShaper::initializeHornType(HornType type)
{
    formants.clear();

    switch (type)
    {
        case HornType::Trumpet:
            // Bright, focused
            {
                HornFormantShaper::FormantFilter f1;
                f1.frequency = 1200.0f;
                f1.amplitude = 1.0f;
                f1.bandwidth = 1.5f;
                formants.push_back(f1);

                HornFormantShaper::FormantFilter f2;
                f2.frequency = 2500.0f;
                f2.amplitude = 0.7f;
                f2.bandwidth = 2.0f;
                formants.push_back(f2);

                HornFormantShaper::FormantFilter f3;
                f3.frequency = 4000.0f;
                f3.amplitude = 0.4f;
                f3.bandwidth = 2.5f;
                formants.push_back(f3);
            }
            break;

        case HornType::Trombone:
            // Warm, broad
            {
                HornFormantShaper::FormantFilter f1;
                f1.frequency = 500.0f;
                f1.amplitude = 1.0f;
                f1.bandwidth = 1.2f;
                formants.push_back(f1);

                HornFormantShaper::FormantFilter f2;
                f2.frequency = 1500.0f;
                f2.amplitude = 0.8f;
                f2.bandwidth = 1.8f;
                formants.push_back(f2);

                HornFormantShaper::FormantFilter f3;
                f3.frequency = 3000.0f;
                f3.amplitude = 0.5f;
                f3.bandwidth = 2.2f;
                formants.push_back(f3);
            }
            break;

        case HornType::Tuba:
            // Dark, massive
            {
                HornFormantShaper::FormantFilter f1;
                f1.frequency = 80.0f;
                f1.amplitude = 1.0f;
                f1.bandwidth = 0.8f;
                formants.push_back(f1);

                HornFormantShaper::FormantFilter f2;
                f2.frequency = 400.0f;
                f2.amplitude = 0.9f;
                f2.bandwidth = 1.2f;
                formants.push_back(f2);

                HornFormantShaper::FormantFilter f3;
                f3.frequency = 1200.0f;
                f3.amplitude = 0.6f;
                f3.bandwidth = 1.8f;
                formants.push_back(f3);

                HornFormantShaper::FormantFilter f4;
                f4.frequency = 2500.0f;
                f4.amplitude = 0.3f;
                f4.bandwidth = 2.5f;
                formants.push_back(f4);
            }
            break;

        case HornType::FrenchHorn:
            // Mellow, complex
            {
                HornFormantShaper::FormantFilter f1;
                f1.frequency = 200.0f;
                f1.amplitude = 1.0f;
                f1.bandwidth = 1.0f;
                formants.push_back(f1);

                HornFormantShaper::FormantFilter f2;
                f2.frequency = 800.0f;
                f2.amplitude = 0.8f;
                f2.bandwidth = 1.5f;
                formants.push_back(f2);

                HornFormantShaper::FormantFilter f3;
                f3.frequency = 2000.0f;
                f3.amplitude = 0.6f;
                f3.bandwidth = 2.0f;
                formants.push_back(f3);

                HornFormantShaper::FormantFilter f4;
                f4.frequency = 3500.0f;
                f4.amplitude = 0.4f;
                f4.bandwidth = 2.8f;
                formants.push_back(f4);
            }
            break;

        case HornType::Saxophone:
            // Reed character
            {
                HornFormantShaper::FormantFilter f1;
                f1.frequency = 400.0f;
                f1.amplitude = 1.0f;
                f1.bandwidth = 1.3f;
                formants.push_back(f1);

                HornFormantShaper::FormantFilter f2;
                f2.frequency = 1500.0f;
                f2.amplitude = 0.7f;
                f2.bandwidth = 1.8f;
                formants.push_back(f2);

                HornFormantShaper::FormantFilter f3;
                f3.frequency = 3000.0f;
                f3.amplitude = 0.5f;
                f3.bandwidth = 2.2f;
                formants.push_back(f3);
            }
            break;

        case HornType::Custom:
        default:
            // Neutral
            {
                HornFormantShaper::FormantFilter f1;
                f1.frequency = 500.0f;
                f1.amplitude = 1.0f;
                f1.bandwidth = 1.5f;
                formants.push_back(f1);

                HornFormantShaper::FormantFilter f2;
                f2.frequency = 1500.0f;
                f2.amplitude = 0.7f;
                f2.bandwidth = 2.0f;
                formants.push_back(f2);

                HornFormantShaper::FormantFilter f3;
                f3.frequency = 3000.0f;
                f3.amplitude = 0.4f;
                f3.bandwidth = 2.5f;
                formants.push_back(f3);
            }
            break;
    }

    // Prepare formants
    for (auto& formant : formants)
    {
        formant.prepare(sr);
    }
}

// FormantFilter implementation
void HornFormantShaper::FormantFilter::prepare(double sampleRate)
{
    sr = sampleRate;
}

float HornFormantShaper::FormantFilter::processSample(float input)
{
    // Simple resonant filter
    float freq = frequency; // Frequency can be modulated by parent
    float bw = bandwidth * 100.0f;
    float r = std::exp(-bw / (freq + bw));
    float coeff = 2.0f * r * SchillingerEcosystem::DSP::fastCosineLookup(phase);

    phase += freq * 2.0f * static_cast<float>(M_PI) / static_cast<float>(sr);
    if (phase >= 2.0f * static_cast<float>(M_PI))
        phase -= 2.0f * static_cast<float>(M_PI);

    state = coeff * state - r * r * input + amplitude * input;
    return state;
}

void HornFormantShaper::FormantFilter::reset()
{
    state = 0.0f;
    phase = 0.0f;
}

//==============================================================================
// Giant Horn Voice Implementation
//==============================================================================

void GiantHornVoice::prepare(double sampleRate)
{
    sr = sampleRate;
    lipReed.prepare(sampleRate);
    bore.prepare(sampleRate);
    bell.prepare(sampleRate);
    formants.prepare(sampleRate);
    reset();
}

void GiantHornVoice::reset()
{
    lipReed.reset();
    bore.reset();
    bell.reset();
    formants.reset();
    currentPressure = 0.0f;
    targetPressure = 0.0f;
    envelopePhase = 0.0f;
    active = false;
}

void GiantHornVoice::trigger(int note, float vel, const GiantGestureParameters& gestureParam,
                             const GiantScaleParameters& scaleParam)
{
    midiNote = note;
    velocity = vel;
    gesture = gestureParam;
    scale = scaleParam;

    // Calculate target pressure from velocity and gesture force
    targetPressure = calculateTargetPressure(vel, gesture.force);

    // Reset envelope
    envelopePhase = 0.0f;
    currentPressure = 0.0f;

    // Set bore length based on note
    float freq = SchillingerEcosystem::DSP::LookupTables::getInstance().midiToFreq(static_cast<float>(note));
    float boreLength = 343.0f / (2.0f * freq);
    bore.setLengthMeters(boreLength);

    active = true;
}

void GiantHornVoice::release(bool damping)
{
    envelopePhase = 2.0f; // Release phase
    if (damping)
    {
        targetPressure = 0.0f;
    }
}

float GiantHornVoice::processSample()
{
    if (!active)
        return 0.0f;

    // Process pressure envelope
    float pressure = processPressureEnvelope();

    if (pressure < 0.0001f && envelopePhase >= 2.0f)
    {
        active = false;
        return 0.0f;
    }

    // Calculate frequency
    float frequency = SchillingerEcosystem::DSP::LookupTables::getInstance().midiToFreq(static_cast<float>(midiNote));

    // Apply scale-based frequency shift (giant instruments are lower)
    frequency *= 1.0f / (1.0f + scale.scaleMeters * 0.05f);

    // Process lip reed exciter
    float excitation = lipReed.processSample(pressure, frequency);

    // Process bore waveguide
    float boreOutput = bore.processSample(excitation);

    // Process bell radiation
    float bellOutput = bell.processSample(boreOutput, 1.5f);

    // Process formant shaping
    float output = formants.processSample(bellOutput);

    // Apply velocity and scale
    output *= velocity;
    output *= 1.0f / (1.0f + scale.scaleMeters * 0.1f); // Giant = quieter

    return output;
}

bool GiantHornVoice::isActive() const
{
    return active;
}

float GiantHornVoice::calculateTargetPressure(float velocity, float force) const
{
    // Combine velocity and gesture force
    return velocity * (0.5f + force * 0.5f);
}

float GiantHornVoice::processPressureEnvelope()
{
    // Giant scale = slower attack
    float attackTime = 0.1f + scale.transientSlowing * 0.7f; // 100-800ms
    float attackCoeff = 1.0f / (attackTime * static_cast<float>(sr));

    float releaseTime = 0.2f + scale.transientSlowing * 0.5f;
    float releaseCoeff = 1.0f / (releaseTime * static_cast<float>(sr));

    if (envelopePhase < 1.0f)
    {
        // Attack
        currentPressure += (targetPressure - currentPressure) * attackCoeff;
        if (std::abs(targetPressure - currentPressure) < 0.001f)
        {
            envelopePhase = 1.0f;
        }
    }
    else if (envelopePhase >= 2.0f)
    {
        // Release (exponential decay for more natural release)
        currentPressure *= std::exp(-releaseCoeff);
    }

    return currentPressure;
}

//==============================================================================
// Giant Horn Voice Manager Implementation
//==============================================================================

GiantHornVoiceManager::GiantHornVoiceManager() = default;

void GiantHornVoiceManager::prepare(double sampleRate, int maxVoices)
{
    currentSampleRate = sampleRate;
    voices.clear();

    for (int i = 0; i < maxVoices; ++i)
    {
        auto voice = std::make_unique<GiantHornVoice>();
        voice->prepare(sampleRate);
        voices.push_back(std::move(voice));
    }
}

void GiantHornVoiceManager::reset()
{
    for (auto& voice : voices)
    {
        voice->reset();
    }
}

GiantHornVoice* GiantHornVoiceManager::findFreeVoice()
{
    // First try to find inactive voice
    for (auto& voice : voices)
    {
        if (!voice->isActive())
        {
            return voice.get();
        }
    }

    // If all active, steal oldest (simple strategy)
    return voices[0].get();
}

GiantHornVoice* GiantHornVoiceManager::findVoiceForNote(int note)
{
    for (auto& voice : voices)
    {
        if (voice->isActive() && voice->midiNote == note)
        {
            return voice.get();
        }
    }
    return nullptr;
}

void GiantHornVoiceManager::handleNoteOn(int note, float velocity,
                                         const GiantGestureParameters& gesture,
                                         const GiantScaleParameters& scale)
{
    GiantHornVoice* voice = findVoiceForNote(note);
    if (voice != nullptr)
    {
        // Retrigger
        voice->trigger(note, velocity, gesture, scale);
    }
    else
    {
        voice = findFreeVoice();
        if (voice != nullptr)
        {
            voice->trigger(note, velocity, gesture, scale);
        }
    }
}

void GiantHornVoiceManager::handleNoteOff(int note, bool damping)
{
    GiantHornVoice* voice = findVoiceForNote(note);
    if (voice != nullptr)
    {
        voice->release(damping);
    }
}

void GiantHornVoiceManager::allNotesOff()
{
    for (auto& voice : voices)
    {
        if (voice->isActive())
        {
            voice->release();
        }
    }
}

float GiantHornVoiceManager::processSample()
{
    float output = 0.0f;
    for (auto& voice : voices)
    {
        output += voice->processSample();
    }

    // Soft clip to prevent overload
    output = std::tanh(output);

    return output;
}

int GiantHornVoiceManager::getActiveVoiceCount() const
{
    int count = 0;
    for (const auto& voice : voices)
    {
        if (voice->isActive())
            count++;
    }
    return count;
}

void GiantHornVoiceManager::setLipReedParameters(const LipReedExciter::Parameters& params)
{
    for (auto& voice : voices)
    {
        voice->lipReed.setParameters(params);
    }
}

void GiantHornVoiceManager::setBoreParameters(const BoreWaveguide::Parameters& params)
{
    for (auto& voice : voices)
    {
        voice->bore.setParameters(params);
    }
}

void GiantHornVoiceManager::setFormantParameters(const HornFormantShaper::Parameters& params)
{
    for (auto& voice : voices)
    {
        voice->formants.setParameters(params);
    }
}

//==============================================================================
// AetherGiantHornsPureDSP Implementation
//==============================================================================

AetherGiantHornsPureDSP::AetherGiantHornsPureDSP()
{
    // Initialize default giant parameters
    currentScale_.scaleMeters = params_.scaleMeters;
    currentScale_.massBias = params_.massBias;
    currentScale_.airLoss = params_.airLoss;
    currentScale_.transientSlowing = params_.transientSlowing;

    currentGesture_.force = params_.force;
    currentGesture_.speed = params_.speed;
    currentGesture_.contactArea = params_.contactArea;
    currentGesture_.roughness = params_.roughness;
}

AetherGiantHornsPureDSP::~AetherGiantHornsPureDSP() = default;

bool AetherGiantHornsPureDSP::prepare(double sampleRate, int blockSize)
{
    sampleRate_ = sampleRate;
    blockSize_ = blockSize;

    voiceManager_.prepare(sampleRate, maxVoices_);

    applyParameters();

    return true;
}

void AetherGiantHornsPureDSP::reset()
{
    voiceManager_.reset();
}

void AetherGiantHornsPureDSP::process(float** outputs, int numChannels, int numSamples)
{
    // Clear outputs
    for (int ch = 0; ch < numChannels; ++ch)
    {
        std::fill(outputs[ch], outputs[ch] + numSamples, 0.0f);
    }

    // Process samples
    for (int i = 0; i < numSamples; ++i)
    {
        float sample = voiceManager_.processSample() * params_.masterVolume;

        // Stereo output (mono source)
        for (int ch = 0; ch < numChannels; ++ch)
        {
            outputs[ch][i] = sample;
        }
    }
}

void AetherGiantHornsPureDSP::handleEvent(const ScheduledEvent& event)
{
    switch (event.type)
    {
        case ScheduledEvent::NOTE_ON:
        {
            GiantGestureParameters gesture = currentGesture_;
            GiantScaleParameters scale = currentScale_;

            voiceManager_.handleNoteOn(event.data.note.midiNote, event.data.note.velocity,
                                       gesture, scale);
            break;
        }

        case ScheduledEvent::NOTE_OFF:
            voiceManager_.handleNoteOff(event.data.note.midiNote, false);
            break;

        case ScheduledEvent::PITCH_BEND:
            // Could apply to bore length modification
            break;

        case ScheduledEvent::CHANNEL_PRESSURE:
        {
            // Channel pressure -> force
            GiantGestureParameters gesture = currentGesture_;
            gesture.force = event.data.channelPressure.pressure;
            currentGesture_ = gesture;
            break;
        }

        case ScheduledEvent::PARAM_CHANGE:
            // Handle parameter changes
            setParameter(event.data.param.paramId, event.data.param.value);
            break;

        case ScheduledEvent::CONTROL_CHANGE:
            // Handle CC messages
            break;

        case ScheduledEvent::RESET:
            reset();
            break;

        default:
            break;
    }
}

float AetherGiantHornsPureDSP::getParameter(const char* paramId) const
{
    // Lip reed
    if (std::strcmp(paramId, "lipTension") == 0) return params_.lipTension;
    if (std::strcmp(paramId, "mouthPressure") == 0) return params_.mouthPressure;
    if (std::strcmp(paramId, "nonlinearity") == 0) return params_.nonlinearity;
    if (std::strcmp(paramId, "chaosThreshold") == 0) return params_.chaosThreshold;
    if (std::strcmp(paramId, "growlAmount") == 0) return params_.growlAmount;
    if (std::strcmp(paramId, "lipMass") == 0) return params_.lipMass;
    if (std::strcmp(paramId, "lipStiffness") == 0) return params_.lipStiffness;

    // Bore
    if (std::strcmp(paramId, "boreLength") == 0) return params_.boreLength;
    if (std::strcmp(paramId, "reflectionCoeff") == 0) return params_.reflectionCoeff;
    if (std::strcmp(paramId, "boreShape") == 0) return params_.boreShape;
    if (std::strcmp(paramId, "flareFactor") == 0) return params_.flareFactor;

    // Bell
    if (std::strcmp(paramId, "bellSize") == 0) return params_.bellSize;

    // Formants
    if (std::strcmp(paramId, "hornType") == 0) return params_.hornType;
    if (std::strcmp(paramId, "brightness") == 0) return params_.brightness;
    if (std::strcmp(paramId, "warmth") == 0) return params_.warmth;
    if (std::strcmp(paramId, "metalness") == 0) return params_.metalness;

    // Giant
    if (std::strcmp(paramId, "scaleMeters") == 0) return params_.scaleMeters;
    if (std::strcmp(paramId, "massBias") == 0) return params_.massBias;
    if (std::strcmp(paramId, "airLoss") == 0) return params_.airLoss;
    if (std::strcmp(paramId, "transientSlowing") == 0) return params_.transientSlowing;

    // Gesture
    if (std::strcmp(paramId, "force") == 0) return params_.force;
    if (std::strcmp(paramId, "speed") == 0) return params_.speed;
    if (std::strcmp(paramId, "contactArea") == 0) return params_.contactArea;
    if (std::strcmp(paramId, "roughness") == 0) return params_.roughness;

    // Global
    if (std::strcmp(paramId, "masterVolume") == 0) return params_.masterVolume;

    return 0.0f;
}

void AetherGiantHornsPureDSP::setParameter(const char* paramId, float value)
{
    // Lip reed
    if (std::strcmp(paramId, "lipTension") == 0) params_.lipTension = value;
    else if (std::strcmp(paramId, "mouthPressure") == 0) params_.mouthPressure = value;
    else if (std::strcmp(paramId, "nonlinearity") == 0) params_.nonlinearity = value;
    else if (std::strcmp(paramId, "chaosThreshold") == 0) params_.chaosThreshold = value;
    else if (std::strcmp(paramId, "growlAmount") == 0) params_.growlAmount = value;
    else if (std::strcmp(paramId, "lipMass") == 0) params_.lipMass = value;
    else if (std::strcmp(paramId, "lipStiffness") == 0) params_.lipStiffness = value;

    // Bore
    else if (std::strcmp(paramId, "boreLength") == 0) params_.boreLength = value;
    else if (std::strcmp(paramId, "reflectionCoeff") == 0) params_.reflectionCoeff = value;
    else if (std::strcmp(paramId, "boreShape") == 0) params_.boreShape = value;
    else if (std::strcmp(paramId, "flareFactor") == 0) params_.flareFactor = value;

    // Bell
    else if (std::strcmp(paramId, "bellSize") == 0) params_.bellSize = value;

    // Formants
    else if (std::strcmp(paramId, "hornType") == 0) params_.hornType = value;
    else if (std::strcmp(paramId, "brightness") == 0) params_.brightness = value;
    else if (std::strcmp(paramId, "warmth") == 0) params_.warmth = value;
    else if (std::strcmp(paramId, "metalness") == 0) params_.metalness = value;

    // Giant
    else if (std::strcmp(paramId, "scaleMeters") == 0) params_.scaleMeters = value;
    else if (std::strcmp(paramId, "massBias") == 0) params_.massBias = value;
    else if (std::strcmp(paramId, "airLoss") == 0) params_.airLoss = value;
    else if (std::strcmp(paramId, "transientSlowing") == 0) params_.transientSlowing = value;

    // Gesture
    else if (std::strcmp(paramId, "force") == 0) params_.force = value;
    else if (std::strcmp(paramId, "speed") == 0) params_.speed = value;
    else if (std::strcmp(paramId, "contactArea") == 0) params_.contactArea = value;
    else if (std::strcmp(paramId, "roughness") == 0) params_.roughness = value;

    // Global
    else if (std::strcmp(paramId, "masterVolume") == 0) params_.masterVolume = value;

    applyParameters();
}

bool AetherGiantHornsPureDSP::savePreset(char* jsonBuffer, int jsonBufferSize) const
{
    int offset = 0;

    // Opening brace
    const char* open = "{";
    std::strncpy(jsonBuffer + offset, open, jsonBufferSize - offset);
    offset += static_cast<int>(std::strlen(open));

    // Write parameters
    writeJsonParameter("lipTension", params_.lipTension, jsonBuffer, offset, jsonBufferSize);
    writeJsonParameter("mouthPressure", params_.mouthPressure, jsonBuffer, offset, jsonBufferSize);
    writeJsonParameter("nonlinearity", params_.nonlinearity, jsonBuffer, offset, jsonBufferSize);
    writeJsonParameter("chaosThreshold", params_.chaosThreshold, jsonBuffer, offset, jsonBufferSize);
    writeJsonParameter("growlAmount", params_.growlAmount, jsonBuffer, offset, jsonBufferSize);
    writeJsonParameter("lipMass", params_.lipMass, jsonBuffer, offset, jsonBufferSize);
    writeJsonParameter("lipStiffness", params_.lipStiffness, jsonBuffer, offset, jsonBufferSize);
    writeJsonParameter("boreLength", params_.boreLength, jsonBuffer, offset, jsonBufferSize);
    writeJsonParameter("reflectionCoeff", params_.reflectionCoeff, jsonBuffer, offset, jsonBufferSize);
    writeJsonParameter("boreShape", params_.boreShape, jsonBuffer, offset, jsonBufferSize);
    writeJsonParameter("flareFactor", params_.flareFactor, jsonBuffer, offset, jsonBufferSize);
    writeJsonParameter("bellSize", params_.bellSize, jsonBuffer, offset, jsonBufferSize);
    writeJsonParameter("hornType", params_.hornType, jsonBuffer, offset, jsonBufferSize);
    writeJsonParameter("brightness", params_.brightness, jsonBuffer, offset, jsonBufferSize);
    writeJsonParameter("warmth", params_.warmth, jsonBuffer, offset, jsonBufferSize);
    writeJsonParameter("metalness", params_.metalness, jsonBuffer, offset, jsonBufferSize);
    writeJsonParameter("scaleMeters", params_.scaleMeters, jsonBuffer, offset, jsonBufferSize);
    writeJsonParameter("massBias", params_.massBias, jsonBuffer, offset, jsonBufferSize);
    writeJsonParameter("airLoss", params_.airLoss, jsonBuffer, offset, jsonBufferSize);
    writeJsonParameter("transientSlowing", params_.transientSlowing, jsonBuffer, offset, jsonBufferSize);
    writeJsonParameter("force", params_.force, jsonBuffer, offset, jsonBufferSize);
    writeJsonParameter("speed", params_.speed, jsonBuffer, offset, jsonBufferSize);
    writeJsonParameter("contactArea", params_.contactArea, jsonBuffer, offset, jsonBufferSize);
    writeJsonParameter("roughness", params_.roughness, jsonBuffer, offset, jsonBufferSize);
    writeJsonParameter("masterVolume", params_.masterVolume, jsonBuffer, offset, jsonBufferSize);

    // Remove trailing comma
    if (offset > 0 && jsonBuffer[offset - 1] == ',')
    {
        offset--;
    }

    // Closing brace
    const char* close = "}";
    std::strncpy(jsonBuffer + offset, close, jsonBufferSize - offset);
    offset += static_cast<int>(std::strlen(close));

    return offset < jsonBufferSize;
}

bool AetherGiantHornsPureDSP::loadPreset(const char* jsonData)
{
    double value = 0.0;

    if (parseJsonParameter(jsonData, "lipTension", value))
        params_.lipTension = static_cast<float>(value);
    if (parseJsonParameter(jsonData, "mouthPressure", value))
        params_.mouthPressure = static_cast<float>(value);
    if (parseJsonParameter(jsonData, "nonlinearity", value))
        params_.nonlinearity = static_cast<float>(value);
    if (parseJsonParameter(jsonData, "chaosThreshold", value))
        params_.chaosThreshold = static_cast<float>(value);
    if (parseJsonParameter(jsonData, "growlAmount", value))
        params_.growlAmount = static_cast<float>(value);
    if (parseJsonParameter(jsonData, "lipMass", value))
        params_.lipMass = static_cast<float>(value);
    if (parseJsonParameter(jsonData, "lipStiffness", value))
        params_.lipStiffness = static_cast<float>(value);
    if (parseJsonParameter(jsonData, "boreLength", value))
        params_.boreLength = static_cast<float>(value);
    if (parseJsonParameter(jsonData, "reflectionCoeff", value))
        params_.reflectionCoeff = static_cast<float>(value);
    if (parseJsonParameter(jsonData, "boreShape", value))
        params_.boreShape = static_cast<float>(value);
    if (parseJsonParameter(jsonData, "flareFactor", value))
        params_.flareFactor = static_cast<float>(value);
    if (parseJsonParameter(jsonData, "bellSize", value))
        params_.bellSize = static_cast<float>(value);
    if (parseJsonParameter(jsonData, "hornType", value))
        params_.hornType = static_cast<float>(value);
    if (parseJsonParameter(jsonData, "brightness", value))
        params_.brightness = static_cast<float>(value);
    if (parseJsonParameter(jsonData, "warmth", value))
        params_.warmth = static_cast<float>(value);
    if (parseJsonParameter(jsonData, "metalness", value))
        params_.metalness = static_cast<float>(value);
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

int AetherGiantHornsPureDSP::getActiveVoiceCount() const
{
    return const_cast<GiantHornVoiceManager&>(voiceManager_).getActiveVoiceCount();
}

void AetherGiantHornsPureDSP::applyParameters()
{
    // Update giant parameters
    currentScale_.scaleMeters = params_.scaleMeters;
    currentScale_.massBias = params_.massBias;
    currentScale_.airLoss = params_.airLoss;
    currentScale_.transientSlowing = params_.transientSlowing;

    currentGesture_.force = params_.force;
    currentGesture_.speed = params_.speed;
    currentGesture_.contactArea = params_.contactArea;
    currentGesture_.roughness = params_.roughness;

    // Apply to voice manager
    LipReedExciter::Parameters lipParams;
    lipParams.lipTension = params_.lipTension;
    lipParams.mouthPressure = params_.mouthPressure;
    lipParams.nonlinearity = params_.nonlinearity;
    lipParams.chaosThreshold = params_.chaosThreshold;
    lipParams.growlAmount = params_.growlAmount;
    lipParams.lipMass = params_.lipMass;
    lipParams.lipStiffness = params_.lipStiffness;
    voiceManager_.setLipReedParameters(lipParams);

    BoreWaveguide::Parameters boreParams;
    boreParams.lengthMeters = params_.boreLength;
    boreParams.reflectionCoeff = params_.reflectionCoeff;
    boreParams.boreShape = static_cast<BoreWaveguide::BoreShape>(static_cast<int>(params_.boreShape));
    boreParams.flareFactor = params_.flareFactor;
    voiceManager_.setBoreParameters(boreParams);

    HornFormantShaper::Parameters formantParams;
    formantParams.hornType = static_cast<HornFormantShaper::HornType>(static_cast<int>(params_.hornType));
    formantParams.brightness = params_.brightness;
    formantParams.warmth = params_.warmth;
    formantParams.metalness = params_.metalness;
    voiceManager_.setFormantParameters(formantParams);
}

void AetherGiantHornsPureDSP::processStereoSample(float& left, float& right)
{
    float sample = voiceManager_.processSample() * params_.masterVolume;
    left = sample;
    right = sample;
}

float AetherGiantHornsPureDSP::calculateFrequency(int midiNote) const
{
    return SchillingerEcosystem::DSP::LookupTables::getInstance().midiToFreq(static_cast<float>(midiNote));
}

bool AetherGiantHornsPureDSP::writeJsonParameter(const char* name, double value,
                                                  char* buffer, int& offset, int bufferSize) const
{
    char temp[256];
    int written = std::snprintf(temp, sizeof(temp), "\"%s\": %.6g,", name, value);
    if (offset + written >= bufferSize)
        return false;
    std::strncpy(buffer + offset, temp, bufferSize - offset);
    offset += written;
    return true;
}

bool AetherGiantHornsPureDSP::parseJsonParameter(const char* json, const char* param, double& value) const
{
    // Simple JSON parser (looking for "param": value)
    char search[256];
    std::snprintf(search, sizeof(search), "\"%s\":", param);

    const char* found = std::strstr(json, search);
    if (found == nullptr)
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
    struct AetherGiantHornsRegistrar {
        AetherGiantHornsRegistrar() {
            registerInstrumentFactory("AetherGiantHorns", []() -> InstrumentDSP* {
                return new AetherGiantHornsPureDSP();
            });
        }
    } registrar;
}
*/

}  // namespace DSP
