cmake_minimum_required(VERSION 3.22)
project(STSVarispeed VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(STS_BUILD_PLUGIN "Build the JUCE VST3 instrument" ON)
set(JUCE_DIR "" CACHE PATH "Path to a local JUCE checkout")

add_library(sts_playback_core STATIC
    ../../dsp/sts_playback_engine.c
)
target_include_directories(sts_playback_core PUBLIC ../../dsp)
set_target_properties(sts_playback_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

include(CTest)
if(BUILD_TESTING)
    add_executable(sts_playback_core_tests tests/STSPlaybackEngineTests.cpp)
    target_link_libraries(sts_playback_core_tests PRIVATE sts_playback_core)
    if(NOT MSVC)
        target_link_libraries(sts_playback_core_tests PRIVATE m)
    endif()
    add_test(NAME sts_playback_core_tests COMMAND sts_playback_core_tests)
endif()

if(STS_BUILD_PLUGIN)
    if(JUCE_DIR)
        add_subdirectory("${JUCE_DIR}" JUCE)
    else()
        include(FetchContent)
        FetchContent_Declare(JUCE
            GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
            GIT_TAG 9.0.1
            GIT_SHALLOW TRUE
        )
        FetchContent_MakeAvailable(JUCE)
    endif()

    juce_add_plugin(STSVarispeed
        COMPANY_NAME Schwung
        BUNDLE_ID com.schwung.stsvarispeed
        PLUGIN_MANUFACTURER_CODE Schw
        PLUGIN_CODE Stsv
        FORMATS VST3
        PRODUCT_NAME "STS Varispeed"
        IS_SYNTH TRUE
        NEEDS_MIDI_INPUT TRUE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        COPY_PLUGIN_AFTER_BUILD FALSE
    )

    target_sources(STSVarispeed PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginProcessor.h
        Source/PluginEditor.cpp
        Source/PluginEditor.h
    )
    target_link_libraries(STSVarispeed PRIVATE
        sts_playback_core
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_gui_extra
    )
    target_compile_definitions(STSVarispeed PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
    )
    juce_generate_juce_header(STSVarispeed)

    if(BUILD_TESTING)
        add_executable(sts_processor_tests tests/STSProcessorTests.cpp)
        target_include_directories(sts_processor_tests PRIVATE Source
            "${CMAKE_CURRENT_BINARY_DIR}/STSVarispeed_artefacts/JuceLibraryCode")
        target_link_libraries(sts_processor_tests PRIVATE STSVarispeed sts_playback_core
            juce::juce_audio_utils juce::juce_audio_processors juce::juce_gui_extra)
        add_test(NAME sts_processor_tests COMMAND sts_processor_tests)
    endif()
endif()
