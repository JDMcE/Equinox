cmake_minimum_required(VERSION 3.22)

project(eeQ VERSION 0.5.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE is vendored in libs/JUCE (cloned at tag 8.0.14)
add_subdirectory(libs/JUCE)

# Family-shared components (design language, presets).
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../jdm_shared" jdm_shared)

# CLAP support is vendored in libs/clap-juce-extensions (with clap submodules)
add_subdirectory(libs/clap-juce-extensions EXCLUDE_FROM_ALL)

juce_add_plugin(eeQ
    COMPANY_NAME "Equinox Audio"
    BUNDLE_ID com.equinoxaudio.equinox
    PLUGIN_MANUFACTURER_CODE Eeqa
    PLUGIN_CODE Eeq1
    FORMATS VST3 Standalone
    PRODUCT_NAME "Equinox"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx EQ)

clap_juce_extensions_plugin(TARGET eeQ
    CLAP_ID "com.equinoxaudio.equinox"
    CLAP_FEATURES audio-effect equalizer stereo
    # Deliver CLAP parameter events in 32-sample slices, matching the
    # processor's sub-block interpolation resolution.
    CLAP_PROCESS_EVENTS_RESOLUTION_SAMPLES 32)

target_sources(eeQ PRIVATE
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
    src/ui/EqDisplay.cpp
    src/ui/BandPanel.cpp)

target_include_directories(eeQ PRIVATE src)

target_compile_definitions(eeQ PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(eeQ
    PRIVATE
        jdm_shared
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_opengl
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Offline DSP test harness (console app around the processor sources).
juce_add_console_app(eeq_tests PRODUCT_NAME "eeq_tests")

target_sources(eeq_tests PRIVATE
    test/DspTests.cpp
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
    src/ui/EqDisplay.cpp
    src/ui/BandPanel.cpp)

target_include_directories(eeq_tests PRIVATE src)

target_compile_definitions(eeq_tests PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_MODAL_LOOPS_PERMITTED=0)

target_link_libraries(eeq_tests
    PRIVATE
        jdm_shared
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_opengl
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
