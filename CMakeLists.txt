cmake_minimum_required(VERSION 3.16.0)

project(gamutplotter VERSION 0.2.1 LANGUAGES CXX)

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

# only set for static Qt build
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -static")
# set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -static-libgcc -static-libstdc++ -ggdb")

LIST (APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
LIST (APPEND CMAKE_PREFIX_PATH "${CMAKE_CURRENT_BINARY_DIR}/i")

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets Concurrent Gui OpenGLWidgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Concurrent Gui OpenGLWidgets)

include(FeatureSummary)
include(MacroBoolTo01)

set(PROJECT_SOURCES
        src/main.cpp
        src/mainwindow.cpp
        src/mainwindow.h
        src/mainwindow.ui
        src/scatterdialog.h
        src/scatterdialog.cpp
        src/scatterdialog.ui
        src/scatter2dchart.h
        src/scatter2dchart.cpp
        src/imageparsersc.h
        src/imageparsersc.cpp
        src/imageformats.h
        src/constant_dataset.h
        src/plot_typedefs.h
        src/global_variables.h
        src/custom3dchart.h
        src/custom3dchart.cpp
        src/shaders_gl.h
        src/helper_funcs.h
        src/camera3dsettingdialog.h
        src/camera3dsettingdialog.cpp
        src/camera3dsettingdialog.ui
)

# move 3rd party into separate project

#add_subdirectory("3rdparty")
#add_dependencies(gamutplotter ext_patch)
#add_dependencies(gamutplotter ext_lcms2)
#add_dependencies(gamutplotter ext_jpegxl)

find_package(LCMS2 2.13 REQUIRED)
find_package(JPEGXL 0.7.0)
# find_package(OpenGL REQUIRED)
# include_directories(${OPENGL_INCLUDE_DIRS})

macro_bool_to_01(JPEGXL_FOUND HAVE_JPEGXL)

if (JPEGXL_FOUND)
    list(APPEND PROJECT_SOURCES
        src/jxlreader.h
        src/jxlreader.cpp
        src/jxlwriter.h
        src/jxlwriter.cpp)
endif()

configure_file(src/gamutplotterconfig.h.cmake ${CMAKE_CURRENT_BINARY_DIR}/gamutplotterconfig.h)

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
target_link_libraries(gamutplotter PRIVATE Qt${QT_VERSION_MAJOR}::Concurrent)
target_link_libraries(gamutplotter PRIVATE Qt${QT_VERSION_MAJOR}::Gui)
target_link_libraries(gamutplotter PRIVATE Qt${QT_VERSION_MAJOR}::OpenGLWidgets)

set_target_properties(gamutplotter PROPERTIES
    MACOSX_BUNDLE_GUI_IDENTIFIER saklistudio.com
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
)

set(TOP_INST_DIR ${CMAKE_CURRENT_BINARY_DIR}/i)
set(EXTPREFIX "${TOP_INST_DIR}")
set(CMAKE_PREFIX_PATH "${EXTPREFIX}")

include_directories(${LCMS2_INCLUDE_DIRS})
target_link_libraries(gamutplotter PRIVATE ${LCMS2_LIBRARIES})

if(JPEGXL_FOUND)
    include_directories(${JPEGXL_INCLUDE_DIRS})
    target_link_libraries(gamutplotter PRIVATE ${JPEGXL_LIBRARIES})
endif()

target_link_libraries(gamutplotter PRIVATE ${OPENGL_LIBRARIES})

install(TARGETS gamutplotter
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(gamutplotter)
endif()
