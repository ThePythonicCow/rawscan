cmake_minimum_required(VERSION 3.13)

# Intention: Use "Semantic Versioning", as documented at https://semver.org/
set(RAWSCAN_VERSION 0.1.4)

project(
    rawscan
    VERSION "${RAWSCAN_VERSION}"
    DESCRIPTION "Read character (byte) terminated lines or records"
    LANGUAGES C
)

# To set future build type to "Release", in build directory invoke:
#       cmake .. -DCMAKE_BUILD_TYPE=Release
# To set future build type to "Debug", in build directory invoke:
#       cmake .. -DCMAKE_BUILD_TYPE=Debug
# Once build type is set, it is remembered in cmake's cache
# for future builds, until set again.

set(DEFAULT_BUILD_TYPE "Release")

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  message(STATUS "Setting '-DCMAKE_BUILD_TYPE=' build type to '${DEFAULT_BUILD_TYPE}' as none was specified.")
  set(CMAKE_BUILD_TYPE "${DEFAULT_BUILD_TYPE}" CACHE STRING "Choose the type of build." FORCE)
  # Set the possible values of build type for cmake-gui
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()

include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    RawscanConfigVersion.cmake
    VERSION "${RAWSCAN_VERSION}"
    COMPATIBILITY SameMajorVersion
)

set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

add_library(rawscan SHARED)

target_sources(
    rawscan
    PRIVATE lib/rawscan.c
    PUBLIC include/rawscan.h
)
target_include_directories(rawscan PUBLIC include)

# need >= C11 for anon union in rawscan.h
target_compile_features(rawscan PUBLIC c_std_11)

target_compile_options(rawscan PRIVATE
    $<$<OR:$<C_COMPILER_ID:Clang>,$<C_COMPILER_ID:GNU>>:
            -pipe -march=native
            $<$<CONFIG:Debug>:-O0 -Wall -Wextra -Wpedantic>
            $<$<CONFIG:Release>:-O3>>

    $<$<C_COMPILER_ID:MSVC>:$<$<CONFIG:Debug>:/Od /Wall /Zi>>
)
add_subdirectory(tests)

include(GNUInstallDirs)

install(
    TARGETS rawscan
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        COMPONENT           Rawscan_RunTime
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        COMPONENT           Rawscan_RunTime
        NAMELINK_COMPONENT  Rawscan_Development
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        COMPONENT           Rawscan_Development
)

INSTALL(
    FILES
        include/rawscan.h
        build/RawscanConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_PREFIX}/usr/include
)
