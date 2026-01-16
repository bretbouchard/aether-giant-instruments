/*
  ==============================================================================

   AetherGiantVoiceDSP.h
   Giant Voice / Roar Engine (Mythic Vocal Synthesis)

   NOT speech synthesis - this is mythic/animal/colossal vocal engine:
   - Turbulent excitation (breath/growl)
   - Nonlinear vocal fold oscillator (with chaos at high pressure)
   - Giant formant cavities (3-5 bandpass filters)
   - Subharmonic generator (octave/fifth down, unstable)
   - Chest/body resonator (modal or waveguide)
   - Distance/air absorption

   Preset archetypes:
   - Colossus Roar (unstable pitch, subharmonics, wide formants)
   - Titan Growl (distorted folds, strong mid formants, aggression)
   - Ancient Chant (slow pitch, drifting formants, ritual)
   - Beast Bark (short envelope, sharp transient, punctuation)
   - World Breath (no pitch, massive filtered noise, tension beds)

  ==============================================================================
*/

#pragma once

#include "AetherGiantBase.h"
#include "dsp/FastRNG.h"
#include "dsp/InstrumentDSP.h"
#include <juce_dsp/juce_dsp.h>
#include <vector>
#include <array>
#include <memory>
#include <cmath>

namespace DSP {

//==============================================================================
/**
 * Giant Voice gesture parameters
 *
 * Specialized for voice synthesis:
 * - force: Diaphragm pressure (breath support)
 * - aggression: Vocal intensity (growl, distortion)
 * - openness: Mouth aperture (vowel space)
 * - roughness: Vocal texture (breathiness, turbulence)
 */
struct GiantVoiceGesture : public GiantGestureParameters
{
    float aggression = 0.5f;      // Vocal intensity (0.0 = calm, 1.0 = screaming)
    float openness = 0.5f;        // Mouth aperture (0.0 = closed, 1.0 = wide open)

    GiantVoiceGesture()
    {
        force = 0.6f;
        speed = 0.2f;
        contactArea = 0.5f;
        roughness = 0.3f;
    }
};

//==============================================================================
/**
 * Breath/pressure generator for giant voice
 *
 * Models:
 * - Slow attack pressure ramp (giant lungs)
 * - Pressure overshoot (initial burst)
 * - Turbulence noise proportional to force
 * - Sustained pressure with natural variation
 */
class BreathPressureGenerator
{
public:
    struct Parameters
{
        float attackTime = 0.1f;       // Pressure attack (seconds)
        float sustainLevel = 0.7f;     // Sustained pressure (0.0 - 1.0)
        float releaseTime = 0.3f;      // Pressure release (seconds)
        float turbulenceAmount = 0.2f; // Noise turbulence (0.0 - 1.0)
        float pressureOvershoot = 0.2f;// Initial overshoot (0.0 - 1.0)
    };

    BreathPressureGenerator();
    ~BreathPressureGenerator() = default;

    void prepare(double sampleRate);
    void reset();

    /** Trigger pressure envelope
        @param velocity    Note velocity (affects pressure level)
        @param force       Diaphragm force (affects max pressure)
        @param aggression  Aggression (affects turbulence) */
    void trigger(float velocity, float force, float aggression);

    /** Release pressure
        @param damping    If true, fast damping release */
    void release(bool damping = false);

    /** Process pressure generator
        @returns    Pressure signal (0.0 - 1.0) plus turbulence */
    float processSample();

    /** Get pure pressure (without turbulence) */
    float getPressure() const { return currentPressure; }

    void setParameters(const Parameters& p);

    bool isActive() const { return active; }

private:
    Parameters params;

    float currentPressure = 0.0f;
    float targetPressure = 0.0f;
    float envelopePhase = 0.0f;  // 0 = attack, 1 = sustain, 2 = release

    bool active = false;
    bool inOvershoot = false;

    double sr = 48000.0;

    // Random number generator for turbulence
    FastRNG rng;

    float calculateAttackCoefficient() const;
    float calculateReleaseCoefficient() const;
};

//==============================================================================
/**
 * Vocal fold oscillator (nonlinear)
 *
 * Models:
 * - Saw/pulse hybrid waveform
 * - Chaos introduced at high pressure
 * - Optional pitch control (roars don't need fixed pitch)
 * - Jitter/instability for realism
 */
class VocalFoldOscillator
{
public:
    enum class PitchMode
    {
        Unstable,    // Unstable pitch (roars, growls)
        Locked,      // Locked to MIDI pitch
        None         // No pitch (breath, noise only)
    };

    struct Parameters
    {
        float frequency = 100.0f;         // Fundamental frequency (Hz)
        float pitchInstability = 0.3f;    // Pitch random variation (0.0 - 1.0)
        float chaosAmount = 0.2f;         // Chaos at high pressure (0.0 - 1.0)
        float waveformMorph = 0.5f;       // Saw (0.0) to pulse (1.0)
        float subharmonicMix = 0.3f;      // Subharmonic content (0.0 - 1.0)
        PitchMode pitchMode = PitchMode::Unstable;
    };

    VocalFoldOscillator();
    ~VocalFoldOscillator() = default;

    void prepare(double sampleRate);
    void reset();

    /** Process oscillator
        @param pressure    Breath pressure (affects chaos)
        @returns          Glottal waveform */
    float processSample(float pressure);

    void setParameters(const Parameters& p);
    Parameters getParameters() const { return params; }

    void setFrequency(float freq);
    void setPitchMode(PitchMode mode);

private:
    Parameters params;

    float phase = 0.0f;
    float subPhase = 0.0f;  // Subharmonic phase

    // Random for instability
    FastRNG rng;

    double sr = 48000.0;

    float calculateInstantaneousFrequency(float pressure) const;
    float generateWaveform(float phase, float morph) const;
};

//==============================================================================
/**
 * Formant filter (giant scale)
 *
 * Single formant bandpass filter with:
 * - Giant bandwidth (huge cavities)
 * - Lower frequency (larger space)
 * - Smooth modulation
 */
class GiantFormantFilter
{
public:
    GiantFormantFilter();
    ~GiantFormantFilter() = default;

    void prepare(double sampleRate);
    void reset();

    /** Process formant filter
        @param input    Input signal
        @returns       Filtered signal */
    float processSample(float input);

    /** Set formant frequency
        @param freq    Formant center frequency (Hz) */
    void setFrequency(float freq);

    /** Set formant bandwidth
        @param bw    Bandwidth in octaves (0.5 = narrow, 2.0 = very wide) */
    void setBandwidth(float bw);

    /** Set formant bandwidth in Hz (more intuitive for speech synthesis)
        @param bwHz    Bandwidth in Hz (typically 50-150 Hz) */
    void setBandwidthHz(float bwHz);

    /** Set formant bandwidth using Q factor
        @param q    Q factor (frequency / bandwidth) */
    void setQ(float q);

    /** Set formant amplitude
        @param amp    Amplitude (0.0 - 1.0) */
    void setAmplitude(float amp);

    float getFrequency() const { return frequency; }
    float getBandwidth() const { return bandwidth; }

private:
    float frequency = 500.0f;
    float bandwidth = 1.0f;
    float amplitude = 1.0f;

    // Bandpass filter (biquad)
    float b0 = 0.0f, b1 = 0.0f, b2 = 0.0f;
    float a1 = 0.0f, a2 = 0.0f;
    float x1 = 0.0f, x2 = 0.0f;
    float y1 = 0.0f, y2 = 0.0f;

    double sr = 48000.0;
    bool coefficientsDirty = true;

    void calculateCoefficients();
};

//==============================================================================
/**
 * Formant stack (3-5 giant formants)
 *
 * Models giant vocal tract:
 * - 3-5 formant filters
 * - Frequencies scaled way down
 * - Wide bandwidths (huge cavities)
 * - Drifting formants for chant effects
 */
class FormantStack
{
public:
    enum class VowelShape
    {
        Ah,        // Ah (wide open, lowest F1)
        Eh,        // Eh (mid openness)
        Ee,        // Ee (high F2, "beet")
        Oh,        // Oh (rounded, low F2)
        Oo,        // Oo (closed, lowest F1 and F2)
        Uh,        // Uh (mid-rounded)
        Ih,        // Ih (high F2, "bit")
        Custom     // User-defined
    };

    struct Parameters
    {
        VowelShape vowelShape = VowelShape::Ah;
        float formantDrift = 0.1f;       // Formant drift speed (0.0 - 1.0)
        float openness = 0.5f;           // Mouth openness (0.0 - 1.0)
        float giantScale = 0.6f;         // Scale factor (1.0 = human, 0.6 = giant)

        // Custom formant frequencies (when vowelShape = Custom)
        float f1 = 600.0f;   // First formant (Hz)
        float f2 = 1200.0f;  // Second formant (Hz)
        float f3 = 2500.0f;  // Third formant (Hz)
        float f4 = 3500.0f;  // Fourth formant (Hz, optional)
        float f5 = 4500.0f;  // Fifth formant (Hz, optional)
    };

    FormantStack();
    ~FormantStack() = default;

    void prepare(double sampleRate);
    void reset();

    /** Process formant stack
        @param input    Glottal source
        @returns       Formant-filtered output */
    float processSample(float input);

    void setParameters(const Parameters& p);
    Parameters getParameters() const { return params; }

    /** Set vowel shape directly */
    void setVowelShape(VowelShape shape, float openness = 0.5f);

private:
    Parameters params;

    std::vector<GiantFormantFilter> formants;

    // Formant drift state
    float driftPhase = 0.0f;
    float baseF1 = 600.0f;
    float baseF2 = 1200.0f;
    float baseF3 = 2500.0f;
    float baseF4 = 3500.0f;

    double sr = 48000.0;

    void updateFormantFrequencies();
    void initializeVowel(VowelShape shape, float openness);
    int getVowelIndex(VowelShape shape) const;
};

//==============================================================================
/**
 * Subharmonic generator
 *
 * Adds:
 * - Octave-down component
 * - Fifth-down component
 * - Unstable tracking (intentional)
 * - Creates "weight" and "body"
 */
class SubharmonicGenerator
{
public:
    struct Parameters
    {
        float octaveMix = 0.3f;      // Octave down level (0.0 - 1.0)
        float fifthMix = 0.2f;       // Fifth down level (0.0 - 1.0)
        float instability = 0.3f;    // Tracking instability (0.0 - 1.0)
    };

    SubharmonicGenerator();
    ~SubharmonicGenerator() = default;

    void prepare(double sampleRate);
    void reset();

    /** Process with subharmonics
        @param input        Input signal
        @param fundamental  Fundamental frequency (Hz)
        @returns           Input plus subharmonics */
    float processSample(float input, float fundamental);

    void setParameters(const Parameters& p);

private:
    Parameters params;

    // Subharmonic oscillators
    float octavePhase = 0.0f;
    float fifthPhase = 0.0f;

    // Instability
    float currentOctaveShift = 1.0f;
    float currentFifthShift = 1.0f;

    FastRNG rng;

    double sr = 48000.0;

    void updateInstability();
};

//==============================================================================
/**
 * Chest/body resonator
 *
 * Adds body and intimidation:
 * - Modal resonator (chest cavity)
 * - Low-frequency emphasis
 * - Creates "size" perception
 */
class ChestResonator
{
public:
    struct Parameters
    {
        float chestFrequency = 80.0f;   // Chest resonance (Hz)
        float chestResonance = 0.7f;    // Q factor (0.0 - 1.0)
        float bodySize = 0.5f;          // Body size (0.0 = small, 1.0 = massive)
    };

    ChestResonator();
    ~ChestResonator() = default;

    void prepare(double sampleRate);
    void reset();

    /** Process with chest resonance
        @param input    Input signal
        @returns       Output with chest resonance */
    float processSample(float input);

    void setParameters(const Parameters& p);

private:
    Parameters params;

    // Modal filter for chest
    struct ChestMode
    {
        float frequency = 80.0f;
        float amplitude = 0.0f;
        float phase = 0.0f;
        float decay = 0.995f;

        void prepare(double sampleRate, float resonance);
        float processSample(float excitation);
        void reset();

    private:
        double sr = 48000.0;
    };

    ChestMode chestMode;
    float lowpassState = 0.0f;

    double sr = 48000.0;

    float calculateLowpassCoefficient(float bodySize) const;
};

//==============================================================================
/**
 * Single giant voice
 */
struct GiantVoice
{
    int midiNote = -1;
    float velocity = 0.0f;
    bool active = false;

    // DSP components
    BreathPressureGenerator breath;
    VocalFoldOscillator vocalFolds;
    FormantStack formants;
    SubharmonicGenerator subharmonics;
    ChestResonator chest;

    // Giant parameters
    GiantScaleParameters scale;
    GiantVoiceGesture gesture;

    void prepare(double sampleRate);
    void reset();
    void trigger(int note, float vel, const GiantVoiceGesture& gesture,
                 const GiantScaleParameters& scale);
    void release(bool damping = false);
    float processSample();
    bool isActive() const;
};

//==============================================================================
/**
 * Giant Voice manager
 *
 * Manages polyphonic voices (typically 4-8 for giant voice).
 */
class GiantVoiceManager
{
public:
    GiantVoiceManager();
    ~GiantVoiceManager() = default;

    void prepare(double sampleRate, int maxVoices = 8);
    void reset();

    GiantVoice* findFreeVoice();
    GiantVoice* findVoiceForNote(int note);

    void handleNoteOn(int note, float velocity, const GiantVoiceGesture& gesture,
                      const GiantScaleParameters& scale);
    void handleNoteOff(int note, bool damping = false);
    void allNotesOff();

    float processSample();
    int getActiveVoiceCount() const;

    void setFormantParameters(const FormantStack::Parameters& params);
    void setSubharmonicParameters(const SubharmonicGenerator::Parameters& params);
    void setChestParameters(const ChestResonator::Parameters& params);

private:
    std::vector<std::unique_ptr<GiantVoice>> voices;
    double currentSampleRate = 48000.0;
};

//==============================================================================
/**
 * Main Aether Giant Voice Pure DSP Instrument
 */
class AetherGiantVoicePureDSP : public InstrumentDSP
{
public:
    AetherGiantVoicePureDSP();
    ~AetherGiantVoicePureDSP() override;

    //==============================================================================
    // InstrumentDSP interface
    bool prepare(double sampleRate, int blockSize) override;
    void reset() override;
    void process(float** outputs, int numChannels, int numSamples) override;
    void handleEvent(const ScheduledEvent& event) override;

    float getParameter(const char* paramId) const override;
    void setParameter(const char* paramId, float value) override;

    bool savePreset(char* jsonBuffer, int jsonBufferSize) const override;
    bool loadPreset(const char* jsonData) override;

    int getActiveVoiceCount() const override;
    int getMaxPolyphony() const override { return maxVoices_; }

    const char* getInstrumentName() const override { return "AetherGiantVoice"; }
    const char* getInstrumentVersion() const override { return "1.0.0"; }

private:
    //==============================================================================
    GiantVoiceManager voiceManager_;

    struct Parameters
    {
        // Breath/pressure
        float breathAttack = 0.1f;
        float breathSustain = 0.7f;
        float breathRelease = 0.3f;
        float turbulence = 0.2f;

        // Vocal folds
        float pitchInstability = 0.3f;
        float chaosAmount = 0.2f;
        float waveformMorph = 0.5f;
        float subharmonicMix = 0.3f;

        // Formants
        float vowelOpenness = 0.5f;
        float formantDrift = 0.1f;

        // Chest
        float chestFrequency = 80.0f;
        float chestResonance = 0.7f;
        float bodySize = 0.5f;

        // Structure (Mutable Instruments-style harmonic complexity)
        // 0.0 = simple, pure vowel (clear formants, stable pitch)
        // 0.5 = balanced (default)
        // 1.0 = complex, rich vocal (wide formant spacing, subharmonics, instability)
        float structure = 0.5f;

        // Giant
        float scaleMeters = 8.0f;
        float massBias = 0.8f;
        float airLoss = 0.5f;
        float transientSlowing = 0.7f;

        // Gesture
        float force = 0.6f;
        float aggression = 0.7f;
        float openness = 0.5f;
        float roughness = 0.6f;

        // Global
        float masterVolume = 0.8f;

    } params_;

    double sampleRate_ = 48000.0;
    int blockSize_ = 512;
    int maxVoices_ = 8;

    // Current giant state
    GiantScaleParameters currentScale_;
    GiantVoiceGesture currentGesture_;

    void applyParameters();
    void processStereoSample(float& left, float& right);
    float calculateFrequency(int midiNote) const;

    // Preset serialization
    bool writeJsonParameter(const char* name, double value, char* buffer,
                            int& offset, int bufferSize) const;
    bool parseJsonParameter(const char* json, const char* param, double& value) const;
};

}  // namespace DSP
