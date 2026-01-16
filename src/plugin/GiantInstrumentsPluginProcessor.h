/*
  ==============================================================================

    GiantInstrumentsPluginProcessor.h
    Created: January 9, 2026
    Author:  Bret Bouchard

    JUCE AudioProcessor wrapper for Giant Instruments
    Unified plugin for all 5 giant instruments with selector

  ==============================================================================
*/

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp/KaneMarcoAetherStringPureDSP.h"
#include "dsp/AetherGiantDrumsDSP.h"
#include "dsp/AetherGiantHornsDSP.h"
#include "dsp/AetherGiantPercussionDSP.h"
#include "dsp/AetherGiantVoiceDSP.h"
#include "dsp/MPEUniversalSupport.h"
#include "dsp/MicrotonalTuning.h"

//==============================================================================
// Giant Instrument Type
//==============================================================================

enum class GiantInstrumentType
{
    GiantStrings = 0,
    GiantDrums,
    GiantVoice,
    GiantHorns,
    GiantPercussion
};

//==============================================================================
// Giant Instruments Plugin Processor
//==============================================================================

class GiantInstrumentsPluginProcessor : public juce::AudioProcessor
{
public:
    GiantInstrumentsPluginProcessor();
    ~GiantInstrumentsPluginProcessor() override;

    //==========================================================================
    // AudioProcessor Interface
    //==========================================================================

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

#ifndef JucePlugin_PreferredChannelConfigurations
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
#endif

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;

    //==========================================================================
    // AudioProcessorEditor Interface
    //==========================================================================

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    //==========================================================================
    // Plugin Information
    //==========================================================================

    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    //==========================================================================
    // MPE Capability Declaration
    //==========================================================================

    bool supportsMPE() const override { return true; }

    //==========================================================================
    // Program/Preset Management
    //==========================================================================

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    //==========================================================================
    // State Management
    //==========================================================================

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    //==========================================================================
    // Giant Instrument Management
    //==========================================================================

    /**
     * Get current instrument type
     */
    GiantInstrumentType getInstrumentType() const { return instrumentType; }

    /**
     * Set instrument type (switches DSP engine)
     */
    void setInstrumentType(GiantInstrumentType type);

    /**
     * Get name of instrument type
     */
    static juce::String getInstrumentTypeName(GiantInstrumentType type);

    //==========================================================================
    // Parameter Access
    //==========================================================================

    /**
     * Get current DSP engine (for editor)
     */
    DSP::InstrumentDSP* getCurrentDSP() { return currentInstrument.get(); }

    /**
     * Get parameter value by name
     */
    float getParameter(const juce::String& name);

    /**
     * Set parameter value by name
     */
    void setParameter(const juce::String& name, float value);

private:
    //==========================================================================
    // Internal Members
    //==========================================================================

    // Current instrument DSP engine
    std::unique_ptr<DSP::InstrumentDSP> currentInstrument;
    GiantInstrumentType instrumentType = GiantInstrumentType::GiantStrings;

    // Critical section for DSP switching
    juce::CriticalSection dspLock;

    // MPE Support (Full MPE for all Giant Instruments)
    std::unique_ptr<MPEUniversalSupport> mpeSupport;
    bool mpeEnabled = true;

    // Microtonal Tuning Support
    std::unique_ptr<MicrotonalTuningManager> tuningManager;
    bool microtonalEnabled = true;

    // Factory presets
    struct PresetInfo
    {
        juce::String name;
        juce::String filePath;
        GiantInstrumentType type;
    };
    std::vector<PresetInfo> factoryPresets;
    int currentProgramIndex = 0;

    //==========================================================================
    // Private Methods
    //==========================================================================

    /**
     * Create DSP engine for instrument type
     */
    std::unique_ptr<DSP::InstrumentDSP> createInstrument(GiantInstrumentType type);

    /**
     * Switch to different instrument (with state preservation if possible)
     */
    void switchInstrument(GiantInstrumentType newType);

    /**
     * Scan and load factory presets
     */
    void loadFactoryPresets();

    /**
     * Get presets folder for instrument type
     */
    juce::File getPresetsFolder(GiantInstrumentType type) const;

    /**
     * Load preset from file
     */
    bool loadPresetFromFile(const juce::File& presetFile);

    /**
     * Process MIDI messages and extract MPE gestures
     */
    void processMPE(const juce::MidiBuffer& midiMessages);

    /**
     * Apply MPE gestures to note
     */
    void applyMPEToNote(int noteNumber, int midiChannel, DSP::InstrumentDSP* dsp);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GiantInstrumentsPluginProcessor)
};
