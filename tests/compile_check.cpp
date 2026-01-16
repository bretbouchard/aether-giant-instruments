/*
 * Simple compilation check for Giant Horns improvements
 * This validates that the new API compiles correctly
 */

#include "dsp/AetherGiantHornsDSP.h"
#include <iostream>
#include <cassert>

using namespace DSP;

// Test helper macros
#define TEST_ASSERT(condition, message) \
    if (!(condition)) { \
        std::cerr << "FAILED: " << message << std::endl; \
        return false; \
    }

#define TEST_INFO(message) \
    std::cout << "INFO: " << message << std::endl;

bool testLipReedParameters()
{
    TEST_INFO("Testing Lip Reed Parameters...");

    LipReedExciter exciter;
    exciter.prepare(48000.0);

    LipReedExciter::Parameters params;
    params.lipTension = 0.5f;
    params.mouthPressure = 0.5f;
    params.nonlinearity = 0.3f;
    params.chaosThreshold = 0.7f;
    params.growlAmount = 0.2f;

    // ENHANCED: New parameters
    params.lipMass = 0.5f;
    params.lipStiffness = 0.5f;

    exciter.setParameters(params);

    // Process some samples to test stability
    for (int i = 0; i < 1000; ++i)
    {
        float output = exciter.processSample(0.8f, 440.0f);
        TEST_ASSERT(std::isfinite(output), "Lip reed output is finite");
        TEST_ASSERT(std::abs(output) <= 2.0f, "Lip reed output is reasonable");
    }

    TEST_INFO("Lip Reed Parameters: PASSED");
    return true;
}

bool testBoreWaveguide()
{
    TEST_INFO("Testing Bore Waveguide...");

    BoreWaveguide bore;
    bore.prepare(48000.0);

    BoreWaveguide::Parameters params;
    params.lengthMeters = 3.0f;
    params.boreShape = BoreWaveguide::BoreShape::Hybrid;
    params.reflectionCoeff = 0.9f;
    params.lossPerMeter = 0.05f;
    params.flareFactor = 0.5f;

    bore.setParameters(params);

    // Test different bore shapes
    bore.setBoreShape(BoreWaveguide::BoreShape::Cylindrical);
    bore.setBoreShape(BoreWaveguide::BoreShape::Conical);
    bore.setBoreShape(BoreWaveguide::BoreShape::Flared);
    bore.setBoreShape(BoreWaveguide::BoreShape::Hybrid);

    // Process some samples
    for (int i = 0; i < 1000; ++i)
    {
        float output = bore.processSample(0.5f);
        TEST_ASSERT(std::isfinite(output), "Bore output is finite");
        TEST_ASSERT(std::abs(output) <= 2.0f, "Bore output is reasonable");
    }

    // Test length adjustment
    float freq1 = bore.getFundamentalFrequency();
    bore.setLengthMeters(5.0f);
    float freq2 = bore.getFundamentalFrequency();
    TEST_ASSERT(freq2 < freq1, "Longer bore = lower frequency");

    TEST_INFO("Bore Waveguide: PASSED");
    return true;
}

bool testBellRadiation()
{
    TEST_INFO("Testing Bell Radiation...");

    BellRadiationFilter bell;
    bell.prepare(48000.0);
    bell.setCutoffFrequency(1000.0f);

    // Process with different bell sizes
    for (int i = 0; i < 1000; ++i)
    {
        float output1 = bell.processSample(0.5f, 0.5f); // Small bell
        float output2 = bell.processSample(0.5f, 2.0f); // Large bell
        TEST_ASSERT(std::isfinite(output1), "Small bell output is finite");
        TEST_ASSERT(std::isfinite(output2), "Large bell output is finite");
    }

    TEST_INFO("Bell Radiation: PASSED");
    return true;
}

bool testHornFormantShaper()
{
    TEST_INFO("Testing Horn Formant Shaper...");

    HornFormantShaper formants;
    formants.prepare(48000.0);

    // Test different horn types
    formants.setHornType(HornFormantShaper::HornType::Trumpet);
    formants.setHornType(HornFormantShaper::HornType::Trombone);
    formants.setHornType(HornFormantShaper::HornType::Tuba);
    formants.setHornType(HornFormantShaper::HornType::FrenchHorn);
    formants.setHornType(HornFormantShaper::HornType::Saxophone);
    formants.setHornType(HornFormantShaper::HornType::Custom);

    HornFormantShaper::Parameters params;
    params.brightness = 0.5f;
    params.warmth = 0.5f;
    params.metalness = 0.7f;
    formants.setParameters(params);

    // Process samples
    for (int i = 0; i < 1000; ++i)
    {
        float output = formants.processSample(0.5f);
        TEST_ASSERT(std::isfinite(output), "Formant output is finite");
        TEST_ASSERT(std::abs(output) <= 2.0f, "Formant output is reasonable");
    }

    TEST_INFO("Horn Formant Shaper: PASSED");
    return true;
}

bool testGiantHornVoice()
{
    TEST_INFO("Testing Giant Horn Voice...");

    GiantHornVoice voice;
    voice.prepare(48000.0);

    GiantGestureParameters gesture;
    gesture.force = 0.6f;
    gesture.speed = 0.3f;
    gesture.contactArea = 0.5f;
    gesture.roughness = 0.3f;

    GiantScaleParameters scale;
    scale.scaleMeters = 5.0f;
    scale.massBias = 0.6f;
    scale.airLoss = 0.4f;
    scale.transientSlowing = 0.6f;

    // Trigger note
    voice.trigger(60, 0.8f, gesture, scale);
    TEST_ASSERT(voice.isActive(), "Voice is active after trigger");

    // Process samples
    for (int i = 0; i < 1000; ++i)
    {
        float output = voice.processSample();
        TEST_ASSERT(std::isfinite(output), "Voice output is finite");
        TEST_ASSERT(std::abs(output) <= 2.0f, "Voice output is reasonable");
    }

    // Release note
    voice.release(false);

    TEST_INFO("Giant Horn Voice: PASSED");
    return true;
}

bool testGiantHornVoiceManager()
{
    TEST_INFO("Testing Giant Horn Voice Manager...");

    GiantHornVoiceManager manager;
    manager.prepare(48000.0, 12);

    GiantGestureParameters gesture;
    gesture.force = 0.6f;
    gesture.speed = 0.3f;
    gesture.contactArea = 0.5f;
    gesture.roughness = 0.3f;

    GiantScaleParameters scale;
    scale.scaleMeters = 5.0f;
    scale.massBias = 0.6f;
    scale.airLoss = 0.4f;
    scale.transientSlowing = 0.6f;

    // Test polyphony
    manager.handleNoteOn(60, 0.8f, gesture, scale);
    manager.handleNoteOn(64, 0.8f, gesture, scale);
    manager.handleNoteOn(67, 0.8f, gesture, scale);

    TEST_ASSERT(manager.getActiveVoiceCount() == 3, "Three voices active");

    // Process samples
    for (int i = 0; i < 1000; ++i)
    {
        float output = manager.processSample();
        TEST_ASSERT(std::isfinite(output), "Manager output is finite");
        TEST_ASSERT(std::abs(output) <= 3.0f, "Manager output is reasonable");
    }

    // Release notes
    manager.handleNoteOff(60, false);
    manager.handleNoteOff(64, false);
    manager.handleNoteOff(67, false);

    TEST_INFO("Giant Horn Voice Manager: PASSED");
    return true;
}

bool testAetherGiantHornsDSP()
{
    TEST_INFO("Testing AetherGiantHornsPureDSP...");

    AetherGiantHornsPureDSP dsp;
    dsp.prepare(48000.0, 512);

    // Test parameter setting/getting
    dsp.setParameter("lipTension", 0.7f);
    TEST_ASSERT(dsp.getParameter("lipTension") == 0.7f, "lipTension parameter works");

    dsp.setParameter("lipMass", 0.6f);
    TEST_ASSERT(dsp.getParameter("lipMass") == 0.6f, "lipMass parameter works");

    dsp.setParameter("lipStiffness", 0.4f);
    TEST_ASSERT(dsp.getParameter("lipStiffness") == 0.4f, "lipStiffness parameter works");

    dsp.setParameter("flareFactor", 0.8f);
    TEST_ASSERT(dsp.getParameter("flareFactor") == 0.8f, "flareFactor parameter works");

    // Test preset save/load
    char jsonBuffer[4096];
    bool saved = dsp.savePreset(jsonBuffer, sizeof(jsonBuffer));
    TEST_ASSERT(saved, "Preset saved successfully");

    bool loaded = dsp.loadPreset(jsonBuffer);
    TEST_ASSERT(loaded, "Preset loaded successfully");

    // Test event handling
    ScheduledEvent noteOnEvent;
    noteOnEvent.type = ScheduledEvent::NOTE_ON;
    noteOnEvent.data.note.midiNote = 60;
    noteOnEvent.data.note.velocity = 0.8f;
    dsp.handleEvent(noteOnEvent);

    TEST_ASSERT(dsp.getActiveVoiceCount() == 1, "Note on triggered voice");

    // Process audio
    float* outputs[2];
    float outputBuffer[2][512];
    outputs[0] = outputBuffer[0];
    outputs[1] = outputBuffer[1];

    for (int ch = 0; ch < 2; ++ch)
    {
        for (int i = 0; i < 512; ++i)
        {
            outputBuffer[ch][i] = 0.0f;
        }
    }

    dsp.process(outputs, 2, 512);

    // Validate output
    for (int i = 0; i < 512; ++i)
    {
        TEST_ASSERT(std::isfinite(outputs[0][i]), "DSP output is finite");
        TEST_ASSERT(std::isfinite(outputs[1][i]), "DSP output is finite");
    }

    TEST_INFO("AetherGiantHornsPureDSP: PASSED");
    return true;
}

int main()
{
    std::cout << "========================================" << std::endl;
    std::cout << "Giant Horns Improvements Compilation Test" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << std::endl;

    bool allPassed = true;

    allPassed &= testLipReedParameters();
    allPassed &= testBoreWaveguide();
    allPassed &= testBellRadiation();
    allPassed &= testHornFormantShaper();
    allPassed &= testGiantHornVoice();
    allPassed &= testGiantHornVoiceManager();
    allPassed &= testAetherGiantHornsDSP();

    std::cout << std::endl;
    std::cout << "========================================" << std::endl;
    if (allPassed)
    {
        std::cout << "ALL TESTS PASSED!" << std::endl;
        std::cout << "========================================" << std::endl;
        return 0;
    }
    else
    {
        std::cout << "SOME TESTS FAILED!" << std::endl;
        std::cout << "========================================" << std::endl;
        return 1;
    }
}
