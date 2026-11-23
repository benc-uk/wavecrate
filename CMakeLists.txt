cmake_minimum_required(VERSION 3.24)

project(WaveCrate VERSION 0.1.0 LANGUAGES C CXX)

if(NOT WIN32 OR NOT MSVC)
    message(FATAL_ERROR "WaveCrate currently requires Windows and the MSVC toolchain.")
endif()

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "Use an x64 MSVC developer environment to build WaveCrate.")
endif()

set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

include(FetchContent)

# JUCE 9.0.5, pinned to an immutable source commit.
FetchContent_Declare(juce
    URL https://github.com/juce-framework/JUCE/archive/d60b7a78030661574a615b586d9325ba7a7fa7dd.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP FALSE
)
FetchContent_MakeAvailable(juce)

# Compile the JUCE modules once for the app and its hardware-independent tests.
add_library(WaveCrateCore STATIC
    Source/AudioPreview.h
    Source/AudioPreview.cpp
    Source/SampleBrowser.h
    Source/SampleDetails.h
    Source/SampleBrowser.cpp
    Source/DesktopLayout.h
    Source/DesktopLayout.cpp
    Source/WaveformData.h
    Source/WaveformData.cpp
    Source/WaveformView.h
    Source/WaveformView.cpp
)

target_compile_features(WaveCrateCore PUBLIC cxx_std_20)
target_include_directories(WaveCrateCore PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Source")
target_include_directories(WaveCrateCore SYSTEM PUBLIC "${juce_SOURCE_DIR}/modules")

target_compile_definitions(WaveCrateCore PUBLIC
    JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1
    JUCE_STANDALONE_APPLICATION=1
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_USE_FLAC=0
    JUCE_USE_OGGVORBIS=0
    JUCE_USE_WINDOWS_MEDIA_FORMAT=0
    JUCE_USE_WINDOWS_MIDI_SERVICES=0
)

target_link_libraries(WaveCrateCore PRIVATE
    juce::juce_gui_basics
    juce::juce_audio_devices
    juce::juce_audio_formats
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)

juce_add_gui_app(WaveCrate
    PRODUCT_NAME "WaveCrate"
    COMPANY_NAME "WaveCrate"
    NEEDS_WEB_BROWSER FALSE
    NEEDS_CURL FALSE
)

target_sources(WaveCrate PRIVATE
    Source/Main.cpp
    Source/MainComponent.h
    Source/MainComponent.cpp
)

target_compile_features(WaveCrate PRIVATE cxx_std_20)

target_compile_definitions(WaveCrate PRIVATE
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:WaveCrate,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:WaveCrate,JUCE_VERSION>"
)

target_link_libraries(WaveCrate PRIVATE
    WaveCrateCore
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags
)

include(CTest)

if(BUILD_TESTING)
    add_executable(WaveCrateTests
        Tests/Main.cpp
        Tests/AudioDeviceSmoke.cpp
        Tests/PreviewCommandsTests.cpp
        Tests/SampleBrowserTests.cpp
        Tests/FolderNavigationTests.cpp
        Tests/AudioPreviewTests.cpp
        Tests/WaveformDataTests.cpp
        Tests/WaveformNavigationTests.cpp
    )
    target_link_libraries(WaveCrateTests PRIVATE
        WaveCrateCore
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
    )
    add_test(NAME WaveCrate.M1 COMMAND WaveCrateTests)
    set_tests_properties(WaveCrate.M1 PROPERTIES TIMEOUT 90)
endif()
