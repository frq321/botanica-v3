cmake_minimum_required(VERSION 3.22)
project(Botanica VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE: use a local checkout with -DBOTANICA_JUCE_PATH=/path/to/JUCE, otherwise JUCE 8.0.8 is downloaded.
set(BOTANICA_JUCE_PATH "" CACHE PATH "Path to a local JUCE checkout (optional)")
if(BOTANICA_JUCE_PATH)
    add_subdirectory(${BOTANICA_JUCE_PATH} JUCE EXCLUDE_FROM_ALL)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.8
        GIT_SHALLOW ON)
    FetchContent_MakeAvailable(JUCE)
endif()

juce_add_binary_data(BotanicaData
    HEADER_NAME BotanicaData.h
    NAMESPACE BotanicaData
    SOURCES piano.pak nature.pak inst.pak)
set_target_properties(BotanicaData PROPERTIES POSITION_INDEPENDENT_CODE ON)

juce_add_plugin(Botanica
    COMPANY_NAME "Botanica"
    PLUGIN_MANUFACTURER_CODE Btnx
    PLUGIN_CODE BtnA
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    FORMATS VST3 Standalone
    VST3_CATEGORIES Instrument Synth
    PRODUCT_NAME "Botanica")

target_sources(Botanica PRIVATE
    BotanicaProcessor.cpp BotanicaProcessor.h
    BotanicaEditor.cpp BotanicaEditor.h
    BotanicaEngine.h BotanicaSamples.h BotanicaImport.h)

target_include_directories(Botanica PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_definitions(Botanica PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_USE_OGGVORBIS=1)

target_link_libraries(Botanica
    PRIVATE BotanicaData juce::juce_audio_utils juce::juce_audio_formats
    PUBLIC  juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

option(BOTANICA_BUILD_TESTS "Build the offline DSP tests" OFF)
if(BOTANICA_BUILD_TESTS)
    enable_testing()
    juce_add_console_app(BotanicaTests PRODUCT_NAME "BotanicaTests")
    target_sources(BotanicaTests PRIVATE BotanicaTests.cpp BotanicaEngine.h BotanicaSamples.h BotanicaImport.h)
    target_include_directories(BotanicaTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(BotanicaTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_USE_OGGVORBIS=1)
    target_link_libraries(BotanicaTests PRIVATE juce::juce_audio_formats juce::juce_audio_basics juce::juce_core
                          PUBLIC juce::juce_recommended_config_flags)
    add_test(NAME BotanicaDsp COMMAND BotanicaTests WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
endif()

option(BOTANICA_BUILD_SNAPSHOT "Build the plugin check / screenshot tool" OFF)
if(BOTANICA_BUILD_SNAPSHOT)
    juce_add_console_app(BotanicaSnapshot PRODUCT_NAME "BotanicaSnapshot")
    target_sources(BotanicaSnapshot PRIVATE BotanicaSnapshot.cpp BotanicaProcessor.cpp BotanicaEditor.cpp)
    target_include_directories(BotanicaSnapshot PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(BotanicaSnapshot PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_USE_OGGVORBIS=1)
    target_link_libraries(BotanicaSnapshot PRIVATE BotanicaData juce::juce_audio_utils juce::juce_audio_formats
                          PUBLIC juce::juce_recommended_config_flags)
endif()
