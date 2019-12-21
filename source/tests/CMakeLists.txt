cmake_minimum_required(VERSION 3.13)
add_executable(rawscan_test)
target_sources(rawscan_test PRIVATE rawscan_test.c)
target_link_libraries(rawscan_test PRIVATE rawscan)
target_include_directories(rawscan_test PRIVATE ${CMAKE_SOURCE_DIR}/include)

add_executable(rawscan_static_test)
target_sources(rawscan_static_test PRIVATE rawscan_static_test.c)
target_include_directories(rawscan_static_test PRIVATE ${CMAKE_SOURCE_DIR}/include)

add_executable(fgets_test)
target_sources(fgets_test PRIVATE fgets_test.c)

add_executable(getline_test)
target_sources(getline_test PRIVATE getline_test.c)

add_executable(random_line_generator)
target_sources(random_line_generator PRIVATE random_line_generator.c)

configure_file(compare_various_apis.sh compare_various_apis COPYONLY)
configure_file(summarize_results.sh summarize_results COPYONLY)
configure_file(regression_stress_test.sh regression_stress_test COPYONLY)
configure_file(python2_test python2_test COPYONLY)
configure_file(python3_test python3_test COPYONLY)
