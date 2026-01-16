/*
  ==============================================================================

    GiantInstrumentsPluginEditor.cpp
    Created: January 9, 2026
    Author:  Bret Bouchard

    JUCE AudioProcessorEditor implementation for Giant Instruments

  ==============================================================================
*/

#include "GiantInstrumentsPluginEditor.h"

//==============================================================================
// GiantInstrumentsPluginEditor Implementation
//==============================================================================

GiantInstrumentsPluginEditor::GiantInstrumentsPluginEditor(GiantInstrumentsPluginProcessor& p)
    : juce::AudioProcessorEditor(p)
    , processor(p)
{
    // Set editor size
    setSize(800, 600);

    //==========================================================================
    // Instrument Selector
    //==========================================================================

    instrumentLabel = std::make_unique<juce::Label>();
    instrumentLabel->setText("Giant Instrument:", juce::dontSendNotification);
    instrumentLabel->setFont(juce::Font(16.0f, juce::Font::bold));
    instrumentLabel->setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(instrumentLabel.get());

    instrumentSelector = std::make_unique<juce::ComboBox>();
    instrumentSelector->addItem("Giant Strings", static_cast<int>(GiantInstrumentType::GiantStrings));
    instrumentSelector->addItem("Giant Drums", static_cast<int>(GiantInstrumentType::GiantDrums));
    instrumentSelector->addItem("Giant Voice", static_cast<int>(GiantInstrumentType::GiantVoice));
    instrumentSelector->addItem("Giant Horns", static_cast<int>(GiantInstrumentType::GiantHorns));
    instrumentSelector->addItem("Giant Percussion", static_cast<int>(GiantInstrumentType::GiantPercussion));
    instrumentSelector->setSelectedId(static_cast<int>(processor.getInstrumentType()), juce::dontSendNotification);
    instrumentSelector->onChange = [this] { instrumentChanged(); };
    addAndMakeVisible(instrumentSelector.get());

    //==========================================================================
    // MPE Enable Toggle
    //==========================================================================

    mpeLabel = std::make_unique<juce::Label>();
    mpeLabel->setText("MPE:", juce::dontSendNotification);
    mpeLabel->setFont(juce::Font(14.0f));
    mpeLabel->setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(mpeLabel.get());

    mpeEnableToggle = std::make_unique<juce::ToggleButton>();
    mpeEnableToggle->setToggleState(true, juce::dontSendNotification);
    mpeEnableToggle->setColour(juce::ToggleButton::textColourId, juce::Colours::white);
    addAndMakeVisible(mpeEnableToggle.get());

    //==========================================================================
    // Microtonal Enable Toggle
    //==========================================================================

    microtonalLabel = std::make_unique<juce::Label>();
    microtonalLabel->setText("Microtonal:", juce::dontSendNotification);
    microtonalLabel->setFont(juce::Font(14.0f));
    microtonalLabel->setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(microtonalLabel.get());

    microtonalEnableToggle = std::make_unique<juce::ToggleButton>();
    microtonalEnableToggle->setToggleState(true, juce::dontSendNotification);
    microtonalEnableToggle->setColour(juce::ToggleButton::textColourId, juce::Colours::white);
    addAndMakeVisible(microtonalEnableToggle.get());

    //==========================================================================
    // Master Volume
    //==========================================================================

    masterVolumeLabel = std::make_unique<juce::Label>();
    masterVolumeLabel->setText("Master Volume:", juce::dontSendNotification);
    masterVolumeLabel->setFont(juce::Font(14.0f));
    masterVolumeLabel->setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(masterVolumeLabel.get());

    masterVolumeSlider = std::make_unique<juce::Slider>();
    masterVolumeSlider->setRange(0.0, 1.0, 0.01);
    masterVolumeSlider->setValue(0.8);
    masterVolumeSlider->setTextBoxStyle(juce::Slider::TextBoxRight, false, 80, 20);
    masterVolumeSlider->onValueChange = [this]
    {
        processor.setParameter("master_volume", static_cast<float>(masterVolumeSlider->getValue()));
    };
    addAndMakeVisible(masterVolumeSlider.get());

    //==========================================================================
    // Preset Selector
    //==========================================================================

    presetLabel = std::make_unique<juce::Label>();
    presetLabel->setText("Preset:", juce::dontSendNotification);
    presetLabel->setFont(juce::Font(14.0f));
    presetLabel->setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(presetLabel.get());

    presetSelector = std::make_unique<juce::ComboBox>();
    refreshPresetList();
    presetSelector->onChange = [this]
    {
        int selectedId = presetSelector->getSelectedId();
        if (selectedId > 0)
        {
            processor.setCurrentProgram(selectedId - 1);
        }
    };
    addAndMakeVisible(presetSelector.get());

    //==========================================================================
    // Info Display
    //==========================================================================

    infoDisplay = std::make_unique<juce::TextEditor>();
    infoDisplay->setReadOnly(true);
    infoDisplay->setMultiLine(true);
    infoDisplay->setColour(juce::TextEditor::backgroundColourId, juce::Colours::darkgrey);
    infoDisplay->setColour(juce::TextEditor::textColourId, juce::Colours::white);
    addAndMakeVisible(infoDisplay.get());

    updateInfoDisplay();

    //==========================================================================
    // Giant Visual (placeholder for now)
    //==========================================================================

    giantVisual = std::make_unique<juce::Component>();
    addAndMakeVisible(giantVisual.get());
}

GiantInstrumentsPluginEditor::~GiantInstrumentsPluginEditor() = default;

//==============================================================================
// Graphics
//==============================================================================

void GiantInstrumentsPluginEditor::paint(juce::Graphics& g)
{
    // Background
    g.fillAll(juce::Colours::black);

    // Title
    g.setColour(juce::Colours::white);
    g.setFont(juce::Font(24.0f, juce::Font::bold));
    g.drawText("GIANT INSTRUMENTS", getLocalBounds().removeFromTop(40),
               juce::Justification::centred, true);

    // Subtitle
    g.setFont(juce::Font(14.0f));
    g.drawText("Physical Modeling Giant Instruments with MPE & Microtonal Support",
               getLocalBounds().removeFromTop(60).withTrimmedTop(45),
               juce::Justification::centred, true);
}

void GiantInstrumentsPluginEditor::resized()
{
    auto area = getLocalBounds();
    area.removeFromTop(70); // Space for title

    // Top row: instrument selector and toggles
    auto topRow = area.removeFromTop(50);
    topRow.removeFromLeft(20);

    instrumentLabel->setBounds(topRow.removeFromLeft(120));
    instrumentSelector->setBounds(topRow.removeFromLeft(200));
    topRow.removeFromLeft(30);

    mpeLabel->setBounds(topRow.removeFromLeft(50));
    mpeEnableToggle->setBounds(topRow.removeFromLeft(60));
    topRow.removeFromLeft(20);

    microtonalLabel->setBounds(topRow.removeFromLeft(80));
    microtonalEnableToggle->setBounds(topRow.removeFromLeft(60));

    area.removeFromTop(20);

    // Second row: master volume and preset selector
    auto secondRow = area.removeFromTop(50);
    secondRow.removeFromLeft(20);

    masterVolumeLabel->setBounds(secondRow.removeFromLeft(120));
    masterVolumeSlider->setBounds(secondRow.removeFromLeft(250));
    secondRow.removeFromLeft(30);

    presetLabel->setBounds(secondRow.removeFromLeft(60));
    presetSelector->setBounds(secondRow.removeFromLeft(300));

    area.removeFromTop(20);

    // Info display (left side)
    auto leftArea = area.removeFromLeft(400);
    leftArea.removeFromLeft(20);
    infoDisplay->setBounds(leftArea.withTrimmedBottom(20));

    // Giant visual (right side)
    auto rightArea = area;
    rightArea.removeFromRight(20);
    giantVisual->setBounds(rightArea.withTrimmedBottom(20));
}

//==============================================================================
// Callbacks
//==============================================================================

void GiantInstrumentsPluginEditor::instrumentChanged()
{
    int selectedId = instrumentSelector->getSelectedId();
    auto newType = static_cast<GiantInstrumentType>(selectedId);
    processor.setInstrumentType(newType);

    updateInfoDisplay();
    refreshPresetList();
}

void GiantInstrumentsPluginEditor::updateInfoDisplay()
{
    juce::String info;

    // Current instrument
    info << "=== Current Instrument ===\n\n";
    info << "Type: " << GiantInstrumentsPluginProcessor::getInstrumentTypeName(processor.getInstrumentType()) << "\n\n";

    // Description based on instrument type
    switch (processor.getInstrumentType())
    {
        case GiantInstrumentType::GiantStrings:
            info << "Massive string ensemble with sympathetic coupling.\n";
            info << "Responds to MPE pressure for bow force, timbre for brightness.\n";
            info << "\nMPE Mapping:\n";
            info << "• Pressure → Bow force\n";
            info << "• Timbre → Filter brightness\n";
            info << "• Pitch Bend → Subtle detune\n";
            break;

        case GiantInstrumentType::GiantDrums:
            info << "Colossal drum synthesis with physical modeling.\n";
            info << "MPE pressure controls strike intensity.\n";
            info << "\nMPE Mapping:\n";
            info << "• Pressure → Strike force\n";
            info << "• Timbre → Drum shell resonance\n";
            info << "• Pitch Bend → Pitch (tuned drums)\n";
            break;

        case GiantInstrumentType::GiantVoice:
            info << "Massive vocal formant synthesis.\n";
            info << "Expressive MPE control over formants and vibrato.\n";
            info << "\nMPE Mapping:\n";
            info << "• Pressure → Vocal intensity\n";
            info << "• Timbre → Formant shift\n";
            info << "• Pitch Bend → Vibrato depth\n";
            break;

        case GiantInstrumentType::GiantHorns:
            info << "Titanic brass section with physical modeling.\n";
            info << "MPE pressure controls breath force.\n";
            info << "\nMPE Mapping:\n";
            info << "• Pressure → Breath force\n";
            info << "• Timbre → Mute brightness\n";
            info << "• Pitch Bend → Fall/ride extent\n";
            break;

        case GiantInstrumentType::GiantPercussion:
            info << "Mythic percussion instruments.\n";
            info << "Gongs, bells, and other resonant metal.\n";
            info << "\nMPE Mapping:\n";
            info << "• Pressure → Strike intensity\n";
            info << "• Timbre → Metallic brightness\n";
            info << "• Pitch Bend → Inharmonicity\n";
            break;
    }

    info << "\n=== Microtonal Support ===\n\n";
    info << "30+ built-in scales available:\n";
    info << "• 12-TET, 19-TET, 22-TET, 24-TET, 31-TET\n";
    info << "• Just Intonation (5-limit, 7-limit)\n";
    info << "• Meantone (quarter/third/fifth comma)\n";
    info << "• Pythagorean, Werckmeister, Vallotti\n";
    info << "• Bohlen-Pierce, Partials, Spectral\n";
    info << "• Indian Shruti, Arabic Maqaam\n";
    info << "• Scala file format support\n";

    infoDisplay->setText(info, false);
}

void GiantInstrumentsPluginEditor::refreshPresetList()
{
    presetSelector->clear();

    int numPrograms = processor.getNumPrograms();
    for (int i = 0; i < numPrograms; ++i)
    {
        juce::String name = processor.getProgramName(i);
        presetSelector->addItem(name, i + 1);
    }

    // Set current preset
    int currentProgram = processor.getCurrentProgram();
    if (currentProgram >= 0 && currentProgram < numPrograms)
    {
        presetSelector->setSelectedId(currentProgram + 1, juce::dontSendNotification);
    }
}
