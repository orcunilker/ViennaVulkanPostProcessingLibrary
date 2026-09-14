include(CTest)

add_executable(testlib testlib.cpp)
target_link_libraries(testlib PUBLIC vvppl)

# measurement program, not a test
add_executable(bench bench.cpp)
target_link_libraries(bench PUBLIC vvppl)

add_test(NAME testlib COMMAND testlib)