cmake_minimum_required(VERSION 3.21)

project(CinemaSurroundPlayer VERSION 1.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

option(BUILD_PLAYER "Compiler le lecteur (nécessite Qt 6.8)" ON)

include(CTest)

# Traitement audio pur C++ : aucune dépendance à Qt, donc testable seul.
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

if(BUILD_TESTING)
    add_executable(surround_tests tests/test_surround.cpp)
    target_link_libraries(surround_tests PRIVATE surround_core)
    add_test(NAME surround_tests COMMAND surround_tests)
endif()

if(BUILD_PLAYER)
    # QAudioBufferOutput (accès à l'audio décodé) existe à partir de Qt 6.8.
    find_package(Qt6 6.8 REQUIRED COMPONENTS
        Core
        Widgets
        Multimedia
        MultimediaWidgets
    )

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
        surround_core
        Qt6::Core
        Qt6::Widgets
        Qt6::Multimedia
        Qt6::MultimediaWidgets
    )

    if(MSVC)
        target_compile_options(CinemaSurroundPlayer PRIVATE /W4)
    else()
        target_compile_options(CinemaSurroundPlayer PRIVATE -Wall -Wextra)
    endif()

    if(WIN32)
        set_target_properties(CinemaSurroundPlayer PROPERTIES
            WIN32_EXECUTABLE TRUE
        )
    endif()

    install(TARGETS CinemaSurroundPlayer RUNTIME DESTINATION bin)
endif()
