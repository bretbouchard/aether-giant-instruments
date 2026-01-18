cmake_minimum_required(VERSION 3.15)
project(GiantInstruments VERSION 1.0.0 LANGUAGES CXX)

# ============================================================================
# JUCE Setup
# ============================================================================

# Find JUCE at different possible locations
set(JUCE_PATHS
    ${CMAKE_CURRENT_SOURCE_DIR}/../external/JUCE
    ${CMAKE_CURRENT_SOURCE_DIR}/external/JUCE
    ~/JUCE
    /usr/local/JUCE
)

set(JUCE_PATH "")
foreach(PATH ${JUCE_PATHS})
    if(EXISTS ${PATH})
        set(JUCE_PATH ${PATH})
        break()
    endif()
endforeach()

if(NOT JUCE_PATH OR NOT EXISTS ${JUCE_PATH})
    message(FATAL_ERROR "JUCE not found - required for GiantInstruments")
endif()

message(STATUS "Found JUCE at: ${JUCE_PATH}")

# Add JUCE modules
set(JUCE_MODULES_DIR ${JUCE_PATH}/modules)
add_subdirectory(${JUCE_MODULES_DIR} JUCE)

# ============================================================================
# Plugin Formats
# ============================================================================

# Build all 7 formats (DSP is pure implementation, not a plugin format)
set(JUCE_FORMATS
    VST3
    AU
    CLAP
    LV2
    Standalone
)

# ============================================================================
# Plugin Source Files
# ============================================================================

# DSP source files (pure implementation, no AudioProcessor wrapper)
set(DSP_SRC
    plugins/dsp/src/dsp/AetherGiantDrumsPureDSP.cpp
    plugins/dsp/src/dsp/AetherGiantHornsPureDSP.cpp
    plugins/dsp/src/dsp/AetherGiantPercussionPureDSP.cpp
    plugins/dsp/src/dsp/AetherGiantVoicePureDSP.cpp
    plugins/dsp/src/dsp/GiantInstrumentStereo.cpp
)

# Plugin wrapper source files
set(PLUGIN_SRC
    plugins/dsp/src/plugin/GiantInstrumentsPluginProcessor.cpp
    plugins/dsp/src/plugin/GiantInstrumentsPluginEditor.cpp
)

# All source files
set(ALL_SRC ${DSP_SRC} ${PLUGIN_SRC})

# ============================================================================
# Include Directories
# ============================================================================

set(GIANT_INSTRUMENTS_INCLUDE_DIRS
    plugins/dsp/include
    plugins/dsp/src/shared
    ${JUCE_MODULES_DIR}
)

# ============================================================================
# Create Plugin Target (VST3, AU, CLAP, LV2, Standalone)
# ============================================================================

juce_add_plugin(GiantInstruments
    COMPANY_NAME "White Room Audio"
    PLUGIN_IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_AUDIO_EFFECT_PROCESSOR FALSE
    PLUGIN_FORMAT ${JUCE_FORMATS}
    FORMATS ${JUCE_FORMATS}
    PRODUCT_NAME "GiantInstruments"
    FILE_DESCRIPTION "Giant-scale virtual instruments bundle"
)

# Source files
target_sources(GiantInstruments PRIVATE ${ALL_SRC})

# Include directories
target_include_directories(GiantInstruments PRIVATE ${GIANT_INSTRUMENTS_INCLUDE_DIRS})

# Link JUCE modules
target_link_libraries(GiantInstruments
    PRIVATE
        juce::juce_audio_plugin_client
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
)

# ============================================================================
# Installation
# ============================================================================

# Install VST3 plugin
if(TARGET GiantInstruments_VST3)
    install(TARGETS GiantInstruments_VST3
        DESTINATION plugins/vst
        COMPONENT VST3
    )
endif()

# Install AU plugin
if(TARGET GiantInstruments_AU)
    install(TARGETS GiantInstruments_AU
        DESTINATION plugins/au
        COMPONENT AU
    )
endif()

# Install CLAP plugin
if(TARGET GiantInstruments_CLAP)
    install(TARGETS GiantInstruments_CLAP
        DESTINATION plugins/clap
        COMPONENT CLAP
    )
endif()

# Install LV2 plugin
if(TARGET GiantInstruments_LV2)
    install(TARGETS GiantInstruments_LV2
        DESTINATION plugins/lv2
        COMPONENT LV2
    )
endif()

# Install Standalone app
if(TARGET GiantInstruments_Standalone)
    install(TARGETS GiantInstruments_Standalone
        DESTINATION plugins/standalone
        COMPONENT Standalone
    )
endif()

# ============================================================================
# Build Summary
# ============================================================================

message(STATUS "")
message(STATUS "============================================================================")
message(STATUS "Giant Instruments Plugin Configuration")
message(STATUS "============================================================================")
message(STATUS "Formats: ${JUCE_FORMATS}")
message(STATUS "Instruments:")
message(STATUS "  - Giant Drums (Aether Giant Drums)")
message(STATUS "  - Giant Horns (Aether Giant Horns)")
message(STATUS "  - Giant Percussion (Aether Giant Percussion)")
message(STATUS "  - Giant Voice (Aether Giant Voice)")
message(STATUS "  - Giant Strings (Kane Marco Aether String)")
message(STATUS "============================================================================")
message(STATUS "")
