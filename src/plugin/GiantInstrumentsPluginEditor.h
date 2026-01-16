/*
  ==============================================================================

    GiantInstrumentsPluginEditor.h
    Created: January 9, 2026
    Author:  Bret Bouchard

    JUCE AudioProcessorEditor for Giant Instruments

  ==============================================================================
*/

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "GiantInstrumentsPluginProcessor.h"

//==============================================================================
// Giant Instruments Plugin Editor
//==============================================================================

class GiantInstrumentsPluginEditor : public juce::AudioProcessorEditor
{
public:
    GiantInstrumentsPluginEditor(GiantInstrumentsPluginProcessor&);
    ~GiantInstrumentsPluginEditor() override;

    //==========================================================================
    // Graphics
    //==========================================================================

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    //==========================================================================
    // Internal Components
    //==========================================================================

    // Reference to processor
    GiantInstrumentsPluginProcessor& processor;

    // Instrument selector
    std::unique_ptr<juce::ComboBox> instrumentSelector;
    std::unique_ptr<juce::Label> instrumentLabel;

    // MPE enable toggle
    std::unique_ptr<juce::ToggleButton> mpeEnableToggle;
    std::unique_ptr<juce::Label> mpeLabel;

    // Microtonal enable toggle
    std::unique_ptr<juce::ToggleButton> microtonalEnableToggle;
    std::unique_ptr<juce::Label> microtonalLabel;

    // Master volume
    std::unique_ptr<juce::Slider> masterVolumeSlider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> masterVolumeAttachment;
    std::unique_ptr<juce::Label> masterVolumeLabel;

    // Preset loader
    std::unique_ptr<juce::ComboBox> presetSelector;
    std::unique_ptr<juce::Label> presetLabel;

    // Info display (shows current instrument info)
    std::unique_ptr<juce::TextEditor> infoDisplay;

    // Giant instrument visual
    std::unique_ptr<juce::Component> giantVisual;

    //==========================================================================
    // Callbacks
    //==========================================================================

    void instrumentChanged();
    void updateInfoDisplay();
    void refreshPresetList();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GiantInstrumentsPluginEditor)
};
