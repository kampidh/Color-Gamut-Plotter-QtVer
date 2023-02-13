#
# Some 3rd party dependencies are borrowed
# from Krita deps build
#

include (ExternalProject)

set(PATCH_COMMAND git apply)

add_subdirectory(ext_lcms2)
