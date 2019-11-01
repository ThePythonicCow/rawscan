cmake_minimum_required( VERSION 3.13 )

project( rawscan
         VERSION 1.0
         DESCRIPTION "Read character (byte) terminated lines or records"
         LANGUAGES C )

add_library( librawscan SHARED )
target_sources( librawscan
                PRIVATE "lib/rawscan.c"
                PUBLIC "include/rawscan.h" )
target_include_directories( librawscan PUBLIC "include" )
target_compile_features( librawscan PRIVATE c_std_11 )

add_executable( rawscantest )
target_sources( rawscantest PRIVATE "src/rawscanstresstest.c" )
target_link_libraries( rawscantest PRIVATE librawscan )

add_executable( random_line_generator )
target_sources( random_line_generator PRIVATE "tests/random_line_generator.c" )
