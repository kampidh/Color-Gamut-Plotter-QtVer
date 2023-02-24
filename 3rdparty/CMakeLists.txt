#
# Some 3rd party dependencies are borrowed
# from Krita deps build
#
# JPEG XL build is kinda broken on Qt's MingW toolkit
# So I build it separately with gcc llvm 15
#

project(gamutplotter-deps)

if (POLICY CMP0002)
    cmake_policy(SET CMP0002 NEW)
endif()

if (POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -static")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -static")

set(TOP_INST_DIR ${CMAKE_CURRENT_BINARY_DIR}/i)
set(EXTPREFIX "${TOP_INST_DIR}")
set(CMAKE_PREFIX_PATH "${EXTPREFIX}")

include (ExternalProject)

message( STATUS "CMAKE_GENERATOR: ${CMAKE_GENERATOR}")
message( STATUS "CMAKE_CL_64: ${CMAKE_CL_64}")
message("CMAKE BUILD TYPE: ${CMAKE_BUILD_TYPE}")

set(GLOBAL_BUILD_TYPE ${CMAKE_BUILD_TYPE})
set(GLOBAL_PROFILE ${GLOBAL_PROFILE} -DBUILD_TESTING=false)


set(PATCH_COMMAND "${EXTPREFIX}/bin/myptch")

add_subdirectory(ext_patch)
add_subdirectory(ext_lcms2)
add_subdirectory(ext_jpegxl)
