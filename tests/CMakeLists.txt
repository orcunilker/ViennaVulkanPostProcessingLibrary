include(CTest)

# headless test: applies all effects to a red image and writes output.ppm
add_executable(testlib testlib.cpp)
target_link_libraries(testlib PUBLIC vvppl)

# headless test: zero, one and two effects on a 2x2 sRGB image, compared with values calculated on the CPU
add_executable(pixeltest pixeltest.cpp)
target_link_libraries(pixeltest PUBLIC vvppl)

# headless test: constructors that fail halfway, the validation layer reports every object left behind
add_executable(cleanuptest cleanuptest.cpp)
target_link_libraries(cleanuptest PUBLIC vvppl)

# measurement program, not a test
add_executable(bench bench.cpp)
target_link_libraries(bench PUBLIC vvppl)

add_test(NAME testlib COMMAND testlib)
add_test(NAME pixeltest COMMAND pixeltest)
add_test(NAME cleanuptest COMMAND cleanuptest)

# a test also fails if the validation layer reports an error
set_tests_properties(testlib pixeltest cleanuptest PROPERTIES FAIL_REGULAR_EXPRESSION "Validation Error")
