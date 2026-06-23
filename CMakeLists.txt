cmake_minimum_required(VERSION 3.22)

project(HitNoteDmx VERSION 0.1.0 LANGUAGES C CXX)

# HitNoteDmx ships for macOS: the ENTTEC driver uses IOKit (discovery) and
# POSIX termios (serial I/O). Non-Apple builds compile a no-op driver stub
# (see EnttecProDmx.cpp) so the GUI / aesthetics can be developed
# cross-platform; DMX output is disabled there.
if(NOT APPLE)
    message(STATUS "HitNoteDmx: non-Apple build - DMX driver is a no-op stub (GUI only).")
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
    FORMATS                     VST3 Standalone
    PRODUCT_NAME                "HitNoteDmx"
    DESCRIPTION                 "MIDI-driven DMX-512 lighting controller (VST3)"
    # Shaped as an INSTRUMENT, not an audio effect: it lives on a MIDI track as
    # the instrument and receives MIDI directly. We keep an audio output bus
    # (emits silence) so Live loads it happily — the pure MIDI-effect shape
    # (IS_MIDI_EFFECT=TRUE, no audio bus) failed to load in Live (see STATUS).
    IS_SYNTH                    TRUE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     TRUE
    VST3_CATEGORIES             "Instrument"
)

target_sources(HitNoteDmx PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/EnttecProDmx.cpp
    Source/MidiLog.cpp
    Source/MidiState.cpp
    Source/Recipes.cpp
    Source/Composition.cpp
    Source/DmxVisualizer.cpp
    Source/TriggerMenu.cpp
    Source/TriggerVocabulary.cpp
    Source/Showcase.cpp
)

# Showcase assets embedded in the binary (works identically for Standalone +
# VST3, no fragile bundle-path lookups). The embedded .adg is a key-gated
# TEMPLATE; its chains are named at runtime from the live vocabulary, and the
# demo MIDI clips are produced the same way (see Showcase.cpp).
juce_add_binary_data(HitNoteDmxData SOURCES
    "assets/showcase/Ableton Rack/Hitnotenames.adg"
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
    HitNoteDmxData
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_gui_basics
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

# IOKit + CoreFoundation back the macOS-only ENTTEC driver. Off-Apple the
# driver is stubbed (see EnttecProDmx.cpp), so these frameworks aren't linked.
if(APPLE)
    target_link_libraries(HitNoteDmx PRIVATE
        "-framework IOKit"
        "-framework CoreFoundation"
    )
endif()

# ---- mapping-tool -----------------------------------------------------------
# A console tool that links the plugin's TriggerVocabulary (the single source
# of truth for the note mapping). It dumps / verifies the frozen mapping
# snapshots in `mappings/`, and is where the clip migration converter + RGB
# importer will live. Reuses the vocabulary, so it can never drift.
juce_add_console_app(mapping-tool PRODUCT_NAME "mapping-tool")
target_sources(mapping-tool PRIVATE
    tools/MappingTool.cpp
    Source/TriggerVocabulary.cpp
)
target_include_directories(mapping-tool PRIVATE Source)
target_compile_definitions(mapping-tool PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
)
target_link_libraries(mapping-tool PRIVATE
    juce::juce_core
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)

# ---- recipe-render ----------------------------------------------------------
# A console tool that renders film-strip / contact-sheet PNG previews of the
# dynamic recipes by driving the REAL composition path (computeDmx) offline.
# Used to eyeball and iterate on recipes without launching the plugin in a host.
juce_add_console_app(recipe-render PRODUCT_NAME "recipe-render")
target_sources(recipe-render PRIVATE
    tools/RecipeRender.cpp
    Source/Recipes.cpp
    Source/Composition.cpp
    Source/MidiState.cpp
    Source/TriggerVocabulary.cpp
)
target_include_directories(recipe-render PRIVATE Source)
target_compile_definitions(recipe-render PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_STANDALONE_APPLICATION=1
)
target_link_libraries(recipe-render PRIVATE
    juce::juce_gui_basics
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)

# ---- recipe-check -----------------------------------------------------------
# A fast numeric regression net for the recipe engine: drives the REAL recipe
# functions + computeDmx() and fails on any non-finite / out-of-range output.
# Reuses the engine sources, so it can never drift from what the audio thread
# runs. Registered as the `recipe-range` CTest below.
juce_add_console_app(recipe-check PRODUCT_NAME "recipe-check")
target_sources(recipe-check PRIVATE
    tools/RecipeCheck.cpp
    Source/Recipes.cpp
    Source/Composition.cpp
    Source/MidiState.cpp
)
target_include_directories(recipe-check PRIVATE Source)
target_compile_definitions(recipe-check PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
)
target_link_libraries(recipe-check PRIVATE
    juce::juce_events
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)

# Guard against accidental mapping drift: the current vocabulary must match the
# frozen snapshot for kMappingVersion. If you changed a note's meaning on
# purpose, bump kMappingVersion and re-freeze (see mappings/README.md).
enable_testing()
add_test(NAME mapping-frozen
         COMMAND mapping-tool check "${CMAKE_CURRENT_SOURCE_DIR}/mappings")

# Guard the recipe engine: every recipe + computeDmx must stay finite and in
# range across a beat / velocity / density sweep (see tools/RecipeCheck.cpp).
add_test(NAME recipe-range COMMAND recipe-check)
