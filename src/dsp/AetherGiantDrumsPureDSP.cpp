/*
  ==============================================================================

   AetherGiantDrumsPureDSP.cpp
   Giant Drum Synthesizer (Seismic Membranes)

   Physical modeling of giant-scale drums with:
   - SVF-based membrane resonator (2-6 primary modes with tension/diameter scaling)
   - Bidirectional shell/cavity coupling (Helmholtz resonator model)
   - Nonlinear loss/saturation (prevents sterile modal ringing)
   - Room coupling (early reflections, "huge room" feel)

   Version 2.0 - Advanced Membrane Physics:
   - State Variable Filter membrane model for realistic 2D vibration patterns
   - Coupled shell/cavity system with natural pitch envelope
   - Better decay characteristics and transient response

  ==============================================================================
*/

#include "dsp/AetherGiantDrumsDSP.h"
#include "dsp/InstrumentFactory.h"
#include "../../../../include/dsp/LookupTables.h"
#include <cmath>
#include <algorithm>
#include <random>

namespace DSP {

//==============================================================================
// SVFMembraneMode Implementation (State Variable Filter)
//==============================================================================

void SVFMembraneMode::prepare(double sr)
{
    sampleRate = sr;

    // Initialize SVF filter state
    z1 = 0.0f;
    z2 = 0.0f;

    // Calculate filter coefficients
    calculateCoefficients();

    reset();
}

float SVFMembraneMode::processSample(float excitation)
{
    // State Variable Filter (TPT structure) for realistic membrane resonance
    // Based on Andy Simper's trapezoidal integrator design

    // Apply excitation through filter
    float hp = excitation - z1 * (resonance + 1.0f) - z2;
    float bp = z1 + frequencyFactor * hp;
    float lp = z2 + frequencyFactor * bp;

    // Update state
    z1 = bp;
    z2 = lp;

    // Output from bandpass (resonant mode)
    float output = bp * amplitude;

    // Apply energy decay (simulates air damping and membrane loss)
    energy = energy * decay + excitation * amplitude;
    output *= energy;

    return output;
}

void SVFMembraneMode::reset()
{
    z1 = 0.0f;
    z2 = 0.0f;
    energy = 0.0f;
    coefficientsDirty = true;
}

void SVFMembraneMode::calculateCoefficients()
{
    // Only recalculate if parameters changed
    if (frequency != cachedFrequency || qFactor != cachedQFactor)
    {
        // Calculate frequency factor for SVF (g parameter)
        // g = (2 * pi * f) / sr
        float omega = 2.0f * juce::MathConstants<float>::pi * frequency;
        frequencyFactor = omega / static_cast<float>(sampleRate);

        // Clamp to prevent instability
        frequencyFactor = juce::jlimit(0.0f, 0.5f, frequencyFactor);

        // Q factor maps to resonance (higher Q = more ringing)
        // For realistic membrane modes, Q ranges from 10-100
        resonance = juce::jlimit(0.0f, 2.0f, qFactor);

        // Update cached values
        cachedFrequency = frequency;
        cachedQFactor = qFactor;
        coefficientsDirty = false;
    }
}

//==============================================================================
// MembraneResonator Implementation
//==============================================================================

MembraneResonator::MembraneResonator()
{
    svfModes.resize(6);  // Max 6 SVF modes
}

void MembraneResonator::prepare(double sampleRate)
{
    sr = sampleRate;

    for (auto& mode : svfModes) {
        mode.prepare(sampleRate);
    }

    updateModeFrequencies();
    updateModeDecays();
}

void MembraneResonator::reset()
{
    totalEnergy = 0.0f;
    strikeEnergy = 0.0f;

    for (auto& mode : svfModes) {
        mode.reset();
    }
}

void MembraneResonator::strike(float velocity, float force, float contactArea)
{
    // Calculate strike energy based on velocity, force, and contact area
    float strikePower = velocity * force * (1.0f + contactArea);

    // Distribute energy among SVF modes (fundamental gets most)
    float energySum = 0.0f;

    for (size_t i = 0; i < svfModes.size() && i < static_cast<size_t>(params.numModes); ++i) {
        // Lower modes get more energy (modeled after circular membrane physics)
        float modeEnergy = strikePower / (1.0f + static_cast<float>(i) * 0.5f);
        svfModes[i].energy = modeEnergy;
        energySum += modeEnergy;

        // Kick the SVF filter with an impulse to start resonance
        // This simulates the initial strike impulse on the membrane
        svfModes[i].processSample(modeEnergy * 0.5f);
    }

    totalEnergy = energySum;
    strikeEnergy = strikePower;
}

float MembraneResonator::processSample()
{
    float output = 0.0f;
    totalEnergy = 0.0f;

    // Sum all active SVF modes
    for (int i = 0; i < params.numModes && i < static_cast<int>(svfModes.size()); ++i) {
        output += svfModes[i].processSample(0.0f);
        totalEnergy += svfModes[i].energy;
    }

    return output;
}

void MembraneResonator::setParameters(const Parameters& p)
{
    params = p;
    updateModeFrequencies();
    updateModeDecays();
}

float MembraneResonator::getEnergy() const
{
    return totalEnergy;
}

void MembraneResonator::updateModeFrequencies()
{
    // Calculate SVF mode frequencies based on circular membrane physics
    // Fundamental (0,1) mode + higher overtones using Bessel function roots
    float fundamental = params.fundamentalFrequency;

    // Scale frequency by diameter (larger drums = lower pitch)
    float diameterScale = 1.0f / std::sqrt(params.diameterMeters);
    fundamental *= diameterScale;

    // Mode ratios for circular membrane (Bessel function J_n roots)
    // (0,1)=1.0, (1,1)=1.59, (2,1)=2.14, (0,2)=2.30, (3,1)=2.65, (1,2)=2.92
    float modeRatios[] = {1.0f, 1.59f, 2.14f, 2.30f, 2.65f, 2.92f};

    // Q factors for realistic membrane decay (higher modes decay faster)
    float modeQFactors[] = {50.0f, 40.0f, 30.0f, 25.0f, 20.0f, 15.0f};

    for (size_t i = 0; i < svfModes.size(); ++i) {
        // Apply inharmonicity to stretch modes (nonlinear membrane behavior)
        float inharmonicStretch = 1.0f + static_cast<float>(i) * params.inharmonicity;
        float newFrequency = fundamental * modeRatios[i] * inharmonicStretch;

        // Set Q factor for realistic decay
        float newQFactor = modeQFactors[i];

        // Check if parameters changed
        if (svfModes[i].frequency != newFrequency || svfModes[i].qFactor != newQFactor)
        {
            svfModes[i].frequency = newFrequency;
            svfModes[i].qFactor = newQFactor;
            svfModes[i].coefficientsDirty = true;
        }

        // Amplitude decreases for higher modes
        svfModes[i].amplitude = 1.0f / (1.0f + static_cast<float>(i) * 0.3f);

        // Recalculate SVF coefficients with new frequency (only if dirty)
        svfModes[i].calculateCoefficients();
    }
}

void MembraneResonator::updateModeDecays()
{
    // Larger drums have longer sustain (slower decay)
    float diameterFactor = std::sqrt(params.diameterMeters);

    for (auto& mode : svfModes) {
        // Base decay modified by diameter
        float modeDecay = params.damping;
        // Slower decay for larger drums (air mass effect)
        modeDecay = modeDecay * (0.995f + 0.004f * diameterFactor);
        mode.decay = std::min(0.9999f, modeDecay);
    }
}

//==============================================================================
// CoupledResonator Implementation (Bidirectional Shell/Cavity)
//==============================================================================

void CoupledResonator::prepare(double sampleRate)
{
    sr = sampleRate;

    // Initialize Helmholtz resonator (cavity air resonance)
    cavityPressure = 0.0f;
    cavityVelocity = 0.0f;

    // Initialize shell resonator
    shellDisplacement = 0.0f;
    shellVelocity = 0.0f;

    // Calculate coupling coefficients
    calculateCouplingCoefficients();

    reset();
}

float CoupledResonator::processSample(float membraneInput)
{
    // Bidirectional coupling between shell and cavity
    // Based on physical modeling principles:
    // - Shell vibrations drive cavity pressure
    // - Cavity pressure affects shell vibration
    // - Creates realistic pitch envelope during decay

    // Calculate forces
    float membraneForce = membraneInput * params.coupling;
    float cavityToShellForce = -cavityPressure * params.cavityToShellCoupling;
    float shellToCavityForce = shellDisplacement * params.shellToCavityCoupling;

    // Shell dynamics (mass-spring-damper)
    float shellAcceleration = (membraneForce + cavityToShellForce -
                               params.shellStiffness * shellDisplacement -
                               params.shellDamping * shellVelocity) / params.shellMass;

    shellVelocity += shellAcceleration / static_cast<float>(sr);
    shellDisplacement += shellVelocity / static_cast<float>(sr);

    // Cavity dynamics (Helmholtz resonator)
    float cavityAcceleration = (shellToCavityForce -
                                 params.cavityStiffness * cavityPressure -
                                 params.cavityDamping * cavityVelocity) / params.cavityMass;

    cavityVelocity += cavityAcceleration / static_cast<float>(sr);
    cavityPressure += cavityVelocity / static_cast<float>(sr);

    // Output mix of shell and cavity
    return shellDisplacement * params.shellMix + cavityPressure * params.cavityMix;
}

void CoupledResonator::reset()
{
    cavityPressure = 0.0f;
    cavityVelocity = 0.0f;
    shellDisplacement = 0.0f;
    shellVelocity = 0.0f;
}

void CoupledResonator::setParameters(const Parameters& p)
{
    params = p;
    calculateCouplingCoefficients();
}

void CoupledResonator::calculateCouplingCoefficients()
{
    // Calculate physical parameters from frequency and Q
    // Cavity acts as Helmholtz resonator
    float cavityOmega = 2.0f * juce::MathConstants<float>::pi * params.cavityFrequency;
    params.cavityMass = 1.0f;
    params.cavityStiffness = cavityOmega * cavityOmega;
    // Damping from Q: Q = 1 / (2 * damping_ratio), so damping_ratio = 1 / (2 * Q)
    // For mass-spring-damper: c = 2 * damping_ratio * sqrt(k * m)
    float cavityDampingRatio = 1.0f / (2.0f * params.cavityQ);
    params.cavityDamping = 2.0f * cavityDampingRatio * std::sqrt(params.cavityStiffness * params.cavityMass);

    // Shell resonator
    float shellOmega = 2.0f * juce::MathConstants<float>::pi * params.shellFormant;
    params.shellMass = 1.0f;
    params.shellStiffness = shellOmega * shellOmega;
    float shellDampingRatio = 1.0f / (2.0f * params.shellQ);
    params.shellDamping = 2.0f * shellDampingRatio * std::sqrt(params.shellStiffness * params.shellMass);

    // Coupling strengths (bidirectional)
    params.cavityToShellCoupling = params.coupling * 0.3f;
    params.shellToCavityCoupling = params.coupling * 0.5f;

    // Output mix
    params.shellMix = 0.4f;
    params.cavityMix = 0.6f;
}

//==============================================================================
// ShellResonator Implementation
//==============================================================================

ShellResonator::ShellResonator()
{
}

void ShellResonator::prepare(double sampleRate)
{
    sr = sampleRate;
    coupledResonator.prepare(sampleRate);

    // Set initial parameters
    CoupledResonator::Parameters coupledParams;
    coupledParams.cavityFrequency = params.cavityFrequency;
    coupledParams.shellFormant = params.shellFormant;
    coupledParams.cavityQ = params.cavityQ;
    coupledParams.shellQ = params.shellQ;
    coupledParams.coupling = params.coupling;

    coupledResonator.setParameters(coupledParams);
}

void ShellResonator::reset()
{
    coupledResonator.reset();
}

void ShellResonator::processMembraneEnergy(float membraneEnergy)
{
    // Feed membrane energy to coupled shell/cavity resonator
    // The bidirectional coupling handles energy transfer
    lastMembraneEnergy = membraneEnergy;
}

float ShellResonator::processSample()
{
    // Process through coupled resonator
    return coupledResonator.processSample(lastMembraneEnergy);
}

void ShellResonator::setParameters(const Parameters& p)
{
    params = p;

    CoupledResonator::Parameters coupledParams;
    coupledParams.cavityFrequency = params.cavityFrequency;
    coupledParams.shellFormant = params.shellFormant;
    coupledParams.cavityQ = params.cavityQ;
    coupledParams.shellQ = params.shellQ;
    coupledParams.coupling = params.coupling;

    coupledResonator.setParameters(coupledParams);
}

//==============================================================================
// DrumNonlinearLoss Implementation
//==============================================================================

DrumNonlinearLoss::DrumNonlinearLoss()
{
}

void DrumNonlinearLoss::prepare(double sampleRate)
{
    sr = sampleRate;
}

void DrumNonlinearLoss::reset()
{
}

float DrumNonlinearLoss::processSample(float input, float velocity)
{
    // Apply soft clipping saturation
    float saturated = softClip(input * (1.0f + saturationAmount));

    // Apply dynamic damping based on level and velocity
    float damping = calculateDynamicDamping(std::abs(input), velocity);

    // Apply mass effect (velocity-dependent loss)
    float massLoss = 1.0f - (massEffect * velocity * 0.1f);

    return saturated * damping * massLoss;
}

void DrumNonlinearLoss::setSaturationAmount(float amount)
{
    saturationAmount = juce::jlimit(0.0f, 1.0f, amount);
}

void DrumNonlinearLoss::setMassEffect(float mass)
{
    massEffect = juce::jlimit(0.0f, 1.0f, mass);
}

float DrumNonlinearLoss::softClip(float x) const
{
    // Soft clipping function (tanh-like)
    if (std::abs(x) < 1.0f) {
        return x - (x * x * x) / 3.0f;
    }
    return (x > 0.0f) ? 0.6667f : -0.6667f;
}

float DrumNonlinearLoss::calculateDynamicDamping(float level, float velocity) const
{
    // Higher levels and velocities get more damping
    float dynamicDamping = 1.0f - (level * velocity * 0.1f);
    return juce::jlimit(0.8f, 1.0f, dynamicDamping);
}

//==============================================================================
// DrumRoomCoupling::ReverbTap Implementation
//==============================================================================

void DrumRoomCoupling::ReverbTap::prepare(double sampleRate, float delayTime,
                                          float feedbackGain, float tapGain)
{
    int delaySamples = static_cast<int>(delayTime * sampleRate);
    delay.resize(delaySamples + 1, 0.0f);
    writeIndex = 0;
    feedback = juce::jlimit(0.0f, 0.95f, feedbackGain);
    gain = juce::jlimit(0.0f, 1.0f, tapGain);
}

float DrumRoomCoupling::ReverbTap::processSample(float input)
{
    // Read from delay line
    int readIndex = writeIndex - 1;
    if (readIndex < 0) readIndex += static_cast<int>(delay.size());

    float delayedSample = delay[readIndex];

    // Write input + feedback
    delay[writeIndex] = input + delayedSample * feedback;

    // Advance write index
    writeIndex++;
    if (writeIndex >= static_cast<int>(delay.size())) {
        writeIndex = 0;
    }

    return delayedSample * gain;
}

void DrumRoomCoupling::ReverbTap::reset()
{
    std::fill(delay.begin(), delay.end(), 0.0f);
    writeIndex = 0;
}

//==============================================================================
// DrumRoomCoupling Implementation
//==============================================================================

DrumRoomCoupling::DrumRoomCoupling()
{
    reverbTaps.resize(4);  // 4 parallel reverb taps
}

void DrumRoomCoupling::prepare(double sampleRate)
{
    sr = sampleRate;

    // Early reflections delay (short delay for room size)
    float earlyDelayTime = params.preDelayMs / 1000.0f;
    int earlyDelaySamples = static_cast<int>(earlyDelayTime * sampleRate);
    earlyReflectionDelay.resize(earlyDelaySamples + 1, 0.0f);
    writeIndex = 0;

    // Setup reverb taps with different delays
    float tapDelays[] = {0.03f, 0.05f, 0.07f, 0.11f};  // Seconds
    float tapFeedbacks[] = {0.5f, 0.4f, 0.3f, 0.2f};
    float tapGains[] = {0.3f, 0.2f, 0.15f, 0.1f};

    for (size_t i = 0; i < reverbTaps.size(); ++i) {
        reverbTaps[i].prepare(sampleRate, tapDelays[i],
                              tapFeedbacks[i] * params.reverbTime / 2.0f,
                              tapGains[i] * params.reflectionGain);
    }
}

void DrumRoomCoupling::reset()
{
    std::fill(earlyReflectionDelay.begin(), earlyReflectionDelay.end(), 0.0f);
    writeIndex = 0;

    for (auto& tap : reverbTaps) {
        tap.reset();
    }
}

float DrumRoomCoupling::processSample(float input)
{
    // Early reflections
    int readIndex = writeIndex - 1;
    if (readIndex < 0) readIndex += static_cast<int>(earlyReflectionDelay.size());

    float earlyReflection = earlyReflectionDelay[readIndex] * params.reflectionGain;
    earlyReflectionDelay[writeIndex] = input;
    writeIndex++;
    if (writeIndex >= static_cast<int>(earlyReflectionDelay.size())) {
        writeIndex = 0;
    }

    // Reverb tail
    float reverbTail = 0.0f;
    for (auto& tap : reverbTaps) {
        reverbTail += tap.processSample(input);
    }

    // Mix dry, early reflections, and reverb
    float roomMix = params.roomSize;
    return input * (1.0f - roomMix * 0.5f) +
           earlyReflection * roomMix +
           reverbTail * roomMix * 0.5f;
}

void DrumRoomCoupling::setParameters(const Parameters& p)
{
    params = p;
    // Re-initialize delays with new parameters
    prepare(sr);
}

//==============================================================================
// GiantDrumVoice Implementation
//==============================================================================

void GiantDrumVoice::prepare(double sampleRate)
{
    membrane.prepare(sampleRate);
    shell.prepare(sampleRate);
    nonlinear.prepare(sampleRate);
    room.prepare(sampleRate);
}

void GiantDrumVoice::reset()
{
    membrane.reset();
    shell.reset();
    nonlinear.reset();
    room.reset();
    active = false;
    velocity = 0.0f;
}

void GiantDrumVoice::trigger(int note, float vel, const GiantGestureParameters& gestureParam,
                             const GiantScaleParameters& scaleParams)
{
    midiNote = note;
    velocity = vel;
    gesture = gestureParam;
    scale = scaleParams;
    active = true;

    // Set membrane parameters based on scale
    MembraneResonator::Parameters memParams;
    memParams.fundamentalFrequency = 80.0f + (note - 36) * 10.0f;  // Map MIDI to frequency
    memParams.tension = 0.5f;
    memParams.diameterMeters = scaleParams.scaleMeters;
    memParams.damping = 0.995f + (1.0f - scaleParams.massBias) * 0.003f;
    memParams.inharmonicity = 0.1f;
    memParams.numModes = 4;
    membrane.setParameters(memParams);

    // Set shell parameters
    ShellResonator::Parameters shellParams;
    shellParams.cavityFrequency = 120.0f / scaleParams.scaleMeters;
    shellParams.shellFormant = 300.0f / scaleParams.scaleMeters;
    shellParams.cavityQ = 0.7f;
    shellParams.shellQ = 0.5f;
    shellParams.coupling = 0.3f;
    shell.setParameters(shellParams);

    // Set nonlinear parameters
    nonlinear.setSaturationAmount(0.1f);
    nonlinear.setMassEffect(scaleParams.massBias);

    // Set room parameters
    DrumRoomCoupling::Parameters roomParams;
    roomParams.roomSize = 0.7f;
    roomParams.reflectionGain = 0.3f;
    roomParams.reverbTime = 2.0f;
    roomParams.preDelayMs = 5.0f;
    room.setParameters(roomParams);

    // Strike the membrane
    membrane.strike(vel, gesture.force, gesture.contactArea);
}

float GiantDrumVoice::processSample()
{
    if (!active) {
        return 0.0f;
    }

    // Process membrane
    float membraneOut = membrane.processSample();

    // Feed energy to shell
    shell.processMembraneEnergy(membrane.getEnergy());

    // Process shell
    float shellOut = shell.processSample();

    // Mix membrane and shell
    float mixed = membraneOut * 0.7f + shellOut * 0.3f;

    // Apply nonlinear loss
    float processed = nonlinear.processSample(mixed, velocity);

    // Apply room coupling
    float output = room.processSample(processed);

    // Check if voice should deactivate
    if (membrane.getEnergy() < 0.0001f) {
        active = false;
    }

    return output;
}

bool GiantDrumVoice::isActive() const
{
    return active;
}

//==============================================================================
// GiantDrumVoiceManager Implementation
//==============================================================================

GiantDrumVoiceManager::GiantDrumVoiceManager()
{
}

void GiantDrumVoiceManager::prepare(double sampleRate, int maxVoices)
{
    currentSampleRate = sampleRate;

    // Allocate voices
    voices.resize(maxVoices);
    for (auto& voice : voices) {
        voice = std::make_unique<GiantDrumVoice>();
        voice->prepare(sampleRate);
    }
}

void GiantDrumVoiceManager::reset()
{
    for (auto& voice : voices) {
        voice->reset();
    }
}

GiantDrumVoice* GiantDrumVoiceManager::findFreeVoice()
{
    // First try to find completely inactive voice
    for (auto& voice : voices) {
        if (!voice->isActive()) {
            return voice.get();
        }
    }

    // If all active, find the one with lowest energy (voice stealing)
    GiantDrumVoice* quietest = voices[0].get();
    float minEnergy = quietest->membrane.getEnergy();

    for (auto& voice : voices) {
        float energy = voice->membrane.getEnergy();
        if (energy < minEnergy) {
            minEnergy = energy;
            quietest = voice.get();
        }
    }

    return quietest;
}

GiantDrumVoice* GiantDrumVoiceManager::findVoiceForNote(int note)
{
    // Find active voice for this note
    for (auto& voice : voices) {
        if (voice->isActive() && voice->midiNote == note) {
            return voice.get();
        }
    }
    return nullptr;
}

void GiantDrumVoiceManager::handleNoteOn(int note, float velocity,
                                         const GiantGestureParameters& gesture,
                                         const GiantScaleParameters& scale)
{
    GiantDrumVoice* voice = findFreeVoice();
    if (voice) {
        voice->trigger(note, velocity, gesture, scale);
    }
}

void GiantDrumVoiceManager::handleNoteOff(int note)
{
    // For drums, note off doesn't immediately stop the voice
    // Drums have natural decay, so we just let them decay naturally
    GiantDrumVoice* voice = findVoiceForNote(note);
    if (voice) {
        // Optionally reduce energy faster on note off
        // But for now, let natural decay happen
    }
}

void GiantDrumVoiceManager::allNotesOff()
{
    for (auto& voice : voices) {
        voice->reset();
    }
}

float GiantDrumVoiceManager::processSample()
{
    float output = 0.0f;

    for (auto& voice : voices) {
        output += voice->processSample();
    }

    // Soft limit to prevent clipping
    if (output > 1.0f) output = 1.0f;
    if (output < -1.0f) output = -1.0f;

    return output;
}

int GiantDrumVoiceManager::getActiveVoiceCount() const
{
    int count = 0;
    for (const auto& voice : voices) {
        if (voice->isActive()) {
            count++;
        }
    }
    return count;
}

void GiantDrumVoiceManager::setMembraneParameters(const MembraneResonator::Parameters& params)
{
    for (auto& voice : voices) {
        voice->membrane.setParameters(params);
    }
}

void GiantDrumVoiceManager::setShellParameters(const ShellResonator::Parameters& params)
{
    for (auto& voice : voices) {
        voice->shell.setParameters(params);
    }
}

void GiantDrumVoiceManager::setRoomParameters(const DrumRoomCoupling::Parameters& params)
{
    for (auto& voice : voices) {
        voice->room.setParameters(params);
    }
}

//==============================================================================
// AetherGiantDrumsPureDSP Implementation
//==============================================================================

AetherGiantDrumsPureDSP::AetherGiantDrumsPureDSP()
{
}

AetherGiantDrumsPureDSP::~AetherGiantDrumsPureDSP()
{
}

bool AetherGiantDrumsPureDSP::prepare(double sampleRate, int blockSize)
{
    sampleRate_ = sampleRate;
    blockSize_ = blockSize;

    voiceManager_.prepare(sampleRate, maxVoices_);

    // Initialize current scale and gesture parameters
    currentScale_.scaleMeters = params_.scaleMeters;
    currentScale_.massBias = params_.massBias;
    currentScale_.airLoss = params_.airLoss;
    currentScale_.transientSlowing = params_.transientSlowing;

    currentGesture_.force = params_.force;
    currentGesture_.speed = params_.speed;
    currentGesture_.contactArea = params_.contactArea;
    currentGesture_.roughness = params_.roughness;

    return true;
}

void AetherGiantDrumsPureDSP::reset()
{
    voiceManager_.reset();
}

void AetherGiantDrumsPureDSP::process(float** outputs, int numChannels, int numSamples)
{
    // Process samples
    for (int sample = 0; sample < numSamples; ++sample) {
        float mono = voiceManager_.processSample() * params_.masterVolume;

        // Process stereo
        processStereoSample(outputs[0][sample], outputs[1][sample]);

        // Mix in mono output
        outputs[0][sample] += mono;
        outputs[1][sample] += mono;
    }
}

void AetherGiantDrumsPureDSP::handleEvent(const ScheduledEvent& event)
{
    switch (event.type) {
        case ScheduledEvent::NOTE_ON: {
            voiceManager_.handleNoteOn(event.data.note.midiNote,
                                       event.data.note.velocity,
                                       currentGesture_,
                                       currentScale_);
            break;
        }
        case ScheduledEvent::NOTE_OFF: {
            voiceManager_.handleNoteOff(event.data.note.midiNote);
            break;
        }
        case ScheduledEvent::RESET: {
            reset();
            break;
        }
        case ScheduledEvent::PARAM_CHANGE: {
            setParameter(event.data.param.paramId, event.data.param.value);
            break;
        }
        default:
            break;
    }
}

float AetherGiantDrumsPureDSP::getParameter(const char* paramId) const
{
    // Membrane parameters
    if (std::strcmp(paramId, "membrane_tension") == 0)
        return params_.membraneTension;
    if (std::strcmp(paramId, "membrane_diameter") == 0)
        return params_.membraneDiameter;
    if (std::strcmp(paramId, "membrane_damping") == 0)
        return params_.membraneDamping;
    if (std::strcmp(paramId, "membrane_inharmonicity") == 0)
        return params_.membraneInharmonicity;

    // Shell parameters
    if (std::strcmp(paramId, "shell_cavity_freq") == 0)
        return params_.shellCavityFreq;
    if (std::strcmp(paramId, "shell_formant") == 0)
        return params_.shellFormant;
    if (std::strcmp(paramId, "shell_coupling") == 0)
        return params_.shellCoupling;

    // Nonlinear parameters
    if (std::strcmp(paramId, "saturation_amount") == 0)
        return params_.saturationAmount;
    if (std::strcmp(paramId, "mass_effect") == 0)
        return params_.massEffect;

    // Room parameters
    if (std::strcmp(paramId, "room_size") == 0)
        return params_.roomSize;
    if (std::strcmp(paramId, "reflection_gain") == 0)
        return params_.reflectionGain;
    if (std::strcmp(paramId, "reverb_time") == 0)
        return params_.reverbTime;

    // Giant parameters
    if (std::strcmp(paramId, "scale_meters") == 0)
        return params_.scaleMeters;
    if (std::strcmp(paramId, "mass_bias") == 0)
        return params_.massBias;
    if (std::strcmp(paramId, "air_loss") == 0)
        return params_.airLoss;
    if (std::strcmp(paramId, "transient_slowing") == 0)
        return params_.transientSlowing;

    // Gesture parameters
    if (std::strcmp(paramId, "force") == 0)
        return params_.force;
    if (std::strcmp(paramId, "speed") == 0)
        return params_.speed;
    if (std::strcmp(paramId, "contact_area") == 0)
        return params_.contactArea;
    if (std::strcmp(paramId, "roughness") == 0)
        return params_.roughness;

    // Global parameters
    if (std::strcmp(paramId, "master_volume") == 0)
        return params_.masterVolume;

    return 0.0f;
}

void AetherGiantDrumsPureDSP::setParameter(const char* paramId, float value)
{
    // Membrane parameters
    if (std::strcmp(paramId, "membrane_tension") == 0) {
        params_.membraneTension = value;
        applyParameters();
    } else if (std::strcmp(paramId, "membrane_diameter") == 0) {
        params_.membraneDiameter = value;
        applyParameters();
    } else if (std::strcmp(paramId, "membrane_damping") == 0) {
        params_.membraneDamping = value;
        applyParameters();
    } else if (std::strcmp(paramId, "membrane_inharmonicity") == 0) {
        params_.membraneInharmonicity = value;
        applyParameters();
    }
    // Shell parameters
    else if (std::strcmp(paramId, "shell_cavity_freq") == 0) {
        params_.shellCavityFreq = value;
        applyParameters();
    } else if (std::strcmp(paramId, "shell_formant") == 0) {
        params_.shellFormant = value;
        applyParameters();
    } else if (std::strcmp(paramId, "shell_coupling") == 0) {
        params_.shellCoupling = value;
        applyParameters();
    }
    // Nonlinear parameters
    else if (std::strcmp(paramId, "saturation_amount") == 0) {
        params_.saturationAmount = value;
    } else if (std::strcmp(paramId, "mass_effect") == 0) {
        params_.massEffect = value;
    }
    // Room parameters
    else if (std::strcmp(paramId, "room_size") == 0) {
        params_.roomSize = value;
        applyParameters();
    } else if (std::strcmp(paramId, "reflection_gain") == 0) {
        params_.reflectionGain = value;
        applyParameters();
    } else if (std::strcmp(paramId, "reverb_time") == 0) {
        params_.reverbTime = value;
        applyParameters();
    }
    // Giant parameters
    else if (std::strcmp(paramId, "scale_meters") == 0) {
        params_.scaleMeters = value;
        currentScale_.scaleMeters = value;
    } else if (std::strcmp(paramId, "mass_bias") == 0) {
        params_.massBias = value;
        currentScale_.massBias = value;
    } else if (std::strcmp(paramId, "air_loss") == 0) {
        params_.airLoss = value;
        currentScale_.airLoss = value;
    } else if (std::strcmp(paramId, "transient_slowing") == 0) {
        params_.transientSlowing = value;
        currentScale_.transientSlowing = value;
    }
    // Gesture parameters
    else if (std::strcmp(paramId, "force") == 0) {
        params_.force = value;
        currentGesture_.force = value;
    } else if (std::strcmp(paramId, "speed") == 0) {
        params_.speed = value;
        currentGesture_.speed = value;
    } else if (std::strcmp(paramId, "contact_area") == 0) {
        params_.contactArea = value;
        currentGesture_.contactArea = value;
    } else if (std::strcmp(paramId, "roughness") == 0) {
        params_.roughness = value;
        currentGesture_.roughness = value;
    }
    // Global parameters
    else if (std::strcmp(paramId, "master_volume") == 0) {
        params_.masterVolume = value;
    }
}

bool AetherGiantDrumsPureDSP::savePreset(char* jsonBuffer, int jsonBufferSize) const
{
    int offset = 0;

    // Write JSON opening
    const char* jsonStart = "{\n";
    int jsonStartLen = static_cast<int>(std::strlen(jsonStart));
    if (offset + jsonStartLen >= jsonBufferSize) return false;
    std::memcpy(jsonBuffer + offset, jsonStart, jsonStartLen);
    offset += jsonStartLen;

    // Write all parameters
    writeJsonParameter("membrane_tension", params_.membraneTension, jsonBuffer, offset, jsonBufferSize);
    writeJsonParameter("membrane_diameter", params_.membraneDiameter, jsonBuffer, offset, jsonBufferSize);
    writeJsonParameter("membrane_damping", params_.membraneDamping, jsonBuffer, offset, jsonBufferSize);
    writeJsonParameter("membrane_inharmonicity", params_.membraneInharmonicity, jsonBuffer, offset, jsonBufferSize);
    writeJsonParameter("shell_cavity_freq", params_.shellCavityFreq, jsonBuffer, offset, jsonBufferSize);
    writeJsonParameter("shell_formant", params_.shellFormant, jsonBuffer, offset, jsonBufferSize);
    writeJsonParameter("shell_coupling", params_.shellCoupling, jsonBuffer, offset, jsonBufferSize);
    writeJsonParameter("saturation_amount", params_.saturationAmount, jsonBuffer, offset, jsonBufferSize);
    writeJsonParameter("mass_effect", params_.massEffect, jsonBuffer, offset, jsonBufferSize);
    writeJsonParameter("room_size", params_.roomSize, jsonBuffer, offset, jsonBufferSize);
    writeJsonParameter("reflection_gain", params_.reflectionGain, jsonBuffer, offset, jsonBufferSize);
    writeJsonParameter("reverb_time", params_.reverbTime, jsonBuffer, offset, jsonBufferSize);
    writeJsonParameter("scale_meters", params_.scaleMeters, jsonBuffer, offset, jsonBufferSize);
    writeJsonParameter("mass_bias", params_.massBias, jsonBuffer, offset, jsonBufferSize);
    writeJsonParameter("air_loss", params_.airLoss, jsonBuffer, offset, jsonBufferSize);
    writeJsonParameter("transient_slowing", params_.transientSlowing, jsonBuffer, offset, jsonBufferSize);
    writeJsonParameter("force", params_.force, jsonBuffer, offset, jsonBufferSize);
    writeJsonParameter("speed", params_.speed, jsonBuffer, offset, jsonBufferSize);
    writeJsonParameter("contact_area", params_.contactArea, jsonBuffer, offset, jsonBufferSize);
    writeJsonParameter("roughness", params_.roughness, jsonBuffer, offset, jsonBufferSize);
    writeJsonParameter("master_volume", params_.masterVolume, jsonBuffer, offset, jsonBufferSize);

    // Write JSON closing (remove trailing comma)
    if (offset > 2) {
        offset -= 2;  // Remove ",\n"
        const char* jsonEnd = "\n}\n";
        int jsonEndLen = static_cast<int>(std::strlen(jsonEnd));
        if (offset + jsonEndLen >= jsonBufferSize) return false;
        std::memcpy(jsonBuffer + offset, jsonEnd, jsonEndLen);
    }

    return true;
}

bool AetherGiantDrumsPureDSP::loadPreset(const char* jsonData)
{
    double value;

    if (parseJsonParameter(jsonData, "membrane_tension", value))
        params_.membraneTension = static_cast<float>(value);
    if (parseJsonParameter(jsonData, "membrane_diameter", value))
        params_.membraneDiameter = static_cast<float>(value);
    if (parseJsonParameter(jsonData, "membrane_damping", value))
        params_.membraneDamping = static_cast<float>(value);
    if (parseJsonParameter(jsonData, "membrane_inharmonicity", value))
        params_.membraneInharmonicity = static_cast<float>(value);
    if (parseJsonParameter(jsonData, "shell_cavity_freq", value))
        params_.shellCavityFreq = static_cast<float>(value);
    if (parseJsonParameter(jsonData, "shell_formant", value))
        params_.shellFormant = static_cast<float>(value);
    if (parseJsonParameter(jsonData, "shell_coupling", value))
        params_.shellCoupling = static_cast<float>(value);
    if (parseJsonParameter(jsonData, "saturation_amount", value))
        params_.saturationAmount = static_cast<float>(value);
    if (parseJsonParameter(jsonData, "mass_effect", value))
        params_.massEffect = static_cast<float>(value);
    if (parseJsonParameter(jsonData, "room_size", value))
        params_.roomSize = static_cast<float>(value);
    if (parseJsonParameter(jsonData, "reflection_gain", value))
        params_.reflectionGain = static_cast<float>(value);
    if (parseJsonParameter(jsonData, "reverb_time", value))
        params_.reverbTime = static_cast<float>(value);
    if (parseJsonParameter(jsonData, "scale_meters", value))
        params_.scaleMeters = static_cast<float>(value);
    if (parseJsonParameter(jsonData, "mass_bias", value))
        params_.massBias = static_cast<float>(value);
    if (parseJsonParameter(jsonData, "air_loss", value))
        params_.airLoss = static_cast<float>(value);
    if (parseJsonParameter(jsonData, "transient_slowing", value))
        params_.transientSlowing = static_cast<float>(value);
    if (parseJsonParameter(jsonData, "force", value))
        params_.force = static_cast<float>(value);
    if (parseJsonParameter(jsonData, "speed", value))
        params_.speed = static_cast<float>(value);
    if (parseJsonParameter(jsonData, "contact_area", value))
        params_.contactArea = static_cast<float>(value);
    if (parseJsonParameter(jsonData, "roughness", value))
        params_.roughness = static_cast<float>(value);
    if (parseJsonParameter(jsonData, "master_volume", value))
        params_.masterVolume = static_cast<float>(value);

    applyParameters();

    return true;
}

int AetherGiantDrumsPureDSP::getActiveVoiceCount() const
{
    return voiceManager_.getActiveVoiceCount();
}

void AetherGiantDrumsPureDSP::applyParameters()
{
    // Apply membrane parameters
    MembraneResonator::Parameters memParams;
    memParams.fundamentalFrequency = 80.0f;
    memParams.tension = params_.membraneTension;
    memParams.diameterMeters = params_.membraneDiameter;
    memParams.damping = params_.membraneDamping;
    memParams.inharmonicity = params_.membraneInharmonicity;
    memParams.numModes = params_.membraneNumModes;
    voiceManager_.setMembraneParameters(memParams);

    // Apply shell parameters
    ShellResonator::Parameters shellParams;
    shellParams.cavityFrequency = params_.shellCavityFreq;
    shellParams.shellFormant = params_.shellFormant;
    shellParams.coupling = params_.shellCoupling;
    voiceManager_.setShellParameters(shellParams);

    // Apply room parameters
    DrumRoomCoupling::Parameters roomParams;
    roomParams.roomSize = params_.roomSize;
    roomParams.reflectionGain = params_.reflectionGain;
    roomParams.reverbTime = params_.reverbTime;
    roomParams.preDelayMs = 5.0f;
    voiceManager_.setRoomParameters(roomParams);
}

void AetherGiantDrumsPureDSP::processStereoSample(float& left, float& right)
{
    // Currently mono output, but could add stereo enhancement here
    // For now, just ensure both channels get the same signal
    // (The actual mixing happens in process())
}

float AetherGiantDrumsPureDSP::calculateFrequency(int midiNote) const
{
    // Use LookupTables for MIDI to frequency conversion
    return SchillingerEcosystem::DSP::LookupTables::getInstance().midiToFreq(static_cast<float>(midiNote));
}

bool AetherGiantDrumsPureDSP::writeJsonParameter(const char* name, double value,
                                                  char* buffer, int& offset, int bufferSize) const
{
    char tempBuffer[256];
    int len = std::snprintf(tempBuffer, sizeof(tempBuffer),
                           "  \"%s\": %.6g,\n", name, value);

    if (offset + len >= bufferSize) {
        return false;
    }

    std::memcpy(buffer + offset, tempBuffer, len);
    offset += len;

    return true;
}

bool AetherGiantDrumsPureDSP::parseJsonParameter(const char* json, const char* param, double& value) const
{
    // Simple JSON parser (looks for "param": value)
    char searchPattern[256];
    std::snprintf(searchPattern, sizeof(searchPattern), "\"%s\":", param);

    const char* found = std::strstr(json, searchPattern);
    if (!found) {
        return false;
    }

    found += std::strlen(searchPattern);

    // Skip whitespace
    while (*found == ' ' || *found == '\t' || *found == '\n') {
        found++;
    }

    // Parse value
    value = std::atof(found);

    return true;
}

//==============================================================================
// Factory Registration
//==============================================================================

// Factory registration disabled for plugin builds
/*
namespace {
    struct AetherGiantDrumsRegistrar {
        AetherGiantDrumsRegistrar() {
            registerInstrumentFactory("AetherGiantDrums", []() -> InstrumentDSP* {
                return new AetherGiantDrumsPureDSP();
            });
        }
    } registrar;
}
*/

}  // namespace DSP
