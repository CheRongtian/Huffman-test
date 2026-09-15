cmake_minimum_required(VERSION 3.16)
project(HuffmanTree VERSION 1.1 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

function(mgz_enable_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

add_library(mgz_core STATIC
    bitstream.c
    huff.c
    lz77.c
    checksum.c
    format.c
)
target_include_directories(mgz_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
mgz_enable_warnings(mgz_core)

add_executable(huff main.c)
target_link_libraries(huff PRIVATE mgz_core)
mgz_enable_warnings(huff)

option(MGZ_BUILD_GUI "Build the Qt 6 Widgets desktop application" ON)

if(MGZ_BUILD_GUI)
    find_package(Qt6 6.2 QUIET COMPONENTS Widgets)

    if(Qt6_FOUND)
        set(CMAKE_AUTOMOC ON)

        qt_add_executable(mgz_gui WIN32 MACOSX_BUNDLE
            gui/main.cpp
            gui/MainWindow.cpp
            gui/MainWindow.h
            gui/DropArea.cpp
            gui/DropArea.h
            gui/LightningOverlay.cpp
            gui/LightningOverlay.h
            gui/CompressionWorker.cpp
            gui/CompressionWorker.h
        )
        target_include_directories(mgz_gui PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/gui
        )
        target_link_libraries(mgz_gui PRIVATE mgz_core Qt6::Widgets)
        mgz_enable_warnings(mgz_gui)

        set_target_properties(mgz_gui PROPERTIES
            OUTPUT_NAME "MGZ Compressor"
            MACOSX_BUNDLE_BUNDLE_NAME "MGZ Compressor"
            MACOSX_BUNDLE_GUI_IDENTIFIER "dev.huffmantree.mgz-compressor"
            MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
            MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
        )
    else()
        message(WARNING
            "Qt 6 Widgets was not found. The huff CLI will be built without "
            "the MGZ Compressor desktop application."
        )
    endif()
endif()
