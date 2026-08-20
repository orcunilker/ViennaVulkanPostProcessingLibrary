include(CTest)

add_executable(testlib testlib.cpp)
target_link_libraries(testlib PUBLIC vvppl)

add_test(NAME testlib COMMAND testlib)