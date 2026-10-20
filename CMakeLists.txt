cmake_minimum_required(VERSION 3.16)
project(ai_audio_engineer CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- Core calibration engine (no OBS dependency) ---------------------------
add_library(aiaudio_core STATIC
    src/core/AudioAnalyzer.cpp
    src/core/FFTAnalyzer.cpp
    src/core/TransientDetector.cpp
    src/core/CalibrationStage.cpp
    src/core/AudioProfile.cpp
    src/core/CalibrationSession.cpp
    src/core/OptimizationEngine.cpp
)
target_include_directories(aiaudio_core PUBLIC src/core)

# --- Unit tests --------------------------------------------------------------
enable_testing()
add_executable(aiaudio_tests
    tests/test_main.cpp
    tests/test_audio_analyzer.cpp
    tests/test_fft_analyzer.cpp
    tests/test_transient_detector.cpp
    tests/test_calibration_session.cpp
    tests/test_optimization_engine.cpp
)
target_include_directories(aiaudio_tests PRIVATE tests)
target_link_libraries(aiaudio_tests PRIVATE aiaudio_core)
add_test(NAME aiaudio_tests COMMAND aiaudio_tests)

# --- OBS plugin target (optional) --------------------------------------------
# Building the real OBS Studio filter plugin requires libobs headers/libs
# from an OBS Studio development installation (obs-studio/libobs). This
# target is only configured -- not required -- and needs
# AIAUDIO_LIBOBS_DIR pointed at a libobs include/lib root.
option(AIAUDIO_BUILD_OBS_PLUGIN "Build the OBS Studio filter plugin (requires libobs)" OFF)
if(AIAUDIO_BUILD_OBS_PLUGIN)
    find_path(LIBOBS_INCLUDE_DIR obs-module.h PATHS ${AIAUDIO_LIBOBS_DIR}/include ${AIAUDIO_LIBOBS_DIR})
    find_library(LIBOBS_LIBRARY NAMES obs libobs PATHS ${AIAUDIO_LIBOBS_DIR}/lib ${AIAUDIO_LIBOBS_DIR}/build/libobs)
    if(NOT LIBOBS_INCLUDE_DIR OR NOT LIBOBS_LIBRARY)
        message(FATAL_ERROR "AIAUDIO_BUILD_OBS_PLUGIN is ON but libobs was not found. Set AIAUDIO_LIBOBS_DIR to your OBS Studio dev SDK root.")
    endif()

    add_library(ai-audio-engineer MODULE
        src/obs-plugin/plugin-main.cpp
        src/obs-plugin/calibration-filter.cpp
    )
    target_include_directories(ai-audio-engineer PRIVATE ${LIBOBS_INCLUDE_DIR})
    target_link_libraries(ai-audio-engineer PRIVATE aiaudio_core ${LIBOBS_LIBRARY})
endif()
