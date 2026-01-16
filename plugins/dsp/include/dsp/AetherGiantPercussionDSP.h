/*
  ==============================================================================

   AetherGiantPercussionDSP.h
   Giant Percussion Synthesizer (Modal Resonator Bank)

   Physical modeling of giant-scale percussion using modal synthesis:
   - Modal resonator bank (8-64 modes for gongs/bells/plates)
   - Nonlinear dispersion (inharmonicity)
   - Damping model (size-scaled decay times)
   - Strike/scrape excitation
   - Stereo radiation patterns

   Preset archetypes:
   - World Gong (Titan) - very long decay, complex swirl
   - Cathedral Bell - clear strike + endless bloom
   - Stone Plate - wide, ominous
   - Mythic Anvil - shorter, brutal, huge transient
   - Fog Chimes - randomized micro-strikes/shimmer

   Instrument types:
   - Gongs (flat, suspended metal discs)
   - Bells (cast, tuned metal)
   - Plates (stone, metal slabs)
   - Chimes (tuned bars)

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
 * Single mode in modal resonator using State Variable Filter
 *
 * Each mode represents a vibrational mode of the object using a 2nd-order
 * resonant filter (SVF) instead of a simple sine oscillator. This provides:
 * - More realistic metallic timbres
 * - Natural frequency-dependent damping
 * - Better transient response
 * - Compatibility with physical modeling principles from Mutable Instruments
 *
 * Based on the modal synthesis approach from Elements/Rings:
 * https://github.com/pichenettes/eurorack/tree/master/rings
 */
struct ModalResonatorMode
{
    float frequency = 440.0f;       // Mode frequency (Hz)
    float Q = 10.0f;                 // Resonance (determines decay time)
    float amplitude = 0.0f;         // Current amplitude (energy)
    float initialAmplitude = 1.0f;   // Starting amplitude (for strike)
    float decay = 0.995f;           // Global decay multiplier

    // State Variable Filter (TPT topology - normalized ladder)
    juce::dsp::StateVariableTPTFilter<float> svf;

    double sampleRate = 48000.0;

    void prepare(double sr);
    float processSample(float input);  // Now takes input excitation
    void excite(float energy);
    void reset();
};

//==============================================================================
/**
 * Modal resonator bank
 *
 * Models vibrating objects (gongs, bells, plates) using
 * superposition of multiple vibrational modes.
 */
class ModalResonatorBank
{
public:
    enum class InstrumentType
    {
        Gong,        // Flat metal disc, inharmonic
        Bell,        // Cast metal, harmonic
        Plate,       // Stone/metal slab, complex
        Chime,       // Tuned bar, harmonic
        Bowl,        // Singing bowl, harmonic+
        Custom       // User-defined mode frequencies
    };

    struct Parameters
    {
        InstrumentType instrumentType = InstrumentType::Gong;
        float sizeMeters = 1.0f;          // Physical size (affects pitch, decay)
        float thickness = 0.5f;            // Thickness (affects inharmonicity)
        float materialHardness = 0.7f;     // Material (0.0 = soft/wood, 1.0 = hard/metal)
        float damping = 0.5f;              // Global damping multiplier
        int numModes = 16;                 // Number of modes (8-64)
        float inharmonicity = 0.3f;        // Frequency spread
        // Structure (Mutable Instruments-style harmonic complexity)
        // 0.0 = harmonic, bell-like (clean modes, consonant overtones)
        // 0.5 = balanced (default)
        // 1.0 = inharmonic, metallic (dissonant mode spread, complex decay)
        float structure = 0.5f;
    };

    ModalResonatorBank();
    ~ModalResonatorBank() = default;

    void prepare(double sampleRate);
    void reset();

    /** Strike the resonator
        @param velocity    Strike velocity (0.0 - 1.0)
        @param force       Strike force (affects initial energy)
        @param contactArea Size of striking surface */
    void strike(float velocity, float force, float contactArea);

    /** Scrape the resonator (continuous excitation)
        @param intensity    Scrape intensity (0.0 - 1.0)
        @param roughness    Surface texture */
    void scrape(float intensity, float roughness);

    /** Process modal bank
        @returns    Summed output from all modes */
    float processSample();

    void setParameters(const Parameters& p);
    Parameters getParameters() const { return params; }

    /** Get total energy (for decay detection) */
    float getTotalEnergy() const;

private:
    Parameters params;
    std::vector<ModalResonatorMode> modes;

    double sr = 48000.0;
    float scrapeEnergy = 0.0f;

    void initializeModes();
    void initializeGongModes();
    void initializeBellModes();
    void initializePlateModes();
    void initializeChimeModes();
    void initializeBowlModes();

    float calculateDecay(float baseDecay, float frequency, float size);
};

//==============================================================================
/**
 * Strike exciter
 *
 * Models the initial transient when striking a percussion instrument:
 * - Click transient (controllable)
 * - Felt/wood mallet noise layer
 * - Force/speed/contactArea/roughness from gesture
 */
class StrikeExciter
{
public:
    enum class MalletType
    {
        Soft,     // Felt, soft attack
        Medium,   // Rubber, balanced
        Hard,     // Wood/hard rubber, sharp attack
        Metal     // Metal beater, very sharp
    };

    struct Parameters
    {
        MalletType malletType = MalletType::Medium;
        float clickAmount = 0.3f;        // Transient click level
        float noiseAmount = 0.2f;        // Mallet noise level
        float brightness = 0.5f;         // High-frequency content
    };

    StrikeExciter();
    ~StrikeExciter() = default;

    void prepare(double sampleRate);
    void reset();

    /** Generate strike excitation
        @param velocity    Strike velocity
        @param force       Strike force
        @param contactArea Size of striking surface
        @param roughness   Surface texture
        @returns           Excitation signal */
    float processSample(float velocity, float force, float contactArea, float roughness);

    void setParameters(const Parameters& p);

private:
    Parameters params;

    // Click transient
    float clickPhase = 0.0f;
    float clickDecay = 0.0f;

    // Noise layer
    FastRNG rng;

    double sr = 48000.0;

    float generateClick();
    float generateNoise(float roughness);
};

//==============================================================================
/**
 * Nonlinear dispersion
 *
 * Models inharmonicity in metal percussion:
 * - High frequencies travel at different speeds
 * - Creates metallic "shimmer"
 * - More pronounced in larger/thinner objects
 */
class NonlinearDispersion
{
public:
    NonlinearDispersion();
    ~NonlinearDispersion() = default;

    void prepare(double sampleRate);
    void reset();

    /** Apply dispersion to input
        @param input            Dry signal
        @param inharmonicity   Amount of dispersion (0.0 - 1.0)
        @returns               Dispersed signal */
    float processSample(float input, float inharmonicity);

    void setInharmonicity(float amount);

private:
    // Allpass filters for phase distortion
    std::vector<float> allpassDelays;
    std::vector<int> delaySizes;
    int writeIndex = 0;

    double sr = 48000.0;
    float inharmonicity = 0.3f;

    void initializeDelays();
};

//==============================================================================
/**
 * Stereo radiation pattern
 *
 * Models how sound radiates from the object:
 * - Directional high frequencies
 * - Omnidirectional low frequencies
 * - Creates stereo width
 */
class StereoRadiationPattern
{
public:
    struct Parameters
    {
        float width = 0.5f;             // Stereo width (0.0 = mono, 1.0 = wide)
        float highFrequencyDirectionality = 0.7f;  // HF directionality
        float rotation = 0.0f;          // Stereo rotation (0.0 - 1.0)
    };

    StereoRadiationPattern();
    ~StereoRadiationPattern() = default;

    void prepare(double sampleRate);
    void reset();

    /** Process stereo radiation
        @param input    Mono input
        @param left     Left output
        @param right    Right output */
    void processSample(float input, float& left, float& right);

    void setParameters(const Parameters& p);

private:
    Parameters params;

    // Simple filters for frequency-dependent panning
    float hfLeft = 0.0f;
    float hfRight = 0.0f;
    float lfLeft = 0.0f;
    float lfRight = 0.0f;

    double sr = 48000.0;

    void calculatePanGains(float frequency, float& leftGain, float& rightGain);
};

//==============================================================================
/**
 * Single giant percussion voice
 */
struct GiantPercussionVoice
{
    int midiNote = -1;
    float velocity = 0.0f;
    bool active = false;

    // DSP components
    ModalResonatorBank resonator;
    StrikeExciter exciter;
    NonlinearDispersion dispersion;
    StereoRadiationPattern radiation;

    // Giant parameters
    GiantScaleParameters scale;
    GiantGestureParameters gesture;

    void prepare(double sampleRate);
    void reset();
    void trigger(int note, float vel, const GiantGestureParameters& gesture,
                 const GiantScaleParameters& scale);
    float processSample(float& left, float& right);
    bool isActive() const;
};

//==============================================================================
/**
 * Giant Percussion voice manager
 *
 * Manages polyphonic percussion voices (typically 16-32 voices).
 */
class GiantPercussionVoiceManager
{
public:
    GiantPercussionVoiceManager();
    ~GiantPercussionVoiceManager() = default;

    void prepare(double sampleRate, int maxVoices = 24);
    void reset();

    GiantPercussionVoice* findFreeVoice();
    GiantPercussionVoice* findVoiceForNote(int note);

    void handleNoteOn(int note, float velocity, const GiantGestureParameters& gesture,
                      const GiantScaleParameters& scale);
    void handleNoteOff(int note);
    void allNotesOff();

    void processSample(float& left, float& right);
    int getActiveVoiceCount() const;

    void setResonatorParameters(const ModalResonatorBank::Parameters& params);
    void setExciterParameters(const StrikeExciter::Parameters& params);
    void setRadiationParameters(const StereoRadiationPattern::Parameters& params);

private:
    std::vector<std::unique_ptr<GiantPercussionVoice>> voices;
    double currentSampleRate = 48000.0;
};

//==============================================================================
/**
 * Main Aether Giant Percussion Pure DSP Instrument
 */
class AetherGiantPercussionPureDSP : public InstrumentDSP
{
public:
    AetherGiantPercussionPureDSP();
    ~AetherGiantPercussionPureDSP() override;

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

    const char* getInstrumentName() const override { return "AetherGiantPercussion"; }
    const char* getInstrumentVersion() const override { return "1.0.0"; }

private:
    //==============================================================================
    GiantPercussionVoiceManager voiceManager_;

    struct Parameters
    {
        // Resonator
        float instrumentType = 0.0f;   // 0 = gong, 1 = bell, 2 = plate, 3 = chime
        float sizeMeters = 2.0f;
        float thickness = 0.5f;
        float materialHardness = 0.8f;
        float damping = 0.5f;
        float numModes = 16.0f;
        float inharmonicity = 0.4f;
        // Structure (Mutable Instruments-style harmonic complexity)
        // 0.0 = harmonic, bell-like (clean modes, consonant overtones)
        // 0.5 = balanced (default)
        // 1.0 = inharmonic, metallic (dissonant mode spread, complex decay)
        float structure = 0.5f;

        // Exciter
        float malletType = 1.0f;        // 0 = soft, 1 = medium, 2 = hard, 3 = metal
        float clickAmount = 0.3f;
        float noiseAmount = 0.2f;
        float brightness = 0.6f;

        // Radiation
        float stereoWidth = 0.7f;
        float hfDirectionality = 0.6f;

        // Giant
        float scaleMeters = 2.0f;
        float massBias = 0.5f;
        float airLoss = 0.3f;
        float transientSlowing = 0.4f;

        // Gesture
        float force = 0.7f;
        float speed = 0.6f;
        float contactArea = 0.5f;
        float roughness = 0.3f;

        // Global
        float masterVolume = 0.8f;

    } params_;

    double sampleRate_ = 48000.0;
    int blockSize_ = 512;
    int maxVoices_ = 24;

    // Current giant state
    GiantScaleParameters currentScale_;
    GiantGestureParameters currentGesture_;

    void applyParameters();
    float calculateFrequency(int midiNote) const;

    // Preset serialization
    bool writeJsonParameter(const char* name, double value, char* buffer,
                            int& offset, int bufferSize) const;
    bool parseJsonParameter(const char* json, const char* param, double& value) const;
};

}  // namespace DSP
