cmake_minimum_required(VERSION 3.15)
project(MPSO LANGUAGES CXX)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

find_package(Threads REQUIRED)
set(MPSO_VOLEPSI_ROOT "/Users/mac-cy/House/MPSO_improve/MPSO/libvolepsi"
    CACHE PATH "Path to a local volePSI installation")
find_package(volePSI REQUIRED HINTS
    "${MPSO_VOLEPSI_ROOT}"
    "${CMAKE_CURRENT_SOURCE_DIR}/libvolepsi"
)

file(GLOB_RECURSE MPSO_CORE_SOURCES CONFIGURE_DEPENDS
    src/*.cpp
)

add_library(mpso_core STATIC ${MPSO_CORE_SOURCES})
target_compile_features(mpso_core PUBLIC cxx_std_17)
target_compile_options(mpso_core PRIVATE -w)
target_include_directories(mpso_core
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_link_libraries(mpso_core
    PUBLIC
        visa::volePSI
        Threads::Threads
        ${CMAKE_DL_LIBS}
)
set_target_properties(mpso_core PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/mpso_core"
)

function(add_mpso_test target source)
    add_executable(${target} ${source})
    target_compile_features(${target} PRIVATE cxx_std_17)
    target_compile_options(${target} PRIVATE -w)
    target_link_libraries(${target} PRIVATE mpso_core)
endfunction()

add_mpso_test(test_mpsi test/test_mpsi.cpp)
add_mpso_test(test_mpsic test/test_mpsic.cpp)
add_mpso_test(test_mpsics test/test_mpsics.cpp)
add_mpso_test(test_mpsu test/test_mpsu.cpp)
