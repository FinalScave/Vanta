option(BUILD_MORNOX_SHARED "Build Mornox shared libraries" OFF)
option(BUILD_MORNOX_STATIC "Build Mornox static libraries" ON)
option(BUILD_MORNOX_TESTS "Build Mornox tests" ON)
option(BUILD_MORNOX_IDE "Build Mornox Qt IDE" ON)
option(MORNOX_ENABLE_WARNINGS "Enable compiler warnings" ON)

if(NOT BUILD_MORNOX_SHARED AND NOT BUILD_MORNOX_STATIC)
    message(FATAL_ERROR "At least one of BUILD_MORNOX_SHARED or BUILD_MORNOX_STATIC must be ON")
endif()

set(MORNOX_CORE_SHARED_TARGET mornox_core_shared)
set(MORNOX_CORE_STATIC_TARGET mornox_core_static)
set(MORNOX_CORE_SHARED_OUTPUT_NAME mornox_core)
set(MORNOX_CORE_STATIC_OUTPUT_NAME mornox_core_static)
