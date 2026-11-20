cmake_minimum_required(VERSION 3.21)

project(CinemaSurroundPlayer VERSION 1.2 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

option(BUILD_PLAYER "Compiler le lecteur (nécessite Qt 6.8)" ON)
option(ENABLE_FFMPEG "Activer le moteur de décodage FFmpeg" ON)

include(CTest)

add_library(surround_core STATIC
    src/SurroundProcessor.h
    src/SurroundProcessor.cpp
)
target_include_directories(surround_core PUBLIC src)

if(MSVC)
    target_compile_options(surround_core PRIVATE /W4)
else()
    target_compile_options(surround_core PRIVATE -Wall -Wextra)
endif()

if(ENABLE_FFMPEG)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(FFMPEG QUIET IMPORTED_TARGET
            libavformat libavcodec libavutil libswresample)
    endif()

    if(TARGET PkgConfig::FFMPEG)
        set(FFMPEG_TARGET PkgConfig::FFMPEG)
    else()
        find_path(FFMPEG_INCLUDE_DIR libavformat/avformat.h REQUIRED)
        find_library(AVFORMAT_LIBRARY NAMES avformat PATHS "${FFMPEG_ROOT}/lib" REQUIRED)
        find_library(AVCODEC_LIBRARY NAMES avcodec PATHS "${FFMPEG_ROOT}/lib" REQUIRED)
        find_library(AVUTIL_LIBRARY NAMES avutil PATHS "${FFMPEG_ROOT}/lib" REQUIRED)
        find_library(SWRESAMPLE_LIBRARY NAMES swresample PATHS "${FFMPEG_ROOT}/lib" REQUIRED)
        add_library(ffmpeg_libs INTERFACE)
        target_include_directories(ffmpeg_libs INTERFACE ${FFMPEG_INCLUDE_DIR})
        target_link_libraries(ffmpeg_libs INTERFACE
            ${AVFORMAT_LIBRARY} ${AVCODEC_LIBRARY}
            ${SWRESAMPLE_LIBRARY} ${AVUTIL_LIBRARY})
        set(FFMPEG_TARGET ffmpeg_libs)
    endif()

    add_library(ffmpeg_audio STATIC
        src/FfmpegAudioDecoder.h
        src/FfmpegAudioDecoder.cpp)
    target_include_directories(ffmpeg_audio PUBLIC src)
    target_link_libraries(ffmpeg_audio PUBLIC ${FFMPEG_TARGET})
    target_compile_definitions(ffmpeg_audio PUBLIC CINEMA_HAS_FFMPEG=1)
endif()

if(BUILD_TESTING)
    add_executable(surround_tests tests/test_surround.cpp)
    target_link_libraries(surround_tests PRIVATE surround_core)
    add_test(NAME surround_tests COMMAND surround_tests)
endif()

if(BUILD_PLAYER)
    find_package(Qt6 6.8 REQUIRED COMPONENTS Core Widgets Multimedia MultimediaWidgets)
    qt_standard_project_setup()

    qt_add_executable(CinemaSurroundPlayer
        src/main.cpp
        src/ClickSlider.h
        src/MainWindow.h
        src/MainWindow.cpp
        src/SurroundOutput.h
        src/SurroundOutput.cpp
    )

    target_link_libraries(CinemaSurroundPlayer PRIVATE
        surround_core Qt6::Core Qt6::Widgets Qt6::Multimedia Qt6::MultimediaWidgets)
    if(ENABLE_FFMPEG)
        target_link_libraries(CinemaSurroundPlayer PRIVATE ffmpeg_audio)
        target_compile_definitions(CinemaSurroundPlayer PRIVATE CINEMA_HAS_FFMPEG=1)
    endif()

    if(MSVC)
        target_compile_options(CinemaSurroundPlayer PRIVATE /W4)
    else()
        target_compile_options(CinemaSurroundPlayer PRIVATE -Wall -Wextra)
    endif()

    if(WIN32)
        set_target_properties(CinemaSurroundPlayer PROPERTIES WIN32_EXECUTABLE TRUE)
    endif()
    install(TARGETS CinemaSurroundPlayer RUNTIME DESTINATION bin)
endif()
