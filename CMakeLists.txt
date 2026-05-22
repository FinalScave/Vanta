cmake_minimum_required(VERSION 3.20)

project(Vanta)

include(cmake/VantaOptions.cmake)
include(cmake/VantaThirdParty.cmake)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(VANTA_BUILTIN_INCLUDE_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}/plugins/builtin/src
    ${CMAKE_CURRENT_SOURCE_DIR}/plugins/builtin/vanta.clice/src
    ${CMAKE_CURRENT_SOURCE_DIR}/plugins/builtin/vanta.cmake/src
    ${CMAKE_CURRENT_SOURCE_DIR}/plugins/builtin/vanta.cpp/src
)

file(GLOB_RECURSE VANTA_CORE_SOURCES CONFIGURE_DEPENDS
    plugins/builtin/src/*.cpp
    plugins/builtin/*/src/*.cpp
    src/core/*.cpp
)

function(vanta_configure_core_target target_name)
    target_include_directories(${target_name}
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/include
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src
            ${CMAKE_CURRENT_SOURCE_DIR}/src/core
            ${VANTA_BUILTIN_INCLUDE_DIRS}
    )

    target_compile_features(${target_name} PUBLIC cxx_std_20)
    target_link_libraries(${target_name} PUBLIC Vanta3p::NlohmannJson)

    if(APPLE)
        target_link_libraries(${target_name} PUBLIC "-framework CoreServices")
    endif()

    if(VANTA_ENABLE_WARNINGS)
        target_compile_options(${target_name} PRIVATE
            $<$<CXX_COMPILER_ID:AppleClang,Clang,GNU>:-Wall -Wextra -Wpedantic>
            $<$<CXX_COMPILER_ID:MSVC>:/W4>
        )
    endif()
endfunction()

if(BUILD_VANTA_STATIC)
    add_library(${VANTA_CORE_STATIC_TARGET} STATIC ${VANTA_CORE_SOURCES})
    vanta_configure_core_target(${VANTA_CORE_STATIC_TARGET})
    set_target_properties(${VANTA_CORE_STATIC_TARGET} PROPERTIES
        OUTPUT_NAME ${VANTA_CORE_STATIC_OUTPUT_NAME}
    )
endif()

if(BUILD_VANTA_SHARED)
    add_library(${VANTA_CORE_SHARED_TARGET} SHARED ${VANTA_CORE_SOURCES})
    vanta_configure_core_target(${VANTA_CORE_SHARED_TARGET})
    set_target_properties(${VANTA_CORE_SHARED_TARGET} PROPERTIES
        OUTPUT_NAME ${VANTA_CORE_SHARED_OUTPUT_NAME}
    )
endif()

if(BUILD_VANTA_STATIC)
    add_library(vanta_core ALIAS ${VANTA_CORE_STATIC_TARGET})
else()
    add_library(vanta_core ALIAS ${VANTA_CORE_SHARED_TARGET})
endif()

file(GLOB_RECURSE VANTA_IDE_SOURCES CONFIGURE_DEPENDS
    src/ide/*.cpp
)

add_library(vanta_ide ${VANTA_IDE_SOURCES})

target_link_libraries(vanta_ide PUBLIC vanta_core)
target_include_directories(vanta_ide
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/src/core
        ${CMAKE_CURRENT_SOURCE_DIR}/src/ide
)
target_compile_features(vanta_ide PUBLIC cxx_std_20)

file(GLOB_RECURSE VANTA_CLI_SOURCES CONFIGURE_DEPENDS
    src/cli/*.cpp
)

add_executable(vanta_cli ${VANTA_CLI_SOURCES})
target_link_libraries(vanta_cli PRIVATE vanta_core)
target_include_directories(vanta_cli PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core
    ${VANTA_BUILTIN_INCLUDE_DIRS}
)
set_target_properties(vanta_cli PROPERTIES OUTPUT_NAME vanta)
add_executable(vanta ALIAS vanta_cli)

if(BUILD_VANTA_TESTS)
    enable_testing()
    file(GLOB_RECURSE VANTA_TEST_SOURCES CONFIGURE_DEPENDS
        tests/*.cpp
    )
    add_executable(vanta_tests ${VANTA_TEST_SOURCES})
    target_link_libraries(vanta_tests PRIVATE vanta_ide Vanta3p::Catch2)
    target_include_directories(vanta_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/src/core
        ${CMAKE_CURRENT_SOURCE_DIR}/src/ide
        ${VANTA_BUILTIN_INCLUDE_DIRS}
    )
    add_test(NAME vanta_tests COMMAND vanta_tests)
endif()
