//
//  ParameterBridge.swift
//  GiantInstrumentsPluginExtension
//
//  Bridge between Swift UI and C++ DSP
//

import Foundation

/// Wrapper for the C++ GiantInstrumentsDSP class
class GiantInstrumentsDSPWrapper {

    private var dspPtr: OpaquePointer?

    init() {
        // Create C++ DSP instance
        dspPtr = GiantInstrumentsDSP_Create()
    }

    deinit {
        // Destroy C++ DSP instance
        if let ptr = dspPtr {
            GiantInstrumentsDSP_Destroy(ptr)
        }
    }

    // MARK: - Initialization

    func initialize(withSampleRate sampleRate: Double, maximumFramesToRender: Int32) {
        if let ptr = dspPtr {
            GiantInstrumentsDSP_Initialize(ptr, sampleRate, maximumFramesToRender)
        }
    }

    // MARK: - Processing

    func process(frameCount: UInt32,
                outputBufferList: UnsafeMutablePointer<AudioBufferList>,
                timestamp: UnsafePointer<AUEventSampleTime>) {
        if let ptr = dspPtr {
            GiantInstrumentsDSP_Process(ptr, frameCount, outputBufferList, timestamp)
        }
    }

    // MARK: - Parameters

    func setParameter(_ address: AUParameterAddress, value: Float) {
        if let ptr = dspPtr {
            GiantInstrumentsDSP_SetParameter(ptr, address, value)
        }
    }

    func getParameter(forAddress address: AUParameterAddress) -> Float {
        guard let ptr = dspPtr else { return 0.0 }
        return GiantInstrumentsDSP_GetParameter(ptr, address)
    }

    // MARK: - MIDI

    func handleMIDIEvent(_ message: [UInt8], messageSize: UInt8) {
        if let ptr = dspPtr {
            message.withUnsafeBufferPointer { buffer in
                if let baseAddress = buffer.baseAddress {
                    GiantInstrumentsDSP_HandleMIDIEvent(ptr, baseAddress, messageSize)
                }
            }
        }
    }

    // MARK: - Presets

    func setState(_ stateData: String) {
        if let ptr = dspPtr {
            stateData.withCString { cString in
                GiantInstrumentsDSP_SetState(ptr, cString)
            }
        }
    }

    func getState() -> String {
        guard let ptr = dspPtr else { return "{}" }
        let cString = GiantInstrumentsDSP_GetState(ptr)
        let state = String(cString: cString)
        // Assuming C++ returns a const char* that we don't need to free
        // If memory is allocated, we'd need a corresponding free function
        return state
    }
}

// MARK: - C Bridge Functions

/// C bridge functions to call into C++ DSP
/// These will be implemented in the C++ files

@_silgen_name("GiantInstrumentsDSP_Create")
func GiantInstrumentsDSP_Create() -> OpaquePointer

@_silgen_name("GiantInstrumentsDSP_Destroy")
func GiantInstrumentsDSP_Destroy(_ dsp: OpaquePointer)

@_silgen_name("GiantInstrumentsDSP_Initialize")
func GiantInstrumentsDSP_Initialize(_ dsp: OpaquePointer, _ sampleRate: Double, _ maximumFramesToRender: Int32)

@_silgen_name("GiantInstrumentsDSP_Process")
func GiantInstrumentsDSP_Process(_ dsp: OpaquePointer, _ frameCount: UInt32, _ outputBufferList: UnsafeMutablePointer<AudioBufferList>, _ timestamp: UnsafePointer<AUEventSampleTime>)

@_silgen_name("GiantInstrumentsDSP_SetParameter")
func GiantInstrumentsDSP_SetParameter(_ dsp: OpaquePointer, _ address: AUParameterAddress, _ value: Float)

@_silgen_name("GiantInstrumentsDSP_GetParameter")
func GiantInstrumentsDSP_GetParameter(_ dsp: OpaquePointer, _ address: AUParameterAddress) -> Float

@_silgen_name("GiantInstrumentsDSP_HandleMIDIEvent")
func GiantInstrumentsDSP_HandleMIDIEvent(_ dsp: OpaquePointer, _ message: UnsafePointer<UInt8>, _ messageSize: UInt8)

@_silgen_name("GiantInstrumentsDSP_SetState")
func GiantInstrumentsDSP_SetState(_ dsp: OpaquePointer, _ stateData: UnsafePointer<CChar>)

@_silgen_name("GiantInstrumentsDSP_GetState")
func GiantInstrumentsDSP_GetState(_ dsp: OpaquePointer) -> UnsafePointer<CChar>
