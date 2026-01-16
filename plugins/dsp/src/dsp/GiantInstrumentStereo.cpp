/*
  ==============================================================================

    GiantInstrumentStereo.cpp
    Stereo processing implementation for Aether Giant Instruments
    Demonstrates Mutable Instruments-style odd/even mode separation

  ==============================================================================
*/

#include "dsp/AetherGiantBase.h"
#include "../../../../include/dsp/StereoProcessor.h"

namespace DSP {

//==============================================================================
// Giant Instrument Mode Processing with Stereo Separation
//==============================================================================

/**
 * Process resonant modes with odd/even stereo separation
 * This is the core Mutable Instruments-style technique used in Rings/Elements
 *
 * @param modes              Array of mode oscillators/resonators
 * @param modeOutputs        Array of mode output samples
 * @param left               Left channel output (accumulated)
 * @param right              Right channel output (accumulated)
 * @param oddEvenEnabled     Enable odd/even separation
 * @param modeOffset         Frequency offset between channels
 * @param width              Stereo width (0-1)
 */
template<size_t NumModes>
void processGiantModesStereo(
    const std::array<std::function<float()>, NumModes>& modes,
    std::array<float, NumModes>& modeOutputs,
    float& left,
    float& right,
    bool oddEvenEnabled,
    float modeOffset,
    float width)
{
    using namespace StereoProcessor;

    // Clear outputs
    left = 0.0f;
    right = 0.0f;

    // Process each mode with stereo separation
    for (size_t i = 0; i < NumModes; ++i)
    {
        // Get mode output
        float modeOutput = modes[i]();
        modeOutputs[i] = modeOutput;

        // Apply odd/even separation
        if (oddEvenEnabled)
        {
            // Even modes (0, 2, 4...) → Left channel
            // Odd modes (1, 3, 5...) → Right channel
            OddEvenSeparation::applySeparation(i, true, modeOutput,
                                               left, right, width);
        }
        else
        {
            // No separation - equal to both channels
            left += modeOutput;
            right += modeOutput;
        }
    }
}

//==============================================================================
// Giant Strings: Odd/Even Mode Separation
//==============================================================================

/**
 * Giant Strings stereo processing
 *
 * String resonances are split into odd/even harmonics:
 * - Even harmonics → Left channel
 * - Odd harmonics → Right channel
 * - Sympathetic resonance has spatial offset
 */
struct GiantStringsStereo
{
    static void processStringModes(
        const std::array<float, 16>& stringModes,  // 16 string resonance modes
        const std::array<float, 8>& sympatheticModes,  // 8 sympathetic modes
        float& left,
        float& right,
        const GiantEnvironmentParameters& env)
    {
        using namespace StereoProcessor;

        float width = env.stereoWidth;
        bool oddEven = env.oddEvenSeparation;

        // Process string modes
        for (size_t i = 0; i < stringModes.size(); ++i)
        {
            if (oddEven)
            {
                // Odd/even separation
                OddEvenSeparation::applySeparation(i, true, stringModes[i],
                                                   left, right, width);
            }
            else
            {
                left += stringModes[i];
                right += stringModes[i];
            }
        }

        // Process sympathetic modes with spatial offset
        for (size_t i = 0; i < sympatheticModes.size(); ++i)
        {
            // Sympathetic modes are slightly offset in stereo
            float spatialOffset = 0.3f;  // 30% offset for sympathetic resonance

            if (oddEven)
            {
                // Apply with spatial offset
                float leftGain = (i % 2 == 0) ? 1.0f : spatialOffset;
                float rightGain = (i % 2 == 1) ? 1.0f : spatialOffset;

                left += sympatheticModes[i] * leftGain;
                right += sympatheticModes[i] * rightGain;
            }
            else
            {
                left += sympatheticModes[i];
                right += sympatheticModes[i];
            }
        }
    }
};

//==============================================================================
// Giant Drums: Shell/Cavity Mode Separation
//==============================================================================

/**
 * Giant Drums stereo processing
 *
 * Drum resonances are split by physical origin:
 * - Shell modes → Left channel
 * - Cavity modes → Right channel
 * - Membrane radiation pattern affects stereo spread
 */
struct GiantDrumsStereo
{
    static void processDrumModes(
        const std::array<float, 12>& shellModes,    // Shell vibration modes
        const std::array<float, 8>& cavityModes,    // Air cavity modes
        const std::array<float, 16>& membraneModes, // Membrane modes
        float& left,
        float& right,
        const GiantEnvironmentParameters& env)
    {
        using namespace StereoProcessor;

        float width = env.stereoWidth;

        // Shell modes → Left
        for (size_t i = 0; i < shellModes.size(); ++i)
        {
            float shellGain = 1.0f;
            if (env.oddEvenSeparation && (i % 2 == 1))
            {
                // Bleed some odd shell modes to right
                shellGain = 1.0f - (width * 0.3f);
            }
            left += shellModes[i] * shellGain;
            right += shellModes[i] * (1.0f - shellGain);
        }

        // Cavity modes → Right
        for (size_t i = 0; i < cavityModes.size(); ++i)
        {
            float cavityGain = 1.0f;
            if (env.oddEvenSeparation && (i % 2 == 0))
            {
                // Bleed some even cavity modes to left
                cavityGain = 1.0f - (width * 0.3f);
            }
            right += cavityModes[i] * cavityGain;
            left += cavityModes[i] * (1.0f - cavityGain);
        }

        // Membrane modes with radiation pattern
        for (size_t i = 0; i < membraneModes.size(); ++i)
        {
            // Membrane radiates in both directions
            // Higher modes are more directional
            float directionality = static_cast<float>(i) / membraneModes.size();

            if (env.oddEvenSeparation)
            {
                // Directional radiation based on mode index
                float leftRadiation = (i % 2 == 0) ? 1.0f : (1.0f - directionality);
                float rightRadiation = (i % 2 == 1) ? 1.0f : (1.0f - directionality);

                left += membraneModes[i] * leftRadiation * width;
                right += membraneModes[i] * rightRadiation * width;
            }
            else
            {
                left += membraneModes[i];
                right += membraneModes[i];
            }
        }
    }
};

//==============================================================================
// Giant Voice: Formant Stereo Separation
//==============================================================================

/**
 * Giant Voice stereo processing
 *
 * Vocal formants are split for stereo imaging:
 * - Odd formants → Left channel
 * - Even formants → Right channel
 * - Vibrato has stereo width
 */
struct GiantVoiceStereo
{
    static void processFormants(
        const std::array<float, 5>& formants,  // Vocal formants
        float vibratoAmount,
        float vibratoRate,
        float& left,
        float& right,
        const GiantEnvironmentParameters& env)
    {
        using namespace StereoProcessor;

        float width = env.stereoWidth;
        bool oddEven = env.oddEvenSeparation;

        // Process formants with odd/even separation
        for (size_t i = 0; i < formants.size(); ++i)
        {
            if (oddEven)
            {
                // Odd/even formant separation
                OddEvenSeparation::applySeparation(i, true, formants[i],
                                                   left, right, width);
            }
            else
            {
                left += formants[i];
                right += formants[i];
            }
        }

        // Apply stereo vibrato
        if (vibratoAmount > 0.0f)
        {
            float vibratoPhase = 0.0f;  // Would be incremented per sample
            float vibratoOscillation = std::sin(vibratoPhase) * vibratoAmount;

            // Vibrato affects stereo width
            float leftVibrato = 1.0f + vibratoOscillation * width;
            float rightVibrato = 1.0f - vibratoOscillation * width;

            left *= leftVibrato;
            right *= rightVibrato;
        }
    }
};

//==============================================================================
// Giant Horns: Bell Radiation Pattern
//==============================================================================

/**
 * Giant Horns stereo processing
 *
 * Horn resonances are split by radiation pattern:
 * - Bell directivity affects stereo imaging
 * - Bore harmonic distribution
 */
struct GiantHornsStereo
{
    static void processHornModes(
        const std::array<float, 10>& bellModes,   // Bell radiation modes
        const std::array<float, 8>& boreModes,    // Bore harmonic modes
        float& left,
        float& right,
        const GiantEnvironmentParameters& env)
    {
        using namespace StereoProcessor;

        float width = env.stereoWidth;

        // Bell radiation pattern (directional)
        for (size_t i = 0; i < bellModes.size(); ++i)
        {
            // Bell is more directional for higher modes
            float directionality = static_cast<float>(i) / bellModes.size();

            if (env.oddEvenSeparation)
            {
                // Alternating directivity
                float leftDirectivity = (i % 2 == 0) ? 1.0f : (1.0f - directionality);
                float rightDirectivity = (i % 2 == 1) ? 1.0f : (1.0f - directionality);

                left += bellModes[i] * leftDirectivity * width;
                right += bellModes[i] * rightDirectivity * width;
            }
            else
            {
                left += bellModes[i];
                right += bellModes[i];
            }
        }

        // Bore harmonic distribution
        for (size_t i = 0; i < boreModes.size(); ++i)
        {
            if (env.oddEvenSeparation)
            {
                // Odd/even harmonic separation
                OddEvenSeparation::applySeparation(i, true, boreModes[i],
                                                   left, right, width);
            }
            else
            {
                left += boreModes[i];
                right += boreModes[i];
            }
        }
    }
};

//==============================================================================
// Giant Percussion: Mode Separation
//==============================================================================

/**
 * Giant Percussion stereo processing
 *
 * Percussion modes are split for stereo imaging:
 * - Odd modes → Left channel
 * - Even modes → Right channel
 * - Scrape position affects stereo placement
 */
struct GiantPercussionStereo
{
    static void processPercussionModes(
        const std::array<float, 12>& impactModes,  // Impact vibration modes
        const std::array<float, 6>& scrapeModes,   // Scrape/rattle modes
        float scrapePosition,  // -1.0 (left) to 1.0 (right)
        float& left,
        float& right,
        const GiantEnvironmentParameters& env)
    {
        using namespace StereoProcessor;

        float width = env.stereoWidth;
        bool oddEven = env.oddEvenSeparation;

        // Process impact modes with odd/even separation
        for (size_t i = 0; i < impactModes.size(); ++i)
        {
            if (oddEven)
            {
                OddEvenSeparation::applySeparation(i, true, impactModes[i],
                                                   left, right, width);
            }
            else
            {
                left += impactModes[i];
                right += impactModes[i];
            }
        }

        // Process scrape modes with spatial position
        for (size_t i = 0; i < scrapeModes.size(); ++i)
        {
            // Scrape position determines stereo placement
            float scrapePan = scrapePosition;  // -1 to 1

            // Convert pan to gains
            float panAngle = (scrapePan + 1.0f) * 0.5f * M_PI * 0.5f;
            float leftGain = std::cos(panAngle);
            float rightGain = std::sin(panAngle);

            // Apply with stereo width
            left += scrapeModes[i] * leftGain * width;
            right += scrapeModes[i] * rightGain * width;
        }
    }
};

//==============================================================================
// Base Stereo Processing for All Giant Instruments
//==============================================================================

/**
 * Process giant instrument with stereo enhancement
 * This would be called from the instrument's process() method
 */
void processGiantInstrumentStereo(
    float* modeOutputs,
    size_t numModes,
    float** outputs,
    int numChannels,
    int numSamples,
    const GiantEnvironmentParameters& env)
{
    using namespace StereoProcessor;

    if (numChannels < 2)
    {
        // Mono output - sum all modes
        for (int i = 0; i < numSamples; ++i)
        {
            outputs[0][i] = 0.0f;
            for (size_t mode = 0; mode < numModes; ++mode)
            {
                outputs[0][i] += modeOutputs[i * numModes + mode];
            }
        }
        return;
    }

    // Stereo output with odd/even mode separation
    for (int i = 0; i < numSamples; ++i)
    {
        float left = 0.0f;
        float right = 0.0f;

        for (size_t mode = 0; mode < numModes; ++mode)
        {
            float modeOutput = modeOutputs[i * numModes + mode];

            if (env.oddEvenSeparation)
            {
                // Odd/even separation
                OddEvenSeparation::applySeparation(mode, true, modeOutput,
                                                   left, right, env.stereoWidth);
            }
            else
            {
                // No separation
                left += modeOutput;
                right += modeOutput;
            }
        }

        // Apply stereo width
        StereoWidth::processWidth(left, right, env.stereoWidth);

        outputs[0][i] = left;
        outputs[1][i] = right;
    }
}

} // namespace DSP
