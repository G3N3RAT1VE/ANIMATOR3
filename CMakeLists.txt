cmake_minimum_required(VERSION 3.22)
project(RiqLive VERSION 0.3.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(JUCE_DIR "C:/JUCE" CACHE PATH "Path to JUCE source")
add_subdirectory(${JUCE_DIR} JUCE)

set(RIQ_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/StemEngine.cpp)

function(riq_setup target send)
    target_sources(${target} PRIVATE ${RIQ_SOURCES})
    target_compile_definitions(${target} PUBLIC
        RIQ_SEND=${send}
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)
    target_link_libraries(${target}
        PRIVATE
            juce::juce_audio_utils
            juce::juce_dsp
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
endfunction()

# Engine: instrument with Mix + 3 stem outputs (Ableton exposes multi-outs for instruments only)
juce_add_plugin(RiqLive
    COMPANY_NAME "CAWOK"
    PLUGIN_MANUFACTURER_CODE Cawk
    PLUGIN_CODE RqLv
    FORMATS VST3
    PRODUCT_NAME "AUTOPILOT"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Instrument Sampler)
riq_setup(RiqLive 0)

# Send: audio effect that streams its track into slot A/B/C
juce_add_plugin(RiqLiveSend
    COMPANY_NAME "CAWOK"
    PLUGIN_MANUFACTURER_CODE Cawk
    PLUGIN_CODE RqLs
    FORMATS VST3
    PRODUCT_NAME "AUTOPILOT Send"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx Tools)
riq_setup(RiqLiveSend 1)

option(RIQ_BUILD_TESTS "Build offline engine test" OFF)
if(RIQ_BUILD_TESTS)
    juce_add_console_app(RiqLiveTest PRODUCT_NAME "RiqLiveTest")
    target_sources(RiqLiveTest PRIVATE test/main.cpp ${RIQ_SOURCES})
    target_compile_definitions(RiqLiveTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
    target_link_libraries(RiqLiveTest PRIVATE juce::juce_audio_utils juce::juce_dsp
        juce::juce_recommended_config_flags)
endif()
