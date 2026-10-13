cmake_minimum_required(VERSION 3.22)
project(SpectralFoundry VERSION 2.0.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(JUCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/JUCE" CACHE PATH "JUCE source directory")
add_subdirectory("${JUCE_DIR}" JUCE-build)
juce_add_plugin(SpectralFoundry
    COMPANY_NAME "Foundry Audio" BUNDLE_ID "audio.foundry.spectral"
    PLUGIN_MANUFACTURER_CODE Fdry PLUGIN_CODE Sfr1
    FORMATS VST3 PRODUCT_NAME "Spectral Foundry"
    IS_SYNTH FALSE NEEDS_MIDI_INPUT FALSE NEEDS_MIDI_OUTPUT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE VST3_CATEGORIES Fx Filter)
target_sources(SpectralFoundry PRIVATE Source/Plugin.cpp Source/Editor.cpp)
juce_add_binary_data(RackTables SOURCES Assets/table0.bin Assets/table1.bin Assets/table2.bin)
target_compile_definitions(SpectralFoundry PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(SpectralFoundry PRIVATE juce::juce_audio_utils juce::juce_dsp PUBLIC RackTables juce::juce_recommended_config_flags)
juce_add_console_app(FoundryTests PRODUCT_NAME "FoundryTests")
target_sources(FoundryTests PRIVATE Tests/Tests.cpp)
target_link_libraries(FoundryTests PRIVATE SpectralFoundry juce::juce_audio_utils juce::juce_dsp)
target_compile_definitions(FoundryTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
