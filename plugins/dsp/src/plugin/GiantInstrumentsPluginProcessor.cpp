/*
  ==============================================================================

    GiantInstrumentsPluginProcessor.cpp
    Created: January 9, 2026
    Author:  Bret Bouchard

    JUCE AudioProcessor wrapper implementation for Giant Instruments

  ==============================================================================
*/

#include "GiantInstrumentsPluginProcessor.h"
#include "GiantInstrumentsPluginEditor.h"

//==============================================================================
// GiantInstrumentsPluginProcessor Implementation
//==============================================================================

GiantInstrumentsPluginProcessor::GiantInstrumentsPluginProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
     : juce::AudioProcessor(BusesProperties()
                     #if ! JucePlugin_IsMidiEffect
                      #if ! JucePlugin_IsSynth
                       .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      #endif
                       .withOutput ("Output",  juce::AudioChannelSet::stereo(), true)
                     #endif
                       )
#endif
{
    // Initialize MPE Support (Full MPE for Giant Instruments)
    mpeSupport = std::make_unique<MPEUniversalSupport>();

    // Configure MPE gesture mapping for Giant Instruments
    // Giant instruments respond to all MPE gestures with physical modeling
    MPEGestureMapping giantMapping;
    giantMapping.pressureToForce = 1.0f;        // Full force/excitation
    giantMapping.timbreToSpeed = 0.7f;          // Envelope times, LFO speed
    giantMapping.pitchBendToRoughness = 0.5f;   // Texture, detune
    giantMapping.timbreToContactArea = 0.8f;    // Filter brightness, resonance
    mpeSupport->setGestureMapping(giantMapping);

    // Initialize Microtonal Tuning Manager
    tuningManager = std::make_unique<MicrotonalTuningManager>();

    // Create initial instrument (Giant Strings as default)
    currentInstrument = createInstrument(instrumentType);

    // Load factory presets
    loadFactoryPresets();
}

GiantInstrumentsPluginProcessor::~GiantInstrumentsPluginProcessor() = default;

//==============================================================================
// AudioProcessor Interface
//==============================================================================

void GiantInstrumentsPluginProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::ScopedLock lock(dspLock);

    if (currentInstrument)
    {
        currentInstrument->prepare(sampleRate, samplesPerBlock);
    }

    // Prepare MPE support
    if (mpeSupport && mpeEnabled)
    {
        mpeSupport->prepare(sampleRate);
    }
}

void GiantInstrumentsPluginProcessor::releaseResources()
{
    juce::ScopedLock lock(dspLock);

    if (currentInstrument)
    {
        currentInstrument->reset();
    }
}

#ifndef JucePlugin_PreferredChannelConfigurations
bool GiantInstrumentsPluginProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    #if JucePlugin_IsMidiEffect
    juce::ignoreUnused(layouts);
    return true;
    #else
    // Support mono and stereo layouts
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    #if ! JucePlugin_IsSynth
    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;
    #endif

    return true;
    #endif
}
#endif

void GiantInstrumentsPluginProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                                    juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    // Clear output buffer
    buffer.clear();

    juce::ScopedLock lock(dspLock);

    if (!currentInstrument)
        return;

    // Process MPE first (before note handling)
    if (mpeSupport && mpeEnabled)
    {
        processMPE(midiMessages);
    }

    // Process MIDI events
    for (const auto metadata : midiMessages)
    {
        const auto message = metadata.getMessage();
        int samplePosition = metadata.samplePosition;

        if (message.isNoteOn())
        {
            int midiNote = message.getNoteNumber();
            int channel = message.getChannel();
            float velocity = message.getVelocity() / 127.0f;

            // Apply MPE gestures if available
            if (mpeSupport && mpeEnabled)
            {
                applyMPEToNote(midiNote, channel, currentInstrument.get());
            }

            // Convert to frequency with microtonal tuning if enabled
            float frequency = midiNote;
            if (microtonalEnabled && tuningManager)
            {
                frequency = tuningManager->getTuning().midiToFrequency(midiNote);
            }

            // Create note-on event
            DSP::ScheduledEvent event;
            event.type = DSP::ScheduledEvent::NOTE_ON;
            event.time = 0.0;
            event.sampleOffset = samplePosition;
            event.data.note.midiNote = midiNote;
            event.data.note.velocity = velocity;

            currentInstrument->handleEvent(event);
        }
        else if (message.isNoteOff())
        {
            DSP::ScheduledEvent event;
            event.type = DSP::ScheduledEvent::NOTE_OFF;
            event.time = 0.0;
            event.sampleOffset = samplePosition;
            event.data.note.midiNote = message.getNoteNumber();

            currentInstrument->handleEvent(event);
        }
        else if (message.isPitchWheel())
        {
            float pitchBendValue = (message.getPitchWheelValue() - 8192) / 8192.0f;

            DSP::ScheduledEvent event;
            event.type = DSP::ScheduledEvent::PITCH_BEND;
            event.time = 0.0;
            event.sampleOffset = samplePosition;
            event.data.pitchBend.bendValue = pitchBendValue;

            currentInstrument->handleEvent(event);
        }
        else if (message.isController())
        {
            DSP::ScheduledEvent event;
            event.type = DSP::ScheduledEvent::CONTROL_CHANGE;
            event.time = 0.0;
            event.sampleOffset = samplePosition;
            event.data.controlChange.controllerNumber = message.getControllerNumber();
            event.data.controlChange.value = message.getControllerValue() / 127.0f;

            currentInstrument->handleEvent(event);
        }
        else if (message.isChannelPressure())
        {
            DSP::ScheduledEvent event;
            event.type = DSP::ScheduledEvent::CHANNEL_PRESSURE;
            event.time = 0.0;
            event.sampleOffset = samplePosition;
            event.data.channelPressure.pressure = message.getChannelPressureValue() / 127.0f;

            currentInstrument->handleEvent(event);
        }
    }

    // Process audio through current instrument
    float* outputs[2] = { buffer.getWritePointer(0), buffer.getWritePointer(1) };
    currentInstrument->process(outputs, buffer.getNumChannels(), buffer.getNumSamples());
}

//==============================================================================
// AudioProcessorEditor Interface
//==============================================================================

juce::AudioProcessorEditor* GiantInstrumentsPluginProcessor::createEditor()
{
    // Generic editor for pluginval testing
    return new juce::GenericAudioProcessorEditor(*this);
}

bool GiantInstrumentsPluginProcessor::hasEditor() const
{
    return true;
}

//==============================================================================
// Plugin Information
//==============================================================================

const juce::String GiantInstrumentsPluginProcessor::getName() const
{
    return JucePlugin_Name;
}

bool GiantInstrumentsPluginProcessor::acceptsMidi() const
{
    return JucePlugin_WantsMidiInput;
}

bool GiantInstrumentsPluginProcessor::producesMidi() const
{
    return JucePlugin_ProducesMidiOutput;
}

bool GiantInstrumentsPluginProcessor::isMidiEffect() const
{
    return JucePlugin_IsMidiEffect;
}

double GiantInstrumentsPluginProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

//==============================================================================
// Program/Preset Management
//==============================================================================

int GiantInstrumentsPluginProcessor::getNumPrograms()
{
    return static_cast<int>(factoryPresets.size());
}

int GiantInstrumentsPluginProcessor::getCurrentProgram()
{
    return currentProgramIndex;
}

void GiantInstrumentsPluginProcessor::setCurrentProgram(int index)
{
    if (index >= 0 && index < static_cast<int>(factoryPresets.size()))
    {
        currentProgramIndex = index;

        const auto& preset = factoryPresets[index];

        // Switch instrument if needed
        if (preset.type != instrumentType)
        {
            setInstrumentType(preset.type);
        }

        // Load preset
        loadPresetFromFile(preset.filePath);
    }
}

const juce::String GiantInstrumentsPluginProcessor::getProgramName(int index)
{
    if (index >= 0 && index < static_cast<int>(factoryPresets.size()))
    {
        return factoryPresets[index].name;
    }
    return {};
}

void GiantInstrumentsPluginProcessor::changeProgramName(int index, const juce::String& newName)
{
    // Factory preset names are not editable
    juce::ignoreUnused(index, newName);
}

//==============================================================================
// State Management
//==============================================================================

void GiantInstrumentsPluginProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    // Create main XML element
    std::unique_ptr<juce::XmlElement> mainXml = std::make_unique<juce::XmlElement>("GiantInstrumentsState");

    // Save instrument type
    mainXml->setAttribute("instrumentType", static_cast<int>(instrumentType));

    // Save MPE state
    mainXml->setAttribute("mpeEnabled", mpeEnabled);
    if (mpeSupport)
    {
        auto mapping = mpeSupport->getGestureMapping();
        mainXml->setAttribute("pressureToForce", mapping.pressureToForce);
        mainXml->setAttribute("timbreToSpeed", mapping.timbreToSpeed);
        mainXml->setAttribute("pitchBendToRoughness", mapping.pitchBendToRoughness);
        mainXml->setAttribute("timbreToContactArea", mapping.timbreToContactArea);
    }

    // Save Microtonal state
    mainXml->setAttribute("microtonalEnabled", microtonalEnabled);
    if (tuningManager)
    {
        auto tuning = tuningManager->getTuning();
        mainXml->setAttribute("tuningSystem", static_cast<int>(tuning.system));
        mainXml->setAttribute("referenceFreq", tuning.rootFrequency);
        mainXml->setAttribute("referenceNote", tuning.rootNote);
    }

    // Save current preset index
    mainXml->setAttribute("currentPreset", currentProgramIndex);

    // Write to memory block
    juce::MemoryOutputStream stream(destData, false);
    mainXml->writeTo(stream);
}

void GiantInstrumentsPluginProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (sizeInBytes <= 0) return;

    // Parse XML
    std::unique_ptr<juce::XmlElement> mainXml(juce::XmlDocument::parse(
        juce::String::fromUTF8(static_cast<const char*>(data), sizeInBytes)));

    if (!mainXml || !mainXml->hasTagName("GiantInstrumentsState"))
    {
        return;
    }

    // Restore instrument type
    int instrumentInt = mainXml->getIntAttribute("instrumentType", 0);
    setInstrumentType(static_cast<GiantInstrumentType>(instrumentInt));

    // Restore MPE state
    mpeEnabled = mainXml->getBoolAttribute("mpeEnabled", true);
    if (mpeSupport)
    {
        MPEGestureMapping mapping;
        mapping.pressureToForce = mainXml->getDoubleAttribute("pressureToForce", 1.0);
        mapping.timbreToSpeed = mainXml->getDoubleAttribute("timbreToSpeed", 0.7);
        mapping.pitchBendToRoughness = mainXml->getDoubleAttribute("pitchBendToRoughness", 0.5);
        mapping.timbreToContactArea = mainXml->getDoubleAttribute("timbreToContactArea", 0.8);
        mpeSupport->setGestureMapping(mapping);
    }

    // Restore Microtonal state
    microtonalEnabled = mainXml->getBoolAttribute("microtonalEnabled", true);
    if (tuningManager)
    {
        MicrotonalTuning tuning;
        int systemInt = mainXml->getIntAttribute("tuningSystem",
            static_cast<int>(TuningSystem::EqualTemperament));
        tuning.system = static_cast<TuningSystem>(systemInt);
        tuning.rootFrequency = mainXml->getDoubleAttribute("referenceFreq", 440.0);
        tuning.rootNote = mainXml->getIntAttribute("referenceNote", 69);
        tuningManager->setTuning(tuning);
    }

    // Restore preset
    int presetIndex = mainXml->getIntAttribute("currentPreset", 0);
    setCurrentProgram(presetIndex);
}

//==============================================================================
// Giant Instrument Management
//==============================================================================

void GiantInstrumentsPluginProcessor::setInstrumentType(GiantInstrumentType type)
{
    if (type == instrumentType)
        return;

    switchInstrument(type);
}

juce::String GiantInstrumentsPluginProcessor::getInstrumentTypeName(GiantInstrumentType type)
{
    switch (type)
    {
        case GiantInstrumentType::GiantStrings:     return "Giant Strings";
        case GiantInstrumentType::GiantDrums:        return "Giant Drums";
        case GiantInstrumentType::GiantVoice:        return "Giant Voice";
        case GiantInstrumentType::GiantHorns:        return "Giant Horns";
        case GiantInstrumentType::GiantPercussion:   return "Giant Percussion";
        default:                                     return "Unknown";
    }
}

//==============================================================================
// Parameter Access
//==============================================================================

float GiantInstrumentsPluginProcessor::getParameter(const juce::String& name)
{
    if (currentInstrument)
    {
        return currentInstrument->getParameter(name.toStdString().c_str());
    }
    return 0.0f;
}

void GiantInstrumentsPluginProcessor::setParameter(const juce::String& name, float value)
{
    if (currentInstrument)
    {
        currentInstrument->setParameter(name.toStdString().c_str(), value);
    }
}

//==============================================================================
// Private Methods
//==============================================================================

std::unique_ptr<DSP::InstrumentDSP> GiantInstrumentsPluginProcessor::createInstrument(GiantInstrumentType type)
{
    switch (type)
    {
        case GiantInstrumentType::GiantStrings:
            return std::make_unique<DSP::KaneMarcoAetherStringPureDSP>();

        case GiantInstrumentType::GiantDrums:
            return std::make_unique<DSP::AetherGiantDrumsPureDSP>();

        case GiantInstrumentType::GiantVoice:
            return std::make_unique<DSP::AetherGiantVoicePureDSP>();

        case GiantInstrumentType::GiantHorns:
            return std::make_unique<DSP::AetherGiantHornsPureDSP>();

        case GiantInstrumentType::GiantPercussion:
            return std::make_unique<DSP::AetherGiantPercussionPureDSP>();

        default:
            return std::make_unique<DSP::KaneMarcoAetherStringPureDSP>();
    }
}

void GiantInstrumentsPluginProcessor::switchInstrument(GiantInstrumentType newType)
{
    if (newType == instrumentType)
        return;

    double sampleRate = getSampleRate();
    int blockSize = getBlockSize();

    // Create new instrument
    auto newInstrument = createInstrument(newType);

    // Prepare new instrument
    newInstrument->prepare(sampleRate, blockSize);

    // Swap (thread-safe with lock)
    {
        juce::ScopedLock lock(dspLock);
        currentInstrument = std::move(newInstrument);
        instrumentType = newType;
    }

    // Update host display
    updateHostDisplay();
}

void GiantInstrumentsPluginProcessor::loadFactoryPresets()
{
    // Get base presets folder
    juce::File presetsFolder = juce::File::getSpecialLocation(juce::File::currentExecutableFile)
        .getParentDirectory()
        .getChildFile("presets");

    // If that doesn't exist, try relative path
    if (!presetsFolder.exists())
    {
        presetsFolder = juce::File("../instruments/kane_marco/presets");
    }

    // Load presets for each giant instrument type
    std::vector<std::pair<GiantInstrumentType, juce::String>> instrumentFolders = {
        {GiantInstrumentType::GiantDrums, "KaneMarcoAetherGiantDrums"},
        {GiantInstrumentType::GiantHorns, "KaneMarcoAetherGiantHorns"},
        {GiantInstrumentType::GiantPercussion, "KaneMarcoAetherGiantPercussion"},
        {GiantInstrumentType::GiantVoice, "KaneMarcoAetherGiantVoice"}
    };

    for (const auto& [type, folderName] : instrumentFolders)
    {
        juce::File instrumentFolder = presetsFolder.getChildFile(folderName);

        if (instrumentFolder.exists())
        {
            juce::Array<juce::File> presetFiles;
            instrumentFolder.findChildFiles(presetFiles, false, "*.json");

            for (const auto& file : presetFiles)
            {
                PresetInfo preset;
                preset.name = file.getFileNameWithoutExtension();
                preset.filePath = file.getFullPathName();
                preset.type = type;
                factoryPresets.push_back(preset);
            }
        }
    }
}

juce::File GiantInstrumentsPluginProcessor::getPresetsFolder(GiantInstrumentType type) const
{
    juce::String folderName;

    switch (type)
    {
        case GiantInstrumentType::GiantStrings:
            folderName = "KaneMarcoAetherString";
            break;
        case GiantInstrumentType::GiantDrums:
            folderName = "KaneMarcoAetherGiantDrums";
            break;
        case GiantInstrumentType::GiantVoice:
            folderName = "KaneMarcoAetherGiantVoice";
            break;
        case GiantInstrumentType::GiantHorns:
            folderName = "KaneMarcoAetherGiantHorns";
            break;
        case GiantInstrumentType::GiantPercussion:
            folderName = "KaneMarcoAetherGiantPercussion";
            break;
    }

    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("Schillinger/Presets")
        .getChildFile(folderName);
}

bool GiantInstrumentsPluginProcessor::loadPresetFromFile(const juce::File& presetFile)
{
    if (!presetFile.existsAsFile())
        return false;

    // Read preset file
    juce::String presetContent = presetFile.loadFileAsString();

    if (!currentInstrument)
        return false;

    // Load preset into current instrument
    return currentInstrument->loadPreset(presetContent.toRawUTF8());
}

void GiantInstrumentsPluginProcessor::processMPE(const juce::MidiBuffer& midiMessages)
{
    if (!mpeSupport) return;

    // Process MPE messages to extract per-note gestures
    mpeSupport->processMIDI(midiMessages);
}

void GiantInstrumentsPluginProcessor::applyMPEToNote(int noteNumber, int midiChannel,
                                                      DSP::InstrumentDSP* dsp)
{
    if (!mpeSupport || !dsp) return;

    // Get MPE gestures for this note
    auto gestures = mpeSupport->getGestureValues(noteNumber, midiChannel);

    // Apply gestures to giant instrument parameters (Full MPE)
    // Force (pressure) → Excitation energy
    if (gestures.force >= 0.0f)
    {
        dsp->setParameter("force", gestures.force);
        dsp->setParameter("note_energy", gestures.force);
    }

    // Speed (timbre) → Envelope times, LFO speed
    if (gestures.speed >= 0.0f)
    {
        dsp->setParameter("speed", gestures.speed);
        dsp->setParameter("env_speed", gestures.speed);
    }

    // Contact Area (timbre) → Filter brightness, resonance
    if (gestures.contactArea >= 0.0f)
    {
        dsp->setParameter("contact_area", gestures.contactArea);
        dsp->setParameter("filter_brightness", gestures.contactArea);
    }

    // Roughness (pitch bend) → Texture, detune
    if (gestures.roughness >= 0.0f)
    {
        dsp->setParameter("roughness", gestures.roughness);
        dsp->setParameter("detune", gestures.roughness);
    }
}

//==============================================================================
// This creates new instances of the plugin
//==============================================================================

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new GiantInstrumentsPluginProcessor();
}
