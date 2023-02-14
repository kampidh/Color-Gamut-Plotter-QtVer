cmake_minimum_required(VERSION 3.16.0)

project(gamutplotter VERSION 0.1 LANGUAGES CXX)

if (POLICY CMP0002)
    cmake_policy(SET CMP0002 NEW)
endif()

if (POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

LIST (APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets DataVisualization Charts Concurrent)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets DataVisualization Charts Concurrent)

include(FeatureSummary)

set(PROJECT_SOURCES
        main.cpp
        mainwindow.cpp
        mainwindow.h
        mainwindow.ui
        scatter3dchart.h
        scatter3dchart.cpp
        scatterdialog.h
        scatterdialog.cpp
        scatter2dchart.h
        scatter2dchart.cpp
        imageparsersc.h
        imageparsersc.cpp
)

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(gamutplotter
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
    )
# Define target properties for Android with Qt 6 as:
#    set_property(TARGET gamutplotter APPEND PROPERTY QT_ANDROID_PACKAGE_SOURCE_DIR
#                 ${CMAKE_CURRENT_SOURCE_DIR}/android)
# For more information, see https://doc.qt.io/qt-6/qt-add-executable.html#target-creation
else()
    if(ANDROID)
        add_library(gamutplotter SHARED
            ${PROJECT_SOURCES}
        )
# Define properties for Android with Qt 5 after find_package() calls as:
#    set(ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android")
    else()
        add_executable(gamutplotter
            ${PROJECT_SOURCES}
        )
    endif()
endif()

target_link_libraries(gamutplotter PRIVATE Qt${QT_VERSION_MAJOR}::Widgets)
target_link_libraries(gamutplotter PRIVATE Qt${QT_VERSION_MAJOR}::DataVisualization)
target_link_libraries(gamutplotter PRIVATE Qt${QT_VERSION_MAJOR}::Charts)
target_link_libraries(gamutplotter PRIVATE Qt${QT_VERSION_MAJOR}::Concurrent)

set_target_properties(gamutplotter PROPERTIES
    MACOSX_BUNDLE_GUI_IDENTIFIER my.example.com
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

message("CMAKE COMMAND:")
message(${CMAKE_COMMAND})

set(TOP_INST_DIR ${CMAKE_CURRENT_BINARY_DIR}/i)
set(EXTPREFIX "${TOP_INST_DIR}")
set(CMAKE_PREFIX_PATH "${EXTPREFIX}")

add_subdirectory("3rdparty")

add_dependencies(gamutplotter ext_lcms2)

find_package(LCMS2 2.13)
include_directories(${LCMS2_INCLUDE_DIRS})
target_link_libraries(gamutplotter PRIVATE ${LCMS2_LIBRARIES})

install(TARGETS gamutplotter
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(gamutplotter)
endif()
