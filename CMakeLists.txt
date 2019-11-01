cmake_minimum_required( VERSION 3.13 )

if( NOT CMAKE_BUILD_TYPE )
  set( CMAKE_BUILD_TYPE Release )
endif()

# Invoking "cmake . -DCMAKE_BUILD_TYPE=Release" will cause subsequent
# builds ("make") to use following CMAKE_C_FLAGS_RELEASE flag,
# while invoking "cmake . -DCMAKE_BUILD_TYPE=Debug" will cause
# subsequent builds to use the following CMAKE_C_FLAGS_DEBUG flag.
# Cmake creates Makefiles that call back into cmake, which in turn
# pickup and have make reuse the cached last set build type, until
# cmake is run again to change its cached setting.

set( CMAKE_C_FLAGS "-Wall -Wextra -Wpedantic" )
set( CMAKE_C_FLAGS_DEBUG "-g" )
set( CMAKE_C_FLAGS_RELEASE "-O3" )

message("CMAKE_C_FLAGS_DEBUG is ${CMAKE_C_FLAGS_DEBUG}")
message("CMAKE_C_FLAGS_RELEASE is ${CMAKE_C_FLAGS_RELEASE}")

project( rawscan
         VERSION 0.1
         DESCRIPTION "Read character (byte) terminated lines or records"
         LANGUAGES C )

add_library( rawscan SHARED )
target_sources( rawscan
                PRIVATE "lib/rawscan.c"
                PUBLIC "include/rawscan.h" )
target_include_directories( rawscan PUBLIC "include" )
target_compile_features( rawscan PRIVATE c_std_11 )

add_executable( rawscantest )
target_sources( rawscantest PRIVATE "src/rawscanstresstest.c" )
target_link_libraries( rawscantest PRIVATE rawscan )

add_executable( random_line_generator )
target_sources( random_line_generator PRIVATE "tests/random_line_generator.c" )
