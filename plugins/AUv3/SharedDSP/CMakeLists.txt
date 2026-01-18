cmake_minimum_required(VERSION 3.15)
project(GiantInstrumentsDSP)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Create static library for DSP
add_library(GiantInstrumentsDSP STATIC
    GiantInstrumentsDSP.h
    GiantInstrumentsDSP.cpp
)

# Include directories
target_include_directories(GiantInstrumentsDSP PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    # Add path to giant_instruments DSP headers
    ${CMAKE_CURRENT_SOURCE_DIR}/../../plugins/dsp/include
    # Add path to JUCE (if needed)
    # ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../external/JUCE/modules
)

# Link libraries
# target_link_libraries(GiantInstrumentsDSP
#     Add any required JUCE modules here
# )

# iOS specific settings
if(IOS)
    set_target_properties(GiantInstrumentsDSP PROPERTIES
        MACOSX_BUNDLE ON
        IOS_INSTALL_COMBINED ON
    )
endif()

# Compiler flags
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(GiantInstrumentsDSP PRIVATE
        -fobjc-arc
        -fvisibility=hidden
    )
endif()
