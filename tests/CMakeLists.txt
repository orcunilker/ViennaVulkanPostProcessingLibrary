#project(MyLibTests LANGUAGES CXX)

include(CTest)

set(TARGET testlib)

set(SOURCE testlib.cpp)

set(GENERATED_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)
file(MAKE_DIRECTORY ${GENERATED_DIR})

set(HEADERS ${GENERATED_DIR}/invert_spv.h)

#slangc kommt aus dem vulkansdk
find_program(SLANGC slangc HINTS $ENV{VULKAN_SDK}/bin REQUIRED)

# uebersetzt invert.slang nach SPIR-V und bettet es als uint32-Array ein
add_custom_command(
    OUTPUT ${GENERATED_DIR}/invert_spv.h
    COMMAND ${SLANGC} ${PROJECT_SOURCE_DIR}/shaders/invert.slang
        -target spirv -profile spirv_1_3 -entry main -stage compute
        -source-embed-style u32 -source-embed-name invert_spv
        -o ${GENERATED_DIR}/invert_spv.h
    DEPENDS ${PROJECT_SOURCE_DIR}/shaders/invert.slang
    COMMENT "slangc invert.slang"
)

add_executable(${TARGET} ${SOURCE} ${HEADERS})

target_compile_features(${TARGET} PUBLIC cxx_std_20)

target_include_directories(${TARGET} PUBLIC ${PROJECT_SOURCE_DIR}/include ${GENERATED_DIR})

target_link_libraries(${TARGET} PUBLIC Vulkan::Vulkan)

add_test(NAME ${TARGET} COMMAND testlib) # Command can be a target
