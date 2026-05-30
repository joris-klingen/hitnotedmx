cmake_minimum_required(VERSION 3.22)

project(HitNoteDmx VERSION 0.1.0 LANGUAGES C CXX)

if(NOT APPLE)
    message(FATAL_ERROR "HitNoteDmx targets macOS only. Build on a Mac.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.4
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(JUCE)

# HitNoteDmx is HitDmx's MIDI-driven sibling: it accepts MIDI notes,
# translates them into chase/breathe/sparkle/etc. recipes at the audio
# rate, and writes the resulting per-channel DMX values out via the same
# ENTTEC USB Pro driver that powers HitDmx.
juce_add_plugin(HitNoteDmx
    COMPANY_NAME                "HitDmx"
    BUNDLE_ID                   "com.hitdmx.HitNoteDmx"
    PLUGIN_MANUFACTURER_CODE    "HtDx"
    PLUGIN_CODE                 "HnDm"
    FORMATS                     VST3
    PRODUCT_NAME                "HitNoteDmx"
    DESCRIPTION                 "MIDI-driven DMX-512 lighting controller (VST3)"
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     TRUE
    VST3_CATEGORIES             "Fx" "Tools"
)

target_sources(HitNoteDmx PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/EnttecProDmx.cpp
    Source/MidiLog.cpp
)

target_include_directories(HitNoteDmx PRIVATE
    Source
)

target_compile_definitions(HitNoteDmx PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(HitNoteDmx PRIVATE
    "-framework IOKit"
    "-framework CoreFoundation"
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_gui_basics
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)
