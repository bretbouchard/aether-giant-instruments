//
//  AudioUnit.swift
//  GiantInstrumentsPluginExtension
//
//  Main AUv3 audio unit implementation for Giant Instruments
//

import AudioToolbox

@objc(AudioUnit)
public class AudioUnit: AUAudioUnit {

    var dsp: GiantInstrumentsDSPWrapper?
    var parameterTree: AUParameterTree!

    // Instrument Type Enum
    enum InstrumentType: Int {
        case giantStrings = 0
        case giantDrums
        case giantVoice
        case giantHorns
        case giantPercussion

        var name: String {
            switch self {
            case .giantStrings: return "Giant Strings"
            case .giantDrums: return "Giant Drums"
            case .giantVoice: return "Giant Voice"
            case .giantHorns: return "Giant Horns"
            case .giantPercussion: return "Giant Percussion"
            }
        }
    }

    // Giant Parameters (All Instruments)
    private let giantParameters: [AUParameterIdentifier: (name: String, range: ClosedRange<Float>, unit: AUUnitParameterUnit, defaultValue: Float)] = [
        "scaleMeters": ("Scale (m)", 0.1...100.0, .meters, 8.0),
        "massBias": ("Mass Bias", 0.0...1.0, .generic, 0.8),
        "airLoss": ("Air Loss", 0.0...1.0, .generic, 0.5),
        "transientSlowing": ("Transient Slowing", 0.0...1.0, .generic, 0.7),
        "distanceMeters": ("Distance (m)", 1.0...100.0, .meters, 10.0),
        "roomSize": ("Room Size", 0.0...1.0, .generic, 0.5),
        "temperature": ("Temperature (°C)", -20.0...50.0, .celsius, 20.0),
        "humidity": ("Humidity", 0.0...1.0, .generic, 0.5),
        "stereoWidth": ("Stereo Width", 0.0...1.0, .generic, 0.5),
        "stereoModeOffset": ("Stereo Mode Offset", 0.0...0.1, .generic, 0.02),
        "oddEvenSeparation": ("Odd/Even Separation", 0.0...1.0, .boolean, 1.0)
    ]

    // Gesture Parameters (All Instruments)
    private let gestureParameters: [AUParameterIdentifier: (name: String, range: ClosedRange<Float>, unit: AUUnitParameterUnit, defaultValue: Float)] = [
        "force": ("Force", 0.0...1.0, .generic, 0.6),
        "speed": ("Speed", 0.0...1.0, .generic, 0.5),
        "contactArea": ("Contact Area", 0.0...1.0, .generic, 0.5),
        "roughness": ("Roughness", 0.0...1.0, .generic, 0.3)
    ]

    // Voice-Specific Parameters
    private let voiceParameters: [AUParameterIdentifier: (name: String, range: ClosedRange<Float>, unit: AUUnitParameterUnit, defaultValue: Float)] = [
        "aggression": ("Aggression", 0.0...1.0, .generic, 0.5),
        "openness": ("Openness", 0.0...1.0, .generic, 0.5),
        "pitchInstability": ("Pitch Instability", 0.0...1.0, .generic, 0.3),
        "chaosAmount": ("Chaos Amount", 0.0...1.0, .generic, 0.2),
        "waveformMorph": ("Waveform Morph", 0.0...1.0, .generic, 0.5),
        "subharmonicMix": ("Subharmonic Mix", 0.0...1.0, .generic, 0.3),
        "vowelOpenness": ("Vowel Openness", 0.0...1.0, .generic, 0.5),
        "formantDrift": ("Formant Drift", 0.0...1.0, .generic, 0.1),
        "giantScale": ("Giant Scale", 0.0...1.0, .generic, 0.6),
        "chestFrequency": ("Chest Freq (Hz)", 20.0...200.0, .hertz, 80.0),
        "chestResonance": ("Chest Resonance", 0.0...1.0, .generic, 0.7),
        "bodySize": ("Body Size", 0.0...1.0, .generic, 0.5)
    ]

    // Breath/Pressure Parameters
    private let breathParameters: [AUParameterIdentifier: (name: String, range: ClosedRange<Float>, unit: AUUnitParameterUnit, defaultValue: Float)] = [
        "breathAttack": ("Breath Attack (s)", 0.01...2.0, .seconds, 0.1),
        "breathSustain": ("Breath Sustain", 0.0...1.0, .generic, 0.7),
        "breathRelease": ("Breath Release (s)", 0.01...2.0, .seconds, 0.3),
        "turbulence": ("Turbulence", 0.0...1.0, .generic, 0.2),
        "pressureOvershoot": ("Pressure Overshoot", 0.0...1.0, .generic, 0.2)
    ]

    // Global Parameters
    private let globalParameters: [AUParameterIdentifier: (name: String, range: ClosedRange<Float>, unit: AUUnitParameterUnit, defaultValue: Float)] = [
        "masterVolume": ("Master Volume", 0.0...1.0, .generic, 0.8),
        "instrumentType": ("Instrument Type", 0.0...4.0, .indexed, 0.0)
    ]

    public override init(componentDescription: AudioComponentDescription,
                         options: AudioComponentInstantiationOptions) throws {
        try super.init(componentDescription: componentDescription, options: options)

        // Initialize DSP wrapper
        dsp = GiantInstrumentsDSPWrapper()

        // Create parameter tree
        parameterTree = AUParameterTree()

        // Register all parameters
        registerParameterGroup(name: "Giant", parameters: giantParameters)
        registerParameterGroup(name: "Gesture", parameters: gestureParameters)
        registerParameterGroup(name: "Voice", parameters: voiceParameters)
        registerParameterGroup(name: "Breath", parameters: breathParameters)
        registerParameterGroup(name: "Global", parameters: globalParameters)

        // Default parameter values
        parameterTree.implementorValueProvider = { [weak self] address in
            guard let self = self else { return 0.0 }
            return self.dsp?.getParameter(forAddress: address) ?? 0.5
        }

        parameterTree.implementorStringFromValueProvider = { parameter, value in
            guard let parameter = parameter else { return "" }

            // Special handling for instrument type
            if parameter.identifier == "instrumentType" {
                let type = InstrumentType(rawValue: Int(value)) ?? .giantStrings
                return type.name
            }

            // Special handling for boolean
            if parameter.unit == .boolean {
                return value > 0.5 ? "On" : "Off"
            }

            // Special handling for frequency
            if parameter.unit == .hertz {
                return String(format: "%.1f Hz", value)
            }

            // Special handling for meters
            if parameter.unit == .meters {
                return String(format: "%.1f m", value)
            }

            // Special handling for seconds
            if parameter.unit == .seconds {
                return String(format: "%.2f s", value)
            }

            // Special handling for celsius
            if parameter.unit == .celsius {
                return String(format: "%.1f °C", value)
            }

            // Default percentage
            return String(format: "%.1f%%", value * 100.0)
        }

        parameterTree.implementorValueObserver = { [weak self] address, value in
            self?.dsp?.setParameter(address, value: value)
        }
    }

    private func registerParameterGroup(name: String,
                                       parameters: [AUParameterIdentifier: (name: String, range: ClosedRange<Float>, unit: AUUnitParameterUnit, defaultValue: Float)]) {
        let group = AUParameterGroup(
            identifier: name,
            name: name,
            children: []

        )

        for (identifier, info) in parameters {
            let parameter = AUParameter(
                identifier: identifier,
                name: info.name,
                address: AUParameterAddress(identifier.hashValue),
                range: info.range,
                unit: info.unit,
                flags: [.flag_IsReadable, .flag_IsWritable, .flag_HasClamp],
                valueStrings: nil,
                subordinateParameters: nil
            )
            parameter.setValue(info.defaultValue)
            parameterTree.registerParameter(parameter)
        }
    }

    public override func allocateRenderResources() throws {
        try super.allocateRenderResources()

        // Initialize DSP with current format
        if let dsp = dsp {
            let format = self.outputBusses[0].format
            dsp.initialize(withSampleRate: format.sampleRate,
                          maximumFramesToRender: Int32(self.maximumFramesToRender))
        }
    }

    public override func deallocateRenderResources() {
        super.deallocateRenderResources()
    }

    public override var internalRenderBlock: AUInternalRenderBlock {
        return { [weak self] timestamp, frameCount, outputBusNumber, outputBufferList, inputBusNumber, events in
            guard let self = self else {
                return kAudioUnitErr_InvalidProperty
            }

            // Process events (MIDI, parameters)
            for event in events {
                self.handleEvent(event)
            }

            // Render audio
            self.dsp?.process(frameCount: frameCount,
                            outputBufferList: outputBufferList,
                            timestamp: timestamp)

            return noErr
        }
    }

    private func handleEvent(_ event: AURenderEvent) {
        switch event.head.eventType {
        case .MIDI:
            if let midiEvent = event.MIDI {
                self.handleMIDI(midiEvent)
            }
        case .parameter:
            if let parameterEvent = event.parameter {
                self.handleParameter(parameterEvent)
            }
        default:
            break
        }
    }

    private func handleMIDI(_ event: AUMIDIEvent) {
        var message = [UInt8](repeating: 0, count: Int(event.length))
        message.withMutableBufferPointer { buffer in
            if let baseAddress = buffer.baseAddress {
                event.getData(&baseAddress.pointee)
                self.dsp?.handleMIDIEvent(message, messageSize: UInt8(event.length))
            }
        }
    }

    private func handleParameter(_ event: AUParameterEvent) {
        dsp?.setParameter(event.parameterAddress, value: event.value)
    }
}
