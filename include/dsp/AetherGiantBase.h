/*
  ==============================================================================

   AetherGiantBase.h
   Shared base class for all Aether Giant instruments

   Provides common infrastructure:
   - Scale-aware physics (scale_meters parameter)
   - Gesture parameter set (force, speed/aggression, contactArea/openness, roughness)
   - Delayed excitation
   - Momentum and inertia
   - Time smear
   - Environmental coupling (air, distance)
   - Cross-role gravity hooks

  ==============================================================================
*/

#pragma once

#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>
#include <functional>
#include <memory>

//==============================================================================
/**
 * Gesture parameters for Aether Giant instruments
 *
 * These four parameters control the "feel" of giant instruments:
 * - force: How much energy is applied (0.0 = gentle, 1.0 = maximum)
 * - speed: How fast the gesture happens (0.0 = very slow, 1.0 = instant)
 * - contactArea: How much surface is involved (0.0 = point, 1.0 = whole)
 * - roughness: Surface texture/irregularity (0.0 = smooth, 1.0 = very rough)
 *
 * Different instruments interpret these differently:
 * - Strings: force=pluck force, speed=pluck velocity, contactArea=finger width, roughness=finger texture
 * - Drums: force=strike force, speed=stick velocity, contactArea=stick tip size, roughness=stick texture
 * - Horns: force=breath pressure, speed=articulation speed, contactArea=lip aperture, roughness=breath turbulence
 * - Voice: force=diaphragm pressure, speed=articulation, contactArea=mouth openness, roughness=vocal texture
 * - Percussion: force=strike force, speed=mallet velocity, contactArea=mallet head size, roughness=mallet hardness
 */
struct GiantGestureParameters
{
    float force = 0.5f;        // Energy applied (0.0 - 1.0)
    float speed = 0.5f;        // Gesture velocity (0.0 - 1.0)
    float contactArea = 0.5f;  // Surface involvement (0.0 - 1.0)
    float roughness = 0.3f;    // Surface texture (0.0 - 1.0)
};

//==============================================================================
/**
 * Giant scale physics parameters
 *
 * Defines the physical scale of the instrument in meters.
 * This affects:
 * - Delayed response (larger = slower)
 * - Resonance frequencies (larger = lower)
 * - Damping characteristics (larger = longer sustain)
 * - Air coupling (larger = more environmental interaction)
 */
struct GiantScaleParameters
{
    float scaleMeters = 0.65f;    // Physical scale in meters (0.1 to 100.0)
    float massBias = 0.5f;        // Mass multiplier (0.0 = light, 1.0 = heavy)
    float airLoss = 0.3f;         // High-frequency air absorption (0.0 = none, 1.0 = heavy)
    float transientSlowing = 0.5f;// Attack time multiplier (0.0 = instant, 1.0 = very slow)
};

//==============================================================================
/**
 * Environmental coupling parameters
 *
 * Controls how the giant instrument interacts with its environment:
 * - Distance perception (air absorption, HF loss)
 * - Room coupling (early reflections, reverb)
 * - Cross-instrument gravity (energy transfer between instruments)
 * - Stereo imaging (Mutable Instruments-style odd/even mode separation)
 */
struct GiantEnvironmentParameters
{
    float distanceMeters = 10.0f;      // Listener distance (1.0 to 100.0)
    float roomSize = 0.5f;             // Room size (0.0 = dry, 1.0 = cathedral)
    float temperature = 20.0f;         // Air temperature Celsius (affects speed of sound)
    float humidity = 0.5f;             // Humidity (affects air absorption)
    bool crossCouplingEnabled = false; // Enable cross-instrument coupling

    // Stereo Enhancement (Mutable Instruments-style odd/even mode separation)
    float stereoWidth = 0.5f;          // 0=mono, 1=full stereo
    float stereoModeOffset = 0.02f;    // Frequency offset between odd/even modes
    bool oddEvenSeparation = true;     // Odd modes to left, even to right
};

//==============================================================================
/**
 * Excitation delay and momentum
 *
 * Giant instruments have delayed response due to their mass.
 * This component models that delay and the resulting momentum buildup.
 */
class GiantExcitationDelay
{
public:
    GiantExcitationDelay();
    ~GiantExcitationDelay() = default;

    void prepare(double sampleRate);
    void reset();

    /** Trigger excitation with gesture-based delay */
    void trigger(const GiantGestureParameters& gesture, const GiantScaleParameters& scale);

    /** Process delayed excitation output
        @returns Excitation signal (0.0 to 1.0) */
    float processSample();

    /** Check if excitation is active */
    bool isActive() const { return active; }

    /** Get current momentum (accumulated energy) */
    float getMomentum() const { return momentum; }

private:
    bool active = false;
    float momentum = 0.0f;
    float output = 0.0f;

    // Delay line for excitation
    std::vector<float> delayLine;
    int writeIndex = 0;
    int delaySamples = 0;

    // Envelope followers
    float attackPhase = 0.0f;
    float decayPhase = 0.0f;

    double sr = 48000.0;

    float calculateDelaySamples(const GiantGestureParameters& gesture,
                                const GiantScaleParameters& scale);
    float calculateAttackRate(const GiantGestureParameters& gesture,
                             const GiantScaleParameters& scale);
};

//==============================================================================
/**
 * Air absorption and distance modeling
 *
 * Models high-frequency loss due to air absorption over distance.
 * Giant instruments are perceived from far away, which affects their tone.
 */
class GiantAirAbsorption
{
public:
    GiantAirAbsorption();
    ~GiantAirAbsorption() = default;

    void prepare(double sampleRate);
    void reset();

    /** Process sample with air absorption
        @param input        Dry signal
        @param distanceMeters Distance from source
        @param humidity     Air humidity (affects absorption)
        @returns            Filtered signal with distance-appropriate tone */
    float processSample(float input, float distanceMeters, float humidity);

    void setDistance(float distance);
    void setHumidity(float humidity);

private:
    // Multi-stage filters for frequency-dependent absorption
    juce::dsp::FirstOrderTPTFilter<float> hfAbsorption;
    juce::dsp::FirstOrderTPTFilter<float> mfAbsorption;

    double sr = 48000.0;
    float currentDistance = 10.0f;
    float currentHumidity = 0.5f;
};

//==============================================================================
/**
 * Time smear and inertia
 *
 * Giant instruments have "slow" transients due to their mass.
 * This component models that time smear and perceived inertia.
 */
class GiantTimeSmear
{
public:
    GiantTimeSmear();
    ~GiantTimeSmear() = default;

    void prepare(double sampleRate);
    void reset();

    /** Process sample with time smear
        @param input            Dry signal
        @param transientSlowing How much to slow transients (0.0 - 1.0)
        @returns                Smeared signal */
    float processSample(float input, float transientSlowing);

    void setSmearAmount(float amount);

private:
    // Asymmetric attack/release smoothing
    float attackSmoother = 0.0f;
    float releaseSmoother = 0.0f;
    float lastOutput = 0.0f;

    double sr = 48000.0;
    float smearAmount = 0.5f;

    float calculateAttackCoefficient(float slowing);
    float calculateReleaseCoefficient(float slowing);
};

//==============================================================================
/**
 * Cross-instrument gravity/coupling
 *
 * Allows giant instruments to "feel" each other's vibrations.
 * This is similar to sympathetic resonance but between different instruments.
 */
class GiantCrossCoupling
{
public:
    struct CouplingInput
    {
        float energy = 0.0f;
        float frequency = 440.0f;
        float sourceId = 0;
    };

    GiantCrossCoupling();
    ~GiantCrossCoupling() = default;

    void prepare(double sampleRate, int maxSources);
    void reset();

    /** Add coupling source
        @param sourceId    Unique identifier for source
        @param energy      Energy from source (0.0 - 1.0)
        @param frequency   Fundamental frequency of source */
    void addCouplingSource(int sourceId, float energy, float frequency);

    /** Get coupled energy for this instrument
        @returns    Coupled energy (sum of all sources) */
    float processSample();

    void setCouplingStrength(float strength);

private:
    struct CouplingSource
    {
        int id = 0;
        float energy = 0.0f;
        float frequency = 440.0f;
        float phase = 0.0f;
    };

    std::vector<CouplingSource> sources;
    float couplingStrength = 0.1f;

    double sr = 48000.0;

    float calculateCouplingTransfer(float energy, float frequency, float targetFrequency);
};

//==============================================================================
/**
 * Base class for all Aether Giant instruments
 *
 * Provides shared functionality:
 * - Scale-aware physics
 * - Gesture interpretation
 * - Environmental effects
 * - Cross-coupling
 *
 * All giant instruments should inherit from this.
 */
class AetherGiantBase
{
public:
    //==============================================================================
    AetherGiantBase();
    virtual ~AetherGiantBase() = default;

    //==============================================================================
    /** Initialize giant instrument base */
    void prepareGiantBase(double sampleRate);

    /** Reset all giant components */
    void resetGiantBase();

    //==============================================================================
    /** Set scale parameters
        @param scale    Scale in meters (affects all physics) */
    void setScaleParameters(const GiantScaleParameters& scale);

    /** Set gesture parameters
        @param gesture    Gesture state (force, speed, etc.) */
    void setGestureParameters(const GiantGestureParameters& gesture);

    /** Set environment parameters
        @param environment    Environment (distance, room, etc.) */
    void setEnvironmentParameters(const GiantEnvironmentParameters& environment);

    //==============================================================================
    /** Trigger note with gesture
        @param note        MIDI note number
        @param velocity    MIDI velocity (0.0 - 1.0)
        @param gesture     Gesture parameters */
    virtual void triggerGiantNote(int note, float velocity,
                                  const GiantGestureParameters& gesture);

    //==============================================================================
    /** Process giant effects (air, smear, coupling)
        @param input    Dry signal from instrument
        @returns        Signal with giant effects applied */
    float processGiantEffects(float input);

    //==============================================================================
    /** Get current scale parameters */
    const GiantScaleParameters& getScaleParameters() const { return scaleParams; }

    /** Get current gesture parameters */
    const GiantGestureParameters& getGestureParameters() const { return gestureParams; }

    /** Get current environment parameters */
    const GiantEnvironmentParameters& getEnvironmentParameters() const { return environmentParams; }

    //==============================================================================
    /** Get current momentum (from excitation delay) */
    float getMomentum() const { return excitationDelay.getMomentum(); }

    /** Get excitation delay status */
    bool isExcitationActive() const { return excitationDelay.isActive(); }

protected:
    //==============================================================================
    // Giant physics parameters
    GiantScaleParameters scaleParams;
    GiantGestureParameters gestureParams;
    GiantEnvironmentParameters environmentParams;

    // Giant DSP components
    GiantExcitationDelay excitationDelay;
    GiantAirAbsorption airAbsorption;
    GiantTimeSmear timeSmear;
    GiantCrossCoupling crossCoupling;

    // State
    double sr = 48000.0;
    int currentNote = 60;  // Middle C
    float currentFrequency = 261.63f;

    //==============================================================================
    /** Calculate fundamental frequency from MIDI note */
    float midiToFrequency(int note) const;

    /** Apply scale-based frequency multiplier
        (larger instruments have lower fundamentals) */
    float applyScaleToFrequency(float baseFreq) const;

    /** Override in subclass to handle instrument-specific note triggering */
    virtual void triggerNoteImpl(int note, float velocity,
                                const GiantGestureParameters& gesture) = 0;

private:
    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AetherGiantBase)
};

//==============================================================================
/**
 * Utility functions for giant instrument physics
 */
namespace GiantPhysics
{
    /** Calculate delay samples based on scale and gesture
        @param scaleMeters    Scale in meters
        @param gestureSpeed   Speed of gesture (0.0 - 1.0)
        @param sampleRate     Audio sample rate
        @returns              Delay in samples */
    int calculateExcitationDelay(float scaleMeters, float gestureSpeed, double sampleRate);

    /** Calculate air absorption coefficients
        @param distance    Distance in meters
        @param humidity    Relative humidity (0.0 - 1.0)
        @param frequency   Frequency of interest
        @returns           Absorption coefficient (0.0 - 1.0) */
    float calculateAirAbsorption(float distance, float humidity, float frequency);

    /** Calculate time smear coefficient
        @param scaleMeters        Scale in meters
        @param transientSlowing   Slowing factor (0.0 - 1.0)
        @returns                  Smoothing coefficient */
    float calculateTimeSmearCoefficient(float scaleMeters, float transientSlowing);

    /** Scale frequency by instrument size
        @param baseFreq     Base frequency
        @param scaleMeters  Scale in meters
        @returns            Scaled frequency */
    float scaleFrequency(float baseFreq, float scaleMeters);
}
