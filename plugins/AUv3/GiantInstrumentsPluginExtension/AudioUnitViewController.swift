//
//  AudioUnitViewController.swift
//  GiantInstrumentsPluginExtension
//
//  SwiftUI view controller for Giant Instruments AUv3
//

import UIKit
import SwiftUI

class AudioUnitViewController: UIViewController {

    var audioUnit: AUAudioUnit?

    override func viewDidLoad() {
        super.viewDidLoad()

        // Create SwiftUI view
        let contentView = GiantInstrumentsUIView(audioUnit: audioUnit)
        let hostingController = UIHostingController(rootView: contentView)

        // Add as child view controller
        addChild(hostingController)
        view.addSubview(hostingController.view)

        // Setup constraints
        hostingController.view.translatesAutoresizingMaskIntoConstraints = false
        NSLayoutConstraint.activate([
            hostingController.view.topAnchor.constraint(equalTo: view.topAnchor),
            hostingController.view.leadingAnchor.constraint(equalTo: view.leadingAnchor),
            hostingController.view.trailingAnchor.constraint(equalTo: view.trailingAnchor),
            hostingController.view.bottomAnchor.constraint(equalTo: view.bottomAnchor)
        ])

        hostingController.didMove(toParent: self)
    }
}

// MARK: - SwiftUI View

struct GiantInstrumentsUIView: View {
    var audioUnit: AUAudioUnit?

    @State private var selectedInstrument = 0
    @State private var masterVolume: Double = 0.8

    // Giant parameters
    @State private var scaleMeters: Double = 8.0
    @State private var massBias: Double = 0.8
    @State private var airLoss: Double = 0.5
    @State private var transientSlowing: Double = 0.7
    @State private var distanceMeters: Double = 10.0
    @State private var roomSize: Double = 0.5
    @State private var stereoWidth: Double = 0.5

    // Gesture parameters
    @State private var force: Double = 0.6
    @State private var speed: Double = 0.5
    @State private var contactArea: Double = 0.5
    @State private var roughness: Double = 0.3

    // Voice-specific parameters
    @State private var aggression: Double = 0.5
    @State private var openness: Double = 0.5
    @State private var pitchInstability: Double = 0.3
    @State private var chaosAmount: Double = 0.2
    @State private var subharmonicMix: Double = 0.3
    @State private var chestFrequency: Double = 80.0

    let instruments = ["Giant Strings", "Giant Drums", "Giant Voice", "Giant Horns", "Giant Percussion"]

    var body: some View {
        ScrollView {
            VStack(spacing: 20) {
                // Header
                Text("Giant Instruments")
                    .font(.largeTitle)
                    .fontWeight(.bold)
                    .padding()

                // Instrument Selector
                VStack(alignment: .leading, spacing: 10) {
                    Text("Instrument")
                        .font(.headline)

                    Picker("Instrument", selection: $selectedInstrument) {
                        ForEach(0..<instruments.count, id: \.self) { index in
                            Text(instruments[index]).tag(index)
                        }
                    }
                    .pickerStyle(SegmentedPickerStyle())
                }
                .padding()
                .background(Color.gray.opacity(0.1))
                .cornerRadius(10)

                // Master Volume
                VStack(alignment: .leading, spacing: 10) {
                    Text("Master Volume: \(Int(masterVolume * 100))%")
                        .font(.headline)

                    Slider(value: $masterVolume, in: 0...1)
                        .onChange(of: masterVolume) { value in
                            setParameter("masterVolume", value: Float(value))
                        }
                }
                .padding()
                .background(Color.gray.opacity(0.1))
                .cornerRadius(10)

                // Giant Parameters
                VStack(alignment: .leading, spacing: 15) {
                    Text("Giant Parameters")
                        .font(.headline)

                    ParameterSlider(name: "Scale (m)", value: $scaleMeters, range: 0.1...100, paramId: "scaleMeters")
                    ParameterSlider(name: "Mass Bias", value: $massBias, range: 0...1, paramId: "massBias")
                    ParameterSlider(name: "Air Loss", value: $airLoss, range: 0...1, paramId: "airLoss")
                    ParameterSlider(name: "Transient Slowing", value: $transientSlowing, range: 0...1, paramId: "transientSlowing")
                    ParameterSlider(name: "Distance (m)", value: $distanceMeters, range: 1...100, paramId: "distanceMeters")
                    ParameterSlider(name: "Room Size", value: $roomSize, range: 0...1, paramId: "roomSize")
                    ParameterSlider(name: "Stereo Width", value: $stereoWidth, range: 0...1, paramId: "stereoWidth")
                }
                .padding()
                .background(Color.gray.opacity(0.1))
                .cornerRadius(10)

                // Gesture Parameters
                VStack(alignment: .leading, spacing: 15) {
                    Text("Gesture Parameters")
                        .font(.headline)

                    ParameterSlider(name: "Force", value: $force, range: 0...1, paramId: "force")
                    ParameterSlider(name: "Speed", value: $speed, range: 0...1, paramId: "speed")
                    ParameterSlider(name: "Contact Area", value: $contactArea, range: 0...1, paramId: "contactArea")
                    ParameterSlider(name: "Roughness", value: $roughness, range: 0...1, paramId: "roughness")
                }
                .padding()
                .background(Color.gray.opacity(0.1))
                .cornerRadius(10)

                // Voice-Specific Parameters (shown for Giant Voice)
                if selectedInstrument == 2 {
                    VStack(alignment: .leading, spacing: 15) {
                        Text("Voice Parameters")
                            .font(.headline)

                        ParameterSlider(name: "Aggression", value: $aggression, range: 0...1, paramId: "aggression")
                        ParameterSlider(name: "Openness", value: $openness, range: 0...1, paramId: "openness")
                        ParameterSlider(name: "Pitch Instability", value: $pitchInstability, range: 0...1, paramId: "pitchInstability")
                        ParameterSlider(name: "Chaos Amount", value: $chaosAmount, range: 0...1, paramId: "chaosAmount")
                        ParameterSlider(name: "Subharmonic Mix", value: $subharmonicMix, range: 0...1, paramId: "subharmonicMix")
                        ParameterSlider(name: "Chest Freq (Hz)", value: $chestFrequency, range: 20...200, paramId: "chestFrequency")
                    }
                    .padding()
                    .background(Color.orange.opacity(0.1))
                    .cornerRadius(10)
                }

                // Preset Management
                VStack(alignment: .leading, spacing: 10) {
                    Text("Presets")
                        .font(.headline)

                    HStack {
                        Button("Save Preset") {
                            // TODO: Implement preset saving
                        }
                        .padding()
                        .background(Color.blue)
                        .foregroundColor(.white)
                        .cornerRadius(8)

                        Button("Load Preset") {
                            // TODO: Implement preset loading
                        }
                        .padding()
                        .background(Color.green)
                        .foregroundColor(.white)
                        .cornerRadius(8)
                    }
                }
                .padding()
                .background(Color.gray.opacity(0.1))
                .cornerRadius(10)
            }
            .padding()
        }
    }

    private func setParameter(_ paramId: String, value: Float) {
        guard let audioUnit = audioUnit,
              let parameterTree = audioUnit.parameterTree else {
            return
        }

        for parameter in parameterTree.allParameters {
            if parameter.identifier == paramId {
                parameter.value = value
                break
            }
        }
    }
}

// MARK: - Parameter Slider Component

struct ParameterSlider: View {
    let name: String
    @Binding var value: Double
    let range: ClosedRange<Double>
    let paramId: String

    var body: some View {
        VStack(alignment: .leading, spacing: 5) {
            Text("\(name): \(formatValue(value))")
                .font(.caption)

            Slider(value: $value, in: range) {
                // TODO: Send parameter to audio unit
            }
        }
    }

    private func formatValue(_ value: Double) -> String {
        if range == 0...1 {
            return String(format: "%.0f%%", value * 100)
        } else if range.upperBound > 100 {
            return String(format: "%.1f", value)
        } else {
            return String(format: "%.2f", value)
        }
    }
}
