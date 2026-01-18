# White Room Giant Instruments AUv3 iOS Plugin

## Project Structure

```
ios-auv3/
├── GiantInstrumentsPluginApp/            # AUv3 host container app
│   ├── AppDelegate.swift
│   ├── ViewController.swift
│   └── Info.plist
├── GiantInstrumentsPluginExtension/      # AUv3 extension
│   ├── AudioUnit.swift
│   ├── AudioUnitViewController.swift
│   ├── ParameterBridge.swift
│   └── Info.plist
└── SharedDSP/                            # C++ static library
    ├── GiantInstrumentsDSP.h
    ├── GiantInstrumentsDSP.cpp
    └── CMakeLists.txt
```

## Features

- **Platform**: iOS (AUv3 Instrument Extension)
- **DSP**: Giant Instruments multi-engine (5 instruments)
  - GiantStrings (Kane Marco Aether String)
  - GiantDrums (Aether Giant Drums)
  - GiantVoice (Aether Giant Voice)
  - GiantHorns (Aether Giant Horns)
  - GiantPercussion (Aether Giant Percussion)
- **Controls**: Giant Gesture System (5D control)
  - Force: Energy applied (0.0 - 1.0)
  - Speed: Gesture velocity (0.0 - 1.0)
  - Contact Area: Surface involvement (0.0 - 1.0)
  - Roughness: Surface texture (0.0 - 1.0)
  - Aggression (voice only): Vocal intensity (0.0 - 1.0)
  - Openness (voice only): Mouth aperture (0.0 - 1.0)
- **Scale Physics**: Scale-aware physics (scale_meters parameter)
- **Presets**: JSON-based preset management
- **UI**: SwiftUI interface optimized for touch

## Building

```bash
# Open in Xcode
open ios-auv3/GiantInstrumentsPlugin.xcodeproj

# Or build from command line
xcodebuild -project GiantInstrumentsPlugin.xcodeproj \
           -scheme GiantInstrumentsPlugin \
           -configuration Release \
           -sdk iphoneos \
           -derivedDataPath build
```

## Deployment Target

- **iOS**: 15.0+
- **Architectures**: arm64 (device), arm64-sim (simulator)

## Notes

- DSP compiled as static C++ library
- No network operations (iOS extension sandbox)
- No background agents or LangGraph
- Pure audio/MIDI processing

## Architecture

The Giant Instruments AUv3 plugin consists of three main components:

1. **Host App**: Minimal iOS app that hosts the AUv3 extension
2. **AUv3 Extension**: The actual audio plugin that processes MIDI/Audio
3. **Shared DSP**: C++ library containing the Giant Instruments DSP implementation

### DSP Integration

The C++ DSP code from the giant_instruments instrument is compiled as a static library and linked into both the host app and the AUv3 extension. This ensures:

- Single source of truth for DSP algorithms
- Efficient performance (native C++)
- Code reuse between iOS and desktop versions

### Giant Instruments DSP Architecture

The giant_instruments bundle contains 5 distinct DSP engines, each with unique characteristics:

#### 1. GiantStrings (Kane Marco Aether String)
- String synthesis with physical modeling
- Giant scale physics (delayed response, momentum)
- Formant filters for body resonance
- Scale-aware frequency scaling

#### 2. GiantDrums (Aether Giant Drums)
- Percussive synthesis with membrane modeling
- Strike force and velocity sensitivity
- Giant resonator (huge body, slow decay)
- Distance/air absorption

#### 3. GiantVoice (Aether Giant Voice)
- Mythic vocal synthesis (NOT speech)
- Breath pressure generator with turbulence
- Vocal fold oscillator (nonlinear, chaos)
- Giant formant cavities (3-5 bandpass filters)
- Subharmonic generator (octave/fifth down)
- Chest/body resonator

#### 4. GiantHorns (Aether Giant Horns)
- Brass synthesis with bore modeling
- Breath pressure and lip excitation
- Giant bell flare and resonances
- Distance/air absorption

#### 5. GiantPercussion (Aether Giant Percussion)
- Mallet/strike percussion synthesis
- Multiple percussion engine types
- Giant resonator body
- Scale-aware decay and resonance

### Parameter Mapping

The iOS UI maps to the following DSP parameter categories:

#### Giant Parameters (All Instruments)
- `scaleMeters`: Physical scale (0.1 to 100.0 meters)
- `massBias`: Mass multiplier (0.0 = light, 1.0 = heavy)
- `airLoss`: High-frequency air absorption (0.0 to 1.0)
- `transientSlowing`: Attack time multiplier (0.0 to 1.0)
- `distanceMeters`: Listener distance (1.0 to 100.0 meters)
- `roomSize`: Room size (0.0 = dry, 1.0 = cathedral)
- `stereoWidth`: Stereo width (0.0 = mono, 1.0 = full)

#### Gesture Parameters (All Instruments)
- `force`: Energy applied (0.0 to 1.0)
- `speed`: Gesture velocity (0.0 to 1.0)
- `contactArea`: Surface involvement (0.0 to 1.0)
- `roughness`: Surface texture (0.0 to 1.0)

#### Voice-Specific Parameters
- `aggression`: Vocal intensity (0.0 to 1.0)
- `openness`: Mouth aperture (0.0 to 1.0)
- `pitchInstability`: Pitch random variation (0.0 to 1.0)
- `chaosAmount`: Chaos at high pressure (0.0 to 1.0)
- `waveformMorph`: Saw (0.0) to pulse (1.0)
- `subharmonicMix`: Subharmonic content (0.0 to 1.0)
- `vowelOpenness`: Vowel openness (0.0 to 1.0)
- `formantDrift`: Formant drift speed (0.0 to 1.0)
- `chestFrequency`: Chest resonance (20 to 200 Hz)
- `chestResonance`: Chest Q factor (0.0 to 1.0)
- `bodySize`: Body size (0.0 = small, 1.0 = massive)

#### Global Parameters
- `masterVolume`: Master volume (0.0 to 1.0)
- `instrumentType`: Instrument selector (0-4)

---

**Status**: Created 2025-01-17
**Location**: juce_backend/instruments/giant_instruments/ios-auv3/
**Template**: Based on localgal/ios-auv3/
