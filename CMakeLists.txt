cmake_minimum_required(VERSION 3.20)

project(Mornox)

include(cmake/MornoxOptions.cmake)
include(cmake/MornoxThirdParty.cmake)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(MORNOX_BUILTIN_INCLUDE_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}/plugins/builtin/src
    ${CMAKE_CURRENT_SOURCE_DIR}/plugins/builtin/mornox.clice/src
    ${CMAKE_CURRENT_SOURCE_DIR}/plugins/builtin/mornox.cmake/src
    ${CMAKE_CURRENT_SOURCE_DIR}/plugins/builtin/mornox.cpp/src
)

file(GLOB_RECURSE MORNOX_CORE_SOURCES CONFIGURE_DEPENDS
    plugins/builtin/src/*.cpp
    plugins/builtin/*/src/*.cpp
    src/core/*.cpp
)

function(mornox_configure_core_target target_name)
    target_include_directories(${target_name}
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/include
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src
            ${CMAKE_CURRENT_SOURCE_DIR}/src/core
            ${MORNOX_BUILTIN_INCLUDE_DIRS}
    )

    target_compile_features(${target_name} PUBLIC cxx_std_20)
    target_link_libraries(${target_name} PUBLIC Mornox3p::NlohmannJson)
    target_compile_options(${target_name} PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/utf-8>
        $<$<CXX_COMPILER_ID:MSVC>:/FS>
    )

    if(APPLE)
        target_link_libraries(${target_name} PUBLIC "-framework CoreServices")
    endif()

    if(MORNOX_ENABLE_WARNINGS)
        target_compile_options(${target_name} PRIVATE
            $<$<CXX_COMPILER_ID:AppleClang,Clang,GNU>:-Wall -Wextra -Wpedantic>
            $<$<CXX_COMPILER_ID:MSVC>:/W4>
        )
    endif()
endfunction()

if(BUILD_MORNOX_STATIC)
    add_library(${MORNOX_CORE_STATIC_TARGET} STATIC ${MORNOX_CORE_SOURCES})
    mornox_configure_core_target(${MORNOX_CORE_STATIC_TARGET})
    set_target_properties(${MORNOX_CORE_STATIC_TARGET} PROPERTIES
        OUTPUT_NAME ${MORNOX_CORE_STATIC_OUTPUT_NAME}
    )
endif()

if(BUILD_MORNOX_SHARED)
    add_library(${MORNOX_CORE_SHARED_TARGET} SHARED ${MORNOX_CORE_SOURCES})
    mornox_configure_core_target(${MORNOX_CORE_SHARED_TARGET})
    set_target_properties(${MORNOX_CORE_SHARED_TARGET} PROPERTIES
        OUTPUT_NAME ${MORNOX_CORE_SHARED_OUTPUT_NAME}
    )
endif()

if(BUILD_MORNOX_STATIC)
    add_library(mornox_core ALIAS ${MORNOX_CORE_STATIC_TARGET})
else()
    add_library(mornox_core ALIAS ${MORNOX_CORE_SHARED_TARGET})
endif()

file(GLOB_RECURSE MORNOX_IDE_SOURCES CONFIGURE_DEPENDS
    src/ide/*.cpp
)
file(GLOB_RECURSE MORNOX_IDE_RESOURCES CONFIGURE_DEPENDS
    src/ide/*.qrc
)

if(BUILD_MORNOX_IDE)
    find_package(Qt6 REQUIRED COMPONENTS Svg Widgets)

    add_executable(mornox_ide ${MORNOX_IDE_SOURCES} ${MORNOX_IDE_RESOURCES})
    set_target_properties(mornox_ide PROPERTIES AUTORCC ON)
    target_link_libraries(mornox_ide PRIVATE mornox_core Qt6::Svg Qt6::Widgets)
    target_include_directories(mornox_ide
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}/src
            ${CMAKE_CURRENT_SOURCE_DIR}/src/core
            ${CMAKE_CURRENT_SOURCE_DIR}/src/ide
    )
    target_compile_features(mornox_ide PRIVATE cxx_std_20)
    target_compile_options(mornox_ide PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/utf-8>
        $<$<CXX_COMPILER_ID:MSVC>:/FS>
    )
endif()

file(GLOB_RECURSE MORNOX_CLI_SOURCES CONFIGURE_DEPENDS
    src/cli/*.cpp
)

add_executable(mornox_cli ${MORNOX_CLI_SOURCES})
target_link_libraries(mornox_cli PRIVATE mornox_core)
target_include_directories(mornox_cli PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core
    ${MORNOX_BUILTIN_INCLUDE_DIRS}
)
target_compile_options(mornox_cli PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/utf-8>
    $<$<CXX_COMPILER_ID:MSVC>:/FS>
)
set_target_properties(mornox_cli PROPERTIES OUTPUT_NAME mornox)
add_executable(mornox ALIAS mornox_cli)

if(BUILD_MORNOX_TESTS)
    enable_testing()
    file(GLOB_RECURSE MORNOX_TEST_SOURCES CONFIGURE_DEPENDS
        tests/*.cpp
    )
    file(GLOB_RECURSE MORNOX_IDE_TEST_SOURCES CONFIGURE_DEPENDS
        src/ide/ui/*.cpp
    )
    list(APPEND MORNOX_IDE_TEST_SOURCES
        src/ide/ui_service.cpp
    )

    add_executable(mornox_tests ${MORNOX_TEST_SOURCES} ${MORNOX_IDE_TEST_SOURCES})
    target_link_libraries(mornox_tests PRIVATE mornox_core Mornox3p::Catch2)
    target_include_directories(mornox_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/src/core
        ${CMAKE_CURRENT_SOURCE_DIR}/src/ide
        ${MORNOX_BUILTIN_INCLUDE_DIRS}
    )
    target_compile_options(mornox_tests PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/utf-8>
        $<$<CXX_COMPILER_ID:MSVC>:/FS>
    )
    add_test(NAME mornox_tests COMMAND mornox_tests)
endif()
