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
