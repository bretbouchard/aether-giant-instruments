//
//  GiantInstrumentsDSP.h
//  SharedDSP
//
//  C++ wrapper for Giant Instruments DSP
//  Provides interface for AUv3 extension to access DSP functionality
//

#ifndef GiantInstrumentsDSP_h
#define GiantInstrumentsDSP_h

#include <AudioToolbox/AudioToolbox.h>
#include <memory>

// Giant Instrument Type
enum class GiantInstrumentType
{
    GiantStrings = 0,
    GiantDrums,
    GiantVoice,
    GiantHorns,
    GiantPercussion
};

class GiantInstrumentsDSP {
public:
    GiantInstrumentsDSP();
    ~GiantInstrumentsDSP();

    // Initialization
    void initialize(double sampleRate, int maximumFramesToRender);

    // DSP Processing
    void process(AUAudioFrameCount frameCount,
                AudioBufferList *outputBufferList,
                const AUEventSampleTime *timestamp,
                AUAudioFrameCount inputBusNumber = 0);

    // Instrument Selection
    void setInstrumentType(GiantInstrumentType type);
    GiantInstrumentType getInstrumentType() const;

    // Parameters
    void setParameter(AUParameterAddress address, float value);
    float getParameter(AUParameterAddress address) const;

    // MIDI
    void handleMIDIEvent(const uint8_t *message, uint8_t messageSize);

    // Presets
    void setState(const char *stateData);
    const char *getState() const;

    // Parameter Addresses (must match AudioUnit.swift)
    enum ParameterAddresses : AUParameterAddress
    {
        // Giant Parameters (All Instruments)
        ScaleMeters = 0,
        MassBias,
        AirLoss,
        TransientSlowing,
        DistanceMeters,
        RoomSize,
        Temperature,
        Humidity,
        StereoWidth,
        StereoModeOffset,
        OddEvenSeparation,

        // Gesture Parameters (All Instruments)
        Force,
        Speed,
        ContactArea,
        Roughness,

        // Voice-Specific Parameters
        Aggression,
        Openness,
        PitchInstability,
        ChaosAmount,
        WaveformMorph,
        SubharmonicMix,
        VowelOpenness,
        FormantDrift,
        GiantScale,
        ChestFrequency,
        ChestResonance,
        BodySize,

        // Breath/Pressure Parameters
        BreathAttack,
        BreathSustain,
        BreathRelease,
        Turbulence,
        PressureOvershoot,

        // Global Parameters
        MasterVolume,
        InstrumentType,

        // Total count
        ParameterCount
    };

private:
    class Impl;
    std::unique_ptr<Impl> impl;
};

#endif /* GiantInstrumentsDSP_h */
