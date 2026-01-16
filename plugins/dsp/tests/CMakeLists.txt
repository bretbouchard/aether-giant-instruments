cmake_minimum_required(VERSION 3.16)
project(AetherGiantVoiceComprehensiveTest)

# Find JUCE at different possible locations
set(JUCE_PATHS
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../external/JUCE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../external/JUCE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../external/JUCE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../../external/JUCE
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

# Add executable
add_executable(AetherGiantVoiceComprehensiveTest
    AetherGiantVoiceComprehensiveTest.cpp
    ../src/dsp/AetherGiantVoicePureDSP.cpp
)

# Include directories
target_include_directories(AetherGiantVoiceComprehensiveTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../include
    ${JUCE_PATH}/modules
)

# C++ standard
target_compile_features(AetherGiantVoiceComprehensiveTest PRIVATE cxx_std_17)

# Compile definitions
target_compile_definitions(AetherGiantVoiceComprehensiveTest PRIVATE
    JUCE_GLOBAL_RENAME_SETTINGS=1
    JUCE_STANDALONE_APPLICATION=1
    JUCE_USE_DSP_SIMD=1
    _DEBUG=1
    JUCE_MODULE_AVAILABLE_juce_core=1
    JUCE_MODULE_AVAILABLE_juce_dsp=1
    JUCE_MODULE_AVAILABLE_juce_data_structures=1
    JUCE_MODULE_AVAILABLE_juce_events=1
    JUCE_MODULE_AVAILABLE_juce_audio_basics=1
)

# Link libraries
target_link_libraries(AetherGiantVoiceComprehensiveTest PRIVATE
    "-framework Accelerate"
    "-framework CoreFoundation"
    "-framework CoreMIDI"
    "-framework CoreAudio"
)
