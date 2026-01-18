/*
  ==============================================================================

    AetherGiantVoiceComprehensiveTest.cpp
    Created: January 13, 2026
    Author: Bret Bouchard

    Comprehensive test suite for Aether Giant Voice (Mythic Vocal Synthesis)

  ==============================================================================
*/

#include "JuceStandaloneConfig.h"
#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>
#include "../include/dsp/AetherGiantVoiceDSP.h"
#include <iostream>
#include <cstdio>
#include <cmath>
#include <vector>

using namespace DSP;

//==============================================================================
// Test Result Tracking
//==============================================================================

struct TestStats {
    int passed = 0;
    int failed = 0;
    int total = 0;

    void pass(const char* testName) {
        total++;
        passed++;
        std::cout << "  [PASS] " << testName << std::endl;
    }

    void fail(const char* testName, const std::string& reason) {
        total++;
        failed++;
        std::cout << "  [FAIL] " << testName << ": " << reason << std::endl;
    }

    void printSummary() {
        std::cout << "\n========================================" << std::endl;
        std::cout << "Test Summary: " << passed << "/" << total << " passed";
        if (failed > 0) {
            std::cout << " (" << failed << " failed)";
        }
        std::cout << "\n========================================" << std::endl;
    }
};

//==============================================================================
// Audio Analysis Utilities
//==============================================================================

float getPeakLevel(const float* buffer, int numSamples) {
    float peak = 0.0f;
    for (int i = 0; i < numSamples; ++i) {
        float abs = std::abs(buffer[i]);
        if (abs > peak) peak = abs;
    }
    return peak;
}

void processAudioInChunks(AetherGiantVoicePureDSP& synth, float* left, float* right, int numSamples, int bufferSize = 512) {
    for (int offset = 0; offset < numSamples; offset += bufferSize) {
        int samplesToProcess = std::min(bufferSize, numSamples - offset);
        float* outputs[] = { left + offset, right + offset };
        synth.process(outputs, 2, samplesToProcess);
    }
}

//==============================================================================
// Test 1: Instrument Initialization
//==============================================================================

bool testInstrumentInit(TestStats& stats) {
    std::cout << "\n[Test 1] Instrument Initialization" << std::endl;

    AetherGiantVoicePureDSP synth;
    if (!synth.prepare(48000.0, 512)) {
        stats.fail("prepare", "Failed to prepare synth");
        return false;
    }

    const char* name = synth.getInstrumentName();
    std::cout << "    Instrument Name: " << name << std::endl;

    if (std::string(name) != "AetherGiantVoice") {
        stats.fail("instrument_name", "Unexpected instrument name");
        return false;
    }

    stats.pass("instrument_init");
    return true;
}

//==============================================================================
// Test 2: Basic Voice Triggering
//==============================================================================

bool testBasicVoice(TestStats& stats) {
    std::cout << "\n[Test 2] Basic Voice Triggering" << std::endl;

    AetherGiantVoicePureDSP synth;
    synth.prepare(48000.0, 512);

    const int numSamples = 12000;
    std::vector<float> left(numSamples);
    std::vector<float> right(numSamples);

    ScheduledEvent event;
    event.type = ScheduledEvent::NOTE_ON;
    event.time = 0.0;
    event.sampleOffset = 0;
    event.data.note.midiNote = 60;
    event.data.note.velocity = 0.8f;
    synth.handleEvent(event);

    processAudioInChunks(synth, left.data(), right.data(), numSamples);

    float peak = getPeakLevel(left.data(), numSamples);
    std::cout << "    Peak: " << peak << std::endl;

    if (peak < 0.0001f) {
        stats.fail("voice_audio", "No audio produced");
        return false;
    }

    stats.pass("basic_voice");
    return true;
}

//==============================================================================
// Test 3: Polyphony
//==============================================================================

bool testPolyphony(TestStats& stats) {
    std::cout << "\n[Test 3] Polyphony" << std::endl;

    AetherGiantVoicePureDSP synth;
    synth.prepare(48000.0, 512);

    const int numSamples = 12000;
    std::vector<float> left(numSamples);
    std::vector<float> right(numSamples);

    // Play multiple notes
    int notes[] = {48, 52, 55, 60};
    for (int note : notes) {
        ScheduledEvent event;
        event.type = ScheduledEvent::NOTE_ON;
        event.time = 0.0;
        event.sampleOffset = 0;
        event.data.note.midiNote = note;
        event.data.note.velocity = 0.7f;
        synth.handleEvent(event);
    }

    processAudioInChunks(synth, left.data(), right.data(), numSamples);

    int activeVoices = synth.getActiveVoiceCount();
    std::cout << "    Active Voices: " << activeVoices << std::endl;

    float peak = getPeakLevel(left.data(), numSamples);
    if (peak < 0.0001f) {
        stats.fail("polyphony_audio", "No audio for chord");
        return false;
    }

    stats.pass("polyphony");
    return true;
}

//==============================================================================
// Test 4: Breath/Pressure Parameters
//==============================================================================

bool testBreathParameters(TestStats& stats) {
    std::cout << "\n[Test 4] Breath/Pressure Parameters" << std::endl;

    AetherGiantVoicePureDSP synth;
    synth.prepare(48000.0, 512);

    // Test different breath attack settings
    float attacks[] = {0.05f, 0.2f, 0.5f};

    for (float attack : attacks) {
        synth.setParameter("breathAttack", attack);

        const int numSamples = 12000;
        std::vector<float> left(numSamples);
        std::vector<float> right(numSamples);

        ScheduledEvent event;
        event.type = ScheduledEvent::NOTE_ON;
        event.time = 0.0;
        event.sampleOffset = 0;
        event.data.note.midiNote = 60;
        event.data.note.velocity = 0.7f;
        synth.handleEvent(event);

        processAudioInChunks(synth, left.data(), right.data(), numSamples);

        float peak = getPeakLevel(left.data(), numSamples);
        std::cout << "    Breath Attack " << attack << ": peak = " << peak << std::endl;

        if (peak < 0.0001f) {
            stats.fail(("breath_attack_" + std::to_string(static_cast<int>(attack * 100))).c_str(), "No audio");
            return false;
        }

        synth.reset();
        synth.prepare(48000.0, 512);
    }

    stats.pass("breath_parameters");
    return true;
}

//==============================================================================
// Test 5: Aggression Parameter
//==============================================================================

bool testAggression(TestStats& stats) {
    std::cout << "\n[Test 5] Aggression Parameter" << std::endl;

    AetherGiantVoicePureDSP synth;
    synth.prepare(48000.0, 512);

    // Test different aggression levels
    float aggressions[] = {0.0f, 0.5f, 1.0f};

    for (float aggression : aggressions) {
        synth.setParameter("aggression", aggression);

        const int numSamples = 12000;
        std::vector<float> left(numSamples);
        std::vector<float> right(numSamples);

        ScheduledEvent event;
        event.type = ScheduledEvent::NOTE_ON;
        event.time = 0.0;
        event.sampleOffset = 0;
        event.data.note.midiNote = 48;
        event.data.note.velocity = 0.8f;
        synth.handleEvent(event);

        processAudioInChunks(synth, left.data(), right.data(), numSamples);

        float peak = getPeakLevel(left.data(), numSamples);
        std::cout << "    Aggression " << aggression << ": peak = " << peak << std::endl;

        if (peak < 0.0001f) {
            stats.fail(("aggression_" + std::to_string(static_cast<int>(aggression * 10))).c_str(), "No audio");
            return false;
        }

        synth.reset();
        synth.prepare(48000.0, 512);
    }

    stats.pass("aggression");
    return true;
}

//==============================================================================
// Test 6: Sample Rate Compatibility
//==============================================================================

bool testSampleRates(TestStats& stats) {
    std::cout << "\n[Test 6] Sample Rate Compatibility" << std::endl;

    double sampleRates[] = {44100.0, 48000.0, 96000.0};

    for (double sr : sampleRates) {
        AetherGiantVoicePureDSP synth;
        if (!synth.prepare(sr, 512)) {
            stats.fail(("samplerate_" + std::to_string(static_cast<int>(sr))).c_str(), "Failed to prepare");
            return false;
        }

        const int numSamples = (int)(sr * 0.25);
        std::vector<float> left(numSamples);
        std::vector<float> right(numSamples);

        ScheduledEvent event;
        event.type = ScheduledEvent::NOTE_ON;
        event.time = 0.0;
        event.sampleOffset = 0;
        event.data.note.midiNote = 60;
        event.data.note.velocity = 0.7f;
        synth.handleEvent(event);

        processAudioInChunks(synth, left.data(), right.data(), numSamples);

        float peak = getPeakLevel(left.data(), numSamples);
        std::cout << "    " << static_cast<int>(sr) << " Hz: peak = " << peak << std::endl;

        if (peak < 0.0001f) {
            stats.fail(("samplerate_" + std::to_string(static_cast<int>(sr))).c_str(), "No audio");
            return false;
        }
    }

    stats.pass("sample_rates");
    return true;
}

//==============================================================================
// Test 7: Stereo Output
//==============================================================================

bool testStereoOutput(TestStats& stats) {
    std::cout << "\n[Test 7] Stereo Output" << std::endl;

    AetherGiantVoicePureDSP synth;
    synth.prepare(48000.0, 512);

    const int numSamples = 12000;
    std::vector<float> left(numSamples);
    std::vector<float> right(numSamples);

    ScheduledEvent event;
    event.type = ScheduledEvent::NOTE_ON;
    event.time = 0.0;
    event.sampleOffset = 0;
    event.data.note.midiNote = 48;
    event.data.note.velocity = 0.7f;
    synth.handleEvent(event);

    processAudioInChunks(synth, left.data(), right.data(), numSamples);

    float leftPeak = getPeakLevel(left.data(), numSamples);
    float rightPeak = getPeakLevel(right.data(), numSamples);

    std::cout << "    Left: " << leftPeak << ", Right: " << rightPeak << std::endl;

    if (leftPeak < 0.0001f || rightPeak < 0.0001f) {
        stats.fail("stereo_output", "No audio in one or both channels");
        return false;
    }

    stats.pass("stereo_output");
    return true;
}

//==============================================================================
// Main Test Runner
//==============================================================================

int main(int argc, char* argv[]) {
    std::cout << "\n========================================" << std::endl;
    std::cout << "AetherGiantVoice Comprehensive Test Suite" << std::endl;
    std::cout << "========================================" << std::endl;

    TestStats stats;

    testInstrumentInit(stats);
    testBasicVoice(stats);
    testPolyphony(stats);
    testBreathParameters(stats);
    testAggression(stats);
    testSampleRates(stats);
    testStereoOutput(stats);

    stats.printSummary();

    return (stats.failed == 0) ? 0 : 1;
}
